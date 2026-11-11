//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw TCP/IP transport

#ifndef Ztcp_HH
#define Ztcp_HH

#include <zlib/ZtcpLib.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmPLock.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiTxStream.hh>
#include <zlib/ZtcHub.hh>

namespace Ztcp_ {

using RxQueue = ZiRxQueue;
using TxQueue = ZiTxQueue;
using RxStream = ZiRxStream<RxQueue>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using RxBufAlloc =
  Zi::IOBufBuiltinAllocator<Zi::IOBufType::RxTCP,
    RxQueue::Node, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using TxBufAlloc =
  Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxTCP,
    TxQueue::Node, Size, ZiIOBuf_Align, ZuStringT<HeapID>, MaxSize>;

} // namespace Ztcp_

namespace Ztcp {

namespace LinkState {
  using namespace Ztc::LinkState;
}

struct Connected { };

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Ztcp.Log">>);
ZuDerive(Host, ZtString<ZtStringHeapID<"Ztcp.Host">>);
using ErrorFn = ZmFn<void(ZeException), ZmFnHeapID<"Ztcp.ErrorFn">>;
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Ztcp.Param">>);

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

struct HubParams {
  Zi::IOBufAllocator *rxAllocator_ = nullptr;
  Zi::IOBufAllocator *txAllocator_ = nullptr;

  HubParams(
    ZiMultiplex *mx_ = nullptr,
    ZuCSpan rxThread_ = {},
    ZuCSpan txThread_ = {}) :
      mx{mx_}, rxThread{rxThread_}, txThread{txThread_},
      errorFn_{defaultErrorFn()} { }

  HubParams &&errorFn(ErrorFn v) { errorFn_ = ZuMv(v); return ZuMv(*this); }
  HubParams &&rxAllocator(Zi::IOBufAllocator *v) {
    rxAllocator_ = v; return ZuMv(*this);
  }
  HubParams &&txAllocator(Zi::IOBufAllocator *v) {
    txAllocator_ = v; return ZuMv(*this);
  }
  ZiMultiplex *multiplex() const { return mx; }
  Zi::IOBufAllocator *rxAllocator() const { return rxAllocator_; }
  Zi::IOBufAllocator *txAllocator() const { return txAllocator_; }

  ZiMultiplex *mx = nullptr;
  ParamString rxThread;
  ParamString txThread;
  ErrorFn errorFn_;
};

struct ClientParams : public HubParams {
  using HubParams::HubParams;

  ClientParams &&errorFn(ErrorFn v)
    { HubParams::errorFn(ZuMv(v)); return ZuMv(*this); }
};

struct ServerParams : public HubParams {
  using HubParams::HubParams;

  ServerParams &&errorFn(ErrorFn v)
    { HubParams::errorFn(ZuMv(v)); return ZuMv(*this); }
};

template <typename Link_, typename LinkRef_>
class Cxn : public ZiConnection {
public:
  using Link = Link_;
  using LinkRef = LinkRef_;

  Cxn(LinkRef link, const ZiCxnInfo &ci) :
    ZiConnection(link->app()->mx(), ci), m_link(ZuMvPtr(link)) { }

  void connected(ZiRxContext &io) override { m_link->connected_0(this, io); }
  void disconnected(bool peer) override {
    LinkRef link_ = ZuMvPtr(m_link);
    if (Link *link = link_)
      link->disconnected_0(this, ZuMvPtr(link_), peer);
  }
private:
  // immutable
  LinkRef	m_link = nullptr;
};

template <typename Link> using CliCxn = Cxn<Link, Link *>;
template <typename Link> using SrvCxn = Cxn<Link, ZmRef<Link>>;

using RxStream = Ztcp_::RxStream;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Ztcp.RxBuf">
using RxBufAlloc = Ztcp_::RxBufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltVHMaxSize,
  ZuString HeapID = "Ztcp.TxBuf">
using TxBufAlloc = Ztcp_::TxBufAlloc<Size, MaxSize, HeapID>;

template <
  typename App, typename Impl, typename RxBufAlloc_, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_>
class Link :
  public ZmPolymorph,
  public ZiTx<Impl>,
  public Ztc::Link
{
  ZuAssert((ZuIs_<typename RxBufAlloc_::Buf, Ztcp_::RxQueue::Node>{}));
  ZuAssert((ZuIs_<typename TxBufAlloc_::Buf, Ztcp_::TxQueue::Node>{}));

  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const Link *link, Ztc::QueueType::T type) :
      m_link{link}, m_type{type} { }

    TelKey telKey() const override {
      auto linkKey = m_link->telKey();
      return {linkKey.template p<0>(), linkKey.template p<1>(), m_type};
    }
    void telemetry(Ztc::QueueTelemetry &data) const override {
      if (m_type == Ztc::QueueType::Rx)
	m_link->rxQueueTelemetry_(data);
      else
	m_link->txQueueTelemetry_(data);
    }

  private:
    const Link			*m_link;
    const Ztc::QueueType::T	m_type;
  };

public:
  using RxBufAlloc = RxBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;
  using RxAllocator = Zi::IOBufAllocator;
  using TxAllocator = Zi::IOBufAllocator;
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using Tx = ZiTx<Impl>;
  using Stream = Impl;
  using StreamRef = Impl *;

friend Cxn;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app) : Link{app, ZuID{} << "tcp:" <<
    ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }
  Link(App *app, ZuID id) : m_app{app}, m_id{ZuMv(id)} {
    app->linkAdded_(this);
  }
  ~Link() { app()->linkDeleted_(this, state_()); }

  App *app() const { return m_app; }
  static Zi::IOBufAllocator *rxAllocator(ZiMultiplex *mx) {
    return [mx]<
      Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
      typename HeapID, unsigned VHMaxSize, auto Grow
    >(Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *) {
      return mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
    }(static_cast<RxBufAlloc *>(nullptr));
  }
  static Zi::IOBufAllocator *txAllocator(ZiMultiplex *mx) {
    return [mx]<
      Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
      typename HeapID, unsigned VHMaxSize, auto Grow
    >(Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow> *) {
      return mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
    }(static_cast<TxBufAlloc *>(nullptr));
  }
  RxAllocator *rxAllocator() const {
    return static_cast<RxAllocator *>(app()->rxAllocator());
  }
  TxAllocator *txAllocator() const {
    return static_cast<TxAllocator *>(app()->txAllocator());
  }
  Cxn *cxn() const { return m_cxn; }
  StreamRef stream() { return impl(); }

  TelKey telKey() const override {
    return {app()->telKey().template p<1>(), m_id};
  }
  void telemetry(Ztc::LinkTelemetry &data) const override {
    data.hubID = app()->telKey().template p<1>();
    data.id = m_id;
    data.rxCalls = 0;
    data.txCalls = 0;
    data.rxBytes = 0;
    data.txBytes = 0;
    data.reconnects = 0;
    if (Cxn *cxn = m_cxn) {
      data.rxCalls = cxn->rxCalls();
      data.txCalls = cxn->txCalls();
      data.rxBytes = cxn->rxBytes();
      data.txBytes = cxn->txBytes();
    }
    data.type = Ztc::LinkType::TCP;
    data.state = state_();
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    TelQueue rxQueue{this, Ztc::QueueType::Rx};
    fn(&rxQueue);
    TelQueue txQueue{this, Ztc::QueueType::Tx};
    fn(&txQueue);
    return 2;
  }
  void up() override { impl()->up_(); }
  void down() override { disconnect(); }

private:
  void connected_0(Cxn *cxn, ZiRxContext &io) {
    app()->rxRun([impl = ZmRef(this->impl()), cxn = ZmRef(cxn)]() {
      impl->connected_1(ZuMv(cxn));
    });
    recvRaw_(io);
  }

  void recvRaw_(ZiRxContext &io) {
    const auto &ci = io.cxn->info();
    auto buf = static_cast<RxAllocator *>(ci.rxAllocator)->allocBuf(
      impl(), 0, ci.mps);
    if (ZuUnlikely(!buf)) { io.disconnect(); return; }
    io.init(ZiRxFn{impl(), [](Impl *link, ZiRxContext &io) {
      auto buf = ZuMv(io.buf);
      if (buf->length) {
	auto cxn = static_cast<Cxn *>(io.cxn);
	link->app()->rxRun([
	  cxn = ZmRef(cxn), buf = ZuMv(buf)
	]() mutable {
	  auto link = static_cast<Impl *>(buf->owner);
	  link->rcvd_(cxn.ptr(), ZuMv(buf));
	});
	const auto &ci = io.cxn->info();
	buf = static_cast<RxAllocator *>(ci.rxAllocator)->allocBuf(
	  link, 0, ci.mps);
      }
      if (ZuUnlikely(!buf)) { io.disconnect(); return true; }
      io.buf = ZuMv(buf);
      return true;
    }}, ZuMv(buf));
  }

  void connected_1(ZmRef<Cxn> cxn) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP connected dispatch outside Rx thread", return);
    if (ZuUnlikely(m_cxn == cxn)) return;
    auto oldState = state_();
    ZmRef<Cxn> old{ZuMvPtr(m_cxn)};
    if (!old) old = m_closing.load_();
    bool replacing = bool(old);
    m_closing = nullptr;
    if (ZuUnlikely(old)) {
      old->close();
      // Retire the old physical output on I/O Tx before publishing B. This
      // neither flushes A nor waits for its disconnect callbacks. Delayed A
      // send setups must reject the closed socket before touching the queue.
      auto old_ = old.ptr();
      old_->send(ZiTxFn{[
	impl = ZmRef(this->impl()), cxn = ZuMv(old)
      ](ZiTxContext &io) {
	io.complete();
	impl->Tx::txQueue.clean();
	(void)cxn;
	return false;
      }});
    }
    m_cxn = ZuMv(cxn);
    m_disconnecting = 0;
    stateChanged_(oldState);
    m_rxStream.clean();
    app()->linkConnected_();
    if (!impl()->connectSocket_(replacing)) { impl()->disconnect_(false); return; }
    // Protocol binding posts first; connected() publication follows this post.
    app()->txRun([impl = ZmRef(this->impl()), cxn = ZmRef<Cxn>{m_cxn}]() mutable {
      impl->m_txCxn = ZuMv(cxn);
    });
    if (!impl()->connectReady_()) return;
    impl()->connected(Connected{});
  }

  Cxn *closing_() const { return m_closing.load_(); }
  void closing_(Cxn *cxn) { m_closing = cxn; }

  template <typename ImplRef_>
  void disconnected_0(Cxn *cxn, ImplRef_ impl_, bool peer) {
    ZmRef<Impl> impl{ZuMvPtr(impl_)};
    app()->rxRun([impl = ZuMv(impl), cxn = ZmRef(cxn), peer]() mutable {
      impl->disconnected_(cxn.ptr());
      auto app = impl->app();
      app->txRun([impl = ZuMv(impl), cxn = ZuMv(cxn), peer]() mutable {
	if (impl->m_txCxn == cxn) impl->m_txCxn = nullptr;
	// Retain owners across posted work; stale closes cannot clean the successor.
	auto mx = cxn->mx();
	mx->txRun([
	  impl = ZuMv(impl), cxn = ZuMv(cxn), peer
	]() mutable {
	  if (impl->closing_() == cxn.ptr()) impl->Tx::txQueue.clean();
	  auto app = impl->app();
	  app->rxRun([
	    impl = ZuMv(impl), cxn = ZuMv(cxn), peer
	  ]() mutable {
	    if (impl->closing_() == cxn.ptr()) {
	      impl->closing_(nullptr);
	      impl->disconnected_1(peer);
	    }
	    impl->app()->linkDisconnected_();
	    (void)cxn;
	  });
	});
      });
    });
  }

  void disconnected_(Cxn *cxn) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnected dispatch outside Rx thread", return);
    if (m_cxn != cxn && m_closing.load_() != cxn) return;
    auto oldState = state_();
    m_cxn = nullptr;
    m_closing = cxn;
    m_disconnecting = 0;
    stateChanged_(oldState);
  }

  void disconnected_1(bool peer) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnect completion outside Rx thread", return);
    m_rxStream.clean();
    impl()->connectClosed_();
    impl()->disconnected(peer);
  }

  void rcvd_(Cxn *cxn, ZmRef<ZiIOBuf> buf) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP receive dispatch outside Rx thread", return);
    if (ZuUnlikely(m_cxn != cxn)) return;
    m_rxStream.push(ZuMv(buf));
    while (m_rxStream) {
      int n = impl()->process(m_rxStream);
      if (!n) return;
      if (ZuUnlikely(n < 0)) {
	disconnect_(true);
	return;
      }
    }
  }

public:
  unsigned txMPS_() const {
    Cxn *cxn = m_cxn;
    return cxn ? cxn->info().mps : ~uint32_t(0);
  }

  unsigned txBufSize_() const {
    auto allocator = txAllocator();
    unsigned ps = allocator->capacity();
    unsigned mps = txMPS_();
    if (ps > mps) ps = mps;
    return ps + allocator->headRoom + allocator->tailRoom;
  }

  ZmRef<ZiIOBuf> allocTxBuf_() {
    auto buf = txAllocator()->allocBuf(impl(), 0, txMPS_());
    if (ZuUnlikely(!buf)) return nullptr;
    return buf;
  }

  template <bool AppThread>
  class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;

  public:
    TxStream_(Link &link) :
      Base(link.txBufSize_(),
	link.txAllocator()->headRoom, link.txAllocator()->tailRoom),
      m_link{&link}
    {
    }

    ~TxStream_() { this->flush(); }
    TxStream_(TxStream_ &&) = default;
    TxStream_ &operator =(TxStream_ &&) = default;

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      auto buf = m_link->allocTxBuf_();
      if (ZuUnlikely(!buf)) return nullptr;
      if (skip < buf->skip) skip = buf->skip;
      if (ZuUnlikely(skip > buf->size ||
	  Base::tailRoom() > buf->size - skip))
	return nullptr;
      buf->skip = skip;
      return buf;
    }

    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      buf->owner = m_link->impl();
      auto link = static_cast<Impl *>(buf->owner);
      if constexpr (AppThread)
	return link->send(ZuMv(buf));
      else
	return link->send_(ZuMv(buf));
    }

  private:
    // immutable
    Link	*m_link;
  };


public:
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  auto txStream() {
    ZmAssert(!app()->txInvoked());
    return TxStream_<true>{*this};
  }
  auto txStream_() { // direct call from within tx thread
    ZmAssert(app()->txInvoked());
    return TxStream_<false>{*this};
  }

  bool send(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(!buf || buf->failed() || !buf->length || m_disconnecting.load_()))
      return tcpTxError_(
	"TCP connection is closed for transmission");
    buf->owner = impl();
    app()->txInvoke([buf = ZuMv(buf)]() mutable {
      auto link = static_cast<Impl *>(buf->owner);
      link->send_(ZuMv(buf));
    });
    return true;
  }

protected:
  bool send_(ZmRef<ZiIOBuf> buf) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Ztcp", (),
      "TCP send_ outside Tx thread", return false);
    if (ZuUnlikely(!buf || buf->failed() || !buf->length ||
	m_disconnecting.load_() || !m_txCxn)) {
      queueTxError_(m_txCxn, "TCP connection is closed for transmission");
      return false;
    }
    buf->owner = impl();
    auto length = buf->length;
    m_txInCount.store_(m_txInCount.load_() + 1);
    m_txInBytes.store_(m_txInBytes.load_() + length);
    m_txCxn->send(ZiTxFn::mvFn(ZuMv(buf), [](ZmRef<ZiIOBuf> buf, ZiTxContext &io) {
      auto cxn = static_cast<Cxn *>(io.cxn);
      if (ZuUnlikely(!cxn->up())) return false;
      auto link = Tx::impl(buf);
      if (auto count = link->Tx::txQueue.count_();
	  ZuUnlikely(count && count >= link->maxQueueLen())) {
	cxn->close();
	link->tcpTxError_("TCP transmit queue full");
	return false;
      }
      // ZiTx admission runs inline on this physical connection's I/O Tx thread.
      link->Tx::send(cxn, ZuMv(buf));
      return false;
    }));
    return true;
  }

public:
  void sent(ZmRef<ZiTxBuf>, bool ok) {
    // ZiConnection::errorSend already closes the failed physical socket.
    // A diagnostic callback cannot authorize closing a different successor.
    if (ZuUnlikely(!ok))
      tcpTxError_("TCP transmit failed");
  }

public:
  void disconnect() {
    auto oldState = state_();
    m_disconnecting = 1;
    stateChanged_(oldState);
    app()->rxInvoke([impl = ZmRef(this->impl())]() {
	impl->disconnect_();
    });
  }
  void disconnect_(bool notify = true) { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnect outside Rx thread", return);
    auto oldState = state_();
    m_disconnecting = 1;
    stateChanged_(oldState);
    impl()->cancelReconn_();
    impl()->stopConnect_();
    auto cxn = ZmRef<Cxn>{ZuMvPtr(m_cxn)};
    if (cxn) {
      m_closing = cxn.ptr();
      auto mx = cxn->mx();
      if (notify)
	mx->txRun([cxn = ZuMv(cxn)]() { cxn->disconnect(); });
      else
	cxn->close();
    }
  }

protected:
  void up_() { }
  void cancelReconn_() { }
  void stopConnect_() { }
  bool connectSocket_(bool) { return true; }
  bool connectReady_() { return true; }
  void connectClosed_() { }


private:
  bool tcpTxError_(ZuCSpan message) {
    auto e = ZeEXCEPT(Error, "Ztcp", message);
    if (m_txErrorFn) m_txErrorFn(e);
    return false;
  }

  void queueTxError_(ZmRef<Cxn> cxn, const char *message) {
    app()->rxRun([impl = ZmRef(this->impl()), cxn = ZuMv(cxn), message] {
      auto e = ZeEXCEPT(Error, "Ztcp", message);
      bool close = impl->m_txErrorFn && !impl->m_txErrorFn(e);
      // Policy callbacks may themselves replace or close the connection.
      if (close && cxn && impl->m_cxn == cxn.ptr()) impl->disconnect_(false);
    });
  }

  LinkState::T state_() const {
    if (m_disconnecting.load_()) return LinkState::Disconnecting;
    return m_cxn ? LinkState::Up : LinkState::Down;
  }

  void stateChanged_(LinkState::T oldState) {
    auto newState = state_();
    if (oldState != newState) app()->linkState_(oldState, newState);
  }

  void rxQueueTelemetry_(Ztc::QueueTelemetry &data) const {
    Cxn *cxn = m_cxn;
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inBytes = cxn ? cxn->rxBytes() : 0;
    data.outBytes = 0;
    data.inCount = cxn ? cxn->rxCalls() : 0;
    data.outCount = 0;
    data.count = m_rxStream.count_();
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Rx;
  }

  void txQueueTelemetry_(Ztc::QueueTelemetry &data) const {
    Cxn *cxn = m_cxn;
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inCount = m_txInCount.load_();
    data.inBytes = m_txInBytes.load_();
    data.outBytes = cxn ? cxn->txBytes() : 0;
    data.outCount = cxn ? cxn->txCalls() : 0;
    data.count = Tx::txQueue.count_();
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Tx;
  }

  // immutable
  App			*m_app = nullptr;
  const ZuID		m_id;

  // shared
  ZmAtomic<unsigned>	m_disconnecting = 0;
  // Configured before the link is used, then stable.
  ZiTxErrorFn		m_txErrorFn;
  // Read-mostly connection reference; mutation remains Rx-owned.
  CxnRef		m_cxn = nullptr;
  // A pending close owns cleanup only until a new association invalidates it.
  // Rx mutates the token; queued Tx/I/O cleanup only reads it. The close
  // continuations retain the connection, so its address cannot be recycled.
  ZmAtomic<Cxn *>	m_closing = nullptr;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  RxStream		m_rxStream;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  // Producer destination changes on this shard before connected() work runs.
  ZmRef<Cxn>		m_txCxn;
  ZmAtomic<uint64_t>	m_txInCount = 0;
  ZmAtomic<uint64_t>	m_txInBytes = 0;
};

template <
  typename App, typename Impl,
  typename RxBufAlloc, typename TxBufAlloc,
  typename Cxn>
using CliLink_ = Link<App, Impl, RxBufAlloc, TxBufAlloc, Cxn, ZmRef<Cxn>>;

template <
  typename App, typename Impl,
  typename RxBufAlloc, typename TxBufAlloc,
  typename Cxn>
using SrvLink_ = Link<App, Impl, RxBufAlloc, TxBufAlloc, Cxn, Cxn *>;

template <
  typename App, typename Impl,
  typename RxBufAlloc_ = Ztcp::RxBufAlloc<>,
  typename TxBufAlloc_ = Ztcp::TxBufAlloc<>>
class CliLink :
  public CliLink_<App, Impl, RxBufAlloc_, TxBufAlloc_, CliCxn<Impl>> {
public:
  using Cxn = CliCxn<Impl>;
  using RxBufAlloc = RxBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;
  using Base = CliLink_<App, Impl, RxBufAlloc, TxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;

friend Base;
template <typename> friend class Client;

  CliLink(App *app) : Base{app} { }
  CliLink(App *app, ZuID id) : Base{app, ZuMv(id)} { }
  CliLink(App *app, Host server, uint16_t port) :
      Base{app}, m_server{ZuMv(server)}, m_port{port} { }
  CliLink(App *app, ZuID id, Host server, uint16_t port) :
      Base{app, ZuMv(id)}, m_server{ZuMv(server)}, m_port{port} { }

  void connect() { app()->rxInvoke([this]() mutable { connect_(); }); }
  void connect(Host server, uint16_t port) {
    app()->rxInvoke(
      [this, server = ZuMv(server), port]() mutable {
	if (m_connectBusy || !impl()->connectAllowed_()) return;
	m_server = ZuMv(server);
	m_port = port;
	connect_();
      });
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }

  void connect_() { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP connect outside Rx thread", return);
    if (m_connectBusy) return;
    if (!impl()->connectAllowed_()) { retryConnect_(); return; }
    cancelReconn_();
    m_connectBusy = true;
    m_connectStopping = false;
    // Consume the resolver through its terminal continuation, including both
    // address families. Move the retained owner into the application post.
    ZiResolver::resolve(m_server, ZiResolver_::ResolveFn{
      [owner = ZmRef(impl()), ip = ZiIP{}](auto result) mutable {
	if (result.template is<ZiIP>()) {
	  if (!ip) ip = result.template p<ZiIP>();
	  return true;
	}
	auto app = owner->app();
	app->rxRun([owner = ZuMv(owner), ip] { owner->resolved_(ip); });
	return false;
      }});
  }

  bool connectAllowed_() const { return true; }
  void connectRetired_() { }
  bool connectSocket_(bool) { return !m_connectStopping; }
  bool connectReady_() { return !m_connectStopping; }
  void connectClosed_() { m_connectBusy = false; }
  // Shutdown stops new setup work, but submitted DNS/socket operations drain
  // through their ordinary completion. The hub/multiplex must remain alive.
  void stopConnect_() { m_connectStopping = true; }
  void resolved_(ZiIP ip) {
    if (!ip) { setupFailed_(true); return; }
    if (m_connectStopping) {
      m_connectBusy = false; impl()->connectRetired_(); return;
    }
    impl()->connectIP_(ip);
  }
  void setupFailed_(bool transient) {
    m_connectBusy = false;
    impl()->connectRetired_();
    impl()->connectFailed(transient);
  }
  void connectIP_(ZiIP ip) {
    app()->mx()->connect(
      ZiConnectFn{ZmRef(impl()),
	[](Impl *impl, const ZiCxnInfo &ci) -> ZiConnection * {
	  return new Cxn(impl, ci);
	}},
      ZiFailFn{ZmRef(impl()), [](Impl *impl, bool transient) {
	auto app = impl->app();
	app->rxRun([owner = ZmRef(impl), transient] { owner->setupFailed_(transient); });
      }}, ZiIP{}, 0, ip, m_port, app()->rxAllocator());
  }

  void connectFailed(bool transient) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP connect failure outside Rx thread", return);
    if (!m_connectStopping && (!transient || !retryConnect_()))
      app()->error_(ZeEXCEPT(Error, "Ztcp", "connect failed"));
  }

protected:
  void up_() { connect(); }

private:
  void retryConnect_(uint64_t serial) {
    if (m_reconnSerial == serial) connect_();
  }
  bool retryConnect_() {
    unsigned freq = app()->reconnFreq();
    if (!freq) return false;
    auto serial = ++m_reconnSerial;
    app()->mx()->del(&m_reconnTimer);
    app()->mx()->add(&m_reconnTimer, Zm::now(freq), ZmScheduler::Update,
      [this, serial](auto &&arm) {
	return arm([this, serial]() { retryConnect_(serial); });
      }, app()->rxThread());
    return true;
  }
  void cancelReconn_() {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP reconnect cancellation outside Rx thread", return);
    ++m_reconnSerial;
    app()->mx()->del(&m_reconnTimer);
  }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_reconnTimer;
  uint64_t m_reconnSerial = 0;
  bool m_connectBusy = false, m_connectStopping = false;
  Host			m_server;
  uint16_t		m_port = 0;
};

template <
  typename App, typename Impl,
  typename RxBufAlloc_ = Ztcp::RxBufAlloc<>,
  typename TxBufAlloc_ = Ztcp::TxBufAlloc<>>
class SrvLink :
  public SrvLink_<App, Impl, RxBufAlloc_, TxBufAlloc_, SrvCxn<Impl>> {
public:
  using Cxn = SrvCxn<Impl>;
  using RxBufAlloc = RxBufAlloc_;
  using TxBufAlloc = TxBufAlloc_;
  using Base = SrvLink_<App, Impl, RxBufAlloc, TxBufAlloc, Cxn>;

  using Base::impl;
  using Base::app;

friend Base;
template <typename> friend class Server;

  SrvLink(App *app) : Base(app) { }
};

template <typename App_> class Hub :
  public ZmEngine<App_>,
  public Ztc::Hub {
friend ZmEngine<App_>;
template <typename, typename, typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename> friend class CliLink;
template <typename, typename, typename, typename> friend class SrvLink;

public:
  using App = App_;
  using HubCtl = ZmEngine<App>;
  using Links = ZmHash<Ztc::Link *,
    ZmHashLock<ZmPLock,
      ZmHashHeapID<"Ztcp.Hub.Links">>>;

  using HubCtl::start;
  using HubCtl::stop;
  using HubCtl::state;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  Hub() : m_id{ZuID{} << "tcp:" <<
    ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }

  bool init(HubParams params) {
    return init_(ZuMv(params), [](const HubParams &) { return true; });
  }
  bool start() override { return HubCtl::start(); }
  bool stop() override { return HubCtl::stop(); }

  TelKey telKey() const override {
    return {Ztc::LinkType::TCP, m_id};
  }
  void telemetry(Ztc::HubTelemetry &data) const override {
    data.id = m_id;
    if (m_mx) data.mxID = m_mx->id();
    data.down = m_down.load_();
    data.disabled = 0;
    data.transient = m_transient.load_();
    data.up = m_up.load_();
    data.reconn = 0;
    data.failed = 0;
    data.nLinks = m_nLinks.load_();
    data.rxThread = m_rxThread;
    data.txThread = m_txThread;
    data.linkType = Ztc::LinkType::TCP;
    data.state = state();
  }
  unsigned allLinks(Ztc::Hub::AllLinksFn fn) const override {
    return app()->allLinks_(ZuMv(fn));
  }
  unsigned allPools(Ztc::Hub::AllPoolsFn fn) const override {
    return app()->allPools_(ZuMv(fn));
  }

protected:
  template <typename Params, typename L>
  bool init_(Params params, L &&l) {
    m_errorFn = ZuMv(params.errorFn_);
    if (!m_errorFn) m_errorFn = defaultErrorFn();
    if (!validate_(params)) return false;
    m_mx = params.mx;
    if (params.rxAllocator()->mx != m_mx ||
	!allocator(params.txAllocator())) return false;
    m_rxAllocator = params.rxAllocator();
    m_txAllocator = params.txAllocator();
    m_rxThread = thread_(params.rxThread, m_mx->rxThread());
    m_txThread = thread_(params.txThread, m_mx->txThread());
    if (!ZuFwd<L>(l)(params)) return false;
    Ztc::HubMgr::add(this);
    m_registered = true;
    return true;
  }

private:
  unsigned thread_(const ParamString &id, unsigned deflt) const {
    return id ? m_mx->sid(id) : deflt;
  }

  template <typename Params>
  bool validate_(const Params &params) {
    if (ZuUnlikely(!params.mx)) {
      error_(ZeEXCEPT(Error, "Ztcp", "multiplexer is null"));
      return false;
    }
    unsigned rxThread = params.rxThread ?
      params.mx->sid(params.rxThread) : params.mx->rxThread();
    unsigned txThread = params.txThread ?
      params.mx->sid(params.txThread) : params.mx->txThread();
    if (!rxThread || rxThread > params.mx->params().nThreads()) {
      error_(ZeEXCEPT(Error, "Ztcp", ([thread = LogMsg{params.rxThread}](auto &s) {
	s << "invalid TCP Rx thread ID \"" << thread << '"';
      })));
      return false;
    }
    if (!txThread || txThread > params.mx->params().nThreads()) {
      error_(ZeEXCEPT(Error, "Ztcp", ([thread = LogMsg{params.txThread}](auto &s) {
	s << "invalid TCP Tx thread ID \"" << thread << '"';
      })));
      return false;
    }
    if (rxThread == txThread) {
      error_(ZeEXCEPT(Error, "Ztcp",
	"TCP Rx and Tx threads must differ"));
      return false;
    }
    if (!params.rxAllocator() ||
	params.rxAllocator()->type != Zi::IOBufType::RxTCP ||
	!params.txAllocator() ||
	params.txAllocator()->type != Zi::IOBufType::TxTCP) {
      error_(ZeEXCEPT(Error, "Ztcp", "TCP Rx/Tx allocators are required"));
      return false;
    }
    return true;
  }

public:
  void final() {
    bool ok = HubCtl::lock(ZmEngineState::Stopped, [this]() {
      if (m_registered) {
	Ztc::HubMgr::del(this);
	m_registered = false;
      }
      final_();
      return true;
    });
    ZiAssert(ok, "Ztcp", (),
      "TCP hub finalization while not stopped", return);
  }

  ZiMultiplex *mx() const { return m_mx; }
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }

  template <typename ...Args>
  void rxRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args>
  void rxInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_rxThread);
  }
  bool rxInvoked() { return m_mx->invoked(m_rxThread); }
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_txThread);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_txThread);
  }
  bool txInvoked() { return m_mx->invoked(m_txThread); }

  // Validate Tx TCP backing created by the underlying Mx.
  bool allocator(Zi::IOBufAllocator *a) {
    return a && a->type == Zi::IOBufType::TxTCP && m_mx &&
      a->mx == m_mx;
  }

  Zi::IOBufAllocator *rxAllocator() const { return m_rxAllocator; }
  Zi::IOBufAllocator *txAllocator() const { return m_txAllocator; }

  void linkConnected_() { }
  void linkDisconnected_() { }

private:
  void linkAdded_(Ztc::Link *link) {
    m_links.add(link);
    Ztc::Hub::linkAdded_(link);
  }
  void linkDeleted_(Ztc::Link *link, LinkState::T state) {
    if (m_links.del(link)) {
      switch (state) {
	case LinkState::Down: Ztc::Hub::linkDownDec_(); break;
	case LinkState::Up: Ztc::Hub::linkUpDec_(); break;
	default: Ztc::Hub::linkTransientDec_(); break;
      }
      Ztc::Hub::linkDeleted_(link);
    }
  }

public:
  void linkState_(
      LinkState::T oldState, LinkState::T newState) {
    switch (oldState) {
      case LinkState::Down: Ztc::Hub::linkDownDec_(); break;
      case LinkState::Up: Ztc::Hub::linkUpDec_(); break;
      default: Ztc::Hub::linkTransientDec_(); break;
    }
    switch (newState) {
      case LinkState::Down: Ztc::Hub::linkDownInc_(); break;
      case LinkState::Up: Ztc::Hub::linkUpInc_(); break;
      default: Ztc::Hub::linkTransientInc_(); break;
    }
  }

protected:
  unsigned allLinks_(Ztc::Hub::AllLinksFn fn) const {
    unsigned n = 0;
    auto i = m_links.citer();
    while (Ztc::Link *link = i.key()) {
      ++n;
      fn(link);
    }
    return n;
  }
  template <typename Link>
  unsigned downLinks_() {
    using DrainLinks = ZtArray<ZmRef<Link>,
      ZtArrayHeapID<"Ztcp.Hub.Drain">>;
    unsigned capacity = m_links.count_();
    auto links = ZtScratch(DrainLinks, capacity);
    {
      auto i = m_links.citer();
      while (Ztc::Link *base = i.key()) {
	auto link = static_cast<Link *>(base);
	// A destructor can wait on the iterator lock after its final deref.
	link->template withRef<Link>([&links](ZmRef<Link> ref) {
	  links.push(ZuMv(ref));
	});
      }
    }
    for (auto &link: links) link->down();
    return links.length();
  }
  unsigned allPools_(Ztc::Hub::AllPoolsFn) const { return 0; }

  void start_() {
    this->started(true);
  }

  // ZmEngine hook; public stop(done) retains done until stop_1() calls
  // stopped(true).  Returning from this function does not complete stop.
  void stop_() {		// enter Rx thread
    rxRun([this]() { stop_0(); });
  }

  void stop_0() {		// Rx thread - enter Tx thread
    txRun([this]() { stop_1(); });
  }

  void stop_1() {		// Tx thread - complete stop
    this->stopped(true);
  }

  template <typename L>
  bool spawn(L &&l) {
    if (!m_mx || !m_mx->running()) return false;
    rxRun(ZuFwd<L>(l));
    return true;
  }

  void wake() {
    if (!m_mx || !m_mx->running()) return;
    rxRun([this]() { this->stopped(); });
  }

  void error_(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

private:
  void final_() { m_errorFn = ErrorFn{}; } // lifecycle finalization

  // stable after init(); m_errorFn is cleared during final()
  const ZuID		m_id;
  using AllocatorRef = Zi::IOBufAllocator *;
  AllocatorRef		m_rxAllocator = nullptr;
  AllocatorRef		m_txAllocator = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  ErrorFn		m_errorFn;
  bool			m_registered = false;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;

  // shared
  // Exceptional lock-guarded registry used by lifecycle and telemetry callers.
  Links			m_links;
};

template <typename App> class Client : public Hub<App> {
template <typename, typename, typename, typename> friend class CliLink;
public:
  using Base = Hub<App>;
friend Base;

  using Base::error_;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  bool init(ClientParams params) {
    return Base::init_(ZuMv(params), [](const ClientParams &) { return true; });
  }

  void final() { Base::final(); }

protected:
  unsigned reconnFreq() const { return 0; }
};

template <typename App_>
class Server : public Hub<App_> {
friend ZmEngine<App_>;

public:
  using App = App_;
  using Base = Hub<App>;
friend Base;

  using Base::mx;
  using Base::error_;
  using Base::stop;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  bool init(ServerParams params) {
    return Base::init_(ZuMv(params), [](const ServerParams &) { return true; });
  }

  void final() { Base::final(); }

  void listen() {
    if (!this->running()) return;
    mx()->listen(
      ZiListenFn{app(),
	[](App *app, const ZiListenInfo &info) {
	  auto server = static_cast<Server *>(app);
	  server->rxRun([server, info = ZiListenInfo{info}]() {
	    server->listening_(info);
	  });
	}},
      ZiFailFn{app(),
	[](App *app, bool transient) {
	  auto server = static_cast<Server *>(app);
	  server->rxRun([server, transient]() {
	    server->listenFailed_(transient);
	  });
	}},
      ZiConnectFn{app(),
	[](App *app, const ZiCxnInfo &ci) -> ZiConnection * {
	  return app->accepted(ci);
	}},
      app()->localIP(), app()->localPort(), app()->nAccepts(), this->rxAllocator(), ZiCxnOptions());
  }

  void stopListening() {
    mx()->del(&m_rebindTimer);
    if (m_listening)
      mx()->stopListening(m_listenIP, m_listenPort);
    m_listening = false;
  }

  void linkConnected_() {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP server link admission outside Rx thread", return);
    ++m_pending;
  }
  void linkDisconnected_() {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP server link completion outside Rx thread", return);
    ZiAssert(m_pending, "Ztcp", (),
      "TCP server disconnect without pending connection", return);
    if (!--m_pending && this->stopping() && m_stopDrained)
      Base::stop_0();
  }

protected:
  unsigned nAccepts() const { return 8; }
  unsigned rebindFreq() const { return 0; }

  void listening_(const ZiListenInfo &info) {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP listen completion outside Rx thread", return);
    if (!this->running()) {
      m_listenClosing = true;
      mx()->stopListening(info.ip, info.port); return;
    }
    m_listenIP = info.ip; m_listenPort = info.port;
    m_listening = true;
    app()->listening(info);
  }
  void listenFailed_(bool transient) {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP listen failure outside Rx thread", return);
    if (!this->running()) return;
    app()->listenFailed(transient);
  }

  void listening(const ZiListenInfo &info) {
    ZiLOG(Info, "Ztcp", ([info](auto &s) {
      s << "listening(" << info.ip << ':' << info.port << ')';
    }));
  }
  void listenFailed(bool transient) {
    unsigned rebindFreq = app()->rebindFreq();
    if (transient && rebindFreq > 0)
      app()->mx()->add(
	  &m_rebindTimer, Zm::now(rebindFreq), ZmScheduler::Update,
	  [this](auto &&arm) { return arm([this]() { listen(); }); },
	  app()->rxThread());
    else
      app()->error_(ZeEXCEPT(Error, "Ztcp", ([transient](auto &s) {
	s << "listen() failed " << (transient ? "(transient)" : "");
      })));
  }

  // ZmEngine hook.  Public stop(done) retains done until Base::stop_1()
  // calls stopped(true) after every continuation below has drained.
  void stop_() {
    this->rxRun([this]() { stopRx_(); });
  }

  void stopRx_() {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP server stop initialization outside Rx thread", return);
    stopListening();
    m_stopDrained = false;
    // Stop the listener on the I/O Rx shard before draining accepted
    // connections on our Rx shard; down() requires the installed m_cxn.
    mx()->rxRun([this]() {
      this->rxRun([this]() { stop_0(); });
    });
  }

  void stop_0() {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP server stop drain outside Rx thread", return);
    if (m_listenClosing) {
      m_listenClosing = false;
      // A late bind queued its close after the initial I/O barrier. Drain that
      // close and any accepted-connection notifications before downLinks_().
      mx()->rxRun([this]() {
	this->rxRun([this]() { stop_0(); });
      });
      return;
    }
    // Link lifetime can extend beyond disconnect completion. Count accepted
    // connections until their completion, not retained telemetry links.
    this->template downLinks_<typename App::Link>();
    this->rxRun([this]() { stop_2(); });
  }

  void stop_2() {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP server stop completion outside Rx thread", return);
    m_stopDrained = true;
    if (!m_pending) Base::stop_0();
  }

private:
  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_rebindTimer;
  unsigned		m_pending = 0;
  ZiIP			m_listenIP;
  uint16_t		m_listenPort = 0;
  bool			m_listening = false;
  bool			m_listenClosing = false;
  bool			m_stopDrained = false;
};

}

#endif /* Ztcp_HH */
