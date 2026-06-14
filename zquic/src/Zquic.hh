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

#include <zlib/ZtArray.hh>
#include <zlib/ZtLocalArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicStream.hh>
#include <zlib/ZquicSched.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicTransportParams.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZquicEndpoint.hh>
#include <zlib/ZquicRecovery.hh>
#include <zlib/ZquicSock.hh>

namespace Zquic {

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Zquic.Log">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Zquic.ParamStrings">>));

struct InitialInfo {
  LongHeader	header;
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

inline bool padForProtectionSample(
  unsigned pnOffset, unsigned pnLength, unsigned tagLen,
  uint8_t *payload, unsigned len, unsigned &payloadLen)
{
  unsigned headerLen = pnOffset + pnLength;
  unsigned minPacketLen = pnOffset + 4 + 16;
  unsigned minPayloadLen = minPacketLen > headerLen + tagLen ?
    minPacketLen - headerLen - tagLen : 0;
  if (payloadLen >= minPayloadLen) return true;
  if (minPayloadLen > len) return false;
  memset(payload + payloadLen, 0, minPayloadLen - payloadLen);
  payloadLen = minPayloadLen;
  return true;
}

inline PacketSpace::T runtimePacketSpace(CryptoLevel::T level)
{
  if (level == CryptoLevel::Initial) return PacketSpace::Initial;
  if (level == CryptoLevel::Handshake) return PacketSpace::Handshake;
  return PacketSpace::AppData;
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
  if (frame.type == FrameType::Crypto) {
    ref = SentFrameRef::crypto(frame.offset, frame.length);
    return true;
  }
  if (frame.type == FrameType::Stream) {
    ref.kind = SentFrameKind::Stream;
    ref.streamID = frame.streamID;
    ref.offset = frame.offset;
    ref.length = frame.length;
    ref.fin = frame.fin;
    ref.range = TxRange{
      nullptr, 0, uint32_t(frame.length), frame.offset};
    return true;
  }
  if (frame.type == FrameType::MaxData ||
      frame.type == FrameType::MaxStreamData ||
      frame.type == FrameType::MaxStreams)
    ref = SentFrameRef::flowUpdate(
      FlowUpdate{frame.type, frame.streamID, frame.value, frame.streamType});
  else if (frame.type == FrameType::DataBlocked ||
      frame.type == FrameType::StreamDataBlocked ||
      frame.type == FrameType::StreamsBlocked)
    ref = SentFrameRef::blocked(
      frame.type, frame.streamID, frame.value, frame.streamType);
  else if (frame.type == FrameType::PathResponse)
    ref = SentFrameRef::pathResponse(frame.payload);
  else if (frame.type == FrameType::HandshakeDone)
    ref = SentFrameRef::handshakeDone();
  else
    ref = SentFrameRef::control();
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
  static constexpr unsigned TokenLength = ResetToken::Length;
  static constexpr unsigned MinLength = 21;

  static int decode(ResetToken &, ZuCSpan datagram);
  static bool verify(ZuCSpan datagram, const ResetToken &);
  static int writeForUnknownCID(
    uint8_t *, unsigned, ZuCSpan receivedPacket, const ResetToken &);
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
    if (!route || route->state == CxnState::Tombstone ||
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
  const ResetToken &statelessResetToken() const {
    return m_statelessResetToken;
  }

  bool acceptInitial(const LongHeader &, unsigned datagramLength);
  bool transportParams(TransportParams &) const;

private:
  CxnID		m_originalDCID;
  CxnID		m_clientInitialSCID;
  CxnID		m_localInitialSCID;
  ResetToken	m_statelessResetToken;
  bool		m_accepted = false;
};

// these counters are intentionally non-atomic
// - occasional off-by-one due to concurrent
//   modification is not a concern
struct RuntimeDiag {
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
  uint64_t	ptoCount = 0;
  uint64_t	retransmittedFrames = 0;
  uint64_t	failures = 0;
  uint64_t	handshakeComplete = 0;
};

template <typename Send>
inline bool sendRuntimeCryptoFlights(
  CryptoStream (&txCrypto)[3], RuntimeDiag &diag,
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

  LongHeader h;
  if (Packet::parseLong(d.buf->cspan(), h) < 0) {
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

  const char *payload =
    reinterpret_cast<const char *>(d.buf->data() + payloadOffset);
  unsigned offset = 0;
  auto frame_ = ZmAlloc(Frame, 1);
  new (&frame_[0]) Frame{};
  auto &frame = frame_[0];
  ZuGuard frameGuard{[&frame]() { frame.~Frame(); }};
  while (offset < payloadLength) {
    unsigned used = 0;
    if (FrameCodec::parse(
	  ZuCSpan{payload + offset, payloadLength - offset},
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
    headerLength = Packet::writeInitial(
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

  int pnLength = PacketNumber::encode(
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
      m_alpn.push(ParamString{ZuCSpan{
	reinterpret_cast<const char *>(p.base), p.len}});
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
    if (m_mx && m_rxThread && m_txThread &&
	!rxInvoked() && !txInvoked()) {
      ZmSemaphore stopped;
      rxInvoke([this, &stopped]() { stop_1(&stopped); });
      stopped.wait();
      return;
    }
    stop_3();
  }

  void stop_3() {
    if (m_mx) m_mx->del(&m_ptoTimer);
    m_mx = nullptr;
    m_rxThread = m_txThread = m_asyncThread = 0;
    m_errorFn = ErrorFn{};
    m_alpn.length(0);
    m_alpnData.length(0);
    m_caPath = m_certPath = m_keyPath = ParamString{};
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
	reinterpret_cast<const char *>(m_alpn[0].base),
	unsigned(m_alpn[0].len)} : ZuCSpan{};
  }
  ZuCSpan caPath() const { return m_caPath; }
  ZuCSpan certPath() const { return m_certPath; }
  ZuCSpan keyPath() const { return m_keyPath; }
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
  bool rxInvoked() { return m_mx->invoked(m_rxThread); }
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(m_txThread, ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(m_txThread, ZuFwd<Args>(args)...);
  }
  bool txInvoked() { return m_mx->invoked(m_txThread); }

protected:
  void stop_1(ZmSemaphore *stopped) {
    txInvoke([this, stopped]() {
      stop_2(stopped);
    });
  }

  void stop_2(ZmSemaphore *stopped) {
    stop_3();
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
    m_maxData = params.maxData();
    m_maxStreamData = params.maxStreamData();
    m_maxStreamsBidi = params.maxStreamsBidi();
    m_maxStreamsUni = params.maxStreamsUni();
    m_maxUDP = params.maxUDP();
    if (!init_alpn_(params.alpn())) return false;
    return l(params);
  }

  void error_(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

  static ZuTime ptoTimeout_() { return Zm::now(1); }
  ZmScheduler::Timer *ptoTimer() { return &m_ptoTimer; }

private:
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
  uint64_t		m_maxData = DefaultMaxData;
  uint64_t		m_maxStreamData = DefaultMaxStreamData;
  uint64_t		m_maxStreamsBidi = DefaultMaxStreamsBidi;
  uint64_t		m_maxStreamsUni = DefaultMaxStreamsUni;
  unsigned		m_maxUDP = MinUDPPayload;
  ZmScheduler::Timer	m_ptoTimer;
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
    return this->init_(ZuMv(params), [](const ServerParams &) { return true; });
  }

  void final() {
    close();
    Base::final();
  }

  bool listen() {
    if (!this->mx()) return false;
    close();
    ZiIP localIP = this->app()->localIP();
    uint16_t localPort = this->app()->localPort();
    return m_endpoint.openUDP(
      this->mx(),
      PathMode::ServerUnconnected,
      localIP, localPort,
      ZiIP{}, 0,
      Endpoint::DatagramFn{this, [](Server *self, Datagram d) {
	self->rxRun([self, d = ZuMv(d)]() mutable {
	  self->received_(ZuMv(d));
	});
      }},
      Endpoint::ReadyFn{this, [](Server *self, Endpoint *) {
	self->app()->listening();
      }},
      Endpoint::FailFn{this, [](Server *self, bool transient) {
	self->failed_0(transient);
      }});
  }

  void close() {
    clearLinks_();
    m_endpoint.closeUDP();
  }

  bool listening() const { return m_endpoint.listening(); }
  bool connected() const { return m_endpoint.connected(); }
  const ZiSockAddr &local() const { return m_endpoint.local(); }
  const EndpointDiag &endpointDiag() const { return m_endpoint.diag(); }

  ZiIP localIP() const { return ZiIP{}; }
  uint16_t localPort() const { return 0; }
  bool sendPacket(const ZmRef<ZiIOBuf> &) { return true; }
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

  ZmRef<ZiIOBuf> allocTxPacket_() {
    return m_endpoint.allocTxPacket();
  }
  bool sendPacket_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (!this->app()->sendPacket(buf)) return true;
    return m_endpoint.send(ZuMv(buf), ZuMv(addr));
  }
  void dissociateRoute_(const CxnID &id) {
    m_routes.retire(id);
  }

private:
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
    link->receivedRouted_(ZuMv(d));
    link->installRoutes_(m_routes);
  }

  Link *route_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return nullptr;
    ZuCSpan packet{
      reinterpret_cast<const char *>(d.buf->data_()), d.buf->length};
    if (Packet::isLong(packet)) return routeLong_(d, packet);
    return routeShort_(d, packet);
  }

  Link *routeLong_(const Datagram &d, ZuCSpan packet) {
    LongHeader h;
    if (Packet::parseLong(packet, h) < 0) return nullptr;
    if (!VersionNegotiation::supported(h.version)) {
      sendVersionNegotiation_(h, d.addr);
      return nullptr;
    }
    if (Link *link = m_routes.find(h.dcid)) return link;
    if (h.type != PacketType::Initial) return nullptr;
    return accept_(InitialInfo{h, d.addr, d.buf->length});
  }

  Link *routeShort_(const Datagram &, ZuCSpan packet) {
    return m_routes.matchShort(packet);
  }

  bool sendStatelessReset_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return false;
    ZuCSpan packet{
      reinterpret_cast<const char *>(d.buf->data_()), d.buf->length};
    if (Packet::isLong(packet)) return false;
    ResetToken token;
    if (!m_routes.resetTokenForShort(packet, token)) return false;
    ZmRef<ZiIOBuf> buf = allocTxPacket_();
    int n = StatelessReset::writeForUnknownCID(
      buf->data_(), buf->size, packet, token);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    return sendPacket_(ZuMv(buf), d.addr);
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
    m_links->del(link);
  }

  void clearLinks_() {
    m_routes.clear();
    m_links = new LinkTable{
      ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  }

  bool sendVersionNegotiation_(const LongHeader &h, ZiSockAddr addr) {
    ZmRef<ZiIOBuf> buf = allocTxPacket_();
    int n = VersionNegotiation::write(
      buf->data_(), buf->size, h.scid, h.dcid);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    return sendPacket_(ZuMv(buf), ZuMv(addr));
  }

  Endpoint		m_endpoint;
  ZmRef<LinkTable>	m_links = new LinkTable{
    ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  CxnRouter<Link>	m_routes;
};

template <typename Link_, typename Impl, typename TxBufAlloc_>
class Stream :
  public ZmPolymorph,
  public ZmPQRx<
    Stream<Link_, Impl, TxBufAlloc_>,
    StreamRxPQueue, ZmPQRxGapIgnore<>>,
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

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Stream(Link *link, int64_t id) : m_link{link}, m_id{id} { }

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
  uint64_t rxBytes() const { return m_rxDelivered; }
  uint64_t finalSize() const { return m_rxState.finalSize(); }
  unsigned rxPending() const { return m_rxQueue.count_(); }
  uint64_t appError() const { return m_appError; }
  StreamError::T error() const { return m_error; }
  bool finSent() const { return m_fin; }
  bool finDequeued() const { return m_finDequeued; }
  bool finReady() const { return m_fin && !m_finDequeued && !m_txQueue.count_(); }
  bool finReceived() const { return m_rxState.finSeen(); }
  bool resetSent() const { return m_resetSent; }
  bool resetReceived() const { return m_resetReceived; }
  bool stopSent() const { return m_stopSent; }
  bool stopReceived() const { return m_stopReceived; }
  bool readOpen() const { return !m_resetReceived && !rxComplete(); }
  bool closedForStreamCredit() const {
    return (rxComplete() || m_resetReceived) && (m_finDequeued || m_resetSent);
  }
  bool streamCreditReturned() const { return m_streamCreditReturned; }
  bool rxComplete() const {
    return m_rxState.complete() && m_rxDelivered == m_rxState.finalSize();
  }
  unsigned rxQueued() { return m_rx.count_(); }

  RxStream &rxStream() { return m_rx; }

  StreamRxPQueue *rxQueue() { return &m_rxQueue; }
  TxDataPQueue *txQueue() { return &m_txQueue; }

  void txCredit(uint64_t limit) { m_txCredit.set(limit); }
  void extendTxCredit(uint64_t limit) { m_txCredit.extend(limit); }
  bool consumeTxCredit(uint64_t n) { return m_txCredit.consume(n); }
  void rxCredit(uint64_t limit) { m_rxCredit.set(limit); }
  void extendRxCredit(uint64_t limit) { m_rxCredit.extend(limit); }
  bool consumeRxCreditTo(uint64_t n) { return m_rxCredit.consumeTo(n); }
  uint64_t lastStreamDataBlocked() const { return m_lastStreamDataBlocked; }
  void lastStreamDataBlocked(uint64_t n) { m_lastStreamDataBlocked = n; }
  void markStreamCreditReturned() { m_streamCreditReturned = true; }

  void process(RxMsg *msg) {
    if (!msg) return;
    StreamRxData &data = msg->data();
    if (!data.bytes) return;
    ZiAssert(data.bufOffset + data.bytes <= data.size,
      "Zquic", (data.bufOffset, data.bytes, data.size),
      "stream Rx queued slice exceeds packet-backed range", return);
    data.skip = unsigned(data.bufOffset);
    data.ZiIOBuf::length = unsigned(data.bytes);
    m_rxDelivered += data.bytes;
    m_rxState.delivered(m_rxDelivered);
    ZmRef<ZiIOBuf> buf = msg;
    m_rx.push(ZuMv(buf));
  }
  void request(const RxQueueSpan &, const RxQueueSpan &) { }
  void scheduleDequeue() { Rx::dequeue(); }
  void rescheduleDequeue() { Rx::dequeue(); }
  void idleDequeue() { }

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
  bool nextTxRange(PacketBudget &, TxRange &range, bool &fin) const {
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
    if (!length) return false;
    auto iter = m_txQueue.citer();
    auto first = iter();
    if (!first || length > first->data().length()) return false;
    uint64_t key = first->data().key();
    auto node = Tx::abort(key);
    if (!node) return false;
    auto &data = node->data();
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
    Stream	*m_stream;
  };

  auto txStream() { return TxStream_<true>{*this}; }
  auto txStream_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream txStream_ outside Tx thread",
      return TxStream_<false>{*this});
    return TxStream_<false>{*this};
  }

  void fin() {
    if (m_fin) return;
    m_fin = true;
    notifyTx_();
  }
  void reset(uint64_t appError) {
    m_resetSent = true;
    m_error = StreamError::Reset;
    m_appError = appError;
  }
  void stop(uint64_t appError) {
    m_stopSent = true;
    m_error = StreamError::Stop;
    m_appError = appError;
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
    return impl()->process(m_rx);
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
    return impl()->process(m_rx);
  }

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
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;
    if (end <= m_rxDelivered) return true;

    uint64_t first = frame.offset < m_rxDelivered ? m_rxDelivered : frame.offset;
    return rxNovelSpans(m_rxQueue, first, end, spans);
  }

  bool queueRxSlices_(
    const Frame &frame, const RxSpans &spans,
    ZmRef<ZiIOBuf> packet, BufDiag *diag) {
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

  void send_(ZmRef<ZiIOBuf> buf) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream send_ outside Tx thread", return);
    if (ZuUnlikely(!buf)) return;
    if (!buf->length) return;
    ZiAssert(buf->skip + buf->length <= buf->size, "Zquic",
      (buf->skip, buf->length, buf->size),
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
    if (ZuUnlikely(!m_link || !m_link->app() || !m_link->app()->mx()))
      return true;
    return m_link->app()->txInvoked();
  }

  template <typename Fn>
  void txInvoke_(Fn &&fn) {
    if (ZuLikely(m_link && m_link->app() && m_link->app()->mx())) {
      m_link->app()->txInvoke(ZuFwd<Fn>(fn));
      return;
    }
    ZuFwd<Fn>(fn)();
  }

  void notifyTx_() {
    if (m_link && m_id >= 0) m_link->streamWritable_(uint64_t(m_id));
  }

  Link			*m_link = nullptr;
  int64_t		m_id;
  uint64_t		m_txBytes = 0;
  uint64_t		m_txBufferedBytes = 0;
  FlowCredit		m_txCredit;
  FlowCredit		m_rxCredit;
  uint64_t		m_lastStreamDataBlocked = uint64_t(-1);
  uint64_t		m_rxDelivered = 0;
  uint64_t		m_appError = 0;
  StreamError::T	m_error = StreamError::None;
  bool			m_fin = false;
  bool			m_finDequeued = false;
  bool			m_resetSent = false;
  bool			m_resetReceived = false;
  bool			m_stopSent = false;
  bool			m_stopReceived = false;
  bool			m_streamCreditReturned = false;
  StreamRxState		m_rxState;
  RxStream		m_rx;
  StreamRxPQueue	m_rxQueue{0};
  TxDataPQueue		m_txQueue{0};
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
	ZmHashHeapID<"Zquic.Stream.ObjectHash">>>>));

template <typename App, typename Impl, typename TxBufAlloc_, typename Stream_>
class Link : public ZmPolymorph {
public:
  using TxBufAlloc = TxBufAlloc_;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  using Streams = Streams_<Stream>;
  using ControlQueue =
    ZmQueue<ControlFrame,
      ZmQueueHeapID<"Zquic.Link.ControlQueue">>;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer = false) :
    m_app{app}, m_isServer{isServer} { }

  App *app() const { return m_app; }
  bool isServer() const { return m_isServer; }
  bool closed() const { return m_closed; }
  uint64_t closeError() const { return m_closeError; }
  uint64_t streamCount() const { return m_streams.count_(); }
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
  unsigned queuedControlFrames() const { return m_controlQueue.count_(); }
  uint64_t rxDataCreditUsed() const { return m_rxDataCredit.used(); }
  uint64_t rxDataCreditLimit() const { return m_rxDataCredit.limit(); }
  uint64_t rxDataCreditAvailable() const { return m_rxDataCredit.available(); }

  void setPeerStreamLimit(Zi::StreamType::T type, uint64_t limit) {
    localLimit_(type).set(limit);
  }
  void setLocalStreamLimit(Zi::StreamType::T type, uint64_t limit) {
    peerLimit_(type).set(limit);
  }

  bool applyMaxStreams(const Frame &frame) {
    if (!validateMaxStreams_(frame)) return false;
    localLimit_(frame.streamType).extend(frame.value);
    openQueued_(frame.streamType);
    return true;
  }
  bool applyMaxData(const Frame &frame) {
    if (!validateMaxData_(frame)) return false;
    if (frame.value <= m_txDataCredit.limit()) return true;
    m_txDataCredit.extend(frame.value);
    return true;
  }
  bool applyMaxStreamData(const Frame &frame) {
    StreamRef stream;
    if (!validateMaxStreamData_(frame, stream)) return false;
    stream->extendTxCredit(frame.value);
    if (streamTxPending_(stream)) streamWritable_(frame.streamID);
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
    Zi::StreamType::T type = StreamID::uni(id) ? Zi::StreamType::Simplex : Zi::StreamType::Duplex;
    uint64_t opened = StreamID::ordinal(id) + 1;
    if (!peerLimit_(type).allowsTo(opened)) return nullptr;

    StreamRef stream = newStream_(int64_t(id));
    if (!stream) return nullptr;
    ZiAssert(peerLimit_(type).openTo(opened), "Zquic",
      (id, opened, peerLimit_(type).limit()),
      "peer stream count advanced past local limit", return nullptr);
    impl()->streamed(stream);
    return stream;
  }

  StreamRef findStream(int64_t id) const {
    return m_streams.find(id);
  }

  int receiveFrame(const Frame &frame, BufDiag *diag = nullptr) {
    if (frame.type != FrameType::ResetStream &&
	frame.type != FrameType::StopSending)
      return -1;
    StreamRef stream = findOrAccept_(frame.streamID);
    if (!stream) return -1;
    if (frame.type == FrameType::ResetStream) {
      uint64_t old = stream->rxCreditUsed();
      uint64_t novel = frame.length > old ? frame.length - old : 0;
      if (frame.length > stream->rxCreditLimit() ||
	  novel > m_rxDataCredit.available())
	return -1;
      if (!stream->receiveReset(frame)) return -1;
      if (!stream->consumeRxCreditTo(frame.length) ||
	  !m_rxDataCredit.consume(novel))
	return -1;
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      return 0;
    }
    return stream->receiveStop(frame) ? 0 : -1;
  }

  int receiveFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    if (frame.type != FrameType::Stream)
      return receiveFrame(frame, diag);
    StreamRef stream = findOrAccept_(frame.streamID);
    if (!stream) return -1;
    int rc = stream->processFrame(frame, m_rxDataCredit, ZuMv(packet), diag);
    if (rc >= 0) {
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      maybeReturnStreamCredit_(stream);
    }
    return rc;
  }

  void streamWritable_(uint64_t id) {
    if (id <= uint64_t(INT64_MAX)) m_streamScheduler.add(id);
  }

  void close(uint64_t errorCode = 0) {
    m_closeError = errorCode;
    m_closed = true;
  }

protected:
  struct InitialKeyDir { enum T { Client, Server }; };
  struct RuntimeCID { enum T { Initial, Local, Peer }; };
  // RFC9000 specifies only a minimum/default of 2 for active_connection_id_limit.
  // This is the local advertised policy cap for active peer-issued CIDs.
  static constexpr unsigned LocalActiveConnectionIDLimit = 8;

  struct LinkCID {
    CxnID			id;
    uint64_t			sequence = 0;
    ResetToken	resetToken;
    CxnState::T		state = CxnState::Tombstone;
    bool			associated = false;
  };
  using LocalCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.LocalCID">>;
  using PeerCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.PeerCID">>;

  unsigned scheduledStreamCount_() const {
    return m_streamScheduler.count();
  }

  bool queueControl_(const ControlFrame &frame) {
    if (!frame || controlQueuedAtLeast_(frame)) return false;
    m_controlQueue.push(frame);
    return true;
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
  bool queueHandshakeDone_() {
    return queueControl_(ControlFrame::handshakeDone());
  }

  bool runtimeEstablished_() const { return m_established; }
  bool runtimeDraining_() const {
    return m_runtimeCloseState == CloseState::Draining;
  }
  bool runtimeHandshakeStarted_() const { return m_handshakeStarted; }
  const RuntimeDiag &runtimeDiag_() const { return m_diag; }
  const Crypto &crypto_() const { return m_crypto; }

  void resetRuntimeDiag_() { m_diag = {}; }
  void resetRuntime_() {
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
    m_controlQueue.clean();
    resetPacketRuntime_();
  }

  void closeRuntime_(uint64_t errorCode = 0) {
    closeLinkState_(errorCode);
    m_established = 0;
  }

  void endpointReady_() { ++m_diag.endpointReady; }
  void endpointFailure_() { ++m_diag.failures; }
  void packetParseFailure_() { ++m_diag.failures; }
  void tlsFailure_() { ++m_diag.failures; }
  void handshakeDoneTx_() { }
  void dataBlocked_(uint64_t) { }
  void streamDataBlocked_(uint64_t, uint64_t) { }
  void streamsBlocked_(Zi::StreamType::T, uint64_t) { }
  void streamFrame(
    uint64_t, uint64_t, ZuCSpan, bool) { }
  void retiredLocalCID_(uint64_t, const CxnID &) { }
  void statelessReset() { }
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
  }

  void setPeerCIDFromHeaderSCID_(const LongHeader &h) {
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
      if (!slot && cid.state == CxnState::Tombstone) slot = &cid;
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
  void retirePeerCIDsPriorTo_(uint64_t sequence) {
    m_peerRetirePriorTo = sequence;
    for (auto &cid : m_peerCIDs)
      if (cid.state == CxnState::Active && cid.sequence < sequence)
	cid.state = CxnState::Retired;
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
    m_established = 1;
    establishState_();
    ++m_diag.handshakeComplete;
  }

  auto negotiatedProtocol_() const {
    return m_crypto.negotiatedProtocol();
  }

  void resetPacketRuntime_() {
    for (auto &s : m_txCrypto) s.reset();
    for (auto &s : m_rxCrypto) s.reset();
    memset(m_txPN, 0, sizeof(m_txPN));
    memset(m_rxLargestPN, 0, sizeof(m_rxLargestPN));
    for (auto &a : m_rxPackets) a.clear();
    for (auto &p : m_txPackets) p.clear();
    memset(m_pendingAck, 0, sizeof(m_pendingAck));
    m_txKeyPhase = false;
  }

  bool installPeerKeyUpdate_(const TrafficSecret &nextRxSecret) {
    if (!m_crypto.updateRxTrafficSecret(CryptoLevel::OneRTT, nextRxSecret))
      return false;
    return withTxLock_([this]() {
      TrafficSecret nextTxSecret;
      if (!PacketProtection::deriveNextTrafficSecret(
	    nextTxSecret, m_crypto.txTrafficSecret(CryptoLevel::OneRTT)) ||
	  !m_crypto.updateTxTrafficSecret(
	    CryptoLevel::OneRTT, nextTxSecret))
	return false;
      m_txKeyPhase = !m_txKeyPhase;
      return true;
    });
  }

  bool checkStatelessReset_(ZuCSpan datagram) {
    if (!StatelessReset::verify(datagram, m_peerResetToken)) return false;
    m_runtimeCloseState = CloseState::Draining;
    m_linkState = LinkState::Draining;
    m_established = 0;
    m_handshakeStarted = 0;
    m_streamScheduler.clear();
    for (auto &p : m_txPackets) p.clear();
    memset(m_pendingAck, 0, sizeof(m_pendingAck));
    ++m_diag.failures;
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
    uint64_t id = m_streamScheduler.next();
    if (id == uint64_t(-1)) return nullptr;
    m_streamScheduler.remove(id);
    if (id > uint64_t(INT64_MAX)) return nullptr;
    return findStream(int64_t(id));
  }

  bool streamTxPending_(const StreamRef &stream) const {
    return stream && (stream->txRangeCount() || stream->finReady());
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
      case FrameType::PathResponse:
	return SentFrameRef::pathResponse(
	  byteSpan(frame.payload, sizeof(frame.payload)));
      case FrameType::HandshakeDone:
	return SentFrameRef::handshakeDone();
      default:
	return SentFrameRef::control();
    }
  }

  ControlFrame controlFrame_(const SentFrameRef &ref) const {
    ControlFrame frame;
    frame.type = ref.controlType;
    frame.streamID = ref.streamID;
    frame.value = ref.value;
    frame.streamType = ref.streamType;
    if (ref.controlType == FrameType::PathResponse)
      memcpy(frame.payload, ref.payload, sizeof(frame.payload));
    return frame;
  }

  template <typename AppendAck, typename SendPacket>
  bool sendQueuedControlPacket_(
    ZiSockAddr addr, AppendAck appendAck, SendPacket sendPacket) {
    while (m_controlQueue.count_()) {
      ControlFrame frame = m_controlQueue.head();
      if (!controlStillValid_(frame)) {
	m_controlQueue.shift();
	continue;
      }
      PacketBuild build;
      build.reset();
      PacketBudget budget;
      budget.pmtu = budget.congestion = budget.antiAmplification =
	app()->maxUDP();
      PacketAssembly assembly;
      unsigned before = build.bytes();
      if (!appendAck(build)) return false;
      unsigned ackBytes = build.bytes() - before;
      if (ackBytes && !assembly.addControl(budget, ackBytes)) return false;
      int n = frame.write(build.scratch(), build.scratchAvail());
      if (n <= 0 || !assembly.addControl(budget, unsigned(n)) ||
	  !build.commitScratch(unsigned(n)))
	return false;
      SentFrameRef ref = controlRef_(frame);
      if (!sendPacket(build, ZuMv(addr), ref)) return false;
      m_controlQueue.shift();
      return true;
    }
    return false;
  }

  template <typename AppendAck, typename SendPacket>
  bool flushControlAndStreams_(
    ZiSockAddr addr, AppendAck appendAck, SendPacket sendPacket) {
    return withTxLock_([this, addr = ZuMv(addr), &appendAck, &sendPacket]() mutable {
      return flushControlAndStreamsLocked_(
	ZuMv(addr),
	[&appendAck](PacketBuild &build) { return appendAck(build); },
	[&sendPacket](
	    PacketBuild &build, ZiSockAddr addr_,
	    const SentFrameRef &ref) {
	  return sendPacket(build, ZuMv(addr_), ref);
	});
    });
  }

  template <typename AppendAck, typename SendPacket>
  bool flushControlAndStreamsLocked_(
    ZiSockAddr addr, AppendAck appendAck, SendPacket sendPacket) {
    bool sent = false;
    while (sendQueuedControlPacket_(
	addr,
	[&appendAck](PacketBuild &build) { return appendAck(build); },
	[&sendPacket](
	    PacketBuild &build, ZiSockAddr addr_,
	    const SentFrameRef &ref) {
	  return sendPacket(build, ZuMv(addr_), ref);
	}))
      sent = true;
    bool streams = flushWritableStreams_(
      addr,
      [this, &appendAck, &sendPacket](StreamRef stream, ZiSockAddr addr_) {
	return sendQueuedStreamPacket_(
	  ZuMv(stream), ZuMv(addr_),
	  [&appendAck](PacketBuild &build) { return appendAck(build); },
	  [&sendPacket](
	      PacketBuild &build, ZiSockAddr addr__,
	      const SentFrameRef &ref) {
	    return sendPacket(build, ZuMv(addr__), ref);
	  });
      });
    while (sendQueuedControlPacket_(
	addr,
	[&appendAck](PacketBuild &build) { return appendAck(build); },
	[&sendPacket](
	    PacketBuild &build, ZiSockAddr addr_,
	    const SentFrameRef &ref) {
	  return sendPacket(build, ZuMv(addr_), ref);
	}))
      sent = true;
    return sent || streams;
  }

  template <typename Fn>
  bool withTxLock_(Fn fn) {
    ZmGuard<ZmLock> guard(m_txLock);
    return fn();
  }

  template <typename AppendAck, typename SendPacket>
  bool sendQueuedStreamPacket_(
    StreamRef stream, ZiSockAddr addr, AppendAck appendAck,
    SendPacket sendPacket) {
    PacketBuild build;
    build.reset();
    PacketBudget budget;
    budget.pmtu = budget.congestion = budget.antiAmplification =
      app()->maxUDP();
    budget.flow = m_txDataCredit.available();
    PacketAssembly assembly;
    if (stream->txRangeCount() && m_txDataCredit.blocked()) {
      streamWritable_(uint64_t(stream->id()));
      queueBlocked_(FrameType::DataBlocked, 0, m_txDataCredit.limit());
      return false;
    }
    if (stream->txRangeCount() && !stream->txCreditAvailable()) {
      streamWritable_(uint64_t(stream->id()));
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
    int n = StreamPacketizer::writeNext(
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
    if (!sendPacket(build, ZuMv(addr), ref)) return false;
    if (info.length && !m_txDataCredit.consume(info.length)) return false;
    m_diag.streamBytesTx += info.length;
    return true;
  }

  template <typename SendOneStream>
  bool flushWritableStreams_(ZiSockAddr addr, SendOneStream sendOneStream) {
    bool sent = false;
    while (scheduledStreamCount_()) {
      StreamRef stream = nextWritableStream_();
      if (!stream || !streamTxPending_(stream)) continue;
      if (!sendOneStream(stream, addr)) {
	if (stream->id() >= 0) streamWritable_(uint64_t(stream->id()));
	return false;
      }
      sent = true;
      if (streamTxPending_(stream) && stream->id() >= 0)
	streamWritable_(uint64_t(stream->id()));
    }
    return sent;
  }

  void noteAck_(CryptoLevel::T level, uint64_t) {
    m_pendingAck[level] = true;
  }

  bool appendPendingAck_(
    CryptoLevel::T level, PacketBuild &build) {
    if (!m_pendingAck[level]) return true;
    int n = m_rxPackets[level].writeFrame(
      build.scratch(), build.scratchAvail(), 0);
    if (n < 0) return false;
    if (!build.commitScratch(unsigned(n))) return false;
    m_pendingAck[level] = false;
    return true;
  }

  void schedulePTO_() {
    if (!app() || !app()->mx() || closed()) return;
    auto &tx = m_txPackets[CryptoLevel::OneRTT];
    if (!tx.bytesInFlight() && !tx.retransmitPending()) return;
    app()->mx()->run(app()->txThread(),
      [link = ZmMkRef(impl())]() { link->pto_(); },
      app()->ptoTimeout_(), ZmScheduler::Advance, app()->ptoTimer());
  }

  bool reclaimPTO_() {
    unsigned n = m_txPackets[CryptoLevel::OneRTT].reclaimOnPTO(1);
    if (n) ++m_diag.ptoCount;
    return n;
  }

  bool nextRetransmit_(SentFrameRef &ref) {
    if (!m_txPackets[CryptoLevel::OneRTT].nextRetransmit(ref)) return false;
    ++m_diag.retransmittedFrames;
    return true;
  }

  bool buildRetransmitStream_(PacketBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Stream) return false;
    StreamRef stream = findStream(int64_t(ref.streamID));
    if (!stream) return false;
    build.reset();
    if (!appendPendingAck_(CryptoLevel::OneRTT, build)) return false;
    int n = FrameCodec::writeStreamPrefix(
      build.scratch(), build.scratchAvail(), ref.streamID, ref.offset,
      ref.length, ref.fin);
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    return build.add(ref.range);
  }

  bool buildRetransmitControl_(PacketBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Control ||
	ref.controlType == FrameType::Unknown)
      return false;
    ControlFrame frame = controlFrame_(ref);
    if (!controlStillValid_(frame)) return false;
    build.reset();
    if (!appendPendingAck_(CryptoLevel::OneRTT, build)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    return n > 0 && build.commitScratch(unsigned(n));
  }

  bool recordRxPacket_(
    CryptoLevel::T level, uint64_t pn) {
    if (m_rxPackets[level].contains(pn)) return false;
    m_rxPackets[level].add(pn);
    if (pn > m_rxLargestPN[level])
      m_rxLargestPN[level] = pn;
    return true;
  }

  void recordTxPacket_(
    CryptoLevel::T level, uint64_t pn, unsigned bytes, ZuCSpan frame) {
    SentFrameRef ref;
    bool ackEliciting = false;
    if (!runtimeFrameRef(frame, ref, ackEliciting)) return;
    recordTxPacket_(level, pn, bytes, ref, ackEliciting);
  }

  void recordTxPacket_(
    CryptoLevel::T level, uint64_t pn, unsigned bytes,
    const SentFrameRef &ref, bool ackEliciting) {
    SentPacket packet;
    packet.pn = pn;
    packet.space = runtimePacketSpace(level);
    packet.bytes = bytes;
    packet.ackEliciting = ackEliciting;
    packet.inFlight = ackEliciting;
    if (ref.kind != SentFrameKind::None) packet.addFrame(ref);
    m_txPackets[level].add(packet);
  }

  void processAckFrame_(CryptoLevel::T level, const Frame &frame) {
    if (!frame.ackRanges.length()) return;
    m_txPackets[level].ack(frame.ackRanges.data(), frame.ackRanges.length());
    if (level == CryptoLevel::OneRTT) schedulePTO_();
  }

  bool buildPayload_(
    CryptoLevel::T level, PacketBuild &build, ZuCSpan frame) {
    build.reset();
    return appendPendingAck_(level, build) && build.add(frame);
  }

  bool buildPayload_(
    CryptoLevel::T level, PacketBuild &build,
    ZuCSpan prefix, ZuCSpan payload) {
    build.reset();
    return appendPendingAck_(level, build) &&
      build.add(prefix) && build.add(payload);
  }

  template <typename SendCryptoPacket>
  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    unsigned chunkMax, ZiSockAddr addr, SendCryptoPacket sendCryptoPacket) {
    return sendRuntimeCryptoFlights(
      m_txCrypto, m_diag,
      data, len, offsets, chunkMax, ZuMv(addr),
      [sendCryptoPacket](
	  CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) mutable {
	return sendCryptoPacket(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
  }

  template <
    typename BuildPayload,
    typename SendInitial, typename SendHandshake, typename SendShort>
  bool sendCryptoPacket_(
    CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr,
    BuildPayload buildPayload,
    SendInitial sendInitial, SendHandshake sendHandshake, SendShort sendShort) {
    PacketBuild build;
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
  bool sendCryptoPacket_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr,
    BuildPayload buildPayload,
    SendInitial sendInitial, SendHandshake sendHandshake, SendShort sendShort) {
    PacketBuild build;
    if (!buildPayload(level, build, prefix, payload)) return false;
    if (level == CryptoLevel::Initial)
      return sendInitial(build, ZuMv(addr), {}, &ref, true);
    if (level == CryptoLevel::Handshake)
      return sendHandshake(build, ZuMv(addr), {}, &ref, true);
    return sendShort(build, ZuMv(addr), {}, &ref, true);
  }

  void recordProtectedPacketTx_(
    CryptoLevel::T level, uint64_t pn, unsigned bytes, ZuCSpan recordFrame,
    const SentFrameRef *recordRef, bool ackEliciting) {
    if (recordRef)
      recordTxPacket_(level, pn, bytes, *recordRef, ackEliciting);
    else
      recordTxPacket_(level, pn, bytes, recordFrame);
    ++m_txPN[level];
    ++m_diag.packetsTx;
    m_diag.bytesTx += bytes;
    if (level == CryptoLevel::OneRTT && ackEliciting) schedulePTO_();
  }

  const auto &initialKeys_(InitialKeyDir::T dir) const {
    return dir == InitialKeyDir::Client ?
      m_crypto.initialKeys().client :
      m_crypto.initialKeys().server;
  }

  template <typename AllocTxPacket, typename SendPacket>
  bool sendProtectedInitialPacket_(
    InitialKeyDir::T keyDir, RuntimeCID::T dcid, RuntimeCID::T scid,
    unsigned pnLength, bool padInitial, PacketBuild &payload, ZiSockAddr addr,
    ZuCSpan recordFrame, const SentFrameRef *recordRef, bool ackEliciting,
    AllocTxPacket allocTxPacket, SendPacket sendPacket) {
    ZmGuard<ZmLock> guard(m_txLock);
    ZmRef<ZiIOBuf> buf = allocTxPacket();
    const auto &initialKeys = initialKeys_(keyDir);
    int headerLen = -1;
    unsigned targetPlainLen = payload.bytes();
    for (unsigned i = 0; i < 4; ++i) {
      headerLen = Packet::writeInitial(
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
    if (PacketNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Initial], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Initial];
    int n = InitialPacketProtection::protectLongV(
      buf->data_(), buf->size, initialKeys, pn,
      byteSpan(buf->data_(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_diag.failures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!sendPacket(ZuMv(buf), ZuMv(addr))) return false;
    recordProtectedPacketTx_(
      CryptoLevel::Initial, pn, unsigned(n), recordFrame, recordRef,
      ackEliciting);
    return true;
  }

  template <typename AllocTxPacket, typename SendPacket>
  bool sendProtectedHandshakePacket_(
    RuntimeCID::T dcid, RuntimeCID::T scid, unsigned pnLength,
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef, bool ackEliciting,
    AllocTxPacket allocTxPacket, SendPacket sendPacket) {
    if (!m_crypto.txTrafficSecretInstalled(CryptoLevel::Handshake)) {
      ++m_diag.failures;
      return false;
    }
    ZmGuard<ZmLock> guard(m_txLock);
    ZmRef<ZiIOBuf> buf = allocTxPacket();
    int headerLen = Packet::writeHandshake(
      buf->data_(), buf->size, runtimeCID_(dcid), runtimeCID_(scid),
      payload.bytes() +
	m_crypto.txTrafficSecret(CryptoLevel::Handshake).tagLen,
      pnLength);
    if (headerLen < 0 ||
	PacketNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Handshake], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Handshake];
    int n = PacketProtection::protectLongV(
      buf->data_(), buf->size,
      m_crypto.txProtectionState(CryptoLevel::Handshake), pn,
      byteSpan(buf->data_(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_diag.failures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!sendPacket(ZuMv(buf), ZuMv(addr))) return false;
    recordProtectedPacketTx_(
      CryptoLevel::Handshake, pn, unsigned(n), recordFrame, recordRef,
      ackEliciting);
    return true;
  }

  template <typename AllocTxPacket, typename SendPacket>
  bool sendProtectedShortPacket_(
    RuntimeCID::T dcid, unsigned pnLength, PacketBuild &payload,
    ZiSockAddr addr, ZuCSpan recordFrame, const SentFrameRef *recordRef,
    bool ackEliciting, AllocTxPacket allocTxPacket, SendPacket sendPacket) {
    if (!m_established &&
	!m_crypto.txTrafficSecretInstalled(CryptoLevel::OneRTT)) {
      ++m_diag.failures;
      return false;
    }
    ZmGuard<ZmLock> guard(m_txLock);
    ZmRef<ZiIOBuf> buf = allocTxPacket();
    int headerLen = Packet::writeShort(
      buf->data_(), buf->size, runtimeCID_(dcid),
      m_txPN[CryptoLevel::OneRTT], pnLength);
    if (headerLen < 0) return false;
    if (m_txKeyPhase) buf->data_()[0] |= 0x04;
    if (!payload.padForProtectionSample(
	  unsigned(headerLen) - pnLength, pnLength,
	  m_crypto.txTrafficSecret(CryptoLevel::OneRTT).tagLen))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::OneRTT];
    int n = PacketProtection::protectShortV(
      buf->data_(), buf->size,
      m_crypto.txProtectionState(CryptoLevel::OneRTT), pn,
      byteSpan(buf->data_(), unsigned(headerLen)),
      payload.data(), payload.count(),
      unsigned(headerLen) - pnLength, pnLength);
    if (n < 0) {
      ++m_diag.failures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!sendPacket(ZuMv(buf), ZuMv(addr))) return false;
    recordProtectedPacketTx_(
      CryptoLevel::OneRTT, pn, unsigned(n), recordFrame, recordRef,
      ackEliciting);
    return true;
  }

  template <typename ReceiveLong, typename ReceiveShort>
  void receiveDatagram_(
    Datagram d, ReceiveLong receiveLong, ReceiveShort receiveShort) {
    ++m_diag.datagramsRx;
    if (!d.buf) {
      ++m_diag.failures;
      return;
    }
    m_diag.bytesRx += d.buf->length;
    bool ok = true;
    unsigned offset = 0;
    while (offset < d.buf->length) {
      ZuCSpan packet{
	reinterpret_cast<const char *>(d.buf->data_() + offset),
	d.buf->length - offset};
      if (Packet::isLong(packet)) {
	LongHeader h;
	if (Packet::parseLong(packet, h) < 0) {
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
    if (!ok) ++m_diag.failures;
  }

  template <typename PrepareLong, typename ConsumeFrames>
  bool receiveProtectedLongPacket_(
    InitialKeyDir::T keyDir, Datagram &d, unsigned packetOffset, unsigned packetLen,
    PrepareLong prepareLong, ConsumeFrames consumeFrames) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{
      reinterpret_cast<const char *>(base), packetLen};
    ZuCSpan datagram{
      reinterpret_cast<const char *>(d.buf->data_()), d.buf->length};
    LongHeader h;
    if (Packet::parseLong(packet, h) < 0) return false;
    if (!prepareLong(h, d)) return false;
    CryptoLevel::T level =
      h.type == PacketType::Initial ? CryptoLevel::Initial :
      h.type == PacketType::Handshake ? CryptoLevel::Handshake :
      CryptoLevel::OneRTT;
    if (h.type != PacketType::Initial && h.type != PacketType::Handshake)
      return false;
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = -1;
    if (level == CryptoLevel::Initial)
      plainLen = InitialPacketProtection::unprotectLong(
	base, packetLen, initialKeys_(keyDir),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    else {
      if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::Handshake)) {
	++m_diag.failures;
	return false;
      }
      plainLen = PacketProtection::unprotectLong(
	base, packetLen,
	m_crypto.rxProtectionState(CryptoLevel::Handshake),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    }
    if (plainLen < 0) {
      if (checkStatelessReset_(datagram)) return true;
      ++m_diag.failures;
      return false;
    }
    if (!recordRxPacket_(level, pn)) return true;
    ++m_diag.packetsRx;
    return consumeFrames(
      level, pn, byteSpan(base + payloadOffset, unsigned(plainLen)),
      d.addr, d.buf);
  }

  template <typename ConsumeFrames>
  bool receiveProtectedShortPacket_(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    ConsumeFrames consumeFrames) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{
      reinterpret_cast<const char *>(base), packetLen};
    ZuCSpan datagram{
      reinterpret_cast<const char *>(d.buf->data_()), d.buf->length};
    if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT)) {
      if (checkStatelessReset_(datagram)) return true;
      return false;
    }
    ShortHeader h;
    if (Packet::parseShort(packet, m_localSCID.length(), h) < 0 ||
	!(h.dcid == m_localSCID)) {
      if (checkStatelessReset_(datagram)) return true;
      return false;
    }
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = PacketProtection::unprotectShort(
      base, packetLen,
      m_crypto.rxProtectionState(CryptoLevel::OneRTT),
      m_rxLargestPN[CryptoLevel::OneRTT],
      h.pnOffset, pn, payloadOffset);
    if (plainLen < 0) {
      TrafficSecret nextSecret;
      PacketProtectionState nextState;
      if (PacketProtection::deriveNextTrafficSecret(
	    nextSecret, m_crypto.rxTrafficSecret(CryptoLevel::OneRTT)) &&
	  nextState.init(nextSecret, CryptoLevel::OneRTT, false)) {
	plainLen = PacketProtection::unprotectShort(
	  base, packetLen, nextState,
	  m_rxLargestPN[CryptoLevel::OneRTT],
	  h.pnOffset, pn, payloadOffset);
	if (plainLen >= 0 &&
	    !installPeerKeyUpdate_(nextSecret))
	  return false;
      }
      if (plainLen < 0) {
	if (checkStatelessReset_(datagram)) return true;
	++m_diag.failures;
	return false;
      }
    }
    if (!recordRxPacket_(CryptoLevel::OneRTT, pn))
      return true;
    ++m_diag.packetsRx;
    return consumeFrames(
      CryptoLevel::OneRTT, pn,
      byteSpan(base + payloadOffset, unsigned(plainLen)), d.addr, d.buf);
  }

  template <typename EmitTLS, typename HandleControl>
  bool consumeProtectedFrames_(
    CryptoLevel::T level, uint64_t pn, ZuCSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, EmitTLS emitTLS,
    HandleControl handleControl) {
    unsigned offset = 0;
    auto frame_ = ZmAlloc(Frame, 1);
    new (&frame_[0]) Frame{};
    auto &frame = frame_[0];
    ZuGuard frameGuard{[&frame]() { frame.~Frame(); }};

    while (offset < frames.length()) {
      unsigned used = 0;
      if (FrameCodec::parse(
	    ZuCSpan{frames.data() + offset, frames.length() - offset},
	    frame, used) < 0 || !used)
	return false;
      ++m_diag.framesRx;
      if (!packetFrameLegal_(level, frame)) return false;
      if (FrameCodec::ackEliciting(frame.type))
	noteAck_(level, pn);
      switch (frame.type) {
	case FrameType::Ack:
	  processAckFrame_(level, frame);
	  break;
	case FrameType::Crypto: {
	  ZuCSpan contiguous;
	  if (m_rxCrypto[level].receiveFrame(frame, contiguous) < 0)
	    return false;
	  m_diag.cryptoBytesRx += frame.payload.length();
	  if (contiguous) {
	    size_t epoch = level == CryptoLevel::Initial ? 0 :
	      level == CryptoLevel::Handshake ? 2 : 3;
	    if (!emitTLS(epoch, contiguous, addr)) return false;
	  }
	  break;
	}
	case FrameType::Stream:
	  m_diag.streamBytesRx += frame.payload.length();
	  if (receiveFrame(frame, ZmRef<ZiIOBuf>{packetBuf}) < 0)
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
	  if (!applyMaxData(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::MaxStreamData:
	  if (!applyMaxStreamData(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::MaxStreams:
	  if (!applyMaxStreams(frame) ||
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
	for (unsigned i = 0; i < frame.ackRanges.length(); ++i)
	  if (frame.ackRanges[i].largest >= m_txPN[level]) return false;
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

  unsigned openQueued_(Zi::StreamType::T type) {
    uint64_t &queued = queued_(type);
    unsigned opened = 0;
    while (queued && localLimit_(type).open()) {
      StreamRef stream = openLocalStream_(type);
      if (!stream) break;
      --queued;
      ++opened;
      impl()->streamed(stream);
    }
    return opened;
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
  bool canPeerSend_(uint64_t id) const {
    return !StreamID::uni(id) || !localInitiated_(id, m_isServer);
  }
  bool canLocalSend_(uint64_t id) const {
    return !StreamID::uni(id) || localInitiated_(id, m_isServer);
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
    StreamRef stream;
    if (!validateStreamDataBlocked_(frame, stream)) return false;
    return stream->receiveBlocked(frame);
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

  bool controlQueuedAtLeast_(const ControlFrame &frame) {
    auto iter = m_controlQueue.iter();
    while (auto ptr = iter.ptr()) {
      const ControlFrame &queued = *ptr;
      if (queued.type != frame.type ||
	  queued.streamID != frame.streamID ||
	  queued.streamType != frame.streamType)
	continue;
      if (frame.type == FrameType::PathResponse) {
	if (!memcmp(queued.payload, frame.payload, sizeof(frame.payload)))
	  return true;
	continue;
      }
      if (frame.type == FrameType::HandshakeDone) return true;
      if (queued.value >= frame.value) return true;
    }
    return false;
  }

  bool controlStillValid_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
	return frame.value == m_rxDataCredit.limit();
      case FrameType::MaxStreamData: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->readOpen() &&
	  frame.value == stream->rxCreditLimit();
      }
      case FrameType::MaxStreams:
	return frame.value == peerLimit_(frame.streamType).limit();
      case FrameType::DataBlocked:
	return m_txDataCredit.blocked() &&
	  frame.value == m_txDataCredit.limit();
      case FrameType::StreamDataBlocked: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && !stream->txCreditAvailable() &&
	  frame.value == stream->txCreditLimit();
      }
      case FrameType::StreamsBlocked:
	return queued_(frame.streamType) &&
	  frame.value == localLimit_(frame.streamType).limit();
      case FrameType::PathResponse:
      case FrameType::HandshakeDone:
	return true;
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
  void maybeReturnStreamCredit_(const StreamRef &stream) {
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
    uint64_t next = limit.limit() == uint64_t(-1) ?
      uint64_t(-1) : limit.limit() + 1;
    limit.extend(next);
    stream->markStreamCreditReturned();
    queueFlowUpdate_(FlowUpdate{FrameType::MaxStreams, 0, limit.limit(), type});
  }

  StreamRef newStream_(int64_t id) {
    auto node = new typename Streams::Node{impl(), id};
    StreamRef stream{node};
    stream->txCredit(initialStreamTxCredit_(uint64_t(id)));
    stream->rxCredit(initialStreamRxCredit_(uint64_t(id)));
    m_streams.addNode(node);
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

  App		*m_app = nullptr;
  bool		m_isServer = false;
  bool		m_closed = false;
  uint64_t	m_closeError = 0;
  FlowCredit	m_txDataCredit;
  FlowCredit	m_rxDataCredit;
  uint64_t	m_rxDataWindow = 0;
  uint64_t	m_nextBidiOrdinal = 0;
  uint64_t	m_nextUniOrdinal = 0;
  StreamLimit	m_peerBidiLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_peerUniLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_localBidiLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_localUniLimit{uint64_t(INT64_MAX) >> 2};
  uint64_t	m_queuedBidi = 0;
  uint64_t	m_queuedUni = 0;
  uint64_t	m_lastDataBlocked = uint64_t(-1);
  uint64_t	m_lastStreamsBlockedBidi = uint64_t(-1);
  uint64_t	m_lastStreamsBlockedUni = uint64_t(-1);

  Streams	m_streams; // rx thread dedicated, used to dispatch received data
  StreamScheduler m_streamScheduler;
  ControlQueue	m_controlQueue{ZmQueueParams{}.initial(8)};

private:
  void resetLinkState_() {
    m_linkState = LinkState::Starting;
    m_runtimeCloseState = CloseState::Open;
    m_runtimeCloseError = 0;
    m_drainPTOs = 0;
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

  RuntimeDiag		m_diag;
  Crypto		m_crypto;
  TransportParams	m_transportParams;
  CryptoStream		m_txCrypto[3];
  CryptoStream		m_rxCrypto[3];
  CxnID			m_initialDCID;
  CxnID			m_localSCID;
  CxnID			m_peerCID;
  ResetToken		m_peerResetToken;
  LocalCIDs		m_localCIDs;
  PeerCIDs		m_peerCIDs;
  uint64_t		m_peerRetirePriorTo = 0;
  uint64_t		m_txPN[3]{};
  ZmLock		m_txLock;
  uint64_t		m_rxLargestPN[3]{};
  AckTracker		m_rxPackets[3];
  PacketTxSpace		m_txPackets[3];
  bool			m_pendingAck[3]{};
  bool			m_txKeyPhase = false;
  unsigned		m_handshakeStarted = 0;
  unsigned		m_established = 0;
  LinkState::T		m_linkState = LinkState::Starting;
  CloseState::T		m_runtimeCloseState = CloseState::Open;
  uint64_t		m_runtimeCloseError = 0;
  unsigned		m_drainPTOs = 0;
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

  CliLink(App *app) : Base{app, false} { }
  CliLink(App *app, Host server, uint16_t port) :
    Base{app, false}, m_server{ZuMv(server)}, m_port{port} { }
  ~CliLink() {
    closeCurrent_(false);
  }

  void connect() {
    if (!app() || !app()->mx()) {
      connectFailed_0(false);
      return;
    }
    app()->rxInvoke([this]() { connect_(); });
  }
  void connect(Host server, uint16_t port) {
    m_server = ZuMv(server);
    m_port = port;
    connect();
  }

  void disconnect() {
    if (!app() || !app()->mx()) {
      disconnect_();
      return;
    }
    app()->rxInvoke([this]() { disconnect_(); });
  }
  void disconnect_() {
    closeCurrent_(true);
    resetRuntimeState_();
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
  const EndpointDiag &cxnDiag() const { return m_endpoint.diag(); }
  const EndpointDiag &endpointDiag() const { return m_endpoint.diag(); }
  const RuntimeDiag &runtimeDiag() const { return Base::runtimeDiag_(); }
  const Crypto &crypto() const { return Base::crypto_(); }

  bool send(StreamRef stream, ZuCSpan payload, bool fin = true) {
    if (!stream || (!payload.length() && !fin))
      return false;
    if (app()->txInvoked()) return send_(ZuMv(stream), payload, fin);
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
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client send_ outside Tx thread", return false);
    return Base::withTxLock_([this, stream = ZuMv(stream), payload, fin]() mutable {
      if (Base::closed() || !stream || !Base::runtimeEstablished_() ||
	  (!payload.length() && !fin))
	return false;
      if (payload.length()) {
	auto tx = stream->txStream_();
	tx << payload;
	tx.flush();
      }
      if (fin) stream->fin();
      return Base::flushControlAndStreamsLocked_(
	m_endpoint.remote(),
	[this](PacketBuild &build) {
	  return appendPendingAck_(CryptoLevel::OneRTT, build);
	},
	[this](
	    PacketBuild &build, ZiSockAddr addr,
	    const SentFrameRef &ref) {
	  return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
	});
      });
  }

  void pto_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client PTO outside Tx thread", return);
    if (Base::closed() || !Base::runtimeEstablished_() ||
	!m_endpoint.connected())
      return;
    Base::reclaimPTO_();
    SentFrameRef ref;
    bool sent = false;
    while (Base::nextRetransmit_(ref)) {
      PacketBuild build;
      if (ref.kind == SentFrameKind::Stream) {
	if (!Base::buildRetransmitStream_(build, ref)) continue;
      } else if (!Base::buildRetransmitControl_(build, ref))
	continue;
      (void)sendShortPacket_(build, m_endpoint.remote(), {}, &ref, true);
      sent = true;
    }
    if (sent) Base::schedulePTO_();
  }

  void connect_() {
    if (!app() || !app()->mx()) {
      connectFailed_0(false);
      return;
    }
    ZiIP ip = m_server;
    if (!ip || !m_port) {
      app()->error_(ZeEXCEPT(Error, "Zquic",
	([server = LogMsg{m_server}, port = m_port](auto &s) {
	  s << '"' << server << "\": invalid QUIC UDP peer port=" << port;
	})));
      connectFailed_0(true);
      return;
    }

    closeCurrent_(false);
    resetRuntimeState_();
    Base::resetRuntimeDiag_();

    if (!m_endpoint.openUDP(
	app()->mx(),
	PathMode::ClientConnected,
	ZiIP{}, 0, ip, m_port,
	Endpoint::DatagramFn{[link = ZmMkRef(impl())](Datagram d) mutable {
	  link->app()->rxRun([link, d = ZuMv(d)]() mutable {
	    link->received_(ZuMv(d));
	  });
	}},
	Endpoint::ReadyFn{[link = ZmMkRef(impl())](Endpoint *ep) mutable {
	  link->app()->rxRun([link, ep]() mutable {
	    link->endpointReady_(ep);
	  });
	}},
	Endpoint::FailFn{[link = ZmMkRef(impl())](bool transient) mutable {
	  link->connectFailed_0(transient);
	    }},
	Endpoint::DownFn{[link = ZmMkRef(impl())](Endpoint *ep) mutable {
	  link->app()->rxRun([link, ep]() mutable {
	    link->endpointDown_(ep);
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
    m_udpReady = 0;
    m_notifyEndpointDown = notify;
    m_endpoint.closeUDP();
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
	m_server}))
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
    return Base::sendCryptoFlights_(
      data, len, offsets, RuntimeCryptoChunk, ZuMv(addr),
      [this](
	  CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) {
	return sendCryptoPacket_(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
  }

  bool sendCryptoPacket_(CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr) {
    return Base::sendCryptoPacket_(
      level, frame, ZuMv(addr),
      [this](CryptoLevel::T level_, PacketBuild &build, ZuCSpan frame_) {
	return buildPayload_(level_, build, frame_);
      },
      [this](PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendInitialPacket_(build, ZuMv(addr_), frame_);
      },
      [this](PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendHandshakePacket_(build, ZuMv(addr_), frame_);
      },
      [this](PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendShortPacket_(build, ZuMv(addr_), frame_);
      });
  }

  bool sendCryptoPacket_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    return Base::sendCryptoPacket_(
      level, prefix, payload, ref, ZuMv(addr),
      [this](
	  CryptoLevel::T level_, PacketBuild &build,
	  ZuCSpan prefix_, ZuCSpan payload_) {
	return buildPayload_(level_, build, prefix_, payload_);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendInitialPacket_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendHandshakePacket_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendShortPacket_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      });
  }

  bool appendPendingAck_(
    CryptoLevel::T level, PacketBuild &build) {
    return Base::appendPendingAck_(level, build);
  }

  bool buildPayload_(CryptoLevel::T level, PacketBuild &build, ZuCSpan frame) {
    return Base::buildPayload_(level, build, frame);
  }

  bool buildPayload_(
    CryptoLevel::T level, PacketBuild &build,
    ZuCSpan prefix, ZuCSpan payload) {
    return Base::buildPayload_(level, build, prefix, payload);
  }

  bool sendInitialPacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Initial, payload, frame)) return false;
    return sendInitialPacket_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_endpoint.connected()) return false;
    return Base::sendProtectedInitialPacket_(
      InitialKeyDir::Client, RuntimeCID::Initial, RuntimeCID::Local,
      RuntimePNLength, true, payload, ZuMv(addr), recordFrame,
      recordRef, ackEliciting,
      [this]() { return m_endpoint.allocTxPacket(); },
      [this](auto buf, ZiSockAddr addr_) {
	return m_endpoint.send(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendHandshakePacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Handshake, payload, frame)) return false;
    return sendHandshakePacket_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_endpoint.connected()) return false;
    return Base::sendProtectedHandshakePacket_(
      RuntimeCID::Peer, RuntimeCID::Local, RuntimePNLength,
      payload, ZuMv(addr), recordFrame, recordRef,
      ackEliciting,
      [this]() { return m_endpoint.allocTxPacket(); },
      [this](auto buf, ZiSockAddr addr_) {
	return m_endpoint.send(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendShortPacket_(ZuCSpan payload, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(CryptoLevel::OneRTT, build, payload)) return false;
    return sendShortPacket_(build, ZuMv(addr), payload);
  }

  bool sendShortPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_endpoint.connected()) return false;
    return Base::sendProtectedShortPacket_(
      RuntimeCID::Peer, RuntimePNLength, payload, ZuMv(addr), recordFrame,
      recordRef, ackEliciting,
      [this]() { return m_endpoint.allocTxPacket(); },
      [this](auto buf, ZiSockAddr addr_) {
	return m_endpoint.send(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendQueuedStreamPacket_(StreamRef stream, ZiSockAddr addr) {
    return Base::sendQueuedStreamPacket_(
      ZuMv(stream), ZuMv(addr),
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
      });
  }

  bool flushPendingAck_(CryptoLevel::T level, ZiSockAddr addr) {
    PacketBuild build;
    build.reset();
    if (!appendPendingAck_(level, build)) return false;
    if (!build.bytes()) return true;
    if (level == CryptoLevel::Initial)
      return sendInitialPacket_(build, ZuMv(addr), {});
    if (level == CryptoLevel::Handshake)
      return sendHandshakePacket_(build, ZuMv(addr), {});
    return sendShortPacket_(build, ZuMv(addr), {});
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
    flushPendingAcks_(ZuMv(addr));
    (void)Base::flushControlAndStreams_(
      m_endpoint.remote(),
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
      });
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtectedLongPacket_(
      InitialKeyDir::Server, d, packetOffset, packetLen,
      [this](const LongHeader &h, Datagram &) {
	Base::setPeerCIDFromHeaderSCID_(h);
	return true;
      },
      [this](
	  CryptoLevel::T level, uint64_t pn, ZuCSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf) {
	return consumeFrames_(level, pn, frames, ZuMv(addr), packetBuf);
      });
  }

  bool receivedShort_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtectedShortPacket_(
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
    return Base::consumeProtectedFrames_(
      level, pn, frames, ZuMv(addr), packetBuf,
      [this](size_t epoch, ZuCSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      },
      [this](
	  CryptoLevel::T level_, const Frame &frame, ZiSockAddr addr_) {
	return handleControlFrame_(level_, frame, ZuMv(addr_));
      });
  }

  bool handleControlFrame_(
    CryptoLevel::T, const Frame &frame, ZiSockAddr addr) {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	(void)Base::flushControlAndStreams_(
	  m_endpoint.remote(),
	  [this](PacketBuild &build) {
	    return appendPendingAck_(CryptoLevel::OneRTT, build);
	  },
	  [this](
	      PacketBuild &build, ZiSockAddr addr_,
	      const SentFrameRef &ref) {
	    return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
	  });
	return true;
      case FrameType::PathChallenge: {
	Base::queuePathResponse_(frame.payload);
	return Base::flushControlAndStreams_(
	  ZuMv(addr),
	  [this](PacketBuild &build) {
	    return appendPendingAck_(CryptoLevel::OneRTT, build);
	  },
	  [this](
	      PacketBuild &build, ZiSockAddr addr_,
	      const SentFrameRef &ref) {
	    return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
	  });
      }
      case FrameType::PathResponse:
      case FrameType::HandshakeDone:
	return true;
      case FrameType::ConnectionClose:
      case FrameType::ApplicationClose:
	Base::closeRuntime_(frame.errorCode);
	closeCurrent_(true);
	return true;
      default:
	return true;
    }
  }

  void dataBlocked_(uint64_t maximum) {
    Base::queueBlocked_(FrameType::DataBlocked, 0, maximum);
    (void)Base::flushControlAndStreams_(
      m_endpoint.remote(),
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
      });
  }
  void streamDataBlocked_(uint64_t streamID, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamDataBlocked, streamID, maximum);
    (void)Base::flushControlAndStreams_(
      m_endpoint.remote(),
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
      });
  }
  void streamsBlocked_(Zi::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamsBlocked, 0, maximum, type);
    (void)Base::flushControlAndStreams_(
      m_endpoint.remote(),
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
      });
  }

  void endpointReady_(Endpoint *ep) {
    if (ep != &m_endpoint) return;
    m_udpReady = 1;
    m_notifyEndpointDown = true;
    ++m_udpReadyCount;
    Base::endpointReady_();
    startHandshake_();
  }

  void endpointDown_(Endpoint *ep) {
    if (ep != &m_endpoint) return;
    m_udpReady = 0;
    if (m_notifyEndpointDown) {
      impl()->disconnected();
    }
    m_notifyEndpointDown = true;
  }

  void connectFailed_0(bool transient) {
    Base::endpointFailure_();
    if (!app() || !app()->mx()) {
      impl()->connectFailed(transient);
      return;
    }
    app()->rxRun([link = ZmMkRef(impl()), transient]() {
      link->connectFailed(transient);
    });
  }

  Endpoint		m_endpoint;
  Host			m_server;
  uint16_t		m_port = 0;
  ClientBootstrap	m_bootstrap;
  bool			m_peerParamsValidated = false;
  bool			m_notifyEndpointDown = true;
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

  template <typename, typename> friend class Server;

  SrvLink(App *app) : Base{app, true} { }

  bool established() const { return Base::runtimeEstablished_(); }
  const RuntimeDiag &runtimeDiag() const { return Base::runtimeDiag_(); }
  const Crypto &crypto() const { return Base::crypto_(); }
  const ZiSockAddr &peer() const { return m_peerAddr; }

  bool send(StreamRef stream, ZuCSpan payload, bool fin = true) {
    if (!stream || (!payload.length() && !fin))
      return false;
    if (app()->txInvoked()) return send_(ZuMv(stream), payload, fin);
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
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server send_ outside Tx thread", return false);
    return Base::withTxLock_([this, stream = ZuMv(stream), payload, fin]() mutable {
      if (Base::closed() || !stream || !Base::runtimeEstablished_() ||
	  (!payload.length() && !fin))
	return false;
      if (payload.length()) {
	auto tx = stream->txStream_();
	tx << payload;
	tx.flush();
      }
      if (fin) stream->fin();
      return Base::flushControlAndStreamsLocked_(
	m_peerAddr,
	[this](PacketBuild &build) {
	  return appendPendingAck_(CryptoLevel::OneRTT, build);
	},
	[this](
	    PacketBuild &build, ZiSockAddr addr,
	    const SentFrameRef &ref) {
	  return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
	});
      });
  }

  void pto_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server PTO outside Tx thread", return);
    if (Base::closed() || !Base::runtimeEstablished_() || !m_peerAddr)
      return;
    Base::reclaimPTO_();
    SentFrameRef ref;
    bool sent = false;
    while (Base::nextRetransmit_(ref)) {
      PacketBuild build;
      if (ref.kind == SentFrameKind::Stream) {
	if (!Base::buildRetransmitStream_(build, ref)) continue;
      } else if (!Base::buildRetransmitControl_(build, ref))
	continue;
      (void)sendShortPacket_(build, m_peerAddr, {}, &ref, true);
      sent = true;
    }
    if (sent) Base::schedulePTO_();
  }

  void close(uint64_t errorCode = 0) {
    if (Base::closed()) return;
    Base::close(errorCode);
    if (!app() || !app()->mx()) {
      close_(errorCode);
      return;
    }
    app()->rxInvoke([link = ZmMkRef(impl()), errorCode]() {
      link->close_(errorCode);
    });
  }

private:
  using InitialKeyDir = typename Base::InitialKeyDir;
  using RuntimeCID = typename Base::RuntimeCID;

  void close_(uint64_t errorCode = 0) {
    Base::closeRuntime_(errorCode);
    if (app())
      static_cast<Server<App, Impl> *>(app())->releaseLink_(impl());
  }

  void resetRuntimeState_() {
    Base::resetRuntime_();
    m_peerAddr.null();
    m_bootstrap = {};
    m_handshakeDoneSent = 0;
  }

  bool initRuntimeCrypto_(const LongHeader &h, unsigned datagramLen) {
    if (h.type != PacketType::Initial ||
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
	{}}))
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
	  return sendHandshakeDone_(ZuMv(addr_));
	return true;
      });
  }

  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    return Base::sendCryptoFlights_(
      data, len, offsets, RuntimeCryptoChunk, ZuMv(addr),
      [this](
	  CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) {
	return sendCryptoPacket_(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
  }

  bool sendCryptoPacket_(CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr) {
    return Base::sendCryptoPacket_(
      level, frame, ZuMv(addr),
      [this](CryptoLevel::T level_, PacketBuild &build, ZuCSpan frame_) {
	return buildPayload_(level_, build, frame_);
      },
      [this](PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendInitialPacket_(build, ZuMv(addr_), frame_);
      },
      [this](PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendHandshakePacket_(build, ZuMv(addr_), frame_);
      },
      [this](PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_) {
	return sendShortPacket_(build, ZuMv(addr_), frame_);
      });
  }

  bool sendCryptoPacket_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    return Base::sendCryptoPacket_(
      level, prefix, payload, ref, ZuMv(addr),
      [this](
	  CryptoLevel::T level_, PacketBuild &build,
	  ZuCSpan prefix_, ZuCSpan payload_) {
	return buildPayload_(level_, build, prefix_, payload_);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendInitialPacket_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendHandshakePacket_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_, ZuCSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendShortPacket_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      });
  }

  bool appendPendingAck_(CryptoLevel::T level, PacketBuild &build) {
    return Base::appendPendingAck_(level, build);
  }

  bool buildPayload_(CryptoLevel::T level, PacketBuild &build, ZuCSpan frame) {
    return Base::buildPayload_(level, build, frame);
  }

  bool buildPayload_(
    CryptoLevel::T level, PacketBuild &build,
    ZuCSpan prefix, ZuCSpan payload) {
    return Base::buildPayload_(level, build, prefix, payload);
  }

  bool sendInitialPacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Initial, payload, frame)) return false;
    return sendInitialPacket_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    return Base::sendProtectedInitialPacket_(
      InitialKeyDir::Server, RuntimeCID::Peer, RuntimeCID::Local,
      RuntimePNLength, false, payload, ZuMv(addr), recordFrame,
      recordRef, ackEliciting,
      [this]() { return app()->allocTxPacket_(); },
      [this, recordRef](auto buf, ZiSockAddr addr_) {
	if (recordRef && !app()->sendFrame(*recordRef)) return true;
	return app()->sendPacket_(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendHandshakePacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Handshake, payload, frame)) return false;
    return sendHandshakePacket_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    return Base::sendProtectedHandshakePacket_(
      RuntimeCID::Peer, RuntimeCID::Local, RuntimePNLength,
      payload, ZuMv(addr), recordFrame, recordRef,
      ackEliciting,
      [this]() { return app()->allocTxPacket_(); },
      [this](auto buf, ZiSockAddr addr_) {
	return app()->sendPacket_(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendShortPacket_(ZuCSpan payload, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(CryptoLevel::OneRTT, build, payload)) return false;
    return sendShortPacket_(build, ZuMv(addr), payload);
  }

  bool sendShortPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    return Base::sendProtectedShortPacket_(
      RuntimeCID::Peer, RuntimePNLength, payload, ZuMv(addr), recordFrame,
      recordRef, ackEliciting,
      [this]() { return app()->allocTxPacket_(); },
      [this, recordRef](auto buf, ZiSockAddr addr_) {
	if (recordRef && !app()->sendFrame(*recordRef)) return true;
	return app()->sendPacket_(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendHandshakeDone_(ZiSockAddr addr) {
    Base::queueHandshakeDone_();
    if (!Base::flushControlAndStreams_(
	ZuMv(addr),
	[this](PacketBuild &build) {
	  return appendPendingAck_(CryptoLevel::OneRTT, build);
	},
	[this](
	    PacketBuild &build, ZiSockAddr addr_,
	    const SentFrameRef &ref) {
	  return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
	}))
      return false;
    m_handshakeDoneSent = 1;
    Base::handshakeDoneTx_();
    return true;
  }

  bool sendQueuedStreamPacket_(StreamRef stream, ZiSockAddr addr) {
    return Base::sendQueuedStreamPacket_(
      ZuMv(stream), ZuMv(addr),
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
      });
  }

  bool flushPendingAck_(CryptoLevel::T level, ZiSockAddr addr) {
    PacketBuild build;
    build.reset();
    if (!appendPendingAck_(level, build)) return false;
    if (!build.bytes()) return true;
    if (level == CryptoLevel::Initial)
      return sendInitialPacket_(build, ZuMv(addr), {});
    if (level == CryptoLevel::Handshake)
      return sendHandshakePacket_(build, ZuMv(addr), {});
    return sendShortPacket_(build, ZuMv(addr), {});
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
    flushPendingAcks_(ZuMv(addr));
    (void)Base::flushControlAndStreams_(
      m_peerAddr,
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr_,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
      });
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtectedLongPacket_(
      InitialKeyDir::Client, d, packetOffset, packetLen,
      [this](const LongHeader &h, Datagram &d_) {
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
    return Base::receiveProtectedShortPacket_(
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
    return Base::consumeProtectedFrames_(
      level, pn, frames, ZuMv(addr), packetBuf,
      [this](size_t epoch, ZuCSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      },
      [this](
	  CryptoLevel::T level_, const Frame &frame, ZiSockAddr addr_) {
	return handleControlFrame_(level_, frame, ZuMv(addr_));
      });
  }

  bool handleControlFrame_(
    CryptoLevel::T, const Frame &frame, ZiSockAddr addr) {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	(void)Base::flushControlAndStreams_(
	  m_peerAddr,
	  [this](PacketBuild &build) {
	    return appendPendingAck_(CryptoLevel::OneRTT, build);
	  },
	  [this](
	      PacketBuild &build, ZiSockAddr addr_,
	      const SentFrameRef &ref) {
	    return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
	  });
	return true;
      case FrameType::PathChallenge: {
	Base::queuePathResponse_(frame.payload);
	return Base::flushControlAndStreams_(
	  ZuMv(addr),
	  [this](PacketBuild &build) {
	    return appendPendingAck_(CryptoLevel::OneRTT, build);
	  },
	  [this](
	      PacketBuild &build, ZiSockAddr addr_,
	      const SentFrameRef &ref) {
	    return sendShortPacket_(build, ZuMv(addr_), {}, &ref, true);
	  });
      }
      case FrameType::PathResponse:
	return true;
      case FrameType::ConnectionClose:
      case FrameType::ApplicationClose:
	close_(frame.errorCode);
	impl()->disconnected();
	return true;
      case FrameType::HandshakeDone:
	return false;
      default:
	return true;
    }
  }

  void dataBlocked_(uint64_t maximum) {
    Base::queueBlocked_(FrameType::DataBlocked, 0, maximum);
    (void)Base::flushControlAndStreams_(
      m_peerAddr,
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
      });
  }
  void streamDataBlocked_(uint64_t streamID, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamDataBlocked, streamID, maximum);
    (void)Base::flushControlAndStreams_(
      m_peerAddr,
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
      });
  }
  void streamsBlocked_(Zi::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamsBlocked, 0, maximum, type);
    (void)Base::flushControlAndStreams_(
      m_peerAddr,
      [this](PacketBuild &build) {
	return appendPendingAck_(CryptoLevel::OneRTT, build);
      },
      [this](
	  PacketBuild &build, ZiSockAddr addr,
	  const SentFrameRef &ref) {
	return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
      });
  }

  void receivedRouted_(Datagram d) {
    received_(ZuMv(d));
  }

  void installRoutes_(CxnRouter<Impl> &routes) {
    routes.add(m_bootstrap.originalDCID(), 0, impl());
    Base::installLocalCIDRoutes_(routes);
  }

  void retireRoutes_(CxnRouter<Impl> &routes) {
    routes.tombstone(m_bootstrap.originalDCID());
    Base::retireLocalCIDRoutes_(routes);
  }

public:
  void retiredLocalCID_(uint64_t, const CxnID &id) {
    if (app())
      static_cast<Server<App, Impl> *>(app())->dissociateRoute_(id);
  }

  ServerBootstrap	m_bootstrap;
  ZiSockAddr		m_peerAddr;
  ZmAtomic<unsigned>	m_handshakeDoneSent = 0;
};

} // namespace Zquic

#endif /* Zquic_HH */
