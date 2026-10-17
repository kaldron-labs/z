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
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiRxStream.hh>

#include <zlib/Ztcp.hh>

#include <zlib/Ztls.hh>

#include <zlib/Zquic.hh>

#include <zlib/ZhttpTransport.hh>
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpProfiles.hh>
#include <zlib/ZhttpConfig.hh>

namespace Zhttp {

namespace Transport_ {

enum {
  H1BufSize = 8<<10,		// common request/response buffer pool block
  H1BufMax = 100<<20		// maximum configured HTTP body/buffer growth
};

using TxBufNode = ZiTxQueue::Node;

inline TxBufNode *txBufNode(ZiIOBuf *buf)
{
  return static_cast<TxBufNode *>(buf);
}

inline const TxBufNode *txBufNode(const ZiIOBuf *buf)
{
  return static_cast<const TxBufNode *>(buf);
}

using TCPRxBufAlloc = Ztcp::RxBufAlloc<
  H1BufSize, H1BufMax, "Zhttp.TCP.Rx">;
using TCPTxBufAlloc = Zi::IOBufAlloc<
  TxBufNode, H1BufSize, H1BufMax, ZuStringT<"Zhttp.TCP.Tx">>;
using TLSRxBufAlloc = Ztls::RxBufAlloc<
  H1BufSize, H1BufMax, "Zhttp.TLS.Rx">;
using TLSTxBufAlloc = Zi::IOBufAlloc<
  TxBufNode, H1BufSize, H1BufMax, ZuStringT<"Zhttp.TLS.Tx">>;
using H3TxBufAlloc = Zquic::StreamTxBufAlloc<
  Zquic::BufSize, ZiIOBuf_DefltMaxSize, "Zhttp.H3.Tx">;

template <typename Link, typename = void>
struct HasTxStream : public ZuFalse { };
template <typename Link>
struct HasTxStream<Link,
  decltype(ZuDeclVal<Link &>().txStream(), void())> : public ZuTrue { };

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

  template <typename App> using Client = Ztcp::Client<App>;
  template <typename App> using Server = Ztcp::Server<App>;
  template <typename App, typename Link>
  using ClientLink =
    Ztcp::CliLink<App, Link, TCPRxBufAlloc, TCPTxBufAlloc>;
  template <typename App, typename Link>
  using ServerLink =
    Ztcp::SrvLink<App, Link, TCPRxBufAlloc, TCPTxBufAlloc>;

  static ClientParams clientParams(
    const HubConfig &hub, const TCPConfig &)
  {
    return ClientParams{
      hub.mx(), hub.rxThread(), hub.txThread()};
  }
  static ServerParams serverParams(
    const HubConfig &hub, const TCPConfig &)
  {
    return ServerParams{
      hub.mx(), hub.rxThread(), hub.txThread()};
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

  template <typename App> using Client = Ztls::Client<App>;
  template <typename App> using Server = Ztls::Server<App>;
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
      .caPath(config.caPath()).certPath(config.certPath())
      .keyPath(config.keyPath()).alpn(ZuSpan<ZuCSpan>{"http/1.1"})
      .mTLS(config.mTLS()).cacheTimeout(config.cacheTimeout());
    return params;
  }
  static ConnectedInfo connected(Connected info) {
    return {
      .alpn = ALPNString{
	info.alpn.data(), unsigned(info.alpn.length())},
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
      .alpn = ALPNString{
	info.alpn.data(), unsigned(info.alpn.length())},
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

// Rx-owned Upgrade state and application dispatch.  Transport teardown has
// already drained Rx and Tx when the generic link calls disconnected().
template <typename Link, typename Consumer>
class StreamBinding {
public:
  struct State {
    ZtEnumValues(int8_t, HTTP, Stream, Terminal);
  };
  struct Term {
    enum { None, End, Reset };
  };

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
    if (!m_peerCap) return;
    if (m_state == State::Stream) {
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
  public NativeTraits<Profile>::Transport::template ClientLink<App, Impl> {
  using HTTP = NativeTraits<Profile>;
  using Traits = HTTP::Transport;
  using Base = Traits::template ClientLink<App, Impl>;
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
  ZuInline int process(Traits::RxStream &rx) {
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
  template <typename L>
  void streamTx_(L &&l) {
    if (!m_stream.tx()) return;
    auto tx = this->txStream_();
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() {
    if (!m_stream.end()) return;
    this->app()->txRun([link = ZmRef(impl())]() mutable {
      link->streamTxEnd_();
    });
  }
  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    Base::sent(ZuMv(buf), ok);
    if (!m_streamEnd || (ok && this->txQueue.count_())) return;
    m_streamEnd = false;
    streamTxClose_();
  }
  void streamTxReset() {
    if (m_stream.reset()) this->disconnect();
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
    return this->txStream();
  }
  template <typename Builder>
  auto transmit_(Builder &) {
    return this->txStream_();
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
    app->rxRun([link = ZmRef(impl())]() mutable {
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
  public NativeTraits<Profile>::Transport::template ServerLink<App, Impl> {
  using HTTP = NativeTraits<Profile>;
  using Traits = HTTP::Transport;
  using Base = Traits::template ServerLink<App, Impl>;
  using StreamBinding =
    H1::StreamBinding<Impl, Impl>;

public:
  enum { TLS = Traits::Secure, Multiplexed = HTTP::Multiplexed };

  ServerLink(App *app, const ZiCxnInfo &ci) :
    Base{app}, m_remoteIP{ci.remoteIP}, m_remotePort{ci.remotePort}
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
    m_stream.disconnected(peer);
    m_session.disconnected(*impl(), peer);
    this->app()->disconnected(*impl(), peer);
    // Response cleanup is Tx-owned. Release its final native reference on Rx
    // before reporting completion to the hub's stop/registry machinery.
    this->app()->txRun([link = ZmRef(impl())]() mutable {
      auto app = link->app();
      app->rxRun([link = ZuMv(link)]() mutable {
	link->releaseDisconnected_();
	auto app = link->app();
	link = nullptr;
	app->linkDrained_();
      });
    });
  }
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
  template <typename L>
  void streamTx_(L &&l) {
    if (!m_stream.tx()) return;
    auto tx = this->txStream_();
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() {
    if (!m_stream.end()) return;
    this->app()->txRun([link = ZmRef(impl())]() mutable {
      link->streamTxEnd_();
    });
  }
  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    Base::sent(ZuMv(buf), ok);
    if (!m_streamEnd || (ok && this->txQueue.count_())) return;
    m_streamEnd = false;
    streamTxClose_();
  }
  void streamTxReset() {
    if (m_stream.reset()) this->disconnect();
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
    return this->txStream();
  }
  template <typename Builder>
  auto transmit_(Builder &) {
    return this->txStream_();
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
  void releaseDisconnected_() {
    if (m_counted) {
      m_counted = false;
      this->app()->release();
    }
  }
  void streamTxEnd_() {
    if (this->txQueue.count_()) {
      m_streamEnd = true;
      return;
    }
    streamTxClose_();
  }
  void streamTxClose_() {
    auto app = this->app();
    app->rxRun([link = ZmRef(impl())]() mutable {
      Traits::disconnect(*link);
    });
  }

  Session		m_session;
  ZmScheduler::Timer	m_idleTimer;
  StreamBinding		m_stream;
  ZiIP			m_remoteIP;
  uint16_t		m_remotePort = 0;
  bool			m_counted = true;
  bool			m_disconnected = false;
  bool			m_streamEnd = false;	// Tx-owned
};

namespace Native_ {

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

template <typename Stream>
auto produced(const Stream &stream, int) -> decltype(stream.produced()) {
  return stream.produced();
}
template <typename Stream>
uint64_t produced(const Stream &, long) { return 0; }

template <typename Stream>
auto failed(const Stream &stream, int) -> decltype(stream.valid(), bool()) {
  return !stream.valid();
}
template <typename Stream>
bool failed(const Stream &stream, long) { return stream.failed(); }

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
    [](void *ctx, unsigned length) { static_cast<Stream *>(ctx)->commit(length); }};
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

template <typename Link>
auto streamID(const Link &link, int) -> decltype(link.streamID()) {
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

template <typename String>
const WriterOps &stringWriterOps()
{
  static constexpr WriterOps ops{
    [](void *ctx, ZuBSpan span) { *static_cast<String *>(ctx) << span; },
    [](void *ctx, const BufferPrint &value) { *static_cast<String *>(ctx) << value; },
    [](void *ctx, ZmRef<ZiIOBuf> buf) {
      *static_cast<String *>(ctx) << ZuBSpan{buf->data(), buf->length};
    },
    [](void *) { return true; },
    [](const void *) { return false; },
    [](const void *ctx) -> uint64_t { return static_cast<const String *>(ctx)->length(); },
    [](const void *) { return unsigned(UINT_MAX); },
    [](const void *) { return 0U; },
    [](const void *) { return 0U; },
    [](void *, unsigned head) -> ZmRef<ZiIOBuf> {
      using Alloc = Zi::IOBufAlloc<ZiRxQueue::Node,
	ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZuStringT<"Zhttp.Header.Print">>;
      auto buf = ZmRef<ZiIOBuf>{new Alloc{}};
      buf->skip = head;
      return buf;
    },
    [](void *ctx, unsigned length) -> ZuSpan<uint8_t> {
      auto &out = *static_cast<String *>(ctx);
      auto offset = out.length();
      return {out.ensure(offset + length) + offset, length};
    },
    [](void *ctx, unsigned length) {
      auto &out = *static_cast<String *>(ctx);
      out.length(out.length() + length);
    }};
  return ops;
}

} // namespace Native_

namespace Native_ {

template <typename Out>
auto printFailed(const Out &out, int) -> decltype(out.ok(), bool()) { return !out.ok(); }
template <typename Out>
bool printFailed(const Out &, long) { return false; }

template <typename Out>
auto printReserve(Out &out, unsigned length, int) -> decltype(out.reserve(length)) {
  return out.reserve(length);
}
template <typename Out>
auto printReserve(Out &out, unsigned length, long) ->
    decltype(out.ensure(length), ZuSpan<uint8_t>()) {
  auto offset = out.length();
  return {out.ensure(offset + length) + offset, length};
}
template <typename Out>
auto printCommit(Out &out, unsigned length, int) -> decltype(out.commit(length), void()) {
  out.commit(length);
}
template <typename Out>
auto printCommit(Out &out, unsigned length, long) -> decltype(out.length(length), void()) {
  out.length(out.length() + length);
}

template <typename Out>
const WriterOps &printWriterOps()
{
  static constexpr WriterOps ops{
    [](void *ctx, ZuBSpan span) { *static_cast<Out *>(ctx) << span; },
    [](void *ctx, const BufferPrint &value) { *static_cast<Out *>(ctx) << value; },
    [](void *ctx, ZmRef<ZiIOBuf> buf) {
      *static_cast<Out *>(ctx) << ZuBSpan{buf->data(), buf->length};
    },
    [](void *ctx) { return !printFailed(*static_cast<Out *>(ctx), 0); },
    [](const void *ctx) { return printFailed(*static_cast<const Out *>(ctx), 0); },
    [](const void *) -> uint64_t { return 0; },
    [](const void *) { return unsigned(UINT_MAX); },
    [](const void *) { return 0U; },
    [](const void *) { return 0U; },
    [](void *, unsigned head) -> ZmRef<ZiIOBuf> {
      using Alloc = Zi::IOBufAlloc<ZiRxQueue::Node,
	ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZuStringT<"Zhttp.Header.Print">>;
      auto buf = ZmRef<ZiIOBuf>{new Alloc{}};
      buf->skip = head;
      return buf;
    },
    [](void *ctx, unsigned length) -> ZuSpan<uint8_t> {
      return printReserve(*static_cast<Out *>(ctx), length, 0);
    },
    [](void *ctx, unsigned length) { printCommit(*static_cast<Out *>(ctx), length, 0); }};
  return ops;
}

struct Print {
  PrintView value;

  template <typename Out>
  void print(Out &out) const {
    Writer writer{&out, printWriterOps<Out>()};
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

template <typename Stream, typename Value>
auto mutableField(Stream &stream, ZuBSpan name, const Value &value, int) ->
    decltype(stream.fieldMutable(name, value), FieldOffset()) {
  auto span = stream.fieldMutable(name, value);
  return {span.offset, span.length};
}
template <typename Stream, typename Value>
FieldOffset mutableField(Stream &stream, ZuBSpan name, const Value &value, long) {
  stream.field(name, value);
  return {};
}

template <typename Stream, typename HeapID>
FieldOutput compressedFields(Stream &stream)
{
  return {&stream,
    [](void *ctx, FieldForm::T form, int, const FieldClass *,
	PrintView key, PrintView value) -> FieldOffset {
      auto &stream = *static_cast<Stream *>(ctx);
      auto emit = [&stream, form, value](ZuBSpan name) -> FieldOffset {
	// The old undeclared-header provider omitted empty names.
	if (form == FieldForm::Literal && !name) return {};
	FieldOffset span;
	print(value, [&stream, form, name, &span](const auto &value_) {
	  switch (form) {
	    case FieldForm::Fixed: stream.fieldFixed(name, value_); break;
	    case FieldForm::Name: stream.fieldName(name, value_); break;
	    case FieldForm::Literal: stream.fieldLiteral(name, value_); break;
	    case FieldForm::Mutable: span = mutableField(stream, name, value_, 0); break;
	  }
	});
	return span;
      };
      if (key.isSpan()) return emit(key.span);
      ZtBArray<ZtArrayHeapID<HeapID{}()>> name;
      Writer out{&name, stringWriterOps<decltype(name)>()};
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
    destroy = [](void *ptr) { delete static_cast<Native *>(ptr); };
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
      pool->mx()->invoke([link = ZuMv(link)]() mutable { link->disconnect(); }, pool->rxThread());
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
