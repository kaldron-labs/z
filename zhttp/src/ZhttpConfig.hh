//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP endpoint configuration

#ifndef ZhttpConfig_HH
#define ZhttpConfig_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuTime.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZhttpProfiles.hh>

class ZiMultiplex;

namespace Zhttp {

ZtEnumStruct(ZhttpAPI, Migration, int8_t, Disabled, Passive, Active);
ZtEnumStruct(ZhttpAPI, H2Policy, int8_t, Force, Prefer, Disable);
ZhttpAPI Migration::T migrationMode(
  ZuCSpan, Migration::T deflt = Migration::Passive);

ZuDerive(ConfigString, ZtString<ZtStringHeapID<"Zhttp.Config">>);

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


struct DiscoveryLimits {
  unsigned	maxRecords = 16;
  unsigned	maxHints = 16;
  unsigned	maxEndpoints = 32;
  unsigned	maxAliasDepth = 8;
};


ZtEnumStruct(ZhttpAPI, ProtoPolicy, int8_t,
  ForceH3, PreferH3, DisableH3);

class Config {
  friend class ProfileCore;

public:
  struct Field {
    enum {
      Links = 1U<<0,
      Concurrency = 1U<<1,
      LinkMax = 1U<<2,
      RequestTimeout = 1U<<3,
      MaxRedirects = 1U<<4,
      MaxRetries = 1U<<5,
      MaxOrigins = 1U<<6,
      MaxAltSvc = 1U<<7,
      RetainedBodyMax = 1U<<8,
      RetainedMessageMax = 1U<<9,
      DiscoveryLimits = 1U<<10,
      Protocol = 1U<<11,
      H2Policy = 1U<<12,
      BlindH3 = 1U<<13,
      AltSvcCrossHost = 1U<<14,
      Secure = 1U<<15,
      TCP = 1U<<16,
      TLS = 1U<<17,
      QUIC = 1U<<18
    };
  };

  unsigned links() const { return m_links; }
  unsigned concurrency() const { return m_concurrency; }
  // Per-link in-flight operation ceiling: H1 pipeline depth or H2/H3 streams.
  unsigned linkMax() const { return m_linkMax; }
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
  ProtoPolicy::T protocol() const { return m_protocol; }
  H2Policy::T h2Policy() const { return m_h2Policy; }
  bool blindH3() const { return m_blindH3; }
  bool altSvcCrossHost() const { return m_altSvcCrossHost; }
  bool secure() const { return m_secure; }
  bool tcp() const { return m_tcp; }
  bool tls() const { return m_tls; }
  bool quic() const { return m_quic; }

  bool valid() const {
    if (!m_links || !m_concurrency || !m_linkMax ||
	m_protocol < ProtoPolicy::ForceH3 ||
	m_protocol > ProtoPolicy::DisableH3 ||
	m_h2Policy < H2Policy::Force || m_h2Policy > H2Policy::Disable)
      return false;
    if (!m_secure) return m_tcp;
    switch (m_protocol) {
      case ProtoPolicy::ForceH3: return m_quic;
      case ProtoPolicy::PreferH3: return m_tls || m_quic;
      case ProtoPolicy::DisableH3: return m_tls;
      default: return false;
    }
  }

  Config &links(unsigned v) {
    m_links = v;
    m_set |= Field::Links;
    return *this;
  }
  Config &concurrency(unsigned v) {
    m_concurrency = v;
    m_set |= Field::Concurrency;
    return *this;
  }
  Config &linkMax(unsigned v) {
    m_linkMax = v;
    m_set |= Field::LinkMax;
    return *this;
  }
  Config &requestTimeout(unsigned v) {
    m_requestTimeout = v;
    m_set |= Field::RequestTimeout;
    return *this;
  }
  Config &maxRedirects(unsigned v) {
    m_maxRedirects = v;
    m_set |= Field::MaxRedirects;
    return *this;
  }
  Config &maxRetries(unsigned v) {
    m_maxRetries = v;
    m_set |= Field::MaxRetries;
    return *this;
  }
  Config &maxOrigins(unsigned v) {
    m_maxOrigins = v;
    m_set |= Field::MaxOrigins;
    return *this;
  }
  Config &maxAltSvc(unsigned v) {
    m_maxAltSvc = v;
    m_set |= Field::MaxAltSvc;
    return *this;
  }
  Config &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    m_set |= Field::RetainedBodyMax;
    return *this;
  }
  Config &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
    m_set |= Field::RetainedMessageMax;
    return *this;
  }
  Config &discoveryLimits(DiscoveryLimits v) {
    m_discoveryLimits = v;
    m_set |= Field::DiscoveryLimits;
    return *this;
  }
  Config &protocol(ProtoPolicy::T v) {
    m_protocol = v;
    m_set |= Field::Protocol;
    return *this;
  }
  Config &h2Policy(H2Policy::T v) {
    m_h2Policy = v;
    m_set |= Field::H2Policy;
    return *this;
  }
  Config &blindH3(bool v) {
    m_blindH3 = v;
    m_set |= Field::BlindH3;
    return *this;
  }
  Config &altSvcCrossHost(bool v) {
    m_altSvcCrossHost = v;
    m_set |= Field::AltSvcCrossHost;
    return *this;
  }
  Config &secure(bool v) {
    m_secure = v;
    m_set |= Field::Secure;
    return *this;
  }
  Config &tcp(bool v) {
    m_tcp = v; m_set |= Field::TCP; return *this;
  }
  Config &tls(bool v) {
    m_tls = v; m_set |= Field::TLS; return *this;
  }
  Config &quic(bool v) {
    m_quic = v; m_set |= Field::QUIC; return *this;
  }

  Config overlay(const Config &o) const {
    Config v{*this};
#define Zhttp_Config_Overlay(bit, member) \
    if (o.m_set &bit) v.member = o.member
    Zhttp_Config_Overlay(Field::Links, m_links);
    Zhttp_Config_Overlay(Field::Concurrency, m_concurrency);
    Zhttp_Config_Overlay(Field::LinkMax, m_linkMax);
    Zhttp_Config_Overlay(Field::RequestTimeout, m_requestTimeout);
    Zhttp_Config_Overlay(Field::MaxRedirects, m_maxRedirects);
    Zhttp_Config_Overlay(Field::MaxRetries, m_maxRetries);
    Zhttp_Config_Overlay(Field::MaxOrigins, m_maxOrigins);
    Zhttp_Config_Overlay(Field::MaxAltSvc, m_maxAltSvc);
    Zhttp_Config_Overlay(Field::RetainedBodyMax, m_retainedBodyMax);
    Zhttp_Config_Overlay(Field::RetainedMessageMax, m_retainedMessageMax);
    Zhttp_Config_Overlay(Field::DiscoveryLimits, m_discoveryLimits);
    Zhttp_Config_Overlay(Field::Protocol, m_protocol);
    Zhttp_Config_Overlay(Field::H2Policy, m_h2Policy);
    Zhttp_Config_Overlay(Field::BlindH3, m_blindH3);
    Zhttp_Config_Overlay(Field::AltSvcCrossHost, m_altSvcCrossHost);
    Zhttp_Config_Overlay(Field::Secure, m_secure);
    Zhttp_Config_Overlay(Field::TCP, m_tcp);
    Zhttp_Config_Overlay(Field::TLS, m_tls);
    Zhttp_Config_Overlay(Field::QUIC, m_quic);
#undef Zhttp_Config_Overlay
    v.m_set |= o.m_set;
    return v;
  }

private:
  // Called once on the merged raw configuration, before policy resolution.
  // Derived defaults leave explicit-setting provenance unchanged.
  Config selected(bool tcp, bool h1, bool h2, bool h3) const {
    Config v{*this};
    if (!(v.m_set & Field::Secure)) v.m_secure = h1 || h2 || h3;
    if (!(v.m_set & Field::TCP)) v.m_tcp = tcp;
    if (!(v.m_set & Field::TLS)) v.m_tls = h1 || h2;
    if (!(v.m_set & Field::QUIC)) v.m_quic = h3;
    if (!(v.m_set & Field::Protocol))
      v.m_protocol = h3 ? ProtoPolicy::PreferH3 : ProtoPolicy::DisableH3;
    if (!(v.m_set & Field::H2Policy))
      v.m_h2Policy = h2 ?
	(h1 ? H2Policy::Prefer : H2Policy::Force) : H2Policy::Disable;
    v.m_tcp &= tcp;
    v.m_tls &= (h1 && v.m_h2Policy != H2Policy::Force) ||
      (h2 && v.m_h2Policy != H2Policy::Disable);
    v.m_quic &= h3;
    return v;
  }

  unsigned	m_links = 1;
  unsigned	m_concurrency = 1;
  unsigned	m_linkMax = 1;
  unsigned	m_requestTimeout = 0;
  unsigned	m_maxRedirects = 8;
  unsigned	m_maxRetries = 0;
  unsigned	m_maxOrigins = 256;
  unsigned	m_maxAltSvc = 8;
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  DiscoveryLimits m_discoveryLimits;
  ProtoPolicy::T m_protocol = ProtoPolicy::PreferH3;
  H2Policy::T	m_h2Policy = H2Policy::Prefer;
  bool		m_blindH3 = false;
  bool		m_altSvcCrossHost = false;
  bool		m_secure = true;
  bool		m_tcp = true;
  bool		m_tls = true;
  bool		m_quic = true;
  uint32_t	m_set = 0;
};


class ServerConfig {
public:
  ServerConfig() { m_tls.policy(H2Policy::Prefer); }

  const ZiIP &localIP() const { return m_localIP; }
  uint16_t port() const { return m_port; }
  unsigned idleTimeout() const { return m_idleTimeout; }
  unsigned maxConnections() const { return m_maxConnections; }
  unsigned maxRequests() const { return m_maxRequests; }
  unsigned altSvcMaxAge() const { return m_altSvcMaxAge; }
  uint64_t retainedBodyMax() const { return m_retainedBodyMax; }
  uint64_t retainedMessageMax() const { return m_retainedMessageMax; }
  uint64_t retainedBytesMax() const { return m_retainedBytesMax; }
  bool tcpEnabled() const { return m_tcpEnabled; }
  bool tlsEnabled() const { return m_tlsEnabled; }
  bool quicEnabled() const { return m_quicEnabled; }
  const TCPConfig &tcpConfig() const { return m_tcp; }
  const H2Config &tlsConfig() const { return m_tls; }
  const QUICConfig &quicConfig() const { return m_quic; }
  QUICConfig quicHubConfig() const {
    QUICConfig config{m_quic};
    // QUIC transport parameters use milliseconds; server idleTimeout uses
    // seconds.  An explicit QUIC value overrides the transport-neutral default.
    if (!config.maxIdleTimeout() && m_idleTimeout)
      config.maxIdleTimeout(uint64_t(m_idleTimeout) * 1000);
    return config;
  }

  ServerConfig &localIP(ZiIP v) { m_localIP = ZuMv(v); return *this; }
  ServerConfig &port(uint16_t v) { m_port = v; return *this; }
  ServerConfig &idleTimeout(unsigned v) {
    m_idleTimeout = v;
    return *this;
  }
  ServerConfig &maxConnections(unsigned v) {
    m_maxConnections = v;
    return *this;
  }
  ServerConfig &maxRequests(unsigned v) {
    m_maxRequests = v;
    return *this;
  }
  ServerConfig &altSvcMaxAge(unsigned v) {
    m_altSvcMaxAge = v;
    return *this;
  }
  ServerConfig &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    return *this;
  }
  ServerConfig &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
    return *this;
  }
  ServerConfig &retainedBytesMax(uint64_t v) {
    m_retainedBytesMax = v;
    return *this;
  }
  ServerConfig &tcp(TCPConfig v = {}) {
    m_tcp = ZuMv(v);
    m_tcpEnabled = true;
    return *this;
  }
  ServerConfig &tls(H2Config v) {
    m_tls = ZuMv(v);
    m_tlsEnabled = true;
    return *this;
  }
  ServerConfig &quic(QUICConfig v) {
    m_quic = ZuMv(v);
    m_quicEnabled = true;
    return *this;
  }

private:
  ZiIP		m_localIP;
  TCPConfig	m_tcp;
  H2Config	m_tls;
  QUICConfig	m_quic;
  unsigned	m_idleTimeout = 0;
  unsigned	m_maxConnections = 0;
  unsigned	m_maxRequests = uint32_t(-1);
  unsigned	m_altSvcMaxAge = 86400;
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  uint64_t	m_retainedBytesMax = uint64_t(-1);
  uint16_t	m_port = 0;
  bool		m_tcpEnabled = false;
  bool		m_tlsEnabled = false;
  bool		m_quicEnabled = false;
};

} // namespace Zhttp

#endif /* ZhttpConfig_HH */
