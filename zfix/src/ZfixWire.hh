//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Complete FIX frame admission and transport-record completion

#ifndef ZfixWire_HH
#define ZfixWire_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZiAssert.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixTx.hh>
#include <zlib/ZfixStats.hh>

namespace Zfix {

namespace WireMode { enum { Generic, Pending, Records }; }

// All output buffers used by the underlying link, including TLS handshake
// buffers, derive from this node. Zero token marks transport-owned traffic.
struct WireBuf : public ZiTxBuf {
  using ZiIOBuf::data;
  uint64_t		token = 0;
  unsigned		part = 0;

  WireBuf(uint8_t *data = nullptr, unsigned size = 0, void *owner = nullptr, unsigned length = 0) :
    ZiTxBuf{data, size, owner, length} { }
};

template <unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Zfix.WireBuf">
using WireBufAlloc = Zi::IOBufAlloc<WireBuf, Size, MaxSize, ZuStringT<HeapID>>;

// Zero size submits the original TCP range. Otherwise size/head/tail describe
// one TLS record, and memory reserves its measured descriptor/backing cost
// including ordinary fallback. Idle pool mappings are not per-frame charges.
struct WireGeometry {
  unsigned	size = 0;
  unsigned	head = 0;
  unsigned	tail = 0;
  uint64_t	memory = 0;

  bool valid() const {
    return !size ? !head && !tail :
      head < size && tail < size - head && memory >= size;
  }
};

struct WireLimits {
  uint64_t	queuedBytes = TxLimits{}.queuedOutputBytes;
  uint64_t	adminBytes = TxLimits{}.adminReserveBytes;
  unsigned	records = TxLimits{}.replayBatch;
};

struct WireState;

// These fields are touched only by physical I/O Tx after publication. The
// protocol owner retains its own publication/completion counters separately.
struct WireIO {
  void *sender = nullptr;
  ZmAnyFn parent;
  ZmRef<ZiConnection> connection; // immutable physical association through I/O completion
  WireState *state = nullptr;
  bool selecting = false;
  unsigned produced = 0, retired = 0;
};
struct WireFrame : public ZmPolymorph {
  TxBody::Ref		body;
  TxContext		context;
  TxImage		frame;
  uint64_t		token = 0;
  uint64_t		charge = 0;
  unsigned		parts = 0;
  unsigned		completed = 0;
  unsigned		produced = 0;
  unsigned		offset = 0;
  bool			direct = false;
  bool			published = false;
  void			*owner = nullptr;
  WireIO		io;
  bool (*produce)(WireFrame *, ZiTxContext &) = nullptr;
  void (*cancel)(WireFrame *) = nullptr;
  void (*ack)(WireFrame *) = nullptr;
  static constexpr uint64_t Closed = uint64_t(1)<<63;
  ZmAtomic<uint64_t> posts{0}; // fixed completion callbacks, no source arbitration

  bool terminal = false, terminalOK = false, retiring = false; // App Tx only
  ZmAtomic<unsigned> ended{0}; // App/IO end-of-payload handshake; second clears it

  bool canceled() const { return uint64_t(posts) & Closed; }
  uint64_t pending() const { return uint64_t(posts) & ~Closed; }
  bool post() {
    uint64_t old = posts;
    for (;;) {
      if (old & Closed) return false;
      uint64_t found = posts.cmpXch(old + 1, old);
      if (found == old) return true;
      old = found;
    }
  }

  void end_(unsigned side) {
    unsigned prior = ended.xchOr(side);
    if (!(prior & side) && (prior | side) == 3) {
      // No actor or backend callback reads payload after its end publication.
      body = {}; frame = {};
      // Both actors ended payload use. Later kernel retirement uses
      // its own retained backing and fixed results, never these capabilities.
      io.parent = {}; io.connection = nullptr; io.sender = nullptr; io.state = nullptr;
    }
  }

  WireFrame(TxBody::Ref body_, TxContext context_, TxImage frame_,
      uint64_t token_, uint64_t charge_, unsigned parts_, bool direct_,
      void *owner_, bool (*produce_)(WireFrame *, ZiTxContext &),
      void (*cancel_)(WireFrame *), void (*ack_)(WireFrame *)) :
    body{ZuMv(body_)}, context{context_}, frame{ZuMv(frame_)}, token{token_},
    charge{charge_}, parts{parts_}, direct{direct_}, owner{owner_},
    produce{produce_}, cancel{cancel_}, ack{ack_} { }
};

// One allocation owns two independent intrusive positions: App Tx admission
// and physical I/O Tx pending output. Only the respective owner touches links.
using WirePending = ZmList<WireFrame,
  ZmListNode<WireFrame, ZmListHeapID<"">>>;
using WireQueue = ZmList<WirePending::Node,
  ZmListNode<WirePending::Node, ZmListHeapID<"Zfix.WireQueue">>>;

// Same existing I/O links move between physical Pending and Active positions.
// Protocol admissions retain their independent physical I/O queue positions.
struct WireState {
  WirePending active;
  WirePending *pending = nullptr;
};

struct WireOps {
  using Queue = WirePending;
  using Node = Queue::Node;
  using Ref = Queue::NodeRef;
  using State = WireState;

  template <typename Impl> static void owner(Node *node, Impl *impl) {
    node->io.sender = impl;
  }
  template <typename Impl> static Impl *owner(const Node *node) {
    return static_cast<Impl *>(node->io.sender);
  }
  template <typename Impl>
  static bool prepare(Impl *, Node *node, ZiConnection *cxn) {
    if (node->canceled()) return false;
    node->io.connection = cxn;
    return true;
  }
  template <typename Impl> static void queued(Impl *impl, Node *node, ZiTxContext &io) {
    auto &state = impl->txState_();
    state.pending = &impl->txQueue;
    node->io.state = &state;
    ZmAssert(node->io.connection.ptr() == io.cxn);
  }
  template <typename Impl>
  static void select(Impl *impl, Ref node, ZiTxContext &io) {
    auto &state = impl->txState_();
    while (node) {
      if (node->canceled()) {
	aborted(impl, ZuMv(node), true); node = state.pending->shift(); continue;
      }
      if (node->io.connection.ptr() != io.cxn) {
	state.pending->unshiftNode(node.ptr()); io.complete(); return;
      }
      state.active.pushNode(node);
      node->io.selecting = true;
      node->produce(node.ptr(), io);
      return;
    }
    io.complete();
  }
  static void inactive(WireFrame *frame) {
    auto node = static_cast<WireQueue::Node *>(frame);
    frame->io.state->active.delNode(static_cast<Node *>(node));
  }
  template <typename Impl> static void retired(Impl *, ZiTxContext &io) {
    ZiAssert(!io.buf, "Zfix", (), "frame retirement escaped producer", io.complete());
  }
  template <typename Impl> static void aborted(Impl *, Ref node, bool removed) {
    if (removed) abort_(ZuMv(node));
  }
  template <typename Impl>
  static void cleanup(Impl *, State &state, Queue &pending, ZiConnection *cxn) {
    cleanup_(state, pending, cxn);
  }
private:
  static void endIO_(WireFrame *frame) {
    if (unsigned(frame->ended) & 2) return;
    frame->posts.xchAdd(1); // reserve the I/O-end notification before publication
    frame->end_(2);
    frame->ack(frame);
  }
  static void abort_(Ref node) {
    node->cancel(node.ptr());
    node->posts.xchOr(WireFrame::Closed);
    endIO_(node.ptr());
  }
  static void cleanup_(State &state, Queue &pending, ZiConnection *cxn) {
    // Exact physical Tx is down OR its context has been explicitly disabled.
    // An obsolete generation must not clean a successor connection's entries.
    for (auto node = pending.headPtr(); node;) {
      auto next = node->Ext::next;
      if (!cxn || node->io.connection.ptr() == cxn)
	abort_(pending.delNode(node));
      node = next;
    }
    for (auto node = state.active.headPtr(); node;) {
      auto next = node->Ext::next;
      if (!cxn || node->io.connection.ptr() == cxn)
	abort_(state.active.delNode(node));
      node = next;
    }
  }
public:
  static void ended(WireFrame *frame) { endIO_(frame); }
  static void cancel(ZmRef<WireQueue::Node> node) {
    auto cxn = node->io.connection;
    if (!cxn || (unsigned(node->ended) & 2)) return;
    cxn->mx()->txRun([node = ZuMv(node), cxn = ZuMv(cxn)]() mutable {
      if (unsigned(node->ended) & 2) return;
      cxn->send_(ZiTxFn{ZuMv(node), [](WireQueue::Node *node, ZiTxContext &io) {
	if (unsigned(node->ended) & 2) return false;
	io.complete(); io.cxn->close();
	if (auto state = node->io.state)
	  cleanup_(*state, *state->pending, io.cxn);
	else endIO_(node);
	return false;
      }});
    });
  }
};

// Tx-owned. admit_ retains a complete frame without exposing bytes; flush_
// follows the LinkTx publication notice and makes that admission writable.
// Exactly one outputDone is posted per admission, including failed/closed
// frames whose queued records never received a socket completion. A successful
// result means every record was written locally; it never means peer ack.
//
// Impl hooks, all on Tx:
// recordAlloc(head) -> ZmRef<WireBuf>, with capacity >= geometry.size;
// recordSend(ZmRef<ZiIOBuf>) -> bool, transferring one record to TCP/TLS;
// outputDone(body, context, ok) MUST POST completion, never invoke LinkTx
//   inline: the core publication cursor advances after flush_ returns. On Tx,
//   call LinkTx::sent_ and then writable_(context.connection) on success to
//   resume work paused by this adapter's admission limit.
// Each turn produces at most WireLimits::records records in publication order.
// recordSendRange encrypts into actual final wire storage;
// recordStart(existing admission) queues its independent I/O Tx links;
// recordDoneIO(token, part, ok) posts fixed results to the protocol owner;
// recordRun(connection, token) posts resume_ for a bounded continuation.
// The underlying ZiTx queue serializes writes; the adapter adds no per-frame
// round-trip scheduling delay. Network sent callbacks post only (token, part, ok)
// to record_ on Tx. Records must complete in transport order. Stale and duplicate
// completions are inert.
// Reopening abruptly retires old admissions without waiting for socket callbacks;
// record backing stays alive through the transport's own buffer references.
// Terminal close follows I/O Tx shutdown; drain posted completions before owner
// destruction. Reopening never reuses an earlier transport token.
template <typename Impl, unsigned Mode = WireMode::Generic> class WireTx {
public:
  explicit WireTx(WireLimits limits = {}) : m_limits{limits} {
    ZmAssert(limits.records && limits.adminBytes <= UINT64_MAX - limits.queuedBytes);
  }

  WireTx(const WireTx &) = delete;
  WireTx &operator =(const WireTx &) = delete;

  bool open_(uint64_t connection, WireGeometry geometry = {}) {
    if (!connection || connection <= m_connection || !geometry.valid() || !m_limits.records)
      return false;
    close_(m_connection);
    m_connection = connection; m_geometry = geometry;
    m_live = true; m_failed = false;
    return true;
  }

  WireStats stats_() const {
    WireStats s{m_stats}; s.queuedCount = count_(); s.queuedBytes = m_bytes; return s;
  }

  const WireGeometry &geometry_() const { return m_geometry; }

  uint64_t bytes_() const { return m_bytes; }
  uint64_t connection_() const { return m_connection; }
  auto count_() const { return m_queue.count_() + m_retiring.count_(); }
  bool failed_() const { return m_failed; }

  bool admit_(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> frame) {
    if (!context.epoch || !context.seqNo) return refuse();
    return admit(ZuMv(body), context, TxImage{ZuMv(frame)});
  }

  // Raw tracking metadata belongs to the external manager; zero epoch/sequence
  // is allowed here and never interpreted as managed session progress.
  bool admitRaw_(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> frame) {
    return admit(ZuMv(body), context, TxImage{ZuMv(frame)});
  }

  bool admit_(TxBody::Ref body, TxContext context, Zi::RxRange frame) {
    if (!context.epoch || !context.seqNo) return refuse();
    return admit(ZuMv(body), context, TxImage{ZuMv(frame)});
  }

  bool admit_(TxBody::Ref body, TxContext context, TxImage frame) {
    if (!context.epoch || !context.seqNo) return refuse();
    return admit(ZuMv(body), context, ZuMv(frame));
  }
  bool admitRaw_(TxBody::Ref body, TxContext context, TxImage frame) {
    return admit(ZuMv(body), context, ZuMv(frame));
  }
private:
  bool admit(TxBody::Ref body, TxContext context, TxImage frame) {
    if (!m_live || m_staged || !body || !frame || frame.metadata->failed() ||
	context.connection != m_connection || m_serial == UINT64_MAX) return refuse();
    uint64_t memory = frame.memory();
    if (frame.metadata.ptr() != body.ptr()) {
      if (body->memory() > UINT64_MAX - memory) return refuse();
      memory += body->memory();
    }
    bool direct = frame.metadata->refCount() == 1 &&
      frame.origin == frame.metadata->skip && frame.length() == frame.metadata->length &&
      dynamic_cast<WireBuf *>(frame.metadata.ptr());
    unsigned parts = 1;
    if (m_geometry.size) {
      unsigned capacity = m_geometry.size - m_geometry.head - m_geometry.tail;
      parts = frame.length() / capacity + !!(frame.length() % capacity);
      direct = direct && parts == 1 && frame.metadata->skip >= m_geometry.head &&
	frame.metadata->skip <= m_geometry.size;
      if (!direct) {
	if (m_geometry.memory > (UINT64_MAX - memory) / parts) return refuse();
	memory += m_geometry.memory * parts;
      }
    }
    constexpr uint64_t overhead = ZmHeapAllocSize<sizeof(WireQueue::Node)>{};
    if (memory > UINT64_MAX - overhead) return refuse();
    uint64_t charge = memory + overhead;
    uint64_t limit = m_limits.queuedBytes +
      (body->administrative() ? m_limits.adminBytes : 0);
    if (charge > limit || m_bytes > limit - charge) return refuse();
    ZmRef<WireQueue::Node> node = new WireQueue::Node{ZuMv(body), context, ZuMv(frame),
      ++m_serial, charge, parts, direct, impl(),
      [](WireFrame *frame, ZiTxContext &io) {
	return produceIO_(static_cast<WireQueue::Node *>(frame), io);
      }, [](WireFrame *frame) {
	if (!frame->post()) return;
	static_cast<Impl *>(frame->owner)->recordDoneIO(
	  ZmRef<WireQueue::Node>{static_cast<WireQueue::Node *>(frame)}, 1, false);
      }, [](WireFrame *frame) {
	static_cast<Impl *>(frame->owner)->recordAckIO(
	  ZmRef<WireQueue::Node>{static_cast<WireQueue::Node *>(frame)});
      }};
    m_queue.pushNode(node);
    m_bytes += charge; m_staged = node; ++m_stats.admitted;
    if (m_bytes > m_stats.peakBytes) m_stats.peakBytes = m_bytes;
    return true;
  }

public:
  void flush_() {
    if (!m_staged) return;
    auto node = m_staged; m_staged = nullptr;
    const auto &item = node->data();
    ++m_stats.published; m_stats.bytes += item.frame.length();
    if (item.context.replay) ++m_stats.replays;
    if (item.body->msgType() == "3") ++m_stats.rejects;
    impl()->frameTrace(item.frame, item.body->msgType(), item.context);
    node->data().published = true;
    if (!m_writing) m_writing = node;
    if (!m_postToken) drain();
  }

  bool record_(uint64_t token, unsigned part, bool ok) {
    auto node = m_queue.headPtr();
    if (!m_live || !node || token != node->data().token ||
	node == m_staged) return false;
    auto &head = node->data();
    if (!part || part <= head.completed) return false;
    if (!ok || part != head.completed + 1 || part > head.parts) {
      fail(); return true;
    }
    if (++head.completed == head.parts) finish(true);
    if (m_live && m_writing && !m_postToken) drain();
    return true;
  }

  void frameTrace(const TxImage &, ZuCSpan, TxContext) { }

  void close_(uint64_t connection) {
    if (connection != m_connection) return;
    m_live = false; m_staged = m_writing = nullptr; m_postToken = 0;
    while (m_queue.count_()) finish(false);
  }

private:
  auto impl() { return static_cast<Impl *>(this); }

  bool refuse() { ++m_stats.refused; return false; }

  void fail() {
    m_failed = true;
    close_(m_connection);
  }

  void settle_(WireQueue::Node *node) {
    auto &item = node->data();
    m_bytes -= item.charge;
    if (item.terminalOK) ++m_stats.completed; else ++m_stats.failed;
    impl()->outputDone(item.body, item.context, item.terminalOK);
    if (!item.io.connection) item.end_(2);
    item.end_(1);
  }
  void settleRetiring_() {
    while (auto node = m_retiring.headPtr()) {
      if (node->pending() || (node->io.connection && !(unsigned(node->ended) & 2))) return;
      auto held = m_retiring.shift();
      node->retiring = false; settle_(node);
    }
  }
  void finish(bool ok) {
    auto node = m_queue.shift();
    auto &item = node->data();
    item.terminal = true; item.terminalOK = ok;
    item.posts.xchOr(WireFrame::Closed);
    WireOps::cancel(ZmRef<WireQueue::Node>{node});
    bool pending = item.pending() || (item.io.connection && !(unsigned(item.ended) & 2));
    // Preserve output completion order while any earlier admission awaits ACK.
    // Logical admission/publication remains independent of this retiring prefix.
    if (pending || m_retiring.count_()) {
      item.retiring = true; m_retiring.pushNode(ZuMv(node));
    } else settle_(node.ptr());
  }

public:
  void ack_(ZmRef<WireQueue::Node> node) {
    auto &item = node->data();
    item.posts.xchSub(1);
    if (item.retiring) settleRetiring_();
  }

public:
  void resume_(uint64_t connection, uint64_t token) {
    if (connection != m_connection || token != m_postToken) return;
    m_postToken = 0;
    drain();
  }

  // The existing admission is also the pending I/O node; original backing
  // remains retained here through DATA and any later native notification.
  static void startIO_(ZmRef<WireQueue::Node> node, ZiTxContext &io) {
    produceIO_(node.ptr(), io);
  }

private:
  static ZiTxFn bindIO_(ZmRef<WireQueue::Node> node) {
    return ZiTxFn{ZuMv(node), [](WireQueue::Node *n, ZiTxContext &io) {
      return produceIO_(n, io);
    }};
  }
  static void doneIO_(WireQueue::Node *node, unsigned part, bool ok) {
    // Commit BEFORE touching an actor pointer. A terminal admission disables
    // future NOTIF/abort posts; earlier posts keep its existing completion alive.
    if (!node->post()) return;
    static_cast<Impl *>(node->owner)->recordDoneIO(ZmRef<WireQueue::Node>{node}, part, ok);
  }
  static bool nextIO_(WireQueue::Node *node, ZiTxContext &io) {
    auto &frame = node->data();
    WireOps::inactive(&frame);
    auto parent = ZuMv(frame.io.parent);
    WireOps::ended(&frame); // no subsequent payload/image access from this producer
    io.fn = ZuMv(parent); io.flags &= ~ZiTxContext::TxDataDone;
    if (!io.initialized()) return true;
    return io();
  }
  static bool produceIO_(WireQueue::Node *node, ZiTxContext &io) {
    auto &frame = node->data();
    auto &state = frame.io;
    if (state.selecting) {
      state.selecting = false;
      state.parent = ZuMv(io.fn);
      io.fn = bindIO_(ZmRef<WireQueue::Node>{node});
    } else if (io.buf) {
      bool ok = io.length >= 0;
      io.buf = nullptr;
      if ((io.flags & ZiTxContext::TxDataDone) || (unsigned(frame.ended) & 2)) {
	doneIO_(node, ++state.retired, ok);
	io.complete(); return true;
      }
      doneIO_(node, ++state.retired, ok);
      if (!ok) {
	frame.posts.xchOr(WireFrame::Closed);
	WireOps::inactive(&frame); WireOps::ended(&frame);
	io.complete(); return true;
      }
    }
    if (io.cxn != state.connection.ptr() || !io.cxn->up()) {
      WireOps::inactive(&frame); WireOps::ended(&frame); io.complete(); return true;
    }
    // App terminal completion may cancel synchronously during doneIO_. A live
    // physical connection must keep draining its actual pending queue; its
    // selector skips canceled admissions and checks each frozen Cxn identity.
    if (state.produced == frame.parts || frame.canceled())
      return nextIO_(node, io);
    ++state.produced;
    io.txRange(frame.frame.metadata, frame.frame.origin, frame.frame.length());
    io.flags &= ~ZiTxContext::TxDataDone;
    return true;
  }

  void drain() {
    unsigned budget = m_limits.records;
    while (m_live && m_writing && budget--) {
      auto node = m_writing;
      auto &item = node->data();
      if (!item.published) { m_writing = nullptr; return; }
      if constexpr (Mode != WireMode::Records) {
	if (!m_geometry.size) {
	  item.produced = item.parts;
	  m_writing = static_cast<WireQueue::Node *>(node->Ext::next);
	  if (!impl()->recordStart(ZmRef<WireQueue::Node>{node})) { fail(); return; }
	  continue;
	}
      }
      if constexpr (Mode != WireMode::Pending) {
	auto part = ++item.produced;
	unsigned length = item.frame.length() - item.offset;
	if (m_geometry.size) {
	  unsigned capacity = m_geometry.size - m_geometry.head - m_geometry.tail;
	  if (length > capacity) length = capacity;
	}
	bool last = part == item.parts;
	if (last) m_writing = static_cast<WireQueue::Node *>(node->Ext::next);
	if (item.direct) {
	  auto buf = ZmRef<WireBuf>::acquire(
	    static_cast<WireBuf *>(ZuMv(item.frame.metadata).release()));
	  buf->token = item.token; buf->part = part;
	  if (!impl()->recordSend(ZuMv(buf))) { fail(); return; }
	} else {
	  auto buf = impl()->recordAlloc(m_geometry.head);
	  if (!buf || buf->size < m_geometry.size) { fail(); return; }
	  buf->token = item.token; buf->part = part;
	  auto source = Zi::RxRange::fromStorage(
	    item.frame.metadata, item.frame.origin + item.offset, length);
	  bool sent = impl()->recordSendRange(ZuMv(source), ZuMv(buf));
	  item.offset += length;
	  if (!sent) { fail(); return; }
	  if (last) item.frame = {};
	}
      }
    }
    if (m_live && m_writing && !m_postToken) {
      m_postToken = m_writing->data().token;
      impl()->recordRun(m_connection, m_postToken);
    }
  }

  const WireLimits	m_limits;

  // Tx-owned transport admissions and completion accounting.
  WireCounters		m_stats;
  WireGeometry		m_geometry;
  WireQueue		m_queue;
  WireQueue		m_retiring; // same App links; ACK retirement, never a pipeline queue
  WireQueue::Node	*m_staged = nullptr;
  WireQueue::Node	*m_writing = nullptr;
  uint64_t		m_postToken = 0;
  uint64_t		m_connection = 0;
  uint64_t		m_serial = 0;
  uint64_t		m_bytes = 0;
  bool			m_live = false;
  bool			m_failed = false;
};

} // Zfix

#endif /* ZfixWire_HH */
