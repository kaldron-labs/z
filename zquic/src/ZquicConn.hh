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
    uint8_t *out, unsigned len, const CxnID &dcid,
    const CxnID &scid) {
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
  unsigned		responseLength = 0;
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

struct CxnIDSlot {
  CxnID			cid;
  uint64_t		sequence = 0;
  uintptr_t		routeToken = 0;
  StatelessResetToken	resetToken;
  CxnIDState::T		state = CxnIDState::Active;
};

class CxnIDRouter {
public:
  static constexpr unsigned Max = 16;

  bool add(const CxnID &cid, uint64_t sequence, uintptr_t token) {
    return add(cid, sequence, token, {});
  }
  bool add(
    const CxnID &cid, uint64_t sequence, uintptr_t token,
    const StatelessResetToken &resetToken) {
    if (!cid.length()) return false;
    for (unsigned i = 0; i < m_count; ++i) {
      if (!(m_slots[i].cid == cid)) continue;
      if (m_slots[i].state == CxnIDState::Tombstone) return false;
      m_slots[i].sequence = sequence;
      m_slots[i].routeToken = token;
      m_slots[i].resetToken = resetToken;
      m_slots[i].state = CxnIDState::Active;
      return true;
    }
    if (m_count >= Max) return false;
    m_slots[m_count++] =
      CxnIDSlot{cid, sequence, token, resetToken, CxnIDState::Active};
    return true;
  }
  bool add(const CxnIDRouter &router) {
    bool ok = true;
    router.all([this, &ok](const CxnIDSlot &slot) {
      if (!add(slot.cid, slot.sequence, slot.routeToken, slot.resetToken))
	ok = false;
    });
    return ok;
  }

  uintptr_t find(const CxnID &cid) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_slots[i].state == CxnIDState::Active && m_slots[i].cid == cid)
	  return m_slots[i].routeToken;
    return 0;
  }
  bool resetToken(const CxnID &cid, StatelessResetToken &token) const {
    for (unsigned i = 0; i < m_count; ++i) {
      if (m_slots[i].state != CxnIDState::Active || !(m_slots[i].cid == cid))
	continue;
      if (!m_slots[i].resetToken.valid()) return false;
      token = m_slots[i].resetToken;
      return true;
    }
    return false;
  }

  bool retire(const CxnID &cid) {
    for (unsigned i = 0; i < m_count; ++i) {
      if (!(m_slots[i].cid == cid) || m_slots[i].state != CxnIDState::Active)
	continue;
      m_slots[i].state = CxnIDState::Retired;
      m_slots[i].routeToken = 0;
      return true;
    }
    return false;
  }

  bool tombstone(const CxnID &cid) {
    for (unsigned i = 0; i < m_count; ++i) {
      if (!(m_slots[i].cid == cid)) continue;
      m_slots[i].state = CxnIDState::Tombstone;
      m_slots[i].routeToken = 0;
      m_slots[i].resetToken = {};
      return true;
    }
    if (m_count >= Max || !cid.length()) return false;
    m_slots[m_count++] = CxnIDSlot{cid, 0, 0, {}, CxnIDState::Tombstone};
    return true;
  }

  CxnIDState::T state(const CxnID &cid) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_slots[i].cid == cid) return m_slots[i].state;
    return CxnIDState::Tombstone;
  }

  unsigned count() const { return m_count; }
  unsigned active() const {
    unsigned n = 0;
    for (unsigned i = 0; i < m_count; ++i)
      if (m_slots[i].state == CxnIDState::Active) ++n;
    return n;
  }
  template <typename Fn>
  void all(Fn fn) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_slots[i].state == CxnIDState::Active) fn(m_slots[i]);
  }

private:
  CxnIDSlot	m_slots[Max];
  unsigned	m_count = 0;
};

struct CxnIDGen {
  static constexpr unsigned InitialLength = MinCIDLength;

  static bool random(CxnID &, unsigned length = InitialLength);
  static bool randomPair(
    CxnID &initialDCID, CxnID &initialSCID,
    unsigned dcidLength = InitialLength, unsigned scidLength = InitialLength);
};

class ClientBootstrap {
public:
  bool started() const { return m_started; }
  bool retried() const { return m_retried; }
  uint64_t retryTokenLength() const { return m_retryTokenLength; }
  const CxnID &initialDCID() const { return m_initialDCID; }
  const CxnID &initialSCID() const { return m_initialSCID; }
  const CxnID &retrySCID() const { return m_retrySCID; }

  bool start(const CxnID &initialDCID, const CxnID &initialSCID) {
    if (initialDCID.length() < MinCIDLength ||
	initialSCID.length() < MinCIDLength)
      return false;
    m_initialDCID = initialDCID;
    m_initialSCID = initialSCID;
    m_retrySCID = {};
    m_retryTokenLength = 0;
    m_started = true;
    m_retried = false;
    return true;
  }
  bool startRandom(
    unsigned dcidLength = CxnIDGen::InitialLength,
    unsigned scidLength = CxnIDGen::InitialLength);

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
	retry.header.scid.length() < MinCIDLength ||
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
    const CxnID &serverInitialSCID) const {
    if (!m_started ||
	!(params.originalDCID == m_initialDCID) ||
	!(params.initialSCID == serverInitialSCID))
      return false;
    if (m_retried)
      return params.retrySCID == m_retrySCID;
    return !params.retrySCID.length();
  }

private:
  CxnID		m_initialDCID;
  CxnID		m_initialSCID;
  CxnID		m_retrySCID;
  uint64_t	m_retryTokenLength = 0;
  bool		m_started = false;
  bool		m_retried = false;
};

class ServerBootstrap {
public:
  bool accepted() const { return m_accepted; }
  const CxnID &originalDCID() const { return m_originalDCID; }
  const CxnID &clientInitialSCID() const { return m_clientInitialSCID; }
  const CxnID &localInitialSCID() const { return m_localInitialSCID; }
  const StatelessResetToken &statelessResetToken() const {
    return m_statelessResetToken;
  }
  const CxnIDRouter &localCIDs() const { return m_localCIDs; }
  const CxnIDRouter &initialDCIDs() const { return m_initialDCIDs; }

  bool acceptInitial(
    const LongHeader &, unsigned datagramLength, uintptr_t routeToken);
  bool transportParams(TransportParams &) const;

private:
  CxnID			m_originalDCID;
  CxnID			m_clientInitialSCID;
  CxnID			m_localInitialSCID;
  StatelessResetToken	m_statelessResetToken;
  CxnIDRouter		m_localCIDs;
  CxnIDRouter		m_initialDCIDs;
  bool			m_accepted = false;
};

} // namespace Zquic

#endif /* ZquicConn_HH */
