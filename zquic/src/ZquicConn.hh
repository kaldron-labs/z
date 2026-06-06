//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC connection bootstrap helpers

#ifndef ZquicConn_HH
#define ZquicConn_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZquicTransportParams.hh>

namespace Zquic {

struct VersionNegotiation {
  static bool supported(uint32_t version) { return version == Version1; }
  static uint32_t preferred() { return Version1; }

  static uint32_t choose(const uint32_t *versions, unsigned nVersions) {
    for (unsigned i = 0; i < nVersions; ++i)
      if (supported(versions[i])) return versions[i];
    return 0;
  }

  static int write(
    uint8_t *out, unsigned len, const ConnectionID &dcid,
    const ConnectionID &scid) {
    uint32_t versions[] = { Version1 };
    return Packet::writeVersionNegotiation(out, len, dcid, scid, versions, 1);
  }

  static int parse(
    ZuCSpan in, uint32_t *versions, unsigned capacity, unsigned &nVersions) {
    return Packet::parseVersionNegotiation(in, versions, capacity, nVersions);
  }
};

struct ServerPacketDecision {
  ServerPacketAction::T	action = ServerPacketAction::Drop;
  LongHeader		header;
  int			responseLength = 0;
};

struct ServerPacket {
  static ServerPacketDecision routeLongHeader(
    ZuCSpan, uint8_t *response, unsigned responseLen);
};

class StatelessResetToken {
public:
  static constexpr unsigned Length = 16;

  StatelessResetToken() = default;
  explicit StatelessResetToken(ZuCSpan token) { set(token); }

  bool set(ZuCSpan);
  bool generate();
  bool valid() const { return m_valid; }
  unsigned length() const { return m_valid ? Length : 0; }
  const uint8_t *data() const { return m_data; }
  ZuCSpan cspan() const {
    return ZuCSpan{
      reinterpret_cast<const char *>(m_data), m_valid ? Length : 0};
  }

  bool equals(const StatelessResetToken &) const;

  friend inline bool operator ==(
    const StatelessResetToken &l, const StatelessResetToken &r) {
    return l.equals(r);
  }

private:
  uint8_t	m_data[Length] = {};
  bool		m_valid = false;
};

struct StatelessReset {
  static constexpr unsigned TokenLength = StatelessResetToken::Length;
  static constexpr unsigned MinLength = 21;

  static int writeForUnknownCID(
    uint8_t *, unsigned, ZuCSpan receivedPacket,
    const StatelessResetToken &);
};

struct CIDSlot {
  ConnectionID	cid;
  uint64_t	sequence = 0;
  uintptr_t	routeToken = 0;
  StatelessResetToken resetToken;
  CIDState::T	state = CIDState::Active;
};

class CIDRouter {
public:
  static constexpr unsigned Max = 16;

  bool add(const ConnectionID &cid, uint64_t sequence, uintptr_t token) {
    return add(cid, sequence, token, {});
  }
  bool add(
    const ConnectionID &cid, uint64_t sequence, uintptr_t token,
    const StatelessResetToken &resetToken) {
    if (!cid.length()) return false;
    for (unsigned i = 0; i < m_count; ++i) {
      if (!(m_slots[i].cid == cid)) continue;
      if (m_slots[i].state == CIDState::Tombstone) return false;
      m_slots[i].sequence = sequence;
      m_slots[i].routeToken = token;
      m_slots[i].resetToken = resetToken;
      m_slots[i].state = CIDState::Active;
      return true;
    }
    if (m_count >= Max) return false;
    m_slots[m_count++] =
      CIDSlot{cid, sequence, token, resetToken, CIDState::Active};
    return true;
  }

  uintptr_t find(const ConnectionID &cid) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_slots[i].state == CIDState::Active && m_slots[i].cid == cid)
	return m_slots[i].routeToken;
    return 0;
  }
  bool resetToken(const ConnectionID &cid, StatelessResetToken &token) const {
    for (unsigned i = 0; i < m_count; ++i) {
      if (m_slots[i].state != CIDState::Active || !(m_slots[i].cid == cid))
	continue;
      if (!m_slots[i].resetToken.valid()) return false;
      token = m_slots[i].resetToken;
      return true;
    }
    return false;
  }

  bool retire(const ConnectionID &cid) {
    for (unsigned i = 0; i < m_count; ++i) {
      if (!(m_slots[i].cid == cid) || m_slots[i].state != CIDState::Active)
	continue;
      m_slots[i].state = CIDState::Retired;
      return true;
    }
    return false;
  }

  bool tombstone(const ConnectionID &cid) {
    for (unsigned i = 0; i < m_count; ++i) {
      if (!(m_slots[i].cid == cid)) continue;
      m_slots[i].state = CIDState::Tombstone;
      m_slots[i].routeToken = 0;
      m_slots[i].resetToken = {};
      return true;
    }
    if (m_count >= Max || !cid.length()) return false;
    m_slots[m_count++] = CIDSlot{cid, 0, 0, {}, CIDState::Tombstone};
    return true;
  }

  CIDState::T state(const ConnectionID &cid) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_slots[i].cid == cid) return m_slots[i].state;
    return CIDState::Tombstone;
  }

  unsigned count() const { return m_count; }
  unsigned active() const {
    unsigned n = 0;
    for (unsigned i = 0; i < m_count; ++i)
      if (m_slots[i].state == CIDState::Active) ++n;
    return n;
  }

private:
  CIDSlot	m_slots[Max];
  unsigned	m_count = 0;
};

struct CIDGenerator {
  static constexpr unsigned InitialLength = 8;

  static bool random(ConnectionID &, unsigned length = InitialLength);
  static bool randomPair(
    ConnectionID &initialDCID, ConnectionID &initialSCID,
    unsigned dcidLength = InitialLength, unsigned scidLength = InitialLength);
};

class ClientBootstrap {
public:
  bool started() const { return m_started; }
  bool retried() const { return m_retried; }
  uint64_t retryTokenLength() const { return m_retryTokenLength; }
  const ConnectionID &initialDCID() const { return m_initialDCID; }
  const ConnectionID &initialSCID() const { return m_initialSCID; }
  const ConnectionID &retrySCID() const { return m_retrySCID; }

  bool start(const ConnectionID &initialDCID, const ConnectionID &initialSCID) {
    if (initialDCID.length() < 8 || initialSCID.length() < 8) return false;
    m_initialDCID = initialDCID;
    m_initialSCID = initialSCID;
    m_retrySCID = {};
    m_retryTokenLength = 0;
    m_started = true;
    m_retried = false;
    return true;
  }
  bool startRandom(
    unsigned dcidLength = CIDGenerator::InitialLength,
    unsigned scidLength = CIDGenerator::InitialLength);

  bool onRetry(ZuCSpan packet) {
    RetryPacket retry;
    if (!m_started ||
	Packet::parseRetry(packet, retry) < 0 ||
	!Packet::validateRetryIntegrity(packet, m_initialDCID))
      return false;
    return onRetry(retry);
  }

  bool onRetry(const RetryPacket &retry) {
    if (!m_started ||
	retry.header.type != PacketType::Retry ||
	retry.header.scid.length() < 8 ||
	!retry.token ||
	retry.integrityTag.length() != 16)
      return false;
    m_retrySCID = retry.header.scid;
    m_retryTokenLength = retry.token.length();
    m_retried = true;
    return true;
  }

  bool validateServerTransportParams(
    const TransportParams &params,
    const ConnectionID &serverInitialSCID) const {
    if (!m_started ||
	!(params.originalDCID == m_initialDCID) ||
	!(params.initialSCID == serverInitialSCID))
      return false;
    if (m_retried)
      return params.retrySCID == m_retrySCID;
    return !params.retrySCID.length();
  }

private:
  ConnectionID	m_initialDCID;
  ConnectionID	m_initialSCID;
  ConnectionID	m_retrySCID;
  uint64_t	m_retryTokenLength = 0;
  bool		m_started = false;
  bool		m_retried = false;
};

class ServerBootstrap {
public:
  bool accepted() const { return m_accepted; }
  const ConnectionID &originalDCID() const { return m_originalDCID; }
  const ConnectionID &clientInitialSCID() const { return m_clientInitialSCID; }
  const ConnectionID &localInitialSCID() const { return m_localInitialSCID; }
  const StatelessResetToken &statelessResetToken() const {
    return m_statelessResetToken;
  }
  const CIDRouter &localCIDs() const { return m_localCIDs; }
  const CIDRouter &initialDCIDs() const { return m_initialDCIDs; }

  bool acceptInitial(
    const LongHeader &, unsigned datagramLength, uintptr_t routeToken);
  bool transportParams(TransportParams &) const;

private:
  ConnectionID		m_originalDCID;
  ConnectionID		m_clientInitialSCID;
  ConnectionID		m_localInitialSCID;
  StatelessResetToken	m_statelessResetToken;
  CIDRouter		m_localCIDs;
  CIDRouter		m_initialDCIDs;
  bool			m_accepted = false;
};

class ConnState {
public:
  LinkState::T state() const { return m_state; }
  CloseState::T closeState() const { return m_closeState; }
  uint64_t closeError() const { return m_closeError; }
  unsigned drainPTOs() const { return m_drainPTOs; }
  bool closed() const { return m_state == LinkState::Closed; }

  bool startHandshake() {
    if (m_state != LinkState::Starting) return false;
    m_state = LinkState::Handshaking;
    return true;
  }
  bool establish() {
    if (m_state != LinkState::Handshaking) return false;
    m_state = LinkState::Established;
    return true;
  }
  bool close(uint64_t error = 0) {
    if (m_state == LinkState::Closed) return false;
    m_closeError = error;
    m_closeState = CloseState::Closing;
    m_state = LinkState::Closing;
    m_drainPTOs = 0;
    return true;
  }
  bool peerClose(uint64_t error = 0) {
    if (m_state == LinkState::Closed) return false;
    m_closeError = error;
    m_closeState = CloseState::Draining;
    m_state = LinkState::Draining;
    m_drainPTOs = 0;
    return true;
  }
  bool drain() {
    if (m_state != LinkState::Closing) return false;
    m_closeState = CloseState::Draining;
    m_state = LinkState::Draining;
    m_drainPTOs = 0;
    return true;
  }
  bool onPTO() {
    if (m_state != LinkState::Draining) return false;
    if (++m_drainPTOs >= 3) {
      m_state = LinkState::Closed;
      m_closeState = CloseState::Closed;
    }
    return true;
  }
  void abort(uint64_t error) {
    m_closeError = error;
    m_state = LinkState::Closed;
    m_closeState = CloseState::Closed;
    m_drainPTOs = 3;
  }
  bool idleTimeout(uint64_t error = 0) {
    if (m_state == LinkState::Closed) return false;
    m_closeError = error;
    m_state = LinkState::Closed;
    m_closeState = CloseState::Closed;
    m_drainPTOs = 3;
    return true;
  }
  bool drop(uint64_t error = 0) {
    if (m_state == LinkState::Closed) return false;
    m_closeError = error;
    m_state = LinkState::Closed;
    m_closeState = CloseState::Closed;
    m_drainPTOs = 3;
    return true;
  }

private:
  LinkState::T	m_state = LinkState::Starting;
  CloseState::T m_closeState = CloseState::Open;
  uint64_t	m_closeError = 0;
  unsigned	m_drainPTOs = 0;
};

} // namespace Zquic

#endif /* ZquicConn_HH */
