//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - normalized application link adapters

#ifndef ZhttpLink_HH
#define ZhttpLink_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmScheduler.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZhttpTransport.hh>
#include <zlib/ZhttpH1Stream.hh>

namespace Zhttp {

ZuDerive(EndpointString, ZtString<ZtStringHeapID<"Zhttp.Endpoint">>);

namespace Link_ {

template <typename Consumer, typename Link, typename Stream>
auto streamProcess(
  Consumer &consumer, Link &link, Stream stream, int) ->
    decltype(consumer.streamProcess(link, ZuMv(stream)), void())
{
  consumer.streamProcess(link, ZuMv(stream));
}
template <typename Consumer, typename Link, typename Stream>
void streamProcess(Consumer &, Link &, Stream, ...) { }

} // namespace Link_

template <typename App, typename Impl, typename Profile>
class ClientLink :
  public ProfileTraits<Profile>::Transport::template ClientLink<App, Impl> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;
  using Base = typename Traits::template ClientLink<App, Impl>;
  using StreamBinding =
    H1::StreamBinding<Impl, ClientLink, typename Traits::RxStream>;

public:
  using Base::Base;
  enum { TLS = Traits::Secure, Multiplexed = HTTP::Multiplexed };

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename ...Args>
  void connect(Args &&...args) {
    m_stream.reopen();
    m_connected = false;
    m_failed = false;
    m_cancelled = false;
    Base::connect(ZuFwd<Args>(args)...);
  }
  template <typename Endpoint>
  void connectEndpoint(const Endpoint &endpoint) {
    connect(endpoint.target, endpoint.port);
  }
  void connected(typename Traits::Connected info) {
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
      m_stream.disconnected(peer);
      this->app()->disconnected(*impl(), peer);
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
    m_cancelled = true;
    Traits::disconnect(*this);
  }
  int process(typename Traits::RxStream &rx) {
    if (m_stream.stream()) return m_stream.process(rx);
    if (m_stream.terminal()) return 0;
    int rc = this->app()->process(*impl(), rx);
    return m_stream.stream() ? m_stream.process(rx) : rc;
  }
  bool streamEnable(bool enabled = true) {
    return m_stream.enable(enabled);
  }
  bool streamAccept() { return m_stream.accept(*impl(), *this); }
  bool streamLocalCap() const { return m_stream.localCap(); }
  bool streamPeerCap() const { return m_stream.peerCap(); }
  template <typename L>
  void streamTx(L &&l) {
    if (!m_stream.tx()) return;
    auto tx = this->txStream();
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() {
    if (!m_stream.end()) return;
    this->app()->txRun([link = ZmMkRef(impl())]() mutable {
      auto app = link->app();
      app->rxRun([link = ZuMv(link)]() mutable {
	Traits::disconnect(*link);
      });
    });
  }
  void streamTxReset() {
    if (m_stream.reset()) this->disconnect();
  }
  template <typename Stream>
  void streamProcess(Stream stream) {
    Link_::streamProcess(*this->app(), *impl(), ZuMv(stream), 0);
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) {
    return parser.process(rx);
  }
  template <typename Builder>
  auto transmit(Builder &) {
    return this->txStream();
  }
  void finish() { }
  bool active() const { return !!this->cxn(); }
  template <typename State>
  void responseHeadersParsed(State *) { }
  template <typename State>
  void responseBodyBytes(State *) { }

private:
  StreamBinding	m_stream;
  bool	m_connected = false;
  bool	m_failed = false;
  bool	m_cancelled = false;
};

template <
  typename App, typename Impl, typename Profile, typename Session>
class ServerLink :
  public ProfileTraits<Profile>::Transport::template ServerLink<App, Impl> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;
  using Base = typename Traits::template ServerLink<App, Impl>;
  using StreamBinding =
    H1::StreamBinding<Impl, ServerLink, typename Traits::RxStream>;

public:
  enum { TLS = Traits::Secure, Multiplexed = HTTP::Multiplexed };

  ServerLink(App *app, const ZiCxnInfo &ci) :
    Base{app}
  {
#ifdef ZmObject_DEBUG
    this->ZmPolymorph::debug();
#endif
    m_remote << ci.remoteIP;
  }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  ZuCSpan remote() const { return m_remote; }
  Session &session() { return m_session; }

  void connected(typename Traits::Connected info) {
    m_session.connected(*impl());
    this->app()->connected(*impl(), HTTP::connected(ZuMv(info)));
    touch();
  }
  void disconnected(bool peer) {
    if (m_disconnected) return;
    m_disconnected = true;
    this->app()->mx()->del(&m_idleTimer);
    this->app()->mx()->run([
      link = ZmMkRef(impl()), peer]() mutable {
	link->notifyDisconnected_(peer);
      }, this->app()->rxThread());
  }
  void notifyDisconnected_(bool peer) {
    m_stream.disconnected(peer);
    m_session.disconnected(*impl(), peer);
    this->app()->disconnected(*impl(), peer);
    if (m_counted) {
      m_counted = false;
      this->app()->release();
    }
    this->app()->linkDrained_();
  }
  int process(typename Traits::RxStream &rx) {
    if (m_stream.stream()) return m_stream.process(rx);
    if (m_stream.terminal()) return 0;
    int rc = m_session.process(*impl(), rx);
    if (m_stream.stream()) rc = m_stream.process(rx);
    if (rc >= 0) touch();
    return rc;
  }
  bool streamEnable(bool enabled = true) {
    return m_stream.enable(enabled);
  }
  bool streamAccept() { return m_stream.accept(*impl(), *this); }
  bool streamLocalCap() const { return m_stream.localCap(); }
  bool streamPeerCap() const { return m_stream.peerCap(); }
  template <typename L>
  void streamTx(L &&l) {
    if (!m_stream.tx()) return;
    auto tx = this->txStream();
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() {
    if (!m_stream.end()) return;
    this->app()->txRun([link = ZmMkRef(impl())]() mutable {
      auto app = link->app();
      app->rxRun([link = ZuMv(link)]() mutable {
	Traits::disconnect(*link);
      });
    });
  }
  void streamTxReset() {
    if (m_stream.reset()) this->disconnect();
  }
  template <typename Stream>
  void streamProcess(Stream stream) {
    Link_::streamProcess(m_session, *impl(), ZuMv(stream), 0);
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) {
    return parser.process(rx);
  }
  template <typename Builder>
  auto transmit(Builder &) {
    return this->txStream();
  }
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
  Session		m_session;
  ZmScheduler::Timer	m_idleTimer;
  StreamBinding		m_stream;
  EndpointString	m_remote;
  bool			m_counted = true;
  bool			m_disconnected = false;
};

} // namespace Zhttp

#endif /* ZhttpLink_HH */
