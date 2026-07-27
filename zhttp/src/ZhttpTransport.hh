//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - private native transport adaptation

#ifndef ZhttpTransport_HH
#define ZhttpTransport_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>

#include <zlib/ZhttpConfig.hh>

namespace Zhttp {

namespace Transport_ {

enum {
  H1BufSize = 8<<10,		// common request/response buffer pool block
  H1BufMax = 100<<20		// maximum configured HTTP body/buffer growth
};

using TCPRxBufAlloc = Ztcp::RxBufAlloc<
  H1BufSize, H1BufMax, "Zhttp.TCP.Rx">;
using TCPTxBufAlloc = Ztcp::TxBufAlloc<
  H1BufSize, H1BufMax, "Zhttp.TCP.Tx">;
using TLSRxBufAlloc = Ztls::RxBufAlloc<
  H1BufSize, H1BufMax, "Zhttp.TLS.Rx">;
using TLSTxBufAlloc = Ztls::TxBufAlloc<
  H1BufSize, H1BufMax, "Zhttp.TLS.Tx">;
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

inline Ztls::HubParams tlsParams(
  const EngineConfig &engine, const TLSConfig &config)
{
  return Ztls::HubParams{
    engine.mx(), engine.rxThread(), engine.txThread()}
    .asyncThread(engine.asyncThread())
    .caPath(config.caPath()).certPath(config.certPath())
    .keyPath(config.keyPath()).alpn(ZuSpan<ZuCSpan>{"http/1.1"});
}

inline Zquic::HubParams quicParams(
  const EngineConfig &engine, const QUICConfig &config,
  uint64_t defltStreams)
{
  Zquic::HubParams params{
    engine.mx(), engine.rxThread(), engine.txThread()};
  params
    .asyncThread(engine.asyncThread())
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
    HTTPVersion = Version::H1,
    Secure = false,
    Multiplexed = false,
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
    const EngineConfig &engine, const TCPConfig &)
  {
    return ClientParams{
      engine.mx(), engine.rxThread(), engine.txThread()};
  }
  static ServerParams serverParams(
    const EngineConfig &engine, const TCPConfig &)
  {
    return ServerParams{
      engine.mx(), engine.rxThread(), engine.txThread()};
  }
  static ConnectedInfo connected(Connected) {
    return {
      .transport = Transport::TCP,
      .httpVersion = Version::H1
    };
  }
  template <typename Engine>
  static bool startClient(Engine &engine) { return engine.start(); }
  template <typename Engine>
  static bool startServer(Engine &engine) { return engine.start(); }
  template <typename Engine>
  static void stopListening(Engine &engine) { engine.stopListening(); }
  template <typename Link>
  static void disconnect(Link &link) { link.disconnect_(); }
};

template <> struct Traits<TLS> {
  enum {
    ID = Transport::TLS,
    HTTPVersion = Version::H1,
    Secure = true,
    Multiplexed = false,
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
    const EngineConfig &engine, const TLSConfig &config)
  {
    return tlsParams(engine, config);
  }
  static ServerParams serverParams(
    const EngineConfig &engine, const TLSConfig &config)
  {
    ServerParams params{
      engine.mx(), engine.rxThread(), engine.txThread()};
    params
      .asyncThread(engine.asyncThread())
      .caPath(config.caPath()).certPath(config.certPath())
      .keyPath(config.keyPath()).alpn(ZuSpan<ZuCSpan>{"http/1.1"})
      .mTLS(config.mTLS()).cacheTimeout(config.cacheTimeout());
    return params;
  }
  static ConnectedInfo connected(Connected info) {
    return {
      .alpn = info.alpn,
      .version = uint32_t(info.version),
      .transport = Transport::TLS,
      .httpVersion = Version::H1,
      .secure = true
    };
  }
  template <typename Engine>
  static bool startClient(Engine &engine) { return engine.start(); }
  template <typename Engine>
  static bool startServer(Engine &engine) { return engine.start(); }
  template <typename Engine>
  static void stopListening(Engine &engine) { engine.stopListening(); }
  template <typename Link>
  static void disconnect(Link &link) { link.disconnect_(); }
};

template <> struct Traits<QUIC> {
  enum {
    ID = Transport::QUIC,
    HTTPVersion = Version::H3,
    Secure = true,
    Multiplexed = true,
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
    const EngineConfig &engine, const QUICConfig &config)
  {
    return quicParams(
      engine, config, QUICConfig::DefltClientStreams);
  }
  static ServerParams serverParams(
    const EngineConfig &engine, const QUICConfig &config)
  {
    return quicParams(
      engine, config, QUICConfig::DefltServerStreams);
  }
  static ConnectedInfo connected(Connected info) {
    return {
      .alpn = info.alpn,
      .version = info.version,
      .transport = Transport::QUIC,
      .httpVersion = Version::H3,
      .secure = true,
      .multiplexed = true
    };
  }
  template <typename Engine>
  static bool startClient(Engine &engine) { return engine.start(); }
  template <typename Engine>
  static bool startServer(Engine &engine) { return engine.start(); }
  template <typename Engine>
  static void stopListening(Engine &) { }
  template <typename Link>
  static void disconnect(Link &link) { link.disconnect_(); }
};

} // namespace Transport_

} // namespace Zhttp

#endif /* ZhttpTransport_HH */
