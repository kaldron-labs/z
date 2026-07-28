//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - transport-neutral engine configuration

#ifndef ZhttpConfig_HH
#define ZhttpConfig_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiIP.hh>

class ZiMultiplex;

namespace Zhttp {

struct TCP { };
struct TLS { };
struct QUIC { };

struct Transport {
  enum { TCP, TLS, QUIC };
};

struct Version {
  enum { H1, H2, H3 };
};

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

struct Migration {
  enum { Disabled, Passive, Active };
};

struct ProtocolPolicy {
  enum { ForceH3, PreferH3, DisableH3 };
};

struct H2Policy {
  enum { Force, Prefer, Disable };
};

struct EndpointSource {
  enum { Origin, HTTPS, IPv4Hint, IPv6Hint, AltSvc };
};

struct DiscoveryLimits {
  unsigned	maxRecords = 16;
  unsigned	maxHints = 16;
  unsigned	maxEndpoints = 32;
  unsigned	maxAliasDepth = 8;
};

struct ResultCode {
  enum {
    OK,
    Failed,
    Cancelled,
    TimedOut,
    RedirectLimit,
    InvalidRedirect,
    ReplayUnsafe,
    Unprocessed,
    Indeterminate
  };
};

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
  int8_t	code = ResultCode::OK;
  int8_t	transport = Transport::TCP;
  int8_t	httpVersion = Version::H1;

  bool ok() const { return code == ResultCode::OK; }
};

namespace BodyDeflt {
  enum {
    // Bound one producer turn without imposing transport flow control.
    TxBatch = 1<<16
  };
}

struct BodyProduce {
  enum {
    More,		// producer made progress and has more input
    Done,		// producer reached the source boundary
    Failed		// source or producer failed
  };
};

struct BodySend {
  enum {
    More,		// another bounded Tx turn is required
    Complete,		// request framing and final boundary were submitted
    Failed,		// production or framing failed
    Cancelled		// Tx ownership was cancelled and drained
  };
};

struct BodyCommit {
  uint64_t	produced = 0;
  uint64_t	committed = 0;
  uint64_t	reset = 0;
  uint64_t	discarded = 0;
  bool		headers = false;
  bool		final = false;
};

namespace Body {

struct EmptyCursor { };

struct None {
  using Cursor = EmptyCursor;
  enum { HasBody = false, Optional = false, Streaming = false };
};

template <typename Cursor_>
struct Fixed {
  using Cursor = Cursor_;
  enum { HasBody = true, Optional = false, Streaming = false };
};

template <typename Cursor_>
struct OptionalFixed {
  using Cursor = Cursor_;
  enum { HasBody = true, Optional = true, Streaming = false };
};

template <typename Cursor_>
struct Stream {
  using Cursor = Cursor_;
  enum { HasBody = true, Optional = false, Streaming = true };
};

template <typename Cursor_>
struct OptionalStream {
  using Cursor = Cursor_;
  enum { HasBody = true, Optional = true, Streaming = true };
};

} // namespace Body

struct AgentEventType {
  enum {
    Selected,
    AttemptFailed,
    Redirected,
    Retried,
    Fallback,
    Completed,
    Cancelled,
    Stopping
  };
};

struct AgentEvent {
  uint64_t	request = 0;
  uint64_t	attempt = 0;
  uint64_t	previousAttempt = 0;
  uint32_t	status = 0;
  uint16_t	redirects = 0;
  uint16_t	retries = 0;
  int8_t	type = AgentEventType::Selected;
  int8_t	result = ResultCode::OK;
  int8_t	transport = Transport::TCP;
  int8_t	httpVersion = Version::H1;
  int8_t	endpointSource = EndpointSource::Origin;
  bool		transient = false;
  bool		responseStarted = false;
};

class AgentConfig {
public:
  unsigned concurrency() const { return m_concurrency; }
  unsigned maxPending() const { return m_maxPending; }
  unsigned admissionBatch() const { return m_admissionBatch; }
  unsigned requestTimeout() const { return m_requestTimeout; }
  unsigned maxRedirects() const { return m_maxRedirects; }
  unsigned maxRetries() const { return m_maxRetries; }
  unsigned maxOrigins() const { return m_maxOrigins; }
  unsigned maxAltSvc() const { return m_maxAltSvc; }
  unsigned bodyTxBatch() const { return m_bodyTxBatch; }
  const DiscoveryLimits &discoveryLimits() const {
    return m_discoveryLimits;
  }
  int8_t protocol() const { return m_protocol; }
  int8_t h2Policy() const { return m_h2Policy; }
  bool blindH3() const { return m_blindH3; }
  bool altSvcCrossHost() const { return m_altSvcCrossHost; }
  bool tcp() const { return m_tcp; }
  bool tls() const { return m_tls; }
  bool quic() const { return m_quic; }

  AgentConfig &concurrency(unsigned v) {
    m_concurrency = v;
    return *this;
  }
  AgentConfig &maxPending(unsigned v) {
    m_maxPending = v;
    return *this;
  }
  AgentConfig &admissionBatch(unsigned v) {
    m_admissionBatch = v;
    return *this;
  }
  AgentConfig &requestTimeout(unsigned v) {
    m_requestTimeout = v;
    return *this;
  }
  AgentConfig &maxRedirects(unsigned v) {
    m_maxRedirects = v;
    return *this;
  }
  AgentConfig &maxRetries(unsigned v) {
    m_maxRetries = v;
    return *this;
  }
  AgentConfig &maxOrigins(unsigned v) {
    m_maxOrigins = v;
    return *this;
  }
  AgentConfig &maxAltSvc(unsigned v) {
    m_maxAltSvc = v;
    return *this;
  }
  AgentConfig &bodyTxBatch(unsigned v) {
    m_bodyTxBatch = v;
    return *this;
  }
  AgentConfig &discoveryLimits(DiscoveryLimits v) {
    m_discoveryLimits = v;
    return *this;
  }
  AgentConfig &protocol(int8_t v) {
    m_protocol = v;
    return *this;
  }
  AgentConfig &h2Policy(int8_t v) {
    m_h2Policy = v;
    return *this;
  }
  AgentConfig &blindH3(bool v) { m_blindH3 = v; return *this; }
  AgentConfig &altSvcCrossHost(bool v) {
    m_altSvcCrossHost = v;
    return *this;
  }
  AgentConfig &tcp(bool v) { m_tcp = v; return *this; }
  AgentConfig &tls(bool v) { m_tls = v; return *this; }
  AgentConfig &quic(bool v) { m_quic = v; return *this; }

private:
  unsigned	m_concurrency = 1;
  unsigned	m_maxPending = 1024;
  unsigned	m_admissionBatch = 256;
  unsigned	m_requestTimeout = 0;
  unsigned	m_maxRedirects = 8;
  unsigned	m_maxRetries = 0;
  unsigned	m_maxOrigins = 256;
  unsigned	m_maxAltSvc = 8;
  unsigned	m_bodyTxBatch = BodyDeflt::TxBatch;
  DiscoveryLimits m_discoveryLimits;
  int8_t	m_protocol = ProtocolPolicy::PreferH3;
  int8_t	m_h2Policy = H2Policy::Prefer;
  bool		m_blindH3 = false;
  bool		m_altSvcCrossHost = false;
  bool		m_tcp = true;
  bool		m_tls = true;
  bool		m_quic = true;
};

ZhttpAPI int8_t migrationMode(
  ZuCSpan, int8_t deflt = Migration::Passive);

ZuDerive(ConfigString, ZtString<ZtStringHeapID<"Zhttp.Config">>);
ZuDerive(ALPNString, ZtString<ZtStringHeapID<"Zhttp.ALPN">>);

struct ConnectedInfo {
  ALPNString	alpn;
  uint32_t	version = 0;
  int8_t	transport = Transport::TCP;
  int8_t	httpVersion = Version::H1;
  bool		secure = false;
  bool		multiplexed = false;
};

class EngineConfig {
public:
  EngineConfig(
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

  EngineConfig &mx(ZiMultiplex *v) { m_mx = v; return *this; }
  EngineConfig &rxThread(ZuCSpan v) { m_rxThread = v; return *this; }
  EngineConfig &txThread(ZuCSpan v) { m_txThread = v; return *this; }
  EngineConfig &asyncThread(ZuCSpan v) { m_asyncThread = v; return *this; }

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
  ZuCSpan caPath() const { return TLSConfig::caPath(); }
  ZuCSpan certPath() const { return TLSConfig::certPath(); }
  ZuCSpan keyPath() const { return TLSConfig::keyPath(); }
  bool mTLS() const { return TLSConfig::mTLS(); }
  int cacheTimeout() const { return TLSConfig::cacheTimeout(); }
  uint32_t headerTableSize() const { return m_headerTableSize; }
  uint32_t maxHeaderListSize() const { return m_maxHeaderListSize; }
  uint32_t initialWindowSize() const { return m_initialWindowSize; }
  uint32_t maxFrameSize() const { return m_maxFrameSize; }
  uint32_t maxConcurrentStreams() const { return m_maxConcurrentStreams; }
  uint32_t maxStreamID() const { return m_maxStreamID; }
  uint32_t maxPending() const { return m_maxPending; }
  uint32_t maxQueuedFrames() const { return m_maxQueuedFrames; }
  unsigned settingsTimeout() const { return m_settingsTimeout; }
  unsigned drainTimeout() const { return m_drainTimeout; }
  int8_t policy() const { return m_policy; }
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
  H2Config &headerTableSize(uint32_t v) {
    m_headerTableSize = v;
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
  H2Config &policy(int8_t v) { m_policy = v; return *this; }
  H2Config &extendedConnect(bool v) {
    m_extendedConnect = v;
    return *this;
  }

private:
  uint32_t	m_headerTableSize = 4096;
  uint32_t	m_maxHeaderListSize = 1U<<16;
  uint32_t	m_initialWindowSize = (1U<<16) - 1;
  uint32_t	m_maxFrameSize = 1U<<14;
  uint32_t	m_maxConcurrentStreams = 100;
  uint32_t	m_maxStreamID = 0x7fffffffU;
  uint32_t	m_maxPending = 1024;
  uint32_t	m_maxQueuedFrames = 4096;
  unsigned	m_settingsTimeout = 10;
  unsigned	m_drainTimeout = 30;
  int8_t	m_policy = H2Policy::Force;
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
    DefltMigrationCIDReserve = 1
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
  double rxDrop() const { return m_rxDrop; }
  double txDrop() const { return m_txDrop; }
  const ZiSockAddr &migrationLocal() const { return m_migrationLocal; }
  uint64_t migrateAfterBytes() const { return m_migrateAfterBytes; }
  int8_t migration() const { return m_migration; }
  bool migrateOnOpen() const { return m_migrateOnOpen; }
  bool migrateAfterHeaders() const { return m_migrateAfterHeaders; }
  bool migrationCloseOnFailure() const {
    return m_migrationCloseOnFailure;
  }
  bool ecn() const { return m_ecn; }
  bool qlog() const { return m_qlog; }
  bool extendedConnect() const { return m_extendedConnect; }

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
  QUICConfig &migration(int8_t v) { m_migration = v; return *this; }
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
  double	m_rxDrop = 0;
  double	m_txDrop = 0;
  int8_t	m_migration = Migration::Passive;
  bool		m_migrateOnOpen = false;
  bool		m_migrateAfterHeaders = false;
  bool		m_migrationCloseOnFailure = false;
  bool		m_ecn = false;
  bool		m_qlog = false;
  bool		m_extendedConnect = false;
};

} // namespace Zhttp

#endif /* ZhttpConfig_HH */
