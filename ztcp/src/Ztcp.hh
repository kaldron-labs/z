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
#include <zlib/ZmHash.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmPLock.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
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
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using RxBufAlloc =
  Zi::IOBufAlloc<RxQueue::Node, Size, MaxSize, ZuStringT<HeapID>>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using TxBufAlloc =
  Zi::IOBufAlloc<TxQueue::Node, Size, MaxSize, ZuStringT<HeapID>>;

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
  HubParams(
    ZiMultiplex *mx_ = nullptr,
    ZuCSpan rxThread_ = {},
    ZuCSpan txThread_ = {}) :
      mx{mx_}, rxThread{rxThread_}, txThread{txThread_},
      errorFn_{defaultErrorFn()} { }

  HubParams &&errorFn(ErrorFn v) { errorFn_ = ZuMv(v); return ZuMv(*this); }

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

  void connected(ZiIOContext &io) override { m_link->connected_0(this, io); }
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
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Ztcp.RxBuf">
using RxBufAlloc = Ztcp_::RxBufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
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
  ZuAssert((ZuIs_<RxBufAlloc_, Ztcp_::RxQueue::Node>{}));
  ZuAssert((ZuIs_<TxBufAlloc_, Ztcp_::TxQueue::Node>{}));

  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const Link *link, Ztc::QueueType::T type) :
      m_link{link}, m_type{type} { }

    ZuTuple<const ZuID &, const ZuID &, Ztc::QueueType::T>
      telKey() const override {
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
  Cxn *cxn() const { return m_cxn; }
  StreamRef stream() { return impl(); }

  ZuTuple<const ZuID &, const ZuID &> telKey() const override {
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
  void connected_0(Cxn *cxn, ZiIOContext &io) {
    app()->rxRun([impl = ZmRef(this->impl()), cxn = ZmRef(cxn)]() {
      impl->connected_1(ZuMv(cxn));
    });
    recvRaw_(io);
  }

  void recvRaw_(ZiIOContext &io) {
    auto buf = ZmRef<ZiIOBuf>{new RxBufAlloc{impl()}};
    auto buf_ = buf.ptr();
    buf_->owner = impl();
    io.init(ZiIOFn::mvFn(ZuMv(buf),
      [](ZmRef<ZiIOBuf> buf, ZiIOContext &io) {
	auto link = static_cast<Impl *>(buf->owner);
	if (ZuUnlikely(io.length < 0)) {
	  io.complete();
	  return true;
	}
	if (io.length > 0) {
	  buf->length += io.length;
	  io.length = 0;
	  auto cxn = static_cast<Cxn *>(io.cxn);
	  link->app()->rxRun([
	    impl = ZmRef(link),
	    cxn = ZmRef(cxn),
	    buf = ZuMv(buf)
	  ]() mutable {
	    impl->rcvd_(cxn.ptr(), ZuMv(buf));
	  });
	  buf = new RxBufAlloc{link};
	}
	auto next = buf.ptr();
	next->owner = link;
	next->skip = 0;
	next->length = 0;
	io.fn.object(ZuMv(buf));
	io.ptr = next->data_();
	io.size = next->size;
	io.offset = 0;
	io.length = 0;
	return true;
      }), buf_->data_(), buf_->size, 0);
  }

  void connected_1(ZmRef<Cxn> cxn) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP connected dispatch outside Rx thread", return);
    if (ZuUnlikely(m_cxn == cxn)) return;
    if (ZuUnlikely(m_cxn)) {
      auto cxn_ = ZuMvPtr(m_cxn);
      cxn_->close();
    }
    auto oldState = state_();
    m_cxn = ZuMv(cxn);
    m_disconnecting = 0;
    stateChanged_(oldState);
    m_rxStream.clean();
    app()->linkConnected_();
    impl()->connected(Connected{});
  }

  template <typename ImplRef_>
  void disconnected_0(Cxn *cxn, ImplRef_ impl_, bool peer) {
    ZmRef<Impl> impl{ZuMvPtr(impl_)};
    app()->rxRun([impl = ZuMv(impl), cxn = ZmRef(cxn), peer]() mutable {
      impl->disconnected_(cxn.ptr());
      auto app = impl->app();
      app->txRun([impl = ZuMv(impl), cxn = ZuMv(cxn), peer]() mutable {
	auto app = impl->app();
	app->rxRun([
	  impl = ZuMv(impl), cxn = ZuMv(cxn), peer
	]() mutable {
	  impl->disconnected_1(peer);
	  (void)cxn;
	});
      });
    });
  }

  void disconnected_(Cxn *cxn) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnected dispatch outside Rx thread", return);
    auto oldState = state_();
    if (m_cxn == cxn) m_cxn = nullptr;
    m_disconnecting = 0;
    stateChanged_(oldState);
  }

  void disconnected_1(bool peer) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnect completion outside Rx thread", return);
    auto app = impl()->app();
    impl()->disconnected(peer);
    m_rxStream.clean();
    app->linkDisconnected_();
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

  ZmRef<ZiIOBuf> allocTxBuf_() {
    ZmRef<ZiIOBuf> buf = new TxBufAlloc{impl()};
    buf->skip = 0;
    buf->length = 0;
    return buf;
  }

  template <bool AppThread>
  class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;

  public:
    TxStream_(Link &link) :
      Base(TxBufAlloc::Size, 0, 0),
      m_link{&link}
    {
    }

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      auto buf = m_link->allocTxBuf_();
      if (ZuUnlikely(!buf || skip > buf->size))
	throw TxStreamAllocFailure{};
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

  struct TxStreamAllocFailure { };

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
    if (ZuUnlikely(!buf || !buf->length || m_disconnecting.load_()))
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
    if (ZuUnlikely(!buf || !buf->length ||
	m_disconnecting.load_() || !m_cxn))
      return tcpTxError_(
	"TCP connection is closed for transmission");
    buf->owner = impl();
    auto length = buf->length;
    m_txInCount.store_(m_txInCount.load_() + 1);
    m_txInBytes.store_(m_txInBytes.load_() + length);
    Tx::send(ZuMv(buf));
    return true;
  }

public:
  void sent(ZmRef<ZiTxBuf>, bool ok) {
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
    auto cxn = ZmRef<Cxn>{ZuMvPtr(m_cxn)};
    if (cxn) {
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

private:
  bool tcpTxError_(ZuCSpan message) {
    auto e = ZeEXCEPT(Error, "Ztcp", message);
    if (m_txErrorFn && !m_txErrorFn(e)) disconnect();
    return false;
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

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  RxStream		m_rxStream;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
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
    ZiIP ip = m_server;
    if (!ip) {
      app()->error_(ZeEXCEPT(Error, "Ztcp", ([server = LogMsg{m_server}](auto &s) {
	s << '"' << server << "\": hostname lookup failure";
      })));
      impl()->connectFailed(true);
      return;
    }
    app()->mx()->connect(
      ZiConnectFn{ZmRef(impl()),
	[](Impl *impl, const ZiCxnInfo &ci) -> ZiConnection * {
	  return new Cxn(impl, ci);
	}},
      ZiFailFn{ZmRef(impl()), [](Impl *impl, bool transient) {
	auto app = impl->app();
	app->rxRun([
	  impl = ZmRef<Impl>{impl}, transient
	]() mutable {
	  impl->connectFailed(transient);
	});
      }},
      ZiIP(), 0, ip, m_port);
  }

  void connectFailed(bool transient) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP connect failure outside Rx thread", return);
    unsigned reconnFreq = app()->reconnFreq();
    if (transient && reconnFreq > 0)
	app()->mx()->add(
	  &m_reconnTimer, Zm::now(reconnFreq), ZmScheduler::Update,
	  [this](auto &&arm) {
	    return arm([this]() { connect_(); });
	  }, app()->rxThread());
    else
      app()->error_(ZeEXCEPT(Error, "Ztcp", "connect failed"));
  }

protected:
  void up_() { connect(); }

private:
  void cancelReconn_() {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP reconnect cancellation outside Rx thread", return);
    app()->mx()->del(&m_reconnTimer);
  }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_reconnTimer;
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

  ZuTuple<Ztc::LinkType::T, const ZuID &> telKey() const override {
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
    m_rxThread = thread_(params.rxThread, m_mx->rxThread());
    m_txThread = thread_(params.txThread, m_mx->txThread());
    if (!ZuFwd<L>(l)(params)) return false;
    Ztc::HubMgr::add(this);
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
    if (!params.mx->running()) {
      error_(ZeEXCEPT(Error, "Ztcp", "multiplexer not running"));
      return false;
    }
    return true;
  }

public:
  void final() {
    bool ok = HubCtl::lock(ZmEngineState::Stopped, [this]() {
      Ztc::HubMgr::del(this);
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
  unsigned downLinks_() {
    unsigned n = 0;
    auto i = m_links.citer();
    while (Ztc::Link *link = i.key()) {
      ++n;
      link->down();
    }
    return n;
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
  ZiMultiplex		*m_mx = nullptr;
  ErrorFn		m_errorFn;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;

  // shared
  // Exceptional lock-guarded registry used by lifecycle and telemetry callers.
  Links			m_links;
};

template <typename App> class Client : public Hub<App> {
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
      app()->localIP(), app()->localPort(), app()->nAccepts(), ZiCxnOptions());
  }

  void stopListening() {
    mx()->del(&m_rebindTimer);
    if (m_listening)
      mx()->stopListening(app()->localIP(), app()->localPort());
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
    m_listening = true;
    app()->listening(info);
  }
  void listenFailed_(bool transient) {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP listen failure outside Rx thread", return);
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
    // Link lifetime can extend beyond disconnect completion. Count accepted
    // connections until their completion, not retained telemetry links.
    this->downLinks_();
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
  bool			m_listening = false;
  bool			m_stopDrained = false;
};

}

#endif /* Ztcp_HH */
