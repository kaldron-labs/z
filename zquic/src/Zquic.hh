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
#include <zlib/ZuElem.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmAlloc.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtLocalArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#if defined(ZDEBUG) && !defined(Zquic_DEBUG)
#define Zquic_DEBUG	// enable testing / debugging
#endif

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicStream.hh>
#include <zlib/ZquicSched.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicTransport.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZquicRecovery.hh>
#include <zlib/ZquicSock.hh>
#include <zlib/ZquicLog.hh>

namespace Zquic {

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Zquic.Log">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Zquic.ParamStrings">>));

struct InitialInfo {
  LongHdr	header;
  ZiSockAddr	peer;
  unsigned	datagramLength = 0;
  CxnID		origDCID;
  CxnID		retrySCID;
  bool		addressValidated = false;
  int8_t	tokenKind = 0;
};

ZuDerive(ALPNData, (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.ALPNData">>));
ZuDerive(ALPN, (ZtArray<ptls_iovec_t, ZtArrayHeapID<"Zquic.ALPN">>));
ZuDerive(AsyncSendPayload,
  (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.AsyncSendPayload">>));
ZuDerive(TokenBytes,
  (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.TokenBytes">>));
ZuDerive(TokenSecret,
  (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.TokenSecret">>));

using ErrorFn = ZmFn<void(ZeException)>;

inline constexpr uint64_t DefaultMaxData = 16U * 1024U * 1024U;
inline constexpr uint64_t DefaultMaxStreamData = 1U * 1024U * 1024U;
inline constexpr uint64_t DefaultMaxStreamsBidi = 128;
inline constexpr uint64_t DefaultMaxStreamsUni = 16;
inline constexpr uint64_t MaxStreamCount = uint64_t(INT64_MAX) >> 2;
inline constexpr uint64_t U64Null = ZuCmp<uint64_t>::null();
inline constexpr uint64_t DefaultTokenLifetime = 600;

struct TokenKind {
  ZtEnum(TokenKind, int8_t, Retry = 1, NewToken = 2);
};

struct TokenStatus {
  ZtEnum(TokenStatus, int8_t,
    OK, Malformed, Expired, Kind, Address, ODCID, Auth);
};

struct TokenInfo {
  TokenKind::T	kind = TokenKind::Retry;
  CxnID		origDCID;
  CxnID		serverCID;
};

struct AddressToken {
  enum {
    SecretLength = 32,
    NonceLength = 16,
    TagLength = 16,
    MaxLength = 128
  };

  static bool generateSecret(TokenSecret &);
  static bool encode(
    TokenBytes &, TokenKind::T, ZuBSpan secret, const ZiSockAddr &,
    const CxnID &origDCID, const CxnID &serverCID,
    uint64_t nowSec, bool bindPort);
  static TokenStatus::T validate(
    TokenInfo &, ZuBSpan token, ZuBSpan secret, const ZiSockAddr &,
    uint64_t nowSec, uint64_t lifetimeSec, bool bindPort);
};

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

inline ZuBSpan byteSpan(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

inline bool runtimeFrameRef(
  ZuBSpan bytes, SentFrameRef &ref, bool &ackEliciting)
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

struct VersionNeg {
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
    ZuBSpan in, uint32_t *versions, unsigned capacity, unsigned &nVersions) {
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
    ZuBSpan, uint8_t *response, unsigned responseLen);
};

struct AddressValidationDiag {
  uint64_t	retrySent = 0;
  uint64_t	retryAccepted = 0;
  uint64_t	retryRejected = 0;
  uint64_t	newTokenAccepted = 0;
  uint64_t	tokenExpired = 0;
  uint64_t	tokenAuthFailure = 0;
  uint64_t	tokenAddressMismatch = 0;
  uint64_t	tokenMalformed = 0;
  uint64_t	tokenKindMismatch = 0;
  uint64_t	tokenODCIDMismatch = 0;
};

struct StatelessRst {
  static constexpr unsigned TokenLength = ResetToken::Length;
  static constexpr unsigned MinLength = 21;

  static int decode(ResetToken &, ZuBSpan datagram);
  static bool verify(ZuBSpan datagram, const ResetToken &);
  static int writeForUnknownCID(
    uint8_t *, unsigned, ZuBSpan receivedPkt, const ResetToken &);
};


} // namespace Zquic

#ifndef Zquic_Private_HH
#include <zlib/Zquic_.hh>
#endif

namespace Zquic {

using Zquic_::Cxn;
using Zquic_::CxnRouter;
using Zquic_::Endpoint_;
using Zquic_::ServerLinks_;

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
  ZuBSpan retryToken() const { return m_retryToken; }
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
    m_retryToken.length(0);
    m_started = true;
    m_retried = false;
    return true;
  }
  bool startRandom(
    unsigned dcidLength = CxnIDGen::InitialLength,
    unsigned scidLength = CxnIDGen::InitialLength);

  bool onRetry(ZuBSpan packet) {
    RetryPkt retry;
    if (!m_started ||
	Pkt::parseRetry(packet, retry) < 0 ||
	!Pkt::validateRetryIntegrity(packet, m_initialDCID))
      return false;
    return onRetry(retry);
  }

  bool onRetry(const RetryPkt &retry) {
    if (!m_started ||
	m_retried ||
	retry.header.type != PktType::Retry ||
	retry.header.scid.length() < MinCIDLength ||
	!retry.token ||
	retry.integrityTag.length() != 16)
      return false;
    m_retrySCID = retry.header.scid;
    m_retryTokenLength = retry.token.length();
    m_retryToken.length(0);
    m_retryToken.append(retry.token.data(), retry.token.length());
    m_retried = true;
    return true;
  }

  bool validateServerTransportParams(
    const TransportParams &params,
    const CxnID &serverInitialSCID) const {
    if (!m_started ||
	!(params.origDCID == m_initialDCID) ||
	!(params.initialSCID == serverInitialSCID))
      return false;
    if (m_retried)
      return params.retrySCID == m_retrySCID;
    return !params.retrySCID;
  }

private:
  CxnID		m_initialDCID;
  CxnID		m_initialSCID;
  CxnID		m_retrySCID;
  TokenBytes	m_retryToken;
  uint64_t	m_retryTokenLength = 0;
  bool		m_started = false;
  bool		m_retried = false;
};

class ServerBootstrap {
public:
  bool accepted() const { return m_accepted; }
  const CxnID &initialDCID() const { return m_initialDCID; }
  const CxnID &origDCID() const { return m_origDCID; }
  const CxnID &clientInitialSCID() const { return m_clientInitialSCID; }
  const CxnID &localInitialSCID() const { return m_localInitialSCID; }
  const ResetToken &statelessResetToken() const {
    return m_statelessResetToken;
  }

  bool acceptInitial(const LongHdr &, unsigned datagramLength);
  bool acceptInitial(
    const InitialInfo &, const LongHdr &, unsigned datagramLength);
  bool transportParams(TransportParams &) const;

private:
  CxnID		m_initialDCID;
  CxnID		m_origDCID;
  CxnID		m_clientInitialSCID;
  CxnID		m_localInitialSCID;
  CxnID		m_retrySCID;
  ResetToken	m_statelessResetToken;
  bool		m_accepted = false;
  bool		m_retried = false;
};

#ifdef Zquic_DEBUG
using RuntimeDiagCounter = uint64_t;
#else
struct RuntimeDiagCounter {
  RuntimeDiagCounter() = default;
  RuntimeDiagCounter(uint64_t) { }

  RuntimeDiagCounter &operator =(uint64_t) { return *this; }
  RuntimeDiagCounter &operator =(uint32_t) { return *this; }
  RuntimeDiagCounter &operator =(bool) { return *this; }
  RuntimeDiagCounter &operator ++() { return *this; }
  RuntimeDiagCounter operator ++(int) { return {}; }
  RuntimeDiagCounter &operator +=(uint64_t) { return *this; }
  RuntimeDiagCounter &operator +=(uint32_t) { return *this; }
  operator uint64_t() const { return 0; }
};
#endif

struct RuntimeRxDiag {
  AckECN	ecnRx[3];

  [[no_unique_address]] RuntimeDiagCounter endpointReady = 0;
  [[no_unique_address]] RuntimeDiagCounter datagramsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter bytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter packetsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter framesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter duplicatePacketsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackCommitsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackElicitingRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackImmediateRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackSnapshotPostsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamNoDataRx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoBytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter invalidStreamFrames = 0;
  [[no_unique_address]] RuntimeDiagCounter closedStreamFrames = 0;
  [[no_unique_address]] RuntimeDiagCounter suspiciousStreamCloses = 0;
  [[no_unique_address]] RuntimeDiagCounter streamMaxClosedRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamMaxInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamCtlClosedRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamCtlInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataStateRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataFinalRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamRxDeqStateRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamRxDeqFinalRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBlockedClosedRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBlockedInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBlockedFinalRx = 0;
  [[no_unique_address]] RuntimeDiagCounter unhandledAppEvents = 0;
  [[no_unique_address]] RuntimeDiagCounter peerKeyUpdates = 0;
  [[no_unique_address]] RuntimeDiagCounter invalidKeyPhases = 0;
  [[no_unique_address]] RuntimeDiagCounter oldKeysAccepted = 0;
  [[no_unique_address]] RuntimeDiagCounter keyDiscards = 0;
  [[no_unique_address]] RuntimeDiagCounter newTokenRx = 0;
  [[no_unique_address]] RuntimeDiagCounter failures = 0;
  [[no_unique_address]] RuntimeDiagCounter handshakeComplete = 0;
};

struct RuntimeTxDiag {
  static constexpr unsigned Spaces = 3;

  uint64_t	packetsTx = 0;
  bool		ptoTimerActive = false;
  bool		lossTimerActive = false;
  AckECN	peerAckECN[3];

  [[no_unique_address]] RuntimeDiagCounter bytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoBytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackOnlyPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamOnlyPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackStreamPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackSnapshotInstallsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackDueInstallsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackAppendTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackAppendEmptyTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackAppendNotDueTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackSentTx = 0;
  [[no_unique_address]] RuntimeDiagCounter controlOnlyPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackControlPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamControlPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackStreamControlPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter otherPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamFramesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter controlFramesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoFramesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter maxDataTx = 0;
  [[no_unique_address]] RuntimeDiagCounter maxStreamDataTx = 0;
  [[no_unique_address]] RuntimeDiagCounter maxStreamsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter dataBlockedTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataBlockedTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamsBlockedTx = 0;
  [[no_unique_address]] RuntimeDiagCounter resetStreamTx = 0;
  [[no_unique_address]] RuntimeDiagCounter stopSendingTx = 0;
  [[no_unique_address]] RuntimeDiagCounter pathChallengeTx = 0;
  [[no_unique_address]] RuntimeDiagCounter pathResponseTx = 0;
  [[no_unique_address]] RuntimeDiagCounter handshakeDoneTx = 0;
  [[no_unique_address]] RuntimeDiagCounter newTokenTx = 0;
  [[no_unique_address]] RuntimeDiagCounter pathRxObserved = 0;
  [[no_unique_address]] RuntimeDiagCounter pathRxSame = 0;
  [[no_unique_address]] RuntimeDiagCounter pathRxNull = 0;
  [[no_unique_address]] RuntimeDiagCounter pathValidationActive = 0;
  [[no_unique_address]] RuntimeDiagCounter pathValidationStarted = 0;
  [[no_unique_address]] RuntimeDiagCounter pathValidationPromoted = 0;
  [[no_unique_address]] RuntimeDiagCounter pathResponseUnknown = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoSched = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoNoLevel = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoArmed = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoExpired = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoFlush = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoRetx = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoProbe = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoCount = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoBackoff = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoTimeoutUS = 0;
  [[no_unique_address]] RuntimeDiagCounter retransmittedFrames = 0;
  [[no_unique_address]] RuntimeDiagCounter lossArmed = 0;
  [[no_unique_address]] RuntimeDiagCounter lossCanceled = 0;
  [[no_unique_address]] RuntimeDiagCounter lossExpired = 0;
  [[no_unique_address]] RuntimeDiagCounter pktBytesInFlight[Spaces] = {};
  [[no_unique_address]] RuntimeDiagCounter sentPackets[Spaces] = {};
  [[no_unique_address]] RuntimeDiagCounter retransmitPending[Spaces] = {};
  [[no_unique_address]] RuntimeDiagCounter retransmittable[Spaces] = {};
  [[no_unique_address]] RuntimeDiagCounter congestionWindow = 0;
  [[no_unique_address]] RuntimeDiagCounter congestionSSThresh = 0;
  [[no_unique_address]] RuntimeDiagCounter congestionBytesInFlight = 0;
  [[no_unique_address]] RuntimeDiagCounter persistentCongestion = 0;
  [[no_unique_address]] RuntimeDiagCounter ecnValidationFailures = 0;
  [[no_unique_address]] RuntimeDiagCounter unhandledAppEvents = 0;
  [[no_unique_address]] RuntimeDiagCounter failures = 0;
};

struct RuntimeDiag {
  RuntimeDiag() = default;
  RuntimeDiag(const RuntimeRxDiag &rx_, const RuntimeTxDiag &tx_) :
    rx{rx_}, tx{tx_} { }

  uint64_t failures() const { return rx.failures + tx.failures; }
  uint64_t unhandledAppEvents() const {
    return rx.unhandledAppEvents + tx.unhandledAppEvents;
  }

  RuntimeRxDiag	rx;
  RuntimeTxDiag	tx;
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
    PktNumSpace::T space;
    if (!pktNumSpaceFromTLSEpoch(epoch, space)) continue;
    unsigned off = unsigned(offsets[epoch]);
    unsigned remaining = unsigned(offsets[epoch + 1] - offsets[epoch]);
    while (remaining) {
      unsigned chunk = remaining > chunkMax ? chunkMax : remaining;
      using Frame = ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Runtime.Frame">>;
      auto frame = ZtLocalArray(Frame, BufSize);
      uint64_t cryptoOffset = txCrypto[space].txOffset();
      int n = txCrypto[space].writeFramePrefix(frame.data(), BufSize, chunk);
      if (n < 0) {
	++diag.failures;
	return false;
      }
      SentFrameRef ref = SentFrameRef::crypto(cryptoOffset, chunk);
      if (!txCrypto[space].sent(cryptoOffset, byteSpan(data + off, chunk))) {
	++diag.failures;
	return false;
      }
      diag.cryptoBytesTx += chunk;
      if (!send(space, byteSpan(frame.data(), unsigned(n)),
	    byteSpan(data + off, chunk), ref, addr))
	return false;
      if (space == PktNumSpace::Handshake &&
	  !send(space, byteSpan(frame.data(), unsigned(n)),
	    byteSpan(data + off, chunk), ref, addr))
	return false;
      off += chunk;
      remaining -= chunk;
    }
  }
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
  EngineParams &&qlog(bool v) {
    m_qlogParams.enabled(v);
    return ZuMv(*this);
  }
  EngineParams &&qlogPath(ZuCSpan v) {
    m_qlogParams.path(v);
    return ZuMv(*this);
  }
  EngineParams &&qlogThread(ZuCSpan v) {
    m_qlogParams.thread(v);
    return ZuMv(*this);
  }
  EngineParams &&qlogRingSize(unsigned v) {
    m_qlogParams.ringSize(v);
    return ZuMv(*this);
  }
  EngineParams &&qlogAge(unsigned v) {
    m_qlogParams.age(v);
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
  EngineParams &&maxIdleTimeout(uint64_t v) {
    m_maxIdleTimeout = v;
    return ZuMv(*this);
  }
  EngineParams &&maxUDP(unsigned v) { m_maxUDP = v; return ZuMv(*this); }
  EngineParams &&addressValidationSecret(ZuBSpan v) {
    m_tokenSecret.length(0);
    m_tokenSecret.append(v.data(), v.length());
    return ZuMv(*this);
  }
  EngineParams &&retryAddressValidation(bool v) {
    m_retryAddressValidation = v;
    return ZuMv(*this);
  }
  EngineParams &&newTokenAddressValidation(bool v) {
    m_newTokenAddressValidation = v;
    return ZuMv(*this);
  }
  EngineParams &&addressValidationLifetime(uint64_t v) {
    m_tokenLifetime = v;
    return ZuMv(*this);
  }
  EngineParams &&addressValidationBindPort(bool v) {
    m_tokenBindPort = v;
    return ZuMv(*this);
  }
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
  const ZquicLogParams &qlogParams() const { return m_qlogParams; }
  ZuCSpan asyncThread() const { return m_asyncThread; }
  uint64_t maxData() const { return m_maxData; }
  uint64_t maxStreamData() const { return m_maxStreamData; }
  uint64_t maxStreamsBidi() const { return m_maxStreamsBidi; }
  uint64_t maxStreamsUni() const { return m_maxStreamsUni; }
  uint64_t maxIdleTimeout() const { return m_maxIdleTimeout; }
  unsigned maxUDP() const { return m_maxUDP; }
  ZuBSpan addressValidationSecret() const {
    return m_tokenSecret;
  }
  bool retryAddressValidation() const { return m_retryAddressValidation; }
  bool newTokenAddressValidation() const {
    return m_newTokenAddressValidation;
  }
  uint64_t addressValidationLifetime() const { return m_tokenLifetime; }
  bool addressValidationBindPort() const { return m_tokenBindPort; }
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
  ZquicLogParams	m_qlogParams;
  ParamString	m_asyncThread;
  uint64_t	m_maxData = DefaultMaxData;
  uint64_t	m_maxStreamData = DefaultMaxStreamData;
  uint64_t	m_maxStreamsBidi = DefaultMaxStreamsBidi;
  uint64_t	m_maxStreamsUni = DefaultMaxStreamsUni;
  uint64_t	m_maxIdleTimeout = 0;
  unsigned	m_maxUDP = MinUDPPayload;
  TokenSecret	m_tokenSecret;
  uint64_t	m_tokenLifetime = DefaultTokenLifetime;
  bool		m_retryAddressValidation = false;
  bool		m_newTokenAddressValidation = false;
  bool		m_tokenBindPort = false;
  ErrorFn	m_errorFn;
};

using ClientParams = EngineParams;
using ServerParams = EngineParams;

template <typename App_> class Engine :
  public ZmPolymorph,
  public ZmEngine<App_> {
friend ZmEngine<App_>;
template <typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename>
friend class CliLink;
template <typename, typename, typename, typename>
friend class SrvLink;
template <typename>
friend class Zquic_::Endpoint_;

public:
  using App = App_;
  using EngineCtl = ZmEngine<App>;

  using EngineCtl::start;
  using EngineCtl::stop;
  using EngineCtl::state;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  enum { Transport = Zi::Transport::QUIC };

  bool init(EngineParams params) {
    return init_(ZuMv(params), Zquic::Vantage::Unknown,
      [](const EngineParams &) { return true; });
  }

  void final() {
    bool ok = EngineCtl::lock(ZmEngineState::Stopped, [this]() {
      ZquicLogger::final(m_qlogTrace);
      m_mx = nullptr;
      m_rxThread = 0;
      m_txThread = 0;
      m_asyncThread = 0;
      m_errorFn = ErrorFn{};
      m_alpnData.length(0);
      m_alpn.length(0);
      m_caPath = ParamString{};
      m_certPath = ParamString{};
      m_keyPath = ParamString{};
      m_keyLogPath = ParamString{};
      m_qlogParams = {};
      m_maxData = DefaultMaxData;
      m_maxStreamData = DefaultMaxStreamData;
      m_maxStreamsBidi = DefaultMaxStreamsBidi;
      m_maxStreamsUni = DefaultMaxStreamsUni;
      m_maxIdleTimeout = 0;
      m_maxUDP = MinUDPPayload;
      m_tokenSecret.length(0);
      m_tokenLifetime = DefaultTokenLifetime;
      m_retryAddressValidation = false;
      m_newTokenAddressValidation = false;
      m_tokenBindPort = false;
      return true;
    });
    ZiAssert(ok, "Zquic", (),
      "QUIC engine finalization while not stopped", return);
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
  ZquicLogger::Trace &qlogTrace() { return m_qlogTrace; }
  ZquicLogDiag qlogDiag() const { return ZquicLogger::diag(); }
  uint64_t maxData() const { return m_maxData; }
  uint64_t maxStreamData() const { return m_maxStreamData; }
  uint64_t maxStreamsBidi() const { return m_maxStreamsBidi; }
  uint64_t maxStreamsUni() const { return m_maxStreamsUni; }
  uint64_t maxIdleTimeout() const { return m_maxIdleTimeout; }
  unsigned maxUDP() const { return m_maxUDP; }
  ZuBSpan addressValidationSecret() const {
    return m_tokenSecret;
  }
  bool retryAddressValidation() const { return m_retryAddressValidation; }
  bool newTokenAddressValidation() const {
    return m_newTokenAddressValidation;
  }
  uint64_t addressValidationLifetime() const { return m_tokenLifetime; }
  bool addressValidationBindPort() const { return m_tokenBindPort; }

  template <typename ...Args>
  void rxRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args>
  void rxInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_rxThread);
  }
  bool rxInvoked() { return m_mx && m_rxThread && m_mx->invoked(m_rxThread); }
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_txThread);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_txThread);
  }
  bool txInvoked() { return m_mx && m_txThread && m_mx->invoked(m_txThread); }

protected:
  template <typename Params, typename L>
  bool init_(Params params, Zquic::Vantage::T vantage, L l) {
    return EngineCtl::lock(
	ZmEngineState::Stopped,
	[this, params = ZuMv(params), vantage, l = ZuMv(l)]() mutable -> bool {
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
      m_qlogParams = params.qlogParams();
      m_maxData = params.maxData();
      m_maxStreamData = params.maxStreamData();
      m_maxStreamsBidi = params.maxStreamsBidi();
      m_maxStreamsUni = params.maxStreamsUni();
      m_maxIdleTimeout = params.maxIdleTimeout();
      m_maxUDP = params.maxUDP();
      {
	ZuBSpan secret = params.addressValidationSecret();
	m_tokenSecret.length(0);
	m_tokenSecret.append(secret.data(), secret.length());
      }
      m_tokenLifetime = params.addressValidationLifetime();
      m_retryAddressValidation = params.retryAddressValidation();
      m_newTokenAddressValidation = params.newTokenAddressValidation();
      m_tokenBindPort = params.addressValidationBindPort();
      if ((m_retryAddressValidation || m_newTokenAddressValidation) &&
	  !m_tokenSecret &&
	  !AddressToken::generateSecret(m_tokenSecret))
	return false;
      if (!init_alpn_(params.alpn())) return false;
      if (m_qlogParams.enabled() &&
	  !ZquicLogger::init(m_qlogTrace, m_qlogParams, vantage))
	return false;
      return l(params);
    });
  }

  void error_(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }

  void warmup_() {
    if (rxInvoked())
      Zquic::warmup();
    else
      rxInvoke([]() { Zquic::warmup(); });
    if (txInvoked())
      Zquic::warmup();
    else
      txInvoke([]() { Zquic::warmup(); });
  }

  void start_() {
    startQLog_();
    warmup_();
    this->started(true);
  }

  void stop_() {
    rxRun([this]() { stop_1(); });
  }

  void stop_1() {
    txRun([this]() { stop_2(); });
  }

  void stop_2() {
    stopQLog_([this]() { stop_3(true); });
  }

  void stop_3(bool ok) {
    this->stopped(ok);
  }

  template <typename L>
  bool spawn(L l) {
    if (!mx() || !mx()->running()) return false;
    rxRun(ZuMv(l));
    return true;
  }

  void wake() {
    if (!mx() || !mx()->running()) return;
    rxRun([this]() { this->stopped(); });
  }

  void startQLog_() {
    if (!m_qlogParams.enabled()) return;
    ZquicLogger::start();
  }

  template <typename L>
  void stopQLog_(L l) {
    if (!m_qlogParams.enabled()) { l(); return; }
    ZquicLogger::close(m_qlogTrace, [this, l = ZuMv(l)]() mutable {
      rxRun(ZuMv(l));
    });
  }

  template <typename Link>
  void disconnected(Link *, bool) { }
  template <typename Link>
  void retireLinkRoutes_(Link *) { }

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
    if (!alpn) return true;
    unsigned bytes = 0;
    for (auto &s : alpn) bytes += s.length();
    if (bytes && !m_alpnData.ensure(bytes)) return false;
    m_alpn.ensure(alpn.length());
    for (auto &s : alpn) {
      unsigned offset = m_alpnData.length();
      m_alpnData << ZuBSpan{s};
      m_alpn.push(ptls_iovec_t{m_alpnData.data() + offset, s.length()});
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
  ZquicLogParams		m_qlogParams;
  ZquicLogger::Trace	m_qlogTrace;
  uint64_t		m_maxData = DefaultMaxData;
  uint64_t		m_maxStreamData = DefaultMaxStreamData;
  uint64_t		m_maxStreamsBidi = DefaultMaxStreamsBidi;
  uint64_t		m_maxStreamsUni = DefaultMaxStreamsUni;
  uint64_t		m_maxIdleTimeout = 0;
  unsigned		m_maxUDP = MinUDPPayload;
  TokenSecret		m_tokenSecret;
  uint64_t		m_tokenLifetime = DefaultTokenLifetime;
  bool			m_retryAddressValidation = false;
  bool			m_newTokenAddressValidation = false;
  bool			m_tokenBindPort = false;
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
  void disconnected(bool peer); // Zquic Rx thread
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
    return this->init_(ZuMv(params), Zquic::Vantage::Client,
      [](const ClientParams &) { return true; });
  }

  void final() {
    Base::final();
  }

  bool sendPkt(const ZmRef<ZiIOBuf> &) { return true; }
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
  void disconnected(bool peer); // Zquic Rx thread
  void streamed(ZmRef<Stream>); // Zquic Rx thread
};
#endif
template <typename App_, typename Link_> class Server :
  public Engine<App_>,
  public Endpoint_<Server<App_, Link_>> {
public:
  using App = App_;
  using Link = Link_;
  using LinkRef = ZmRef<Link>;
  using LinkTable = ServerLinks_<Link>;
  using Base = Engine<App>;
  using Endpoint = Endpoint_<Server>;
  using Base::app;
  static constexpr bool EndpointRef = false;
  static constexpr unsigned TLSBufSize = Client<App>::TLSBufSize;
  static constexpr unsigned RuntimePNLength = Client<App>::RuntimePNLength;
  static constexpr unsigned RuntimeCryptoChunk = Client<App>::RuntimeCryptoChunk;

template <typename, typename, typename, typename>
friend class SrvLink;
friend ZmEngine<App>;

  bool init(ServerParams params) {
    if (!params.certPath() || !params.keyPath()) {
      auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
      errorFn(ZeEXCEPT(Error, "Zquic",
	"server certPath and keyPath are required"));
      return false;
    }
    return this->init_(ZuMv(params), Zquic::Vantage::Server,
      [](const ServerParams &) { return true; });
  }

  void final() {
    m_links = nullptr;
    m_routes.final();
    Base::final();
  }

  bool listening() const { return Endpoint::listening(); }
  bool connected() const { return Endpoint::connected(); }
  const ZiSockAddr &local() const { return Endpoint::local(); }
  EndpointDiag endpointDiag() const {
    auto mx = this->mx();
    if (!mx) return Endpoint::diag();
    if (mx->invoked(mx->txThread())) return Endpoint::diag();
    EndpointDiag diag;
    ZmSemaphore done;
    auto server = const_cast<Server *>(this);
    mx->txRun([server, &diag, &done]() mutable {
      diag = server->Endpoint::diag();
      done.post();
    });
    done.wait();
    return diag;
  }
  AddressValidationDiag addressValidationDiag() const {
    auto mx = this->mx();
    if (!mx) return {};
    auto server = const_cast<Server *>(this);
    if (server->rxInvoked()) return m_addressValidationDiag;
    AddressValidationDiag diag;
    ZmSemaphore done;
    mx->rxRun([server, &diag, &done]() mutable {
      diag = server->m_addressValidationDiag;
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
    using Snapshot =
      ZtArray<LinkRef, ZtArrayHeapID<"Zquic.Server.LinkSnapshot">>;
    unsigned nLinks = 0;
    ZmSemaphore done;
    this->rxInvoke([this, &nLinks, &done]() {
      if (this->state() == ZmEngineState::Running)
	nLinks = unsigned(m_links->count_());
      done.post();
    });
    done.wait();
    auto snapshot = ZtLocalArray(Snapshot, nLinks);
    this->rxInvoke([this, &snapshot, &done]() {
      if (this->state() == ZmEngineState::Running) {
	auto links = m_links;
	auto i = links->citer();
	while (LinkRef ref = i.val())
	  if (ref) snapshot.push(ZuMv(ref));
      }
      done.post();
    });
    done.wait();
    for (auto &ref : snapshot)
      if (ref) fn(ref);
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
    return Endpoint::allocTxPkt();
  }
  bool sendPkt_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (!this->app()->sendPkt(buf)) return true;
    return Endpoint::send(ZuMv(buf), ZuMv(addr));
  }
  bool sendPktRaw_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    return Endpoint::send(ZuMv(buf), ZuMv(addr));
  }
  void dissociateRoute_(const CxnID &id) {
    m_routes.retire(id);
  }

public:
  void endpointDatagram_(Datagram d) {
    this->rxRun([this, d = ZuMv(d)]() mutable {
      received_(ZuMv(d));
    });
  }
  void endpointReady_(Endpoint *) {
    this->rxRun([this]() {
      switch (this->state()) {
	case ZmEngineState::Starting:
	case ZmEngineState::StopPending:
	  this->started(true);
	  break;
	default:
	  break;
      }
      this->app()->listening();
    });
  }
  void endpointFailed_(bool transient) {
    this->rxRun([this, transient]() {
      failed_0(transient);
    });
  }
  void endpointDown_(Endpoint *) { }
  void endpointTxDrained_() {
    this->rxRun([this]() { txDrained_(); });
  }

private:
  void txDrained_() {
    auto links = m_links;
    auto i = links->citer();
    while (LinkRef ref = i.val())
      if (ref)
	ref->app()->txRun([link = ZuMv(ref)]() mutable {
	  link->txDrained_();
	});
  }

  void failed_0(bool transient) {
    switch (this->state()) {
      case ZmEngineState::Starting:
      case ZmEngineState::StopPending:
	this->started(false);
	return;
      default:
	break;
    }
    this->app()->listenFailed(transient);
  }

  void received_(Datagram d) {
    if (this->state() != ZmEngineState::Running) {
      Endpoint::failure();
      return;
    }
    Link *link = route_(d);
    if (!link) {
      sendStatelessReset_(d);
      Endpoint::failure();
      return;
    }
    if (link->receivedRouted_(ZuMv(d)))
      link->installRoutes_(m_routes);
    else
      link->disconnect();
  }

  Link *route_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return nullptr;
    auto packet = d.buf->cspan();
    if (Pkt::isLong(packet)) return routeLong_(d, packet);
    return routeShort_(d, packet);
  }

  Link *routeLong_(const Datagram &d, ZuBSpan packet) {
    LongHdr h;
    if (Pkt::parseLong(packet, h) < 0) return nullptr;
    if (!VersionNeg::supported(h.version)) {
      sendVersionNegotiation_(h, d.addr);
      return nullptr;
    }
    if (Link *link = m_routes.find(h.dcid)) return link;
    if (h.type != PktType::Initial) return nullptr;
    InitialInfo info{h, d.addr, d.buf->length};
    if (!validateInitial_(info, packet)) {
      if (app()->retryAddressValidation())
	sendRetry_(h, d.addr);
      return nullptr;
    }
    return accept_(info);
  }

  Link *routeShort_(const Datagram &, ZuBSpan packet) {
    return m_routes.matchShort(packet);
  }

  bool sendStatelessReset_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return false;
    auto packet = d.buf->cspan();
    if (Pkt::isLong(packet)) return false;
    ResetToken token;
    if (!m_routes.resetTokenForShort(packet, token)) return false;
    ZmRef<ZiIOBuf> buf = allocTxPkt_();
    int n = StatelessRst::writeForUnknownCID(
      buf->data_(), buf->size, packet, token);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    bool sent = sendPkt_(ZuMv(buf), d.addr);
    if (sent) {
      ZquicLOG(app()->qlogTrace(), ([
	resetBytes = unsigned(n)
      ](auto &o, ZuTime time) {
	SecEvent event{
	  .kind = SecKind::StatelessRst,
	  .value = resetBytes,
	  .success = true
	};
	event.trigger = SecTrigger::Sent;
	event.reason = SecReason::UnknownCID;
	o.logSecEvent(EventName::StatelessRst, event, time);
      }));
    }
    return sent;
  }

  Link *accept_(const InitialInfo &info) {
    LinkRef link;
    link = this->app()->accepted(info);
    if (!link) return nullptr;
    ZquicLOG(app()->qlogTrace(), ([
      local = local(),
      remote = info.peer,
      origDCID = info.origDCID.length() ? info.origDCID : info.header.dcid,
      dcid = info.header.dcid,
      scid = info.header.scid
    ](auto &o, ZuTime time) {
      Zquic::LinkInfo linkInfo{
	.vantage = Zquic::Vantage::Server,
	.origDCID = origDCID,
	.groupID = origDCID,
	.dcid = dcid,
	.scid = scid
      };
      o.logCxnStarted(
	CxnStartedEvent{.local = local, .remote = remote, .linkInfo = linkInfo},
	time);
    }));
    Link *ptr = link.ptr();
    ptr->acceptInitialInfo_(info);
    if (!addLink_(ZuMv(link))) return nullptr;
    return ptr;
  }

  bool addLink_(LinkRef link) {
    if (!link) return false;
    if (this->state() != ZmEngineState::Running) return false;
    Link *ptr = link.ptr();
    if (m_links->findVal(ptr)) return true;
    m_links->add(ZuMv(link));
    return true;
  }

public:
  void disconnected(Link *link, bool) {
    if (Base::stopping()) {
      if (m_stopCount && !--m_stopCount)
	stop_1();
      return;
    }
    m_links->del(link);
  }

  void retireLinkRoutes_(Link *link) {
    if (this->state() == ZmEngineState::Running && link)
      link->retireRoutes_(m_routes);
  }

private:
  void start_() {
    if (!m_links)
      m_links = new LinkTable{
	ZmHashParams().bits(5).loadFactor(1).cBits(3)};
    m_routes.init();
    m_stopCount = 0;
    Base::startQLog_();
    Base::warmup_();
    ZiIP localIP = this->app()->localIP();
    uint16_t localPort = this->app()->localPort();
    if (!Endpoint::init(this->mx()) ||
	!Endpoint::openUDP(PathMode::ServerUnconnected, localIP, localPort)) {
      Base::stopQLog_([this]() { this->started(false); });
    }
  }

  void stop_() {
    Endpoint::disconnect();
    auto links = m_links;
    m_stopCount = links->count_();
    auto i = links->citer();
    while (LinkRef ref = i.val())
      if (ref) ref->disconnect();
    m_routes.clear();
    if (!m_stopCount) stop_1();
  }

  void stop_1() {
    m_links->clean();
    Base::stopQLog_([this]() { stop_2(); });
  }

  void stop_2() {
    this->stopped(true);
  }

  bool sendVersionNegotiation_(const LongHdr &h, ZiSockAddr addr) {
    ZmRef<ZiIOBuf> buf = allocTxPkt_();
    int n = VersionNeg::write(
      buf->data_(), buf->size, h.scid, h.dcid);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    bool sent = sendPkt_(ZuMv(buf), ZuMv(addr));
    if (sent) {
      ZquicLOG(app()->qlogTrace(), ([
	version = h.version
      ](auto &o, ZuTime time) {
	VersionEvent event;
	new (event.serverVersions.push()) uint32_t(Version1);
	new (event.clientVersions.push()) uint32_t(version);
	o.logVersionInfo(event, time);
      }));
    }
    return sent;
  }

  ZuBSpan initialToken_(ZuBSpan packet, const LongHdr &h) const {
    if (!h.tokenLength) return {};
    return ZuBSpan{packet.data() + h.tokenOffset, unsigned(h.tokenLength)};
  }

  bool validateInitial_(InitialInfo &info, ZuBSpan packet) {
    if (!app()->retryAddressValidation()) return true;
    ZuBSpan token = initialToken_(packet, info.header);
    if (!token) {
      ++m_addressValidationDiag.retryRejected;
      ZquicLOG(app()->qlogTrace(), ([
	tokenLength = 0U
      ](auto &o, ZuTime time) {
	SecEvent event{
	  .kind = SecKind::Retry,
	  .reason = SecReason::MissingToken,
	  .value = tokenLength,
	  .success = false
	};
	event.trigger = SecTrigger::Validated;
	o.logSecEvent(EventName::RetryValid, event, time);
	event.kind = SecKind::Token;
	o.logSecEvent(EventName::TokenReject, event, time);
      }));
      return false;
    }
    TokenInfo tokenInfo;
    TokenStatus::T status = AddressToken::validate(
      tokenInfo, token, app()->addressValidationSecret(), info.peer,
      uint64_t(Zm::now().sec()), app()->addressValidationLifetime(),
      app()->addressValidationBindPort());
    if (status != TokenStatus::OK) {
      ++m_addressValidationDiag.retryRejected;
      switch (status) {
	case TokenStatus::Expired: ++m_addressValidationDiag.tokenExpired; break;
	case TokenStatus::Auth: ++m_addressValidationDiag.tokenAuthFailure; break;
	case TokenStatus::Address:
	  ++m_addressValidationDiag.tokenAddressMismatch;
	  break;
	case TokenStatus::Malformed:
	  ++m_addressValidationDiag.tokenMalformed;
	  break;
	case TokenStatus::Kind: ++m_addressValidationDiag.tokenKindMismatch; break;
	case TokenStatus::ODCID: ++m_addressValidationDiag.tokenODCIDMismatch; break;
	default: break;
      }
      ZquicLOG(app()->qlogTrace(), ([
	status,
	tokenLength = token.length()
      ](auto &o, ZuTime time) {
	SecReason::T reason = SecReason::Unknown;
	switch (status) {
	  case TokenStatus::Expired: reason = SecReason::Expired; break;
	  case TokenStatus::Auth: reason = SecReason::Auth; break;
	  case TokenStatus::Address: reason = SecReason::Address; break;
	  case TokenStatus::Malformed:
	    reason = SecReason::Malformed;
	    break;
	  case TokenStatus::Kind: reason = SecReason::Kind; break;
	  case TokenStatus::ODCID: reason = SecReason::ODCID; break;
	  default: break;
	}
	SecEvent event{
	  .kind = SecKind::Token,
	  .reason = reason,
	  .value = tokenLength,
	  .success = false
	};
	event.trigger = SecTrigger::Validated;
	o.logSecEvent(EventName::TokenReject, event, time);
      }));
      return false;
    }
    switch (tokenInfo.kind) {
      case TokenKind::Retry:
	if (!(tokenInfo.serverCID == info.header.dcid)) {
	  ++m_addressValidationDiag.retryRejected;
	  ++m_addressValidationDiag.tokenODCIDMismatch;
	  ZquicLOG(app()->qlogTrace(), ([
	    tokenLength = token.length()
	  ](auto &o, ZuTime time) {
	    SecEvent event{
	      .kind = SecKind::Retry,
	      .reason = SecReason::RetrySCID,
	      .value = tokenLength,
	      .success = false
	    };
	    event.trigger = SecTrigger::Validated;
	    o.logSecEvent(EventName::RetryValid, event, time);
	    event.kind = SecKind::Token;
	    o.logSecEvent(EventName::TokenReject, event, time);
	  }));
	  return false;
	}
	info.retrySCID = info.header.dcid;
	info.origDCID = tokenInfo.origDCID.length() ?
	  tokenInfo.origDCID : info.header.dcid;
	++m_addressValidationDiag.retryAccepted;
	ZquicLOG(app()->qlogTrace(), ([
	  tokenLength = token.length()
	](auto &o, ZuTime time) {
	  SecEvent event{
	    .kind = SecKind::Retry,
	    .reason = SecReason::OK,
	    .value = tokenLength,
	    .success = true
	  };
	  event.trigger = SecTrigger::Validated;
	  o.logSecEvent(EventName::RetryValid, event, time);
	  event.kind = SecKind::Token;
	  o.logSecEvent(EventName::TokenValid, event, time);
	}));
	break;
      case TokenKind::NewToken:
	if (!app()->newTokenAddressValidation() || tokenInfo.serverCID.length()) {
	  ++m_addressValidationDiag.retryRejected;
	  ++m_addressValidationDiag.tokenKindMismatch;
	  ZquicLOG(app()->qlogTrace(), ([
	    tokenLength = token.length()
	  ](auto &o, ZuTime time) {
	    SecEvent event{
	      .kind = SecKind::Token,
	      .reason = SecReason::NewTokenPolicy,
	      .value = tokenLength,
	      .success = false
	    };
	    event.trigger = SecTrigger::Validated;
	    o.logSecEvent(EventName::TokenReject, event, time);
	  }));
	  return false;
	}
	info.origDCID = info.header.dcid;
	++m_addressValidationDiag.newTokenAccepted;
	ZquicLOG(app()->qlogTrace(), ([
	  tokenLength = token.length()
	](auto &o, ZuTime time) {
	  SecEvent event{
	    .kind = SecKind::Token,
	    .reason = SecReason::OK,
	    .value = tokenLength,
	    .success = true
	  };
	  event.trigger = SecTrigger::Validated;
	  o.logSecEvent(EventName::TokenValid, event, time);
	}));
	break;
      default:
	++m_addressValidationDiag.retryRejected;
	++m_addressValidationDiag.tokenKindMismatch;
	ZquicLOG(app()->qlogTrace(), ([
	  tokenLength = token.length()
	](auto &o, ZuTime time) {
	  SecEvent event{
	    .kind = SecKind::Token,
	    .reason = SecReason::Kind,
	    .value = tokenLength,
	    .success = false
	  };
	  event.trigger = SecTrigger::Validated;
	  o.logSecEvent(EventName::TokenReject, event, time);
	}));
	return false;
    }
    info.addressValidated = true;
    info.tokenKind = int8_t(tokenInfo.kind);
    return true;
  }

  bool sendRetry_(const LongHdr &h, ZiSockAddr addr) {
    CxnID retrySCID;
    TokenBytes token;
    if (!CxnIDGen::random(retrySCID) ||
	!AddressToken::encode(
	  token, TokenKind::Retry, app()->addressValidationSecret(), addr,
	  h.dcid, retrySCID, uint64_t(Zm::now().sec()),
	  app()->addressValidationBindPort()))
      return false;
    ZmRef<ZiIOBuf> buf = allocTxPkt_();
    int n = Pkt::writeRetryAuthenticated(
      buf->data_(), buf->size, h.scid, retrySCID,
      token, h.dcid);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    bool sent = sendPkt_(ZuMv(buf), ZuMv(addr));
    if (sent) {
      ++m_addressValidationDiag.retrySent;
      ZquicLOG(app()->qlogTrace(), ([
	tokenLength = token.length(),
	packetBytes = unsigned(n)
      ](auto &o, ZuTime time) {
	SecEvent event{
	  .kind = SecKind::Retry,
	  .value = tokenLength,
	  .success = true
	};
	event.trigger = SecTrigger::Sent;
	event.reason = SecReason::AddrValid;
	o.logSecEvent(EventName::RetrySent, event, time);
	event.kind = SecKind::Token;
	event.value = packetBytes;
	o.logSecEvent(EventName::TokenIssued, event, time);
      }));
    }
    return sent;
  }

  // Rx thread exclusive
  ZmRef<LinkTable>	m_links = new LinkTable{
    ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  CxnRouter<Link>	m_routes;
  AddressValidationDiag	m_addressValidationDiag;
  unsigned		m_stopCount = 0;
};


} // namespace Zquic

#ifndef Zquic_Stream_HH
#include <zlib/Zquic_Stream.hh>
#endif

#ifndef Zquic_Link_HH
#include <zlib/Zquic_Link.hh>
#endif

#ifndef Zquic_CliLink_HH
#include <zlib/Zquic_CliLink.hh>
#endif

#ifndef Zquic_SrvLink_HH
#include <zlib/Zquic_SrvLink.hh>
#endif

#endif /* Zquic_HH */
