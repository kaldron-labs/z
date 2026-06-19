//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC transport API

#ifndef Zquic_HH
#define Zquic_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#endif

#include <zpicotls.h>

#include <zlib/ZuObject.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmAlloc.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmQueue.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtLocalArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#if defined(ZDEBUG) && !defined(Zquic_DEBUG)
#define Zquic_DEBUG	// enable testing / debugging
#endif

#ifdef Zquic_DEBUG
#define Zquic_DEBUG_LOG_(e) do { if (debugLog_()) ZiLOG(Debug, "Zquic", (e)); } while (0)
#else
#define Zquic_DEBUG_LOG_(e) (void())
#endif

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicStream.hh>
#include <zlib/ZquicSched.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicTransport.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZquicEndpoint.hh>
#include <zlib/ZquicRecovery.hh>
#include <zlib/ZquicSock.hh>

namespace Zquic {

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Zquic.Log">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Zquic.ParamStrings">>));

struct InitialInfo {
  LongHdr	header;
  ZiSockAddr	peer;
  unsigned	datagramLength = 0;
};

ZuDerive(ALPNData, (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.ALPNData">>));
ZuDerive(ALPN, (ZtArray<ptls_iovec_t, ZtArrayHeapID<"Zquic.ALPN">>));
ZuDerive(AsyncSendPayload,
  (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.AsyncSendPayload">>));

using ErrorFn = ZmFn<void(ZeException)>;

inline constexpr uint64_t DefaultMaxData = 16U * 1024U * 1024U;
inline constexpr uint64_t DefaultMaxStreamData = 1U * 1024U * 1024U;
inline constexpr uint64_t DefaultMaxStreamsBidi = 128;
inline constexpr uint64_t DefaultMaxStreamsUni = 16;
inline constexpr uint64_t MaxStreamCount = uint64_t(INT64_MAX) >> 2;

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

inline bool padForProtSample(
  unsigned pnOffset, unsigned pnLength, unsigned tagLen,
  uint8_t *payload, unsigned len, unsigned &payloadLen)
{
  unsigned headerLen = pnOffset + pnLength;
  unsigned minPktLen = pnOffset + 4 + 16;
  unsigned minPayloadLen = minPktLen > headerLen + tagLen ?
    minPktLen - headerLen - tagLen : 0;
  if (payloadLen >= minPayloadLen) return true;
  if (minPayloadLen > len) return false;
  memset(payload + payloadLen, 0, minPayloadLen - payloadLen);
  payloadLen = minPayloadLen;
  return true;
}

inline PktSpace::T runtimePktSpace(CryptoLevel::T level)
{
  if (level == CryptoLevel::Initial) return PktSpace::Initial;
  if (level == CryptoLevel::Handshake) return PktSpace::Handshake;
  return PktSpace::AppData;
}

inline ZuCSpan byteSpan(const uint8_t *data, unsigned len)
{
  return ZuCSpan{data, len};
}

inline bool cryptoLevelFromEpoch(size_t epoch, CryptoLevel::T &level)
{
  if (epoch == 0) {
    level = CryptoLevel::Initial;
    return true;
  }
  if (epoch == 2) {
    level = CryptoLevel::Handshake;
    return true;
  }
  if (epoch >= 3) {
    level = CryptoLevel::OneRTT;
    return true;
  }
  return false;
}

inline bool runtimeFrameRef(
  ZuCSpan bytes, SentFrameRef &ref, bool &ackEliciting)
{
  ref = {};
  ackEliciting = false;
  if (!bytes) return true;
  Frame frame;
  unsigned used = 0;
  if (FrameCodec::parse(bytes, frame, used) < 0 || !used) return false;
  ackEliciting = FrameCodec::ackEliciting(frame.type);
  if (!ackEliciting) return true;
  switch (frame.type) {
  case FrameType::Crypto:
    ref = SentFrameRef::crypto(frame.offset, frame.length);
    return true;
  case FrameType::Stream:
    ref.kind = SentFrameKind::Stream;
    ref.streamID = frame.streamID;
    ref.offset = frame.offset;
    ref.length = frame.length;
    ref.fin = frame.fin;
    ref.range = TxRange{
      nullptr, 0, uint32_t(frame.length), frame.offset};
    return true;
  case FrameType::MaxData:
  case FrameType::MaxStreamData:
  case FrameType::MaxStreams:
    ref = SentFrameRef::flowUpdate(
      FlowUpdate{frame.type, frame.streamID, frame.value, frame.streamType});
    break;
  case FrameType::DataBlocked:
  case FrameType::StreamDataBlocked:
  case FrameType::StreamsBlocked:
    ref = SentFrameRef::blocked(
      frame.type, frame.streamID, frame.value, frame.streamType);
    break;
  case FrameType::PathResponse:
    ref = SentFrameRef::pathResponse(frame.payload);
    break;
  case FrameType::HandshakeDone:
    ref = SentFrameRef::handshakeDone();
    break;
  default:
    ref = SentFrameRef::control();
    break;
  }
  return true;
}

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
    return Pkt::writeVersionNegotiation(out, len, dcid, scid, versions, 1);
  }

  static int parse(
    ZuCSpan in, uint32_t *versions, unsigned capacity, unsigned &nVersions) {
    return Pkt::parseVersionNegotiation(in, versions, capacity, nVersions);
  }
};

struct ServerPktDecision {
  ServerPktAction::T	action = ServerPktAction::Drop;
  LongHdr		header;
  unsigned		responseLength = 0;
};

struct ServerPkt {
  static ServerPktDecision routeLongHdr(
    ZuCSpan, uint8_t *response, unsigned responseLen);
};

struct StatelessReset {
  static constexpr unsigned TokenLength = ResetToken::Length;
  static constexpr unsigned MinLength = 21;

  static int decode(ResetToken &, ZuCSpan datagram);
  static bool verify(ZuCSpan datagram, const ResetToken &);
  static int writeForUnknownCID(
    uint8_t *, unsigned, ZuCSpan receivedPkt, const ResetToken &);
};

template <typename Link_>
struct Cxn : public ZuObject {
  Cxn() = default;
  Cxn(
    const CxnID &id_, uint64_t sequence_, Link_ *link_,
    const ResetToken &resetToken_, CxnState::T state_) :
      id{id_},
      sequence{sequence_},
      link{link_},
      resetToken{resetToken_},
      state{state_} { }

  CxnID			id;
  uint64_t		sequence = 0;
  Link_			*link = nullptr;
  ResetToken		resetToken;
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
  static constexpr unsigned TombstoneMax = 64;
  using Tombstones = ZmQueue<CxnID,
    ZmQueueHeapID<"Zquic.Endpoint.CxnRouter.Tombstones">>;

  CxnRouter() : m_routes{new Routes{
      ZmHashParams().bits(5).loadFactor(1).cBits(3)}} { }

  bool add(const CxnID &id, uint64_t sequence, Link_ *link) {
    return add(id, sequence, link, {});
  }
  bool add(
    const CxnID &id, uint64_t sequence, Link_ *link,
    const ResetToken &resetToken) {
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
  bool resetToken(const CxnID &id, ResetToken &token) const {
    auto route = m_routes->findVal(id);
    if (!route || route->state != CxnState::Active ||
	!route->resetToken.valid())
      return false;
    token = route->resetToken;
    return true;
  }
  bool resetTokenForShort(ZuCSpan packet, ResetToken &token) const {
    const Route *route = matchShortRoute_(packet, false);
    if (!route || !route->resetToken.valid()) return false;
    token = route->resetToken;
    return true;
  }

  bool retire(const CxnID &id) {
    auto route = m_routes->findVal(id);
    if (!route || route->state != CxnState::Active) return false;
    m_routes->del(id);
    return true;
  }

  bool tombstone(const CxnID &id) {
    if (!id.length()) return false;
    if (auto route = m_routes->findVal(id)) {
      if (route->state == CxnState::Tombstone) return true;
      route->state = CxnState::Tombstone;
      route->link = nullptr;
      route->resetToken = {};
      m_tombstones.push(id);
      trimTombstones_();
      return true;
    }
    auto route = new Route{id, 0, nullptr, {}, CxnState::Tombstone};
    m_routes->add(route);
    m_tombstones.push(id);
    trimTombstones_();
    return true;
  }

  CxnState::T state(const CxnID &id) const {
    auto route = m_routes->findVal(id);
    return route ? route->state : CxnState::Tombstone;
  }

  void clear() {
    m_routes = new Routes{ZmHashParams().bits(5).loadFactor(1).cBits(3)};
    m_tombstones.clean();
  }

  unsigned count() const { return m_routes->count_(); }
  unsigned tombstoneCount() const { return m_tombstones.count_(); }
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
  void trimTombstones_() {
    while (m_tombstones.count_() > TombstoneMax) {
      CxnID id = m_tombstones.shift();
      if (id.length()) m_routes->del(id);
    }
  }
  const Route *matchShortRoute_(ZuCSpan packet, bool activeOnly) const {
    if (!packet || packet.length() < 2 || Pkt::isLong(packet)) return nullptr;
    unsigned max = packet.length() - 1;
    if (max > CxnIDMax) max = CxnIDMax;
    while (max) {
      CxnID id{ZuCSpan{packet.data() + 1, max}};
      if (auto route = m_routes->findVal(id)) {
	if (route->state != CxnState::Tombstone &&
	    (!activeOnly || route->state == CxnState::Active))
	  return route;
      }
      --max;
    }
    return nullptr;
  }

  // Rx thread exclusive
  ZmRef<Routes>	m_routes;
  Tombstones	m_tombstones{ZmQueueParams{}.initial(TombstoneMax)};
};

template <typename Link_>
struct ServerLinkEntry : public ZuObject {
  using LinkRef = ZmRef<Link_>;

  ServerLinkEntry() = default;
  ServerLinkEntry(LinkRef ref_) : link{ref_.ptr()}, linkRef{ZuMv(ref_)} { }

  Link_		*link = nullptr;
  LinkRef	linkRef;
};

template <typename Link_>
inline Link_ *ServerLinkEntry_LinkAxor(
  const ServerLinkEntry<Link_> *entry) {
  return entry->link;
}

template <typename Link_>
ZuDerive(ServerLinks_,
  (ZmHash<ZmRef<ServerLinkEntry<Link_>>,
    ZmHashKey<ServerLinkEntry_LinkAxor<Link_>,
      ZmHashLock<ZmPLock,
	ZmHashHeapID<"Zquic.Server.LinkTable">>>>));

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
    RetryPkt retry;
    if (!m_started ||
	Pkt::parseRetry(packet, retry) < 0 ||
	!Pkt::validateRetryIntegrity(packet, m_initialDCID))
      return false;
    return onRetry(retry);
  }

  bool onRetry(const RetryPkt &retry) {
    if (!m_started ||
	retry.header.type != PktType::Retry ||
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
  const ResetToken &statelessResetToken() const {
    return m_statelessResetToken;
  }

  bool acceptInitial(const LongHdr &, unsigned datagramLength);
  bool transportParams(TransportParams &) const;

private:
  CxnID		m_originalDCID;
  CxnID		m_clientInitialSCID;
  CxnID		m_localInitialSCID;
  ResetToken	m_statelessResetToken;
  bool		m_accepted = false;
};

struct RuntimeRxDiag {
  uint64_t	endpointReady = 0;
  uint64_t	datagramsRx = 0;
  uint64_t	bytesRx = 0;
  uint64_t	packetsRx = 0;
  uint64_t	framesRx = 0;
  uint64_t	cryptoBytesRx = 0;
  uint64_t	streamBytesRx = 0;
  uint64_t	invalidStreamFrames = 0;
  uint64_t	closedStreamFrames = 0;
  uint64_t	suspiciousStreamCloses = 0;
  uint64_t	unhandledAppEvents = 0;
  uint64_t	peerKeyUpdates = 0;
  uint64_t	invalidKeyPhases = 0;
  uint64_t	oldKeysAccepted = 0;
  uint64_t	keyDiscards = 0;
  AckECN	ecnRx[3];
  uint64_t	failures = 0;
  uint64_t	handshakeComplete = 0;
};

struct RuntimeTxDiag {
  static constexpr unsigned Spaces = 3;

  uint64_t	packetsTx = 0;
  uint64_t	bytesTx = 0;
  uint64_t	cryptoBytesTx = 0;
  uint64_t	streamBytesTx = 0;
  uint64_t	ptoCount = 0;
  uint32_t	ptoBackoff = 0;
  uint64_t	ptoTimeoutUS = 0;
  uint64_t	retransmittedFrames = 0;
  uint64_t	pktBytesInFlight[Spaces] = {};
  uint32_t	sentPackets[Spaces] = {};
  uint32_t	retransmitPending[Spaces] = {};
  uint32_t	retransmittable[Spaces] = {};
  uint64_t	congestionWindow = 0;
  uint64_t	congestionSSThresh = 0;
  uint64_t	congestionBytesInFlight = 0;
  uint64_t	persistentCongestion = 0;
  bool		ptoTimerActive = false;
  bool		lossTimerActive = false;
  AckECN	peerAckECN[3];
  uint64_t	ecnValidationFailures = 0;
  uint64_t	unhandledAppEvents = 0;
  uint64_t	failures = 0;
};

struct RuntimeDiag {
  RuntimeDiag() = default;
  RuntimeDiag(const RuntimeRxDiag &rx, const RuntimeTxDiag &tx) {
    endpointReady = rx.endpointReady;
    datagramsRx = rx.datagramsRx;
    bytesRx = rx.bytesRx;
    packetsRx = rx.packetsRx;
    framesRx = rx.framesRx;
    cryptoBytesRx = rx.cryptoBytesRx;
    streamBytesRx = rx.streamBytesRx;
    invalidStreamFrames = rx.invalidStreamFrames;
    closedStreamFrames = rx.closedStreamFrames;
    suspiciousStreamCloses = rx.suspiciousStreamCloses;
    unhandledAppEvents = rx.unhandledAppEvents + tx.unhandledAppEvents;
    peerKeyUpdates = rx.peerKeyUpdates;
    invalidKeyPhases = rx.invalidKeyPhases;
    oldKeysAccepted = rx.oldKeysAccepted;
    keyDiscards = rx.keyDiscards;
    for (unsigned i = 0; i < 3; ++i) ecnRx[i] = rx.ecnRx[i];
    handshakeComplete = rx.handshakeComplete;

    packetsTx = tx.packetsTx;
    bytesTx = tx.bytesTx;
    cryptoBytesTx = tx.cryptoBytesTx;
    streamBytesTx = tx.streamBytesTx;
    ptoCount = tx.ptoCount;
    ptoBackoff = tx.ptoBackoff;
    ptoTimeoutUS = tx.ptoTimeoutUS;
    retransmittedFrames = tx.retransmittedFrames;
    for (unsigned i = 0; i < RuntimeTxDiag::Spaces; ++i) {
      pktBytesInFlight[i] = tx.pktBytesInFlight[i];
      sentPackets[i] = tx.sentPackets[i];
      retransmitPending[i] = tx.retransmitPending[i];
      retransmittable[i] = tx.retransmittable[i];
    }
    congestionWindow = tx.congestionWindow;
    congestionSSThresh = tx.congestionSSThresh;
    congestionBytesInFlight = tx.congestionBytesInFlight;
    persistentCongestion = tx.persistentCongestion;
    ptoTimerActive = tx.ptoTimerActive;
    lossTimerActive = tx.lossTimerActive;
    for (unsigned i = 0; i < 3; ++i) peerAckECN[i] = tx.peerAckECN[i];
    ecnValidationFailures = tx.ecnValidationFailures;

    failures = rx.failures + tx.failures;
  }

  uint64_t	endpointReady = 0;
  uint64_t	datagramsRx = 0;
  uint64_t	bytesRx = 0;
  uint64_t	packetsRx = 0;
  uint64_t	framesRx = 0;
  uint64_t	packetsTx = 0;
  uint64_t	bytesTx = 0;
  uint64_t	cryptoBytesRx = 0;
  uint64_t	cryptoBytesTx = 0;
  uint64_t	streamBytesRx = 0;
  uint64_t	streamBytesTx = 0;
  uint64_t	invalidStreamFrames = 0;
  uint64_t	closedStreamFrames = 0;
  uint64_t	suspiciousStreamCloses = 0;
  uint64_t	unhandledAppEvents = 0;
  uint64_t	peerKeyUpdates = 0;
  uint64_t	invalidKeyPhases = 0;
  uint64_t	oldKeysAccepted = 0;
  uint64_t	keyDiscards = 0;
  AckECN	ecnRx[3];
  uint64_t	ptoCount = 0;
  uint32_t	ptoBackoff = 0;
  uint64_t	ptoTimeoutUS = 0;
  uint64_t	retransmittedFrames = 0;
  uint64_t	pktBytesInFlight[RuntimeTxDiag::Spaces] = {};
  uint32_t	sentPackets[RuntimeTxDiag::Spaces] = {};
  uint32_t	retransmitPending[RuntimeTxDiag::Spaces] = {};
  uint32_t	retransmittable[RuntimeTxDiag::Spaces] = {};
  uint64_t	congestionWindow = 0;
  uint64_t	congestionSSThresh = 0;
  uint64_t	congestionBytesInFlight = 0;
  uint64_t	persistentCongestion = 0;
  bool		ptoTimerActive = false;
  bool		lossTimerActive = false;
  AckECN	peerAckECN[3];
  uint64_t	ecnValidationFailures = 0;
  uint64_t	failures = 0;
  uint64_t	handshakeComplete = 0;
};

template <typename Send>
inline bool sendRuntimeCryptoFlights(
  CryptoStream (&txCrypto)[3], RuntimeTxDiag &diag,
  const uint8_t *data, unsigned len, const size_t offsets[5],
  unsigned chunkMax, ZiSockAddr addr, Send send)
{
  if (!chunkMax) return false;
  for (size_t epoch = 0; epoch < 4; ++epoch) {
    if (offsets[epoch + 1] < offsets[epoch] || offsets[epoch + 1] > len) {
      ++diag.failures;
      return false;
    }
    if (offsets[epoch + 1] <= offsets[epoch]) continue;
    CryptoLevel::T level;
    if (!cryptoLevelFromEpoch(epoch, level)) continue;
    unsigned off = unsigned(offsets[epoch]);
    unsigned remaining = unsigned(offsets[epoch + 1] - offsets[epoch]);
    while (remaining) {
      unsigned chunk = remaining > chunkMax ? chunkMax : remaining;
      using Frame = ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Runtime.Frame">>;
      auto frame = ZtLocalArray(Frame, BufSize);
      uint64_t cryptoOffset = txCrypto[level].txOffset();
      int n = txCrypto[level].writeFramePrefix(frame.data(), BufSize, chunk);
      if (n < 0) {
	++diag.failures;
	return false;
      }
      SentFrameRef ref = SentFrameRef::crypto(cryptoOffset, chunk);
      if (!txCrypto[level].sent(cryptoOffset, byteSpan(data + off, chunk))) {
	++diag.failures;
	return false;
      }
      diag.cryptoBytesTx += chunk;
      if (!send(level, byteSpan(frame.data(), unsigned(n)),
	    byteSpan(data + off, chunk), ref, addr))
	return false;
      if (level == CryptoLevel::Handshake &&
	  !send(level, byteSpan(frame.data(), unsigned(n)),
	    byteSpan(data + off, chunk), ref, addr))
	return false;
      off += chunk;
      remaining -= chunk;
    }
  }
  return true;
}

inline void inspectRuntimeDatagram(RuntimeDiag &diag, const Datagram &d)
{
  ++diag.datagramsRx;
  if (!d.buf) {
    ++diag.failures;
    return;
  }
  diag.bytesRx += d.buf->length;

  LongHdr h;
  if (Pkt::parseLong(d.buf->cspan(), h) < 0) {
    ++diag.failures;
    return;
  }
  ++diag.packetsRx;

  if (h.length < h.pnLength || h.length > d.buf->length) {
    ++diag.failures;
    return;
  }
  unsigned packetLength = h.length;
  if (h.pnOffset > d.buf->length - packetLength) {
    ++diag.failures;
    return;
  }
  unsigned payloadLength = packetLength - h.pnLength;
  unsigned payloadOffset = h.payloadOffset;
  if (payloadOffset > d.buf->length ||
      payloadLength > d.buf->length - payloadOffset) {
    ++diag.failures;
    return;
  }

  ZuCSpan payload{d.buf->data() + payloadOffset, payloadLength};
  unsigned offset = 0;
  auto frame_ = ZmAlloc(Frame, 1);
  new (&frame_[0]) Frame{};
  auto &frame = frame_[0];
  ZuGuard frameGuard{[&frame]() { frame.~Frame(); }};
  while (offset < payloadLength) {
    unsigned used = 0;
    if (FrameCodec::parse(
	  ZuCSpan{payload.data() + offset, payloadLength - offset},
	  frame, used) < 0 || !used) {
      ++diag.failures;
      return;
    }
    ++diag.framesRx;
    offset += used;
  }
}

inline bool writeInitialPingProbe(ZiIOBuf *buf, uint64_t packetNumber)
{
  if (!buf) return false;
  if (buf->size < MinUDPPayload && !buf->ensure(MinUDPPayload)) return false;

  CxnID dcid{"zqserv01"};
  CxnID scid{"zqcli001"};
  static constexpr unsigned PNLength = 1;
  uint8_t *out = buf->data_();
  unsigned payloadLength = 1;
  int headerLength = -1;

  for (unsigned i = 0; i < 4; ++i) {
    headerLength = Pkt::writeInitial(
      out, buf->size, dcid, scid, payloadLength, PNLength);
    if (headerLength < 0 ||
	MinUDPPayload < unsigned(headerLength) + PNLength + 1)
      return false;
    unsigned nextPayloadLength =
      MinUDPPayload - unsigned(headerLength) - PNLength;
    if (nextPayloadLength == payloadLength) break;
    payloadLength = nextPayloadLength;
  }
  if (headerLength < 0) return false;

  int pnLength = PktNumber::encode(
    out + headerLength, buf->size - unsigned(headerLength),
    packetNumber, PNLength);
  if (pnLength != int(PNLength)) return false;
  int pingLength = FrameCodec::writePing(
    out + headerLength + PNLength,
    buf->size - unsigned(headerLength) - PNLength);
  if (pingLength != 1) return false;

  unsigned offset = unsigned(headerLength) + PNLength + unsigned(pingLength);
  if (offset > MinUDPPayload) return false;
  memset(out + offset, 0, MinUDPPayload - offset);
  buf->skip = 0;
  buf->length = MinUDPPayload;
  return true;
}

struct EngineParams {
  EngineParams(
    ZiMultiplex *mx = nullptr,
    ZuCSpan rxThread = {},
    ZuCSpan txThread = {}) :
      m_mx{mx}, m_rxThread{rxThread}, m_txThread{txThread},
      m_errorFn{defaultErrorFn()} { }

  EngineParams &&caPath(ZuCSpan v) { m_caPath = v; return ZuMv(*this); }
  EngineParams &&certPath(ZuCSpan v) { m_certPath = v; return ZuMv(*this); }
  EngineParams &&keyPath(ZuCSpan v) { m_keyPath = v; return ZuMv(*this); }
  EngineParams &&keyLogPath(ZuCSpan v) {
    m_keyLogPath = v;
    return ZuMv(*this);
  }
  EngineParams &&asyncThread(ZuCSpan v) {
    m_asyncThread = v;
    return ZuMv(*this);
  }
  EngineParams &&maxData(uint64_t v) { m_maxData = v; return ZuMv(*this); }
  EngineParams &&maxStreamData(uint64_t v) {
    m_maxStreamData = v;
    return ZuMv(*this);
  }
  EngineParams &&maxStreamsBidi(uint64_t v) {
    m_maxStreamsBidi = v;
    return ZuMv(*this);
  }
  EngineParams &&maxStreamsUni(uint64_t v) {
    m_maxStreamsUni = v;
    return ZuMv(*this);
  }
  EngineParams &&maxUDP(unsigned v) { m_maxUDP = v; return ZuMv(*this); }
  EngineParams &&alpn(ZuSpan<ZuCSpan> v) {
    m_alpn.length(0);
    m_alpn.ensure(v.length());
    for (auto &s : v) m_alpn.push(ParamString{s});
    return ZuMv(*this);
  }
  EngineParams &&alpn(ZuSpan<const ptls_iovec_t> v) {
    m_alpn.length(0);
    m_alpn.ensure(v.length());
    for (auto &p : v)
      m_alpn.push(ParamString{ZuCSpan{p.base, unsigned(p.len)}});
    return ZuMv(*this);
  }
  EngineParams &&errorFn(ErrorFn v) { m_errorFn = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex *mx() const { return m_mx; }
  ZuCSpan rxThread() const { return m_rxThread; }
  ZuCSpan txThread() const { return m_txThread; }
  const ParamStrings &alpn() const { return m_alpn; }
  ZuCSpan caPath() const { return m_caPath; }
  ZuCSpan certPath() const { return m_certPath; }
  ZuCSpan keyPath() const { return m_keyPath; }
  ZuCSpan keyLogPath() const { return m_keyLogPath; }
  ZuCSpan asyncThread() const { return m_asyncThread; }
  uint64_t maxData() const { return m_maxData; }
  uint64_t maxStreamData() const { return m_maxStreamData; }
  uint64_t maxStreamsBidi() const { return m_maxStreamsBidi; }
  uint64_t maxStreamsUni() const { return m_maxStreamsUni; }
  unsigned maxUDP() const { return m_maxUDP; }
  const ErrorFn &errorFn() const { return m_errorFn; }
  ErrorFn &errorFn() { return m_errorFn; }

private:
  ZiMultiplex	*m_mx = nullptr;
  ParamString	m_rxThread;
  ParamString	m_txThread;
  ParamStrings	m_alpn;
  ParamString	m_caPath;
  ParamString	m_certPath;
  ParamString	m_keyPath;
  ParamString	m_keyLogPath;
  ParamString	m_asyncThread;
  uint64_t	m_maxData = DefaultMaxData;
  uint64_t	m_maxStreamData = DefaultMaxStreamData;
  uint64_t	m_maxStreamsBidi = DefaultMaxStreamsBidi;
  uint64_t	m_maxStreamsUni = DefaultMaxStreamsUni;
  unsigned	m_maxUDP = MinUDPPayload;
  ErrorFn	m_errorFn;
};

using ClientParams = EngineParams;
using ServerParams = EngineParams;

template <typename App_> class Engine : public ZmPolymorph {
template <typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename>
friend class CliLink;
template <typename, typename, typename, typename>
friend class SrvLink;

public:
  using App = App_;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  enum { Transport = Zi::Transport::QUIC };

  bool init(EngineParams params) {
    return init_(ZuMv(params), [](const EngineParams &) { return true; });
  }

  void final() {
    if (m_mx && m_rxThread && m_txThread) {
      ZiAssert(!rxInvoked() && !txInvoked(), "Zquic", (),
	"QUIC engine finalization from I/O thread", return);
      ZmSemaphore stopped;
      rxInvoke([this, &stopped]() { stop_1(&stopped); });
      stopped.wait();
      return;
    }
    clear_();
  }

  void clear_() {
    m_mx = nullptr;
    m_rxThread = m_txThread = m_asyncThread = 0;
    m_errorFn = ErrorFn{};
    m_alpn.length(0);
    m_alpnData.length(0);
    m_caPath = m_certPath = m_keyPath = ParamString{};
    m_keyLogPath = ParamString{};
    m_maxData = DefaultMaxData;
    m_maxStreamData = DefaultMaxStreamData;
    m_maxStreamsBidi = DefaultMaxStreamsBidi;
    m_maxStreamsUni = DefaultMaxStreamsUni;
    m_maxUDP = MinUDPPayload;
  }

  ZiMultiplex *mx() const { return m_mx; }
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }
  unsigned asyncThread() const { return m_asyncThread; }

  const ptls_iovec_t *alpn_list() const { return m_alpn.data(); }
  unsigned alpn_count() const { return m_alpn.length(); }
  ZuCSpan firstALPN() const {
    return m_alpn.length() ?
      ZuCSpan{
	m_alpn[0].base, unsigned(m_alpn[0].len)} : ZuCSpan{};
  }
  ZuCSpan caPath() const { return m_caPath; }
  ZuCSpan certPath() const { return m_certPath; }
  ZuCSpan keyPath() const { return m_keyPath; }
  ZuCSpan keyLogPath() const { return m_keyLogPath; }
  uint64_t maxData() const { return m_maxData; }
  uint64_t maxStreamData() const { return m_maxStreamData; }
  uint64_t maxStreamsBidi() const { return m_maxStreamsBidi; }
  uint64_t maxStreamsUni() const { return m_maxStreamsUni; }
  unsigned maxUDP() const { return m_maxUDP; }

  template <typename ...Args>
  void rxRun(Args &&...args) {
    m_mx->run(m_rxThread, ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  void rxInvoke(Args &&...args) {
    m_mx->invoke(m_rxThread, ZuFwd<Args>(args)...);
  }
  bool rxInvoked() { return m_mx && m_rxThread && m_mx->invoked(m_rxThread); }
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(m_txThread, ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(m_txThread, ZuFwd<Args>(args)...);
  }
  bool txInvoked() { return m_mx && m_txThread && m_mx->invoked(m_txThread); }

protected:
  void stop_1(ZmSemaphore *stopped) {
    txInvoke([this, stopped]() {
      stop_2(stopped);
    });
  }

  void stop_2(ZmSemaphore *stopped) {
    rxInvoke([this, stopped]() {
      stop_3(stopped);
    });
  }

  void stop_3(ZmSemaphore *stopped) {
    txInvoke([this, stopped]() {
      stop_4(stopped);
    });
  }

  void stop_4(ZmSemaphore *stopped) {
    clear_();
    stopped->post();
  }

  template <typename Params, typename L>
  bool init_(Params params, L l) {
    m_errorFn = ZuMv(params.errorFn());
    if (!m_errorFn) m_errorFn = defaultErrorFn();
    if (!validate_(params)) return false;
    m_mx = params.mx();
    m_rxThread = thread_(params.rxThread(), m_mx->rxThread());
    m_txThread = thread_(params.txThread(), m_mx->txThread());
    m_asyncThread = params.asyncThread() ?
      m_mx->sid(params.asyncThread()) : 0;
    m_caPath = params.caPath();
    m_certPath = params.certPath();
    m_keyPath = params.keyPath();
    m_keyLogPath = params.keyLogPath();
    m_maxData = params.maxData();
    m_maxStreamData = params.maxStreamData();
    m_maxStreamsBidi = params.maxStreamsBidi();
    m_maxStreamsUni = params.maxStreamsUni();
    m_maxUDP = params.maxUDP();
    if (!init_alpn_(params.alpn())) return false;
    warmup_();
    return l(params);
  }

  void error_(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

private:
  void warmup_() {
    rxInvoke([]() { Zquic::warmup(); });
    txInvoke([]() { Zquic::warmup(); });
  }

  unsigned thread_(const ParamString &id, unsigned deflt) const {
    return id ? m_mx->sid(id) : deflt;
  }

  template <typename Params>
  bool validate_(const Params &params) {
    if (ZuUnlikely(!params.mx())) {
      error_(ZeEXCEPT(Error, "Zquic", "multiplexer is null"));
      return false;
    }
    unsigned rxThread = params.rxThread() ?
      params.mx()->sid(params.rxThread()) : params.mx()->rxThread();
    unsigned txThread = params.txThread() ?
      params.mx()->sid(params.txThread()) : params.mx()->txThread();
    if (!rxThread || rxThread > params.mx()->params().nThreads()) {
      error_(ZeEXCEPT(Error, "Zquic",
	([thread = LogMsg{params.rxThread()}](auto &s) {
	s << "invalid QUIC Rx thread ID \"" << thread << '"';
      })));
      return false;
    }
    if (!txThread || txThread > params.mx()->params().nThreads()) {
      error_(ZeEXCEPT(Error, "Zquic",
	([thread = LogMsg{params.txThread()}](auto &s) {
	s << "invalid QUIC Tx thread ID \"" << thread << '"';
      })));
      return false;
    }
    if (rxThread == txThread) {
      error_(ZeEXCEPT(Error, "Zquic",
	"QUIC Rx and Tx threads must differ"));
      return false;
    }
    if (!params.mx()->running()) {
      error_(ZeEXCEPT(Error, "Zquic", "multiplexer not running"));
      return false;
    }
    if (params.maxUDP() > BufSize) {
      error_(ZeEXCEPT(Error, "Zquic",
	([maxUDP = params.maxUDP()](auto &s) {
	s << "maxUDP " << maxUDP << " exceeds packet buffer size " << BufSize;
      })));
      return false;
    }
    if (params.asyncThread()) {
#ifdef _WIN32
      error_(ZeEXCEPT(Error, "Zquic",
	"asyncThread is unsupported on Windows"));
      return false;
#else
      unsigned asyncThread = params.mx()->sid(params.asyncThread());
      if (!asyncThread || asyncThread > params.mx()->params().nThreads()) {
	error_(ZeEXCEPT(Error, "Zquic",
	  ([thread = LogMsg{params.asyncThread()}](auto &s) {
	  s << "invalid async thread ID \"" << thread << '"';
	})));
	return false;
      }
      if (asyncThread == rxThread || asyncThread == txThread) {
	error_(ZeEXCEPT(Error, "Zquic",
	  "async thread must differ from QUIC Rx and Tx threads"));
	return false;
      }
      if (asyncThread == params.mx()->rxThread() ||
	  asyncThread == params.mx()->txThread()) {
	error_(ZeEXCEPT(Error, "Zquic",
	  "async thread must differ from I/O threads"));
	return false;
      }
      if (!params.mx()->params().thread(asyncThread).isolated()) {
	error_(ZeEXCEPT(Error, "Zquic",
	  "async thread must be isolated"));
	return false;
      }
#endif
    }
    return true;
  }

  bool init_alpn_(const ParamStrings &alpn) {
    m_alpn.length(0);
    m_alpnData.length(0);
    if (!alpn.length()) return true;
    unsigned bytes = 0;
    for (auto &s : alpn) bytes += s.length();
    m_alpnData.length(bytes);
    m_alpn.ensure(alpn.length());
    unsigned offset = 0;
    for (auto &s : alpn) {
      memcpy(m_alpnData.data() + offset, s.data(), s.length());
      m_alpn.push(ptls_iovec_t{m_alpnData.data() + offset, s.length()});
      offset += s.length();
    }
    return true;
  }

  // immutable after init()
  ZiMultiplex		*m_mx = nullptr;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;
  unsigned		m_asyncThread = 0;
  ErrorFn		m_errorFn;
  ALPNData		m_alpnData;
  ALPN			m_alpn;
  ParamString		m_caPath;
  ParamString		m_certPath;
  ParamString		m_keyPath;
  ParamString		m_keyLogPath;
  uint64_t		m_maxData = DefaultMaxData;
  uint64_t		m_maxStreamData = DefaultMaxStreamData;
  uint64_t		m_maxStreamsBidi = DefaultMaxStreamsBidi;
  uint64_t		m_maxStreamsUni = DefaultMaxStreamsUni;
  unsigned		m_maxUDP = MinUDPPayload;
};

// CRTP - aligned client implementation should conform to this interface:
#if 0
struct App : public Zquic::Client<App> {
  struct Link;
  struct Stream;
};

struct App::Stream : public Zquic::CliStream<Link, Stream> {
  // Zquic Rx thread.
  // >0 consumed progress, 0 leave queued data, <0 reset/close per policy.
  int process(Zquic::RxStream &);
};

struct App::Link : public Zquic::CliLink<App, Link, App::Stream> {
  Link(App *, Zquic::Host server, uint16_t port);

  void connected(Zi::Connected); // Zquic Rx thread
  void disconnected(); // Zquic Rx thread
  void connectFailed(bool transient); // Zquic Rx thread
  void streamed(ZmRef<Stream>); // Zquic Rx thread

  unsigned reconnFreq() const; // optional
};
#endif
template <typename App_> class Client : public Engine<App_> {
public:
  using App = App_;
  using Base = Engine<App>;
  static constexpr unsigned TLSBufSize = 64 * 1024;
  static constexpr unsigned RuntimePNLength = 2;
  static constexpr unsigned RuntimeCryptoChunk = 900;

  bool init(ClientParams params) {
    if (bool(params.certPath()) != bool(params.keyPath())) {
      auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
      errorFn(ZeEXCEPT(Error, "Zquic",
	"client certPath and keyPath must be configured together"));
      return false;
    }
    return this->init_(ZuMv(params), [](const ClientParams &) { return true; });
  }

  void final() {
    Base::final();
  }
};

// CRTP - aligned server implementation should conform to this interface:
#if 0
struct AppLink;
struct AppStream;
struct App : public Zquic::Server<App, AppLink> {

  // Optional: create or reject a logical QUIC connection.
  ZmRef<AppLink> accepted(const Zquic::InitialInfo &);
};

struct AppStream : public Zquic::SrvStream<AppLink, AppStream> {
  // Zquic Rx thread.
  // >0 consumed progress, 0 leave queued data, <0 reset/close per policy.
  int process(Zquic::RxStream &);
};

struct AppLink : public Zquic::SrvLink<App, AppLink, AppStream> {
  AppLink(App *);

  void connected(Zi::Connected); // Zquic Rx thread
  void disconnected(); // Zquic Rx thread
  void streamed(ZmRef<Stream>); // Zquic Rx thread
};
#endif
template <typename App_, typename Link_> class Server : public Engine<App_> {
public:
  using App = App_;
  using Link = Link_;
  using LinkRef = ZmRef<Link>;
  using LinkEntry = ServerLinkEntry<Link>;
  using LinkTable = ServerLinks_<Link>;
  using Base = Engine<App>;
  using Base::app;
  static constexpr unsigned TLSBufSize = Client<App>::TLSBufSize;
  static constexpr unsigned RuntimePNLength = Client<App>::RuntimePNLength;
  static constexpr unsigned RuntimeCryptoChunk = Client<App>::RuntimeCryptoChunk;

template <typename, typename, typename, typename>
friend class SrvLink;

  bool init(ServerParams params) {
    if (!params.certPath() || !params.keyPath()) {
      auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
      errorFn(ZeEXCEPT(Error, "Zquic",
	"server certPath and keyPath are required"));
      return false;
    }
    if (!this->init_(ZuMv(params), [](const ServerParams &) { return true; }))
      return false;
    return m_endpoint.init(this->mx());
  }

  void final() {
    close();
    Base::final();
  }

  bool listen() {
    ZiAssert(this->mx(), "Zquic", (),
      "QUIC server listen before app initialization", return false);
    close();
    ZiIP localIP = this->app()->localIP();
    uint16_t localPort = this->app()->localPort();
    return m_endpoint.openUDP(
      PathMode::ServerUnconnected,
      localIP, localPort,
      ZiIP{}, 0,
      Endpoint::DatagramFn{this, [](Server *self, Datagram d) {
	self->rxRun([self, d = ZuMv(d)]() mutable {
	  self->received_(ZuMv(d));
	});
      }},
      Endpoint::ReadyFn{this, [](Server *self, Endpoint *) {
	self->rxRun([self]() {
	  self->app()->listening();
	});
      }},
      Endpoint::FailFn{this, [](Server *self, bool transient) {
	self->rxRun([self, transient]() {
	  self->failed_0(transient);
	});
      }},
      Endpoint::DownFn{},
      Endpoint::TxDrainedFn{this, [](Server *self) {
	self->rxRun([self]() {
	  self->txDrained_();
	});
      }});
  }

  void close() {
    if (this->mx() && this->rxThread()) {
      ZmSemaphore stopped;
      this->rxInvoke([this, &stopped]() {
	close_();
	stopped.post();
      });
      stopped.wait();
      return;
    }
    close_();
  }

  void close_() {
    clearLinks_();
    m_endpoint.closeUDP(Endpoint::CloseFn{});
  }

  bool listening() const { return m_endpoint.listening(); }
  bool connected() const { return m_endpoint.connected(); }
  const ZiSockAddr &local() const { return m_endpoint.local(); }
  EndpointDiag endpointDiag() const {
    auto mx = this->mx();
    if (!mx) return m_endpoint.diag();
    if (mx->invoked(mx->txThread())) return m_endpoint.diag();
    EndpointDiag diag;
    ZmSemaphore done;
    auto server = const_cast<Server *>(this);
    mx->txRun([server, &diag, &done]() mutable {
      diag = server->m_endpoint.diag();
      done.post();
    });
    done.wait();
    return diag;
  }
  template <typename Fn>
  void allLinks(Fn fn) {
    ZiAssert(this->mx() && this->rxThread(), "Zquic", (),
      "QUIC server link iteration before app initialization", return);
    ZiAssert(!this->rxInvoked() && !this->txInvoked(), "Zquic", (),
      "QUIC server link iteration from I/O thread", return);
    ZmSemaphore done;
    this->rxInvoke([this, &fn, &done]() {
      auto links = m_links;
      auto i = links->citer();
      while (auto node = i()) {
	auto entry = node->val();
	if (entry && entry->linkRef) fn(entry->linkRef);
      }
      done.post();
    });
    done.wait();
  }

  ZiIP localIP() const { return ZiIP{}; }
  uint16_t localPort() const { return 0; }
  bool sendPkt(const ZmRef<ZiIOBuf> &) { return true; }
  bool sendFrame(const SentFrameRef &) { return true; }

  void listenFailed(bool transient) {
    this->error_(ZeEXCEPT(Error, "Zquic",
      ([transient](auto &s) {
	s << "QUIC server listen failed transient=" << transient;
      })));
  }
  LinkRef accepted(const InitialInfo &) {
    this->error_(ZeEXCEPT(Error, "Zquic",
      "QUIC server App must provide accepted(const InitialInfo &)"));
    return {};
  }

  ZmRef<ZiIOBuf> allocTxPkt_() {
    return m_endpoint.allocTxPkt();
  }
  bool sendPkt_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (!this->app()->sendPkt(buf)) return true;
    return m_endpoint.send(ZuMv(buf), ZuMv(addr));
  }
  void dissociateRoute_(const CxnID &id) {
    m_routes.retire(id);
  }

private:
  void txDrained_() {
    auto links = m_links;
    auto i = links->citer();
    while (auto node = i()) {
      auto entry = node->val();
      if (entry && entry->link)
	entry->link->app()->txRun([link = entry->linkRef]() mutable {
	  link->txDrained_();
	});
    }
  }

  void failed_0(bool transient) {
    this->app()->listenFailed(transient);
  }

  void received_(Datagram d) {
    Link *link = route_(d);
    if (!link) {
      sendStatelessReset_(d);
      m_endpoint.failure();
      return;
    }
    if (link->receivedRouted_(ZuMv(d)))
      link->installRoutes_(m_routes);
    else
      releaseLink_(link);
  }

  Link *route_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return nullptr;
    ZuCSpan packet{d.buf->data_(), d.buf->length};
    if (Pkt::isLong(packet)) return routeLong_(d, packet);
    return routeShort_(d, packet);
  }

  Link *routeLong_(const Datagram &d, ZuCSpan packet) {
    LongHdr h;
    if (Pkt::parseLong(packet, h) < 0) return nullptr;
    if (!VersionNegotiation::supported(h.version)) {
      sendVersionNegotiation_(h, d.addr);
      return nullptr;
    }
    if (Link *link = m_routes.find(h.dcid)) return link;
    if (h.type != PktType::Initial) return nullptr;
    return accept_(InitialInfo{h, d.addr, d.buf->length});
  }

  Link *routeShort_(const Datagram &, ZuCSpan packet) {
    return m_routes.matchShort(packet);
  }

  bool sendStatelessReset_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return false;
    ZuCSpan packet{d.buf->data_(), d.buf->length};
    if (Pkt::isLong(packet)) return false;
    ResetToken token;
    if (!m_routes.resetTokenForShort(packet, token)) return false;
    ZmRef<ZiIOBuf> buf = allocTxPkt_();
    int n = StatelessReset::writeForUnknownCID(
      buf->data_(), buf->size, packet, token);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    return sendPkt_(ZuMv(buf), d.addr);
  }

  Link *accept_(const InitialInfo &info) {
    LinkRef link;
    link = this->app()->accepted(info);
    if (!link) return nullptr;
    Link *ptr = link.ptr();
    if (!addLink_(ZuMv(link))) return nullptr;
    return ptr;
  }

  bool addLink_(LinkRef link) {
    if (!link) return false;
    Link *ptr = link.ptr();
    if (m_links->findVal(ptr)) return true;
    m_links->add(new LinkEntry{ZuMv(link)});
    return true;
  }

  void releaseLink_(Link *link) {
    if (!link) return;
    link->retireRoutes_(m_routes);
    LinkRef ref;
    if (auto entry = m_links->findVal(link)) ref = entry->linkRef;
    link->teardownTimers([this, link, ref = ZuMv(ref)]() mutable {
      this->rxRun([this, link, ref = ZuMv(ref)]() mutable {
	m_links->del(link);
      });
    });
  }

  void clearLinks_() {
    auto i = m_links->citer();
    while (auto node = i()) {
      auto entry = node->val();
      if (entry && entry->link) {
	LinkRef ref = entry->linkRef;
	auto link = entry->link;
	link->shutdown_();
	link->retireRoutes_(m_routes);
	link->teardownTimers([this, link, ref = ZuMv(ref)]() mutable {
	  this->rxRun([this, link, ref = ZuMv(ref)]() mutable {
	    m_links->del(link);
	  });
	});
      }
    }
    m_routes.clear();
  }

  bool sendVersionNegotiation_(const LongHdr &h, ZiSockAddr addr) {
    ZmRef<ZiIOBuf> buf = allocTxPkt_();
    int n = VersionNegotiation::write(
      buf->data_(), buf->size, h.scid, h.dcid);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    return sendPkt_(ZuMv(buf), ZuMv(addr));
  }

  // Rx thread exclusive
  Endpoint		m_endpoint;
  ZmRef<LinkTable>	m_links = new LinkTable{
    ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  CxnRouter<Link>	m_routes;
};

template <typename Link_, typename Impl, typename TxBufAlloc_>
class Stream :
  public ZmPolymorph,
  // Rx thread exclusive base. Only the Rx thread may use ZmPQRx APIs/state.
  public ZmPQRx<
    Stream<Link_, Impl, TxBufAlloc_>,
    StreamRxPQueue, ZmPQRxGapIgnore<>>,
  // Tx thread exclusive base. Only the Tx thread may use ZmPQTx APIs/state.
  public ZmPQTx<
    Stream<Link_, Impl, TxBufAlloc_>,
    TxDataPQueue> {
public:
  using Self = Stream<Link_, Impl, TxBufAlloc_>;
  using Link = Link_;
  using Impl_ = Impl;
  using TxBufAlloc = TxBufAlloc_;
  using Rx = ZmPQRx<Self, StreamRxPQueue, ZmPQRxGapIgnore<>>;
  using Tx = ZmPQTx<Self, TxDataPQueue>;
  using RxMsg = StreamRxPQueue::Node;
  using RxQueueSpan = StreamRxPQueue::Span;
  using TxMsg = TxDataPQueue::Node;
  using TxSpan = TxDataPQueue::Span;
  using TxKey = TxDataPQueue::Key;
  using TxUnackdQueue = StreamTxPQueue;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Stream(Link *link, int64_t id) : m_link{link}, m_id{id} {
#ifdef ZmObject_DEBUG
    this->debug();
#endif
  }

  Link *link() const { return m_link; }
  int64_t id() const { return m_id; }
  uint64_t txBytes() const { return m_txBytes; }
  uint64_t txBufferedBytes() const { return m_txBufferedBytes; }
  uint64_t txCreditLimit() const { return m_txCredit.limit(); }
  uint64_t txCreditAvailable() const { return m_txCredit.available(); }
  uint64_t rxCreditUsed() const { return m_rxCredit.used(); }
  uint64_t rxCreditLimit() const { return m_rxCredit.limit(); }
  uint64_t rxCreditAvailable() const { return m_rxCredit.available(); }
  unsigned txRangeCount() const { return m_txQueue.count_(); }
  unsigned txUnackdCount() const { return m_txUnackd.count_(); }
  uint64_t txUnackdBytes() const { return m_txUnackd.length_(); }
  uint64_t rxBytes() const { return m_rxDelivered; }
  uint64_t finalSize() const { return m_rxState.finalSize(); }
  unsigned rxPending() const { return m_rxQueue.count_(); }
  uint64_t appError() const { return m_appError; }
  StreamError::T error() const { return m_error; }
  bool finSent() const { return m_fin; }
  bool finDequeued() const { return m_finDequeued; }
  bool finReady() const {
    return !m_resetSent && m_fin && !m_finDequeued && !m_txQueue.count_();
  }
  bool finReceived() const { return m_rxState.finSeen(); }
  bool resetSent() const { return m_resetSent; }
  bool resetReceived() const { return m_resetReceived; }
  bool stopSent() const { return m_stopSent; }
  bool resetAckd() const { return m_resetAckd; }
  bool stopAckd() const { return m_stopAckd; }
  bool stopReceived() const { return m_stopReceived; }
  bool readOpen() const { return !m_resetReceived && !rxComplete(); }
  bool closedForStreamCredit() const {
    return (rxComplete() || m_resetReceived) && (m_finDequeued || m_resetSent);
  }
  bool streamCreditReturned() const { return m_streamCreditReturned; }
  bool txQueued() const { return m_txQueued; }
  void txQueued(bool v) { m_txQueued = v; }
  bool rxComplete() const {
    return m_rxState.complete() && m_rxDelivered == m_rxState.finalSize();
  }
  unsigned rxQueued() const { return m_rx.count_(); }

  RxStream &rxStream() { return m_rx; }

  StreamRxPQueue *rxQueue() { return &m_rxQueue; }
  void drainRx_() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx drain outside Rx thread", return);
    while (m_rxQueue.shift());
    m_rx.clean();
  }
  TxDataPQueue *txQueue() { return &m_txQueue; }
  TxUnackdQueue *txUnackdQueue() { return &m_txUnackd; }

  void txCredit(uint64_t limit) { m_txCredit.set(limit); }
  void extendTxCredit(uint64_t limit) { m_txCredit.extend(limit); }
  bool consumeTxCredit(uint64_t n) { return m_txCredit.consume(n); }
  void rxCredit(uint64_t limit) { m_rxCredit.set(limit); }
  void extendRxCredit(uint64_t limit) { m_rxCredit.extend(limit); }
  bool consumeRxCreditTo(uint64_t n) { return m_rxCredit.consumeTo(n); }
  uint64_t lastStreamDataBlocked() const { return m_lastStreamDataBlocked; }
  void lastStreamDataBlocked(uint64_t n) { m_lastStreamDataBlocked = n; }
  void markStreamCreditReturned() { m_streamCreditReturned = true; }
  unsigned queuedControlFrames() const {
    unsigned n = 0;
    if (m_maxStreamDataControl.queued) ++n;
    if (m_streamDataBlockedControl.queued) ++n;
    if (m_resetStreamControl.queued) ++n;
    if (m_stopSendingControl.queued) ++n;
    return n;
  }
  bool controlQueued() const { return queuedControlFrames(); }
  bool queueMaxStreamData(uint64_t value) {
    return queueControl_(
      m_maxStreamDataControl,
      ControlFrame::flowUpdate(FlowUpdate{
	FrameType::MaxStreamData, uint64_t(m_id), value,
	Zi::StreamType::Duplex}));
  }
  bool queueStreamDataBlocked(uint64_t value) {
    return queueControl_(
      m_streamDataBlockedControl,
      ControlFrame::blocked(
	FrameType::StreamDataBlocked, uint64_t(m_id), value));
  }
  bool queueResetStream(uint64_t appError, uint64_t finalSize) {
    return queueControl_(
      m_resetStreamControl,
      ControlFrame::resetStream(uint64_t(m_id), appError, finalSize));
  }
  bool queueStopSending(uint64_t appError) {
    return queueControl_(
      m_stopSendingControl,
      ControlFrame::stopSending(uint64_t(m_id), appError));
  }
  bool nextQueuedControl(ControlFrame &frame) const {
    if (m_maxStreamDataControl.queued) {
      frame = m_maxStreamDataControl.frame;
      return true;
    }
    if (m_streamDataBlockedControl.queued) {
      frame = m_streamDataBlockedControl.frame;
      return true;
    }
    if (m_resetStreamControl.queued) {
      frame = m_resetStreamControl.frame;
      return true;
    }
    if (m_stopSendingControl.queued) {
      frame = m_stopSendingControl.frame;
      return true;
    }
    return false;
  }
  void controlSent(const ControlFrame &frame) {
    PendingControl *slot = controlSlot_(frame.type);
    if (slot && slot->frame == frame) slot->queued = false;
  }
  void clearControl(const SentFrameRef &ref) {
    PendingControl *slot = controlSlot_(ref.controlType);
    if (!slot) return;
    ControlFrame frame = controlFrame_(ref);
    if (slot->frame == frame) *slot = {};
  }
  void clearControls() {
    m_maxStreamDataControl = {};
    m_streamDataBlockedControl = {};
    m_resetStreamControl = {};
    m_stopSendingControl = {};
  }
  bool controlStillValid(const ControlFrame &frame) const {
    const PendingControl *slot = controlSlot_(frame.type);
    if (!slot || !(slot->frame == frame)) return false;
    switch (frame.type) {
      case FrameType::MaxStreamData:
	return readOpen() && frame.value == rxCreditLimit();
      case FrameType::StreamDataBlocked:
	return !txCreditAvailable() && frame.value == txCreditLimit();
      case FrameType::ResetStream:
	return resetSent() && !resetAckd() && frame.value == txBytes();
      case FrameType::StopSending:
	return stopSent() && !stopAckd() && !resetReceived() && !rxComplete();
      default:
	return false;
    }
  }

  void process(RxMsg *msg) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream process outside Rx thread", return);
    if (!msg) return;
    StreamRxData &data = msg->data();
    if (!data.bytes) return;
    ZiAssert(data.bufOffset + data.bytes <= data.size, "Zquic", (),
      "stream Rx queued slice exceeds packet-backed range", return);
    data.skip = unsigned(data.bufOffset);
    data.ZiIOBuf::length = unsigned(data.bytes);
    m_rxDelivered += data.bytes;
    m_rxState.delivered(m_rxDelivered);
    ZmRef<Zquic_::IOQueue::Node> buf =
      static_cast<Zquic_::IOQueue::Node *>(msg);
    m_rx.push(ZuMv(buf));
  }
  void request(const RxQueueSpan &, const RxQueueSpan &) { }
  void scheduleDequeue() { rxRun_([stream = ZmRef<Self>{impl()}]() {
    stream->dequeueRx_();
  }); }
  void rescheduleDequeue() { scheduleDequeue(); }
  void idleDequeue() { }
  void dequeueRx_() {
    Rx::dequeue();
    if (m_link && !m_rx.empty())
      m_link->streamRxDequeued_(ZmRef<Impl>{impl()});
  }

  bool send_(TxMsg *, bool) { return true; }
  bool resend_(TxMsg *, bool) { return true; }
  bool sendGap_(const TxSpan &, bool) { return true; }
  bool resendGap_(const TxSpan &, bool) { return true; }
  void archive_(TxMsg *) { }
  ZmRef<TxMsg> retrieve_(TxKey, TxKey) { return nullptr; }
  void scheduleSend() { }
  void rescheduleSend() { }
  void idleSend() { }
  void scheduleResend() { }
  void rescheduleResend() { }
  void idleResend() { }
  void scheduleArchive() { }
  void rescheduleArchive() { }
  void idleArchive() { }

  bool txRange(unsigned i, TxRange &range) const {
    auto iter = m_txQueue.citer();
    while (auto node = iter()) {
      if (!i--) {
	const auto &data = node->data();
	range = TxRange{
	  node, uint32_t(data.bufOffset), uint32_t(data.bytes),
	  data.streamOffset};
	return true;
      }
    }
    return false;
  }
  bool nextTxRange(PktBudget &, TxRange &range, bool &fin) const {
    fin = false;
    if (txRange(0, range)) return true;
    if (!finReady()) return false;
    range = {};
    range.streamOffset = m_txBytes;
    fin = true;
    return true;
  }
  bool dequeueTxRange(TxRange &range) {
    auto iter = m_txQueue.citer();
    auto node = iter();
    if (!node) return false;
    return consumeTxRange(range, uint32_t(node->data().length()));
  }
  bool commitTxRange(TxRange &range, uint32_t length) {
    return consumeTxRange(range, length);
  }
  bool consumeTxRange(TxRange &range, uint32_t length) {
    auto iter = m_txQueue.citer();
    auto first = iter();
    if (!first) return false;
    return consumeTxRange(range, TxRange{
      first, uint32_t(first->data().bufOffset),
      uint32_t(first->data().bytes), first->data().streamOffset}, length);
  }
  bool commitTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    return consumeTxRange(range, selected, length);
  }
  bool consumeTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    if (!length) return false;
    if (!selected.buf) return false;
    uint64_t key = selected.streamOffset;
    auto current = m_txQueue.find(key);
    if (!current) return false;
    ZiIOBuf *currentBuf = current.ptr();
    if (selected.buf.ptr() != currentBuf) return false;
    const auto &selectedData = current->data();
    if (selected.offset != selectedData.bufOffset ||
	selected.streamOffset != selectedData.streamOffset ||
	selected.length != selectedData.bytes ||
	length > selectedData.length())
      return false;
    auto node = Tx::abort(key);
    if (!node || node.ptr() != current.ptr()) return false;
    auto &data = node->data();
    if (selected.offset != data.bufOffset ||
	selected.streamOffset != data.streamOffset ||
	selected.length != data.bytes ||
	length > data.length())
      return false;
    range = TxRange{
      node, uint32_t(data.bufOffset), length, data.streamOffset};
    range.length = length;
    if (length > m_txBufferedBytes) m_txBufferedBytes = 0;
    else m_txBufferedBytes -= length;
    if (length < data.length()) {
      data.clipHead(length);
      Tx::send(ZuMv(node));
    }
    return true;
  }
  bool dequeueFin(uint64_t &offset) {
    if (!finReady()) return false;
    offset = m_txBytes;
    m_finDequeued = true;
    return true;
  }
  bool recordTxUnackd(uint64_t offset, uint64_t length, bool fin) {
    if (!length && !fin) return false;
    return m_txUnackd.add(new TxUnackdQueue::Node{
      TxUnackdRange{offset, length, fin}}) != ZmPQResult::Invalid;
  }
  bool ackTxUnackd(uint64_t offset, uint64_t length, bool fin = false) {
    uint64_t n = length + (fin ? 1 : 0);
    return n && m_txUnackd.clear(offset, n);
  }
  bool txStillUnackd(uint64_t offset, uint64_t length, bool fin = false) const {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n) return false;
    bool found = false;
    (void)m_txUnackd.spans(offset, n, [&found](const auto &) {
      found = true;
      return false;
    });
    return found;
  }
  bool ackReset(uint64_t appError, uint64_t finalSize) {
    if (!m_resetSent || m_txBytes != finalSize) return false;
    m_resetAckd = true;
    return true;
  }
  bool ackStop(uint64_t appError) {
    if (!m_stopSent) return false;
    m_stopAckd = true;
    return true;
  }

  template <bool AppThread>
  class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;

  public:
    TxStream_(Stream &stream) : Base(unsigned(BufSize), 0, 0), m_stream{&stream} { }

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      ZiAssert(skip <= BufSize, "Zquic", (skip),
	"invalid stream headroom " << skip, return nullptr);
      ZmRef<ZiIOBuf> buf = new TxDataPQueue::Node{m_stream};
      buf->skip = skip;
      buf->length = 0;
      return buf;
    }

    void sendBuf_(ZmRef<ZiIOBuf> buf) {
      buf->owner = m_stream;
      auto stream = static_cast<Stream *>(buf->owner);
      if (AppThread)
	stream->send(ZuMv(buf));
      else
	stream->send_(ZuMv(buf));
    }

  private:
    // immutable
    Stream	*m_stream;
  };

  auto txStream() { return TxStream_<true>{*this}; }
  auto txStream_() { // direct call from within tx thread
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream txStream_ outside Tx thread",
      return TxStream_<false>{*this});
    return TxStream_<false>{*this};
  }

  void fin() {
    if (m_fin || m_resetSent) return;
    m_fin = true;
    notifyTx_();
  }
  void reset(uint64_t appError) {
    if (m_resetSent) return;
    m_resetSent = true;
    m_error = StreamError::Reset;
    m_appError = appError;
    if (m_link && m_id >= 0)
      m_link->localResetStream_(uint64_t(m_id), appError, m_txBytes);
    if (m_link && m_id >= 0)
      m_link->streamResetSent_(
	uint64_t(m_id), appError, m_txBytes);
  }
  void stop(uint64_t appError) {
    if (m_stopSent) return;
    m_stopSent = true;
    m_error = StreamError::Stop;
    m_appError = appError;
    if (m_link && m_id >= 0)
      m_link->localStopSending_(uint64_t(m_id), appError);
    if (m_link && m_id >= 0)
      m_link->streamStopSendingSent_(uint64_t(m_id), appError);
  }

  bool receiveFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    return receiveFrame_(frame, nullptr, diag, ZuMv(packet));
  }
  bool receiveFrame(
    const Frame &frame, ReceiveFlow &flow, ZmRef<ZiIOBuf> packet,
    BufDiag *diag = nullptr) {
    return receiveFrame_(frame, &flow, diag, ZuMv(packet));
  }

  int processFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    uint64_t delivered = m_rxState.delivered();
    bool finSeen = m_rxState.finSeen();
    unsigned pending = m_rxQueue.count_();
    if (!receiveFrame(frame, ZuMv(packet), diag)) return -1;
    if (m_rxState.delivered() == delivered && m_rxState.finSeen() == finSeen &&
	m_rxQueue.count_() == pending)
      return 0;
    return processRx_();
  }
  int processFrame(
    const Frame &frame, FlowCredit &connection,
    ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    uint64_t delivered = m_rxState.delivered();
    bool finSeen = m_rxState.finSeen();
    unsigned pending = m_rxQueue.count_();
    if (!receiveFrame_(frame, nullptr, &connection, diag, ZuMv(packet)))
      return -1;
    if (m_rxState.delivered() == delivered && m_rxState.finSeen() == finSeen &&
	m_rxQueue.count_() == pending)
      return 0;
    return processRx_();
  }
  int processRx_() { return impl()->process(m_rx); }

  bool receiveReset(const Frame &frame) {
    if (frame.type != FrameType::ResetStream || m_id < 0 ||
	frame.streamID != uint64_t(m_id) ||
	m_rxDelivered > frame.length ||
	rxPendingBeyond_(frame.length))
      return false;
    StreamRxState next = m_rxState;
    if (!next.receive(frame.length, 0, true)) return false;
    m_rxState = next;
    m_resetReceived = true;
    m_error = StreamError::Reset;
    m_appError = frame.errorCode;
    return true;
  }

  bool receiveBlocked(const Frame &frame) const {
    if (frame.type != FrameType::StreamDataBlocked || m_id < 0 ||
	frame.streamID != uint64_t(m_id))
      return false;
    if (m_rxState.finalSizeKnown() && frame.value > m_rxState.finalSize())
      return false;
    return frame.value <= m_rxCredit.limit();
  }

  bool receiveStop(const Frame &frame) {
    if (frame.type != FrameType::StopSending || m_id < 0 ||
	frame.streamID != uint64_t(m_id))
      return false;
    m_stopReceived = true;
    m_error = StreamError::Stop;
    m_appError = frame.errorCode;
    return true;
  }

private:
  bool receiveFrame_(
    const Frame &frame, ReceiveFlow *flow, BufDiag *diag,
    ZmRef<ZiIOBuf> packet) {
    return receiveFrame_(frame, flow, nullptr, diag, ZuMv(packet));
  }

  bool receiveFrame_(
    const Frame &frame, ReceiveFlow *flow, FlowCredit *connection,
    BufDiag *diag, ZmRef<ZiIOBuf> packet) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream receive outside Rx thread", return false);
    if (frame.type != FrameType::Stream || m_id < 0 ||
	frame.streamID != uint64_t(m_id) ||
	frame.length != frame.payload.length() ||
	frame.length > BufSize)
      return false;
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;

    if (!m_rxState.validate(frame.offset, frame.length, frame.fin))
      return false;
    if (frame.fin && rxPendingBeyond_(end)) return false;

    auto spans = ZtLocalArray(RxSpans, m_rxQueue.count_() + 1);
    if (frame.length && !newRxSpans_(frame, spans)) return false;
    uint64_t newBytes = rxSpanBytes(spans);
    if (flow && !flow->receive(end, newBytes)) return false;
    if (connection) {
      if (end > m_rxCredit.limit() || newBytes > connection->available())
	return false;
      if (!m_rxCredit.consumeTo(end) || !connection->consume(newBytes))
	return false;
    }

    if (spans.length())
      if (!queueRxSlices_(frame, spans, ZuMv(packet), diag)) return false;

    return m_rxState.receive(frame.offset, frame.length, frame.fin);
  }

  bool newRxSpans_(const Frame &frame, RxSpans &spans) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx span check outside Rx thread", return false);
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;
    if (end <= m_rxDelivered) return true;

    uint64_t first = frame.offset < m_rxDelivered ? m_rxDelivered : frame.offset;
    return rxNovelSpans(m_rxQueue, first, end, spans);
  }

  bool queueRxSlices_(
    const Frame &frame, const RxSpans &spans,
    ZmRef<ZiIOBuf> packet, BufDiag *diag) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx queue outside Rx thread", return false);
    if (!packet) return false;
    for (unsigned i = 0; i < spans.length(); ++i) {
      uint64_t payloadOffset = spans[i].first - frame.offset;
      uint64_t length64 = spans[i].length();
      if (payloadOffset > frame.payload.length() ||
	  length64 > frame.payload.length() - payloadOffset)
	return false;
      const uint8_t *data = reinterpret_cast<const uint8_t *>(
	frame.payload.data() + payloadOffset);
      Rx::rcvd(new StreamRxPQueue::Node{
	packet, data, unsigned(length64), this, spans[i].first});
    }
    return true;
  }

  bool rxPendingBeyond_(uint64_t finalSize) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx pending check outside Rx thread", return false);
    auto iter = m_rxQueue.citer();
    while (auto node = iter()) {
      const StreamRxData &data = node->data();
      if (data.offset > finalSize || data.bytes > finalSize - data.offset)
	return true;
    }
    return false;
  }

  void send(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(!buf || !buf->length)) return;
    buf->owner = this;
    txInvoke_([buf = ZuMv(buf)]() mutable {
      auto stream = static_cast<Stream *>(buf->owner);
      stream->send_(ZuMv(buf));
    });
  }

  void send_(ZmRef<ZiIOBuf> buf) { // direct call from within tx thread
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream send_ outside Tx thread", return);
    if (ZuUnlikely(!buf)) return;
    if (m_resetSent) return;
    if (!buf->length) return;
    ZiAssert(buf->skip + buf->length <= buf->size, "Zquic", (),
      "stream Tx buffer range violation", return);
    uint32_t offset = buf->skip;
    uint32_t length = buf->length;
    auto node = static_cast<TxMsg *>(buf.ptr());
    node->data().publish(offset, length, m_txBytes);
    Tx::send(node);
    m_txBytes += length;
    m_txBufferedBytes += length;
    notifyTx_();
  }

  bool txInvoked_() const {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Tx access before app initialization",
      return false);
    return m_link->app()->txInvoked();
  }
  bool rxInvoked_() const {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Rx access before app initialization",
      return false);
    return m_link->app()->rxInvoked();
  }

  template <typename Fn>
  void txInvoke_(Fn &&fn) {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Tx invoke before app initialization",
      return);
    m_link->app()->txInvoke(ZuFwd<Fn>(fn));
  }
  template <typename Fn>
  void rxRun_(Fn &&fn) {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Rx run before app initialization",
      return);
    m_link->app()->rxRun(ZuFwd<Fn>(fn));
  }

  void notifyTx_() {
    if (m_link && m_id >= 0)
      m_link->streamWritable_(ZmRef<Impl>{impl()});
  }
  struct PendingControl {
    ControlFrame	frame;
    bool		queued = false;
  };
  bool queueControl_(PendingControl &slot, const ControlFrame &frame) {
    if (m_id < 0 || !frame) return false;
    if (slot.frame == frame) {
      if (slot.queued) return false;
      slot.queued = true;
      return true;
    }
    if (slot.frame &&
	frame.type != FrameType::ResetStream &&
	frame.type != FrameType::StopSending &&
	slot.frame.value >= frame.value)
      return false;
    slot.frame = frame;
    slot.queued = true;
    return true;
  }
  PendingControl *controlSlot_(FrameType::T type) {
    switch (type) {
      case FrameType::MaxStreamData: return &m_maxStreamDataControl;
      case FrameType::StreamDataBlocked: return &m_streamDataBlockedControl;
      case FrameType::ResetStream: return &m_resetStreamControl;
      case FrameType::StopSending: return &m_stopSendingControl;
      default: return nullptr;
    }
  }
  const PendingControl *controlSlot_(FrameType::T type) const {
    return const_cast<Stream *>(this)->controlSlot_(type);
  }
  static ControlFrame controlFrame_(const SentFrameRef &ref) {
    ControlFrame frame;
    frame.type = ref.controlType;
    frame.streamID = ref.streamID;
    frame.value = ref.value;
    if (ref.controlType == FrameType::ResetStream) {
      frame.errorCode = ref.value;
      frame.value = ref.length;
    } else if (ref.controlType == FrameType::StopSending) {
      frame.errorCode = ref.value;
      frame.value = 0;
    }
    frame.streamType = ref.streamType;
    return frame;
  }

  // immutable
  Link			*m_link = nullptr;
  int64_t		m_id;

  // Rx thread exclusive
  FlowCredit		m_rxCredit;
  uint64_t		m_rxDelivered = 0;
  bool			m_resetReceived = false;
  bool			m_stopReceived = false;
  bool			m_streamCreditReturned = false;
  StreamRxState		m_rxState;
  RxStream		m_rx;
  StreamRxPQueue	m_rxQueue{0};
  uint64_t		m_lastStreamDataBlocked = uint64_t(-1);
  PendingControl	m_maxStreamDataControl;
  PendingControl	m_streamDataBlockedControl;
  uint64_t		m_appError = 0;
  StreamError::T	m_error = StreamError::None;

  // Tx thread exclusive
  uint64_t		m_txBytes = 0;
  uint64_t		m_txBufferedBytes = 0;
  FlowCredit		m_txCredit;
  bool			m_fin = false;
  bool			m_finDequeued = false;
  bool			m_resetSent = false;
  bool			m_resetAckd = false;
  bool			m_stopSent = false;
  bool			m_stopAckd = false;
  PendingControl	m_resetStreamControl;
  PendingControl	m_stopSendingControl;
  bool			m_txQueued = false;
  TxDataPQueue		m_txQueue{0};
  StreamTxPQueue	m_txUnackd{0};
};

template <typename Link, typename Impl, typename TxBufAlloc_ = StreamTxBufAlloc<>>
class CliStream : public Stream<Link, Impl, TxBufAlloc_> {
public:
  using Base = Stream<Link, Impl, TxBufAlloc_>;
  using Base::Base;
};

template <typename Link, typename Impl, typename TxBufAlloc_ = StreamTxBufAlloc<>>
class SrvStream : public Stream<Link, Impl, TxBufAlloc_> {
public:
  using Base = Stream<Link, Impl, TxBufAlloc_>;
  using Base::Base;
};

template <typename Stream_>
inline int64_t Stream_IDAxor(const Stream_ &s) { return s.id(); }

template <typename Stream_>
ZuDerive(Streams_,
  (ZmHash<Stream_,
    ZmHashNode<Stream_,
      ZmHashKey<Stream_IDAxor<Stream_>,
	ZmHashLock<ZmPLock,
	  ZmHashHeapID<"Zquic.Stream.ObjectHash">>>>>));

template <typename App, typename Impl, typename TxBufAlloc_, typename Stream_>
class Link : public ZmPolymorph {
template <typename, typename>
friend class Server;
public:
  using TxBufAlloc = TxBufAlloc_;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  using Streams = Streams_<Stream>;
  using StreamsRef = ZmRef<Streams>;
  using PathResponses =
    ZmQueue<ControlFrame,
      ZmQueueHeapID<"Zquic.Link.PathResponses">>;
  using StreamQueue =
    ZmQueue<StreamRef,
      ZmQueueHeapID<"Zquic.Link.StreamQueue">>;
  static constexpr unsigned OpenQueuedBatch = 32;
  // PATH_RESPONSE frames are concrete replies; cap queued payloads at the
  // per-packet sent-frame metadata limit.
  static constexpr unsigned PathResponseMax = SentPkt::MaxFrames;
  static constexpr unsigned RecoveryScanBatch = 256;
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer = false) :
    m_app{app}, m_isServer{isServer}, m_streams{new Streams} {
    for (unsigned i = 0; i < 3; ++i) {
      CryptoStream *crypto = &m_rxCrypto[i];
      crypto->dequeueFn([this, crypto]() {
	this->app()->rxRun([crypto]() {
	  crypto->dequeueRx_();
	});
      });
    }
    for (unsigned i = 0; i < AckManager::Spaces; ++i) {
      auto space = PktSpace::T(i);
      AckTracker *tracker = &m_rxAcks.tracker(space);
      tracker->dequeueFn([this, tracker]() {
	this->app()->rxRun([tracker]() {
	  tracker->dequeueRx_();
	});
      });
    }
  }
  ~Link() {
    clearCallbacks_();
    assert(!timersActive_());
  }

  void initCryptoDelivery_() {
    for (unsigned i = 0; i < 3; ++i) {
      auto level = CryptoLevel::T(i);
      CryptoStream *crypto = &m_rxCrypto[i];
      crypto->deliveryFn([this, level](ZuCSpan span) {
	size_t epoch = level == CryptoLevel::Initial ? 0 :
	  level == CryptoLevel::Handshake ? 2 : 3;
	if (!this->impl()->emitTLS_(epoch, span, m_rxCryptoAddr[level]))
	  this->tlsFailure_();
      });
    }
  }

  void clearCallbacks_() {
    for (auto &crypto : m_rxCrypto) {
      crypto.dequeueFn({});
      crypto.deliveryFn({});
    }
    for (unsigned i = 0; i < AckManager::Spaces; ++i)
      m_rxAcks.tracker(PktSpace::T(i)).dequeueFn({});
  }

  App *app() const { return m_app; }
  bool isServer() const { return m_isServer; }
  bool closed() const { return m_closed; }
  uint64_t closeError() const { return m_closeError; }
  uint64_t streamCount() const { return m_streams->count_(); }
  uint64_t peerStreamLimit(Zi::StreamType::T type) const {
    return localLimit_(type).limit();
  }
  uint64_t localStreamsOpened(Zi::StreamType::T type) const {
    return localLimit_(type).opened();
  }
  uint64_t queuedLocalStreams(Zi::StreamType::T type) const {
    return queued_(type);
  }
  uint64_t localStreamLimit(Zi::StreamType::T type) const {
    return peerLimit_(type).limit();
  }
  uint64_t peerStreamsOpened(Zi::StreamType::T type) const {
    return peerLimit_(type).opened();
  }
  bool localStreamsBlocked(Zi::StreamType::T type) const {
    return queued_(type) != 0;
  }
  unsigned queuedControlFrames() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control queue inspection outside Tx thread", return 0);
    unsigned n = 0;
    if (m_maxDataControl.queued) ++n;
    if (m_dataBlockedControl.queued) ++n;
    if (m_handshakeDoneControl.queued) ++n;
    if (m_pathChallengeControl.queued) ++n;
    for (unsigned i = 0; i < 2; ++i) {
      if (m_maxStreamsControl[i].queued) ++n;
      if (m_streamsBlockedControl[i].queued) ++n;
    }
    n += m_pathResponses.count_();
    auto iter = m_streams->citer();
    while (auto node = iter())
      n += node->data().queuedControlFrames();
    return n;
  }
  uint64_t rxDataCreditUsed() const { return m_rxDataCredit.used(); }
  uint64_t rxDataCreditLimit() const { return m_rxDataCredit.limit(); }
  uint64_t rxDataCreditAvailable() const { return m_rxDataCredit.available(); }

  void setPeerStreamLimit(Zi::StreamType::T type, uint64_t limit) {
    localLimit_(type).set(limit);
  }
  void setLocalStreamLimit(Zi::StreamType::T type, uint64_t limit) {
    peerLimit_(type).set(limit);
  }

  bool rxApplyMaxStreams_(const Frame &frame) {
    if (!validateMaxStreams_(frame)) return false;
    uint64_t value = frame.value;
    Zi::StreamType::T type = frame.streamType;
    app()->txRun([link = ZmMkRef(impl()), type, value]() mutable {
      link->txApplyMaxStreams_(type, value);
      link->flushTx_();
    });
    return true;
  }
  bool applyMaxData(const Frame &frame) {
    if (!validateMaxData_(frame)) return false;
    txApplyMaxData_(frame.value);
    return true;
  }
  bool applyMaxStreamData(const Frame &frame) {
    if (frame.type == FrameType::MaxStreamData &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->resetSent() || stream->finDequeued()) {
	  noteInvalidStreamActivity_(
	    TransportError::StreamState, true);
	  return true;
	}
      } else if (closedStreamID_(frame.streamID) &&
	  canLocalSend_(frame.streamID)) {
	noteInvalidStreamActivity_(TransportError::StreamState, true);
	return true;
      }
    }
    StreamRef stream;
    if (!validateMaxStreamData_(frame, stream)) {
      noteInvalidStreamActivity_(TransportError::StreamState);
      return false;
    }
    txApplyMaxStreamData_(ZuMv(stream), frame.value);
    return true;
  }
  bool receiveDataBlocked(const Frame &frame) {
    return validateDataBlocked_(frame);
  }
  bool receiveStreamDataBlocked(const Frame &frame) {
    return receiveStreamDataBlocked_(frame);
  }
  bool receiveStreamsBlocked(const Frame &frame) {
    return validateStreamsBlocked_(frame);
  }
  bool frameLegal(CryptoLevel::T level, const Frame &frame) const {
    return packetFrameLegal_(level, frame);
  }

  StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Duplex) {
    if (!localLimit_(type).open()) {
      ++queued_(type);
      queueBlocked_(
	FrameType::StreamsBlocked, 0, localLimit_(type).limit(), type);
      impl()->streamsBlocked_(type, localLimit_(type).limit());
      return nullptr;
    }
    return openLocalStream_(type);
  }

  StreamRef acceptPeerStream(uint64_t id) {
    if (id > uint64_t(INT64_MAX)) return nullptr;
    if (StreamID::server(id) == m_isServer) return nullptr;
    if (auto stream = findStream(int64_t(id))) return stream;
    if (peerOpenedStreamID_(id)) return nullptr;
    Zi::StreamType::T type = StreamID::uni(id) ? Zi::StreamType::Simplex : Zi::StreamType::Duplex;
    uint64_t opened = StreamID::ordinal(id) + 1;
    if (!peerLimit_(type).allowsTo(opened)) return nullptr;

    StreamRef stream = newStream_(int64_t(id));
    if (!stream) return nullptr;
    ZiAssert(peerLimit_(type).openTo(opened), "Zquic",
      (), "peer stream count advanced past local limit", return nullptr);
    impl()->streamed(stream);
    scheduleStreamWritable_(stream);
    return stream;
  }

  StreamRef findStream(int64_t id) const {
    return m_streams->find(id);
  }

  int receiveFrame(const Frame &frame, BufDiag *diag = nullptr) {
    if (frame.type != FrameType::ResetStream &&
	frame.type != FrameType::StopSending)
      return -1;
    StreamRef stream = findOrAccept_(frame.streamID);
    if (!stream) {
      if (frame.streamID <= uint64_t(INT64_MAX) &&
	  closedStreamID_(frame.streamID)) {
	return 0;
      }
      noteInvalidStreamActivity_(TransportError::StreamState);
      return -1;
    }
    if (frame.type == FrameType::ResetStream) {
      if (stream->resetReceived()) {
	if (stream->finalSize() == frame.length) return 0;
	noteInvalidStreamActivity_(TransportError::FinalSize, true, true);
	return -1;
      }
      uint64_t old = stream->rxCreditUsed();
      uint64_t novel = frame.length > old ? frame.length - old : 0;
      if (frame.length > stream->rxCreditLimit() ||
	  novel > m_rxDataCredit.available()) {
	noteInvalidStreamActivity_(TransportError::FlowControl, false, true);
	return -1;
      }
      if (!stream->receiveReset(frame)) {
	noteInvalidStreamActivity_(TransportError::FinalSize, false, true);
	return -1;
      }
      if (!stream->consumeRxCreditTo(frame.length) ||
	  !m_rxDataCredit.consume(novel)) {
	noteInvalidStreamActivity_(TransportError::FlowControl, false, true);
	return -1;
      }
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      returnStreamCredit_(stream);
      impl()->streamResetReceived(stream, frame.errorCode, frame.length);
      reapStream_(stream);
      return 0;
    }
    if (stream->resetSent()) return 0;
    if (stream->stopReceived()) return 0;
    if (stream->receiveStop(frame)) {
      impl()->streamStopSendingReceived(stream, frame.errorCode);
      return 0;
    }
    noteInvalidStreamActivity_(TransportError::StreamState);
    return -1;
  }

  int receiveFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr,
    bool *immediateAck = nullptr) {
    if (frame.type != FrameType::Stream)
      return receiveFrame(frame, diag);
    StreamRef stream = findOrAccept_(frame.streamID);
    if (!stream) {
      if (frame.streamID > uint64_t(INT64_MAX) ||
	  !closedStreamID_(frame.streamID) ||
	  !canPeerSend_(frame.streamID)) {
	noteInvalidStreamActivity_(TransportError::StreamState);
	return -1;
      }
      if (immediateAck) *immediateAck = true;
      return 0;
    }
    bool rxClosed = stream->rxComplete() || stream->resetReceived();
    int rc = stream->processFrame(frame, m_rxDataCredit, ZuMv(packet), diag);
    if (rc >= 0) {
      if ((rxClosed || !rc) && immediateAck) *immediateAck = true;
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      returnStreamCredit_(stream);
      impl()->streamData(stream, frame.offset, frame.payload, frame.fin);
      if (frame.fin) reapStream_(stream);
    } else {
      TransportError::T error = stream->rxComplete() || stream->resetReceived() ?
	TransportError::StreamState : TransportError::FinalSize;
      noteInvalidStreamActivity_(error, false, true);
    }
    return rc;
  }
  void streamRxDequeued_(StreamRef stream) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx dequeue processing outside Rx thread", return);
    if (!stream) return;
    bool wasRxQueued = stream->rxPending() || stream->rxQueued();
    int rc = stream->processRx_();
    if (rc >= 0) {
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      returnStreamCredit_(stream);
      if (wasRxQueued && !stream->rxPending() && !stream->rxQueued())
	reapStream_(stream);
      return;
    }
    TransportError::T error = stream->rxComplete() || stream->resetReceived() ?
      TransportError::StreamState : TransportError::FinalSize;
    noteInvalidStreamActivity_(error, false, true);
  }

  void streamWritable_(const StreamRef &stream) {
    if (!stream || stream->id() < 0 || stream->txQueued()) return;
    stream->txQueued(true);
    m_streamQueue.push(stream);
  }
  void scheduleStreamWritable_(StreamRef stream) {
    if (!stream || stream->id() < 0) return;
    app()->txInvoke(impl(), [
      link = impl(),
      stream = ZuMv(stream)
    ]() mutable {
      if (link->streamTxPending_(stream)) {
	link->streamWritable_(stream);
	link->flushTx_();
      }
      return link;
    });
  }
  bool localResetStream_(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    return queueResetStream_(streamID, appError, finalSize);
  }
  bool localStopSending_(uint64_t streamID, uint64_t appError) {
    return queueStopSending_(streamID, appError);
  }
  void streamResetSent_(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    impl()->streamResetSent(streamID, appError, finalSize);
  }
  void streamStopSendingSent_(uint64_t streamID, uint64_t appError) {
    impl()->streamStopSendingSent(streamID, appError);
  }
  void transportClose_(FrameType::T type, uint64_t errorCode) {
    impl()->transportClose(type, errorCode);
  }

  void close(uint64_t errorCode = 0) {
    m_closeError = errorCode;
    m_closed = true;
    if (app() && app()->mx()) cancelTimers();
  }
  void closeForStreamActivity_(TransportError::T error) {
    ++m_rxDiag.suspiciousStreamCloses;
    m_suspiciousStreamClosed = true;
    if (!m_closed) close(error);
  }
  void noteInvalidStreamActivity_(
    TransportError::T error, bool closedStream = false, bool immediate = false) {
    ++m_rxDiag.invalidStreamFrames;
    if (closedStream) ++m_rxDiag.closedStreamFrames;
    if (m_suspiciousStreamClosed) return;
    if (immediate || ++m_suspiciousStreamFrames >= SuspiciousStreamThreshold)
      closeForStreamActivity_(error);
  }

protected:
  struct InitialKeyDir { enum T { Client, Server }; };
  struct RuntimeCID { enum T { Initial, Local, Peer }; };
  enum { RuntimePNLength = 2 };
  // RFC9000 specifies only a minimum/default of 2 for active_connection_id_limit.
  // This is the local advertised policy cap for active peer-issued CIDs.
  static constexpr unsigned LocalActiveConnectionIDLimit = 8;
  static constexpr unsigned SuspiciousStreamThreshold = 8;

  struct LinkCID {
    CxnID			id;
    uint64_t			sequence = 0;
    ResetToken	resetToken;
    CxnState::T		state = CxnState::Tombstone;
    bool			associated = false;
  };
  struct PathState {
    Path		path;
    Path		prev;
    CxnID		peerCID;
    uint64_t		peerSeq = 0;
    ZuTime		deadline;
    PathChallenge	challenge;
    bool		active = false;
  };
  struct AckSnapshot {
    CryptoLevel::T	level = CryptoLevel::Initial;
    uint64_t		gen = 0;
    uint64_t		delay = 0;
    uint64_t		largestRxTime = 0;
    AckECN		ecn;
    unsigned		nRanges = 0;
    bool		due = false;
    AckRange		ranges[Frame::MaxAckRanges];
  };
  struct TxAckWork {
    AckSnapshot		ack;
    uint64_t		gen = 0;
    PktAckBatch		ackBatch;
    PktLossBatch	lossBatch;
    bool		ecnValidated = false;
    bool		lossPhase = false;
    bool		congestionOpened = false;
    bool		retransmit = false;
  };
  struct TxCryptoSnapshot {
    TrafficSecret	secrets[3];
    bool		installed[3] = {};
  };
  struct TxPktRefs {
    bool add(const SentFrameRef &ref, Stream *stream = nullptr) {
      if (ref.kind == SentFrameKind::None || n >= SentPkt::MaxFrames)
	return false;
      refs[n++] = ref;
      streams[n - 1] = stream;
      return true;
    }
    unsigned count() const { return n; }
    const SentFrameRef &operator [](unsigned i) const { return refs[i]; }
    Stream *stream(unsigned i) const { return streams[i]; }

    SentFrameRef	refs[SentPkt::MaxFrames];
    Stream		*streams[SentPkt::MaxFrames] = {};
    unsigned		n = 0;
  };
  struct PendingControl {
    ControlFrame	frame;
    bool		queued = false;
  };
  using LocalCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.LocalCID">>;
  using PeerCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.PeerCID">>;

  unsigned scheduledStreamCount_() const {
    return m_streamQueue.count_();
  }

  bool queueControl_(const ControlFrame &frame) {
    if (!frame) return false;
    app()->txInvoke(impl(), [link = impl(), frame]() mutable {
      link->txQueueControl_(frame);
      return link;
    });
    return true;
  }
  bool txQueueControl_(const ControlFrame &frame) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control queue mutation outside Tx thread", return false);
    if (!frame) return false;
    switch (frame.type) {
      case FrameType::MaxData:
	return queuePendingControl_(m_maxDataControl, frame);
      case FrameType::MaxStreams:
	return queuePendingControl_(
	  m_maxStreamsControl[streamTypeIndex_(frame.streamType)], frame);
      case FrameType::DataBlocked:
	return queuePendingControl_(m_dataBlockedControl, frame);
      case FrameType::StreamsBlocked:
	return queuePendingControl_(
	  m_streamsBlockedControl[streamTypeIndex_(frame.streamType)], frame);
      case FrameType::PathChallenge:
	return queuePendingControl_(m_pathChallengeControl, frame);
      case FrameType::PathResponse:
	m_pathResponses.push(frame);
	while (m_pathResponses.count_() > PathResponseMax) m_pathResponses.shift();
	return true;
      case FrameType::HandshakeDone:
	return queuePendingControl_(m_handshakeDoneControl, frame);
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending: {
	if (frame.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream = findStream(int64_t(frame.streamID));
	if (!stream) return false;
	bool queued = false;
	switch (frame.type) {
	  case FrameType::MaxStreamData:
	    queued = stream->queueMaxStreamData(frame.value);
	    break;
	  case FrameType::StreamDataBlocked:
	    queued = stream->queueStreamDataBlocked(frame.value);
	    break;
	  case FrameType::ResetStream:
	    queued = stream->queueResetStream(frame.errorCode, frame.value);
	    break;
	  case FrameType::StopSending:
	    queued = stream->queueStopSending(frame.errorCode);
	    break;
	  default:
	    break;
	}
	if (queued) streamWritable_(stream);
	return queued;
      }
      default:
	return false;
    }
  }
  bool queueFlowUpdate_(const FlowUpdate &update) {
    return update.needed() && queueControl_(ControlFrame::flowUpdate(update));
  }
  bool queueBlocked_(
    FrameType::T type, uint64_t streamID, uint64_t value,
    Zi::StreamType::T streamType = Zi::StreamType::Duplex) {
    ControlFrame frame = ControlFrame::blocked(
      type, streamID, value, streamType);
    if (!blockedFrameNeeded_(frame)) return false;
    if (!queueControl_(frame)) return false;
    noteBlockedQueued_(frame);
    return true;
  }
  bool queuePathResponse_(ZuCSpan data) {
    return queueControl_(ControlFrame::pathResponse(data));
  }
  bool queuePathChallenge_(ZuCSpan data) {
    return queueControl_(ControlFrame::pathChallenge(data));
  }
  bool queueHandshakeDone_() {
    return queueControl_(ControlFrame::handshakeDone());
  }
  bool queueResetStream_(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    return queueControl_(ControlFrame::resetStream(
      streamID, appError, finalSize));
  }
  bool queueStopSending_(uint64_t streamID, uint64_t appError) {
    return queueControl_(ControlFrame::stopSending(streamID, appError));
  }
  bool rxApplyMaxData_(const Frame &frame) {
    if (!validateMaxData_(frame)) return false;
    uint64_t value = frame.value;
    app()->txRun([link = ZmMkRef(impl()), value]() mutable {
      link->txApplyMaxData_(value);
      link->flushTx_();
    });
    return true;
  }
  bool rxApplyMaxStreamData_(const Frame &frame) {
    if (frame.type == FrameType::MaxStreamData &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->resetSent() || stream->finDequeued()) {
	  noteInvalidStreamActivity_(
	    TransportError::StreamState, true);
	  return true;
	}
      }
    }
    StreamRef stream;
    if (!validateMaxStreamData_(frame, stream)) {
      noteInvalidStreamActivity_(TransportError::StreamState);
      return false;
    }
    uint64_t value = frame.value;
    app()->txRun([
      link = ZmMkRef(impl()),
      stream = ZuMv(stream),
      value
    ]() mutable {
      link->txApplyMaxStreamData_(ZuMv(stream), value);
      link->flushTx_();
    });
    return true;
  }
  bool txApplyMaxData_(uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_DATA processing outside Tx thread", return false);
    if (value <= m_txDataCredit.limit()) return true;
    m_txDataCredit.extend(value);
    m_dataBlockedControl = {};
    return true;
  }
  bool txApplyMaxStreamData_(StreamRef stream, uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_STREAM_DATA processing outside Tx thread", return false);
    if (!stream) return false;
    stream->extendTxCredit(value);
    stream->clearControl(SentFrameRef::blocked(
      FrameType::StreamDataBlocked, uint64_t(stream->id()),
      stream->lastStreamDataBlocked(), Zi::StreamType::Duplex));
    if (streamTxPending_(stream)) streamWritable_(stream);
    return true;
  }
  bool txApplyMaxStreams_(Zi::StreamType::T type, uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_STREAMS processing outside Tx thread", return false);
    localLimit_(type).extend(value);
    openQueued_(type, OpenQueuedBatch);
    scheduleOpenQueued_(type);
    return true;
  }

  bool runtimeEstablished_() const { return m_established; }
  bool debugLog_() const {
#if defined(Zquic_DEBUG) && defined(ZiMultiplex_DEBUG)
    return app() && app()->mx() && app()->mx()->debug();
#else
    return false;
#endif
  }
  bool runtimeDraining_() const {
    return m_runtimeCloseState == CloseState::Draining;
  }
  bool runtimeHandshakeStarted_() const { return m_handshakeStarted; }
  RuntimeRxDiag rxDiagSnapshot_() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx diagnostic snapshot outside Rx thread", return {});
    return m_rxDiag;
  }
  RuntimeTxDiag txDiagSnapshot_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx diagnostic snapshot outside Tx thread", return {});
    return txDiag_();
  }
  RuntimeDiag runtimeDiag() const {
    if (!app() || !app()->mx()) return runtimeDiag_();
    ZiAssert(!rxInvoked_() && !txInvoked_(), "Zquic", (),
      "QUIC runtime diagnostic snapshot from I/O thread", return {});
    RuntimeRxDiag rx = m_rxDiag;
    RuntimeTxDiag tx;
    ZmSemaphore txDone;
    auto link = const_cast<Link *>(this)->impl();
    app()->txRun([link, &tx, &txDone]() {
      tx = link->txDiagSnapshot_();
      txDone.post();
    });
    txDone.wait();
    return {rx, tx};
  }
  RuntimeDiag runtimeDiag_() const {
    return {m_rxDiag, txDiag_()};
  }
  PathDiag pathDiag() const {
    if (!app() || !app()->mx()) return pathDiag_();
    ZiAssert(!rxInvoked_(), "Zquic", (),
      "QUIC path diagnostic snapshot from Rx thread", return {});
    if (txInvoked_()) return pathDiag_();
    PathDiag diag;
    ZmSemaphore done;
    auto link = const_cast<Link *>(this)->impl();
    app()->txRun([link, &diag, &done]() {
      diag = link->pathDiag_();
      done.post();
    });
    done.wait();
    return diag;
  }
  PathDiag pathDiag_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path diagnostic snapshot outside Tx thread", return {});
    return m_path.diag();
  }
  bool pathValidated_() const { return m_path.validated(); }
  unsigned activePathMaxUDP_() const { return m_path.activeMaxUDP(); }
  const ZiSockAddr &activePathRemote_() const { return m_path.remote(); }
  uint64_t pathAntiAmplification_() const {
    return m_path.antiAmplificationRemaining();
  }
  const Crypto &crypto_() const { return m_crypto; }
  void snapshotTxCrypto_(TxCryptoSnapshot &snapshot) const {
    for (unsigned i = 0; i < 3; ++i) {
      auto level = CryptoLevel::T(i);
      snapshot.installed[i] = m_crypto.txTrafficSecretInstalled(level);
      if (snapshot.installed[i])
	snapshot.secrets[i] = m_crypto.txTrafficSecret(level);
    }
  }
  bool txInstallTrafficSecret_(
    CryptoLevel::T level, const TrafficSecret &secret) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx traffic secret install outside Tx thread", return false);
    if (!secret.valid()) return false;
    m_txTrafficSecrets[level] = secret;
    if (!m_txProt[level].init(m_txTrafficSecrets[level], level, true)) {
      m_txTrafficSecrets[level].clear();
      return false;
    }
    return true;
  }
  bool installTxCrypto_(const TxCryptoSnapshot &snapshot) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx crypto snapshot install outside Tx thread", return false);
    for (unsigned i = 0; i < 3; ++i)
      if (snapshot.installed[i] &&
	  !txInstallTrafficSecret_(CryptoLevel::T(i), snapshot.secrets[i]))
	return false;
    return true;
  }
  bool txTrafficSecretInstalled_(CryptoLevel::T level) const {
    return m_txTrafficSecrets[level].valid();
  }
  const TrafficSecret &txTrafficSecret_(CryptoLevel::T level) const {
    return m_txTrafficSecrets[level];
  }
  PktProtState &txProtState_(CryptoLevel::T level) {
    return m_txProt[level];
  }

  bool recordTxUnackd_(
    CryptoLevel::T level, const SentFrameRef &ref, Stream *stream = nullptr) {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	if (stream) {
	  if (stream->txStillUnackd(ref.offset, ref.length, ref.fin))
	    return true;
	  return stream->recordTxUnackd(ref.offset, ref.length, ref.fin);
	}
	StreamRef stream_ = findStream(int64_t(ref.streamID));
	return stream_ &&
	  (stream_->txStillUnackd(ref.offset, ref.length, ref.fin) ||
	    stream_->recordTxUnackd(ref.offset, ref.length, ref.fin));
      }
      case SentFrameKind::Crypto:
	if (txRangeStillUnackd_(
	      m_txCryptoUnackd[level], ref.offset, ref.length))
	  return true;
	return m_txCryptoUnackd[level].add(
	    new CryptoTxPQueue::Node{
	      TxUnackdRange{ref.offset, ref.length}}) != ZmPQResult::Invalid;
      default:
	return true;
    }
  }
  void recordTxUnackd_(CryptoLevel::T level, const TxPktRefs &refs) {
    for (unsigned i = 0; i < refs.count(); ++i)
      (void)recordTxUnackd_(level, refs[i], refs.stream(i));
  }
  bool ackTxFrame_(
    CryptoLevel::T level, const SentFrameRef &ref, Stream *stream = nullptr) {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	if (stream) return stream->ackTxUnackd(ref.offset, ref.length, ref.fin);
	StreamRef stream_ = findStream(int64_t(ref.streamID));
	return stream_ && stream_->ackTxUnackd(ref.offset, ref.length, ref.fin);
      }
      case SentFrameKind::Crypto:
	return m_txCryptoUnackd[level].clear(ref.offset, ref.length);
      case SentFrameKind::Control: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream_;
	if (!stream) {
	  stream_ = findStream(int64_t(ref.streamID));
	  stream = stream_.ptr();
	}
	switch (ref.controlType) {
	  case FrameType::ResetStream: {
	    bool ackd = stream && stream->ackReset(ref.value, ref.length);
	    if (ackd) stream->clearControl(ref);
	    return ackd;
	  }
	  case FrameType::StopSending:
	    if (stream && stream->ackStop(ref.value)) {
	      stream->clearControl(ref);
	      return true;
	    }
	    return false;
	  default:
	    controlAckd_(ref);
	    return true;
	}
      }
      default:
	return true;
    }
  }
  bool txRangeStillUnackd_(
    const CryptoTxPQueue &queue, uint64_t offset, uint64_t length) const {
    if (!length) return false;
    bool found = false;
    queue.spans(offset, length, [&found](const auto &) {
      found = true;
      return false;
    });
    return found;
  }
  bool frameStillUnackd_(
    CryptoLevel::T level, const SentFrameRef &ref) const {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream = findStream(int64_t(ref.streamID));
	return stream &&
	  stream->txStillUnackd(ref.offset, ref.length, ref.fin);
      }
      case SentFrameKind::Crypto:
	return txRangeStillUnackd_(
	  m_txCryptoUnackd[level], ref.offset, ref.length);
      case SentFrameKind::Control:
	return controlStillValid_(controlFrame_(ref));
      default:
	return true;
    }
  }
  static bool clipStreamRef_(
    SentFrameRef &ref, uint64_t first, uint64_t end) {
    constexpr uint64_t maxTxRangeField = UINT32_MAX;
    uint64_t dataEnd = ref.offset + ref.length;
    if (dataEnd < ref.offset) return false;
    uint64_t oldOffset = ref.offset;
    uint64_t oldLength = ref.length;
    uint64_t oldRangeOffset = ref.range.offset;
    if (first > dataEnd) first = dataEnd;
    uint64_t dataFirst = first;
    uint64_t dataLimit = end < dataEnd ? end : dataEnd;
    uint64_t newLength = dataLimit > dataFirst ? dataLimit - dataFirst : 0;
    if (newLength > maxTxRangeField) return false;
    bool newFin = ref.fin && end > dataEnd;
    ref.offset = dataFirst;
    ref.length = newLength;
    ref.fin = newFin;
    ref.range.streamOffset = dataFirst;
    ref.range.length = uint32_t(newLength);
    if (dataFirst >= oldOffset && dataFirst <= oldOffset + oldLength) {
      uint64_t advance = dataFirst - oldOffset;
      if (oldRangeOffset + advance <= maxTxRangeField)
	ref.range.offset = uint32_t(oldRangeOffset + advance);
    }
    return newLength || newFin;
  }
  bool clipStreamRetransmit_(SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Stream ||
	ref.streamID > uint64_t(INT64_MAX))
      return false;
    StreamRef stream = findStream(int64_t(ref.streamID));
    if (!stream) return false;
    uint64_t n = ref.length + (ref.fin ? 1 : 0);
    if (!n || n < ref.length) return false;
    uint64_t first = 0;
    uint64_t end = 0;
    bool found = false;
    stream->txUnackdQueue()->spans(
      ref.offset, n, [&found, &first, &end](const auto &span) {
	first = span.key();
	end = span.key() + span.length();
	found = end >= first;
	return false;
      });
    if (!found) return false;
    uint64_t oldEnd = ref.offset + n;
    if (oldEnd < ref.offset) return false;
    if (end < oldEnd) {
      tail = ref;
      if (!clipStreamRef_(tail, end, oldEnd)) tail = {};
    }
    return clipStreamRef_(ref, first, end);
  }
  bool clipCryptoRetransmit_(
    CryptoLevel::T level, SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Crypto ||
	m_txSpaceDiscarded[level] || !ref.length)
      return false;
    uint64_t first = 0;
    uint64_t end = 0;
    bool found = false;
    m_txCryptoUnackd[level].spans(
      ref.offset, ref.length, [&found, &first, &end](const auto &span) {
	first = span.key();
	end = span.key() + span.length();
	found = end >= first;
	return false;
      });
    if (!found) return false;
    uint64_t oldEnd = ref.offset + ref.length;
    if (oldEnd < ref.offset || end <= first) return false;
    if (end < oldEnd) {
      tail = ref;
      tail.offset = end;
      tail.length = oldEnd - end;
    }
    ref.offset = first;
    ref.length = end - first;
    return true;
  }
  bool clipRetransmit_(
    CryptoLevel::T level, SentFrameRef &ref, SentFrameRef &tail) {
    switch (ref.kind) {
      case SentFrameKind::Stream:
	return clipStreamRetransmit_(ref, tail);
      case SentFrameKind::Crypto:
	return clipCryptoRetransmit_(level, ref, tail);
      case SentFrameKind::Control:
	tail = {};
	return controlStillValid_(controlFrame_(ref));
      default:
	tail = {};
	return true;
    }
  }

  void resetRuntimeDiag_() {
    m_rxDiag = {};
    m_txDiag = {};
  }
  RuntimeTxDiag txDiag_() const {
    RuntimeTxDiag diag = m_txDiag;
    for (unsigned i = 0; i < RuntimeTxDiag::Spaces; ++i) {
      const PktTxSpace &space = m_txPkts[i];
      diag.pktBytesInFlight[i] = space.bytesInFlight();
      diag.sentPackets[i] = space.count();
      diag.retransmitPending[i] = space.retransmitPending();
      diag.retransmittable[i] = space.retransmittable();
    }
    diag.ptoTimerActive = !!m_ptoTimer;
    diag.lossTimerActive = !!m_lossTimer;
    diag.ptoBackoff = m_ptoBackoff.count();
    diag.ptoTimeoutUS =
      uint64_t(m_ptoBackoff.timeout(m_rtt, maxAckDelay_()).microsecs());
    return diag;
  }
  void updateCongestionDiag_() {
    m_txDiag.congestionWindow = m_congestion.cwnd();
    m_txDiag.congestionSSThresh = m_congestion.ssthresh();
    m_txDiag.congestionBytesInFlight = m_congestion.bytesInFlight();
  }
  void noteRxECN_(CryptoLevel::T level, EcnMark::T ecn) {
    AckECN &diag = m_rxDiag.ecnRx[level];
    switch (ecn) {
      case EcnMark::ECT0: ++diag.ect0; break;
      case EcnMark::ECT1: ++diag.ect1; break;
      case EcnMark::CE: ++diag.ce; break;
    }
  }
  unsigned congestionAllowance_() const {
    uint64_t cwnd = m_congestion.cwnd();
    uint64_t inFlight = m_congestion.bytesInFlight();
    if (inFlight >= cwnd) return 0;
    uint64_t allowance = cwnd - inFlight;
    return allowance > uint64_t(unsigned(-1)) ?
      unsigned(-1) : unsigned(allowance);
  }
  bool ecnDisabled_() const { return m_path.ecnDisabled(); }
  void setEcnDisabled_(bool b = true) { m_path.setEcnDisabled(b); }
  PktBudget sendBudget_() const {
    PktBudget budget;
    unsigned maxUDP = m_path.activeMaxUDP();
    budget.pmtu = maxUDP;
    budget.antiAmplification = m_path.sendAllowance();
    unsigned allowance = congestionAllowance_();
    budget.congestion = allowance < maxUDP ? allowance : maxUDP;
    return budget;
  }
  static bool sameAddr_(const ZiSockAddr &l, const ZiSockAddr &r) {
    if (!l || !r) return !l && !r;
    return l.m_sin.sin_family == r.m_sin.sin_family &&
      l.m_sin.sin_port == r.m_sin.sin_port &&
      l.m_sin.sin_addr.s_addr == r.m_sin.sin_addr.s_addr;
  }
  void resetPath_() {
    m_path = m_isServer ?
      Path::server(ZiSockAddr{}, ZiSockAddr{}) :
      Path::client(ZiSockAddr{}, ZiSockAddr{});
    m_path.configuredMaxUDP(app()->maxUDP());
    m_path.peerMaxUDP(app()->maxUDP());
    if (!m_isServer) m_path.validated();
    m_validatingPath = {};
    m_pathChallengeControl = {};
  }
  void initClientPath_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = ZmMkRef(impl()),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      link->initClientPathTx_(ZuMv(local), ZuMv(remote));
    });
  }
  void initServerPath_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = ZmMkRef(impl()),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      link->initServerPathTx_(ZuMv(local), ZuMv(remote));
    });
  }
  void initClientPathTx_(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC client path initialization outside Tx thread", return);
    m_path = Path::client(ZuMv(local), ZuMv(remote));
    m_path.configuredMaxUDP(app()->maxUDP());
    m_path.peerMaxUDP(app()->maxUDP());
    m_path.validated();
  }
  void initServerPathTx_(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC server path initialization outside Tx thread", return);
    m_path = Path::server(ZuMv(local), ZuMv(remote));
    m_path.configuredMaxUDP(app()->maxUDP());
    m_path.peerMaxUDP(app()->maxUDP());
  }
  void updatePeerPathMaxUDP_() {
    if (!m_crypto.peerTransportParamsReceived()) return;
    uint64_t maxUDP = m_crypto.peerTransportParams().maxUDPPayloadSize;
    if (maxUDP > BufSize) maxUDP = BufSize;
    app()->txRun([link = ZmMkRef(impl()), maxUDP = unsigned(maxUDP)]() mutable {
      link->m_path.peerMaxUDP(maxUDP);
    });
  }
  void validatePath_() {
    app()->txRun([link = ZmMkRef(impl())]() mutable {
      link->validatePathTx_();
    });
  }
  void validatePathTx_() { m_path.validated(); }
  void recordPathRx_(unsigned bytes) {
    app()->txRun([link = ZmMkRef(impl()), bytes]() mutable {
      link->recordPathRxTx_(bytes);
    });
  }
  void recordPathRxTx_(unsigned bytes) { m_path.received(bytes); }
  void observePathRx_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = ZmMkRef(impl()),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      link->observePathRxTx_(ZuMv(local), ZuMv(remote));
    });
  }
  void observePathRxTx_(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path receive observation outside Tx thread", return);
    if (!remote || sameAddr_(remote, m_path.remote()))
      return;
    if (m_validatingPath.active &&
	sameAddr_(remote, m_validatingPath.path.remote()))
      return;
    startPathValidation_(ZuMv(local), ZuMv(remote));
  }
  bool startPathValidation_(
    ZiSockAddr local, ZiSockAddr remote, bool armTimer = true) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path validation start outside Tx thread", return false);
    if (!remote || sameAddr_(remote, m_path.remote())) return false;
    PathState state;
    state.prev = m_path;
    state.path = m_isServer ?
      Path::server(ZuMv(local), remote) :
      Path::client(ZuMv(local), remote);
    state.path.configuredMaxUDP(m_path.configuredMaxUDP());
    state.path.peerMaxUDP(m_path.peerMaxUDP());
    selectPathCID_(state);
    if (!state.challenge.generate())
      return false;
    state.deadline = pathValidationDeadline_();
    state.active = true;
    m_validatingPath = state;
    if (armTimer)
      schedulePathTimer_(state.deadline);
    txQueueControl_(ControlFrame::pathChallenge(
      m_validatingPath.challenge.cspan()));
    impl()->queueTxFlush_(m_validatingPath.path.remote());
    return true;
  }
  bool onPathResponse_(ZuCSpan data) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PATH_RESPONSE processing outside Tx thread", return false);
    if (!m_validatingPath.active ||
	!m_validatingPath.challenge.equals(data))
      return false;
    m_pathChallengeControl = {};
    promotePath_();
    return true;
  }
  void receivePathResponse_(ZuCSpan data) {
    if (data.length() != PathChallenge::Length) return;
    uint8_t payload[PathChallenge::Length];
    memcpy(payload, data.data(), sizeof(payload));
    app()->txRun([link = ZmMkRef(impl()), payload]() mutable {
      link->onPathResponse_(byteSpan(payload, sizeof(payload)));
    });
  }
  void promotePath_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path promotion outside Tx thread", return);
    if (!m_validatingPath.active) return;
    m_path = m_validatingPath.path;
    m_path.validated();
    bindPromotedCID_();
    m_validatingPath = {};
    cancelPathTimer_();
    impl()->pathPromoted_();
    const Path &path = m_path;
    impl()->pathUpdate(
      path.local(), path.remote(), path.validated(), path.activeMaxUDP());
    impl()->queueTxFlush_();
  }
  void failPathValidation_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path-validation failure outside Tx thread", return);
    if (!m_validatingPath.active) return;
    ZiSockAddr local = m_validatingPath.path.local();
    ZiSockAddr remote = m_validatingPath.path.remote();
    m_validatingPath = {};
    cancelPathTimer_();
    impl()->migrationFailure(local, remote);
    impl()->queueTxFlush_();
  }
  void pathExpired_() { failPathValidation_(); }
#ifdef Zquic_DEBUG
  void growActivePathForTest_(unsigned size) {
    m_path.startProbe(size);
    m_path.probeAckd();
  }
  bool startPMTUDProbeForTest_(unsigned size) {
    return m_path.startProbeChecked(size);
  }
  void ackPMTUDProbeForTest_(unsigned size) {
    onPMTUDProbeAckd_(size);
  }
  void losePMTUDProbeForTest_(unsigned size) {
    onPMTUDProbeLost_(size);
  }
  void expirePMTUDProbeForTest_() { pmtudExpired_(); }
  void applyPathHintForTest_(PathHint hint) { applyPathHint_(hint); }
  unsigned activePathMaxUDPForTest_() const { return m_path.activeMaxUDP(); }
  unsigned pathProbeSizeForTest_() const { return m_path.probeSize(); }
  bool pathProbeRetryPendingForTest_() const {
    return m_path.probeRetryPending();
  }
  const PathDiag &pathDiagForTest_() const { return m_path.diag(); }
  PktBudget sendBudgetForTest_() const { return sendBudget_(); }
  bool validatingPath_() const { return m_validatingPath.active; }
  bool startPathValidationForTest_(ZiSockAddr local, ZiSockAddr remote) {
    if (!remote || sameAddr_(remote, m_path.remote()))
      return false;
    if (m_validatingPath.active &&
	sameAddr_(remote, m_validatingPath.path.remote()))
      return false;
    return startPathValidation_(ZuMv(local), ZuMv(remote), false);
  }
  const ZiSockAddr &validatingRemote_() const {
    return m_validatingPath.path.remote();
  }
  ZuCSpan validatingChallenge_() const {
    return m_validatingPath.active ?
      m_validatingPath.challenge.cspan() : ZuCSpan{};
  }
  bool installOneRTTForTest_(
    const TrafficSecret &rx, const TrafficSecret &tx, const CxnID &localCID) {
    m_localSCID = localCID;
    m_established = 1;
    clearPeerKeyState_();
    return m_crypto.updateRxTrafficSecret(CryptoLevel::OneRTT, rx) &&
      txInstallTrafficSecret_(CryptoLevel::OneRTT, tx);
  }
  void discardPeerKeysForTest_() { discardOldPeerKeys_(); }
#endif
  template <typename SendPkt>
  bool sendPathPkt_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSend(bytes)) return false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) return false;
    return m_path.reserveSend(bytes);
  }
  template <typename SendPkt>
  bool sendPathProbePkt_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSendProbe(bytes)) return false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) return false;
    m_path.sent(bytes);
    return true;
  }
  template <typename SendPkt>
  bool sendPathPktApp_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSend(bytes)) return false;
    bool sent = false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr), sent)) return false;
    return !sent || m_path.reserveSend(bytes);
  }
  template <typename SendPkt>
  bool sendPathProbePktApp_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSendProbe(bytes)) return false;
    bool sent = false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr), sent)) return false;
    if (sent) m_path.sent(bytes);
    return true;
  }
  unsigned txPNLength_(CryptoLevel::T level) const {
    uint64_t largestAckd = m_txLargestAckd[level];
    if (ZuCmp<uint64_t>::null(largestAckd)) return RuntimePNLength;
    return PktNumber::encodedLength(m_txPN[level], largestAckd);
  }
#ifdef Zquic_DEBUG
  void setTxPNForTest_(CryptoLevel::T level, uint64_t pn) {
    m_txPN[level] = pn;
  }
#endif
  void resetRuntime_() {
    drainStreamsRx_();
    cancelTimers();
    resetLinkState_();
    m_established = 0;
    m_handshakeStarted = 0;
    m_initialDCID = {};
    m_localSCID = {};
    m_peerCID = {};
    m_peerResetToken = {};
    clearCIDState_();
    m_transportParams = {};
    m_txDataCredit = {};
    m_rxDataCredit = {};
    m_rxDataWindow = 0;
    m_lastDataBlocked = uint64_t(-1);
    m_lastStreamsBlockedBidi = uint64_t(-1);
    m_lastStreamsBlockedUni = uint64_t(-1);
    m_crypto.resetTLS();
    resetPath_();
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->resetTxRuntime_();
      return link;
    });
  }
  void resetTxRuntime_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx runtime reset outside Tx thread", return);
    ++m_txRuntimeGen;
    m_timerTeardown = false;
    clearPendingControls_();
    resetPktRuntime_();
    m_congestion = NewReno{app()->maxUDP()};
    updateCongestionDiag_();
  }

  void closeRuntime_(uint64_t errorCode = 0) {
    drainStreamsRx_();
    cancelTimers();
    closeLinkState_(errorCode);
    m_established = 0;
    m_crypto.resetTLS();
  }

  void resetTLS_() {
    m_crypto.resetTLS();
  }

  void shutdown_() {
    cancelTimers();
    closeLinkState_(0);
    m_established = 0;
    m_handshakeStarted = 0;
    m_crypto.resetTLS();
  }

  void endpointReady_() { ++m_rxDiag.endpointReady; }
  void endpointFailure_() { ++m_rxDiag.failures; }
  void packetParseFailure_() { ++m_rxDiag.failures; }
  void tlsFailure_() { ++m_rxDiag.failures; }
  void handshakeDoneTx_() { }
  void pathPromoted_() { }
  void dataBlocked_(uint64_t maximum) {
    impl()->flowBlocked(
      FrameType::DataBlocked, 0, Zi::StreamType::Duplex, maximum);
  }
  void streamDataBlocked_(uint64_t streamID, uint64_t maximum) {
    impl()->flowBlocked(
      FrameType::StreamDataBlocked, streamID,
      Zi::StreamType::Duplex, maximum);
  }
  void streamsBlocked_(Zi::StreamType::T type, uint64_t maximum) {
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
  }
  void streamOpen(StreamRef, bool) { ++m_rxDiag.unhandledAppEvents; }
  void streamData(
    StreamRef, uint64_t, ZuCSpan, bool) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void streamResetReceived(
    StreamRef, uint64_t, uint64_t) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void streamResetSent(uint64_t, uint64_t, uint64_t) {
    ++m_txDiag.unhandledAppEvents;
  }
  void streamStopSendingReceived(StreamRef, uint64_t) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void streamStopSendingSent(uint64_t, uint64_t) {
    ++m_txDiag.unhandledAppEvents;
  }
  void flowBlocked(
    FrameType::T, uint64_t, Zi::StreamType::T, uint64_t) {
    ++m_txDiag.unhandledAppEvents;
  }
  void transportClose(FrameType::T, uint64_t) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void pathUpdate(
    const ZiSockAddr &, const ZiSockAddr &, bool, unsigned) {
    ++m_txDiag.unhandledAppEvents;
  }
  void migrationFailure(const ZiSockAddr &, const ZiSockAddr &) {
    ++m_txDiag.unhandledAppEvents;
  }
  void streamFrame(
    uint64_t, uint64_t, ZuCSpan, bool) { }
  void retiredLocalCID_(uint64_t, const CxnID &) { }
  void statelessReset() { ++m_rxDiag.unhandledAppEvents; }
  void disconnected() { }

  void setRuntimeCIDs_(
    const CxnID &initialDCID,
    const CxnID &localSCID,
    const CxnID &peerCID) {
    m_initialDCID = initialDCID;
    m_localSCID = localSCID;
    m_peerCID = peerCID;
    addLocalCID_(localSCID, 0);
    addPeerCID_(peerCID, 0);
    if (auto cid = findCID_(m_peerCIDs, peerCID))
      cid->associated = true;
  }

  void setPeerCIDFromHdrSCID_(const LongHdr &h) {
    if (h.scid.length()) m_peerCID = h.scid;
  }

  void setPeerResetToken_(const ResetToken &token) {
    m_peerResetToken = token;
    if (m_peerCID.length())
      addPeerCID_(m_peerCID, 0, token);
  }

  bool addLocalCID_(
    const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken = {}) {
    return addCID_(m_localCIDs, id, sequence, resetToken, true);
  }
  bool addPeerCID_(
    const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken = {}) {
    return addCID_(m_peerCIDs, id, sequence, resetToken, false);
  }
  LinkCID *localCID_(uint64_t sequence) {
    return findCID_(m_localCIDs, sequence);
  }
  const LinkCID *localCID_(uint64_t sequence) const {
    return findCID_(m_localCIDs, sequence);
  }
  const LinkCID *peerCID_(uint64_t sequence) const {
    return findCID_(m_peerCIDs, sequence);
  }
  bool retireLocalCID_(uint64_t sequence, CxnID &id) {
    LinkCID *cid = localCID_(sequence);
    if (!cid || cid->state == CxnState::Tombstone) return false;
    id = cid->id;
    cid->state = CxnState::Retired;
    cid->associated = false;
    return true;
  }
  bool receiveNewConnectionID_(const Frame &frame) {
    if (frame.type != FrameType::NewConnectionID) return false;
    if (!frame.length || frame.length > CxnIDMax || !frame.resetToken.valid())
      return false;
    if (frame.offset > frame.value) return false;
    CxnID id{frame.payload};
    if (!id.length()) return false;
    if (frame.offset > m_peerRetirePriorTo)
      retirePeerCIDsPriorTo_(frame.offset);
    if (frame.value < m_peerRetirePriorTo)
      return true;
    return addPeerCID_(id, frame.value, frame.resetToken);
  }
  bool receiveRetireConnectionID_(const Frame &frame) {
    if (frame.type != FrameType::RetireConnectionID) return false;
    CxnID id;
    if (!retireLocalCID_(frame.value, id)) return false;
    impl()->retiredLocalCID_(frame.value, id);
    return true;
  }
  void selectPathCID_(PathState &state) {
    state.peerCID = m_peerCID;
    for (auto &cid : m_peerCIDs) {
      if (cid.state != CxnState::Active || cid.associated)
	continue;
      state.peerCID = cid.id;
      state.peerSeq = cid.sequence;
      return;
    }
    if (auto cid = findCID_(m_peerCIDs, m_peerCID))
      state.peerSeq = cid->sequence;
  }
  void bindPromotedCID_() {
    for (auto &cid : m_peerCIDs)
      cid.associated = false;
    if (!m_validatingPath.peerCID.length()) return;
    if (auto cid = findCID_(m_peerCIDs, m_validatingPath.peerSeq)) {
      if (cid->id == m_validatingPath.peerCID) {
	cid->associated = true;
	m_peerCID = cid->id;
      }
    }
  }
  ZuTime pathValidationDeadline_() const {
    ZuTime timeout = ptoTimeout_();
    int64_t usec = timeout.microsecs();
    if (usec < 1000000) usec = 1000000;
    if (usec > int64_t(-1) / 3) usec = int64_t(-1) / 3;
    return runtimeNow_() + timeUS(uint64_t(usec) * 3);
  }
  template <typename Routes>
  void installLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_localCIDs) {
      if (cid.state != CxnState::Active || cid.associated) continue;
      if (!routes.add(cid.id, cid.sequence, impl(), cid.resetToken)) continue;
      cid.associated = true;
    }
  }
  template <typename Routes>
  void retireLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_localCIDs) {
      if (cid.state == CxnState::Tombstone || !cid.associated) continue;
      routes.retire(cid.id);
      cid.associated = false;
      cid.state = CxnState::Retired;
    }
  }
  template <typename Routes>
  void tombstoneLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_localCIDs) {
      if (cid.state == CxnState::Tombstone || !cid.associated) continue;
      routes.tombstone(cid.id);
      cid.associated = false;
      cid.state = CxnState::Tombstone;
    }
  }

  void clearCIDState_() {
    m_localCIDs.clear();
    m_peerCIDs.clear();
    m_peerRetirePriorTo = 0;
  }
  template <typename CIDs>
  static LinkCID *findCID_(CIDs &cids, uint64_t seq) {
    for (auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.sequence == seq)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  static const LinkCID *findCID_(const CIDs &cids, uint64_t seq) {
    for (const auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.sequence == seq)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  static LinkCID *findCID_(CIDs &cids, const CxnID &id) {
    for (auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.id == id)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  bool addCID_(
    CIDs &cids, const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken, bool local) {
    if (!id.length()) return false;
    if (!local && !peerCIDTokenUnique_(cids, id, resetToken)) return false;
    if (auto cid = findCID_(cids, sequence)) {
      if (!(cid->id == id)) return false;
      if (resetToken.valid() && cid->resetToken.valid() &&
	  !(cid->resetToken == resetToken))
	return false;
      cid->resetToken = resetToken;
      cid->state = CxnState::Active;
      return true;
    }
    if (auto cid = findCID_(cids, id)) {
      if (cid->sequence != sequence) return false;
      if (resetToken.valid() && cid->resetToken.valid() &&
	  !(cid->resetToken == resetToken))
	return false;
      cid->resetToken = resetToken;
      cid->state = CxnState::Active;
      return true;
    }
    unsigned active = 0;
    LinkCID *slot = nullptr;
    for (auto &cid : cids) {
      if (cid.state == CxnState::Active) ++active;
      if (!slot && cid.state != CxnState::Active) slot = &cid;
    }
    uint64_t limit = local ?
      LocalActiveConnectionIDLimit :
      m_transportParams.activeConnectionIDLimit;
    if (active >= limit)
      return false;
    if (!slot && cids.length() < limit)
      slot = cids.push();
    if (!slot) return false;
    *slot = LinkCID{id, sequence, resetToken, CxnState::Active, false};
    return true;
  }
  template <typename CIDs>
  bool peerCIDTokenUnique_(
    const CIDs &cids, const CxnID &id, const ResetToken &resetToken) const {
    if (!resetToken.valid()) return true;
    for (const auto &cid : cids) {
      if (cid.state == CxnState::Tombstone ||
	  !cid.resetToken.valid() || !(cid.resetToken == resetToken))
	continue;
      if (!(cid.id == id)) return false;
    }
    return true;
  }
  void retirePeerCIDsPriorTo_(uint64_t sequence) {
    m_peerRetirePriorTo = sequence;
    for (auto &cid : m_peerCIDs)
      if (cid.state == CxnState::Active && cid.sequence < sequence) {
	cid.state = CxnState::Retired;
      }
  }

  const CxnID &runtimeCID_(RuntimeCID::T cid) const {
    switch (cid) {
      case RuntimeCID::Initial: return m_initialDCID;
      case RuntimeCID::Local: return m_localSCID;
      default: return m_peerCID;
    }
  }

  template <typename AppLike>
  void configureLocalTransportParams_(AppLike *app) {
    m_transportParams.initialSCID = m_localSCID;
    m_transportParams.maxUDPPayloadSize = app->maxUDP();
    m_transportParams.initialMaxData = app->maxData();
    m_transportParams.initialMaxStreamDataBidiLocal =
      app->maxStreamData();
    m_transportParams.initialMaxStreamDataBidiRemote =
      app->maxStreamData();
    m_transportParams.initialMaxStreamDataUni = app->maxStreamData();
    m_transportParams.initialMaxStreamsBidi = app->maxStreamsBidi();
    m_transportParams.initialMaxStreamsUni = app->maxStreamsUni();
    m_transportParams.activeConnectionIDLimit = LocalActiveConnectionIDLimit;
    m_rxDataCredit.set(m_transportParams.initialMaxData);
    m_rxDataWindow = m_transportParams.initialMaxData;
  }

  bool loadServerTransportParams_(const ServerBootstrap &bootstrap) {
    if (bootstrap.transportParams(m_transportParams)) return true;
    tlsFailure_();
    return false;
  }

  bool deriveInitial_() {
    if (m_crypto.deriveInitial(m_initialDCID)) return true;
    tlsFailure_();
    return false;
  }

  bool initTLS_(CryptoConfig config) {
    config.localTransportParams = &m_transportParams;
    if (m_crypto.initTLS(config)) return true;
    tlsFailure_();
    return false;
  }

  bool startRuntimeHandshake_() {
    if (m_handshakeStarted) return false;
    m_handshakeStarted = 1;
    startHandshakeState_();
    return true;
  }

  bool runtimeReadyToEstablish_() const {
    return !m_established &&
      m_crypto.oneRTTReady() &&
      m_crypto.txTrafficSecretInstalled(CryptoLevel::OneRTT) &&
      m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT);
  }

  bool validateServerTransportParams_(const ClientBootstrap &bootstrap) {
    if (!m_crypto.peerTransportParamsReceived() ||
	!bootstrap.validateServerTransportParams(
	  m_crypto.peerTransportParams(), m_peerCID))
      return false;
    const auto &params = m_crypto.peerTransportParams();
    if (params.statelessResetTokenPresent)
      m_peerResetToken = params.statelessResetToken;
    return true;
  }

  bool validateClientTransportParams_() const {
    return m_crypto.peerTransportParamsReceived() &&
      !m_crypto.peerTransportParams().statelessResetTokenPresent;
  }

  void establishRuntime_() {
    if (m_crypto.peerTransportParamsReceived()) {
      const auto &params = m_crypto.peerTransportParams();
      m_txDataCredit.set(params.initialMaxData);
      m_peerBidiLimit.set(params.initialMaxStreamsBidi);
      m_peerUniLimit.set(params.initialMaxStreamsUni);
    }
    updatePeerPathMaxUDP_();
    m_established = 1;
    discardPktSpace_(CryptoLevel::Initial);
    discardPktSpace_(CryptoLevel::Handshake);
    establishState_();
    ++m_rxDiag.handshakeComplete;
  }

  auto negotiatedProtocol_() const {
    return m_crypto.negotiatedProtocol();
  }

  void resetPktRuntime_() {
    cancelTimers();
    for (auto &s : m_txCrypto) s.reset();
    for (auto &s : m_rxCrypto) s.reset();
    for (auto &s : m_txTrafficSecrets) s.clear();
    for (auto &p : m_txProt) p.clear();
    memset(m_txPN, 0, sizeof(m_txPN));
    for (auto &pn : m_txLargestAckd) pn = uint64_t(-1);
    memset(m_rxLargestPN, 0, sizeof(m_rxLargestPN));
    m_rxAcks.clear();
    clearPeerKeyState_();
    for (auto &ack : m_txAck) ack = {};
    for (auto &ecn : m_peerAckECN) ecn.reset();
    for (auto &p : m_txPkts) p.clear();
    m_coalesceInitial = nullptr;
    m_coalesceAddr = {};
    m_coalesceLong = false;
    memset(m_rxSpaceDiscarded, 0, sizeof(m_rxSpaceDiscarded));
    memset(m_txSpaceDiscarded, 0, sizeof(m_txSpaceDiscarded));
    m_rtt = {};
    m_ptoBackoff.reset();
    m_txKeyPhase = false;
  }

  void drainStreamsRx_() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream table Rx drain outside Rx thread", return);
    auto i = m_streams->iter();
    while (auto stream = i()) stream->drainRx_();
  }

  void clearPeerKeyState_() {
    m_rxOldTrafficSecret.clear();
    m_rxOldProt.clear();
    m_rxNextTrafficSecret.clear();
    m_rxNextProt.clear();
    m_rxOldKeyDiscard = {};
    m_rxKeyPhase = false;
    m_rxOldKeyPhase = false;
  }
  bool ensureNextPeerKey_() {
    if (m_rxNextProt.valid()) return true;
    if (!PktProt::deriveNextTrafficSecret(
	  m_rxNextTrafficSecret,
	  m_crypto.rxTrafficSecret(CryptoLevel::OneRTT)))
      return false;
    if (!m_rxNextProt.init(
	  m_rxNextTrafficSecret, CryptoLevel::OneRTT, false)) {
      m_rxNextTrafficSecret.clear();
      return false;
    }
    return true;
  }
  bool commitPeerKeyUpdate_(const TrafficSecret &nextRxSecret) {
    m_rxOldTrafficSecret = m_crypto.rxTrafficSecret(CryptoLevel::OneRTT);
    if (!m_rxOldProt.init(
	  m_rxOldTrafficSecret, CryptoLevel::OneRTT, false)) {
      m_rxOldTrafficSecret.clear();
      return false;
    }
    m_rxOldKeyPhase = m_rxKeyPhase;
    if (!m_crypto.updateRxTrafficSecret(CryptoLevel::OneRTT, nextRxSecret))
      return false;
    m_rxKeyPhase = !m_rxKeyPhase;
    m_rxNextTrafficSecret.clear();
    m_rxNextProt.clear();
    m_rxOldKeyDiscard = keyDiscardDeadline_();
    ++m_rxDiag.peerKeyUpdates;
    schedulePeerKeyDiscard_(m_rxOldKeyDiscard);
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->txInstallPeerKeyUpdate_();
      return link;
    });
    return true;
  }
  void schedulePeerKeyDiscard_(ZuTime deadline) {
    app()->txRun([link = ZmMkRef(impl()), deadline]() mutable {
      link->scheduleKeyDiscardTimer_(deadline);
    });
  }
  void discardOldPeerKeys_() {
    if (!m_rxOldProt.valid()) return;
    m_rxOldTrafficSecret.clear();
    m_rxOldProt.clear();
    m_rxOldKeyDiscard = {};
    ++m_rxDiag.keyDiscards;
  }
  bool txInstallPeerKeyUpdate_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer key update outside Tx thread", return false);
    TrafficSecret nextTxSecret;
    if (!PktProt::deriveNextTrafficSecret(
	  nextTxSecret, txTrafficSecret_(CryptoLevel::OneRTT)) ||
	!txInstallTrafficSecret_(CryptoLevel::OneRTT, nextTxSecret))
      return false;
    m_txKeyPhase = !m_txKeyPhase;
    return true;
  }

  bool checkStatelessReset_(ZuCSpan datagram) {
    if (!StatelessReset::verify(datagram, m_peerResetToken)) return false;
    m_runtimeCloseState = CloseState::Draining;
    m_linkState = LinkState::Draining;
    m_established = 0;
    m_handshakeStarted = 0;
    m_streamQueue.clean();
    for (auto &p : m_txPkts) p.clear();
    m_rxAcks.clear();
    ++m_rxDiag.failures;
    impl()->statelessReset();
    return true;
  }

  template <
    unsigned TLSBufSize_, typename SendFlights,
    typename MarkEstablished, typename AfterEstablished>
  bool advanceTLS_(
    size_t inEpoch, ZuCSpan input, ZiSockAddr addr,
    SendFlights sendFlights, MarkEstablished markEstablished,
    AfterEstablished afterEstablished) {
    ZmRef<ZiIOBuf> out =
      new CryptoTxBufAlloc<TLSBufSize_, TLSBufSize_>{this};
    size_t offsets[5] = {};
    int n =
      m_crypto.handleTLSMessage(out.ptr(), offsets, inEpoch, input);
    if (n < 0) {
      tlsFailure_();
      return false;
    }
    if (!sendFlights(out->data(), unsigned(n), offsets, addr))
      return false;
    markEstablished();
    return afterEstablished(ZuMv(addr));
  }

  StreamRef nextWritableStream_() {
    StreamRef stream = m_streamQueue.head();
    if (!stream) return nullptr;
    m_streamQueue.shift();
    stream->txQueued(false);
    return stream;
  }

  bool streamTxPending_(const StreamRef &stream) const {
    return stream &&
      (stream->controlQueued() ||
	(!stream->resetSent() && (stream->txRangeCount() || stream->finReady())));
  }

  SentFrameRef controlRef_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	return SentFrameRef::flowUpdate(
	  FlowUpdate{frame.type, frame.streamID, frame.value, frame.streamType});
      case FrameType::DataBlocked:
      case FrameType::StreamDataBlocked:
      case FrameType::StreamsBlocked:
	return SentFrameRef::blocked(
	  frame.type, frame.streamID, frame.value, frame.streamType);
      case FrameType::PathChallenge:
	return SentFrameRef::pathChallenge(
	  byteSpan(frame.payload, sizeof(frame.payload)));
      case FrameType::PathResponse:
	return SentFrameRef::pathResponse(
	  byteSpan(frame.payload, sizeof(frame.payload)));
      case FrameType::HandshakeDone:
	return SentFrameRef::handshakeDone();
      case FrameType::ResetStream:
	return SentFrameRef::resetStream(
	  frame.streamID, frame.errorCode, frame.value);
      case FrameType::StopSending:
	return SentFrameRef::stopSending(frame.streamID, frame.errorCode);
      default:
	return SentFrameRef::control();
    }
  }

  ControlFrame controlFrame_(const SentFrameRef &ref) const {
    ControlFrame frame;
    frame.type = ref.controlType;
    frame.streamID = ref.streamID;
    frame.value = ref.value;
    if (ref.controlType == FrameType::ResetStream) {
      frame.errorCode = ref.value;
      frame.value = ref.length;
    } else if (ref.controlType == FrameType::StopSending) {
      frame.errorCode = ref.value;
      frame.value = 0;
    }
    frame.streamType = ref.streamType;
    if (ref.controlType == FrameType::PathChallenge ||
	ref.controlType == FrameType::PathResponse)
      memcpy(frame.payload, ref.payload, sizeof(frame.payload));
    return frame;
  }

  bool appendControl_(
    const ControlFrame &frame, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    ControlFrame *sentControls, unsigned &nSentControls) {
    if (!controlStillValid_(frame)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    if (n <= 0 || !assembly.addControl(budget, unsigned(n)) ||
	!build.commitScratch(unsigned(n)))
      return false;
    if (!refs.add(controlRef_(frame))) return false;
    sentControls[nSentControls++] = frame;
    return true;
  }

  bool appendQueuedControl_(
    PendingControl &slot, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    ControlFrame *sentControls, unsigned &nSentControls) {
    if (!slot.queued) return true;
    if (!controlStillValid_(slot.frame)) {
      slot = {};
      return true;
    }
    return appendControl_(
      slot.frame, build, budget, assembly, refs,
      sentControls, nSentControls);
  }

  bool appendQueuedControls_(
    PktBuild &build, PktBudget &budget, PktAssembly &assembly,
    TxPktRefs &refs, ControlFrame *sentControls,
    unsigned &nSentControls) {
    if (refs.count() >= SentPkt::MaxFrames) return true;
    if (!appendQueuedControl_(
	  m_maxDataControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    for (unsigned i = 0; i < 2 && refs.count() < SentPkt::MaxFrames; ++i)
      if (!appendQueuedControl_(
	    m_maxStreamsControl[i], build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedControl_(
	  m_dataBlockedControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    for (unsigned i = 0; i < 2 && refs.count() < SentPkt::MaxFrames; ++i)
      if (!appendQueuedControl_(
	    m_streamsBlockedControl[i], build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedControl_(
	  m_pathChallengeControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedControl_(
	  m_handshakeDoneControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    auto iter = m_pathResponses.iter();
    while (refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame = iter();
      if (!frame) break;
      if (!appendControl_(
	    frame, build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
    }
    return true;
  }

  bool appendStreamControls_(
    const StreamRef &stream, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    ControlFrame *sentControls, unsigned &nSentControls) {
    while (stream && refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame;
      if (!stream->nextQueuedControl(frame)) return true;
      if (!stream->controlStillValid(frame)) {
	stream->controlSent(frame);
	continue;
      }
      if (!appendControl_(
	    frame, build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
      stream->controlSent(frame);
      return true;
    }
    return true;
  }

  void controlSent_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::MaxData:
	m_maxDataControl.queued = false;
	break;
      case FrameType::MaxStreams:
	m_maxStreamsControl[streamTypeIndex_(frame.streamType)].queued = false;
	break;
      case FrameType::DataBlocked:
	m_dataBlockedControl.queued = false;
	break;
      case FrameType::StreamsBlocked:
	m_streamsBlockedControl[streamTypeIndex_(frame.streamType)].queued = false;
	break;
      case FrameType::PathChallenge:
	m_pathChallengeControl.queued = false;
	break;
      case FrameType::PathResponse:
	m_pathResponses.shift();
	break;
      case FrameType::HandshakeDone:
	m_handshakeDoneControl.queued = false;
	break;
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending:
	if (frame.streamID <= uint64_t(INT64_MAX))
	  if (StreamRef stream = findStream(int64_t(frame.streamID)))
	    stream->controlSent(frame);
	break;
      default:
	break;
    }
    noteControlDequeued_(frame);
  }

  void clearPendingControl_(PendingControl &slot, const ControlFrame &frame) {
    if (slot.frame == frame) slot = {};
  }
  void controlAckd_(const SentFrameRef &ref) {
    ControlFrame frame = controlFrame_(ref);
    switch (ref.controlType) {
      case FrameType::MaxData:
	clearPendingControl_(m_maxDataControl, frame);
	break;
      case FrameType::MaxStreams:
	clearPendingControl_(
	  m_maxStreamsControl[streamTypeIndex_(ref.streamType)], frame);
	break;
      case FrameType::DataBlocked:
	clearPendingControl_(m_dataBlockedControl, frame);
	break;
      case FrameType::StreamsBlocked:
	clearPendingControl_(
	  m_streamsBlockedControl[streamTypeIndex_(ref.streamType)], frame);
	break;
      case FrameType::PathChallenge:
	clearPendingControl_(m_pathChallengeControl, frame);
	break;
      case FrameType::HandshakeDone:
	clearPendingControl_(m_handshakeDoneControl, frame);
	break;
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending:
	if (ref.streamID <= uint64_t(INT64_MAX))
	  if (StreamRef stream = findStream(int64_t(ref.streamID)))
	    stream->clearControl(ref);
	break;
      default:
	break;
    }
  }

  template <typename AppendAck, typename SendPkt>
  bool sendQueuedControlPkt_(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control packetization outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    PktAssembly assembly;
    TxPktRefs refs;
    unsigned before = build.bytes();
    if (!appendAck(build)) return false;
    unsigned ackBytes = build.bytes() - before;
    if (ackBytes && !budget.add(ackBytes)) return false;
    ControlFrame sentControls[SentPkt::MaxFrames];
    unsigned nSentControls = 0;
    if (!appendQueuedControls_(
	  build, budget, assembly, refs, sentControls, nSentControls))
      return false;
    if (refs.count()) {
      if (!sendPkt(build, ZuMv(addr), refs)) return false;
      for (unsigned i = 0; i < nSentControls; ++i)
	controlSent_(sentControls[i]);
      return true;
    }
    return false;
  }

  template <typename AppendAck, typename SendPkt>
  bool flushControlAndStreams_(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control/stream flush outside Tx thread", return false);
    return flushControlAndStreamsTx_(
      ZuMv(addr),
      [&appendAck](PktBuild &build) { return appendAck(build); },
      [&sendPkt](
	  PktBuild &build, ZiSockAddr addr_,
	  const TxPktRefs &refs) {
	return sendPkt(build, ZuMv(addr_), refs);
      });
  }

  template <typename AppendAck, typename SendPkt>
  bool flushControlAndStreamsTx_(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control/stream flush outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_txDataCredit.available();
    PktAssembly assembly;
    TxPktRefs refs;
    unsigned before = build.bytes();
    if (!appendAck(build)) return false;
    unsigned ackBytes = build.bytes() - before;
    if (ackBytes && !budget.add(ackBytes)) return false;
    ControlFrame sentControls[SentPkt::MaxFrames];
    unsigned nSentControls = 0;
    if (!appendQueuedControls_(
	  build, budget, assembly, refs, sentControls, nSentControls))
      return false;
    bool blocked = false;
    while (scheduledStreamCount_() && refs.count() < SentPkt::MaxFrames) {
      StreamRef stream = nextWritableStream_();
      if (!stream || !streamTxPending_(stream)) continue;
      if (stream->controlQueued()) {
	if (!appendStreamControls_(
	      stream, build, budget, assembly, refs,
	      sentControls, nSentControls))
	  return false;
	if (streamTxPending_(stream) && stream->id() >= 0)
	  streamWritable_(stream);
	continue;
      }
      if (stream->txRangeCount() && m_txDataCredit.blocked()) {
	streamWritable_(stream);
	queueBlocked_(FrameType::DataBlocked, 0, m_txDataCredit.limit());
	blocked = true;
	break;
      }
      if (stream->txRangeCount() && !stream->txCreditAvailable()) {
	streamWritable_(stream);
	queueBlocked_(
	  FrameType::StreamDataBlocked, uint64_t(stream->id()),
	  stream->txCreditLimit());
	blocked = true;
	break;
      }
      StreamFrameInfo info;
      int n = StreamPktizer::writeNext(
	build.scratch(), build.scratchAvail(),
	budget, assembly, *stream, &info);
      if (n <= 0) {
	if (stream->id() >= 0) streamWritable_(stream);
	break;
      }
      if (!build.commitScratch(unsigned(n)) || !build.add(info.range))
	return false;
      SentFrameRef ref =
	SentFrameRef::stream(info.streamID, info.range, info.fin);
      if (!info.length) {
	ref.offset = info.offset;
	ref.length = 0;
      }
      if (!refs.add(ref, stream.ptr()))
	return false;
      if (info.length && !m_txDataCredit.consume(info.length)) return false;
      m_txDiag.streamBytesTx += info.length;
      returnStreamCredit_(stream);
      if (streamTxPending_(stream) && stream->id() >= 0)
	streamWritable_(stream);
    }
    if (refs.count()) {
      if (!sendPkt(build, ZuMv(addr), refs)) return false;
      for (unsigned i = 0; i < nSentControls; ++i)
	controlSent_(sentControls[i]);
      return true;
    }
    return blocked;
  }

  template <typename AppendAck, typename SendPkt>
  bool sendQueuedStreamPkt_(
    StreamRef stream, ZiSockAddr addr, AppendAck appendAck,
    SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream packetization outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_txDataCredit.available();
    PktAssembly assembly;
    if (stream->txRangeCount() && m_txDataCredit.blocked()) {
      streamWritable_(stream);
      queueBlocked_(FrameType::DataBlocked, 0, m_txDataCredit.limit());
      return false;
    }
    if (stream->txRangeCount() && !stream->txCreditAvailable()) {
      streamWritable_(stream);
      uint64_t limit = stream->txCreditLimit();
      queueBlocked_(FrameType::StreamDataBlocked, uint64_t(stream->id()), limit);
      return false;
    }
    unsigned before = build.bytes();
    if (!appendAck(build)) return false;
    unsigned controlBytes = build.bytes() - before;
    if (controlBytes && !assembly.addControl(budget, controlBytes))
      return false;
    StreamFrameInfo info;
    int n = StreamPktizer::writeNext(
      build.scratch(), build.scratchAvail(),
      budget, assembly, *stream, &info);
    if (n <= 0 || !build.commitScratch(unsigned(n)))
      return false;
    if (!build.add(info.range)) return false;
    SentFrameRef ref = SentFrameRef::stream(info.streamID, info.range, info.fin);
    if (!info.length) {
      ref.offset = info.offset;
      ref.length = 0;
    }
    TxPktRefs refs;
    if (!refs.add(ref, stream.ptr())) return false;
    if (!sendPkt(build, ZuMv(addr), refs)) return false;
    if (info.length && !m_txDataCredit.consume(info.length)) return false;
    m_txDiag.streamBytesTx += info.length;
    return true;
  }

  static PktSpace::T pktSpace_(CryptoLevel::T level) {
    return PktSpace::T(level);
  }

  uint64_t localMaxAckDelayUS_() const {
    return uint64_t(m_transportParams.maxAckDelay) * 1000;
  }

  uint64_t nowUS_() const {
    return uint64_t(runtimeNow_().microsecs());
  }

  bool immediateAck_(CryptoLevel::T level) const {
    if (level != CryptoLevel::OneRTT) return true;
    const AckTracker &tracker = m_rxAcks.tracker(pktSpace_(level));
    return tracker.multipleRanges();
  }

  void postAckSnapshot_(CryptoLevel::T level, ZiSockAddr addr) {
    PktSpace::T space = pktSpace_(level);
    if (!m_rxAcks.pending(space)) return;
    AckSnapshot ack;
    ack.level = level;
    ack.gen = m_rxAcks.gen(space);
    ack.largestRxTime = m_rxAcks.largestRxTime(space);
    ack.due = m_rxAcks.immediate(space);
    ack.ecn = m_rxAcks.ackECN(space);
    const AckTracker &tracker = m_rxAcks.tracker(space);
    int nRanges = tracker.snapshot(ack.ranges, Frame::MaxAckRanges);
    if (nRanges < 0) return;
    ack.nRanges = unsigned(nRanges);
    bool deadline = m_rxAcks.deadlineSet(space);
    uint64_t deadlineUS = deadline ? m_rxAcks.deadline(space) : 0;
    app()->txRun([
      link = ZmMkRef(impl()), ack, addr = ZuMv(addr), deadline, deadlineUS
    ]() mutable {
      if (link->noteAckTx_(ack)) {
	link->cancelAckDelayTimer_();
	link->flushTx_(ZuMv(addr));
      } else if (deadline) {
	link->scheduleAckDelayTimer_(timeUS(deadlineUS));
      }
    });
  }

  void noteAck_(
    CryptoLevel::T level, uint64_t pn, bool ackEliciting, ZiSockAddr addr,
    bool forceImmediate = false) {
    PktSpace::T space = pktSpace_(level);
    uint64_t now = nowUS_();
    bool immediate = ackEliciting && (forceImmediate || immediateAck_(level));
    if (ackEliciting)
      m_rxAcks.ackEliciting(
	space, pn, now, localMaxAckDelayUS_(), immediate);
    postAckSnapshot_(level, ZuMv(addr));
  }

  bool noteAckTx_(const AckSnapshot &ack) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK snapshot install outside Tx thread", return false);
    bool due = m_txAck[ack.level].due || ack.due;
    m_txAck[ack.level] = ack;
    m_txAck[ack.level].due = due;
    return due;
  }

  bool appendPendingAck_(
    CryptoLevel::T level, PktBuild &build, bool ackOnly = false) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK append outside Tx thread", return false);
    AckSnapshot &ack = m_txAck[level];
    if (!ack.nRanges) return true;
    if (ackOnly && !ack.due) return true;
    uint64_t delay = 0;
    if (level == CryptoLevel::OneRTT) {
      uint64_t now = nowUS_();
      if (now > ack.largestRxTime)
	delay = (now - ack.largestRxTime) >>
	  m_transportParams.ackDelayExponent;
    }
    int n = !m_path.ecnDisabled() && ack.ecn.any() ?
      FrameCodec::writeAckECN(
	build.scratch(), build.scratchAvail(), ack.ranges, ack.nRanges,
	delay, ack.ecn) :
      FrameCodec::writeAckRanges(
	build.scratch(), build.scratchAvail(), ack.ranges, ack.nRanges,
	delay);
    if (n < 0) return false;
    if (!build.commitScratch(unsigned(n))) return false;
    build.markAck(unsigned(level));
    return true;
  }

  void ackSentTx_(CryptoLevel::T level) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK commit outside Tx thread", return);
    AckSnapshot ack = m_txAck[level];
    if (!ack.nRanges) return;
    m_txAck[level].nRanges = 0;
    m_txAck[level].due = false;
    app()->rxRun([link = ZmMkRef(impl()), level, gen = ack.gen]() mutable {
      link->ackSentRx_(level, gen);
    });
  }

  void ackSentRx_(CryptoLevel::T level, uint64_t gen) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK receive-state commit outside Rx thread", return);
    m_rxAcks.sent(pktSpace_(level), gen);
  }

  void scheduleAckDelayTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "ACK delay", out, ZmScheduler::Advance, &m_ackDelayTimer,
      [](auto link) { link->ackDelay_(); });
  }
  void cancelAckDelayTimer_() { cancelTimer_("ACK delay", &m_ackDelayTimer); }

  void scheduleLossTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "loss time", out, ZmScheduler::Update, &m_lossTimer,
      [](auto link) { link->lossTime_(); });
  }
  ZuTime lossThreshold_() const { return m_rtt.timeThreshold(); }
  ZuTime persistentCongestionThreshold_() const {
    return ptoTimeout_() * ZuDecimal{3};
  }
  ZuTime nextLossTime_() const {
    ZuTime threshold = lossThreshold_();
    ZuTime out;
    bool have = false;
    for (unsigned i = 0; i < 3; ++i) {
      auto level = CryptoLevel::T(i);
      if (m_txSpaceDiscarded[level]) continue;
      ZuTime deadline =
	m_txPkts[level].nextLossTime(
	  m_txLargestAckd[level], threshold, RecoveryScanBatch);
      if (!*deadline) continue;
      if (!have || deadline < out) {
	out = deadline;
	have = true;
      }
    }
    return out;
  }
  bool detectLoss_(
    CryptoLevel::T level, ZuTime now, PktLossBatch &batch, bool &complete) {
    complete = true;
    if (m_txSpaceDiscarded[level]) return false;
    ZuTime threshold = lossThreshold_();
    if (!*threshold) return false;
    PktTxUpdate update;
    complete = m_txPkts[level].markTimeThresholdLossBatch(
      m_txLargestAckd[level], now, threshold, batch,
      RecoveryScanBatch, &update);
    if (!update.lostBytes)
      return false;
    applyLossUpdateTx_(update);
    if (complete && m_txPkts[level].persistentCongestion(
	persistentCongestionThreshold_(), RecoveryScanBatch))
      persistentCongestion_();
    updateCongestionDiag_();
    return true;
  }
  void detectLossTx_(
    unsigned level, ZuTime now, PktLossBatch batch, bool lost) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss scan outside Tx thread", return);
    while (level < 3) {
      CryptoLevel::T cryptoLevel = CryptoLevel::T(level);
      bool complete = true;
      lost |= detectLoss_(cryptoLevel, now, batch, complete);
      if (!complete) {
	app()->txRun([
	  link = ZmMkRef(impl()), level, now, batch, lost
	]() mutable {
	  link->detectLossTx_(level, now, batch, lost);
	});
	return;
      }
      ++level;
      batch = {};
    }
    if (lost) {
      impl()->queueRetransmit_();
      schedulePTO_();
    }
    scheduleLossTimer_();
  }
  void scheduleLossTimer_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer schedule outside Tx thread", return);
    if (closed()) return;
    ZuTime out = nextLossTime_();
    if (!*out) {
      cancelLossTimer_();
      return;
    }
    ZuTime now = runtimeNow_();
    if (out <= now) out = now + RttEstimator::Granularity;
    scheduleLossTimer_(out);
  }
  void cancelLossTimer_() { cancelTimer_("loss time", &m_lossTimer); }

  void schedulePTO() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO schedule before app initialization", return);
    if (closed()) return;
    app()->txInvoke([link = ZmMkRef(impl())]() mutable {
      link->schedulePTO_();
    });
  }

  void schedulePMTUD() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PMTUD schedule before app initialization", return);
    if (closed()) return;
    app()->txInvoke([link = ZmMkRef(impl())]() mutable {
      link->schedulePMTUD_();
    });
  }

  void schedulePTO_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PTO schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO schedule before app initialization", return);
    if (closed()) return;
    CryptoLevel::T level = CryptoLevel::Initial;
    if (!ptoLevel_(level)) return;
    ZuTime out = ptoDeadline_(level);
    Zquic_DEBUG_LOG_(([level, bif = m_txPkts[level].bytesInFlight()](auto &s) {
	s << "PTO armed level=" << int(level) << " bytesInFlight=" << bif;
      }));
    schedulePTOTimer_(out);
  }

  void schedulePTOTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "PTO", out, ZmScheduler::Advance, &m_ptoTimer,
      [](auto link) { link->pto_(); });
  }

  void cancelPTO() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO cancel before app initialization", return);
    cancelPTO_();
  }

  void cancelPTO_() {
    if (app() && app()->mx()) app()->mx()->del(&m_ptoTimer);
  }

  void scheduleIdleTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "idle", out, ZmScheduler::Update, &m_idleTimer,
      [](auto link) { link->idleTimeout_(); });
  }
  void cancelIdleTimer_() { cancelTimer_("idle", &m_idleTimer); }

  void scheduleCloseTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "close", out, ZmScheduler::Update, &m_closeTimer,
      [](auto link) { link->closeTimeout_(); });
  }
  void cancelCloseTimer_() { cancelTimer_("close", &m_closeTimer); }

  void scheduleKeyDiscardTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "key discard", out, ZmScheduler::Advance, &m_keyDiscardTimer,
      [](auto link) { link->keyDiscard_(); });
  }
  void cancelKeyDiscardTimer_() {
    cancelTimer_("key discard", &m_keyDiscardTimer);
  }

  void schedulePMTUDTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "PMTUD", out, ZmScheduler::Advance, &m_pmtudTimer,
      [](auto link) { link->pmtudTimeout_(); });
  }
  void cancelPMTUDTimer_() { cancelTimer_("PMTUD", &m_pmtudTimer); }

  void schedulePMTUD_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD schedule outside Tx thread", return);
    if (closed() || !m_established || m_path.probePending() || m_pmtudTimer)
      return;
    if (!m_path.nextProbeSize()) return;
    impl()->queueTxFlush_();
  }

  void schedulePathTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "path validation", out, ZmScheduler::Update, &m_pathTimer,
      [](auto link) { link->pathTimeout_(); });
  }
  void cancelPathTimer_() {
    cancelTimer_("path validation", &m_pathTimer);
  }

  void cancelTimers() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC timer cancel before app initialization", return);
    if (txInvoked_()) {
      cancelTimers_();
      return;
    }
    app()->txRun([link = ZmMkRef(impl())]() mutable {
      link->cancelTimers_();
    });
  }
  template <typename Fn>
  void teardownTimers(Fn fn) {
    if (!app() || !app()->mx()) {
      fn();
      return;
    }
    app()->txRun([link = ZmMkRef(impl()), fn = ZuMv(fn)]() mutable {
      link->m_timerTeardown = true;
      link->cancelTimers_();
      link->app()->txRun([link = ZuMv(link), fn = ZuMv(fn)]() mutable {
	fn();
      });
    });
  }

  void cancelTimers_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC timer cancel outside Tx thread", return);
    // Live-state cleanup only; release/destruction paths use teardownTimers().
    cancelAckDelayTimer_();
    cancelLossTimer_();
    cancelPTO_();
    cancelIdleTimer_();
    cancelCloseTimer_();
    cancelKeyDiscardTimer_();
    cancelPMTUDTimer_();
    cancelPathTimer_();
  }
  bool timersActive_() const {
    return m_ackDelayTimer || m_lossTimer || m_ptoTimer ||
      m_idleTimer || m_closeTimer || m_keyDiscardTimer ||
      m_pmtudTimer || m_pathTimer;
  }

  void ackDelay_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK delay timer outside Tx thread", return);
    if (m_timerTeardown) return;
    if (!closed()) impl()->ackDelayExpired_();
  }
  void lossTime_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer outside Tx thread", return);
    if (m_timerTeardown) return;
    if (!closed()) impl()->lossTimeExpired_();
  }
  void idleTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC idle timer outside Tx thread", return);
    if (m_timerTeardown) return;
    if (!closed()) impl()->idleExpired_();
  }
  void closeTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC close timer outside Tx thread", return);
    if (m_timerTeardown) return;
    impl()->closeExpired_();
  }
  void keyDiscard_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC key discard timer outside Tx thread", return);
    if (m_timerTeardown) return;
    if (!closed()) impl()->keyDiscardExpired_();
  }
  void pmtudTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD timer outside Tx thread", return);
    if (m_timerTeardown) return;
    if (!closed()) impl()->pmtudExpired_();
  }
  void pathTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path-validation timer outside Tx thread", return);
    if (m_timerTeardown) return;
    if (!closed()) impl()->pathExpired_();
  }

  void ackDelayExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK delay expiry outside Tx thread", return);
    m_txAck[CryptoLevel::OneRTT].due = true;
    impl()->flushTx_();
  }
  void lossTimeExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer expired outside Tx thread", return);
	  if (closed()) return;
	  ZuTime now = runtimeNow_();
	  detectLossTx_(0, now, {}, false);
	}
  void idleExpired_() { }
  void closeExpired_() { }
  void keyDiscardExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC key discard expiry outside Tx thread", return);
    app()->rxRun([link = ZmMkRef(impl())]() mutable {
      link->clearExpiredKeysRx_();
    });
  }
  void clearExpiredKeysRx_() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC key discard outside Rx thread", return);
    discardOldPeerKeys_();
  }
  void pmtudExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD expiry outside Tx thread", return);
    if (!m_path.probeExpired()) return;
    schedulePMTUD_();
  }
  void applyPathHint_(PathHint hint) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path hint outside Tx thread", return);
    if (!hint) return;
    m_path.applyHint(hint);
    if (!m_path.probePending()) cancelPMTUDTimer_();
    schedulePMTUD_();
  }
  void onPMTUDProbeAckd_(unsigned size) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD ACK outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeAckd();
    cancelPMTUDTimer_();
    schedulePMTUD_();
  }
  void onPMTUDProbeLost_(unsigned size) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD loss outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeLost();
    cancelPMTUDTimer_();
    schedulePMTUD_();
  }
  void persistentCongestion_() {
    m_congestion.persistentCongestion();
    ++m_txDiag.persistentCongestion;
    updateCongestionDiag_();
  }

  void discardPktSpace_(CryptoLevel::T level) {
    if (level == CryptoLevel::OneRTT) return;
    m_rxSpaceDiscarded[level] = true;
    m_rxCrypto[level].reset();
    m_rxAcks.sent(PktSpace::T(level));
    app()->txRun([link = ZmMkRef(impl()), level]() mutable {
      link->discardTxPktSpace_(level);
    });
  }

  void discardTxPktSpace_(CryptoLevel::T level) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC sent-packet discard outside Tx thread", return);
    if (level == CryptoLevel::OneRTT) return;
    m_txSpaceDiscarded[level] = true;
    m_congestion.release(m_txPkts[level].bytesInFlight());
    m_txPkts[level].clear();
    m_txCrypto[level].reset();
    m_txCryptoUnackd[level].clean();
    m_txAck[level].nRanges = 0;
    updateCongestionDiag_();
    scheduleLossTimer_();
  }

  bool reclaimPTO_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PTO reclaim outside Tx thread", return false);
    CryptoLevel::T level = CryptoLevel::Initial;
    if (!ptoLevel_(level)) return false;
    unsigned n = m_txPkts[level].reclaimOnPTO(2);
    if (n) {
      ++m_txDiag.ptoCount;
      m_ptoBackoff.expired();
      Zquic_DEBUG_LOG_(([level, n](auto &s) {
	  s << "PTO fired level=" << int(level) << " probes=" << n;
	}));
    }
    return n;
  }

  bool nextRetransmit_(CryptoLevel::T &level, SentFrameRef &ref) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC retransmit selection outside Tx thread", return false);
    for (unsigned i = 0; i < 3; ++i) {
      CryptoLevel::T l = CryptoLevel::T(i);
      if (m_txSpaceDiscarded[l]) continue;
      do {
	if (!m_txPkts[l].nextRetransmit(ref)) goto nextSpace;
	SentFrameRef tail;
	if (!clipRetransmit_(l, ref, tail)) continue;
	if (tail) (void)requeueRetransmit_(l, tail);
	break;
      } while (true);
      level = l;
      ++m_txDiag.retransmittedFrames;
#ifdef Zquic_DEBUG
      if (ref.kind == SentFrameKind::Stream)
	ZiLOG(Info, "Zquic.retx", ([
	  level, ref
	](auto &s) {
	  s << "stream retransmit selected level=" << int(level) <<
	    " streamID=" << ref.streamID <<
	    " offset=" << ref.offset <<
	    " length=" << ref.length <<
	    " fin=" << int(ref.fin) <<
	    " rangeOffset=" << ref.range.offset <<
	    " rangeLength=" << ref.range.length <<
	    " hasBuf=" << int(!!ref.range.buf);
	}));
#endif
      Zquic_DEBUG_LOG_(([level, ref](auto &s) {
	  s << "retransmit queued level=" << int(level) <<
	    " kind=" << int(ref.kind) <<
	    " streamID=" << ref.streamID <<
	    " offset=" << ref.offset <<
	    " length=" << ref.length;
      }));
      return true;
nextSpace:
      continue;
    }
    return false;
  }

  bool requeueRetransmit_(CryptoLevel::T level, const SentFrameRef &ref) {
    ZiAssert(txInvoked_(), "Zquic",
      (), "QUIC retransmit requeue outside Tx thread", return false);
    if (m_txSpaceDiscarded[level]) return false;
    if (!frameStillUnackd_(level, ref)) return false;
    return m_txPkts[level].requeueRetransmit(ref);
  }

  bool buildRetransmitStream_(PktBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Stream) return false;
    build.reset();
    if (!appendPendingAck_(CryptoLevel::OneRTT, build)) return false;
    int n = FrameCodec::writeStreamPrefix(
      build.scratch(), build.scratchAvail(), ref.streamID, ref.offset,
      ref.length, ref.fin);
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    return build.add(ref.range);
  }

  bool buildRetransmitControl_(PktBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Control ||
	ref.controlType == FrameType::Unknown ||
	ref.controlType == FrameType::PathResponse)
      return false;
    ControlFrame frame = controlFrame_(ref);
    if (!controlStillValid_(frame)) return false;
    build.reset();
    if (!appendPendingAck_(CryptoLevel::OneRTT, build)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    return n > 0 && build.commitScratch(unsigned(n));
  }

  bool buildPingProbe_(PktBuild &build) {
    build.reset();
    if (!appendPendingAck_(CryptoLevel::OneRTT, build)) return false;
    int n = FrameCodec::writePing(build.scratch(), build.scratchAvail());
    return n > 0 && build.commitScratch(unsigned(n));
  }

  template <typename SendProbe>
  bool sendPMTUDProbe_(ZiSockAddr addr, SendProbe sendProbe) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD probe send outside Tx thread", return false);
    if (!m_established || m_path.probePending()) return false;
    unsigned size = m_path.nextProbeSize();
    if (!size || !m_path.canSendProbe(size) || !m_congestion.canSend(size))
      return false;
    m_path.startProbe(size);
    PktBuild build;
    if (!buildPingProbe_(build) || !sendProbe(build, ZuMv(addr), size)) {
      m_path.probeLost();
      return false;
    }
    schedulePMTUDTimer_(runtimeNow_() + ptoTimeout_());
    return true;
  }

  bool buildRetransmitCrypto_(
    CryptoLevel::T level, PktBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Crypto || m_txSpaceDiscarded[level])
      return false;
    ZuCSpan payload;
    if (!m_txCrypto[level].txPayload(ref.offset, ref.length, payload))
      return false;
    build.reset();
    if (!appendPendingAck_(level, build)) return false;
    int n = FrameCodec::writeCryptoPrefix(
      build.scratch(), build.scratchAvail(), ref.offset, payload.length());
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    return build.add(payload);
  }

  bool recordRxPkt_(
    CryptoLevel::T level, uint64_t pn,
    EcnMark::T ecn = EcnMark::NotECT) {
    if (m_rxAcks.tracker(pktSpace_(level)).contains(pn)) return false;
    m_rxAcks.received(
      pktSpace_(level), pn, nowUS_(), localMaxAckDelayUS_(), false, false,
      ecn);
    noteRxECN_(level, ecn);
    if (pn > m_rxLargestPN[level])
      m_rxLargestPN[level] = pn;
    return true;
  }

  void recordTxPkt_(
    CryptoLevel::T level, uint64_t pn, unsigned bytes, ZuCSpan frame,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    SentFrameRef ref;
    bool ackEliciting = false;
    if (!runtimeFrameRef(frame, ref, ackEliciting)) return;
    recordTxPkt_(
      level, pn, bytes, ref, ackEliciting, false, 0, ackLevel, ackLargest);
  }

  void recordTxPkt_(
    CryptoLevel::T level, uint64_t pn, unsigned bytes,
    const SentFrameRef &ref, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    TxPktRefs refs;
    if (ref.kind != SentFrameKind::None) refs.add(ref);
    recordTxPkt_(
      level, pn, bytes, refs, ackEliciting, pmtudProbe, pmtudSize,
      ackLevel, ackLargest);
  }

  void recordTxPkt_(
    CryptoLevel::T level, uint64_t pn, unsigned bytes,
    const TxPktRefs &refs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    if (m_txSpaceDiscarded[level]) return;
    if (!ackEliciting && !refs.count()) return;
    SentPkt packet;
    packet.pn = pn;
    packet.space = runtimePktSpace(level);
    packet.bytes = bytes;
    packet.sentTime = runtimeNow_();
    packet.ackEliciting = ackEliciting;
    packet.inFlight = ackEliciting;
    packet.pmtudProbe = pmtudProbe;
    packet.pmtudSize = pmtudSize;
    packet.ackLevel = ackLevel;
    packet.ackLargest = ackLargest;
    for (unsigned i = 0; i < refs.count(); ++i)
      packet.addFrame(refs[i], refs.stream(i));
    if (!m_txPkts[level].add(packet)) return;
    recordTxUnackd_(level, refs);
    if (ackEliciting) {
      m_congestion.sent(bytes);
      updateCongestionDiag_();
      scheduleLossTimer_();
    }
  }

  void processAckFrame_(CryptoLevel::T level, const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK receive processing outside Rx thread", return);
    if (!frame.ackRanges.length()) return;
    AckSnapshot ack;
    ack.level = level;
    ack.delay = frame.value;
    ack.ecn = frame.ackECN;
    ack.nRanges = frame.ackRanges.length();
    for (unsigned i = 0; i < ack.nRanges; ++i)
      ack.ranges[i] = frame.ackRanges[i];
    app()->txRun([link = ZmMkRef(impl()), ack]() mutable {
      link->processAckFrameTx_(ack);
    });
  }

  void applyAckUpdateTx_(const PktTxUpdate &update, bool &congestionOpened) {
    applyAckOfAckTx_(update);
    for (unsigned i = 0; i < update.nAckdFrames; ++i) {
      const SentFrameRef &ref = update.ackdFrames[i];
      StreamRef streamRef;
      Stream *stream = static_cast<Stream *>(update.ackdOwners[i]);
      if ((ref.kind == SentFrameKind::Stream ||
	  ref.kind == SentFrameKind::Control) &&
	  ref.streamID <= uint64_t(INT64_MAX)) {
	streamRef = findStream(int64_t(ref.streamID));
	stream = streamRef.ptr();
      }
      if (ackTxFrame_(update.level, ref, stream))
	switch (ref.kind) {
	  case SentFrameKind::Stream:
	    if (ref.fin) reapStream_(streamRef);
	    break;
	  case SentFrameKind::Control:
	    if (ref.controlType == FrameType::ResetStream)
	      reapStream_(streamRef);
	    break;
	  default:
	    break;
	}
    }
    if (update.normalAckdBytes) {
      m_congestion.ackd(update.normalAckdBytes);
      congestionOpened = true;
    }
    if (update.pmtudAckdBytes) {
      m_congestion.ackd(update.pmtudAckdBytes, true);
      onPMTUDProbeAckd_(update.pmtudAckdSize);
    }
  }
  void applyAckOfAckTx_(const PktTxUpdate &update) {
    for (unsigned i = 0; i < 3; ++i) {
      if (!update.ackdAck[i]) continue;
      auto level = CryptoLevel::T(i);
      uint64_t largest = update.ackLargest[i];
      app()->rxRun([link = ZmMkRef(impl()), level, largest]() mutable {
	link->ackAckdRx_(level, largest);
      });
    }
  }
  void ackAckdRx_(CryptoLevel::T level, uint64_t largest) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK-of-ACK trim outside Rx thread", return);
    m_rxAcks.tracker(pktSpace_(level)).ackdByPeer(largest);
  }
  void applyLossUpdateTx_(const PktTxUpdate &update) {
    if (update.normalLostBytes)
      m_congestion.lostAt(
	update.normalLostBytes,
	uint64_t(update.normalLostSentTime.microsecs()));
    if (update.pmtudLostBytes) {
      m_congestion.lostAt(
	update.pmtudLostBytes,
	uint64_t(update.pmtudLostSentTime.microsecs()), true);
      onPMTUDProbeLost_(update.pmtudLostSize);
    }
  }

  void processAckFrameTx_(const AckSnapshot &ack) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    TxAckWork work;
    work.ack = ack;
    work.gen = m_txRuntimeGen;
    processAckFrameTx_(ZuMv(work));
  }
  void processAckFrameTx_(TxAckWork work) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    AckSnapshot &ack = work.ack;
    if (work.gen != m_txRuntimeGen) return;
    if (!ack.nRanges || m_txSpaceDiscarded[ack.level]) return;
    if (!ackFrameValidTx_(ack)) {
      ++m_txDiag.failures;
      return;
    }
    if (!work.ecnValidated) {
      validateAckECN_(ack);
      for (unsigned i = 0; i < ack.nRanges; ++i) {
	uint64_t largest = ack.ranges[i].largest;
	if (ZuCmp<uint64_t>::null(m_txLargestAckd[ack.level]) ||
	    largest > m_txLargestAckd[ack.level])
	  m_txLargestAckd[ack.level] = largest;
      }
      work.ecnValidated = true;
    }
    PktTxUpdate update;
    if (!work.lossPhase) {
      if (!m_txPkts[ack.level].ackBatch(
	  ack.ranges, ack.nRanges, work.ackBatch, RecoveryScanBatch,
	  ack.level, &update)) {
	applyAckUpdateTx_(update, work.congestionOpened);
	app()->txRun([link = ZmMkRef(impl()), work = ZuMv(work)]() mutable {
	  link->processAckFrameTx_(ZuMv(work));
	});
	return;
      }
      applyAckUpdateTx_(update, work.congestionOpened);
      work.lossPhase = true;
    } else
      applyAckUpdateTx_(update, work.congestionOpened);
    if (work.ackBatch.haveAckForLoss) {
      PktTxUpdate lossUpdate;
      if (!m_txPkts[ack.level].markPktThresholdLossBatch(
	  work.ackBatch.largestAckdForLoss, 3, work.lossBatch,
	  RecoveryScanBatch, &lossUpdate)) {
	applyLossUpdateTx_(lossUpdate);
	if (lossUpdate.lostBytes) work.retransmit = true;
	app()->txRun([link = ZmMkRef(impl()), work = ZuMv(work)]() mutable {
	  link->processAckFrameTx_(ZuMv(work));
	});
	return;
      }
      applyLossUpdateTx_(lossUpdate);
      if (lossUpdate.lostBytes) work.retransmit = true;
    }
    if (work.lossBatch.lost) {
      if (m_txPkts[ack.level].persistentCongestion(
	  persistentCongestionThreshold_(), RecoveryScanBatch))
	persistentCongestion_();
    }
    if (work.ackBatch.ackd && ack.level == CryptoLevel::OneRTT) {
      ZuTime now = runtimeNow_();
      if (*work.ackBatch.latestSentTime && work.ackBatch.latestSentTime < now)
	m_rtt.sample(
	  now - work.ackBatch.latestSentTime, ackDelay_(ack.delay), true);
      m_ptoBackoff.reset();
    } else if (work.ackBatch.ackd) {
      m_ptoBackoff.reset();
    }
    updateCongestionDiag_();
    schedulePTO_();
    scheduleLossTimer_();
    if (work.congestionOpened || congestionAllowance_())
      impl()->flushTx_();
    if (work.retransmit) impl()->queueRetransmit_();
  }

  bool ackFrameValidTx_(const AckSnapshot &ack) const {
    for (unsigned i = 0; i < ack.nRanges; ++i)
      if (ack.ranges[i].largest >= m_txPN[ack.level]) return false;
    return true;
  }

  bool validateAckECN_(const AckSnapshot &ack) {
    if (!ack.ecn.any()) return true;
    unsigned i = unsigned(ack.level);
    const AckECN &last = m_peerAckECN[i];
    bool fail =
      ack.ecn.ect0 < last.ect0 ||
      ack.ecn.ect1 < last.ect1 ||
      ack.ecn.ce < last.ce;
    uint64_t total = ack.ecn.ect0 + ack.ecn.ect1;
    if (total < ack.ecn.ect0) fail = true;
    uint64_t withCE = total + ack.ecn.ce;
    if (withCE < total) fail = true;
    if (ack.nRanges) {
      uint64_t largest = ack.ranges[ack.nRanges - 1].largest;
      if (withCE > largest + 1) fail = true;
    }
    if (fail) {
      m_path.setEcnDisabled();
      ++m_txDiag.ecnValidationFailures;
      return false;
    }
    m_peerAckECN[i] = ack.ecn;
    m_txDiag.peerAckECN[i] = ack.ecn;
    return true;
  }

  static ZuTime runtimeNow_() { return Zm::now(); }
  ZuTime maxAckDelay_() const {
    if (!m_crypto.peerTransportParamsReceived()) return ZuTime{0};
    return timeUS(m_crypto.peerTransportParams().maxAckDelay * 1000);
  }
  ZuTime ackDelay_(uint64_t delay) const {
    if (!m_crypto.peerTransportParamsReceived()) return ZuTime{0};
    const auto &params = m_crypto.peerTransportParams();
    ZuTime maxAckDelay = maxAckDelay_();
    uint64_t maxAckUsec = uint64_t(maxAckDelay.microsecs());
    if (delay > (maxAckUsec >> params.ackDelayExponent))
      return maxAckDelay;
    delay <<= params.ackDelayExponent;
    ZuTime ackDelay = timeUS(delay);
    return ackDelay < maxAckDelay ? ackDelay : maxAckDelay;
  }
  ZuTime ptoTimeout_() const {
    return m_ptoBackoff.timeout(m_rtt, maxAckDelay_());
  }
  ZuTime keyDiscardDeadline_() const {
    return runtimeNow_() + ptoTimeout_() * ZuDecimal{3};
  }
  bool buildPayload_(
    CryptoLevel::T level, PktBuild &build, ZuCSpan frame) {
    build.reset();
    return appendPendingAck_(level, build) && build.add(frame);
  }

  bool buildPayload_(
    CryptoLevel::T level, PktBuild &build,
    ZuCSpan prefix, ZuCSpan payload) {
    build.reset();
    return appendPendingAck_(level, build) &&
      build.add(prefix) && build.add(payload);
  }

  template <typename SendPkt>
  bool holdInitialForCoalesce_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    if (!m_coalesceLong || m_coalesceInitial)
      return sendPkt(ZuMv(buf), ZuMv(addr));
    m_coalesceInitial = ZuMv(buf);
    m_coalesceAddr = ZuMv(addr);
    return true;
  }

  template <typename SendPkt>
  bool sendHandshakeCoalesced_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    if (!m_coalesceInitial) return sendPkt(ZuMv(buf), ZuMv(addr));
    ZmRef<ZiIOBuf> initial = ZuMv(m_coalesceInitial);
    ZiSockAddr initialAddr = ZuMv(m_coalesceAddr);
    unsigned initialEnd = initial->skip + initial->length;
    if (initialEnd <= initial->size &&
	initial->length <= initial->size - buf->length) {
      memcpy(
	initial->data_() + initialEnd,
	buf->data_() + buf->skip, buf->length);
      initial->length += buf->length;
      return sendPkt(ZuMv(initial), ZuMv(initialAddr));
    }
    if (!sendPkt(ZuMv(initial), ZuMv(initialAddr))) return false;
    return sendPkt(ZuMv(buf), ZuMv(addr));
  }

  template <typename SendPkt>
  bool flushCoalescedInitial_(SendPkt sendPkt) {
    if (!m_coalesceInitial) return true;
    ZmRef<ZiIOBuf> initial = ZuMv(m_coalesceInitial);
    ZiSockAddr addr = ZuMv(m_coalesceAddr);
    return sendPkt(ZuMv(initial), ZuMv(addr));
  }
  void beginLongCoalesce_() { m_coalesceLong = true; }
  void endLongCoalesce_() { m_coalesceLong = false; }

  template <typename SendCryptoPkt>
  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    unsigned chunkMax, ZiSockAddr addr, SendCryptoPkt sendCryptoPkt) {
    return sendRuntimeCryptoFlights(
      m_txCrypto, m_txDiag,
      data, len, offsets, chunkMax, ZuMv(addr),
      [sendCryptoPkt](
	  CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) mutable {
	return sendCryptoPkt(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
  }

  template <
    typename BuildPayload,
    typename SendInitial, typename SendHandshake, typename SendShort>
  bool sendCryptoPkt_(
    CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr,
    BuildPayload buildPayload,
    SendInitial sendInitial, SendHandshake sendHandshake, SendShort sendShort) {
    PktBuild build;
    if (!buildPayload(level, build, frame)) return false;
    if (level == CryptoLevel::Initial)
      return sendInitial(build, ZuMv(addr), frame);
    if (level == CryptoLevel::Handshake)
      return sendHandshake(build, ZuMv(addr), frame);
    return sendShort(build, ZuMv(addr), frame);
  }

  template <
    typename BuildPayload,
    typename SendInitial, typename SendHandshake, typename SendShort>
  bool sendCryptoPkt_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr,
    BuildPayload buildPayload,
    SendInitial sendInitial, SendHandshake sendHandshake, SendShort sendShort) {
    PktBuild build;
    if (!buildPayload(level, build, prefix, payload)) return false;
    if (level == CryptoLevel::Initial)
      return sendInitial(build, ZuMv(addr), {}, &ref, true);
    if (level == CryptoLevel::Handshake)
      return sendHandshake(build, ZuMv(addr), {}, &ref, true);
    return sendShort(build, ZuMv(addr), {}, &ref, true);
  }

  void recordProtPktTx_(
    CryptoLevel::T level, uint64_t pn, unsigned bytes, ZuCSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    if (recordRefs)
      recordTxPkt_(
	level, pn, bytes, *recordRefs, ackEliciting, pmtudProbe, pmtudSize,
	ackLevel, ackLargest);
    else
      recordTxPkt_(level, pn, bytes, recordFrame, ackLevel, ackLargest);
    ++m_txPN[level];
    ++m_txDiag.packetsTx;
    m_txDiag.bytesTx += bytes;
    Zquic_DEBUG_LOG_(([level, pn, bytes, ackEliciting](auto &s) {
	s << "packet sent level=" << int(level) <<
	  " pn=" << pn <<
	  " bytes=" << bytes <<
	  " ackEliciting=" << ackEliciting;
      }));
    if (ackEliciting) schedulePTO();
  }

  bool txAckMeta_(
    CryptoLevel::T level, PktBuild &payload,
    uint8_t &ackLevel, uint64_t &ackLargest) const {
    ackLevel = 3;
    ackLargest = 0;
    if (!payload.ack(level)) return false;
    const AckSnapshot &ack = m_txAck[level];
    if (!ack.nRanges) return false;
    ackLevel = uint8_t(level);
    ackLargest = ack.ranges[ack.nRanges - 1].largest;
    return true;
  }

  const auto &initialKeys_(InitialKeyDir::T dir) const {
    return dir == InitialKeyDir::Client ?
      m_crypto.initialKeys().client :
      m_crypto.initialKeys().server;
  }

  template <typename AllocTxPkt, typename SendPkt>
	  bool sendProtInitialPkt_(
	    InitialKeyDir::T keyDir, RuntimeCID::T dcid, RuntimeCID::T scid,
	    unsigned pnLength, bool padInitial, PktBuild &payload, ZiSockAddr addr,
	    ZuCSpan recordFrame, const TxPktRefs *recordRefs, bool ackEliciting,
	    AllocTxPkt allocTxPkt, SendPkt sendPkt) {
	    ZiAssert(txInvoked_(), "Zquic", (),
	      "QUIC Initial packet protection outside Tx thread", return false);
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    const auto &initialKeys = initialKeys_(keyDir);
    int headerLen = -1;
    unsigned targetPlainLen = payload.bytes();
    for (unsigned i = 0; i < 4; ++i) {
      headerLen = Pkt::writeInitial(
	buf->data_(), buf->size, runtimeCID_(dcid), runtimeCID_(scid),
	targetPlainLen + InitialSecret::TagLen, pnLength);
      if (headerLen < 0) return false;
      if (!padInitial) break;
      unsigned minPlainLen = MinUDPPayload -
	unsigned(headerLen) - pnLength - InitialSecret::TagLen;
      if (minPlainLen <= targetPlainLen) break;
      targetPlainLen = minPlainLen;
    }
    if (padInitial && !payload.padTo(targetPlainLen)) return false;
    if (PktNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Initial], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Initial];
    int n = InitialPktProt::protectLongV(
      buf->data_(), buf->size, initialKeys, pn,
      byteSpan(buf->data_(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_txDiag.failures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) return false;
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta_(CryptoLevel::Initial, payload, ackLevel, ackLargest);
    recordProtPktTx_(
      CryptoLevel::Initial, pn, unsigned(n), recordFrame, recordRefs,
      ackEliciting, false, 0, ackLevel, ackLargest);
    if (payload.ack(CryptoLevel::Initial)) ackSentTx_(CryptoLevel::Initial);
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
	  bool sendProtHandshakePkt_(
	    RuntimeCID::T dcid, RuntimeCID::T scid, unsigned pnLength,
	    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
	    const TxPktRefs *recordRefs, bool ackEliciting,
	    AllocTxPkt allocTxPkt, SendPkt sendPkt) {
	    ZiAssert(txInvoked_(), "Zquic", (),
	      "QUIC Handshake packet protection outside Tx thread", return false);
	    if (!txTrafficSecretInstalled_(CryptoLevel::Handshake)) {
	      ++m_txDiag.failures;
	      return false;
	    }
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    int headerLen = Pkt::writeHandshake(
      buf->data_(), buf->size, runtimeCID_(dcid), runtimeCID_(scid),
      payload.bytes() +
	txTrafficSecret_(CryptoLevel::Handshake).tagLen,
      pnLength);
    if (headerLen < 0 ||
	PktNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Handshake], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Handshake];
    int n = PktProt::protectLongV(
      buf->data_(), buf->size,
      txProtState_(CryptoLevel::Handshake), pn,
      byteSpan(buf->data_(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_txDiag.failures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) return false;
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta_(CryptoLevel::Handshake, payload, ackLevel, ackLargest);
    recordProtPktTx_(
      CryptoLevel::Handshake, pn, unsigned(n), recordFrame, recordRefs,
      ackEliciting, false, 0, ackLevel, ackLargest);
    if (payload.ack(CryptoLevel::Handshake))
      ackSentTx_(CryptoLevel::Handshake);
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
	  bool sendProtShortPkt_(
    RuntimeCID::T dcid, unsigned pnLength, PktBuild &payload,
    ZiSockAddr addr, ZuCSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt,
    unsigned pmtudSize = 0) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Short packet protection outside Tx thread", return false);
    if (!m_established &&
	!txTrafficSecretInstalled_(CryptoLevel::OneRTT)) {
      ++m_txDiag.failures;
      return false;
    }
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    int headerLen = Pkt::writeShort(
      buf->data_(), buf->size, runtimeCID_(dcid),
      m_txPN[CryptoLevel::OneRTT], pnLength, m_txKeyPhase);
    if (headerLen < 0) return false;
    if (!payload.padForProtSample(
	  unsigned(headerLen) - pnLength, pnLength,
	  txTrafficSecret_(CryptoLevel::OneRTT).tagLen))
      return false;
    if (pmtudSize) {
      unsigned tagLen = txTrafficSecret_(CryptoLevel::OneRTT).tagLen;
      unsigned headerBytes = unsigned(headerLen);
      if (pmtudSize <= headerBytes + tagLen) return false;
      if (!payload.padTo(pmtudSize - headerBytes - tagLen)) return false;
    }
    uint64_t pn = m_txPN[CryptoLevel::OneRTT];
    int n = PktProt::protectShortV(
      buf->data_(), buf->size,
      txProtState_(CryptoLevel::OneRTT), pn,
      byteSpan(buf->data_(), unsigned(headerLen)),
      payload.data(), payload.count(),
      unsigned(headerLen) - pnLength, pnLength);
    if (n < 0) {
      ++m_txDiag.failures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) return false;
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta_(CryptoLevel::OneRTT, payload, ackLevel, ackLargest);
    recordProtPktTx_(
      CryptoLevel::OneRTT, pn, unsigned(n), recordFrame, recordRefs,
      ackEliciting, pmtudSize != 0, pmtudSize, ackLevel, ackLargest);
    if (payload.ack(CryptoLevel::OneRTT)) ackSentTx_(CryptoLevel::OneRTT);
    return true;
  }

  template <typename ReceiveLong, typename ReceiveShort>
  void receiveDatagram_(
    Datagram d, ReceiveLong receiveLong, ReceiveShort receiveShort) {
    ++m_rxDiag.datagramsRx;
    if (!d.buf) {
      ++m_rxDiag.failures;
      return;
    }
    m_rxDiag.bytesRx += d.buf->length;
    recordPathRx_(d.buf->length);
    bool ok = true;
    unsigned offset = 0;
    while (offset < d.buf->length) {
      ZuCSpan packet{
	d.buf->data_() + offset, d.buf->length - offset};
      if (Pkt::isLong(packet)) {
	LongHdr h;
	if (Pkt::parseLong(packet, h) < 0) {
	  ok = false;
	  break;
	}
	uint64_t packetLen = uint64_t(h.pnOffset) + h.length;
	if (packetLen > packet.length() || packetLen < h.payloadOffset ||
	    packetLen > UINT_MAX) {
	  ok = false;
	  break;
	}
	if (!receiveLong(d, offset, unsigned(packetLen))) ok = false;
	offset += unsigned(packetLen);
	continue;
      }
      if (!receiveShort(d, offset, d.buf->length - offset)) ok = false;
      break;
    }
    if (!ok) ++m_rxDiag.failures;
  }

  template <typename PrepareLong, typename ConsumeFrames>
  bool receiveProtLongPkt_(
    InitialKeyDir::T keyDir, Datagram &d, unsigned packetOffset, unsigned packetLen,
    PrepareLong prepareLong, ConsumeFrames consumeFrames) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{base, packetLen};
    ZuCSpan datagram{d.buf->data_(), d.buf->length};
    LongHdr h;
    if (Pkt::parseLong(packet, h) < 0) return false;
    if (!prepareLong(h, d)) return false;
    CryptoLevel::T level =
      h.type == PktType::Initial ? CryptoLevel::Initial :
      h.type == PktType::Handshake ? CryptoLevel::Handshake :
      CryptoLevel::OneRTT;
    if (h.type != PktType::Initial && h.type != PktType::Handshake)
      return false;
    if (m_rxSpaceDiscarded[level]) return true;
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = -1;
    if (level == CryptoLevel::Initial)
      plainLen = InitialPktProt::unprotectLong(
	base, packetLen, initialKeys_(keyDir),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    else {
      if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::Handshake)) {
	++m_rxDiag.failures;
	return false;
      }
      plainLen = PktProt::unprotectLong(
	base, packetLen,
	m_crypto.rxProtState(CryptoLevel::Handshake),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    }
    if (plainLen < 0) {
      if (checkStatelessReset_(datagram)) return true;
      ++m_rxDiag.failures;
      return false;
    }
    if (!recordRxPkt_(level, pn, d.ecn)) return true;
    ++m_rxDiag.packetsRx;
    return consumeFrames(
      level, pn, byteSpan(base + payloadOffset, unsigned(plainLen)),
      d.addr, d.buf);
  }

  template <typename ConsumeFrames>
  bool receiveProtShortPkt_(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    ConsumeFrames consumeFrames) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{base, packetLen};
    ZuCSpan datagram{d.buf->data_(), d.buf->length};
    if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT)) {
      if (checkStatelessReset_(datagram)) return true;
      return false;
    }
    ShortHdr h;
    if (Pkt::parseShort(packet, m_localSCID.length(), h) < 0 ||
	!(h.dcid == m_localSCID)) {
      if (checkStatelessReset_(datagram)) return true;
      return false;
    }
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = PktProt::unprotectShort(
      base, packetLen,
      m_crypto.rxProtState(CryptoLevel::OneRTT),
      m_rxLargestPN[CryptoLevel::OneRTT],
      h.pnOffset, pn, payloadOffset);
    if (plainLen < 0) {
      if (m_rxOldProt.valid()) {
	plainLen = PktProt::unprotectShort(
	  base, packetLen, m_rxOldProt,
	  m_rxLargestPN[CryptoLevel::OneRTT],
	  h.pnOffset, pn, payloadOffset);
	if (plainLen >= 0) {
	  if (!((base[0] & 0x04) == (m_rxOldKeyPhase ? 0x04 : 0))) {
	    ++m_rxDiag.invalidKeyPhases;
	    return false;
	  }
	  ++m_rxDiag.oldKeysAccepted;
	}
      }
      if (plainLen < 0 && ensureNextPeerKey_()) {
	plainLen = PktProt::unprotectShort(
	  base, packetLen, m_rxNextProt,
	  m_rxLargestPN[CryptoLevel::OneRTT],
	  h.pnOffset, pn, payloadOffset);
	if (plainLen >= 0) {
	  bool phase = base[0] & 0x04;
	  if (phase == m_rxKeyPhase ||
	      m_rxOldProt.valid() ||
	      pn <= m_rxLargestPN[CryptoLevel::OneRTT]) {
	    ++m_rxDiag.invalidKeyPhases;
	    return false;
	  }
	  if (!commitPeerKeyUpdate_(m_rxNextTrafficSecret))
	    return false;
	}
      }
      if (plainLen < 0) {
	if (checkStatelessReset_(datagram)) return true;
	++m_rxDiag.failures;
	return false;
      }
    } else if ((base[0] & 0x04) != (m_rxKeyPhase ? 0x04 : 0)) {
      ++m_rxDiag.invalidKeyPhases;
      return false;
    }
    if (!recordRxPkt_(CryptoLevel::OneRTT, pn, d.ecn))
      return true;
    ++m_rxDiag.packetsRx;
    return consumeFrames(
      CryptoLevel::OneRTT, pn,
      byteSpan(base + payloadOffset, unsigned(plainLen)), d.addr, d.buf);
  }

  template <typename EmitTLS, typename HandleControl>
  bool consumeProtFrames_(
    CryptoLevel::T level, uint64_t pn, ZuCSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, EmitTLS emitTLS,
    HandleControl handleControl) {
    unsigned offset = 0;
    auto frame_ = ZmAlloc(Frame, 1);
    new (&frame_[0]) Frame{};
    auto &frame = frame_[0];
    ZuGuard frameGuard{[&frame]() { frame.~Frame(); }};
    bool ackEliciting = false;
    bool immediateAck = false;

    while (offset < frames.length()) {
      unsigned used = 0;
      if (FrameCodec::parse(
	  ZuCSpan{frames.data() + offset, frames.length() - offset},
	  frame, used) < 0 || !used)
	return false;
      ++m_rxDiag.framesRx;
      if (!packetFrameLegal_(level, frame)) return false;
      if (FrameCodec::ackEliciting(frame.type))
	ackEliciting = true;
      switch (frame.type) {
	case FrameType::Ack:
	  processAckFrame_(level, frame);
	  break;
	case FrameType::Crypto: {
	  ZuCSpan contiguous;
	  m_rxCryptoAddr[level] = addr;
	  if (m_rxCrypto[level].receiveFrame(frame, contiguous) < 0)
	    return false;
	  m_rxDiag.cryptoBytesRx += frame.payload.length();
	  if (contiguous) {
	    size_t epoch = level == CryptoLevel::Initial ? 0 :
	      level == CryptoLevel::Handshake ? 2 : 3;
	    if (!emitTLS(epoch, contiguous, addr)) return false;
	  }
	  break;
	}
	case FrameType::Stream:
	  m_rxDiag.streamBytesRx += frame.payload.length();
	  if (receiveFrame(
	      frame, ZmRef<ZiIOBuf>{packetBuf}, nullptr, &immediateAck) < 0)
	    return false;
	  impl()->streamFrame(
	    frame.streamID, frame.offset, frame.payload, frame.fin);
	  break;
	case FrameType::ResetStream:
	case FrameType::StopSending:
	  if (receiveFrame(frame) < 0) return false;
	  break;
	case FrameType::NewConnectionID:
	  if (!receiveNewConnectionID_(frame)) return false;
	  break;
	case FrameType::RetireConnectionID:
	  if (!receiveRetireConnectionID_(frame)) return false;
	  break;
	case FrameType::MaxData:
	  if (!rxApplyMaxData_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::MaxStreamData:
	  if (!rxApplyMaxStreamData_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::MaxStreams:
	  if (!rxApplyMaxStreams_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::DataBlocked:
	  if (!validateDataBlocked_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::StreamDataBlocked:
	  if (!receiveStreamDataBlocked_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::StreamsBlocked:
	  if (!validateStreamsBlocked_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::PathChallenge:
	case FrameType::PathResponse:
	case FrameType::ConnectionClose:
	case FrameType::ApplicationClose:
	case FrameType::HandshakeDone:
	  if (!handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::Padding:
	case FrameType::Ping:
	case FrameType::NewToken:
	  break;
	default:
	  return false;
	}
      offset += used;
    }
    noteAck_(level, pn, ackEliciting, ZuMv(addr), immediateAck);
    return true;
  }

private:
  bool packetFrameLegal_(CryptoLevel::T level, const Frame &frame) const {
    switch (frame.type) {
      case FrameType::Unknown:
	return false;
      case FrameType::Padding:
      case FrameType::Ping:
      case FrameType::ConnectionClose:
	return true;
      case FrameType::Ack:
	return true;
      case FrameType::Crypto:
	return level != CryptoLevel::OneRTT || m_established;
      case FrameType::Stream:
      case FrameType::ResetStream:
      case FrameType::StopSending:
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
      case FrameType::DataBlocked:
      case FrameType::StreamDataBlocked:
      case FrameType::StreamsBlocked:
      case FrameType::NewConnectionID:
      case FrameType::RetireConnectionID:
      case FrameType::PathChallenge:
      case FrameType::PathResponse:
	return level == CryptoLevel::OneRTT;
      case FrameType::NewToken:
	return !m_isServer && level == CryptoLevel::OneRTT &&
	  frame.payload.length();
      case FrameType::ApplicationClose:
	return level == CryptoLevel::OneRTT;
      case FrameType::HandshakeDone:
	return !m_isServer && level == CryptoLevel::OneRTT;
      default:
	return false;
    }
  }

  const StreamLimit &localLimit_(Zi::StreamType::T type) const {
    return type == Zi::StreamType::Simplex ? m_peerUniLimit : m_peerBidiLimit;
  }

  StreamLimit &localLimit_(Zi::StreamType::T type) {
    return type == Zi::StreamType::Simplex ? m_peerUniLimit : m_peerBidiLimit;
  }

  const StreamLimit &peerLimit_(Zi::StreamType::T type) const {
    return type == Zi::StreamType::Simplex ? m_localUniLimit : m_localBidiLimit;
  }

  StreamLimit &peerLimit_(Zi::StreamType::T type) {
    return type == Zi::StreamType::Simplex ? m_localUniLimit : m_localBidiLimit;
  }

  const uint64_t &queued_(Zi::StreamType::T type) const {
    return type == Zi::StreamType::Simplex ? m_queuedUni : m_queuedBidi;
  }

  uint64_t &queued_(Zi::StreamType::T type) {
    return type == Zi::StreamType::Simplex ? m_queuedUni : m_queuedBidi;
  }
  const uint64_t &lastStreamsBlocked_(Zi::StreamType::T type) const {
    return type == Zi::StreamType::Simplex ?
      m_lastStreamsBlockedUni : m_lastStreamsBlockedBidi;
  }
  uint64_t &lastStreamsBlocked_(Zi::StreamType::T type) {
    return type == Zi::StreamType::Simplex ?
      m_lastStreamsBlockedUni : m_lastStreamsBlockedBidi;
  }

  StreamRef openLocalStream_(Zi::StreamType::T type) {
    return newStream_(nextStreamID_(type));
  }

  unsigned openQueued_(Zi::StreamType::T type, unsigned limit) {
    uint64_t &queued = queued_(type);
    unsigned opened = 0;
    while (queued && opened < limit && localLimit_(type).open()) {
      StreamRef stream = openLocalStream_(type);
      if (!stream) break;
      --queued;
      ++opened;
      streamQueuedOpen_(stream);
      scheduleStreamWritable_(stream);
    }
    return opened;
  }
  void streamQueuedOpen_(StreamRef stream) {
    if (!stream) return;
    app()->rxRun([link = ZmMkRef(impl()), stream = ZuMv(stream)]() mutable {
      link->streamed(ZuMv(stream));
    });
  }
  void scheduleOpenQueued_(Zi::StreamType::T type) {
    unsigned i = type == Zi::StreamType::Simplex ? 1 : 0;
    if (!queued_(type) || localLimit_(type).blocked() || m_openQueuedPending[i])
      return;
    m_openQueuedPending[i] = true;
    app()->txRun([link = ZmMkRef(impl()), type, i]() mutable {
      link->m_openQueuedPending[i] = false;
      link->openQueued_(type, OpenQueuedBatch);
      link->scheduleOpenQueued_(type);
    });
  }

  StreamRef findOrAccept_(uint64_t id) {
    if (id > uint64_t(INT64_MAX)) return nullptr;
    if (auto stream = findStream(int64_t(id))) return stream;
    if (StreamID::server(id) == m_isServer) return nullptr;
    return acceptPeerStream(id);
  }

  static bool localInitiated_(uint64_t id, bool isServer) {
    return StreamID::server(id) == isServer;
  }
  static unsigned streamTypeIndex_(Zi::StreamType::T type) {
    return type == Zi::StreamType::Simplex ? 1 : 0;
  }
  bool canPeerSend_(uint64_t id) const {
    return !StreamID::uni(id) || !localInitiated_(id, m_isServer);
  }
  bool canLocalSend_(uint64_t id) const {
    return !StreamID::uni(id) || localInitiated_(id, m_isServer);
  }
  bool peerOpenedStreamID_(uint64_t id) const {
    if (id > uint64_t(INT64_MAX) || localInitiated_(id, m_isServer))
      return false;
    Zi::StreamType::T type =
      StreamID::uni(id) ? Zi::StreamType::Simplex : Zi::StreamType::Duplex;
    return StreamID::ordinal(id) < peerLimit_(type).opened();
  }
  bool localOpenedStreamID_(uint64_t id) const {
    if (id > uint64_t(INT64_MAX) || !localInitiated_(id, m_isServer))
      return false;
    uint64_t opened = StreamID::uni(id) ? m_nextUniOrdinal : m_nextBidiOrdinal;
    return StreamID::ordinal(id) < opened;
  }
  bool closedStreamID_(uint64_t id) const {
    if (id > uint64_t(INT64_MAX)) return false;
    if (peerOpenedStreamID_(id)) return true;
    return localOpenedStreamID_(id);
  }

  bool validateMaxData_(const Frame &frame) const {
    return frame.type == FrameType::MaxData;
  }
  bool validateMaxStreams_(const Frame &frame) const {
    return frame.type == FrameType::MaxStreams &&
      frame.value <= MaxStreamCount;
  }
  bool validateMaxStreamData_(const Frame &frame, StreamRef &stream) {
    stream = nullptr;
    if (frame.type != FrameType::MaxStreamData ||
	frame.streamID > uint64_t(INT64_MAX) ||
	!canLocalSend_(frame.streamID))
      return false;
    stream = findStream(int64_t(frame.streamID));
    if (stream) return true;
    if (localInitiated_(frame.streamID, m_isServer)) return false;
    if (StreamID::uni(frame.streamID)) return false;
    stream = acceptPeerStream(frame.streamID);
    return stream;
  }
  bool validateDataBlocked_(const Frame &frame) const {
    return frame.type == FrameType::DataBlocked &&
      frame.value <= m_rxDataCredit.limit();
  }
  bool validateStreamDataBlocked_(const Frame &frame, StreamRef &stream) {
    stream = nullptr;
    if (frame.type != FrameType::StreamDataBlocked ||
	frame.streamID > uint64_t(INT64_MAX) ||
	!canPeerSend_(frame.streamID))
      return false;
    stream = findOrAccept_(frame.streamID);
    return stream && frame.value <= stream->rxCreditLimit();
  }
  bool validateStreamsBlocked_(const Frame &frame) const {
    return frame.type == FrameType::StreamsBlocked &&
      frame.value <= peerLimit_(frame.streamType).limit();
  }

  bool receiveStreamDataBlocked_(const Frame &frame) {
    if (frame.type == FrameType::StreamDataBlocked &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->resetReceived() || stream->rxComplete()) {
	  noteInvalidStreamActivity_(TransportError::StreamState, true);
	  return true;
	}
      } else if (closedStreamID_(frame.streamID) &&
	  canPeerSend_(frame.streamID)) {
	noteInvalidStreamActivity_(TransportError::StreamState, true);
	return true;
      }
    }
    StreamRef stream;
    if (!validateStreamDataBlocked_(frame, stream)) {
      noteInvalidStreamActivity_(TransportError::StreamState);
      return false;
    }
    if (!stream->receiveBlocked(frame)) {
      noteInvalidStreamActivity_(TransportError::FinalSize, false, true);
      return false;
    }
    return true;
  }
  bool blockedFrameNeeded_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::DataBlocked:
	return m_lastDataBlocked != frame.value;
      case FrameType::StreamDataBlocked: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return !stream || stream->lastStreamDataBlocked() != frame.value;
      }
      case FrameType::StreamsBlocked:
	return lastStreamsBlocked_(frame.streamType) != frame.value;
      default:
	return true;
    }
  }
  void noteBlockedQueued_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::DataBlocked:
	m_lastDataBlocked = frame.value;
	break;
      case FrameType::StreamDataBlocked:
	if (StreamRef stream = findStream(int64_t(frame.streamID)))
	  stream->lastStreamDataBlocked(frame.value);
	break;
      case FrameType::StreamsBlocked:
	lastStreamsBlocked_(frame.streamType) = frame.value;
	break;
      default:
	break;
    }
  }
  void noteControlDequeued_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::StreamsBlocked:
	lastStreamsBlocked_(frame.streamType) = uint64_t(-1);
	break;
      default:
	break;
    }
  }

  bool queuePendingControl_(PendingControl &slot, const ControlFrame &frame) {
    if (!frame) return false;
    if (slot.frame == frame) {
      if (slot.queued) return false;
      slot.queued = true;
      return true;
    }
    if (slot.frame) {
      switch (frame.type) {
	case FrameType::MaxData:
	case FrameType::MaxStreams:
	case FrameType::DataBlocked:
	case FrameType::StreamsBlocked:
	  if (slot.frame.value >= frame.value) return false;
	  break;
	case FrameType::HandshakeDone:
	  return false;
	default:
	  break;
      }
    }
    slot.frame = frame;
    slot.queued = true;
    return true;
  }
  void clearPendingControls_() {
    m_maxDataControl = {};
    m_dataBlockedControl = {};
    m_handshakeDoneControl = {};
    m_pathChallengeControl = {};
    for (unsigned i = 0; i < 2; ++i) {
      m_maxStreamsControl[i] = {};
      m_streamsBlockedControl[i] = {};
    }
    m_pathResponses.clean();
    auto iter = m_streams->iter();
    while (auto node = iter()) node->data().clearControls();
  }

  bool controlStillValid_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
	return m_maxDataControl.frame == frame &&
	  frame.value == m_rxDataCredit.limit();
      case FrameType::MaxStreamData: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::MaxStreams:
	return m_maxStreamsControl[streamTypeIndex_(frame.streamType)].frame ==
	  frame && frame.value == peerLimit_(frame.streamType).limit();
      case FrameType::DataBlocked:
	return m_dataBlockedControl.frame == frame &&
	  m_txDataCredit.blocked() &&
	  frame.value == m_txDataCredit.limit();
      case FrameType::StreamDataBlocked: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::StreamsBlocked:
	return m_streamsBlockedControl[streamTypeIndex_(frame.streamType)].frame ==
	  frame && queued_(frame.streamType) &&
	  frame.value == localLimit_(frame.streamType).limit();
      case FrameType::ResetStream: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::StopSending: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::PathChallenge:
	return m_pathChallengeControl.frame == frame &&
	  m_validatingPath.active &&
	  m_validatingPath.challenge.equals(byteSpan(
	    frame.payload, sizeof(frame.payload)));
      case FrameType::PathResponse:
	return true;
      case FrameType::HandshakeDone:
	return m_handshakeDoneControl.frame == frame;
      default:
	return false;
    }
  }

  void maybeExtendMaxData_() {
    uint64_t window = m_rxDataWindow ? m_rxDataWindow :
      m_transportParams.initialMaxData;
    if (!window || m_rxDataCredit.available() > window / 2) return;
    uint64_t maximum =
      m_rxDataCredit.used() > uint64_t(-1) - window ? uint64_t(-1) :
      m_rxDataCredit.used() + window;
    if (maximum <= m_rxDataCredit.limit()) return;
    m_rxDataCredit.extend(maximum);
    queueFlowUpdate_(
      FlowUpdate{FrameType::MaxData, 0, maximum, Zi::StreamType::Duplex});
  }
  void maybeExtendMaxStreamData_(const StreamRef &stream) {
    if (!stream || !stream->readOpen()) return;
    uint64_t window = initialStreamRxCredit_(uint64_t(stream->id()));
    if (!window || stream->rxCreditAvailable() > window / 2) return;
    uint64_t maximum =
      stream->rxCreditUsed() > uint64_t(-1) - window ? uint64_t(-1) :
      stream->rxCreditUsed() + window;
    if (maximum <= stream->rxCreditLimit()) return;
    stream->extendRxCredit(maximum);
    queueFlowUpdate_(FlowUpdate{
      FrameType::MaxStreamData, uint64_t(stream->id()), maximum,
      Zi::StreamType::Duplex});
  }
  void returnStreamCredit_(const StreamRef &stream) {
    if (!stream || stream->id() < 0 || stream->streamCreditReturned())
      return;
    uint64_t id = uint64_t(stream->id());
    if (localInitiated_(id, m_isServer)) return;
    if (!stream->rxComplete() && !stream->resetReceived()) return;
    if (!StreamID::uni(id) && !stream->finDequeued() && !stream->resetSent())
      return;
    Zi::StreamType::T type =
      StreamID::uni(id) ? Zi::StreamType::Simplex : Zi::StreamType::Duplex;
    StreamLimit &limit = peerLimit_(type);
    if (limit.limit() >= MaxStreamCount) return;
    uint64_t next = limit.limit() + 1;
    limit.extend(next);
    stream->markStreamCreditReturned();
    queueFlowUpdate_(FlowUpdate{FrameType::MaxStreams, 0, limit.limit(), type});
  }

  bool txClosed_(const Stream *stream) const {
    if (!canLocalSend_(uint64_t(stream->id()))) return true;
    if (stream->resetSent()) return stream->resetAckd();
    if (!stream->finSent()) return false;
    return stream->finDequeued() && !stream->txUnackdCount();
  }
  bool rxClosed_(const Stream *stream) const {
    if (!canPeerSend_(uint64_t(stream->id()))) return true;
    return stream->rxComplete() || stream->resetReceived();
  }
  bool streamReapable_(const Stream *stream) const {
    if (!stream || stream->id() < 0) return false;
    if (stream->txQueued() || stream->txRangeCount() ||
	stream->txBufferedBytes() || stream->txUnackdCount() ||
	stream->rxPending() || stream->rxQueued())
      return false;
    if (!rxClosed_(stream) || !txClosed_(stream)) return false;
    if (!streamCreditSettled_(stream))
      return false;
    return true;
  }
  bool reapStream_(Stream *stream) {
    if (!streamReapable_(stream)) return false;
    int64_t id = stream->id();
    return m_streams->del(id);
  }
  bool streamCreditSettled_(const Stream *stream) const {
    uint64_t id = uint64_t(stream->id());
    if (localInitiated_(id, m_isServer)) return true;
    if (stream->streamCreditReturned()) return true;
    Zi::StreamType::T type =
      StreamID::uni(id) ? Zi::StreamType::Simplex : Zi::StreamType::Duplex;
    return peerLimit_(type).limit() >= MaxStreamCount;
  }
  StreamRef newStream_(int64_t id) {
    auto node = new typename Streams::Node{impl(), id};
    StreamRef stream{node};
    stream->txCredit(initialStreamTxCredit_(uint64_t(id)));
    stream->rxCredit(initialStreamRxCredit_(uint64_t(id)));
    m_streams->addNode(node);
    impl()->streamOpen(
      stream, StreamID::server(uint64_t(id)) == m_isServer);
    return stream;
  }

  uint64_t initialStreamTxCredit_(uint64_t id) const {
    if (!m_crypto.peerTransportParamsReceived()) return 0;
    const auto &params = m_crypto.peerTransportParams();
    if (StreamID::uni(id)) return params.initialMaxStreamDataUni;
    bool local = StreamID::server(id) == m_isServer;
    return local ? params.initialMaxStreamDataBidiRemote :
      params.initialMaxStreamDataBidiLocal;
  }

  uint64_t initialStreamRxCredit_(uint64_t id) const {
    if (StreamID::uni(id) && StreamID::server(id) == m_isServer) return 0;
    if (StreamID::uni(id)) return m_transportParams.initialMaxStreamDataUni;
    bool local = StreamID::server(id) == m_isServer;
    return local ? m_transportParams.initialMaxStreamDataBidiLocal :
      m_transportParams.initialMaxStreamDataBidiRemote;
  }

  int64_t nextStreamID_(Zi::StreamType::T type) {
    uint64_t &ordinal =
      type == Zi::StreamType::Simplex ? m_nextUniOrdinal : m_nextBidiOrdinal;
    uint64_t id = (ordinal++ << 2) |
      (m_isServer ? 1U : 0U) |
      (type == Zi::StreamType::Simplex ? 2U : 0U);
    ZiAssert(id <= uint64_t(INT64_MAX), "Zquic", (id),
      "stream ID overflow id=" << id, return INT64_MAX);
    return int64_t(id);
  }

  // immutable
  App		*m_app = nullptr;
  bool		m_isServer = false;

  // Rx thread exclusive
  bool		m_closed = false;
  uint64_t	m_closeError = 0;
  FlowCredit	m_rxDataCredit;
  uint64_t	m_rxDataWindow = 0;
  StreamLimit	m_peerBidiLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_peerUniLimit{uint64_t(INT64_MAX) >> 2};
  StreamsRef	m_streams;

  // Tx thread exclusive
  FlowCredit	m_txDataCredit;
  uint64_t	m_nextBidiOrdinal = 0;
  uint64_t	m_nextUniOrdinal = 0;
  StreamLimit	m_localBidiLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_localUniLimit{uint64_t(INT64_MAX) >> 2};
  uint64_t	m_queuedBidi = 0;
  uint64_t	m_queuedUni = 0;
  bool		m_openQueuedPending[2] = {};
  uint64_t	m_lastDataBlocked = uint64_t(-1);
  uint64_t	m_lastStreamsBlockedBidi = uint64_t(-1);
  uint64_t	m_lastStreamsBlockedUni = uint64_t(-1);
  StreamQueue	m_streamQueue;
  PendingControl	m_maxDataControl;
  PendingControl	m_maxStreamsControl[2];
  PendingControl	m_dataBlockedControl;
  PendingControl	m_streamsBlockedControl[2];
  PendingControl	m_handshakeDoneControl;
  PendingControl	m_pathChallengeControl;
  PathResponses	m_pathResponses{ZmQueueParams{}.initial(PathResponseMax)};

private:
  bool rxInvoked_() const {
    ZiAssert(m_app && m_app->mx(), "Zquic", (),
      "QUIC link Rx access before app initialization", return false);
    return m_app->rxInvoked();
  }
  bool txInvoked_() const {
    ZiAssert(m_app && m_app->mx(), "Zquic", (),
      "QUIC link Tx access before app initialization", return false);
    return m_app->txInvoked();
  }

  template <typename Fn>
  void scheduleCxnTimer_(
    const char *name, ZuTime out, int mode, ZmScheduler::Timer *timer,
    Fn fn) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC timer schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC timer schedule before app initialization", return);
    if (m_timerTeardown) return;
    Zquic_DEBUG_LOG_(([name](auto &s) {
	s << "QUIC timer armed name=" << name;
      }));
    app()->mx()->run(app()->txThread(),
      [link = impl(), fn]() mutable { fn(link); },
      out, mode, timer);
  }

  void cancelTimer_(const char *, ZmScheduler::Timer *timer) {
    if (app() && app()->mx()) app()->mx()->del(timer);
  }

  bool ptoLevel_(CryptoLevel::T &level) const {
    bool have = false;
    ZuTime out;
    for (unsigned i = 0; i < 3; ++i) {
      auto l = CryptoLevel::T(i);
      if (m_txSpaceDiscarded[l]) continue;
      const auto &tx = m_txPkts[l];
      if (!tx.bytesInFlight() && !tx.retransmitPending()) continue;
      ZuTime deadline = ptoDeadline_(l);
      if (!have || deadline < out) {
	level = l;
	out = deadline;
	have = true;
      }
    }
    return have;
  }

  ZuTime ptoDeadline_(CryptoLevel::T level) const {
    ZuTime t = m_txPkts[level].latestAckSentTime();
    if (!*t) t = Zm::now();
    ZuTime delay = level == CryptoLevel::OneRTT ? maxAckDelay_() : ZuTime{0};
    return t + m_ptoBackoff.timeout(m_rtt, delay);
  }

  void resetLinkState_() {
    m_linkState = LinkState::Starting;
    m_runtimeCloseState = CloseState::Open;
    m_runtimeCloseError = 0;
    m_drainPTOs = 0;
    m_suspiciousStreamFrames = 0;
    m_suspiciousStreamClosed = false;
  }

  bool startHandshakeState_() {
    if (m_linkState != LinkState::Starting) return false;
    m_linkState = LinkState::Handshaking;
    return true;
  }

  bool establishState_() {
    if (m_linkState != LinkState::Handshaking) return false;
    m_linkState = LinkState::Established;
    return true;
  }

  bool closeLinkState_(uint64_t error = 0) {
    if (m_linkState == LinkState::Closed) return false;
    m_runtimeCloseError = error;
    m_runtimeCloseState = CloseState::Closing;
    m_linkState = LinkState::Closing;
    m_drainPTOs = 0;
    return true;
  }

  // Rx thread exclusive
  RuntimeRxDiag		m_rxDiag;
  Crypto		m_crypto;
  TransportParams	m_transportParams;
  CryptoStream		m_rxCrypto[3];
  ZiSockAddr		m_rxCryptoAddr[3];
  CxnID			m_initialDCID;
  CxnID			m_localSCID;
  CxnID			m_peerCID;
  ResetToken		m_peerResetToken;
  LocalCIDs		m_localCIDs;
  PeerCIDs		m_peerCIDs;
  uint64_t		m_rxLargestPN[3]{};
  AckManager		m_rxAcks;
  TrafficSecret		m_rxOldTrafficSecret;
  PktProtState		m_rxOldProt;
  TrafficSecret		m_rxNextTrafficSecret;
  PktProtState		m_rxNextProt;
  ZuTime		m_rxOldKeyDiscard;
  RttEstimator		m_rtt;
  PTOBackoff		m_ptoBackoff;
  // Connection-owned timers; callbacks run on Tx.
  ZmScheduler::Timer	m_ackDelayTimer;
  ZmScheduler::Timer	m_lossTimer;
  ZmScheduler::Timer	m_ptoTimer;
  ZmScheduler::Timer	m_idleTimer;
  ZmScheduler::Timer	m_closeTimer;
  ZmScheduler::Timer	m_keyDiscardTimer;
  // Active-path timers owned by this Link; callbacks run on Tx.
  ZmScheduler::Timer	m_pmtudTimer;
  ZmScheduler::Timer	m_pathTimer;
  bool			m_rxSpaceDiscarded[3]{};
  unsigned		m_handshakeStarted = 0;
  unsigned		m_established = 0;
  LinkState::T		m_linkState = LinkState::Starting;
  CloseState::T		m_runtimeCloseState = CloseState::Open;
  uint64_t		m_runtimeCloseError = 0;
  unsigned		m_drainPTOs = 0;
  uint64_t		m_peerRetirePriorTo = 0;
  unsigned		m_suspiciousStreamFrames = 0;
  bool			m_suspiciousStreamClosed = false;
  bool			m_rxKeyPhase = false;
  bool			m_rxOldKeyPhase = false;

  // Tx thread exclusive
  CryptoStream		m_txCrypto[3];
  CryptoTxPQueue m_txCryptoUnackd[3];
  TrafficSecret		m_txTrafficSecrets[3];
  PktProtState		m_txProt[3];
  ZmRef<ZiIOBuf>	m_coalesceInitial;
  ZiSockAddr		m_coalesceAddr;
  RuntimeTxDiag		m_txDiag;
  NewReno		m_congestion;
  uint64_t		m_txRuntimeGen = 0;
  bool			m_timerTeardown = false;
  uint64_t		m_txPN[3]{};
  uint64_t		m_txLargestAckd[3]{
    uint64_t(-1), uint64_t(-1), uint64_t(-1)};
  PktTxSpace		m_txPkts[3];
  AckSnapshot		m_txAck[3];
  AckECN		m_peerAckECN[3];
  Path			m_path;
  PathState		m_validatingPath;
  bool			m_txSpaceDiscarded[3]{};
  bool			m_coalesceLong = false;
  bool			m_txKeyPhase = false;
};

template <
  typename App, typename Impl, typename Stream_,
  typename TxBufAlloc_ = StreamTxBufAlloc<>>
class CliLink :
  public Link<App, Impl, TxBufAlloc_, Stream_> {
public:
  using Base = Link<App, Impl, TxBufAlloc_, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  static constexpr unsigned TLSBufSize = 64 * 1024;
  static constexpr unsigned RuntimePNLength = 2;
  static constexpr unsigned RuntimeCryptoChunk = 900;
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  template <typename, typename, typename> friend class Zquic::Stream;

  CliLink(App *app) : Base{app, false} { Base::initCryptoDelivery_(); }
  CliLink(App *app, Host server, uint16_t port) :
    Base{app, false}, m_server{ZuMv(server)}, m_port{port} {
    Base::initCryptoDelivery_();
  }
  ~CliLink() = default;

  void connect() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->connect_();
      return link;
    });
  }
  void connect(Host server, uint16_t port) {
    m_server = ZuMv(server);
    m_port = port;
    connect();
  }

  void disconnect() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client disconnect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->disconnect_();
      return link;
    });
  }
  template <typename Fn>
  void disconnect(Fn fn) {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client disconnect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), fn = ZuMv(fn)]() mutable {
      link->disconnect_(ZuMv(fn));
      return link;
    });
  }
  void disconnect_() { // direct call from within rx thread
    disconnect_([]() { });
  }
  template <typename Fn>
  void disconnect_(Fn fn) { // direct call from within rx thread
    closeCurrent_(true, ZuMv(fn));
    Base::resetTLS_();
  }
  void abort() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client abort before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->abort_();
      return link;
    });
  }
  template <typename Fn>
  void abort(Fn fn) {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client abort before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), fn = ZuMv(fn)]() mutable {
      link->abort_(ZuMv(fn));
      return link;
    });
  }
  void abort_() {
    abort_([]() { });
  }
  template <typename Fn>
  void abort_(Fn fn) {
    Base::close();
    closeEndpointDrained_(true, ZuMv(fn));
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }
  bool udpReady() const { return m_udpReady; }
  uint64_t udpReadyCount() const { return m_udpReadyCount; }
  bool ready() const { return m_udpReady; }
  bool established() const { return Base::runtimeEstablished_(); }
  Endpoint *endpoint() { return &m_endpoint; }
  const Endpoint *endpoint() const { return &m_endpoint; }
  Endpoint *cxn() const {
    return m_endpoint.connected() ? const_cast<Endpoint *>(&m_endpoint) : nullptr;
  }
  const ZiSockAddr &local() const { return m_endpoint.local(); }
  const ZiSockAddr &remote() const { return m_endpoint.remote(); }
  EndpointDiag cxnDiag() const { return endpointDiag(); }
  EndpointDiag endpointDiag() const {
    if (!app() || !app()->mx()) return m_endpoint.diag();
    auto mx = app()->mx();
    if (mx->invoked(mx->txThread())) return m_endpoint.diag();
    EndpointDiag diag;
    ZmSemaphore done;
    auto link = const_cast<CliLink *>(this)->impl();
    mx->txRun([link, &diag, &done]() mutable {
      diag = link->m_endpoint.diag();
      done.post();
    });
    done.wait();
    return diag;
  }
  RuntimeDiag runtimeDiag() const { return Base::runtimeDiag(); }
  PathDiag pathDiag() const { return Base::pathDiag(); }
  bool pathValidated() const { return Base::pathValidated_(); }
  unsigned activePathMaxUDP() const { return Base::activePathMaxUDP_(); }
  uint64_t pathAntiAmplification() const {
    return Base::pathAntiAmplification_();
  }
  const Crypto &crypto() const { return Base::crypto_(); }

  bool send(StreamRef stream, ZuCSpan payload, bool fin = true) {
    if (!app() || !app()->mx()) return false;
    if (!stream || (!payload.length() && !fin))
      return false;
    AsyncSendPayload payload_;
    payload_.length(payload.length());
    if (payload.length())
      memcpy(payload_.data(), payload.data(), payload.length());
    app()->txInvoke([
      link = ZmMkRef(this->impl()),
      stream = ZuMv(stream),
      payload = ZuMv(payload_),
      fin
    ]() mutable {
      link->send_(
	ZuMv(stream),
	ZuCSpan{payload.data(), payload.length()},
	fin);
    });
    return true;
  }
  bool send_(StreamRef stream, ZuCSpan payload, bool fin = true) {
    // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client send_ outside Tx thread", return false);
    if (Base::closed() || !stream || (!payload.length() && !fin))
      return false;
    if (payload.length()) {
      auto tx = stream->txStream_();
      tx << payload;
      tx.flush();
    }
    if (fin) stream->fin();
    queueTxFlush_();
    return true;
  }

  void pto_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client PTO outside Tx thread", return);
    if (Base::closed() || !m_endpoint.connected())
      return;
    if (flushTx_()) return;
    bool probe = Base::reclaimPTO_();
    if (retransmit_()) return;
    if (probe) (void)sendPingProbe_();
  }

  void queueRetransmit_() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client retransmit before app initialization", return);
    app()->txRun([link = ZmMkRef(impl())]() mutable {
      link->retransmit_();
    });
  }

  bool retransmit_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client retransmit outside Tx thread", return false);
    if (Base::closed() || !m_endpoint.connected())
      return false;
    SentFrameRef ref;
    bool sent = false;
    CryptoLevel::T level;
    constexpr unsigned MaxBatch = 16;
    unsigned processed = 0;
    while (processed < MaxBatch && Base::nextRetransmit_(level, ref)) {
      ++processed;
      PktBuild build;
      if (ref.kind == SentFrameKind::Crypto) {
	if (!Base::buildRetransmitCrypto_(level, build, ref)) continue;
	bool ok =
	  level == CryptoLevel::Initial ?
	    sendInitialPkt_(build, m_endpoint.remote(), {}, &ref, true) :
	  level == CryptoLevel::Handshake ?
	    sendHandshakePkt_(build, m_endpoint.remote(), {}, &ref, true) :
	    sendShortPkt_(build, m_endpoint.remote(), {}, &ref, true);
	if (!ok) {
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	sent = true;
	continue;
      }
      if (level != CryptoLevel::OneRTT || !Base::runtimeEstablished_())
	continue;
      if (!Base::congestionAllowance_()) {
	Base::requeueRetransmit_(level, ref);
	break;
      }
      if (ref.kind == SentFrameKind::Stream) {
	if (!Base::buildRetransmitStream_(build, ref)) {
#ifdef Zquic_DEBUG
	  ZiLOG(Info, "Zquic.retx", ([ref](auto &s) {
	    s << "stream retransmit build failed streamID=" <<
	      ref.streamID << " offset=" << ref.offset <<
	      " length=" << ref.length << " fin=" << int(ref.fin);
	  }));
#endif
	  continue;
	}
      } else if (!Base::buildRetransmitControl_(build, ref))
	continue;
      if (!sendShortPkt_(build, m_endpoint.remote(), {}, &ref, true)) {
#ifdef Zquic_DEBUG
	if (ref.kind == SentFrameKind::Stream)
	  ZiLOG(Info, "Zquic.retx", ([ref](auto &s) {
	    s << "stream retransmit send failed streamID=" <<
	      ref.streamID << " offset=" << ref.offset <<
	      " length=" << ref.length << " fin=" << int(ref.fin);
	  }));
#endif
	Base::requeueRetransmit_(level, ref);
	break;
      }
      sent = true;
    }
    if (sent) Base::schedulePTO_();
    if (sent && processed == MaxBatch) queueRetransmit_();
    return sent;
  }

  bool sendPingProbe_() {
    PktBuild build;
    if (!Base::buildPingProbe_(build)) return false;
    typename Base::TxPktRefs refs;
    return sendShortPkt_(
      build, m_endpoint.remote(), {},
      &refs, true);
  }

  bool sendPMTUDProbe_(ZiSockAddr addr) {
    return Base::sendPMTUDProbe_(
      ZuMv(addr),
      [this](PktBuild &build, ZiSockAddr addr_, unsigned size) {
	typename Base::TxPktRefs refs;
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true, size);
      });
  }

  void connect_() { // direct call from within rx thread
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    ZiIP ip = m_server;
    if (!ip || !m_port) {
      app()->error_(ZeEXCEPT(Error, "Zquic",
	([server = LogMsg{m_server}, port = m_port](auto &s) {
	  s << '"' << server << "\": invalid QUIC UDP peer port=" << port;
	})));
      connectFailed_0(true);
      return;
    }

    closeCurrent_(false, [link = ZmMkRef(impl()), ip]() mutable {
      link->connectOpen_(ip);
    });
  }

  void connectOpen_(ZiIP ip) {
    resetRuntimeState_();
    Base::resetRuntimeDiag_();

    if (!m_endpoint.init(app()->mx())) {
      connectFailed_0(false);
      return;
    }
    if (!m_endpoint.openUDP(
	PathMode::ClientConnected,
	ZiIP{}, 0, ip, m_port,
	Endpoint::DatagramFn{[link = impl()](Datagram d) mutable {
	  link->app()->rxRun([link, d = ZuMv(d)]() mutable {
	    link->received_(ZuMv(d));
	  });
	}},
	Endpoint::ReadyFn{[link = impl()](Endpoint *ep) mutable {
	  link->app()->rxRun([link, ep]() mutable {
	    link->endpointReady_(ep);
	  });
	}},
	Endpoint::FailFn{[link = impl()](bool transient) mutable {
	  link->app()->rxRun([link, transient]() mutable {
	    link->connectFailed_0(transient);
	  });
	}},
	Endpoint::DownFn{[link = impl()](Endpoint *ep) mutable {
	  link->app()->rxRun([link, ep]() mutable {
	    link->endpointDown_(ep);
	  });
	}},
	Endpoint::TxDrainedFn{[link = impl()]() mutable {
	  link->app()->txRun([link]() mutable {
	    link->txDrained_();
	  });
	}}))
      connectFailed_0(false);
  }

  void connectFailed(bool transient) {
    auto e = ZeEXCEPT(Error, "Zquic", ([transient](auto &s) {
      s << "QUIC UDP connect failed transient=" << transient;
    }));
    if (app())
      app()->error_(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

private:
  using InitialKeyDir = typename Base::InitialKeyDir;
  using RuntimeCID = typename Base::RuntimeCID;

  void closeCurrent_(bool notify) {
    closeCurrent_(notify, []() { });
  }

  template <typename Fn>
  void closeCurrent_(bool notify, Fn fn) {
    if (notify && app() && app()->mx() &&
	Base::runtimeEstablished_() &&
	m_endpoint.connected() && m_endpoint.remote()) {
      closeAfterConnectionClose_(notify, m_endpoint.remote(), ZuMv(fn));
      return;
    }
    closeEndpoint_(notify, ZuMv(fn));
  }

  template <typename Fn>
  void closeAfterConnectionClose_(bool notify, ZiSockAddr addr, Fn fn) {
    Base::close();
    app()->txInvoke(impl(), [
      link = impl(),
      notify,
      addr = ZuMv(addr),
      fn = ZuMv(fn)
    ]() mutable {
      (void)link->sendConnectionClose_(ZuMv(addr), true);
      link->app()->rxRun([link, notify, fn = ZuMv(fn)]() mutable {
	link->closeEndpointDrained_(notify, ZuMv(fn));
      });
      return link;
    });
  }

  void closeEndpoint_(bool notify) {
    closeEndpoint_(notify, []() { });
  }

  template <typename Fn>
  void closeEndpoint_(bool notify, Fn fn) {
    Base::teardownTimers([
      link = ZmMkRef(impl()), notify, fn = ZuMv(fn)
    ]() mutable {
      link->app()->rxRun([link, notify, fn = ZuMv(fn)]() mutable {
	link->closeEndpointDrained_(notify, ZuMv(fn));
      });
    });
  }

  void closeEndpointDrained_(bool notify) {
    closeEndpointDrained_(notify, []() { });
  }
  template <typename Fn>
  void closeEndpointDrained_(bool notify, Fn fn) {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC endpoint close drain outside Rx thread", return);
    m_udpReady = 0;
    m_notifyEndpointDown = notify;
    if (Base::closed()) Base::clearCallbacks_();
    Base::resetTLS_();
    m_endpoint.closeUDP(Endpoint::CloseFn{
	[link = impl(), fn = ZuMv(fn)]() mutable {
	  link->app()->rxRun([fn = ZuMv(fn)]() mutable {
	    fn();
	  });
	}});
  }

  void resetRuntimeState_() {
    Base::resetRuntime_();
    m_peerParamsValidated = false;
    m_bootstrap = {};
  }

  bool initRuntimeCrypto_() {
    if (!m_bootstrap.startRandom()) {
      Base::tlsFailure_();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.initialDCID(), m_bootstrap.initialSCID(),
      m_bootstrap.initialDCID());
    Base::configureLocalTransportParams_(app());
    if (!Base::deriveInitial_()) return false;
    if (!Base::initTLS_(CryptoConfig{
	false, false, app()->firstALPN(), app()->caPath(), {}, {},
	app()->keyLogPath(), m_server}))
      return false;
    return true;
  }

  bool startHandshake_() {
    if (Base::runtimeHandshakeStarted_()) return true;
    resetRuntimeState_();
    if (!initRuntimeCrypto_()) return false;
    if (!Base::startRuntimeHandshake_()) return false;
    return emitTLS_(0, {}, m_endpoint.remote());
  }

  void markEstablished_() {
    if (!Base::runtimeReadyToEstablish_())
      return;
    if (!m_peerParamsValidated) {
      if (!Base::validateServerTransportParams_(m_bootstrap)) {
	Base::tlsFailure_();
	app()->error_(ZeEXCEPT(Error, "Zquic",
	  "server QUIC transport parameters failed validation"));
	return;
      }
      m_peerParamsValidated = true;
    }
    Base::establishRuntime_();
    Base::validatePath_();
    Base::schedulePMTUD();
    impl()->connected(Zi::Connected{
      .transport = Zi::Transport::QUIC,
      .alpn = Base::negotiatedProtocol_(),
      .version = int(Version1)
    });
  }

  bool emitTLS_(size_t inEpoch, ZuCSpan input, ZiSockAddr addr) {
    return Base::template advanceTLS_<TLSBufSize>(
      inEpoch, input, ZuMv(addr),
      [this](
	  const uint8_t *data, unsigned len, const size_t offsets[5],
	  ZiSockAddr addr_) {
	return sendCryptoFlights_(data, len, offsets, ZuMv(addr_));
      },
      [this]() { markEstablished_(); },
      [](ZiSockAddr) { return true; });
  }

  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    typename Base::TxCryptoSnapshot txCrypto;
    Base::snapshotTxCrypto_(txCrypto);
    AsyncSendPayload payload;
    payload.length(len);
    if (len) memcpy(payload.data(), data, len);
    size_t offset0 = offsets[0];
    size_t offset1 = offsets[1];
    size_t offset2 = offsets[2];
    size_t offset3 = offsets[3];
    size_t offset4 = offsets[4];
    app()->txRun([
      link = ZmMkRef(impl()),
      txCrypto,
      payload = ZuMv(payload),
      offset0, offset1, offset2, offset3, offset4,
      addr = ZuMv(addr)
    ]() mutable {
      size_t offsets_[5] = {offset0, offset1, offset2, offset3, offset4};
      if (!link->installTxCrypto_(txCrypto)) return;
      (void)link->sendCryptoFlightsTx_(
	payload.data(), payload.length(), offsets_, ZuMv(addr));
    });
    return true;
  }

  bool sendCryptoFlightsTx_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client crypto send outside Tx thread", return false);
    Base::beginLongCoalesce_();
    bool ok = Base::sendCryptoFlights_(
      data, len, offsets, RuntimeCryptoChunk, ZuMv(addr),
      [this](
	  CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) {
	return sendCryptoPkt_(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
    ok = Base::flushCoalescedInitial_([this](auto buf, ZiSockAddr addr_) {
      return Base::sendPathPkt_(
	ZuMv(buf), ZuMv(addr_),
	[this](auto buf_, ZiSockAddr addr__) {
	  return m_endpoint.send(ZuMv(buf_), ZuMv(addr__));
	});
    }) && ok;
    Base::endLongCoalesce_();
    return ok;
  }

  bool sendCryptoPkt_(CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client crypto packet send outside Tx thread", return false);
    return Base::sendCryptoPkt_(
      level, frame, ZuMv(addr),
      [this](CryptoLevel::T level_, PktBuild &build, ZuCSpan frame_) {
	return buildPayload_(level_, build, frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendInitialPkt_(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendHandshakePkt_(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	  return sendShortPkt_(build, ZuMv(addr_), frame_);
      });
  }

  void txDrained_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Tx drain outside Tx thread", return);
    flushTx_();
  }

  void flushStreamWritable_(StreamRef stream) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client stream Tx flush outside Tx thread", return);
    if (!stream || stream->id() < 0) return;
    Base::streamWritable_(stream);
    if (!flushTx_()) queueTxFlush_();
  }

  void queueTxFlush_() {
    app()->txRun([link = ZmMkRef(impl())]() mutable {
      link->flushTx_();
    });
  }
  void queueTxFlush_(ZiSockAddr addr) {
    app()->txRun([
      link = ZmMkRef(impl()),
      addr = ZuMv(addr)
    ]() mutable {
      link->flushTx_(ZuMv(addr));
    });
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
    if (Base::closed() || !Base::runtimeEstablished_() ||
	!m_endpoint.remote())
      return false;
    return flushTx_(m_endpoint.remote());
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
    if (!addr) return flushTx_();
    if (Base::closed()) return false;
    flushPendingAcks_(addr);
    if (!Base::runtimeEstablished_()) return false;
    bool sent = Base::flushControlAndStreams_(
      addr,
      [this](PktBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const typename Base::TxPktRefs &refs) {
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true);
      });
    sent |= sendPMTUDProbe_(ZuMv(addr));
    return sent;
  }

  bool sendCryptoPkt_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client crypto packet send outside Tx thread", return false);
    return Base::sendCryptoPkt_(
      level, prefix, payload, ref, ZuMv(addr),
      [this](
	  CryptoLevel::T level_, PktBuild &build,
	  ZuCSpan prefix_, ZuCSpan payload_) {
	return buildPayload_(level_, build, prefix_, payload_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendInitialPkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendHandshakePkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendShortPkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      });
  }

  bool appendPendingAck_(
    CryptoLevel::T level, PktBuild &build) {
    return Base::appendPendingAck_(level, build);
  }

  bool buildPayload_(CryptoLevel::T level, PktBuild &build, ZuCSpan frame) {
    return Base::buildPayload_(level, build, frame);
  }

  bool buildPayload_(
    CryptoLevel::T level, PktBuild &build,
    ZuCSpan prefix, ZuCSpan payload) {
    return Base::buildPayload_(level, build, prefix, payload);
  }

  bool sendInitialPkt_(ZuCSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(CryptoLevel::Initial, payload, frame)) return false;
    return sendInitialPkt_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_endpoint.connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return Base::sendProtInitialPkt_(
      InitialKeyDir::Client, RuntimeCID::Initial, RuntimeCID::Local,
      Base::txPNLength_(CryptoLevel::Initial), true, payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return m_endpoint.allocTxPkt(); },
      [this](auto buf, ZiSockAddr addr_) {
	return Base::holdInitialForCoalesce_(
	  ZuMv(buf), ZuMv(addr_),
	  [this](auto buf_, ZiSockAddr addr__) {
	    return Base::sendPathPkt_(
	      ZuMv(buf_), ZuMv(addr__),
	      [this](auto buf__, ZiSockAddr addr___) {
		return m_endpoint.send(ZuMv(buf__), ZuMv(addr___));
	      });
	  });
      });
  }

  bool sendHandshakePkt_(ZuCSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Handshake send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(CryptoLevel::Handshake, payload, frame)) return false;
    return sendHandshakePkt_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_endpoint.connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return Base::sendProtHandshakePkt_(
      RuntimeCID::Peer, RuntimeCID::Local,
      Base::txPNLength_(CryptoLevel::Handshake),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return m_endpoint.allocTxPkt(); },
      [this](auto buf, ZiSockAddr addr_) {
	return Base::sendHandshakeCoalesced_(
	  ZuMv(buf), ZuMv(addr_),
	  [this](auto buf_, ZiSockAddr addr__) {
	    return Base::sendPathPkt_(
	      ZuMv(buf_), ZuMv(addr__),
	      [this](auto buf__, ZiSockAddr addr___) {
		return m_endpoint.send(ZuMv(buf__), ZuMv(addr___));
	      });
	  });
      });
  }

  bool sendShortPkt_(ZuCSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Short send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(CryptoLevel::OneRTT, build, payload)) return false;
    return sendShortPkt_(build, ZuMv(addr), payload);
  }

  bool sendShortPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    unsigned pmtudSize = 0) {
    if (!m_endpoint.connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return sendShortPkt_(payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting, pmtudSize);
  }

  bool sendShortPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const typename Base::TxPktRefs *recordRefs, bool ackEliciting,
    unsigned pmtudSize = 0) {
    if (!m_endpoint.connected()) return false;
    return Base::sendProtShortPkt_(
      RuntimeCID::Peer, Base::txPNLength_(CryptoLevel::OneRTT),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return m_endpoint.allocTxPkt(); },
      [this, pmtudSize](auto buf, ZiSockAddr addr_) {
	if (pmtudSize)
	  return Base::sendPathProbePkt_(
	    ZuMv(buf), ZuMv(addr_),
	    [this](auto buf_, ZiSockAddr addr__) {
	      return m_endpoint.send(ZuMv(buf_), ZuMv(addr__));
	    });
	return Base::sendPathPkt_(
	  ZuMv(buf), ZuMv(addr_),
	  [this](auto buf_, ZiSockAddr addr__) {
	    return m_endpoint.send(ZuMv(buf_), ZuMv(addr__));
	  });
      },
      pmtudSize);
  }

  bool sendConnectionClose_(ZiSockAddr addr, bool closing = false) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client CONNECTION_CLOSE outside Tx thread", return false);
    if ((!closing && Base::closed()) || !Base::runtimeEstablished_() ||
	!m_endpoint.connected() || !addr)
      return false;
    PktBuild build;
    int n = FrameCodec::writeConnectionClose(
      build.scratch(), build.scratchAvail(), 0);
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    return sendShortPkt_(build, ZuMv(addr), {});
  }

  bool sendQueuedStreamPkt_(StreamRef stream, ZiSockAddr addr) {
    return Base::sendQueuedStreamPkt_(
      ZuMv(stream), ZuMv(addr),
      [this](PktBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const typename Base::TxPktRefs &refs) {
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true);
      });
  }

  bool flushPendingAck_(CryptoLevel::T level, ZiSockAddr addr) {
    PktBuild build;
    build.reset();
    if (!Base::appendPendingAck_(level, build, true)) return false;
    if (!build.bytes()) return true;
    if (level == CryptoLevel::Initial)
      return sendInitialPkt_(build, ZuMv(addr), {});
    if (level == CryptoLevel::Handshake)
      return sendHandshakePkt_(build, ZuMv(addr), {});
    return sendShortPkt_(build, ZuMv(addr), {});
  }

  bool flushPendingAcks_(ZiSockAddr addr) {
    return flushPendingAck_(CryptoLevel::Initial, addr) &&
      flushPendingAck_(CryptoLevel::Handshake, addr) &&
      flushPendingAck_(CryptoLevel::OneRTT, ZuMv(addr));
  }

  void received_(Datagram d) {
    ZiSockAddr addr = d.addr;
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong_(d_, packetOffset, packetLen);
      },
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort_(d_, packetOffset, packetLen);
      });
    queueTxFlush_(ZuMv(addr));
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtLongPkt_(
      InitialKeyDir::Server, d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &) {
	Base::setPeerCIDFromHdrSCID_(h);
	return true;
      },
      [this](
	  CryptoLevel::T level, uint64_t pn, ZuCSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf) {
	return consumeFrames_(level, pn, frames, ZuMv(addr), packetBuf);
      });
  }

  bool receivedShort_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtShortPkt_(
      d, packetOffset, packetLen,
      [this](
	  CryptoLevel::T level, uint64_t pn, ZuCSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf) {
	return consumeFrames_(level, pn, frames, ZuMv(addr), packetBuf);
      });
  }

  bool consumeFrames_(
    CryptoLevel::T level, uint64_t pn, ZuCSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf) {
    ZiSockAddr peer = addr;
    bool ok = Base::consumeProtFrames_(
      level, pn, frames, ZuMv(addr), packetBuf,
      [this](size_t epoch, ZuCSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      },
      [this](
	  CryptoLevel::T level_, const Frame &frame, ZiSockAddr addr_) {
	return handleControlFrame_(level_, frame, ZuMv(addr_));
      });
    if (ok && level == CryptoLevel::OneRTT && Base::runtimeEstablished_())
      Base::observePathRx_(m_endpoint.local(), ZuMv(peer));
    return ok;
  }

  bool handleControlFrame_(
    CryptoLevel::T, const Frame &frame, ZiSockAddr addr) {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	queueTxFlush_();
	return true;
      case FrameType::PathChallenge: {
	Base::queuePathResponse_(frame.payload);
	queueTxFlush_(ZuMv(addr));
	return true;
      }
      case FrameType::PathResponse:
	Base::receivePathResponse_(frame.payload);
	return true;
      case FrameType::HandshakeDone:
	return true;
	      case FrameType::ConnectionClose:
	      case FrameType::ApplicationClose:
		Base::closeRuntime_(frame.errorCode);
		Base::transportClose_(frame.type, frame.errorCode);
		closeCurrent_(true);
		return true;
      default:
	return true;
    }
  }

  void dataBlocked_(uint64_t maximum) {
    Base::queueBlocked_(FrameType::DataBlocked, 0, maximum);
    impl()->flowBlocked(
      FrameType::DataBlocked, 0, Zi::StreamType::Duplex, maximum);
    queueTxFlush_();
  }
  void streamDataBlocked_(uint64_t streamID, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamDataBlocked, streamID, maximum);
    impl()->flowBlocked(
      FrameType::StreamDataBlocked, streamID,
      Zi::StreamType::Duplex, maximum);
    queueTxFlush_();
  }
  void streamsBlocked_(Zi::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamsBlocked, 0, maximum, type);
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
    queueTxFlush_();
  }

  void endpointReady_(Endpoint *ep) {
    if (ep != &m_endpoint) return;
    m_notifyEndpointDown = true;
    ++m_udpReadyCount;
    Base::endpointReady_();
    Base::initClientPath_(ep->local(), ep->remote());
    startHandshake_();
    m_udpReady = 1;
  }

  void endpointDown_(Endpoint *ep) {
    if (ep != &m_endpoint) return;
    m_udpReady = 0;
    bool notify = m_notifyEndpointDown;
    m_notifyEndpointDown = true;
    resetRuntimeState_();
    if (notify) {
      impl()->disconnected();
    }
  }

  void connectFailed_0(bool transient) {
    Base::endpointFailure_();
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect failure before app initialization", return);
    app()->rxRun([link = ZmMkRef(impl()), transient]() {
      link->connectFailed(transient);
    });
  }

  // Rx thread exclusive
  Endpoint		m_endpoint;
  Host			m_server;
  uint16_t		m_port = 0;
  ClientBootstrap	m_bootstrap;
  bool			m_peerParamsValidated = false;
  bool			m_notifyEndpointDown = true;

  // shared
  ZmAtomic<uint64_t>	m_udpReadyCount = 0;
  ZmAtomic<unsigned>	m_udpReady = 0;
};

template <
  typename App, typename Impl, typename Stream_,
  typename TxBufAlloc_ = StreamTxBufAlloc<>>
class SrvLink :
  public Link<App, Impl, TxBufAlloc_, Stream_> {
public:
  using Base = Link<App, Impl, TxBufAlloc_, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  static constexpr unsigned TLSBufSize = 64 * 1024;
  static constexpr unsigned RuntimePNLength = 2;
  static constexpr unsigned RuntimeCryptoChunk = 900;
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  template <typename, typename, typename> friend class Zquic::Stream;

  template <typename, typename> friend class Server;

  SrvLink(App *app) : Base{app, true} { Base::initCryptoDelivery_(); }

  bool established() const { return Base::runtimeEstablished_(); }
  RuntimeDiag runtimeDiag() const { return Base::runtimeDiag(); }
  PathDiag pathDiag() const { return Base::pathDiag(); }
  bool pathValidated() const { return Base::pathValidated_(); }
  unsigned activePathMaxUDP() const { return Base::activePathMaxUDP_(); }
  uint64_t pathAntiAmplification() const {
    return Base::pathAntiAmplification_();
  }
  const Crypto &crypto() const { return Base::crypto_(); }
  const ZiSockAddr &peer() const { return m_peerAddr; }

  bool send(StreamRef stream, ZuCSpan payload, bool fin = true) {
    if (!app() || !app()->mx()) return false;
    if (!stream || (!payload.length() && !fin))
      return false;
    AsyncSendPayload payload_;
    payload_.length(payload.length());
    if (payload.length())
      memcpy(payload_.data(), payload.data(), payload.length());
    app()->txInvoke([
      link = ZmMkRef(this->impl()),
      stream = ZuMv(stream),
      payload = ZuMv(payload_),
      fin
    ]() mutable {
      link->send_(
	ZuMv(stream),
	ZuCSpan{payload.data(), payload.length()},
	fin);
    });
    return true;
  }
  bool send_(StreamRef stream, ZuCSpan payload, bool fin = true) {
    // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server send_ outside Tx thread", return false);
    if (Base::closed() || !stream || (!payload.length() && !fin))
      return false;
    if (payload.length()) {
      auto tx = stream->txStream_();
      tx << payload;
      tx.flush();
    }
    if (fin) stream->fin();
    queueTxFlush_();
    return true;
  }

  void pto_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server PTO outside Tx thread", return);
    if (Base::closed() || !m_peerAddr)
      return;
    if (flushTx_()) return;
    bool probe = Base::reclaimPTO_();
    if (retransmit_()) return;
    if (probe) (void)sendPingProbe_();
  }

  void queueRetransmit_() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC server retransmit before app initialization", return);
    app()->txRun([link = ZmMkRef(impl())]() mutable {
      link->retransmit_();
    });
  }

  bool retransmit_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server retransmit outside Tx thread", return false);
    if (Base::closed() || !m_peerAddr)
      return false;
    SentFrameRef ref;
    bool sent = false;
    CryptoLevel::T level;
    constexpr unsigned MaxBatch = 16;
    unsigned processed = 0;
    while (processed < MaxBatch && Base::nextRetransmit_(level, ref)) {
      ++processed;
      PktBuild build;
      if (ref.kind == SentFrameKind::Crypto) {
	if (!Base::buildRetransmitCrypto_(level, build, ref)) continue;
	bool ok =
	  level == CryptoLevel::Initial ?
	    sendInitialPkt_(build, m_peerAddr, {}, &ref, true) :
	  level == CryptoLevel::Handshake ?
	    sendHandshakePkt_(build, m_peerAddr, {}, &ref, true) :
	    sendShortPkt_(build, m_peerAddr, {}, &ref, true);
	if (!ok) {
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	sent = true;
	continue;
      }
      if (level != CryptoLevel::OneRTT || !Base::runtimeEstablished_())
	continue;
      if (!Base::congestionAllowance_()) {
	Base::requeueRetransmit_(level, ref);
	break;
      }
      if (ref.kind == SentFrameKind::Stream) {
	if (!Base::buildRetransmitStream_(build, ref)) {
#ifdef Zquic_DEBUG
	  ZiLOG(Info, "Zquic.retx", ([ref](auto &s) {
	    s << "stream retransmit build failed streamID=" <<
	      ref.streamID << " offset=" << ref.offset <<
	      " length=" << ref.length << " fin=" << int(ref.fin);
	  }));
#endif
	  continue;
	}
      } else if (!Base::buildRetransmitControl_(build, ref))
	continue;
      if (!sendShortPkt_(build, m_peerAddr, {}, &ref, true)) {
#ifdef Zquic_DEBUG
	if (ref.kind == SentFrameKind::Stream)
	  ZiLOG(Info, "Zquic.retx", ([ref](auto &s) {
	    s << "stream retransmit send failed streamID=" <<
	      ref.streamID << " offset=" << ref.offset <<
	      " length=" << ref.length << " fin=" << int(ref.fin);
	  }));
#endif
	Base::requeueRetransmit_(level, ref);
	break;
      }
      sent = true;
    }
    if (sent) Base::schedulePTO_();
    if (sent && processed == MaxBatch) queueRetransmit_();
    return sent;
  }

  bool sendPingProbe_() {
    PktBuild build;
    if (!Base::buildPingProbe_(build)) return false;
    typename Base::TxPktRefs refs;
    return sendShortPkt_(
      build, m_peerAddr, {}, &refs, true);
  }

  bool sendPMTUDProbe_(ZiSockAddr addr) {
    return Base::sendPMTUDProbe_(
      ZuMv(addr),
      [this](PktBuild &build, ZiSockAddr addr_, unsigned size) {
	typename Base::TxPktRefs refs;
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true, size);
      });
  }

  void close(uint64_t errorCode = 0) {
    if (Base::closed()) return;
    Base::close(errorCode);
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC server close before app initialization", return);
    app()->rxInvoke([link = ZmMkRef(impl()), errorCode]() {
      link->close_(errorCode);
    });
  }

private:
  using InitialKeyDir = typename Base::InitialKeyDir;
  using RuntimeCID = typename Base::RuntimeCID;

  void close_(
    uint64_t errorCode = 0, bool notify = false, bool deferRelease = false) {
    Base::closeRuntime_(errorCode);
    if (notify) impl()->disconnected();
    if (!app()) return;
    if (deferRelease) {
      m_releaseDeferred = true;
      return;
    }
    static_cast<Server<App, Impl> *>(app())->releaseLink_(impl());
  }

  void resetRuntimeState_() {
    Base::resetRuntime_();
    m_peerAddr.null();
    m_bootstrap = {};
    m_handshakeDoneSent = 0;
  }

  bool initRuntimeCrypto_(const LongHdr &h, unsigned datagramLen) {
    if (h.type != PktType::Initial ||
	!m_bootstrap.acceptInitial(h, datagramLen)) {
      Base::packetParseFailure_();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.originalDCID(), m_bootstrap.localInitialSCID(),
      m_bootstrap.clientInitialSCID());
    Base::addLocalCID_(
      m_bootstrap.localInitialSCID(), 0, m_bootstrap.statelessResetToken());
    if (!Base::loadServerTransportParams_(m_bootstrap)) return false;
    Base::configureLocalTransportParams_(app());
    if (!Base::deriveInitial_()) return false;
    if (!Base::initTLS_(CryptoConfig{
	true, false, app()->firstALPN(), {}, app()->certPath(), app()->keyPath(),
	app()->keyLogPath(), {}}))
      return false;
    if (!Base::startRuntimeHandshake_()) return false;
    return true;
  }

  void markEstablished_() {
    if (!Base::runtimeReadyToEstablish_())
      return;
    if (!Base::validateClientTransportParams_()) {
      Base::tlsFailure_();
      app()->error_(ZeEXCEPT(Error, "Zquic",
	"client QUIC transport parameters failed validation"));
      return;
    }
    Base::establishRuntime_();
    Base::validatePath_();
    Base::schedulePMTUD();
    impl()->connected(Zi::Connected{
      .transport = Zi::Transport::QUIC,
      .alpn = Base::negotiatedProtocol_(),
      .version = int(Version1)
    });
  }

  bool emitTLS_(size_t inEpoch, ZuCSpan input, ZiSockAddr addr) {
    return Base::template advanceTLS_<TLSBufSize>(
      inEpoch, input, ZuMv(addr),
      [this](
	  const uint8_t *data, unsigned len, const size_t offsets[5],
	  ZiSockAddr addr_) {
	return sendCryptoFlights_(data, len, offsets, ZuMv(addr_));
      },
      [this]() { markEstablished_(); },
      [this](ZiSockAddr addr_) {
	if (Base::runtimeEstablished_() && !m_handshakeDoneSent)
	  app()->txRun([link = ZmMkRef(impl()), addr = ZuMv(addr_)]() mutable {
	    (void)link->sendHandshakeDone_(ZuMv(addr));
	  });
	return true;
      });
  }

  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    typename Base::TxCryptoSnapshot txCrypto;
    Base::snapshotTxCrypto_(txCrypto);
    AsyncSendPayload payload;
    payload.length(len);
    if (len) memcpy(payload.data(), data, len);
    size_t offset0 = offsets[0];
    size_t offset1 = offsets[1];
    size_t offset2 = offsets[2];
    size_t offset3 = offsets[3];
    size_t offset4 = offsets[4];
    app()->txRun([
      link = ZmMkRef(impl()),
      txCrypto,
      payload = ZuMv(payload),
      offset0, offset1, offset2, offset3, offset4,
      addr = ZuMv(addr)
    ]() mutable {
      size_t offsets_[5] = {offset0, offset1, offset2, offset3, offset4};
      if (!link->installTxCrypto_(txCrypto)) return;
      (void)link->sendCryptoFlightsTx_(
	payload.data(), payload.length(), offsets_, ZuMv(addr));
    });
    return true;
  }

  bool sendCryptoFlightsTx_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server crypto send outside Tx thread", return false);
    Base::beginLongCoalesce_();
    bool ok = Base::sendCryptoFlights_(
      data, len, offsets, RuntimeCryptoChunk, ZuMv(addr),
      [this](
	  CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) {
	return sendCryptoPkt_(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
    ok = Base::flushCoalescedInitial_([this](auto buf, ZiSockAddr addr_) {
      return Base::sendPathPktApp_(
	ZuMv(buf), ZuMv(addr_),
	[this](auto buf_, ZiSockAddr addr__, bool &sent) {
	  return sendPktPath_(ZuMv(buf_), ZuMv(addr__), sent);
	});
    }) && ok;
    Base::endLongCoalesce_();
    return ok;
  }

  bool sendCryptoPkt_(CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server crypto packet send outside Tx thread", return false);
    return Base::sendCryptoPkt_(
      level, frame, ZuMv(addr),
      [this](CryptoLevel::T level_, PktBuild &build, ZuCSpan frame_) {
	return buildPayload_(level_, build, frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendInitialPkt_(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendHandshakePkt_(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendShortPkt_(build, ZuMv(addr_), frame_);
      });
  }

  bool sendPktPath_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, bool &sent) {
    sent = false;
    if (!app()->sendPkt(buf)) return true;
    sent = static_cast<Server<App, Impl> *>(app())->m_endpoint.send(
      ZuMv(buf), ZuMv(addr));
    return sent;
  }

  void txDrained_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Tx drain outside Tx thread", return);
    flushTx_();
  }

  void flushStreamWritable_(StreamRef stream) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server stream Tx flush outside Tx thread", return);
    if (!stream || stream->id() < 0) return;
    Base::streamWritable_(stream);
    if (!flushTx_()) queueTxFlush_();
  }

  void queueTxFlush_() {
    app()->txRun([link = ZmMkRef(impl())]() mutable {
      link->flushTx_();
    });
  }
  void queueTxFlush_(ZiSockAddr addr) {
    app()->txRun([
      link = ZmMkRef(impl()),
      addr = ZuMv(addr)
    ]() mutable {
      link->flushTx_(ZuMv(addr));
    });
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server flush outside Tx thread", return false);
    if (Base::closed() || !Base::runtimeEstablished_() || !m_peerAddr)
      return false;
    return flushTx_(m_peerAddr);
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server flush outside Tx thread", return false);
    if (!addr) return flushTx_();
    if (Base::closed()) return false;
    flushPendingAcks_(addr);
    if (!Base::runtimeEstablished_()) return false;
    bool sent = Base::flushControlAndStreams_(
      addr,
      [this](PktBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const typename Base::TxPktRefs &refs) {
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true);
      });
    sent |= sendPMTUDProbe_(ZuMv(addr));
    return sent;
  }

  bool sendCryptoPkt_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server crypto packet send outside Tx thread", return false);
    return Base::sendCryptoPkt_(
      level, prefix, payload, ref, ZuMv(addr),
      [this](
	  CryptoLevel::T level_, PktBuild &build,
	  ZuCSpan prefix_, ZuCSpan payload_) {
	return buildPayload_(level_, build, prefix_, payload_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendInitialPkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendHandshakePkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendShortPkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      });
  }

  bool appendPendingAck_(CryptoLevel::T level, PktBuild &build) {
    return Base::appendPendingAck_(level, build);
  }

  bool buildPayload_(CryptoLevel::T level, PktBuild &build, ZuCSpan frame) {
    return Base::buildPayload_(level, build, frame);
  }

  bool buildPayload_(
    CryptoLevel::T level, PktBuild &build,
    ZuCSpan prefix, ZuCSpan payload) {
    return Base::buildPayload_(level, build, prefix, payload);
  }

  bool sendInitialPkt_(ZuCSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(CryptoLevel::Initial, payload, frame)) return false;
    return sendInitialPkt_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return Base::sendProtInitialPkt_(
      InitialKeyDir::Server, RuntimeCID::Peer, RuntimeCID::Local,
      Base::txPNLength_(CryptoLevel::Initial), false, payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs](auto buf, ZiSockAddr addr_) {
	if (recordRefs)
	  for (unsigned i = 0; i < recordRefs->count(); ++i)
	    if (!app()->sendFrame((*recordRefs)[i])) return true;
	return Base::holdInitialForCoalesce_(
	  ZuMv(buf), ZuMv(addr_),
	  [this](auto buf_, ZiSockAddr addr__) {
	    return Base::sendPathPktApp_(
	      ZuMv(buf_), ZuMv(addr__),
	      [this](auto buf__, ZiSockAddr addr___, bool &sent) {
		return sendPktPath_(ZuMv(buf__), ZuMv(addr___), sent);
	      });
	  });
      });
  }

  bool sendHandshakePkt_(ZuCSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Handshake send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(CryptoLevel::Handshake, payload, frame)) return false;
    return sendHandshakePkt_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return Base::sendProtHandshakePkt_(
      RuntimeCID::Peer, RuntimeCID::Local,
      Base::txPNLength_(CryptoLevel::Handshake),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this](auto buf, ZiSockAddr addr_) {
	return Base::sendHandshakeCoalesced_(
	  ZuMv(buf), ZuMv(addr_),
	  [this](auto buf_, ZiSockAddr addr__) {
	    return Base::sendPathPktApp_(
	      ZuMv(buf_), ZuMv(addr__),
	      [this](auto buf__, ZiSockAddr addr___, bool &sent) {
		return sendPktPath_(ZuMv(buf__), ZuMv(addr___), sent);
	      });
	  });
      });
  }

  bool sendShortPkt_(ZuCSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Short send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(CryptoLevel::OneRTT, build, payload)) return false;
    return sendShortPkt_(build, ZuMv(addr), payload);
  }

  bool sendShortPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    unsigned pmtudSize = 0) {
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return sendShortPkt_(payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting, pmtudSize);
  }

  bool sendShortPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const typename Base::TxPktRefs *recordRefs, bool ackEliciting,
    unsigned pmtudSize = 0) {
    return Base::sendProtShortPkt_(
      RuntimeCID::Peer, Base::txPNLength_(CryptoLevel::OneRTT),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs, pmtudSize](auto buf, ZiSockAddr addr_) {
	if (recordRefs)
	  for (unsigned i = 0; i < recordRefs->count(); ++i)
	    if (!app()->sendFrame((*recordRefs)[i])) return true;
	if (pmtudSize)
	  return Base::sendPathProbePktApp_(
	    ZuMv(buf), ZuMv(addr_),
	    [this](auto buf_, ZiSockAddr addr__, bool &sent) {
	      return sendPktPath_(ZuMv(buf_), ZuMv(addr__), sent);
	    });
	return Base::sendPathPktApp_(
	  ZuMv(buf), ZuMv(addr_),
	  [this](auto buf_, ZiSockAddr addr__, bool &sent) {
	    return sendPktPath_(ZuMv(buf_), ZuMv(addr__), sent);
	  });
      },
      pmtudSize);
  }

  bool sendHandshakeDone_(ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server HANDSHAKE_DONE outside Tx thread", return false);
    Base::queueHandshakeDone_();
    if (!Base::flushControlAndStreams_(
	ZuMv(addr),
	[this](PktBuild &build) {
	  return appendPendingAck_(CryptoLevel::OneRTT, build);
	},
	[this](
	    PktBuild &build, ZiSockAddr addr_,
	    const typename Base::TxPktRefs &refs) {
	  return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true);
	}))
      return false;
    m_handshakeDoneSent = 1;
    Base::handshakeDoneTx_();
    return true;
  }

  bool sendQueuedStreamPkt_(StreamRef stream, ZiSockAddr addr) {
    return Base::sendQueuedStreamPkt_(
      ZuMv(stream), ZuMv(addr),
      [this](PktBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const SentFrameRef &ref) {
	return sendShortPkt_(build, ZuMv(addr_), {}, &ref, true);
      });
  }

  bool flushPendingAck_(CryptoLevel::T level, ZiSockAddr addr) {
    PktBuild build;
    build.reset();
    if (!Base::appendPendingAck_(level, build, true)) return false;
    if (!build.bytes()) return true;
    if (level == CryptoLevel::Initial)
      return sendInitialPkt_(build, ZuMv(addr), {});
    if (level == CryptoLevel::Handshake)
      return sendHandshakePkt_(build, ZuMv(addr), {});
    return sendShortPkt_(build, ZuMv(addr), {});
  }

  bool flushPendingAcks_(ZiSockAddr addr) {
    return flushPendingAck_(CryptoLevel::Initial, addr) &&
      flushPendingAck_(CryptoLevel::Handshake, addr) &&
      flushPendingAck_(CryptoLevel::OneRTT, ZuMv(addr));
  }

  bool received_(Datagram d) {
    ZiSockAddr addr = d.addr;
    if (!Base::runtimeHandshakeStarted_() && d.buf)
      Base::initServerPath_(app()->local(), d.addr);
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong_(d_, packetOffset, packetLen);
      },
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort_(d_, packetOffset, packetLen);
      });
    if (m_releaseDeferred) return false;
    queueTxFlush_(ZuMv(addr));
    return true;
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtLongPkt_(
      InitialKeyDir::Client, d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &d_) {
	if (!Base::runtimeHandshakeStarted_()) {
	  m_peerAddr = d_.addr;
	  if (!initRuntimeCrypto_(h, d_.buf->length)) return false;
	}
	return true;
      },
      [this](
	  CryptoLevel::T level, uint64_t pn, ZuCSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf) {
	return consumeFrames_(level, pn, frames, ZuMv(addr), packetBuf);
      });
  }

  bool receivedShort_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtShortPkt_(
      d, packetOffset, packetLen,
      [this](
	  CryptoLevel::T level, uint64_t pn, ZuCSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf) {
	return consumeFrames_(level, pn, frames, ZuMv(addr), packetBuf);
      });
  }

  bool consumeFrames_(
    CryptoLevel::T level, uint64_t pn, ZuCSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf) {
    ZiSockAddr peer = addr;
    bool ok = Base::consumeProtFrames_(
      level, pn, frames, ZuMv(addr), packetBuf,
      [this](size_t epoch, ZuCSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      },
      [this](
	  CryptoLevel::T level_, const Frame &frame, ZiSockAddr addr_) {
	return handleControlFrame_(level_, frame, ZuMv(addr_));
      });
    if (ok && level == CryptoLevel::OneRTT && Base::runtimeEstablished_())
      Base::observePathRx_(app()->local(), ZuMv(peer));
    return ok;
  }

  bool handleControlFrame_(
    CryptoLevel::T, const Frame &frame, ZiSockAddr addr) {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	queueTxFlush_();
	return true;
      case FrameType::PathChallenge: {
	Base::queuePathResponse_(frame.payload);
	queueTxFlush_(ZuMv(addr));
	return true;
      }
      case FrameType::PathResponse:
	Base::receivePathResponse_(frame.payload);
	return true;
	      case FrameType::ConnectionClose:
	      case FrameType::ApplicationClose:
		close_(frame.errorCode, true, true);
		Base::transportClose_(frame.type, frame.errorCode);
		return true;
      case FrameType::HandshakeDone:
	return false;
      default:
	return true;
    }
  }

  void dataBlocked_(uint64_t maximum) {
    Base::queueBlocked_(FrameType::DataBlocked, 0, maximum);
    impl()->flowBlocked(
      FrameType::DataBlocked, 0, Zi::StreamType::Duplex, maximum);
    queueTxFlush_();
  }
  void streamDataBlocked_(uint64_t streamID, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamDataBlocked, streamID, maximum);
    impl()->flowBlocked(
      FrameType::StreamDataBlocked, streamID,
      Zi::StreamType::Duplex, maximum);
    queueTxFlush_();
  }
  void streamsBlocked_(Zi::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamsBlocked, 0, maximum, type);
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
    queueTxFlush_();
  }

  void pathPromoted_() {
    m_peerAddr = Base::activePathRemote_();
  }

  bool receivedRouted_(Datagram d) {
    return received_(ZuMv(d));
  }

  void installRoutes_(CxnRouter<Impl> &routes) {
    routes.add(m_bootstrap.originalDCID(), 0, impl());
    Base::installLocalCIDRoutes_(routes);
  }

  void retireRoutes_(CxnRouter<Impl> &routes) {
    routes.tombstone(m_bootstrap.originalDCID());
    Base::tombstoneLocalCIDRoutes_(routes);
  }

  void retiredLocalCID_(uint64_t, const CxnID &id) {
    if (app())
      static_cast<Server<App, Impl> *>(app())->dissociateRoute_(id);
  }

  // Rx thread exclusive
  ServerBootstrap	m_bootstrap;
  ZiSockAddr		m_peerAddr;
  bool			m_releaseDeferred = false;

  // shared
  ZmAtomic<unsigned>	m_handshakeDoneSent = 0;
};

} // namespace Zquic

#undef Zquic_DEBUG_LOG_

#endif /* Zquic_HH */
