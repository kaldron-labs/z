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
#include <zlib/ZmList.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmEngine.hh>

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

ZuDerive(IOQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));

using RxStream = ZiRxStream<IOQueue>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = ZiIOBuf_HeapID{}()>
using BufAlloc =
  Zi::IOBufAlloc<IOQueue::Node, Size, MaxSize, ZuStringT<HeapID>>;

} // namespace Ztcp_

namespace Ztcp {

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
    ZiConnection(link->app()->mx(), ci), m_link(ZuMv(link)) { }

  void connected(ZiIOContext &io) override { m_link->connected_0(this, io); }
  void disconnected(bool peer) override {
    if (Link *link = m_link) link->disconnected_0(this, ZuMv(m_link), peer);
  }
  Ztc::Link *telLink(const void *owner) const override {
    Link *link = m_link;
    return link && link->app() == owner ? link : nullptr;
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
using RxBufAlloc = Ztcp_::BufAlloc<Size, MaxSize, HeapID>;

template <
  unsigned Size = ZiIOBuf_DefltSize,
  unsigned MaxSize = ZiIOBuf_DefltMaxSize,
  ZuString HeapID = "Ztcp.TxBuf">
using TxBufAlloc = Ztcp_::BufAlloc<Size, MaxSize, HeapID>;

template <
  typename App, typename Impl, typename RxBufAlloc_, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_>
class Link :
  public ZmPolymorph,
  public ZiTx<Impl>,
  public Ztc::Link
{
  ZuAssert((ZuIs_<RxBufAlloc_, Ztcp_::IOQueue::Node>{}));
  ZuAssert((ZuIs_<TxBufAlloc_, Ztcp_::IOQueue::Node>{}));

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

  Link(App *app) : m_app(app) { }
  ~Link() = default;

  App *app() const { return m_app; }
  Cxn *cxn() const { return m_cxn; }
  StreamRef stream() { return impl(); }

  ZuTuple<ZuID, ZuID> telKey() const override {
    return {app()->telID(), telID()};
  }
  void telemetry(Ztc::LinkTelemetry &data) const override {
    data.hubID = app()->telID();
    data.id = telID();
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
    data.state = m_cxn ? Ztc::LinkState::Up :
      m_disconnecting.load_() ?
	Ztc::LinkState::Disconnecting : Ztc::LinkState::Down;
  }
  Ztc::Queue *rxQueue() const override { return nullptr; }
  Ztc::Queue *txQueue() const override { return nullptr; }
  void up() override { impl()->telUp_(); }
  void down() override { disconnect(); }

private:
  void connected_0(Cxn *cxn, ZiIOContext &io) {
    app()->rxRun([impl = ZmMkRef(this->impl()), cxn = ZmMkRef(cxn)]() {
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
	    impl = ZmMkRef(link),
	    cxn = ZmMkRef(cxn),
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
    if (ZuUnlikely(m_cxn)) { auto cxn_ = ZuMv(m_cxn); cxn_->close(); }
    m_cxn = ZuMv(cxn);
    m_disconnecting = 0;
    m_rxStream.clean();
    impl()->connected(Connected{});
  }

  template <typename ImplRef_>
  void disconnected_0(Cxn *cxn, ImplRef_ impl_, bool peer) {
    ZmRef<Impl> impl{ZuMv(impl_)};
    app()->rxRun([impl = ZuMv(impl), cxn = ZmMkRef(cxn), peer]() mutable {
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
    if (m_cxn == cxn) m_cxn = nullptr;
    m_disconnecting = 0;
    m_rxStream.clean();
  }

  void disconnected_1(bool peer) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnect completion outside Rx thread", return);
    auto app = impl()->app();
    impl()->disconnected(peer);
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

    void sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      buf->owner = m_link->impl();
      auto link = static_cast<Impl *>(buf->owner);
      if constexpr (AppThread)
	link->send(ZuMv(buf));
      else
	link->send_(ZuMv(buf));
    }

  private:
    // immutable
    Link	*m_link;
  };

  struct TxStreamAllocFailure { };

public:
  auto txStream() { return TxStream_<true>{*this}; }
  auto txStream_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Ztcp", (),
      "TCP txStream_ outside Tx thread", return TxStream_<false>{*this});
    return TxStream_<false>{*this};
  }

  void send(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    buf->owner = impl();
    app()->txInvoke([buf = ZuMv(buf)]() mutable {
      auto link = static_cast<Impl *>(buf->owner);
      link->send_(ZuMv(buf));
    });
  }

protected:
  void send_(ZmRef<ZiIOBuf> buf) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Ztcp", (),
      "TCP send_ outside Tx thread", return);
    if (ZuUnlikely(!buf || !buf->length)) return;
    if (ZuUnlikely(m_disconnecting.load_())) return;
    buf->owner = impl();
    Tx::send(ZuMv(buf));
  }

public:
  void disconnect() {
    m_disconnecting = 1;
    app()->rxInvoke([this]() { disconnect_(); });
  }
  void disconnect_(bool notify = true) { // direct call from within rx thread
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnect outside Rx thread", return);
    m_disconnecting = 1;
    app()->mx()->del(&m_reconnTimer);
    auto cxn = ZmRef<Cxn>{ZuMv(m_cxn)};
    m_cxn = nullptr;
    if (cxn) {
      auto mx = cxn->mx();
      if (notify)
	mx->txRun([cxn = ZuMv(cxn)]() { cxn->disconnect(); });
      else
	cxn->close();
    }
  }

protected:
  void telUp_() { }

private:
  ZuID telID() const {
    return ZuID{} << "tcp:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>();
  }

  // immutable
  App			*m_app = nullptr;

  // shared
  ZmAtomic<unsigned>	m_disconnecting = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_reconnTimer;
  CxnRef		m_cxn = nullptr;	// read by Tx thread
  RxStream		m_rxStream;
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
  CliLink(App *app, Host server, uint16_t port) :
      Base{app}, m_server{ZuMv(server)}, m_port{port} { }

  void connect() { app()->rxInvoke([this]() mutable { connect_(); }); }
  void connect(Host server, uint16_t port) {
    m_server = ZuMv(server);
    m_port = port;
    app()->rxInvoke([this]() mutable { connect_(); });
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
      ZiConnectFn{ZmMkRef(impl()),
	[](Impl *impl, const ZiCxnInfo &ci) -> ZiConnection * {
	  return new Cxn(impl, ci);
	}},
      ZiFailFn{ZmMkRef(impl()), [](Impl *impl, bool transient) {
	impl->connectFailed(transient);
      }},
      ZiIP(), 0, ip, m_port);
  }

  void connectFailed(bool transient) {
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
  void telUp_() { connect(); }

private:
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

  using HubCtl::start;
  using HubCtl::stop;
  using HubCtl::state;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  bool init(HubParams params) {
    return init_(ZuMv(params), [](const HubParams &) { return true; });
  }
  bool start() override { return HubCtl::start(); }
  bool stop() override { return HubCtl::stop(); }

  ZuID telID() const {
    return ZuID{} << "tcp:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>();
  }
  ZuTuple<Ztc::LinkType::T, ZuID> telKey() const override {
    return {Ztc::LinkType::TCP, telID()};
  }
  void telemetry(Ztc::HubTelemetry &data) const override {
    data.id = telID();
    if (m_mx) data.mxID = m_mx->id();
    data.down = 0;
    data.disabled = 0;
    data.transient = 0;
    data.up = 0;
    data.reconn = 0;
    data.failed = 0;
    data.nLinks = 0;
    data.rxThread = m_rxThread;
    data.txThread = m_txThread;
    data.linkType = Ztc::LinkType::TCP;
    data.state = state();
    const_cast<Hub *>(this)->allLinks({
      &data, [](Ztc::HubTelemetry *data, Ztc::Link *link) {
	Ztc::LinkTelemetry linkData;
	link->telemetry(linkData);
	++data->nLinks;
	switch (linkData.state) {
	  case Ztc::LinkState::Down:		  ++data->down; break;
	  case Ztc::LinkState::Disabled:	  ++data->disabled; break;
	  case Ztc::LinkState::ReconnectPending:
	  case Ztc::LinkState::Reconnecting:	  ++data->reconn; break;
	  case Ztc::LinkState::Up:		  ++data->up; break;
	  case Ztc::LinkState::Failed:		  ++data->failed; break;
	  default:				  ++data->transient; break;
	}
      }});
  }
  void allLinks(Ztc::Hub::AllLinksFn fn) override {
    app()->allLinks_(ZuMv(fn));
  }
  void allPools(Ztc::Hub::AllPoolsFn fn) override {
    app()->allPools_(ZuMv(fn));
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

  void linkDisconnected_() { }

protected:
  void allLinks_(Ztc::Hub::AllLinksFn) { }
  void allPools_(Ztc::Hub::AllPoolsFn) { }

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
  void final_() { m_errorFn = ErrorFn{}; } // direct call from within rx thread

  // immutable after init()
  ZiMultiplex		*m_mx = nullptr;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;

  // Rx thread exclusive after init()
  alignas(Zm::CacheLineSize)
  ErrorFn		m_errorFn;
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

  void allLinks_(Ztc::Hub::AllLinksFn fn) {
    mx()->allCxns(Ztc::Mx::AllCxnsFn{
      [app = app(), fn = ZuMv(fn)](Ztc::Connection *cxn_) mutable {
	auto cxn = static_cast<ZiConnection *>(cxn_);
	if (auto link = cxn->telLink(app)) fn(link);
      }});
  }

  void listen() {
    mx()->listen(
      ZiListenFn{app(),
	[](App *app, const ZiListenInfo &info) { app->listening(info); }},
      ZiFailFn{app(),
	[](App *app, bool transient) { app->listenFailed(transient); }},
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

  void linkDisconnected_() {
    ZiAssert(this->rxInvoked(), "Ztcp", (),
      "TCP server link completion outside Rx thread", return);
    if (!this->stopping() || !m_stopCount) return;
    if (!--m_stopCount && m_stopDrained) Base::stop_0();
  }

protected:
  unsigned nAccepts() const { return 8; }
  unsigned rebindFreq() const { return 0; }

  void listening(const ZiListenInfo &info) {
    m_listening = true;
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
    stopListening();
    m_stopCount = 0;
    m_stopDrained = false;
    // Drain accepted connections whose connected_1() is already queued
    // before enumerating links; down() requires the installed m_cxn.
    this->rxRun([this]() { stop_0(); });
  }

  void stop_0() {
    allLinks_({this, [](Server *server, Ztc::Link *link) {
      ++server->m_stopCount;
      link->down();
    }});
    this->rxRun([this]() { stop_1(); });
  }

  void stop_1() {
    m_stopDrained = true;
    if (!m_stopCount) Base::stop_0();
  }

private:
  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmScheduler::Timer	m_rebindTimer;
  unsigned		m_stopCount = 0;
  bool			m_listening = false;
  bool			m_stopDrained = false;
};

}

#endif /* Ztcp_HH */
