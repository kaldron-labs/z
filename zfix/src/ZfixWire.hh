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
#include <zlib/ZiTx.hh>
#include <zlib/ZfixTx.hh>
#include <zlib/ZfixStats.hh>

namespace Zfix {

// Zero size sends a complete frame for TCP without copying. Otherwise size,
// head/tail and memory describe one transport record. memory conservatively
// includes the buffer object and any jumbo storage, preserving pool tuning.
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
};

struct WireFrame {
  TxBody::Ref		body;
  TxContext		context;
  ZmRef<ZiIOBuf>	frame;
  uint64_t		charge = 0;
};

using WireQueue = ZmList<WireFrame, ZmListHeapID<"Zfix.WireQueue",
  ZmListNode<ZmObject>>>;

// Tx-owned. admit_ retains a complete frame without exposing bytes; flush_
// follows the LinkTx publication notice and makes that admission writable.
// Exactly one outputDone is posted per admission, including failed/closed
// frames whose queued records never received a socket completion. A successful
// result means every record was written locally; it never means peer ack.
//
// Impl hooks, all on Tx:
// recordAlloc(head) -> ZmRef<ZiIOBuf>, with capacity >= geometry.size;
// recordSend(ZmRef<ZiIOBuf>) -> bool, transferring one record to TCP/TLS;
// outputDone(body, context, ok) MUST POST completion, never invoke LinkTx
//   inline: the core publication cursor advances after flush_ returns. On Tx,
//   call LinkTx::sent_ and then writable_(context.connection) on success to
//   resume work paused by this adapter's admission limit.
// Each flush queues all records in order without waiting for earlier frames.
// The underlying ZiTx queue serializes writes; the adapter adds no per-frame
// round-trip scheduling delay. Network sent callbacks retain the completed buffer
// and post record_ on Tx. FIFO completion of a frame's final buffer completes the
// frame. Earlier records and stale completions cannot match that buffer.
// Native send errors close their connection; teardown fails unfinished frames.
// Reopening abruptly retires old admissions without waiting for socket callbacks;
// record backing stays alive through the transport's own buffer references.
// Terminal close follows I/O Tx shutdown; drain posted completions before owner
// destruction. Each transmission attempt uses distinct output buffers.
template <typename Impl> class WireTx {
public:
  explicit WireTx(WireLimits limits = {}) : m_limits{limits} {
    ZmAssert(limits.adminBytes <= UINT64_MAX - limits.queuedBytes);
  }

  WireTx(const WireTx &) = delete;
  WireTx &operator =(const WireTx &) = delete;

  bool open_(uint64_t connection, WireGeometry geometry = {}) {
    if (!connection || connection <= m_connection || !geometry.valid()) return false;
    close_(m_connection);
    m_connection = connection; m_geometry = geometry;
    m_live = true; m_failed = false;
    return true;
  }

  WireStats stats_() const {
    WireStats s{m_stats}; s.queuedCount = m_queue.count_(); s.queuedBytes = m_bytes; return s;
  }

  uint64_t bytes_() const { return m_bytes; }
  uint64_t connection_() const { return m_connection; }
  auto count_() const { return m_queue.count_(); }
  bool failed_() const { return m_failed; }

  bool admit_(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> frame) {
    if (!context.epoch || !context.seqNo) return refuse();
    return admit(ZuMv(body), context, ZuMv(frame));
  }

  // Raw tracking metadata belongs to the external manager; zero epoch/sequence
  // is allowed here and never interpreted as managed session progress.
  bool admitRaw_(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> frame) {
    return admit(ZuMv(body), context, ZuMv(frame));
  }

private:
  bool admit(TxBody::Ref body, TxContext context, ZmRef<ZiIOBuf> frame) {
    if (!m_live || m_staged || !body || !frame || !frame->length || frame->failed() ||
	context.connection != m_connection) return refuse();
    uint64_t memory = sizeof(ZiTxBuf) + uint64_t(frame->size);
    unsigned parts = 1;
    if (m_geometry.size) {
      unsigned capacity = m_geometry.size - m_geometry.head - m_geometry.tail;
      parts = frame->length / capacity + !!(frame->length % capacity);
      if (m_geometry.memory > (UINT64_MAX - memory) / parts) return refuse();
      memory += m_geometry.memory * parts;
    }
    if (memory > UINT64_MAX - sizeof(WireQueue::Node)) return refuse();
    uint64_t charge = memory + sizeof(WireQueue::Node);
    uint64_t limit = m_limits.queuedBytes +
      (body->administrative() ? m_limits.adminBytes : 0);
    if (charge > limit || m_bytes > limit - charge) return refuse();
    auto node = m_queue.push(WireFrame{ZuMv(body), context, ZuMv(frame), charge});
    m_bytes += charge; m_staged = node; ++m_stats.admitted;
    if (m_bytes > m_stats.peakBytes) m_stats.peakBytes = m_bytes;
    return true;
  }

public:
  void flush_() {
    if (!m_staged) return;
    auto node = m_staged; m_staged = nullptr;
    const auto &item = node->data();
    ++m_stats.published; m_stats.bytes += item.frame->length;
    if (item.context.replay) ++m_stats.replays;
    if (item.body->msgType() == "3") ++m_stats.rejects;
    impl()->frameTrace(ZuCSpan{item.frame->cspan()}, item.context);
    send(node->data());
  }

  bool record_(const ZiIOBuf *buf, bool ok) {
    auto node = m_queue.headPtr();
    if (!m_live || !node || buf != node->data().frame.ptr() ||
	node == m_staged) return false;
    if (!ok) fail();
    else finish(true);
    return true;
  }

  void frameTrace(ZuCSpan, TxContext) { }

  void close_(uint64_t connection) {
    if (connection != m_connection) return;
    m_live = false; m_staged = nullptr;
    while (m_queue.count_()) finish(false);
  }

private:
  auto impl() { return static_cast<Impl *>(this); }

  bool refuse() { ++m_stats.refused; return false; }

  void fail() {
    m_failed = true;
    close_(m_connection);
  }

  void finish(bool ok) {
    auto node = m_queue.shift();
    auto &item = node->data();
    m_bytes -= item.charge;
    if (ok) ++m_stats.completed; else ++m_stats.failed;
    impl()->outputDone(ZuMv(item.body), item.context, ok);
  }

  void send(WireFrame &item) {
    if (!m_live) return;
    if (!m_geometry.size) {
      if (!impl()->recordSend(item.frame)) fail();
      return;
    }
    unsigned offset = 0;
    unsigned capacity = m_geometry.size - m_geometry.head - m_geometry.tail;
    unsigned size = item.frame->length;
    auto data = item.frame->data();
    while (offset < size) {
      auto buf = impl()->recordAlloc(m_geometry.head);
      if (!buf || buf->size < m_geometry.size) { fail(); return; }
      unsigned length = size - offset;
      if (length > capacity) length = capacity;
      buf->skip = m_geometry.head; buf->length = 0;
      if (!buf->append(data + offset, length)) { fail(); return; }
      offset += length;
      if (offset == size) item.frame = buf;
      if (!impl()->recordSend(ZuMv(buf))) { fail(); return; }
    }
  }

  const WireLimits	m_limits;

  // Tx-owned transport admissions and completion accounting.
  WireCounters		m_stats;
  WireGeometry		m_geometry;
  WireQueue		m_queue;
  WireQueue::Node	*m_staged = nullptr;
  uint64_t		m_connection = 0;
  uint64_t		m_bytes = 0;
  bool			m_live = false;
  bool			m_failed = false;
};

} // Zfix

#endif /* ZfixWire_HH */
