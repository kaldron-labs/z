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

#include <zlib/ZhttpCore.hh>

namespace Zhttp {

struct TCP { };
struct TLS { };
struct QUIC { };

ZtEnumStruct(ZhttpAPI, Transport, int8_t, TCP, TLS, QUIC);
template <typename Protocol_, int Version_>
struct Profile {
  using Protocol = Protocol_;
  enum {
    HTTPVersion = Version_,
    Multiplexed = Version_ != Version::H1
  };
};

using H1TCP = Profile<TCP, Version::H1>;
using H1TLS = Profile<TLS, Version::H1>;
using H2TLS = Profile<TLS, Version::H2>;
using H3QUIC = Profile<QUIC, Version::H3>;

template <typename> struct IsProfile : public ZuFalse { };
template <> struct IsProfile<H1TCP> : public ZuTrue { };
template <> struct IsProfile<H1TLS> : public ZuTrue { };
template <> struct IsProfile<H2TLS> : public ZuTrue { };
template <> struct IsProfile<H3QUIC> : public ZuTrue { };

ZtEnumStruct(ZhttpAPI, Migration, int8_t, Disabled, Passive, Active);
ZtEnumStruct(ZhttpAPI, H2Policy, int8_t, Force, Prefer, Disable);
ZhttpAPI Migration::T migrationMode(
  ZuCSpan, Migration::T deflt = Migration::Passive);

ZuDerive(ConfigString, ZtString<ZtStringHeapID<"Zhttp.Config">>);
ZuDerive(ALPNString, ZtString<ZtStringHeapID<"Zhttp.ALPN">>);

struct ConnectedInfo {
  ALPNString	alpn;
  uint32_t	version = 0;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;
  bool		secure = false;
  bool		multiplexed = false;
};

class HubConfig {
public:
  HubConfig(
    ZiMultiplex *mx = nullptr,
    ZuCSpan rxThread = {},
    ZuCSpan txThread = {})
  :
    m_mx{mx}, m_rxThread{rxThread}, m_txThread{txThread}
  {
  }

  ZiMultiplex *mx() const { return m_mx; }
  ZuCSpan rxThread() const { return m_rxThread; }
  ZuCSpan txThread() const { return m_txThread; }
  ZuCSpan asyncThread() const { return m_asyncThread; }

  HubConfig &mx(ZiMultiplex *v) { m_mx = v; return *this; }
  HubConfig &rxThread(ZuCSpan v) { m_rxThread = v; return *this; }
  HubConfig &txThread(ZuCSpan v) { m_txThread = v; return *this; }
  HubConfig &asyncThread(ZuCSpan v) { m_asyncThread = v; return *this; }

private:
  ZiMultiplex	*m_mx = nullptr;
  ConfigString	m_rxThread;
  ConfigString	m_txThread;
  ConfigString	m_asyncThread;
};

class TCPConfig {
};

class TLSConfig {
public:
  ZuCSpan caPath() const { return m_caPath; }
  ZuCSpan certPath() const { return m_certPath; }
  ZuCSpan keyPath() const { return m_keyPath; }
  bool mTLS() const { return m_mTLS; }
  int cacheTimeout() const { return m_cacheTimeout; }

  TLSConfig &caPath(ZuCSpan v) { m_caPath = v; return *this; }
  TLSConfig &certPath(ZuCSpan v) { m_certPath = v; return *this; }
  TLSConfig &keyPath(ZuCSpan v) { m_keyPath = v; return *this; }
  TLSConfig &mTLS(bool v) { m_mTLS = v; return *this; }
  TLSConfig &cacheTimeout(int v) { m_cacheTimeout = v; return *this; }

private:
  ConfigString	m_caPath;
  ConfigString	m_certPath;
  ConfigString	m_keyPath;
  int		m_cacheTimeout = -1;
  bool		m_mTLS = false;
};

class H2Config : public TLSConfig {
public:
  enum { MaxHPackCapacity = 1U<<24 };

  ZuCSpan caPath() const { return TLSConfig::caPath(); }
  ZuCSpan certPath() const { return TLSConfig::certPath(); }
  ZuCSpan keyPath() const { return TLSConfig::keyPath(); }
  bool mTLS() const { return TLSConfig::mTLS(); }
  int cacheTimeout() const { return TLSConfig::cacheTimeout(); }
  uint32_t hpackRxCapacity() const { return m_hpackRxCapacity; }
  uint32_t hpackTxCapacity() const { return m_hpackTxCapacity; }
  uint32_t maxHeaderListSize() const { return m_maxHeaderListSize; }
  uint32_t initialWindowSize() const { return m_initialWindowSize; }
  uint32_t maxFrameSize() const { return m_maxFrameSize; }
  uint32_t maxConcurrentStreams() const { return m_maxConcurrentStreams; }
  uint32_t maxStreamID() const { return m_maxStreamID; }
  uint32_t maxPending() const { return m_maxPending; }
  uint32_t maxQueuedFrames() const { return m_maxQueuedFrames; }
  unsigned settingsTimeout() const { return m_settingsTimeout; }
  unsigned drainTimeout() const { return m_drainTimeout; }
  H2Policy::T policy() const { return m_policy; }
  bool extendedConnect() const { return m_extendedConnect; }

  H2Config &caPath(ZuCSpan v) {
    TLSConfig::caPath(v);
    return *this;
  }
  H2Config &certPath(ZuCSpan v) {
    TLSConfig::certPath(v);
    return *this;
  }
  H2Config &keyPath(ZuCSpan v) {
    TLSConfig::keyPath(v);
    return *this;
  }
  H2Config &mTLS(bool v) {
    TLSConfig::mTLS(v);
    return *this;
  }
  H2Config &cacheTimeout(int v) {
    TLSConfig::cacheTimeout(v);
    return *this;
  }
  H2Config &hpackRxCapacity(uint32_t v) {
    m_hpackRxCapacity = v;
    return *this;
  }
  H2Config &hpackTxCapacity(uint32_t v) {
    m_hpackTxCapacity = v;
    return *this;
  }
  H2Config &maxHeaderListSize(uint32_t v) {
    m_maxHeaderListSize = v;
    return *this;
  }
  H2Config &initialWindowSize(uint32_t v) {
    m_initialWindowSize = v;
    return *this;
  }
  H2Config &maxFrameSize(uint32_t v) {
    m_maxFrameSize = v;
    return *this;
  }
  H2Config &maxConcurrentStreams(uint32_t v) {
    m_maxConcurrentStreams = v;
    return *this;
  }
  H2Config &maxStreamID(uint32_t v) {
    m_maxStreamID = v;
    return *this;
  }
  H2Config &maxPending(uint32_t v) {
    m_maxPending = v;
    return *this;
  }
  H2Config &maxQueuedFrames(uint32_t v) {
    m_maxQueuedFrames = v;
    return *this;
  }
  H2Config &settingsTimeout(unsigned v) {
    m_settingsTimeout = v;
    return *this;
  }
  H2Config &drainTimeout(unsigned v) {
    m_drainTimeout = v;
    return *this;
  }
  H2Config &policy(H2Policy::T v) { m_policy = v; return *this; }
  H2Config &extendedConnect(bool v) {
    m_extendedConnect = v;
    return *this;
  }

private:
  uint32_t	m_hpackRxCapacity = 4096;
  uint32_t	m_hpackTxCapacity = 4096;
  uint32_t	m_maxHeaderListSize = 1U<<16;
  uint32_t	m_initialWindowSize = (1U<<16) - 1;
  uint32_t	m_maxFrameSize = 1U<<14;
  uint32_t	m_maxConcurrentStreams = 100;
  uint32_t	m_maxStreamID = 0x7fffffffU;
  uint32_t	m_maxPending = 1024;
  uint32_t	m_maxQueuedFrames = 4096;
  unsigned	m_settingsTimeout = 10;
  unsigned	m_drainTimeout = 30;
  H2Policy::T	m_policy = H2Policy::Force;
  bool		m_extendedConnect = false;
};

class QUICConfig {
public:
  // HTTP transfer windows: enough for large bodies without connection-level
  // head-of-line flow-control stalls; applications may lower them explicitly.
  static constexpr uint64_t DefltMaxData = uint64_t(100)<<20;
  static constexpr uint64_t DefltMaxStreamData = uint64_t(16)<<20;
  static constexpr uint64_t RoleDefault = uint64_t(-1);
  enum {
    // Request concurrency defaults; the server limit also bounds admission.
    DefltClientStreams = 100,
    DefltServerStreams = 4096,
    // Three mandatory H3 streams plus bounded diagnostic/extension headroom.
    DefltControlStreams = 16,
    DefltMaxQueuedFrames = 4096,
    DefltMigrationCIDReserve = 1,
    // Bound per-connection QPACK storage to 16MiB/1M sections.  These are
    // trusted local policy limits, not protocol limits or peer permissions.
    MaxQPackCapacity = 1U<<24,
    MaxQPackSections = 1U<<20
  };

  ZuCSpan caPath() const { return m_caPath; }
  ZuCSpan certPath() const { return m_certPath; }
  ZuCSpan keyPath() const { return m_keyPath; }
  ZuCSpan keyLogPath() const { return m_keyLogPath; }
  ZuCSpan qlogPath() const { return m_qlogPath; }
  uint64_t maxData() const { return m_maxData; }
  uint64_t maxStreamData() const { return m_maxStreamData; }
  uint64_t maxStreamsDuplex() const { return m_maxStreamsDuplex; }
  uint64_t maxStreamsSimplex() const { return m_maxStreamsSimplex; }
  uint64_t maxIdleTimeout() const { return m_maxIdleTimeout; }
  ZuTime heartbeat() const { return m_heartbeat; }
  unsigned maxUDP() const { return m_maxUDP; }
  unsigned migrationCIDReserve() const { return m_migrationCIDReserve; }
  unsigned qlogRingSize() const { return m_qlogRingSize; }
  unsigned qlogAge() const { return m_qlogAge; }
  unsigned qpackRxCapacity() const { return m_qpackRxCapacity; }
  unsigned qpackTxCapacity() const { return m_qpackTxCapacity; }
  unsigned qpackRxBlocked() const { return m_qpackRxBlocked; }
  unsigned qpackTxSections() const { return m_qpackTxSections; }
  unsigned maxQueuedFrames() const { return m_maxQueuedFrames; }
  double rxDrop() const { return m_rxDrop; }
  double txDrop() const { return m_txDrop; }
  const ZiSockAddr &migrationLocal() const { return m_migrationLocal; }
  uint64_t migrateAfterBytes() const { return m_migrateAfterBytes; }
  Migration::T migration() const { return m_migration; }
  bool migrateOnOpen() const { return m_migrateOnOpen; }
  bool migrateAfterHeaders() const { return m_migrateAfterHeaders; }
  bool migrationCloseOnFailure() const {
    return m_migrationCloseOnFailure;
  }
  bool ecn() const { return m_ecn; }
  bool qlog() const { return m_qlog; }
  bool extendedConnect() const { return m_extendedConnect; }
  bool qpackValid() const {
    return m_qpackRxCapacity <= MaxQPackCapacity &&
      m_qpackTxCapacity <= MaxQPackCapacity &&
      m_qpackRxBlocked <= MaxQPackSections &&
      m_qpackTxSections <= MaxQPackSections;
  }

  QUICConfig &caPath(ZuCSpan v) { m_caPath = v; return *this; }
  QUICConfig &certPath(ZuCSpan v) { m_certPath = v; return *this; }
  QUICConfig &keyPath(ZuCSpan v) { m_keyPath = v; return *this; }
  QUICConfig &keyLogPath(ZuCSpan v) { m_keyLogPath = v; return *this; }
  QUICConfig &qlogPath(ZuCSpan v) { m_qlogPath = v; return *this; }
  QUICConfig &maxData(uint64_t v) { m_maxData = v; return *this; }
  QUICConfig &maxStreamData(uint64_t v) {
    m_maxStreamData = v;
    return *this;
  }
  QUICConfig &maxStreamsDuplex(uint64_t v) {
    m_maxStreamsDuplex = v;
    return *this;
  }
  QUICConfig &maxStreamsSimplex(uint64_t v) {
    m_maxStreamsSimplex = v;
    return *this;
  }
  QUICConfig &maxIdleTimeout(uint64_t v) {
    m_maxIdleTimeout = v;
    return *this;
  }
  QUICConfig &heartbeat(ZuTime v) { m_heartbeat = v; return *this; }
  QUICConfig &maxUDP(unsigned v) { m_maxUDP = v; return *this; }
  QUICConfig &migrationCIDReserve(unsigned v) {
    m_migrationCIDReserve = v;
    return *this;
  }
  QUICConfig &qlogRingSize(unsigned v) {
    m_qlogRingSize = v;
    return *this;
  }
  QUICConfig &qlogAge(unsigned v) { m_qlogAge = v; return *this; }
  QUICConfig &qpackRxCapacity(unsigned v) {
    m_qpackRxCapacity = v;
    return *this;
  }
  QUICConfig &qpackTxCapacity(unsigned v) {
    m_qpackTxCapacity = v;
    return *this;
  }
  QUICConfig &qpackRxBlocked(unsigned v) {
    m_qpackRxBlocked = v;
    return *this;
  }
  QUICConfig &qpackTxSections(unsigned v) {
    m_qpackTxSections = v;
    return *this;
  }
  QUICConfig &maxQueuedFrames(unsigned v) {
    m_maxQueuedFrames = v;
    return *this;
  }
  QUICConfig &rxDrop(double v) { m_rxDrop = v; return *this; }
  QUICConfig &txDrop(double v) { m_txDrop = v; return *this; }
  QUICConfig &migrationLocal(const ZiSockAddr &v) {
    m_migrationLocal = v;
    return *this;
  }
  QUICConfig &migrateAfterBytes(uint64_t v) {
    m_migrateAfterBytes = v;
    return *this;
  }
  QUICConfig &migration(Migration::T v) { m_migration = v; return *this; }
  QUICConfig &migrateOnOpen(bool v) {
    m_migrateOnOpen = v;
    return *this;
  }
  QUICConfig &migrateAfterHeaders(bool v) {
    m_migrateAfterHeaders = v;
    return *this;
  }
  QUICConfig &migrationCloseOnFailure(bool v) {
    m_migrationCloseOnFailure = v;
    return *this;
  }
  QUICConfig &ecn(bool v) { m_ecn = v; return *this; }
  QUICConfig &qlog(bool v) { m_qlog = v; return *this; }
  QUICConfig &extendedConnect(bool v) {
    m_extendedConnect = v;
    return *this;
  }

private:
  ConfigString	m_caPath;
  ConfigString	m_certPath;
  ConfigString	m_keyPath;
  ConfigString	m_keyLogPath;
  ConfigString	m_qlogPath;
  ZiSockAddr	m_migrationLocal;
  uint64_t	m_maxData = DefltMaxData;
  uint64_t	m_maxStreamData = DefltMaxStreamData;
  uint64_t	m_maxStreamsDuplex = RoleDefault;
  uint64_t	m_maxStreamsSimplex = DefltControlStreams;
  uint64_t	m_maxIdleTimeout = 0;
  uint64_t	m_migrateAfterBytes = 0;
  ZuTime	m_heartbeat;
  unsigned	m_maxUDP = 0;
  unsigned	m_migrationCIDReserve = DefltMigrationCIDReserve;
  unsigned	m_qlogRingSize = 0;
  unsigned	m_qlogAge = 0;
  unsigned	m_qpackRxCapacity = 0;
  unsigned	m_qpackTxCapacity = 0;
  unsigned	m_qpackRxBlocked = 0;
  unsigned	m_qpackTxSections = 0;
  unsigned	m_maxQueuedFrames = DefltMaxQueuedFrames;
  double	m_rxDrop = 0;
  double	m_txDrop = 0;
  Migration::T	m_migration = Migration::Passive;
  bool		m_migrateOnOpen = false;
  bool		m_migrateAfterHeaders = false;
  bool		m_migrationCloseOnFailure = false;
  bool		m_ecn = false;
  bool		m_qlog = false;
  bool		m_extendedConnect = false;
};

namespace Transport_ {

enum {
  H1BufSize = 8<<10,		// common request/response buffer pool block
  H1BufMax = 100<<20		// maximum configured HTTP body/buffer growth
};

using TxCompleteFn = ZmFn<void(bool),
  ZmFnHeapID<"Zhttp.TxComplete">>;

// The HTTP transports already retain the final wire buffer until the native
// socket send completes.  Keep the response fence on that buffer rather than
// building a parallel per-buffer tracker.
struct TxBufNode : public ZiTxQueue::Node {
  using Base = ZiTxQueue::Node;
  using Base::Base;

  ~TxBufNode() { txComplete = {}; }

  void complete(bool ok) {
    auto fn = ZuMv(txComplete);
    txComplete = {};
    if (fn) fn(ok);
  }

  TxCompleteFn txComplete;
};

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

inline Ztls::HubParams tlsParams(
  const HubConfig &hub, const TLSConfig &config)
{
  return Ztls::HubParams{
    hub.mx(), hub.rxThread(), hub.txThread()}
    .asyncThread(hub.asyncThread())
    .caPath(config.caPath()).certPath(config.certPath())
    .keyPath(config.keyPath()).alpn(ZuSpan<ZuCSpan>{"http/1.1"});
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
      .alpn = info.alpn,
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
      .alpn = info.alpn,
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
struct ProfileTraits {
  using Profile = Profile_;
  using Protocol = typename Profile::Protocol;
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
  static ConnectedInfo connected(typename Transport::Connected info) {
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
  template <typename Stream>
  auto operator ()(Stream &stream) const ->
    decltype(stream.flush(), void());
};

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
  template <typename Builder>
  auto transmitTx_(Builder &) {
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
  public ProfileTraits<Profile>::Transport::template ServerLink<App, Impl> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;
  using Base = typename Traits::template ServerLink<App, Impl>;
  using StreamBinding =
    H1::StreamBinding<Impl, Impl>;

public:
  enum { TLS = Traits::Secure, Multiplexed = HTTP::Multiplexed };
  using TxCompleteFn = Transport_::TxCompleteFn;

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
      link = ZmRef(impl()), peer]() mutable {
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
    this->app()->txRun([link = ZmRef(impl())]() mutable {
      link->streamTxEnd_();
    });
  }
  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    ZiIOBuf *sent = buf.ptr();
    auto fn = ZuMv(Transport_::txBufNode(sent)->txComplete);
    Transport_::txBufNode(sent)->txComplete = {};
    if (m_txLast == sent) {
      m_txLast = nullptr;
      m_txOK = ok;
    }
    if (!ok) {
      auto i = this->txQueue.iter();
      while (auto queued = i())
        Transport_::txBufNode(queued)->complete(false);
    }
    Base::sent(ZuMv(buf), ok);
    if (fn)
      fn(ok);
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
  bool active() const { return !!this->cxn(); }
  using Base::send;
  bool send(ZmRef<ZiIOBuf> buf) {
    ZiIOBuf *last = buf.ptr();
    m_txLast = last;
    m_txReady = true;
    if (!Base::send(ZuMv(buf))) {
      if (m_txLast == last) m_txLast = nullptr;
      m_txOK = false;
      return false;
    }
    return true;
  }
  void txComplete(TxCompleteFn fn) {
    m_txComplete = ZuMv(fn);
  }
  void txCancel() {
    m_txComplete = {};
    m_txLast = nullptr;
    m_txReady = false;
  }
  bool txFence(TxCompleteFn fn) {
    if (!m_txReady) return false;
    m_txReady = false;
    if (!m_txLast) {
      fn(m_txOK);
      return true;
    }
    auto node = Transport_::txBufNode(m_txLast);
    if (node->txComplete) return false;
    node->txComplete = ZuMv(fn);
    m_txLast = nullptr;
    return true;
  }
  void finish() {
    auto fn = ZuMv(m_txComplete);
    m_txComplete = {};
    if (!fn) return;
    if (!m_txReady) {
      fn(false);
      return;
    }
    m_txReady = false;
    if (!m_txLast) {
      fn(m_txOK);
      return;
    }
    Transport_::txBufNode(m_txLast)->txComplete = ZuMv(fn);
    m_txLast = nullptr;
  }
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
    app->rxRun([link = ZmRef(impl())]() mutable {
      Traits::disconnect(*link);
    });
  }

  Session		m_session;
  ZmScheduler::Timer	m_idleTimer;
  StreamBinding		m_stream;
  ZiIP			m_remoteIP;
  uint16_t		m_remotePort = 0;
  TxCompleteFn		m_txComplete;
  ZiIOBuf		*m_txLast = nullptr;
  bool			m_txOK = true;
  bool			m_txReady = false;
  bool			m_counted = true;
  bool			m_disconnected = false;
  bool			m_streamEnd = false;	// Tx-owned
};



namespace Hubs_ {

template <typename Hub, typename = void>
struct HasStopAccepting : public ZuFalse { };
template <typename Hub>
struct HasStopAccepting<Hub,
  decltype(ZuDeclVal<Hub &>().stopAccepting(), void())> : public ZuTrue { };

using DoneFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.Hubs.Done">>;

struct Entry {
  using StartFn = void (*)(void *, DoneFn);
  using StopFn = void (*)(void *, DoneFn);
  using FinalFn = void (*)(void *);
  using StopAcceptingFn = void (*)(void *);

  void			*ptr = nullptr;
  StartFn		start = nullptr;
  StopFn		stop = nullptr;
  FinalFn		final = nullptr;
  StopAcceptingFn	stopAccepting = nullptr;
  bool			started = false;
};

} // namespace Hubs_

class Hubs : public ZmEngine<Hubs> {
public:
  using Engine = ZmEngine<Hubs>;
  using DoneFn = Hubs_::DoneFn;
  using Entries =
    ZtArray<Hubs_::Entry, ZtArrayHeapID<"Zhttp.Hubs">>;
  using Engine::running;
  using Engine::start;
  using Engine::state;
  using Engine::stop;
  using Engine::stopping;
  unsigned count() const { return m_entries.length(); }

  template <typename Done>
  void start(Done &&done) {
    Engine::start([done = DoneFn{ZuFwd<Done>(done)}](bool ok) mutable {
      done(ok);
    });
  }
  template <typename Done>
  void stop(Done &&done) {
    Engine::stop([done = DoneFn{ZuFwd<Done>(done)}](bool ok) mutable {
      done(ok);
    });
  }

  template <typename Hub, typename ...Args>
  bool init(Hub &hub, Args &&...args) {
    return Engine::lock(ZmEngineState::Stopped, [&]() {
      if (!hub.init(ZuFwd<Args>(args)...)) {
	final_();
	return false;
      }
      m_entries.push(Hubs_::Entry{
	.ptr = &hub,
	.start = [](void *ptr, DoneFn done) {
	  static_cast<Hub *>(ptr)->start(
	    [done = ZuMv(done)](bool ok) mutable { done(ok); });
	},
	.stop = [](void *ptr, DoneFn done) {
	  static_cast<Hub *>(ptr)->stop(
	    [done = ZuMv(done)](bool ok) mutable { done(ok); });
	},
	.final = [](void *ptr) {
	  static_cast<Hub *>(ptr)->final();
	},
	.stopAccepting = [](void *ptr) {
	  if constexpr (Hubs_::HasStopAccepting<Hub>{})
	    static_cast<Hub *>(ptr)->stopAccepting();
	}
      });
      return true;
    });
  }

  void final() {
    (void)Engine::stop();
    (void)Engine::lock(ZmEngineState::Stopped, [this]() {
      final_();
      return true;
    });
  }

private:
  friend Engine;

  void start_() {
    if (!m_entries) {
	Engine::started(false);
	return;
    }
    m_next = 0;
    startNext_();
  }

  void startNext_() {
    if (m_next >= m_entries.length()) {
      Engine::started(true);
      return;
    }
    unsigned i = m_next++;
    auto &entry = m_entries[i];
    entry.start(entry.ptr, DoneFn{this, [i](Hubs *hubs, bool ok) {
      hubs->started_(i, ok);
    }});
  }

  void started_(unsigned i, bool ok) {
    if (ok) {
      m_entries[i].started = true;
      startNext_();
      return;
    }
    m_rollback = true;
    m_stopOK = true;
    m_next = i;
    stopNext_();
  }

  void stop_() {
    m_rollback = false;
    m_stopOK = true;
    m_next = m_entries.length();
    for (auto &entry: m_entries)
      if (entry.started) entry.stopAccepting(entry.ptr);
    stopNext_();
  }

  void stopNext_() {
    while (m_next) {
      auto &entry = m_entries[--m_next];
      if (!entry.started) continue;
      entry.stop(entry.ptr,
	DoneFn{this, [](Hubs *hubs, bool ok) {
	  hubs->stopped_(ok);
	}});
      return;
    }
    if (m_rollback) {
      m_rollback = false;
      Engine::started(false);
    } else
      Engine::stopped(m_stopOK);
  }

  void stopped_(bool ok) {
    if (!ok) m_stopOK = false;
    m_entries[m_next].started = false;
    stopNext_();
  }

  void final_() {
    for (unsigned i = m_entries.length(); i; --i)
      m_entries[i - 1].final(m_entries[i - 1].ptr);
    m_entries.length(0);
  }

  Entries	m_entries;
  unsigned	m_next = 0;
  bool		m_stopOK = true;
  bool		m_rollback = false;
};


} // namespace Zhttp

#endif /* ZhttpTransport_HH */
