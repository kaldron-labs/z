//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - private native transport adaptation

#ifndef ZhttpNative_HH
#define ZhttpNative_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <stdint.h>
#include <limits.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiIOContext.hh>
#include <zlib/ZiRxStream.hh>

#include <zlib/Ztcp.hh>

#include <zlib/Ztls.hh>

#include <zlib/Zquic.hh>

#include <zlib/ZhttpTransport.hh>
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpProfiles.hh>
#include <zlib/ZhttpConfig.hh>
#include "ZhttpCompression.hh"
#include "ZhttpFieldImpl.hh"

namespace Zhttp {

struct RetainedEntry {
  ZmRef<ZiIOBuf> buf;
  bool final = false;

  RetainedEntry() = default;
  RetainedEntry(ZmRef<ZiIOBuf> buf_, bool final_) :
    buf{ZuMv(buf_)}, final{final_} { }
};
using RetainedEntries =
  ZtArray<RetainedEntry, ZtArrayHeapID<"Zhttp.RetainedTx.Entries">>;

namespace Transport_ {

enum {
  H1BufSize = 8<<10,		// common request/response buffer pool block
  H1BufMax = 100<<20		// maximum configured HTTP body/buffer growth
};

using TxBufNode = ZiTxQueue::Node;

struct TCPHeader;
struct TCPState {
  ZiTxQueue active;
  ZiTxQueue pending;
  ZmAtomic<unsigned> count{0}; // one pending/active count per header, independent of body buffers
};

// The admission is the real first serialized header, never an empty carrier.
struct TCPHeader : public TxBufNode {
  RetainedEntries entries;
  ZmRef<ZiConnection> connection;
  ZmRef<ZmPolymorph> link;
  ZiMultiplex *mx = nullptr; // immutable dispatch, frozen before early Rx posts
  TCPState *state = nullptr;
  void (*ownerAck)(ZmRef<TCPHeader>) = nullptr;
  void (*terminalPost)(ZmRef<TCPHeader>) = nullptr;
  ZmAtomic<unsigned> posts{0}; // outstanding cross-shard continuations
  ZmAtomic<unsigned> ioEnded{0};
  unsigned appThread = 0;
  unsigned cursor = 0;
  enum Park : uint8_t { None, Input };
  Park parked = None;
  bool first = true;
  bool streaming = false, ended = true;
  bool terminalReserved = false, terminalPosted = false; // I/O owner
  bool appTerminal = false, appSettled = false; // App Tx owner

  TCPHeader(uint8_t *data, unsigned size, Zi::IOBufAllocator_ *allocator,
      void *owner = nullptr, unsigned length = 0, bool initial = false) :
    TxBufNode{data, size, allocator, owner, length, initial} { }

  void detach_() {
    connection = nullptr; state = nullptr;
  }
  void prepareEnd_() {
    if (terminalReserved) return;
    terminalReserved = true;
    ++posts; // terminal notification joins the existing continuation drain
  }
  void endIO_() {
    prepareEnd_();
    entries.length(0);
    ioEnded = 1;
  }
  void postEnd_() {
    if (terminalPosted) return;
    terminalPosted = true;
    terminalPost(ZmRef<TCPHeader>{this}); // LAST owner operation
  }

};

struct TCPTxOps {
  using State = TCPState;
  using Queue = ZiTxQueue;
  using Node = Queue::Node;
  using Ref = Queue::NodeRef;

  template <typename Impl> static Impl *owner(const TCPHeader *head) {
    return reinterpret_cast<Impl *>(uintptr_t(head->owner) & ~uintptr_t(1));
  }
  template <typename Impl>
  static void select(Impl *impl, Ref node, ZiTxContext &io) {
    while (node) {
      auto head = static_cast<TCPHeader *>(node.ptr());
      if (head->ioEnded.load_()) {
	aborted(ZuMv(node));
	node = impl->txState_().pending.shift();
	continue;
      }
      // Frozen physical ownership prevents a successor from consuming an old
      // association's header, input policy, or retained source ranges.
      bool waiting = head->parked == TCPHeader::Input &&
	!head->ended && head->cursor == head->entries.length();
      if (head->connection.ptr() != io.cxn || waiting) {
	head->state->pending.unshiftNode(node.ptr()); io.complete(); return;
      }
      head->state->active.pushNode(node);
      head->parked = TCPHeader::None;
      io.state = 0; io.tos = {};
      io.fn = ZiTxFn{ZmRef<TCPHeader>{head}, [](TCPHeader *head, ZiTxContext &io) {
	return produce_<Impl>(head, io);
      }};
      produce_<Impl>(head, io);
      return;
    }
    io.complete();
  }
  static void aborted(Ref node) {
    auto head = static_cast<TCPHeader *>(node.ptr());
    --head->state->count;
    head->endIO_();
    head->postEnd_();
  }
  template <typename Impl>
  static void cleanup(Impl *, State &state, ZiConnection *cxn) {
    for (auto node = state.pending.headPtr(); node;) {
      auto next = node->Ext::next;
      auto head = static_cast<TCPHeader *>(node);
      if (!cxn || head->connection.ptr() == cxn)
	aborted(state.pending.delNode(node));
      node = next;
    }
    for (auto node = state.active.headPtr(); node;) {
      auto next = node->Ext::next;
      auto head = static_cast<TCPHeader *>(node);
      if (!cxn || head->connection.ptr() == cxn)
	aborted(state.active.delNode(node));
      node = next;
    }
  }

  static void postAck_(ZmRef<TCPHeader> head) {
    auto mx = head->mx;
    auto thread = head->appThread;
    mx->run([head = ZuMv(head)]() mutable {
      auto ack = head->ownerAck;
      ack(ZuMv(head));
    }, thread);
  }
  template <typename Impl> static void postAck_(ZmRef<TCPHeader> head, unsigned bytes) {
    auto mx = head->mx;
    auto thread = head->appThread;
    mx->run([head = ZuMv(head), bytes]() mutable {
      auto link = static_cast<Impl *>(head->link.ptr());
      link->admitTCPBytes_(bytes);
      auto ack = head->ownerAck;
      ack(ZuMv(head));
    }, thread);
  }
  template <typename Impl> static void wake(TCPHeader *head) {
    if (!head->state || head->ioEnded.load_() || !head->connection) return;
    head->connection->send_(ZiTxFn{ZmRef<TCPHeader>{head},
      [](TCPHeader *head, ZiTxContext &io) {
	if (!io.completed() || head->ioEnded.load_() ||
	    head->connection.ptr() != io.cxn || !io.cxn->up()) return false;
	auto &queue = head->state->pending;
	if (queue.headPtr() != static_cast<Node *>(head)) return false;
	select(owner<Impl>(head), queue.shift(), io);
	return true;
      }});
  }

private:
  static void park_(TCPHeader *head, ZiTxContext &io) {
    auto held = head->state->active.delNode(static_cast<Node *>(head));
    ZmAssert(held);
    head->state->pending.unshiftNode(held.ptr());
    head->parked = TCPHeader::Input;
    io.complete();
  }
  template <typename Impl> static bool produce_(TCPHeader *head, ZiTxContext &io) {
    if (io.buf) {
      if (io.length < 0) {
	auto impl = owner<Impl>(head);
	auto state = head->state;
	io.complete();
	impl->sent(ZmRef<ZiTxBuf>{head}, false);
	cleanup(impl, *state, head->connection.ptr());
	return true;
      }
      io.offset += io.length;
      if (io.offset < io.buf->length) return true;
      io.buf = nullptr;
    }
    if (head->ioEnded.load_() || !io.cxn->up()) {
      auto impl = owner<Impl>(head);
      auto state = head->state;
      auto cxn = head->connection;
      io.complete();
      cleanup(impl, *state, cxn.ptr());
      return true;
    }
    ZiAssert(io.cxn == head->connection.ptr(), "Zhttp", (),
      "HTTP source uses another physical connection", io.complete(); return true);
    if (!head->first && head->cursor == head->entries.length()) {
      head->entries.length(0); head->cursor = 0;
      if (head->streaming && !head->ended) {
	park_(head, io);
	return true;
      }
      auto impl = owner<Impl>(head);
      auto state = head->state;
      auto held = state->active.delNode(static_cast<Node *>(head));
      --state->count;
      io.complete();
      head->owner = impl;
      head->endIO_();
      impl->sent(held, true);
      head->postEnd_();
      select(impl, state->pending.shift(), io);
      return true;
    }
    if (head->first) {
      head->first = false;
      io.buf = ZmRef<TCPHeader>{head};
    } else {
      auto &entry = head->entries[head->cursor++];
      io.buf = owner<Impl>(head)->wireTCP_(entry.buf);
    }
    io.offset = 0;
    io.length = 0;
    return true;
  }
};

template <typename Impl> struct TCPDrain {
  using Notify = void (*)(ZmRef<Impl>, bool);
  ZmRef<Impl> link;
  Notify notify;
  bool peer;
};
template <typename Impl>
struct alignas(Zm::CacheLineSize) TCPActor {
  unsigned count = 0;
  ZmAtomic<uint64_t> inCount{0}, inBytes{0};
  ZmRef<ZiConnection> cxn; // App Tx destination; published from Rx binding
  ZtArray<TCPDrain<Impl>, ZtArrayHeapID<"Zhttp.TCP.Drains">> drains;
  alignas(Zm::CacheLineSize) TCPState io; // exclusively I/O Tx
};
struct TCPNoActor { };
template <typename Impl, typename Traits>
using TCPActorT = ZuIf<Traits::ID == Transport::TCP, TCPActor<Impl>, TCPNoActor>;

// Request producers pass their existing first-header handle across turns.
// An upgraded framed writer uses one local header owner per synchronous turn.
template <typename Link, bool AppThread>
class TCPStream : public ZiTxStream<TCPStream<Link, AppThread>> {
  using Base = ZiTxStream<TCPStream<Link, AppThread>>;
  using Alloc = typename Link::TxBufAlloc;
public:
  enum { RetainBuffers = true };
  TCPStream(Link &link, ZmRef<TCPHeader> *slot = nullptr) :
    Base{Alloc::Size, 0, 0}, m_link{&link}, m_slot{slot},
    m_head{slot ? *slot : ZmRef<TCPHeader>{}} { }
  ~TCPStream() { this->flush(); if (!m_slot) endTCP_(); }

  ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
    auto buf = m_link->allocTxBuf_();
    if (!buf || skip > buf->size) return nullptr;
    buf->skip = skip;
    m_allocated = buf;
    return buf;
  }
  bool allocatedBuf_(const ZiIOBuf *buf) const {
    return buf == m_allocated.ptr();
  }
  template <typename Buf,
    typename = ZuIfT<ZuIsSame<ZuDecay<Buf>, ZmRef<ZiIOBuf>>{}>>
  bool allocatedBuf_(Buf &&buf) {
    // An earlier first header consumes provenance when published. Flush that
    // wire before accepting authority for this unpublished writer allocation.
    if (!this->flush()) return false;
    m_allocated = ZuFwd<Buf>(buf);
    return true;
  }
  bool appendBuf(ZmRef<ZiIOBuf> buf) {
    if (!this->flush() || !buf || buf->failed() || !sendBuf_(ZuMv(buf), true)) {
      this->fail();
      return false;
    }
    return true;
  }
  TCPStream &operator <<(ZmRef<ZiIOBuf> buf) {
    appendBuf(ZuMv(buf));
    return *this;
  }
  template <typename V,
    typename = ZuIfT<!ZuIsConvertible<V, ZmRef<ZiIOBuf>>{}>>
  TCPStream &operator <<(V &&value) {
    Base::operator <<(ZuFwd<V>(value));
    return *this;
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
    if (!m_head) {
      if (buf != m_allocated) {
	auto own = m_link->allocTxBuf_();
	own->append(buf->cspan());
	buf = ZuMv(own);
      }
      auto head = ZmRef<TCPHeader>::acquire(
	static_cast<TCPHeader *>(ZuMv(buf).release()));
      if (!m_link->startTCP_(head, AppThread)) return false;
      m_head = ZuMv(head);
      if (m_slot) *m_slot = m_head;
      m_allocated = nullptr;
      return true;
    }
    if (buf == m_allocated) m_allocated = nullptr;
    return m_link->partTCP_(m_head, RetainedEntry{ZuMv(buf), false});
  }
  void endTCP_() {
    if (m_ended) return;
    this->flush(); m_ended = true;
    if (m_head) m_link->endTCP_(m_head);
    if (m_slot) *m_slot = nullptr;
    m_head = nullptr;
  }
private:
  Link *m_link;
  ZmRef<TCPHeader> *m_slot;
  ZmRef<TCPHeader> m_head;
  ZmRef<ZiIOBuf> m_allocated;
  bool m_ended = false;
};

using TCPRxBufAlloc = Ztcp::RxBufAlloc<H1BufSize, H1BufMax, "Zhttp.TCP.Rx">;
using TCPTxBufAlloc = Zi::IOBufBuiltin<
  TCPHeader,
  H1BufSize,
  ZiIOBuf_DefltAlign,
  ZuStringT<"Zhttp.TCP.Tx">,
  H1BufMax>;
using TLSRxBufAlloc = Ztls::RxBufAlloc<H1BufSize, H1BufMax, "Zhttp.TLS.Rx">;
using TLSTxBufAlloc = Ztls::TxBufAlloc<
  H1BufSize, H1BufMax, "Zhttp.TLS.Tx">;
using H3TxBufAlloc = Zquic::StreamTxBufAlloc<
  Zquic::BufSize, ZiIOBuf_DefltMaxSize, "Zhttp.H3.Tx">;

template <typename Link, typename = void>
struct HasTxStream : public ZuFalse { };
template <typename Link>
struct HasTxStream<Link,
  decltype(ZuDeclVal<Link &>().txStream(), void())> : public ZuTrue { };

template <bool AppThread, typename Native,
  typename = decltype(ZuDeclVal<const Native &>().template txCanAdopt<AppThread>(
    ZuDeclVal<const ZiIOBuf &>(), unsigned{}, unsigned{}))>
bool canAdopt(
  const Native &native, const ZiIOBuf &buf, unsigned head, unsigned tail, int)
{
  return native.template txCanAdopt<AppThread>(buf, head, tail);
}
template <bool AppThread, typename Native>
bool canAdopt(
  const Native &native, const ZiIOBuf &buf, unsigned head, unsigned tail, long)
{
  if (head > buf.skip || tail > buf.avail()) return false;
  uint64_t front = native.template txHeadRoom<AppThread>();
  uint64_t back = native.template txTailRoom<AppThread>();
  uint64_t reserve = front + back;
  uint64_t length = uint64_t(buf.length) + head + tail;
  return reserve < native.txMaxSize() && length <= native.txMaxSize() - reserve &&
    buf.skip - head >= front && buf.avail() - tail >= back;
}

template <typename Link, typename Rx, typename = void>
struct HasProcess : public ZuFalse { };
template <typename Link, typename Rx>
struct HasProcess<Link, Rx,
  decltype(ZuDeclVal<Link &>().process(ZuDeclVal<Rx &>()), void())> :
    public ZuTrue { };

template <typename Link, typename Info, typename = void>
struct HasConnected : public ZuFalse { };
template <typename Link, typename Info>
struct HasConnected<Link, Info,
  decltype(ZuDeclVal<Link &>().connected(ZuDeclVal<Info>()), void())> :
    public ZuTrue { };

template <typename Link, typename = void>
struct HasDisconnected : public ZuFalse { };
template <typename Link>
struct HasDisconnected<Link,
  decltype(ZuDeclVal<Link &>().disconnected(false), void())> : public ZuTrue { };

template <
  typename Link, typename Rx, typename Info,
  typename = ZuIfT<
    HasTxStream<Link>{} &&
    HasProcess<Link, Rx>{} &&
    HasConnected<Link, Info>{} &&
    HasDisconnected<Link>{}>>
struct LinkContract { };

inline Zquic::MigrationMode::T migration(int8_t v)
{
  switch (v) {
    case Migration::Disabled: return Zquic::MigrationMode::Disabled;
    case Migration::Active: return Zquic::MigrationMode::Active;
    default: return Zquic::MigrationMode::Passive;
  }
}

inline Ztls::ClientParams tlsParams(
  const HubConfig &hub, const TLSConfig &config)
{
  return Ztls::ClientParams{
    hub.mx(), hub.rxThread(), hub.txThread()}.hub(
      [&hub, &config](auto &p) {
	p.asyncThread(hub.asyncThread())
	  .rxAllocator(config.rxAllocator()).txAllocator(config.txAllocator())
	  .caPath(config.caPath()).certPath(config.certPath())
	  .keyPath(config.keyPath()).alpn(ZuSpan<ZuCSpan>{"http/1.1"});
      });
}

inline Zquic::HubParams quicParams(
  const HubConfig &hub, const QUICConfig &config,
  uint64_t defltStreams)
{
  Zquic::HubParams params{
    hub.mx(), hub.rxThread(), hub.txThread()};
  params
    .asyncThread(hub.asyncThread())
    .caPath(config.caPath()).certPath(config.certPath())
    .keyPath(config.keyPath()).keyLogPath(config.keyLogPath())
    .alpn(ZuSpan<ZuCSpan>{"h3"})
    .maxData(config.maxData()).maxStreamData(config.maxStreamData())
    .maxStreamsDuplex(
      config.maxStreamsDuplex() != QUICConfig::RoleDefault ?
	config.maxStreamsDuplex() : defltStreams)
    .maxStreamsSimplex(config.maxStreamsSimplex())
    .maxIdleTimeout(config.maxIdleTimeout())
    .heartBeat(config.heartbeat())
    .migrationMode(migration(config.migration()))
    .migCIDRes(config.migrationCIDReserve())
    .migCloseOnFail(config.migrationCloseOnFailure())
    .ecn(config.ecn()).qlog(config.qlog())
    .qlogPath(config.qlogPath());
  if (config.maxUDP()) params.maxUDP(config.maxUDP());
  if (config.qlogRingSize()) params.qlogRingSize(config.qlogRingSize());
  if (config.qlogAge()) params.qlogAge(config.qlogAge());
  return params;
}

// Native hubs retain dedicated receive/transmit allocator ownership. Reuse a
// live registration across init/final cycles; a finalized multiplexer requires
// a new pair rather than republishing its retired allocators.
template <typename Native, typename Rx, typename Tx, Zi::IOBufType::T RxType,
  Zi::IOBufType::T TxType>
class Hub : public Native {
public:
  using Native::Native;
  template <typename Params> bool init(Params params) {
    auto mx = mx_(params);
    if (params.rxAllocator()) m_rx = params.rxAllocator();
    else if (!m_rx || m_rx->mx() != mx)
      m_rx = mx->template allocator<RxType, typename Rx::Buf, Rx::Size, Rx::MaxSize,
	Rx::Align, typename Rx::Heap>();
    if (params.txAllocator()) m_tx = params.txAllocator();
    else if (!m_tx || m_tx->mx() != mx)
      m_tx = mx->template allocator<TxType, typename Tx::Buf, Tx::Size, Tx::MaxSize,
	Tx::Align, typename Tx::Heap>();
    params.rxAllocator(m_rx).txAllocator(m_tx);
    return Native::init(ZuMv(params));
  }
private:
  static ZiMultiplex *mx_(const Ztcp::HubParams &params) { return params.mx; }
  static ZiMultiplex *mx_(const Ztls::HubParams &params) { return params.mx(); }
  Zi::IOBufAllocator *m_rx = nullptr;
  Zi::IOBufAllocator *m_tx = nullptr;
};

template <typename Protocol> struct Traits;

template <> struct Traits<TCP> {
  enum {
    ID = Transport::TCP,
    Secure = false,
    Datagram = false
  };

  using ClientParams = Ztcp::ClientParams;
  using ServerParams = Ztcp::ServerParams;
  using Config = TCPConfig;
  using RxStream = Ztcp::RxStream;
  using Connected = Ztcp::Connected;

  using RxAllocator = TCPRxBufAlloc;
  using TxAllocator = TCPTxBufAlloc;
  template <typename App>
  using Client = Hub<Ztcp::Client<App>, RxAllocator, TxAllocator,
    Zi::IOBufType::RxTCP, Zi::IOBufType::TxTCP>;
  template <typename App>
  using Server = Hub<Ztcp::Server<App>, RxAllocator, TxAllocator,
    Zi::IOBufType::RxTCP, Zi::IOBufType::TxTCP>;
  template <typename App, typename Link>
  using ClientLink =
    Ztcp::CliLink<App, Link, TCPRxBufAlloc, TCPTxBufAlloc>;
  template <typename App, typename Link>
  using ServerLink =
    Ztcp::SrvLink<App, Link, TCPRxBufAlloc, TCPTxBufAlloc>;

  static ClientParams clientParams(
    const HubConfig &hub, const TCPConfig &config)
  {
    ClientParams params{hub.mx(), hub.rxThread(), hub.txThread()};
    params.rxAllocator(config.rxAllocator()).txAllocator(config.txAllocator());
    return params;
  }
  static ServerParams serverParams(
    const HubConfig &hub, const TCPConfig &config)
  {
    ServerParams params{hub.mx(), hub.rxThread(), hub.txThread()};
    params.rxAllocator(config.rxAllocator()).txAllocator(config.txAllocator());
    return params;
  }
  static ConnectedInfo connected(Connected) {
    return {
      .transport = Transport::TCP
    };
  }
  template <typename Hub>
  static bool startClient(Hub &hub) { return hub.start(); }
  template <typename Hub>
  static bool startServer(Hub &hub) { return hub.start(); }
  template <typename Hub>
  static void stopListening(Hub &hub) { hub.stopListening(); }
  template <typename Link>
  static void disconnect(Link &link) { link.disconnect_(); }
};

template <> struct Traits<TLS> {
  enum {
    ID = Transport::TLS,
    Secure = true,
    Datagram = false
  };

  using ClientParams = Ztls::ClientParams;
  using ServerParams = Ztls::ServerParams;
  using Config = TLSConfig;
  using RxStream = Ztls::RxStream;
  using Connected = Ztls::Connected;

  using RxAllocator = TLSRxBufAlloc;
  using TxAllocator = TLSTxBufAlloc;
  template <typename App>
  using Client = Hub<Ztls::Client<App>, RxAllocator, TxAllocator,
    Zi::IOBufType::RxTLS, Zi::IOBufType::TxTLS>;
  template <typename App>
  using Server = Hub<Ztls::Server<App>, RxAllocator, TxAllocator,
    Zi::IOBufType::RxTLS, Zi::IOBufType::TxTLS>;
  template <typename App, typename Link>
  using ClientLink =
    Ztls::CliLink<App, Link, TLSRxBufAlloc, TLSTxBufAlloc>;
  template <typename App, typename Link>
  using ServerLink =
    Ztls::SrvLink<App, Link, TLSRxBufAlloc, TLSTxBufAlloc>;

  static ClientParams clientParams(
    const HubConfig &hub, const TLSConfig &config)
  {
    return tlsParams(hub, config);
  }
  static ServerParams serverParams(
    const HubConfig &hub, const TLSConfig &config)
  {
    ServerParams params{
      hub.mx(), hub.rxThread(), hub.txThread()};
    params
      .asyncThread(hub.asyncThread())
      .rxAllocator(config.rxAllocator()).txAllocator(config.txAllocator())
      .caPath(config.caPath()).certPath(config.certPath())
      .keyPath(config.keyPath()).alpn(ZuSpan<ZuCSpan>{"http/1.1"});
    params.mTLS(config.mTLS()).cacheTimeout(config.cacheTimeout());
    return params;
  }
  static ConnectedInfo connected(Connected info) {
    return {
      .alpn = ALPNString{info.alpn},
      .version = uint32_t(info.version),
      .transport = Transport::TLS,
      .secure = true
    };
  }
  template <typename Hub>
  static bool startClient(Hub &hub) { return hub.start(); }
  template <typename Hub>
  static bool startServer(Hub &hub) { return hub.start(); }
  template <typename Hub>
  static void stopListening(Hub &hub) { hub.stopListening(); }
  template <typename Link>
  static void disconnect(Link &link) { link.disconnect_(); }
};

template <> struct Traits<QUIC> {
  enum {
    ID = Transport::QUIC,
    Secure = true,
    Datagram = true
  };

  using ClientParams = Zquic::ClientParams;
  using ServerParams = Zquic::ServerParams;
  using Config = QUICConfig;
  using RxStream = Zquic::RxStream;
  using Connected = Zquic::Connected;

  template <typename App> using Client = Zquic::Client<App>;
  template <typename App, typename Link>
  using Server = Zquic::Server<App, Link>;
  template <typename App, typename Link, typename Stream>
  using ClientLink = Zquic::CliLink<App, Link, Stream, H3TxBufAlloc>;
  template <typename App, typename Link, typename Stream>
  using ServerLink = Zquic::SrvLink<App, Link, Stream, H3TxBufAlloc>;
  template <typename Link, typename Stream>
  using ClientStream = Zquic::CliStream<Link, Stream>;
  template <typename Link, typename Stream>
  using ServerStream = Zquic::SrvStream<Link, Stream>;

  static ClientParams clientParams(
    const HubConfig &hub, const QUICConfig &config)
  {
    return quicParams(
      hub, config, QUICConfig::DefltClientStreams);
  }
  static ServerParams serverParams(
    const HubConfig &hub, const QUICConfig &config)
  {
    return quicParams(
      hub, config, QUICConfig::DefltServerStreams);
  }
  static ConnectedInfo connected(Connected info) {
    return {
      .alpn = ALPNString{info.alpn},
      .version = info.version,
      .transport = Transport::QUIC,
      .secure = true
    };
  }
  template <typename Hub>
  static bool startClient(Hub &hub) { return hub.start(); }
  template <typename Hub>
  static bool startServer(Hub &hub) { return hub.start(); }
  template <typename Hub>
  static void stopListening(Hub &) { }
  template <typename Link>
  static void disconnect(Link &link) { link.disconnect_(); }
};

template <typename Base_, typename Impl, typename Traits>
class TCPProtocol : public Base_ {
  using Base = Base_;
public:
  using Base::Base;
  auto impl() { return static_cast<Impl *>(this); }
  // Preserve native queue identity and physical counters. HTTP owns admission
  // totals and counts each pending/active batch once. Physical output backing
  // adds no second count. I/O publishes the count without cross-owner scans.
  virtual unsigned allQueues(Ztc::QueueMgr::AllFn fn) const {
    if constexpr (!ZuIs_<Base, Ztc::Link>{}) return 0;
    else if constexpr (Traits::ID != Transport::TCP)
      return Base::allQueues(ZuMv(fn));
    else return Base::allQueues([this, &fn](Ztc::Queue *queue) {
      if (queue->telKey().template p<2>() != Ztc::QueueType::Tx) { fn(queue); return; }
      TelQueue adapted{this, queue};
      fn(&adapted);
    });
  }
  bool connectSocket_(bool replacing) {
    if (!Base::connectSocket_(replacing)) return false;
    if constexpr (Traits::ID == Transport::TCP) {
      auto cxn = ZmRef<ZiConnection>{this->cxn()}; // Rx freezes physical binding
      this->app()->txRun([link = ZmRef(impl()), cxn = ZuMv(cxn)]() mutable {
	auto old = ZuMv(link->m_tcp.cxn);
	link->m_tcp.cxn = ZuMv(cxn);
	if (old && old != link->m_tcp.cxn) link->cleanupTCP_(ZuMv(old));
      });
    }
    return true;
  }
  void connectClosed_() {
    Base::connectClosed_();
    if constexpr (Traits::ID == Transport::TCP)
      this->app()->txRun([link = ZmRef(impl())]() mutable {
	auto cxn = ZuMv(link->m_tcp.cxn);
	if (cxn) link->cleanupTCP_(ZuMv(cxn));
      });
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  void admitTCPBytes_(unsigned bytes) {
    m_tcp.inBytes.store_(m_tcp.inBytes.load_() + bytes);
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  TCPState &txState_() { return m_tcp.io; }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  bool sendTCP_(ZmRef<TCPHeader> header, unsigned bytes) {
    ZiAssert(this->app()->txInvoked(), "Zhttp", (),
      "HTTP TCP admission outside App Tx", return false);
    if (!m_tcp.cxn || !m_tcp.cxn->up() || !header->length) return false;
    ++m_tcp.inCount;
    m_tcp.inBytes.store_(m_tcp.inBytes.load_() + bytes);
    header->connection = m_tcp.cxn;
    header->state = &m_tcp.io;
    header->connection->send(ZiTxFn{ZuMv(header), [](TCPHeader *header, ZiTxContext &io) {
      if (!io.cxn->up() || header->ioEnded.load_()) {
	header->endIO_(); header->postEnd_(); return false;
      }
      auto impl = TCPTxOps::owner<Impl>(header);
      auto &queue = header->state->pending;
      queue.pushNode(header);
      ++header->state->count;
      if (io.completed()) TCPTxOps::select(impl, queue.shift(), io);
      return true;
    }});
    return true;
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  void cleanupTCP_(ZmRef<ZiConnection> cxn) {
    cxn->send(ZiTxFn{[
      link = ZmRef(impl()), cxn = ZuMv(cxn)
    ](ZiTxContext &io) mutable {
      (void)cxn; // retain the exact physical connection through cleanup
      io.complete();
      TCPTxOps::cleanup(link.ptr(), link->m_tcp.io, io.cxn);
      auto app = link->app();
      app->rxRun([link = ZuMv(link)] { }); // release final link holds on Rx
      return false;
    }});
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  ZmRef<ZiIOBuf> wireTCP_(ZmRef<ZiIOBuf> source) {
    // Ownership has transferred. Dedicated output already has the correct
    // lifetime/allocator role; foreign application or receive data is serialized
    // once into real HTTP output backing, without retagging its allocator.
    if (source->allocator == this->txAllocator()) return source;
    auto out = this->allocTxBuf_();
    out->append(source->cspan());
    return out;
  }
  template <typename Builder, typename T = Traits,
    typename = ZuIfT<T::ID == Transport::TCP>>
  auto transmit_(Builder &, ZmRef<TCPHeader> &head) {
    return TCPStream<Impl, false>{*impl(), &head};
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  bool startTCP_(ZmRef<TCPHeader> header, bool appThread) {
    header->streaming = true; header->ended = false;
    header->appThread = this->app()->txThread();
    header->link = ZmRef<ZmPolymorph>{impl()};
    header->owner = reinterpret_cast<void *>(uintptr_t(impl()) | 1);
    initTCP_(header.ptr());
    if (appThread) {
      if (header->ioEnded.load_()) return false;
      ++header->posts;
      this->app()->txRun([link = ZmRef(impl()), header = ZuMv(header)]() mutable {
	link->enqueueTCP_(header);
	link = nullptr; // no actor capture remains past the registration ACK
	auto ack = header->ownerAck;
	ack(ZuMv(header));
      });
      return true;
    }
    return enqueueTCP_(ZuMv(header));
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  bool enqueueTCP_(ZmRef<TCPHeader> header) {
    registerTCP_(header.ptr());
    if (sendTCP_(header, header->length)) return true;
    this->app()->mx()->txRun([header = ZuMv(header)] {
      header->endIO_(); header->postEnd_();
    });
    return false;
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  bool partTCP_(ZmRef<TCPHeader> header, RetainedEntry entry) {
    if (!entry.buf || entry.buf->failed()) return false;
    unsigned length = entry.buf->length;
    if (!length) return true;
    if (header->ioEnded.load_()) return false;
    ++header->posts;
    auto mx = header->mx;
    mx->txRun([header = ZuMv(header), entry = ZuMv(entry), length]() mutable {
      if (!header->ioEnded.load_()) {
	// Only I/O Tx changes queue storage; reclaim consumed metadata promptly.
	if (header->cursor && header->cursor >= header->entries.length() / 2) {
	  header->entries.shift(header->cursor); header->cursor = 0;
	}
	header->entries.push(ZuMv(entry));
	TCPTxOps::wake<Impl>(header.ptr());
      }
      entry.buf = nullptr; // no source capture can outlive the terminal ACK
      TCPTxOps::postAck_<Impl>(ZuMv(header), length);
    });
    return true;
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  void endTCP_(ZmRef<TCPHeader> header) {
    if (header->ioEnded.load_()) return;
    ++header->posts;
    auto mx = header->mx;
    mx->txRun([header = ZuMv(header)]() mutable {
      if (!header->ioEnded.load_()) {
	header->ended = true;
	TCPTxOps::wake<Impl>(header.ptr());
      }
      TCPTxOps::postAck_(ZuMv(header));
    });
  }
  template <bool AppThread>
  auto frameTx_() {
    if constexpr (Traits::ID == Transport::TCP)
      return TCPStream<Impl, AppThread>{*impl()};
    else if constexpr (AppThread) return this->txStream();
    else return this->txStream_();
  }

  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  void initTCP_(TCPHeader *header) {
    header->mx = this->app()->mx();
    header->terminalPost = [](ZmRef<TCPHeader> header) {
      auto mx = header->mx;
      auto thread = header->appThread;
      mx->run([header = ZuMv(header)]() mutable {
	header->appTerminal = true;
	auto ack = header->ownerAck;
	ack(ZuMv(header));
      }, thread);
    };
    header->ownerAck = [](ZmRef<TCPHeader> header) {
      auto link = static_cast<Impl *>(header->link.ptr()); // reserved capability
      link->ackTCP_(ZuMv(header));
    };
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  void registerTCP_(TCPHeader *) { ++m_tcp.count; }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  void ackTCP_(ZmRef<TCPHeader> header) {
    unsigned pending = --header->posts;
    if (!header->appTerminal || pending) return;
    ZmAssert(header->ioEnded.load_() && !header->appSettled && m_tcp.count);
    header->appSettled = true;
    // Move the native-link hold to Rx BEFORE dropping any Cxn hold. Final link
    // destruction follows the same Rx ownership as normal disconnected drain.
    auto link = ZmRef<Impl>::acquire(static_cast<Impl *>(ZuMv(header->link).release()));
    auto mx = this->app()->mx();
    auto rx = this->app()->rxThread();
    header->detach_();
    decltype(m_tcp.drains) fns;
    bool drained = !--m_tcp.count;
    if (drained) {
      fns = ZuMv(m_tcp.drains);
      m_tcp.drains.init_();
    }
    // Release this descriptor before signaling the App-owned logical drain.
    // Backend retirement copies still retain their own actual source handles;
    // physical disconnected drain precedes allocator-owner stop completion.
    header = nullptr;
    if (drained) impl()->tcpDrained_();
    // LAST App-owner operation. Release our extra hold on Rx before invoking
    // the existing drain callbacks, whose own release/completion order stands.
    mx->run([link = ZuMv(link), fns = ZuMv(fns)]() mutable {
      link = nullptr;
      for (auto &fn : fns) {
	auto notify = fn.notify;
	notify(ZuMv(fn.link), fn.peer);
      }
    }, rx);
  }
  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  bool pendingTCP_() const { return m_tcp.count; }
  void drainTCP_(bool peer, typename TCPDrain<Impl>::Notify notify) {
    auto app = this->app();
    app->txRun([link = ZmRef(impl()), peer, notify]() mutable {
      if constexpr (Traits::ID == Transport::TCP) {
	if (link->m_tcp.count) {
	  link->m_tcp.drains.push(TCPDrain<Impl>{ZuMv(link), notify, peer});
	  return;
	}
      }
      auto app = link->app();
      app->mx()->run([link = ZuMv(link), peer, notify]() mutable {
	notify(ZuMv(link), peer);
      }, app->rxThread());
    });
  }

  template <typename T = Traits, typename = ZuIfT<T::ID == Transport::TCP>>
  bool sendRetained_(RetainedEntries entries) {
    ZiAssert(this->app()->txInvoked(), "Zhttp", (),
      "retained TCP message outside Tx thread", return false);
    if (!entries.length()) return false;
    auto first = entries.shift();
    unsigned bytes = first.buf->length;
    for (const auto &part : entries) {
      uint64_t n = part.buf->length;
      if (n > UINT_MAX - bytes) return false;
      bytes += unsigned(n);
    }
    // begin() serialized this first HTTP header through our TCP allocator.
    auto header = ZmRef<TCPHeader>::acquire(
      static_cast<TCPHeader *>(ZuMv(first.buf).release()));
    header->entries = ZuMv(entries);
    header->appThread = this->app()->txThread();
    header->link = ZmRef<ZmPolymorph>{impl()};
    header->owner = reinterpret_cast<void *>(uintptr_t(impl()) | 1);
    initTCP_(header.ptr());
    registerTCP_(header.ptr());
    if (sendTCP_(header, bytes)) return true;
    this->app()->mx()->txRun([header = ZuMv(header)] {
      header->endIO_(); header->postEnd_();
    });
    return false;
  }

private:
  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const TCPProtocol *link, const Ztc::Queue *native) :
      m_link{link}, m_native{native} { }
    TelKey telKey() const override { return m_native->telKey(); }
    void telemetry(Ztc::QueueTelemetry &data) const override {
      m_native->telemetry(data);
      data.inCount += m_link->m_tcp.inCount.load_();
      data.inBytes += m_link->m_tcp.inBytes.load_();
      data.count += m_link->m_tcp.io.count.load_();
    }
  private:
    const TCPProtocol *m_link;
    const Ztc::Queue *m_native;
  };
  [[no_unique_address]] TCPActorT<Impl, Traits> m_tcp;
};

template <typename App, typename Impl, typename Traits>
using ClientProtocol = TCPProtocol<
  typename Traits::template ClientLink<App, Impl>, Impl, Traits>;
template <typename App, typename Impl, typename Traits>
using ServerProtocol = TCPProtocol<
  typename Traits::template ServerLink<App, Impl>, Impl, Traits>;


// The optional H1 TLS close capability is armed once on I/O Tx, after the
// final record submissions. It never participates in allocation or data sends.
template <typename Impl>
class TLSClose {
public:
  void closeAfterTx_() {
    closeLink_()->app()->rxInvoke([link = ZmRef(closeLink_())]() mutable {
      auto cxn = ZmRef{link->cxn()}; // exact physical capability, Rx-owned lookup
      if (!cxn) return;
      cxn->send(ZiTxFn{ZuMv(link), [](Impl *link, ZiTxContext &io) {
	if (!io.cxn->up()) return false;
	auto self = static_cast<TLSClose *>(link);
	self->m_end = ZmRef{io.cxn};
	self->closeTLSIO_(io);
	return false;
      }});
    });
  }
  void sentTLS_(bool ok) {
    if (!m_end) return;
    if (!ok) { m_end = nullptr; return; } // core aborts the failed physical send
    // Same-owner control; the actual retirement already retains the Cxn.
    // A stateless bound callback allocates no Fn closure or submission node.
    m_end->send_(ZiTxFn{closeLink_(), [](Impl *link, ZiTxContext &io) {
      static_cast<TLSClose *>(link)->closeTLSIO_(io);
      return false;
    }});
  }
  void endedTLS_() {
    // Quiescent terminal handoff: the existing physical I/O cleanup fence has
    // passed and Rx cleared cxn(), preventing later end controls from arming.
    m_end = nullptr;
  }
private:
  auto closeLink_() { return static_cast<Impl *>(this); }
  void closeTLSIO_(ZiTxContext &io) {
    if (m_end.ptr() != io.cxn) return;
    if (!io.cxn->up()) { m_end = nullptr; return; }
    if (io.buf || closeLink_()->txQueue.count_()) return;
    m_end = nullptr;
    closeLink_()->streamTxClose_();
  }
  ZmRef<ZiConnection> m_end; // I/O Tx; one cold handle per closing TLS link
};
struct NoTLSClose { };

} // namespace Transport_

template <
  typename Profile_,
  typename = ZuIfT<IsProfile<Profile_>{}>>
struct NativeTraits {
  using Profile = Profile_;
  using Protocol = Profile::Protocol;
  using Transport = Transport_::Traits<Protocol>;

  enum {
    HTTPVersion = Profile::HTTPVersion,
    Multiplexed = Profile::Multiplexed
  };

  static ConnectedInfo apply(ConnectedInfo connected) {
    connected.httpVersion = HTTPVersion;
    connected.multiplexed = Multiplexed;
    return connected;
  }
  static ConnectedInfo connected(Transport::Connected info) {
    return apply(Transport::connected(ZuMv(info)));
  }
};

namespace Stream_ {

struct Frame {
  int64_t operator ()(ZuSpan<uint8_t>) const;
};
struct Data {
  void operator ()(ZuSpan<uint8_t>) const;
};
struct Tx {
  template <typename Stream,
    typename = decltype(ZuDeclVal<Stream &>().flush(), void())>
  void operator ()(Stream &stream) const;
};

template <typename Consumer, typename Stream,
  typename = decltype(ZuDeclVal<Consumer &>().peerEnd(ZuMv(ZuDeclVal<Stream &>())), void())>
void peerEnd(Consumer &consumer, Stream stream, int)
{
  consumer.peerEnd(ZuMv(stream));
}
template <typename Consumer, typename Stream>
void peerEnd(Consumer &, Stream, ...) { }

template <typename Consumer, typename Stream,
  typename = decltype(ZuDeclVal<Consumer &>().error(ZuMv(ZuDeclVal<Stream &>())), void())>
void error(Consumer &consumer, Stream stream, int)
{
  consumer.error(ZuMv(stream));
}
template <typename Consumer, typename Stream>
void error(Consumer &, Stream, ...) { }

template <typename Tx_, typename = void>
struct IsTx : public ZuFalse { };
template <typename Tx_>
struct IsTx<Tx_, decltype(
  ZuDeclVal<Tx_ &>().flush(), void())> : public ZuTrue { };

template <typename Link, typename = void>
struct IsLink : public ZuFalse { };
template <typename Link>
struct IsLink<Link, decltype(
  ZuDeclVal<Link &>().streamLocalCap(),
  ZuDeclVal<Link &>().streamPeerCap(),
  ZuDeclVal<Link &>().streamTx(Tx{}),
  ZuDeclVal<Link &>().streamTxEnd(),
  ZuDeclVal<Link &>().streamTxReset(),
  void())> : public ZuTrue { };

template <typename Rx, typename = void>
struct IsRx : public ZuFalse { };
template <typename Rx>
struct IsRx<Rx, decltype(
  ZuDeclVal<Rx &>().consume(Frame{}, Data{}),
  ZuDeclVal<Rx &>().empty(),
  ZuDeclVal<Rx &>().length(),
  void())> : public ZuTrue { };

template <typename Link>
class Base {
public:
  Base(Link &link) : m_link{&link} {
    ZuAssert(IsLink<Link>{}, "invalid Zhttp logical-stream link");
  }

  bool localCap() const { return m_link->streamLocalCap(); }
  bool peerCap() const { return m_link->streamPeerCap(); }

  template <typename L>
  void txStream(L &&l) {
    m_link->streamTx([&l](auto &tx) {
      ZuAssert(
	Stream_::IsTx<ZuDecay<decltype(tx)>>{},
	"invalid Zhttp logical-stream Tx stream");
      ZuFwd<L>(l)(tx);
    });
  }
  template <typename L>
  void txStream_(L &&l) {
    m_link->streamTx_([&l](auto &tx) {
      ZuAssert(
	Stream_::IsTx<ZuDecay<decltype(tx)>>{},
	"invalid Zhttp logical-stream Tx stream");
      ZuFwd<L>(l)(tx);
    });
  }

  void end() { m_link->streamTxEnd(); }
  void reset() { m_link->streamTxReset(); }

private:
  Link	*m_link;
};

} // namespace Stream_

// Non-owning, shard-affine facade.  Link and callback-scoped Tx objects
// outlive each call only; txStream() is not transport-send completion.
template <typename Link>
class Stream : public Stream_::Base<Link> {
  using Base = Stream_::Base<Link>;

public:
  Stream(Link &link) : Base{link} { }
};

template <typename Link>
Stream(Link &) -> Stream<Link>;

// Rx-owner adapter.  process() synchronously prompts the consumer with the
// populated queue; unread bytes remain queued.  Peer end and reset/error are
// separate callbacks.  Disable ingress on the Rx shard before disable_(),
// drain pending Rx work, then call final_() from that drain continuation.
template <typename Link, typename Consumer>
class StreamDispatch {
public:
  void init(Link &link, Consumer &consumer) {
    ZmAssert(!m_link && !m_consumer);
    m_link = &link;
    m_consumer = &consumer;
  }

  template <typename Rx>
  int process(Rx &rx) {
    ZuAssert(
      Stream_::IsRx<Rx>{}, "invalid Zhttp logical-stream Rx queue");
    if (!m_link || !m_consumer) return 0;
    auto link = m_link;
    int rc = m_consumer->process(Stream{*link}, rx);
    if (rc < 0) {
      m_link = nullptr;
      Stream{*link}.reset();
    }
    return rc;
  }

  void peerEnd() {
    if (!m_link || !m_consumer) return;
    auto link = m_link;
    m_link = nullptr;
    Stream_::peerEnd(*m_consumer, Stream{*link}, 0);
  }
  void error() {
    if (!m_link || !m_consumer) return;
    auto link = m_link;
    m_link = nullptr;
    Stream_::error(*m_consumer, Stream{*link}, 0);
  }

  void disable_() { m_link = nullptr; }
  void final_() {
    ZmAssert(!m_link);
    m_consumer = nullptr;
  }

private:
  Link		*m_link = nullptr;
  Consumer	*m_consumer = nullptr;
};

namespace H1 {

struct StreamState {
  ZtEnumValues(int8_t, HTTP, Stream, Terminal);
};
struct StreamTerm {
  enum { None, End, Reset };
};

// Rx-owned Upgrade state and application dispatch.  Transport teardown has
// already drained Rx and Tx when the generic link calls disconnected().
template <typename Link, typename Consumer>
class StreamBinding {
public:
  using State = StreamState;
  using Term = StreamTerm;

  bool enable(bool enabled) {
    if (m_state != State::HTTP) return false;
    m_enabled = enabled;
    return true;
  }
  bool accept(Link &link, Consumer &consumer) {
    if (!m_enabled || m_state != State::HTTP) return false;
    m_dispatch.init(link, consumer);
    m_peerCap = true;
    m_state = State::Stream;
    return true;
  }

  bool localCap() const { return m_enabled; }
  bool peerCap() const { return m_peerCap; }
  bool stream() const { return m_state == State::Stream; }
  bool terminal() const { return m_state == State::Terminal; }
  bool tx() const { return m_enabled && m_state != State::Terminal; }

  template <typename Rx>
  int process(Rx &rx) { return m_dispatch.process(rx); }

  void reopen() {
    m_state = State::HTTP;
    m_term = Term::None;
    m_enabled = false;
    m_peerCap = false;
  }

  bool end() {
    if (m_state != State::Stream) return false;
    m_term = Term::End;
    m_state = State::Terminal;
    return true;
  }
  bool reset() {
    if (m_term == Term::Reset) return false;
    m_term = Term::Reset;
    m_state = State::Terminal;
    return true;
  }

  void disconnected(bool peer) {
    if (m_peerCap && m_state == State::Stream) {
      if (peer)
	m_dispatch.peerEnd();
      else
	m_dispatch.error();
    }
    m_state = State::Terminal;
    m_dispatch.disable_();
    m_dispatch.final_();
  }

private:
  StreamDispatch<Link, Consumer>	m_dispatch;
  State::T			m_state = State::HTTP;
  uint8_t			m_term = Term::None;
  bool				m_enabled = false;
  bool				m_peerCap = false;
};

} // namespace H1

template <typename Link, typename Builder>
auto streamMessageTx(Link &link, Builder &builder,
    ZmRef<Transport_::TCPHeader> &head, int) -> decltype(link.transmit_(builder, head)) {
  return link.transmit_(builder, head);
}
template <typename Link, typename Builder>
auto streamMessageTx(Link &link, Builder &builder,
    ZmRef<Transport_::TCPHeader> &, ...) {
  return link.transmit_(builder);
}
template <typename Tx>
auto finishTCPStream(Tx &tx, int) -> decltype(tx.endTCP_(), void()) { tx.endTCP_(); }
template <typename Tx>
void finishTCPStream(Tx &, ...) { }

namespace Link_ {

template <typename Consumer, typename Stream, typename Rx,
  typename = decltype(int(ZuDeclVal<Consumer &>().process(ZuMv(ZuDeclVal<Stream &>()),
    ZuDeclVal<Rx &>())))>
int process(Consumer &consumer, Stream stream, Rx &rx, int)
{
  return int(consumer.process(ZuMv(stream), rx));
}
template <typename Consumer, typename Stream, typename Rx>
int process(Consumer &, Stream, Rx &, ...) { return 0; }

template <typename Consumer, typename Stream,
  typename = decltype(ZuDeclVal<Consumer &>().peerEnd(ZuMv(ZuDeclVal<Stream &>())), void())>
void peerEnd(Consumer &consumer, Stream stream, int)
{
  consumer.peerEnd(ZuMv(stream));
}
template <typename Consumer, typename Stream>
void peerEnd(Consumer &, Stream, ...) { }

template <typename Consumer, typename Stream,
  typename = decltype(ZuDeclVal<Consumer &>().error(ZuMv(ZuDeclVal<Stream &>())), void())>
void error(Consumer &consumer, Stream stream, int)
{
  consumer.error(ZuMv(stream));
}
template <typename Consumer, typename Stream>
void error(Consumer &, Stream, ...) { }

} // namespace Link_

template <typename App, typename Impl, typename Profile>
class ClientLink :
  public Transport_::ClientProtocol<
    App, Impl, typename NativeTraits<Profile>::Transport> {
  using HTTP = NativeTraits<Profile>;
  using Traits = HTTP::Transport;
  using Base = Transport_::ClientProtocol<App, Impl, Traits>;
  using StreamBinding =
    H1::StreamBinding<Impl, Impl>;

public:
  using Base::Base;
  enum { TLS = Traits::Secure, Multiplexed = HTTP::Multiplexed };

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Zhttp::Session channelSession() const {
    return {uintptr_t(this->cxn()), Traits::ID};
  }
  uint64_t channelStreamID() const { return 0; }

  template <typename ...Args>
  void connect(Args &&...args) {
    m_stream.reopen();
    this->app()->txRun([link = ZmRef(impl())]() {
      link->streamClosedTx_();
    });
    m_connected = false;
    m_failed = false;
    m_cancelled = false;
    Base::connect(ZuFwd<Args>(args)...);
  }
  template <typename Endpoint>
  void connectEndpoint(const Endpoint &endpoint) {
    connect(endpoint.target, endpoint.port);
  }
  void connected(Traits::Connected info) {
    if (m_cancelled) {
      Traits::disconnect(*this);
      return;
    }
    m_connected = true;
    this->app()->connected(*impl(), HTTP::connected(ZuMv(info)));
  }
  void disconnected(bool peer) {
    if (m_connected) {
      m_connected = false;
      m_failed = true;
      if constexpr (Traits::ID == Transport::TCP) {
	this->drainTCP_(peer, [](ZmRef<Impl> link, bool peer) {
	  link->m_stream.disconnected(peer);
	  link->app()->txRun([link = ZuMv(link), peer]() mutable {
	    link->streamClosedTx_();
	    auto app = link->app();
	    app->rxRun([link = ZuMv(link), peer] {
	      link->app()->disconnected(*link, peer);
	    });
	  });
	});
      } else {
	m_stream.disconnected(peer);
	this->app()->txRun([link = ZmRef(impl())]() { link->streamClosedTx_(); });
	this->app()->disconnected(*impl(), peer);
      }
    } else if (!m_failed) {
      m_failed = true;
      this->app()->connectFailed(*impl(), false);
    }
  }
  void connectFailed(bool transient) {
    if (m_failed) return;
    m_failed = true;
    this->app()->connectFailed(*impl(), transient);
  }
  void disconnect() {
    this->app()->rxInvoke([link = ZmRef(impl())]() { link->disconnectRx_(); });
  }
  ZuInline int process(Traits::RxStream &rx) {
    if (m_stream.stream()) return m_stream.process(rx);
    if (m_stream.terminal()) return 0;
    int rc = this->app()->process(*impl(), rx);
    if (!m_stream.stream()) return rc;
    int streamRC = m_stream.process(rx);
    return streamRC ? streamRC : rc;
  }
  // Negotiation and borrowed application-thread writers are Rx-affine.
  bool streamEnable(bool enabled = true) {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.enable(enabled)) return false;
    this->app()->txRun([link = ZmRef(impl()), enabled]() {
      link->streamEnableTx_(enabled);
    });
    return true;
  }
  bool streamAccept() {
    ZmAssert(this->app()->rxInvoked());
    return m_stream.accept(*impl(), *impl());
  }
  bool streamLocalCap() const { return m_stream.localCap(); }
  bool streamPeerCap() const { return m_stream.peerCap(); }
  template <typename L>
  void streamTx(L &&l) {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.tx()) return;
    auto tx = this->template frameTx_<true>();
    ZuFwd<L>(l)(tx);
  }
  template <typename L>
  void streamTx_(L &&l) {
    ZmAssert(this->app()->txInvoked());
    if (!m_streamTx) return;
    auto tx = this->template frameTx_<false>();
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() {
    this->app()->rxInvoke([link = ZmRef(impl())]() {
      link->streamEndRx_();
    });
  }
  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    Base::sent(ZuMv(buf), ok);
    if constexpr (Traits::ID != Transport::TCP &&
	Traits::ID != Transport::TLS) {
      if (!m_streamEnd || (ok && this->txQueue.count_())) return;
      m_streamEnd = false;
      streamTxClose_();
    }
  }
  void tcpDrained_() {
    ZmAssert(this->app()->txInvoked());
    if (!m_streamEnd) return;
    m_streamEnd = false;
    streamTxClose_();
  }
  void streamTxReset() {
    this->app()->rxInvoke([link = ZmRef(impl())]() {
      link->streamResetRx_();
    });
  }
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return impl()->channel().frameInput(rx);
  }
  template <typename Stream>
  void peerEnd(Stream stream) {
    impl()->channel().frameEnd();
  }
  template <typename Stream>
  void error(Stream stream) {
    impl()->channel().frameError();
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) {
    return parser.process(rx);
  }
  template <typename Builder>
  auto transmit(Builder &) {
    return this->template frameTx_<true>();
  }
  using Base::transmit_;
  template <typename Builder>
  auto transmit_(Builder &) {
    // Bodyless H1 output participates in the same close drain as body output.
    return this->template frameTx_<false>();
  }

  void finish() { }
  bool active() const { return !!this->cxn(); }
  template <typename State>
  void responseHeadersParsed(State *) { }
  template <typename State>
  void responseBodyBytes(State *) { }

private:
  void disconnectRx_() {
    ZmAssert(this->app()->rxInvoked());
    m_cancelled = true;
    Traits::disconnect(*this);
  }
  void streamClosedTx_() {
    ZmAssert(this->app()->txInvoked());
    m_streamTx = m_streamEnd = false;
  }
  void streamEnableTx_(bool enabled) {
    ZmAssert(this->app()->txInvoked());
    m_streamTx = enabled;
  }
  void streamEndRx_() {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.end()) return;
    if constexpr (Traits::ID == Transport::TLS) {
      auto cxn = ZmRef{this->cxn()};
      if (!cxn) return;
      this->app()->txRun([
	link = ZmRef(impl()), cxn = ZuMv(cxn)]() mutable {
	link->streamTxEnd_(ZuMv(cxn));
      });
    } else {
      this->app()->txRun([link = ZmRef(impl())]() {
	link->streamTxEnd_();
      });
    }
  }
  void streamResetRx_() {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.reset()) return;
    this->app()->txRun([link = ZmRef(impl())]() {
      link->streamEnableTx_(false);
      link->app()->rxRun([link]() { link->disconnect(); });
    });
  }
  void streamTxEnd_(ZmRef<ZiConnection> cxn = {}) {
    ZmAssert(this->app()->txInvoked());
    m_streamTx = false;
    if constexpr (Traits::ID != Transport::TLS) {
      bool pending;
      if constexpr (Traits::ID == Transport::TCP) pending = this->pendingTCP_();
      else pending = this->txQueue.count_();
      if (pending) {
	m_streamEnd = true;
	return;
      }
    }
    // TLS disconnect queues close_notify after preceding App Tx records and
    // drains its actual wire buffer on I/O Tx before closing the frozen Cxn.
    streamTxClose_(ZuMv(cxn));
  }
  void streamTxClose_(ZmRef<ZiConnection> cxn = {}) {
    auto app = this->app();
    app->rxRun([link = ZmRef(impl()), cxn = ZuMv(cxn)]() mutable {
      if constexpr (Traits::ID == Transport::TLS)
	if (link->cxn() != cxn.ptr()) return;
      Traits::disconnect(*link);
    });
  }

  alignas(Zm::CacheLineSize)
  StreamBinding	m_stream;
  bool		m_connected = false;
  bool		m_failed = false;
  bool		m_cancelled = false;
  alignas(Zm::CacheLineSize)
  bool		m_streamTx = false;	// Tx-owned
  bool		m_streamEnd = false;	// Tx-owned
};

using ServerDrainFn = ZmFn<void(), ZmFnHeapID<"Zhttp.ServerDrain">>;

template <
  typename App, typename Impl, typename Profile, typename Session>
class ServerLink :
  public Transport_::ServerProtocol<
    App, Impl, typename NativeTraits<Profile>::Transport>,
  public ZuIf<NativeTraits<Profile>::Transport::ID == Transport::TLS,
    Transport_::TLSClose<Impl>, Transport_::NoTLSClose> {
  using HTTP = NativeTraits<Profile>;
  using Traits = HTTP::Transport;
  using Base = Transport_::ServerProtocol<App, Impl, Traits>;
  friend Transport_::TLSClose<Impl>;
  using StreamBinding =
    H1::StreamBinding<Impl, Impl>;

public:
  enum { TLS = Traits::Secure, Multiplexed = HTTP::Multiplexed };

  ServerLink(App *app, const ZiCxnInfo &ci) :
    Base{app}, m_remoteIP{ci.remote.ip()}, m_remotePort{ci.remote.port()}
  {
#ifdef ZmObject_DEBUG
    this->ZmPolymorph::debug();
#endif
  }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  const ZiIP &remoteIP() const { return m_remoteIP; }
  uint16_t remotePort() const { return m_remotePort; }
  Zhttp::Session session() const {
    return {uintptr_t(impl()), HTTP::Transport::ID};
  }
  Session &rxState() { return m_session; }
  const Session &rxState() const { return m_session; }

  void connected(Traits::Connected info) {
    m_session.connected(*impl());
    this->app()->connected(*impl(), HTTP::connected(ZuMv(info)));
    touch();
  }
  void disconnected(bool peer) {
    if (m_disconnected) return;
    m_disconnected = true;
    this->app()->mx()->del(&m_idleTimer);
    this->app()->mx()->run([
      link = ZmRef(impl()), peer]() mutable {
	link->notifyDisconnected_(peer);
      }, this->app()->rxThread());
  }
  void notifyDisconnected_(bool peer) {
    if constexpr (Traits::ID == Transport::TLS) this->endedTLS_();
    if constexpr (Traits::ID == Transport::TCP) {
      this->drainTCP_(peer, [](ZmRef<Impl> link, bool peer) {
	link->notifyAfterTCP_(peer);
      });
    } else notifyAfterTCP_(peer);
  }
  void notifyAfterTCP_(bool peer) {
    m_stream.disconnected(peer);
    m_session.disconnected(*impl(), peer);
    this->app()->disconnected(*impl(), peer);
    // Response cleanup is Tx-owned. Release its final native reference on Rx
    // before reporting completion to the hub's stop/registry machinery.
    impl()->appDrained(ServerDrainFn::mvFn(ZmRef(impl()), [](ZmRef<Impl> link) {
      link->app()->txRun([link = ZuMv(link)]() mutable {
	link->streamClosedTx_();
	auto app = link->app();
	app->rxRun([link = ZuMv(link)]() mutable {
	  link->releaseDisconnected_();
	  auto app = link->app();
	  link = nullptr;
	  app->linkDrained_();
	});
      });
    }));
  }
  // Applications with deferred teardown complete this hook back on Rx.
  void appDrained(ServerDrainFn done) { done(); }
  ZuInline int process(Traits::RxStream &rx) {
    if (m_stream.stream()) return m_stream.process(rx);
    if (m_stream.terminal()) return 0;
    int rc = m_session.process(*impl(), rx);
    if (m_stream.stream()) {
      int streamRC = m_stream.process(rx);
      if (streamRC) rc = streamRC;
    }
    if (rc >= 0) touch();
    return rc;
  }
  // Negotiation and borrowed application-thread writers are Rx-affine.
  bool streamEnable(bool enabled = true) {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.enable(enabled)) return false;
    this->app()->txRun([link = impl(), enabled]() {
      link->streamEnableTx_(enabled);
    });
    return true;
  }
  bool streamAccept() {
    ZmAssert(this->app()->rxInvoked());
    return m_stream.accept(*impl(), *impl());
  }
  bool streamLocalCap() const { return m_stream.localCap(); }
  bool streamPeerCap() const { return m_stream.peerCap(); }
  template <typename L>
  void streamTx(L &&l) {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.tx()) return;
    auto tx = this->template frameTx_<true>();
    ZuFwd<L>(l)(tx);
  }
  template <typename L>
  void streamTx_(L &&l) {
    ZmAssert(this->app()->txInvoked());
    if (!m_streamTx) return;
    auto tx = this->template frameTx_<false>();
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() {
    this->app()->rxInvoke([link = ZmRef(impl())]() {
      link->streamEndRx_();
    });
  }
  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    Base::sent(ZuMv(buf), ok);
    if constexpr (Traits::ID == Transport::TCP) return;
    if constexpr (Traits::ID == Transport::TLS) this->sentTLS_(ok);
    else {
      if (!m_streamEnd || (ok && this->txQueue.count_())) return;
      m_streamEnd = false;
      streamTxClose_();
    }
  }
  void tcpDrained_() {
    ZmAssert(this->app()->txInvoked());
    if (!m_streamEnd) return;
    m_streamEnd = false;
    streamTxClose_();
  }
  void streamTxReset() {
    this->app()->rxInvoke([link = ZmRef(impl())]() {
      link->streamResetRx_();
    });
  }
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return impl()->channel().frameInput(rx);
  }
  template <typename Stream>
  void peerEnd(Stream stream) {
    impl()->channel().frameEnd();
  }
  template <typename Stream>
  void error(Stream stream) {
    impl()->channel().frameError();
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) {
    return parser.process(rx);
  }
  template <typename Builder>
  auto transmit(Builder &) {
    return this->template frameTx_<true>();
  }
  using Base::transmit_;
  template <typename Builder>
  auto transmit_(Builder &) {
    // Bodyless H1 output participates in the same close drain as body output.
    return this->template frameTx_<false>();
  }

  bool active() const { return !!this->cxn(); }
  void finish() { }
  void touch() {
    if (m_disconnected) return;
    auto timeout = this->app()->idleTimeout();
    if (!timeout) return;
    this->app()->mx()->add(
      &m_idleTimer, Zm::now(timeout), ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([link = impl()]() { link->disconnect(); });
      }, this->app()->rxThread());
  }

private:
  void streamClosedTx_() {
    ZmAssert(this->app()->txInvoked());
    m_streamTx = false;
    if constexpr (Traits::ID != Transport::TLS) m_streamEnd = false;
  }
  void releaseDisconnected_() {
    if (m_counted) {
      m_counted = false;
      this->app()->release();
    }
  }
  void streamEnableTx_(bool enabled) {
    ZmAssert(this->app()->txInvoked());
    m_streamTx = enabled;
  }
  void streamEndRx_() {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.end()) return;
    this->app()->txRun([link = impl()]() {
      link->closeAfterTx_();
    });
  }
  void streamResetRx_() {
    ZmAssert(this->app()->rxInvoked());
    if (!m_stream.reset()) return;
    this->app()->txRun([link = impl()]() {
      link->streamEnableTx_(false);
      link->app()->rxRun([link]() { link->disconnect(); });
    });
  }
public:
  void closeAfterTx_() {
    ZmAssert(this->app()->txInvoked());
    m_streamTx = false;
    if constexpr (Traits::ID == Transport::TLS)
      Transport_::TLSClose<Impl>::closeAfterTx_();
    else {
      bool pending;
      if constexpr (Traits::ID == Transport::TCP) pending = this->pendingTCP_();
      else pending = this->txQueue.count_();
      if (pending) { m_streamEnd = true; return; }
      streamTxClose_();
    }
  }
private:
  void streamTxClose_() {
    auto app = this->app();
    app->rxRun([link = impl()]() {
      Traits::disconnect(*link);
    });
  }

  ZiIP			m_remoteIP;
  uint16_t		m_remotePort = 0;
  alignas(Zm::CacheLineSize)
  Session		m_session;
  ZmScheduler::Timer	m_idleTimer;
  StreamBinding		m_stream;
  bool			m_counted = true;
  bool			m_disconnected = false;
  alignas(Zm::CacheLineSize)
  bool			m_streamTx = false;	// Tx-owned
  ZuCanOverlap ZuIf<Traits::ID == Transport::TLS,
    Transport_::NoTLSClose, bool> m_streamEnd{}; // App Tx for cleartext
};

namespace Native_ {

// Embedded in a Tx-owned producer. The native queue retains the containing
// allocation through this record; callbacks invalidate raw link back-pointers.
struct FinalCmd : MessageRecord {
  void (*emit)(void *);
  void (*cancel)(void *);
};

// Tx-owned immutable final fields. Providers and printables are borrowed only
// while render() runs; encoders see owned spans when the DATA prefix drains.
class ZhttpAPI FinalFields {
public:
  bool render(unsigned, const CatalogView &, FieldProvider &);
  void fields(FieldOutput &) const;

private:
  struct Field {
    unsigned name, nameLen, value, valueLen;
    int id;
    FieldClass classification;
    FieldForm::T form;
    bool never;
  };
  using Storage = ZtArray<Field, ZtArrayHeapID<"Zhttp.Final.Fields">>;
  using Alloc = Zi::IOBufBuiltin<
    ZiRxQueue::Node,
    ZiIOBuf_DefltSize,
    ZiIOBuf_DefltAlign,
    ZuStringT<"Zhttp.Final.Bytes">,
    ZiIOBuf_DefltMaxSize>;

  static const WriterOps &writerOps_();
  ZuSpan<uint8_t> reserve_(unsigned);
  void commit_(unsigned);
  void append_(ZuBSpan);
  void field_(const CatalogView &, FieldForm::T, int, const FieldClass *,
    PrintView, PrintView, bool);

  Storage m_fields;
  ZmRef<ZiIOBuf> m_bytes;
  unsigned m_limit = 0;
  unsigned m_cost = 0;
  unsigned m_reserved = 0;
  bool m_failed = false;
};

template <typename Emit>
struct BorrowedEmit {
  BodyEmitRecord record;
  Emit &emit;

  BorrowedEmit(Emit &emit_) : emit{emit_} {
    static constexpr RecordOps refs{
      [](const void *, const void *) { },
      [](const void *, const void *) { },
      [](const void *, const void *, const void *) { }};
    record.ctx = this;
    record.ops = &refs;
    record.emit = [](void *ctx, BodyWrite write) {
      auto &emit = static_cast<BorrowedEmit *>(ctx)->emit;
      using R = decltype(emit(write));
      if constexpr (ZuIsSame<R, void>{}) { emit(write); return true; }
      else return emit(write);
    };
  }
};

} // namespace Native_

namespace Native_ {

template <typename Stream,
  typename = decltype(ZuDeclVal<const Stream &>().produced())>
decltype(auto) produced(const Stream &stream, int) {
  return stream.produced();
}
template <typename Stream>
uint64_t produced(const Stream &, long) { return 0; }

template <typename Stream,
  typename = decltype(ZuDeclVal<const Stream &>().valid())>
bool failed(const Stream &stream, int) {
  return !stream.valid();
}
template <typename Stream>
bool failed(const Stream &stream, long) { return stream.failed(); }

template <typename Stream, typename = void>
struct NeedsProvenance : ZuFalse { };
template <typename Stream>
struct NeedsProvenance<Stream, decltype(Stream::RetainBuffers, void())> :
  ZuBool<Stream::RetainBuffers> { };

template <typename Stream, typename = decltype(
  ZuDeclVal<const Stream &>().allocatedBuf_(ZuDeclVal<const ZiIOBuf *>()))>
bool allocated(const Stream &stream, const ZiIOBuf *buf, int) {
  return stream.allocatedBuf_(buf);
}
template <typename Stream>
bool allocated(const Stream &, const ZiIOBuf *, ...) {
  return !NeedsProvenance<Stream>{};
}
template <typename Stream, typename = decltype(
  ZuDeclVal<Stream &>().allocatedBuf_(ZuDeclVal<ZmRef<ZiIOBuf>>()))>
bool allocated(Stream &stream, ZmRef<ZiIOBuf> buf, int) {
  return stream.allocatedBuf_(ZuMv(buf));
}
template <typename Stream>
bool allocated(Stream &, ZmRef<ZiIOBuf>, ...) {
  return !NeedsProvenance<Stream>{};
}

template <typename Stream>
auto owned(Stream &stream, ZmRef<ZiIOBuf> buf, int)
  -> decltype(stream.appendBuf(ZuMv(buf)))
{
  return stream.appendBuf(ZuMv(buf));
}
template <typename Stream>
bool owned(Stream &stream, ZmRef<ZiIOBuf> buf, ...)
{
  stream << ZuMv(buf);
  return !stream.failed();
}
template <typename Stream>
bool fits(const Stream &stream, const ZiIOBuf &buf, unsigned head, unsigned tail)
{
  if (head > buf.skip || tail > buf.avail()) return false;
  uint64_t reserve = uint64_t(stream.headRoom()) + stream.tailRoom();
  uint64_t length = uint64_t(buf.length) + head + tail;
  return reserve < stream.maxSize() && length <= stream.maxSize() - reserve &&
    buf.skip - head >= stream.headRoom() && buf.avail() - tail >= stream.tailRoom();
}

template <typename Stream>
const WriterOps &writerOps()
{
  static constexpr WriterOps ops{
    [](void *ctx, ZuBSpan span) { *static_cast<Stream *>(ctx) << span; },
    [](void *ctx, const BufferPrint &value) { *static_cast<Stream *>(ctx) << value; },
    [](void *ctx, ZmRef<ZiIOBuf> buf) { *static_cast<Stream *>(ctx) << ZuMv(buf); },
    [](void *ctx) {
      auto &stream = *static_cast<Stream *>(ctx);
      bool ok = stream.flush();
      return ok && !failed(stream, 0);
    },
    [](const void *ctx) { return failed(*static_cast<const Stream *>(ctx), 0); },
    [](const void *ctx) -> uint64_t {
      return produced(*static_cast<const Stream *>(ctx), 0);
    },
    [](const void *ctx) { return static_cast<const Stream *>(ctx)->maxSize(); },
    [](const void *ctx) { return static_cast<const Stream *>(ctx)->headRoom(); },
    [](const void *ctx) { return static_cast<const Stream *>(ctx)->tailRoom(); },
    [](void *ctx, unsigned head) { return static_cast<Stream *>(ctx)->allocBuf(head); },
    [](void *ctx, unsigned length) { return static_cast<Stream *>(ctx)->reserve(length); },
    [](void *ctx, unsigned length) { static_cast<Stream *>(ctx)->commit(length); },
    [](void *ctx, ZmRef<ZiIOBuf> buf) {
      return owned(*static_cast<Stream *>(ctx), ZuMv(buf), 0);
    },
    [](const void *ctx, const ZiIOBuf &buf, unsigned head, unsigned tail) {
      return fits(*static_cast<const Stream *>(ctx), buf, head, tail);
    },
    [](const void *ctx, const ZiIOBuf *buf) {
      return allocated(*static_cast<const Stream *>(ctx), buf, 0);
    },
    [](void *ctx, ZmRef<ZiIOBuf> buf) {
      return allocated(*static_cast<Stream *>(ctx), ZuMv(buf), 0);
    }};
  return ops;
}

} // namespace Native_

template <typename Stream>
WriteOutcome::T invokeBodyWriter(BodyWrite write, Stream &stream)
{
  Writer out{&stream, Native_::writerOps<Stream>()};
  auto result = write(out);
  if (!out.flush()) return WriteOutcome::Failed;
  return result;
}

namespace Native_ {

template <typename Link,
  typename = decltype(ZuDeclVal<const Link &>().streamID())>
decltype(auto) streamID(const Link &link, int) {
  return link.streamID();
}
template <typename Link>
uint64_t streamID(const Link &, long) { return 0; }

template <typename Link>
const ChannelOps &serverChannelOps()
{
  static constexpr ChannelOps ops{
    [](const void *ctx) { return static_cast<const Link *>(ctx)->app()->mx(); },
    [](const void *ctx) { return static_cast<const Link *>(ctx)->app()->rxThread(); },
    [](const void *ctx) { return static_cast<const Link *>(ctx)->app()->txThread(); },
    [](void *ctx) {
      auto link = ZmRef{static_cast<Link *>(ctx)};
      auto app = link->app();
      app->mx()->invoke([link = ZuMv(link)]() mutable { link->disconnect(); }, app->rxThread());
    },
    [](void *ctx, uint64_t sequence, bool ordered, ResponseRef response) {
      auto link = static_cast<Link *>(ctx);
      if (ordered) link->routeSend(sequence, ZuMv(response));
      else link->send(ZuMv(response));
    },
    [](const void *ctx) -> const CatalogView & {
      return static_cast<const Link *>(ctx)->app()->server->owner()->responseCatalog();
    },
    [](void *ctx, bool enabled) {
      return static_cast<Link *>(ctx)->streamEnable(enabled);
    },
    [](void *ctx) { return static_cast<Link *>(ctx)->streamAccept(); },
    [](void *ctx) { return static_cast<Link *>(ctx)->streamLocalCap(); },
    [](void *ctx) { return static_cast<Link *>(ctx)->streamPeerCap(); },
    [](const void *ctx) { return static_cast<const Link *>(ctx)->rxState().requestSequence(); },
    [](void *ctx, PrintView value) {
      bool ok = false;
      static_cast<Link *>(ctx)->streamTx_([value, &ok](auto &stream) {
	Writer out{&stream, writerOps<ZuDecay<decltype(stream)>>()};
	value.write(out);
	ok = out.flush();
      });
      return ok;
    },
    [](void *ctx) { static_cast<Link *>(ctx)->streamTxEnd(); },
    [](void *ctx) { static_cast<Link *>(ctx)->streamTxReset(); },
    [](const void *ctx) { return static_cast<const Link *>(ctx)->session(); },
    [](const void *ctx) -> uint64_t { return streamID(*static_cast<const Link *>(ctx), 0); },
    [](const void *ctx) -> const ZiIP & { return static_cast<const Link *>(ctx)->remoteIP(); },
    [](const void *ctx) { return static_cast<const Link *>(ctx)->remotePort(); },
    [](void *ctx, StreamCancelFn fn) {
      static_cast<Link *>(ctx)->responseCancel(ZuMv(fn));
    },
    [](const void *ctx) {
      if constexpr (!Link::Multiplexed)
	return static_cast<const Link *>(ctx)->rxState().parser.http10();
      else return false;
    },
    [](const void *ctx) {
      if constexpr (!Link::Multiplexed)
	return static_cast<const Link *>(ctx)->rxState().parser.bodyFramed();
      else return false;
    },
    [](void *ctx, PrintView value) {
      bool ok = false;
      static_cast<Link *>(ctx)->streamTx([value, &ok](auto &stream) {
	Writer out{&stream, writerOps<ZuDecay<decltype(stream)>>()};
	value.write(out);
	ok = out.flush();
      });
      return ok;
    }};
  return ops;
}

template <typename Link>
Channel serverChannel(Link *link)
{
  Channel channel{serverChannelOps<Link>()};
  channel.bind(link);
  return channel;
}

} // namespace Native_

namespace Native_ {

struct Print {
  PrintView value;

  template <typename Out>
  void print(Out &out) const {
    Writer writer{&out, bufferWriterOps<Out, false>()};
    value.write(writer);
  }
  friend ZuPrintFn ZuPrintType(Print *);
};

template <typename L>
void print(PrintView value, L &&l) {
  if (value.isSpan()) l(value.span);
  else l(Print{value});
}

} // namespace Native_

namespace Native_ {

template <typename Stream, typename Value,
  typename = decltype(ZuDeclVal<Stream &>().fieldMutable(
    ZuDeclVal<ZuBSpan>(), ZuDeclVal<const Value &>(), bool{}))>
FieldOffset mutableField(Stream &stream, ZuBSpan name, const Value &value, bool never, int) {
  auto span = stream.fieldMutable(name, value, never);
  return {span.offset, span.length};
}
template <typename Stream, typename Value>
FieldOffset mutableField(Stream &stream, ZuBSpan name, const Value &value, bool never, long) {
  stream.field(name, value, never);
  return {};
}

template <typename Stream, typename HeapID>
FieldOutput compressedFields(Stream &stream)
{
  return {&stream,
    [](void *ctx, FieldForm::T form, int, const FieldClass *,
	PrintView key, PrintView value, bool never) -> FieldOffset {
      auto &stream = *static_cast<Stream *>(ctx);
      auto emit = [&stream, form, value, never](ZuBSpan name) -> FieldOffset {
	// The old undeclared-header provider omitted empty names.
	if (form == FieldForm::Literal && !name) return {};
	FieldOffset span;
	print(value, [&stream, form, name, never, &span](const auto &value_) {
	  switch (form) {
	    case FieldForm::Fixed: stream.fieldFixed(name, value_, never); break;
	    case FieldForm::Name: stream.fieldName(name, value_, never); break;
	    case FieldForm::Literal: stream.fieldLiteral(name, value_, never); break;
	    case FieldForm::Mutable: span = mutableField(stream, name, value_, never, 0); break;
	  }
	});
	return span;
      };
      if (key.isSpan()) return emit(key.span);
      using Name = ZtBArray<ZtArrayHeapID<HeapID{}()>>;
      auto name = ZtScratch(Name, Compression::PrintScratchBuiltin);
      Writer out{&name, bufferWriterOps<decltype(name), true>()};
      key.write(out);
      return emit(name);
    }};
}

} // namespace Native_

// Native contexts keep the original endpoint lifetime, including after final().
// Explicit shutdown/final drains their dependents before endpoint destruction.
struct Backend {
  void *ctx = nullptr;
  void *allocation = nullptr;
  void (*destroy)(void *) = nullptr;
  void (*print)(void *) = nullptr;

  Backend() = default;
  Backend(const Backend &) = delete;
  Backend &operator =(const Backend &) = delete;
  ~Backend() { if (allocation) destroy(allocation); }

  template <typename Native>
  void own(Native *native) {
    ctx = allocation = native;
    // Native is the concrete allocation type; no deletion through a base.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
    destroy = [](void *ptr) { delete static_cast<Native *>(ptr); };
#pragma GCC diagnostic pop
  }
  void printDiag() { if (print) print(ctx); }
};

namespace Native_ {

template <typename Link>
const ChannelOps &clientChannelOps()
{
  static constexpr ChannelOps ops{
    .mx = [](const void *ctx) { return static_cast<const Link *>(ctx)->pool()->mx(); },
    .rxThread = [](const void *ctx) {
      return static_cast<const Link *>(ctx)->pool()->rxThread();
    },
    .txThread = [](const void *ctx) {
      return static_cast<const Link *>(ctx)->pool()->txThread();
    },
    .disconnect = [](void *ctx) {
      auto link = ZmRef{static_cast<Link *>(ctx)};
      auto pool = link->pool();
      // Close invalidates the retained producer on Tx before native teardown.
      pool->mx()->invoke([link = ZuMv(link)]() mutable { link->close(); }, pool->rxThread());
    },
    .send = nullptr,
    .responseCatalog = nullptr,
    .streamEnable = [](void *ctx, bool enabled) {
      return static_cast<Link *>(ctx)->streamEnable(enabled);
    },
    .streamAccept = [](void *ctx) { return static_cast<Link *>(ctx)->streamAccept(); },
    .streamLocalCap = [](void *ctx) {
      return static_cast<Link *>(ctx)->streamLocalCap();
    },
    .streamPeerCap = [](void *ctx) { return static_cast<Link *>(ctx)->streamPeerCap(); },
    .sequence = [](const void *ctx) -> uint64_t {
      return static_cast<const Link *>(ctx)->channelSequence();
    },
    .streamWrite_ = [](void *ctx, PrintView value) {
      bool ok = false;
      static_cast<Link *>(ctx)->streamTx_([value, &ok](auto &stream) {
	Writer out{&stream, writerOps<ZuDecay<decltype(stream)>>()};
	value.write(out);
	ok = out.flush();
      });
      return ok;
    },
    .streamEnd = [](void *ctx) { static_cast<Link *>(ctx)->streamTxEnd(); },
    .streamReset = [](void *ctx) { static_cast<Link *>(ctx)->streamTxReset(); },
    .session = [](const void *ctx) { return static_cast<const Link *>(ctx)->channelSession(); },
    .streamID = [](const void *ctx) { return static_cast<const Link *>(ctx)->channelStreamID(); },
    .remoteIP = nullptr,
    .remotePort = nullptr,
    .responseCancel = nullptr,
    .http10 = [](const void *ctx) { return static_cast<const Link *>(ctx)->channelHTTP10(); },
    .bodyFramed = [](const void *ctx) {
      return static_cast<const Link *>(ctx)->channelBodyFramed();
    },
    .streamWrite = [](void *ctx, PrintView value) {
      bool ok = false;
      static_cast<Link *>(ctx)->streamTx([value, &ok](auto &stream) {
	Writer out{&stream, writerOps<ZuDecay<decltype(stream)>>()};
	value.write(out);
	ok = out.flush();
      });
      return ok;
    }};
  return ops;
}

template <typename Link>
Channel clientChannel(Link *link)
{
  Channel channel{clientChannelOps<Link>()};
  channel.bind(link);
  return channel;
}

} // namespace Native_

} // namespace Zhttp

#endif /* ZhttpNative_HH */
