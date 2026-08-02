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

template <typename Consumer, typename Stream, typename Rx>
auto process(Consumer &consumer, Stream stream, Rx &rx, int) ->
    decltype(int(consumer.process(ZuMv(stream), rx)))
{
  return int(consumer.process(ZuMv(stream), rx));
}
template <typename Consumer, typename Stream, typename Rx>
int process(Consumer &, Stream, Rx &, ...) { return 0; }

template <typename Consumer, typename Stream>
auto peerEnd(Consumer &consumer, Stream stream, int) ->
    decltype(consumer.peerEnd(ZuMv(stream)), void())
{
  consumer.peerEnd(ZuMv(stream));
}
template <typename Consumer, typename Stream>
void peerEnd(Consumer &, Stream, ...) { }

template <typename Consumer, typename Stream>
auto error(Consumer &consumer, Stream stream, int) ->
    decltype(consumer.error(ZuMv(stream)), void())
{
  consumer.error(ZuMv(stream));
}
template <typename Consumer, typename Stream>
void error(Consumer &, Stream, ...) { }

} // namespace Link_

template <typename App, typename Impl, typename Profile>
class ClientLink :
  public ProfileTraits<Profile>::Transport::template ClientLink<App, Impl> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;
  using Base = typename Traits::template ClientLink<App, Impl>;
  using StreamBinding =
    H1::StreamBinding<Impl, Impl>;

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
    if (!m_stream.stream()) return rc;
    int streamRC = m_stream.process(rx);
    return streamRC ? streamRC : rc;
  }
  bool streamEnable(bool enabled = true) {
    return m_stream.enable(enabled);
  }
  bool streamAccept() { return m_stream.accept(*impl(), *impl()); }
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
      link->streamTxEnd_();
    });
  }
  void sent(ZmRef<ZiTxBuf>, bool ok) {
    if (!m_streamEnd || (ok && this->txQueue.count_())) return;
    m_streamEnd = false;
    streamTxClose_();
  }
  void streamTxReset() {
    if (m_stream.reset()) this->disconnect();
  }
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Link_::process(*this->app(), ZuMv(stream), rx, 0);
  }
  template <typename Stream>
  void peerEnd(Stream stream) {
    Link_::peerEnd(*this->app(), ZuMv(stream), 0);
  }
  template <typename Stream>
  void error(Stream stream) {
    Link_::error(*this->app(), ZuMv(stream), 0);
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
  void streamTxEnd_() {
    if (this->txQueue.count_()) {
      m_streamEnd = true;
      return;
    }
    streamTxClose_();
  }
  void streamTxClose_() {
    auto app = this->app();
    app->rxRun([link = ZmMkRef(impl())]() mutable {
      Traits::disconnect(*link);
    });
  }

  StreamBinding	m_stream;
  bool		m_connected = false;
  bool		m_failed = false;
  bool		m_cancelled = false;
  bool		m_streamEnd = false;	// Tx-owned
};

template <
  typename App, typename Impl, typename Profile, typename Session>
class ServerLink :
  public ProfileTraits<Profile>::Transport::template ServerLink<App, Impl> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;
  using Base = typename Traits::template ServerLink<App, Impl>;
  using StreamBinding =
    H1::StreamBinding<Impl, Impl>;

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
    if (m_stream.stream()) {
      int streamRC = m_stream.process(rx);
      if (streamRC) rc = streamRC;
    }
    if (rc >= 0) touch();
    return rc;
  }
  bool streamEnable(bool enabled = true) {
    return m_stream.enable(enabled);
  }
  bool streamAccept() { return m_stream.accept(*impl(), *impl()); }
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
      link->streamTxEnd_();
    });
  }
  void sent(ZmRef<ZiTxBuf>, bool ok) {
    if (!m_streamEnd || (ok && this->txQueue.count_())) return;
    m_streamEnd = false;
    streamTxClose_();
  }
  void streamTxReset() {
    if (m_stream.reset()) this->disconnect();
  }
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Link_::process(m_session, ZuMv(stream), rx, 0);
  }
  template <typename Stream>
  void peerEnd(Stream stream) {
    Link_::peerEnd(m_session, ZuMv(stream), 0);
  }
  template <typename Stream>
  void error(Stream stream) {
    Link_::error(m_session, ZuMv(stream), 0);
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
  void streamTxEnd_() {
    if (this->txQueue.count_()) {
      m_streamEnd = true;
      return;
    }
    streamTxClose_();
  }
  void streamTxClose_() {
    auto app = this->app();
    app->rxRun([link = ZmMkRef(impl())]() mutable {
      Traits::disconnect(*link);
    });
  }

  Session		m_session;
  ZmScheduler::Timer	m_idleTimer;
  StreamBinding		m_stream;
  EndpointString	m_remote;
  bool			m_counted = true;
  bool			m_disconnected = false;
  bool			m_streamEnd = false;	// Tx-owned
};

} // namespace Zhttp

#endif /* ZhttpLink_HH */
