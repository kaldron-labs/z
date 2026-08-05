//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - transport-neutral hub configuration

#ifndef ZhttpConfig_HH
#define ZhttpConfig_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiIP.hh>

class ZiMultiplex;

namespace Zhttp {

struct TCP { };
struct TLS { };
struct QUIC { };

ZtEnumStruct(ZhttpAPI, Transport, int8_t, TCP, TLS, QUIC);
ZtEnumStruct(ZhttpAPI, Version, int8_t, H1, H2, H3);

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
ZtEnumStruct(ZhttpAPI, ProtocolPolicy, int8_t,
  ForceH3, PreferH3, DisableH3);
ZtEnumStruct(ZhttpAPI, H2Policy, int8_t, Force, Prefer, Disable);
ZtEnumStruct(ZhttpAPI, EndpointSource, int8_t,
  Origin, HTTPS, IPv4Hint, IPv6Hint, AltSvc);

struct DiscoveryLimits {
  unsigned	maxRecords = 16;
  unsigned	maxHints = 16;
  unsigned	maxEndpoints = 32;
  unsigned	maxAliasDepth = 8;
};

ZtEnumStruct(ZhttpAPI, ResultCode, int8_t,
  OK, Failed, Cancelled, TimedOut, RedirectLimit, InvalidRedirect,
  ReplayUnsafe, Unprocessed, Indeterminate);

struct Result {
  uint64_t	request = 0;
  uint64_t	attempt = 0;
  uint64_t	requestBodyProduced = 0;
  uint64_t	requestBodyCommitted = 0;
  uint64_t	requestBodyReset = 0;
  uint64_t	requestBodyDiscarded = 0;
  uint64_t	responseBodyReceived = 0;
  uint64_t	responseBodyConsumed = 0;
  uint64_t	responseBodyReset = 0;
  uint64_t	responseBodyDiscarded = 0;
  uint32_t	status = 0;
  uint16_t	redirects = 0;
  uint16_t	retries = 0;
  ResultCode::T	code = ResultCode::OK;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;

  bool ok() const { return code == ResultCode::OK; }
};

struct BodyCommit {
  uint64_t	produced = 0;
  uint64_t	committed = 0;
  uint64_t	reset = 0;
  uint64_t	discarded = 0;
  bool		headers = false;
  bool		final = false;
};

ZtEnumStruct(ZhttpAPI, ResponseOutcome, int8_t,
  Success, BuildFailed, TxFailed, Reset, Cancelled);

struct ResponseResult {
  BodyCommit		body;
  ResponseOutcome::T	outcome = ResponseOutcome::BuildFailed;
};

namespace Body {

struct None {
  enum { HasBody = false, Optional = false, Streaming = false };
};

struct Fixed {
  enum { HasBody = true, Optional = false, Streaming = false };
};

struct OptionalFixed {
  enum { HasBody = true, Optional = true, Streaming = false };
};

struct Stream {
  enum { HasBody = true, Optional = false, Streaming = true };
};

struct OptionalStream {
  enum { HasBody = true, Optional = true, Streaming = true };
};

} // namespace Body

struct HeaderPad : public ZuPrintable {
  HeaderPad(unsigned length_, uint8_t fill_ = 0xff) :
    length{length_}, fill{fill_} { }

  template <typename S>
  void print(S &s) const {
    for (unsigned i = length; i; --i) s << char(fill);
  }

  unsigned	length;
  uint8_t	fill;

  friend ZuPrintFn ZuPrintType(HeaderPad *);
};

template <typename T> struct IsHeaderPad : public ZuFalse { };
template <> struct IsHeaderPad<HeaderPad> : public ZuTrue { };

ZtEnumStruct(ZhttpAPI, ClientEventType, int8_t,
  Selected, AttemptFailed, Redirected, Retried, Fallback, Completed,
  Cancelled);

struct ClientEvent {
  uint64_t	request = 0;
  uint64_t	attempt = 0;
  uint64_t	previousAttempt = 0;
  uint32_t	status = 0;
  uint16_t	redirects = 0;
  uint16_t	retries = 0;
  ClientEventType::T type = ClientEventType::Selected;
  ResultCode::T	result = ResultCode::OK;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;
  EndpointSource::T endpointSource = EndpointSource::Origin;
  bool		transient = false;
  bool		responseStarted = false;
};

class ClientConfig {
public:
  unsigned concurrency() const { return m_concurrency; }
  unsigned requestTimeout() const { return m_requestTimeout; }
  unsigned maxRedirects() const { return m_maxRedirects; }
  unsigned maxRetries() const { return m_maxRetries; }
  unsigned maxOrigins() const { return m_maxOrigins; }
  unsigned maxAltSvc() const { return m_maxAltSvc; }
  uint64_t retainedBodyMax() const { return m_retainedBodyMax; }
  uint64_t retainedMessageMax() const { return m_retainedMessageMax; }
  const DiscoveryLimits &discoveryLimits() const {
    return m_discoveryLimits;
  }
  ProtocolPolicy::T protocol() const { return m_protocol; }
  H2Policy::T h2Policy() const { return m_h2Policy; }
  bool blindH3() const { return m_blindH3; }
  bool altSvcCrossHost() const { return m_altSvcCrossHost; }
  bool tcp() const { return m_tcp; }
  bool tls() const { return m_tls; }
  bool quic() const { return m_quic; }

  ClientConfig &concurrency(unsigned v) {
    m_concurrency = v;
    return *this;
  }
  ClientConfig &requestTimeout(unsigned v) {
    m_requestTimeout = v;
    return *this;
  }
  ClientConfig &maxRedirects(unsigned v) {
    m_maxRedirects = v;
    return *this;
  }
  ClientConfig &maxRetries(unsigned v) {
    m_maxRetries = v;
    return *this;
  }
  ClientConfig &maxOrigins(unsigned v) {
    m_maxOrigins = v;
    return *this;
  }
  ClientConfig &maxAltSvc(unsigned v) {
    m_maxAltSvc = v;
    return *this;
  }
  ClientConfig &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    return *this;
  }
  ClientConfig &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
    return *this;
  }
  ClientConfig &discoveryLimits(DiscoveryLimits v) {
    m_discoveryLimits = v;
    return *this;
  }
  ClientConfig &protocol(ProtocolPolicy::T v) {
    m_protocol = v;
    return *this;
  }
  ClientConfig &h2Policy(H2Policy::T v) {
    m_h2Policy = v;
    return *this;
  }
  ClientConfig &blindH3(bool v) { m_blindH3 = v; return *this; }
  ClientConfig &altSvcCrossHost(bool v) {
    m_altSvcCrossHost = v;
    return *this;
  }
  ClientConfig &tcp(bool v) { m_tcp = v; return *this; }
  ClientConfig &tls(bool v) { m_tls = v; return *this; }
  ClientConfig &quic(bool v) { m_quic = v; return *this; }

private:
  unsigned	m_concurrency = 1;
  unsigned	m_requestTimeout = 0;
  unsigned	m_maxRedirects = 8;
  unsigned	m_maxRetries = 0;
  unsigned	m_maxOrigins = 256;
  unsigned	m_maxAltSvc = 8;
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  DiscoveryLimits m_discoveryLimits;
  ProtocolPolicy::T m_protocol = ProtocolPolicy::PreferH3;
  H2Policy::T	m_h2Policy = H2Policy::Prefer;
  bool		m_blindH3 = false;
  bool		m_altSvcCrossHost = false;
  bool		m_tcp = true;
  bool		m_tls = true;
  bool		m_quic = true;
};

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

} // namespace Zhttp

#endif /* ZhttpConfig_HH */
