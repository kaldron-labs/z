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
#include <zlib/ZiTransport.hh>

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

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Ztcp.Log">>);
ZuDerive(Host, ZtString<ZtStringHeapID<"Ztcp.Host">>);
using ErrorFn = ZmFn<void(ZeException)>;
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Ztcp.Param">>);

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

struct EngineParams {
  EngineParams(
    ZiMultiplex *mx_ = nullptr,
    ZuCSpan rxThread_ = {},
    ZuCSpan txThread_ = {}) :
      mx{mx_}, rxThread{rxThread_}, txThread{txThread_},
      errorFn_{defaultErrorFn()} { }

  EngineParams &&errorFn(ErrorFn v) { errorFn_ = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex *mx = nullptr;
  ParamString rxThread;
  ParamString txThread;
  ErrorFn errorFn_;
};

struct ClientParams : public EngineParams {
  using EngineParams::EngineParams;

  ClientParams &&errorFn(ErrorFn v)
    { EngineParams::errorFn(ZuMv(v)); return ZuMv(*this); }
};

struct ServerParams : public EngineParams {
  using EngineParams::EngineParams;

  ServerParams &&errorFn(ErrorFn v)
    { EngineParams::errorFn(ZuMv(v)); return ZuMv(*this); }
};

template <typename Link_, typename LinkRef_>
class Cxn : public ZiConnection {
public:
  using Link = Link_;
  using LinkRef = LinkRef_;

  Cxn(LinkRef link, const ZiCxnInfo &ci) :
    ZiConnection(link->app()->mx(), ci), m_link(ZuMv(link)) { }

  void connected(ZiIOContext &io) { m_link->connected_0(this, io); }
  void disconnected(bool peer) {
    if (Link *link = m_link) link->disconnected_0(this, ZuMv(m_link), peer);
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
  public ZiTx<Impl>
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
  StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Duplex) {
    return type == Zi::StreamType::Duplex ? impl() : nullptr;
  }

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
    impl()->connected(Zi::Connected{.transport = Zi::Transport::TCP});
  }

  template <typename ImplRef_>
  void disconnected_0(Cxn *cxn, ImplRef_ impl_, bool peer) {
    ZmRef<Impl> impl{ZuMv(impl_)};
    app()->rxRun([impl = ZuMv(impl), cxn = ZmMkRef(cxn), peer]() {
      impl->disconnected_(cxn.ptr(), peer);
      auto mx = cxn->mx();
      mx->txRun([impl = ZuMv(impl), cxn = ZuMv(cxn)]() { });
    });
  }

  void disconnected_(Cxn *cxn, bool peer) {
    ZiAssert(app()->rxInvoked(), "Ztcp", (),
      "TCP disconnected dispatch outside Rx thread", return);
    if (m_cxn == cxn) m_cxn = nullptr;
    m_disconnecting = 0;
    m_rxStream.clean();
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
      if (ZuUnlikely(!buf || skip)) throw TxStreamAllocFailure{};
      return buf;
    }

    void sendBuf_(ZmRef<ZiIOBuf> buf) {
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

private:
  // immutable
  App			*m_app = nullptr;

  // Rx thread exclusive
  ZmScheduler::Timer	m_reconnTimer;
  CxnRef		m_cxn = nullptr;	// read by Tx thread
  RxStream		m_rxStream;

  // shared
  ZmAtomic<unsigned>	m_disconnecting = 0;
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
	    return arm(ZmFn<>{this, [](CliLink *link) { link->connect_(); }});
	  }, app()->rxThread());
    else
      app()->error_(ZeEXCEPT(Error, "Ztcp", "connect failed"));
  }

private:
  // Rx thread exclusive
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

template <typename App_> class Engine : public ZmEngine<App_> {
friend ZmEngine<App_>;
template <typename, typename, typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename> friend class CliLink;
template <typename, typename, typename, typename> friend class SrvLink;

public:
  using App = App_;
  using EngineCtl = ZmEngine<App>;

  using EngineCtl::start;
  using EngineCtl::stop;
  using EngineCtl::state;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  enum { Transport = Zi::Transport::TCP };

  bool init(EngineParams params) {
    return init_(ZuMv(params), [](const EngineParams &) { return true; });
  }

protected:
  template <typename Params, typename L>
  bool init_(Params params, L l) {
    m_errorFn = ZuMv(params.errorFn_);
    if (!m_errorFn) m_errorFn = defaultErrorFn();
    if (!validate_(params)) return false;
    m_mx = params.mx;
    m_rxThread = thread_(params.rxThread, m_mx->rxThread());
    m_txThread = thread_(params.txThread, m_mx->txThread());
    return l(params);
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
    bool ok = EngineCtl::lock(ZmEngineState::Stopped, [this]() {
      final_();
      return true;
    });
    ZiAssert(ok, "Ztcp", (),
      "TCP engine finalization while not stopped", return);
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

protected:
  void start_() {
    this->started(true);
  }

  void stop_() {
    rxRun([this]() {
      txRun([this]() { this->stopped(true); });
    });
  }

  template <typename L>
  bool spawn(L l) {
    if (!m_mx || !m_mx->running()) return false;
    rxRun(ZuMv(l));
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
  ErrorFn		m_errorFn;
};

template <typename App> class Client : public Engine<App> {
public:
  using Base = Engine<App>;
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
class Server : public Engine<App_> {
public:
  using App = App_;
  using Base = Engine<App>;
friend Base;

  using Base::mx;
  using Base::error_;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  bool init(ServerParams params) {
    return Base::init_(ZuMv(params), [](const ServerParams &) { return true; });
  }

  void final() { Base::final(); }

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

private:
  // Rx thread exclusive
  ZmScheduler::Timer	m_rebindTimer;
  bool			m_listening = false;
};

}

#endif /* Ztcp_HH */
