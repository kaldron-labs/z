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

#include <zlib/ZuObject.hh>
#include <zlib/ZmHash.hh>

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

struct StatelessReset {
  static constexpr unsigned TokenLength = StatelessResetToken::Length;
  static constexpr unsigned MinLength = 21;

  static int decode(StatelessResetToken &, ZuCSpan datagram);
  static bool verify(ZuCSpan datagram, const StatelessResetToken &);
  static int writeForUnknownCID(
    uint8_t *, unsigned, ZuCSpan receivedPacket,
    const StatelessResetToken &);
};

template <typename Link_>
struct Cxn : public ZuObject {
  Cxn() = default;
  Cxn(
    const CxnID &id_, uint64_t sequence_, Link_ *link_,
    const StatelessResetToken &resetToken_, CxnState::T state_) :
      id{id_},
      sequence{sequence_},
      link{link_},
      resetToken{resetToken_},
      state{state_} { }

  CxnID			id;
  uint64_t		sequence = 0;
  Link_			*link = nullptr;
  StatelessResetToken	resetToken;
  CxnState::T		state = CxnState::Active;
};

template <typename Link_>
inline const CxnID &Cxn_IDAxor(const Cxn<Link_> *cxn) { return cxn->id; }

template <typename Link_>
ZuDerive(CxnRoutes_,
  (ZmHash<ZmRef<Cxn<Link_>>,
    ZmHashKey<Cxn_IDAxor<Link_>,
      ZmHashLock<ZmPLock,
	ZmHashHeapID<"Zquic.Endpoint.CxnRouter">>>>));

template <typename Link_>
class CxnRouter {
public:
  using Route = Cxn<Link_>;
  using Routes = CxnRoutes_<Link_>;

  CxnRouter() : m_routes{new Routes{
      ZmHashParams().bits(5).loadFactor(1).cBits(3)}} { }

  bool add(const CxnID &id, uint64_t sequence, Link_ *link) {
    return add(id, sequence, link, {});
  }
  bool add(
    const CxnID &id, uint64_t sequence, Link_ *link,
    const StatelessResetToken &resetToken) {
    if (!id.length() || !link) return false;
    if (auto route = m_routes->findVal(id)) {
      if (route->state == CxnState::Tombstone) return false;
      route->sequence = sequence;
      route->link = link;
      route->resetToken = resetToken;
      route->state = CxnState::Active;
      return true;
    }
    m_routes->add(new Route{id, sequence, link, resetToken, CxnState::Active});
    return true;
  }

  Link_ *find(const CxnID &id) const {
    auto route = m_routes->findVal(id);
    if (!route || route->state != CxnState::Active) return nullptr;
    return route->link;
  }
  Link_ *matchShort(ZuCSpan packet, CxnID *id = nullptr) const {
    const Route *route = matchShortRoute_(packet, true);
    if (!route) return nullptr;
    if (id) *id = route->id;
    return route->link;
  }
  bool resetToken(const CxnID &id, StatelessResetToken &token) const {
    auto route = m_routes->findVal(id);
    if (!route || route->state == CxnState::Tombstone ||
	!route->resetToken.valid())
      return false;
    token = route->resetToken;
    return true;
  }
  bool resetTokenForShort(ZuCSpan packet, StatelessResetToken &token) const {
    const Route *route = matchShortRoute_(packet, false);
    if (!route || !route->resetToken.valid()) return false;
    token = route->resetToken;
    return true;
  }

  bool retire(const CxnID &id) {
    auto route = m_routes->findVal(id);
    if (!route || route->state != CxnState::Active) return false;
    route->state = CxnState::Retired;
    route->link = nullptr;
    return true;
  }

  bool tombstone(const CxnID &id) {
    if (!id.length()) return false;
    if (auto route = m_routes->findVal(id)) {
      route->state = CxnState::Tombstone;
      route->link = nullptr;
      route->resetToken = {};
      return true;
    }
    m_routes->add(new Route{id, 0, nullptr, {}, CxnState::Tombstone});
    return true;
  }

  CxnState::T state(const CxnID &id) const {
    auto route = m_routes->findVal(id);
    return route ? route->state : CxnState::Tombstone;
  }

  void clear() {
    m_routes = new Routes{ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  }

  unsigned count() const { return m_routes->count_(); }
  unsigned active() const {
    unsigned n = 0;
    all([&n](const Route &) { ++n; });
    return n;
  }
  template <typename Fn>
  void all(Fn fn) const {
    auto i = m_routes->citer();
    while (auto node = i()) {
      const auto &route = *node->val();
      if (route.state == CxnState::Active) fn(route);
    }
  }

private:
  const Route *matchShortRoute_(ZuCSpan packet, bool activeOnly) const {
    if (!packet || packet.length() < 2 || Packet::isLong(packet)) return nullptr;
    const Route *best = nullptr;
    auto i = m_routes->citer();
    while (auto node = i()) {
      const auto &route = *node->val();
      if (!route.id.length()) continue;
      if (route.state == CxnState::Tombstone) continue;
      if (activeOnly && route.state != CxnState::Active) continue;
      if (packet.length() < 1 + route.id.length()) continue;
      if (memcmp(packet.data() + 1, route.id.data(), route.id.length()))
	continue;
      if (!best || route.id.length() > best->id.length())
	best = &route;
    }
    return best;
  }

  ZmRef<Routes>	m_routes;
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

  bool acceptInitial(const LongHeader &, unsigned datagramLength);
  bool transportParams(TransportParams &) const;

private:
  CxnID			m_originalDCID;
  CxnID			m_clientInitialSCID;
  CxnID			m_localInitialSCID;
  StatelessResetToken	m_statelessResetToken;
  bool			m_accepted = false;
};

} // namespace Zquic

#endif /* ZquicConn_HH */
