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

#include <zlib/ZuElem.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuUnroll.hh>

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

#include <zlib/ZtArray.hh>
#include <zlib/ZtLocalArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZtcHub.hh>

#include <zlib/ZtlsBackend.hh>

#if defined(ZDEBUG) && !defined(Zquic_DEBUG)
#define Zquic_DEBUG	// enable testing / debugging
#endif

namespace Zquic {
  inline constexpr uint64_t U64Null = ZuCmp<uint64_t>::null();
}
namespace Zquic_ { using namespace Zquic; }

#include <zlib/ZquicTypes.hh>
#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicPacket.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicPQueue.hh>
#include <zlib/ZquicStreamUtil.hh>
#include <zlib/ZquicSched.hh>
#include <zlib/ZquicTransport.hh>
#include <zlib/ZquicLog.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZquicRecovery.hh>
#include <zlib/ZquicPath.hh>
#include <zlib/ZquicSock.hh>
#include <zlib/ZquicDiag.hh>

namespace Zquic {

// Public connection metadata and token/address validation
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

using ErrorFn = ZmFn<void(ZeException), ZmFnHeapID<"Zquic.ErrorFn">>;

inline constexpr uint64_t DefaultMaxData = (16<<20); // 16M
inline constexpr uint64_t DefaultMaxStreamData = (1<<20); // 1M
inline constexpr uint64_t DefaultMaxStreamsDuplex = 128;
inline constexpr uint64_t DefaultMaxStreamsSimplex = 16;
inline constexpr uint64_t MaxStreamCount = uint64_t(INT64_MAX) >> 2;
inline constexpr uint64_t DefaultTokenLifetime = 600;
inline constexpr unsigned LocalCIDLimit = 8;
inline constexpr unsigned DefaultMigCIDRes = 1;

ZtEnumStruct(TokenKind, int8_t, Retry = 1, NewToken = 2);

ZtEnumStruct(TokenStatus, int8_t,
  OK, Malformed, Expired, Kind, Address, ODCID, Auth);

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

inline bool sentFrameRef(
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
  case FrameType::NewCxnID:
    ref = SentFrameRef::newCxnID(
      frame.value, frame.offset, CxnID{frame.payload}, frame.resetToken);
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
    return Pkt::writeVerNeg(out, len, dcid, scid, versions, 1);
  }

  static int parse(
    ZuBSpan in, uint32_t *versions, unsigned capacity, unsigned &nVersions) {
    return Pkt::parseVerNeg(in, versions, capacity, nVersions);
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

struct AddrValidationDiag {
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

struct StatelessReset {
  static constexpr unsigned TokenLength = ResetToken::Length;
  static constexpr unsigned MinLength = 21;

  static int decode(ResetToken &, ZuBSpan datagram);
  static bool verify(ZuBSpan datagram, const ResetToken &);
  static int writeForUnknownCID(
    uint8_t *, unsigned, ZuBSpan receivedPkt, const ResetToken &);
};

} // namespace Zquic

#include <zlib/Zquic_.hh>

namespace Zquic {

using Zquic_::Cxn;
using Zquic_::CxnRouter;
using Zquic_::Endpoint_;
using Zquic_::ServerLinks_;

// Connection bootstrap
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
  uint64_t retryTokenLength() const { return m_retryToken.length(); }
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
    m_retryToken = {};
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
    m_retryToken = retry.token;
    m_retried = true;
    return true;
  }

  bool validateServerParams(
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

inline bool validCryptoOffsets(
  const CryptoOffsets &offsets, unsigned len)
{
  for (unsigned epoch = 0; epoch < TLSEpochCount; ++epoch)
    if (offsets[epoch + 1] < offsets[epoch] ||
	offsets[epoch + 1] > len)
      return false;
  return true;
}

template <typename CryptoStreams, typename Send>
inline bool sendCryptoFlights_(
  CryptoStreams &txCrypto, LinkTxDiag &diag,
  const uint8_t *data, unsigned len, const CryptoOffsets &offsets,
  unsigned chunkMax, ZiSockAddr addr, Send send)
{
  if (!chunkMax) return false;
  for (unsigned epoch = 0; epoch < TLSEpochCount; ++epoch) {
    if (offsets[epoch + 1] <= offsets[epoch]) continue;
    PktNumSpace::T space;
    if (!spaceFromTLSEpoch(epoch, space)) continue;
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

// Raw array-reference ingress preserves the public picotls-style callback
// contract; the internal helper also accepts the owning ZuArray directly.
template <typename Send>
inline bool sendCryptoFlights(
  CryptoStream (&txCrypto)[PktNumSpace::N], LinkTxDiag &diag,
  const uint8_t *data, unsigned len, const size_t offsets[5],
  unsigned chunkMax, ZiSockAddr addr, Send send)
{
  if (!offsets) {
    ++diag.failures;
    return false;
  }
  CryptoOffsets offsets_(TLSEpochCount + 1, false);
  for (unsigned i = 0; i <= TLSEpochCount; ++i)
    offsets_[i] = offsets[i];
  if (!validCryptoOffsets(offsets_, len)) {
    ++diag.failures;
    return false;
  }
  return sendCryptoFlights_(
    txCrypto, diag, data, len, offsets_, chunkMax, ZuMv(addr), ZuMv(send));
}

// Hub configuration
class HubParams {
public:
  HubParams(
    ZiMultiplex *mx = nullptr,
    ZuCSpan rxThread = {},
    ZuCSpan txThread = {})
  :
    m_mx{mx}, m_rxThread{rxThread}, m_txThread{txThread},
    m_errorFn{defaultErrorFn()}
  {
  }

  HubParams &&caPath(ZuCSpan v) { m_caPath = v; return ZuMv(*this); }
  HubParams &&certPath(ZuCSpan v) { m_certPath = v; return ZuMv(*this); }
  HubParams &&keyPath(ZuCSpan v) { m_keyPath = v; return ZuMv(*this); }
  HubParams &&keyLogPath(ZuCSpan v) {
    m_keyLogPath = v;
    return ZuMv(*this);
  }
  HubParams &&qlog(bool v) {
    m_qlogParams.enabled(v);
    return ZuMv(*this);
  }
  HubParams &&qlogPath(ZuCSpan v) {
    m_qlogParams.path(v);
    return ZuMv(*this);
  }
  HubParams &&qlogRingSize(unsigned v) {
    m_qlogParams.ringSize(v);
    return ZuMv(*this);
  }
  HubParams &&qlogAge(unsigned v) {
    m_qlogParams.age(v);
    return ZuMv(*this);
  }
  HubParams &&asyncThread(ZuCSpan v) {
    m_asyncThread = v;
    return ZuMv(*this);
  }
  HubParams &&maxData(uint64_t v) { m_maxData = v; return ZuMv(*this); }
  HubParams &&maxStreamData(uint64_t v) {
    m_maxStreamData = v;
    return ZuMv(*this);
  }
  HubParams &&maxStreamsDuplex(uint64_t v) {
    m_maxStreamsDuplex = v;
    return ZuMv(*this);
  }
  HubParams &&maxStreamsSimplex(uint64_t v) {
    m_maxStreamsSimplex = v;
    return ZuMv(*this);
  }
  HubParams &&maxIdleTimeout(uint64_t v) {
    m_maxIdleTimeout = v;
    return ZuMv(*this);
  }
  HubParams &&heartBeat(ZuTime v) {
    m_heartBeat = normalizeHeartBeat_(v);
    return ZuMv(*this);
  }
  HubParams &&maxUDP(unsigned v) { m_maxUDP = v; return ZuMv(*this); }
  HubParams &&ecn(bool v) { m_ecn = v; return ZuMv(*this); }
  HubParams &&migrationMode(MigrationMode::T v) {
    m_migrationMode = v;
    return ZuMv(*this);
  }
  HubParams &&activeMigration(bool v) {
    m_migrationMode = v ? MigrationMode::Active : MigrationMode::Passive;
    return ZuMv(*this);
  }
  HubParams &&migCIDRes(unsigned v) {
    m_migCIDRes = clampMigCIDRes_(v);
    return ZuMv(*this);
  }
  HubParams &&migCloseOnFail(bool v) {
    m_migCloseOnFail = v;
    return ZuMv(*this);
  }
  HubParams &&addrValidationSecret(ZuBSpan v) {
    m_tokenSecret = v;
    return ZuMv(*this);
  }
  HubParams &&retryAddrValidate(bool v) {
    m_retryAddrValidate = v;
    return ZuMv(*this);
  }
  HubParams &&newTokenAddrValidate(bool v) {
    m_newTokenAddrValidate = v;
    return ZuMv(*this);
  }
  HubParams &&addrValidationLifetime(uint64_t v) {
    m_addrValidationLifetime = v;
    return ZuMv(*this);
  }
  HubParams &&addrValidatePeerPort(bool v) {
    m_addrValidatePeerPort = v;
    return ZuMv(*this);
  }
  HubParams &&alpn(ZuSpan<ZuCSpan> v) {
    m_alpn = {};
    m_alpn.ensure(v.length());
    for (auto &s : v) m_alpn.push(ParamString{s});
    return ZuMv(*this);
  }
  HubParams &&alpn(ZuSpan<const ptls_iovec_t> v) {
    m_alpn = {};
    m_alpn.ensure(v.length());
    for (auto &p : v)
      m_alpn.push(ParamString{ZuCSpan{p.base, unsigned(p.len)}});
    return ZuMv(*this);
  }
  HubParams &&errorFn(ErrorFn v) { m_errorFn = ZuMv(v); return ZuMv(*this); }

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
  uint64_t maxStreamsDuplex() const { return m_maxStreamsDuplex; }
  uint64_t maxStreamsSimplex() const { return m_maxStreamsSimplex; }
  uint64_t maxIdleTimeout() const { return m_maxIdleTimeout; }
  ZuTime heartBeat() const { return m_heartBeat; }
  unsigned maxUDP() const { return m_maxUDP; }
  bool ecn() const { return m_ecn; }
  MigrationMode::T migrationMode() const { return m_migrationMode; }
  bool activeMigration() const {
    return m_migrationMode == MigrationMode::Active;
  }
  unsigned migCIDRes() const { return m_migCIDRes; }
  bool migCloseOnFail() const { return m_migCloseOnFail; }
  ZuBSpan addrValidationSecret() const {
    return m_tokenSecret;
  }
  bool retryAddrValidate() const { return m_retryAddrValidate; }
  bool newTokenAddrValidate() const {
    return m_newTokenAddrValidate;
  }
  uint64_t addrValidationLifetime() const { return m_addrValidationLifetime; }
  bool addrValidatePeerPort() const { return m_addrValidatePeerPort; }
  const ErrorFn &errorFn() const { return m_errorFn; }
  ErrorFn &errorFn() { return m_errorFn; }

private:
  static unsigned clampMigCIDRes_(unsigned v) {
    enum { Max = LocalCIDLimit - 1 };
    return v > Max ? Max : v;
  }
  static ZuTime normalizeHeartBeat_(ZuTime v) {
    if (!*v || v == ZuTime{0}) return {};
    if (v < ZuTime{0} || v.nsec() < 0 || v.nsec() >= 1000000000)
      throw ZeEXCEPT(Error, "Zquic",
	"invalid QUIC heartbeat interval");
    return v;
  }

  ZiMultiplex		*m_mx = nullptr;
  ParamString		m_rxThread;
  ParamString		m_txThread;
  ParamStrings		m_alpn;
  ParamString		m_caPath;
  ParamString		m_certPath;
  ParamString		m_keyPath;
  ParamString		m_keyLogPath;
  ZquicLogParams	m_qlogParams;
  ParamString		m_asyncThread;
  uint64_t		m_maxData = DefaultMaxData;
  uint64_t		m_maxStreamData = DefaultMaxStreamData;
  uint64_t		m_maxStreamsDuplex = DefaultMaxStreamsDuplex;
  uint64_t		m_maxStreamsSimplex = DefaultMaxStreamsSimplex;
  uint64_t		m_maxIdleTimeout = 0;
  ZuTime		m_heartBeat;
  unsigned		m_maxUDP = MinUDPPayload;
  bool			m_ecn = false;
  MigrationMode::T	m_migrationMode = MigrationMode::Passive;
  unsigned		m_migCIDRes = DefaultMigCIDRes;
  bool			m_migCloseOnFail = false;
  TokenSecret		m_tokenSecret;
  uint64_t		m_addrValidationLifetime = DefaultTokenLifetime;
  bool			m_retryAddrValidate = false;
  bool			m_newTokenAddrValidate = false;
  bool			m_addrValidatePeerPort = false;
  ErrorFn		m_errorFn;
};

using ClientParams = HubParams;
using ServerParams = HubParams;

template <typename App_> class Hub :
  public ZmPolymorph,
  public ZmEngine<App_>,
  public Ztc::Hub {
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
  using HubCtl = ZmEngine<App>;

  using HubCtl::start;
  using HubCtl::stop;
  using HubCtl::state;
  using HubCtl::stopping;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  bool init(HubParams params) {
    return init_(ZuMv(params), Zquic::Vantage::Unknown,
      [](const HubParams &) { return true; });
  }
  bool start() override { return HubCtl::start(); }
  bool stop() override { return HubCtl::stop(); }

  ZuID telID() const {
    return ZuID{} << "quic:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>();
  }
  ZuTuple<Ztc::LinkType::T, ZuID> telKey() const override {
    return {Ztc::LinkType::QUIC, telID()};
  }
  void telemetry(Ztc::HubTelemetry &data) const override {
    data.id = telID();
    if (m_mx) data.mxID = m_mx->id();
    data.down = 0;
    data.disabled = 0;
    data.transient = 0;
    data.up = 0;
    data.reconn = 0;
    data.failed = 0;
    data.nLinks = 0;
    data.rxThread = m_rxThread;
    data.txThread = m_txThread;
    data.linkType = Ztc::LinkType::QUIC;
    data.state = state();
    const_cast<Hub *>(this)->allLinks({
      &data, [](Ztc::HubTelemetry *data, Ztc::Link *link) {
	Ztc::LinkTelemetry linkData;
	link->telemetry(linkData);
	++data->nLinks;
	switch (linkData.state) {
	  case Ztc::LinkState::Down:		  ++data->down; break;
	  case Ztc::LinkState::Disabled:	  ++data->disabled; break;
	  case Ztc::LinkState::ReconnectPending:
	  case Ztc::LinkState::Reconnecting:	  ++data->reconn; break;
	  case Ztc::LinkState::Up:		  ++data->up; break;
	  case Ztc::LinkState::Failed:		  ++data->failed; break;
	  default:				  ++data->transient; break;
	}
      }});
  }
  void allLinks(Ztc::Hub::AllLinksFn fn) override {
    app()->allLinks_(ZuMv(fn));
  }
  void allPools(Ztc::Hub::AllPoolsFn fn) override {
    app()->allPools_(ZuMv(fn));
  }

  void final() {
    bool ok = HubCtl::lock(ZmEngineState::Stopped, [this]() {
      Ztc::HubMgr::del(this);
      ZquicLogger::final(m_qlogTrace);
      m_mx = nullptr;
      m_rxThread = 0;
      m_txThread = 0;
      m_asyncThread = 0;
      m_errorFn = ErrorFn{};
      m_alpnData = {};
      m_alpn = {};
      m_caPath = ParamString{};
      m_certPath = ParamString{};
      m_keyPath = ParamString{};
      m_keyLogPath = ParamString{};
      m_qlogParams = {};
      m_maxData = DefaultMaxData;
      m_maxStreamData = DefaultMaxStreamData;
      m_maxStreamsDuplex = DefaultMaxStreamsDuplex;
      m_maxStreamsSimplex = DefaultMaxStreamsSimplex;
      m_maxIdleTimeout = 0;
      m_heartBeat = {};
      m_maxUDP = MinUDPPayload;
      m_ecn = false;
      m_migrationMode = MigrationMode::Passive;
      m_migCIDRes = DefaultMigCIDRes;
      m_migCloseOnFail = false;
      m_tokenSecret = {};
      m_addrValidationLifetime = DefaultTokenLifetime;
      m_retryAddrValidate = false;
      m_newTokenAddrValidate = false;
      m_addrValidatePeerPort = false;
      return true;
    });
    ZiAssert(ok, "Zquic", (),
      "QUIC hub finalization while not stopped", return);
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
  uint64_t maxStreamsDuplex() const { return m_maxStreamsDuplex; }
  uint64_t maxStreamsSimplex() const { return m_maxStreamsSimplex; }
  uint64_t maxIdleTimeout() const { return m_maxIdleTimeout; }
  ZuTime heartBeat() const { return m_heartBeat; }
  unsigned maxUDP() const { return m_maxUDP; }
  bool ecn() const { return m_ecn; }
  MigrationMode::T migrationMode() const { return m_migrationMode; }
  bool activeMigration() const {
    return m_migrationMode == MigrationMode::Active;
  }
  unsigned migCIDRes() const { return m_migCIDRes; }
  bool migCloseOnFail() const { return m_migCloseOnFail; }
  ZuBSpan addrValidationSecret() const {
    return m_tokenSecret;
  }
  bool retryAddrValidate() const { return m_retryAddrValidate; }
  bool newTokenAddrValidate() const {
    return m_newTokenAddrValidate;
  }
  uint64_t addrValidationLifetime() const { return m_addrValidationLifetime; }
  bool addrValidatePeerPort() const { return m_addrValidatePeerPort; }

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
  bool init_(Params params, Zquic::Vantage::T vantage, L &&l) {
    bool ok = HubCtl::lock(
	ZmEngineState::Stopped,
	[this, params = ZuMv(params), vantage, l = ZuFwd<L>(l)]() mutable -> bool {
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
      m_maxStreamsDuplex = params.maxStreamsDuplex();
      m_maxStreamsSimplex = params.maxStreamsSimplex();
      m_maxIdleTimeout = params.maxIdleTimeout();
      m_heartBeat = params.heartBeat();
      m_maxUDP = params.maxUDP();
      m_ecn = params.ecn();
      m_migrationMode = params.migrationMode();
      m_migCIDRes = params.migCIDRes();
      m_migCloseOnFail = params.migCloseOnFail();
      {
	ZuBSpan secret = params.addrValidationSecret();
	m_tokenSecret = secret;
      }
      m_addrValidationLifetime = params.addrValidationLifetime();
      m_retryAddrValidate = params.retryAddrValidate();
      m_newTokenAddrValidate = params.newTokenAddrValidate();
      m_addrValidatePeerPort = params.addrValidatePeerPort();
      if ((m_retryAddrValidate || m_newTokenAddrValidate) &&
	  !m_tokenSecret &&
	  !AddressToken::generateSecret(m_tokenSecret))
	return false;
      if (!init_alpn_(params.alpn())) return false;
      if (m_qlogParams.enabled() &&
	  !ZquicLogger::init(m_qlogTrace, m_qlogParams, vantage))
	return false;
      return l(params);
    });
    if (ok) Ztc::HubMgr::add(this);
    return ok;
  }

  void allLinks_(Ztc::Hub::AllLinksFn) { }
  void allPools_(Ztc::Hub::AllPoolsFn) { }

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

  void stop_() {		// hub callback - enter Rx thread
    rxRun([this]() { stop_0(); });
  }

  void stop_0() {		// Rx thread - enter Tx thread
    txRun([this]() { stop_1(); });
  }

  void stop_1() {		// Tx thread - stop qlog
    stopQLog_([this]() { stop_2(true); });
  }

  void stop_2(bool ok) {	// qlog callback - complete stop
    this->stopped(ok);
  }

  template <typename L>
  bool spawn(L &&l) {
    if (!mx() || !mx()->running()) return false;
    rxRun(ZuFwd<L>(l));
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
  void stopQLog_(L &&l) {
    if (!m_qlogParams.enabled()) { l(); return; }
    ZquicLogger::close(m_qlogTrace, [this, l = ZuFwd<L>(l)]() mutable {
      rxRun([l = ZuMv(l)]() mutable {
	ZquicLogger::stopIdle();
	l();
      });
    });
  }

  template <typename Link>
  void disconnected(Link *, bool) { }
  template <typename Link>
  void retireLinkRoutes_(Link *) { }
  template <typename Link>
  bool earlyData(
    Link *, ZuBSpan &, const TransportParams *&, ZuBSpan &) {
    return false;
  }
  template <typename Link>
  bool validateEarlyParams(Link *, ZuBSpan params) {
    return !params;
  }
  template <typename Link>
  void saveEarlyData(Link *, ZuBSpan, const TransportParams &) { }
  template <typename Link>
  uint32_t maxEarlyData(Link *) { return 0; }
  template <typename Link>
  bool allowEarlyStream(Link *, uint64_t, bool) { return false; }
  template <typename Link>
  void earlyDataAccepted(Link *) { }
  template <typename Link>
  void earlyDataRejected(Link *, ZeroRTTReason::T) { }

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
    m_alpn = {};
    m_alpnData = {};
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
  ZquicLogParams	m_qlogParams;
  ZquicLogger::Trace	m_qlogTrace;
  uint64_t		m_maxData = DefaultMaxData;
  uint64_t		m_maxStreamData = DefaultMaxStreamData;
  uint64_t		m_maxStreamsDuplex = DefaultMaxStreamsDuplex;
  uint64_t		m_maxStreamsSimplex = DefaultMaxStreamsSimplex;
  uint64_t		m_maxIdleTimeout = 0;
  ZuTime		m_heartBeat;
  unsigned		m_maxUDP = MinUDPPayload;
  bool			m_ecn = false;
  MigrationMode::T	m_migrationMode = MigrationMode::Passive;
  unsigned		m_migCIDRes = DefaultMigCIDRes;
  bool			m_migCloseOnFail = false;
  TokenSecret		m_tokenSecret;
  uint64_t		m_addrValidationLifetime = DefaultTokenLifetime;
  bool			m_retryAddrValidate = false;
  bool			m_newTokenAddrValidate = false;
  bool			m_addrValidatePeerPort = false;
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

  void connected(Zquic::Connected); // Zquic Rx thread
  void disconnected(bool peer); // Zquic Rx thread
  void connectFailed(bool transient); // Zquic Rx thread
  void streamed(ZmRef<Stream>); // Zquic Rx thread

  unsigned reconnFreq() const; // optional
};
#endif
template <typename App_> class Client : public Hub<App_> {
public:
  using App = App_;
  using Base = Hub<App>;
  static constexpr unsigned TLSBufSize = (64<<10); // 64K
  static constexpr unsigned PNLength = 2;
  static constexpr unsigned CryptoChunk = 900;

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

  void connected(Zquic::Connected); // Zquic Rx thread
  void disconnected(bool peer); // Zquic Rx thread
  void streamed(ZmRef<Stream>); // Zquic Rx thread
};
#endif
template <typename App_, typename Link_> class Server :
  public Hub<App_>,
  public Endpoint_<Server<App_, Link_>> {
public:
  using App = App_;
  using Link = Link_;
  using LinkRef = ZmRef<Link>;
  using LinkTable = ServerLinks_<Link>;
  using LinkRefs =
    ZtArray<LinkRef, ZtArrayHeapID<"Zquic.Server.LinkRefs">>;
  using Base = Hub<App>;
  using Endpoint = Endpoint_<Server>;
  using Base::app;
  using Base::allLinks;
  static constexpr bool EndpointRef = false;
  static constexpr unsigned TLSBufSize = Client<App>::TLSBufSize;
  static constexpr unsigned PNLength = Client<App>::PNLength;
  static constexpr unsigned CryptoChunk = Client<App>::CryptoChunk;

template <typename, typename, typename, typename>
friend class SrvLink;
friend ZmEngine<App>;

  ~Server() { finalTicketKey_(); }

  bool init(ServerParams params) {
    if (!params.certPath() || !params.keyPath()) {
      auto errorFn = params.errorFn() ? params.errorFn() : defaultErrorFn();
      errorFn(ZeEXCEPT(Error, "Zquic",
	"server certPath and keyPath are required"));
      return false;
    }
    return this->init_(ZuMv(params), Zquic::Vantage::Server,
      [this](const ServerParams &) {
	if (!m_ticketKey) m_ticketKey = Ztls::Backend::ticket_key_new();
	return m_ticketKey;
      });
  }

  void final() {
    m_links = nullptr;
    m_routes.final();
    Base::final();
    finalTicketKey_();
  }

  bool listening() const { return Endpoint::listening(); }
  bool connected() const { return Endpoint::connected(); }
  const ZiSockAddr &local() const { return Endpoint::local(); }
  template <typename L>
  void endpointDiag(L &&l) const {
    auto mx = this->mx();
    if (!mx || mx->invoked(mx->txThread())) {
      EndpointDiag diag = Endpoint::diag();
      l(diag);
      return;
    }
    auto server = const_cast<Server *>(this);
    mx->txRun([server, l = ZuFwd<L>(l)]() mutable {
      EndpointDiag diag = server->Endpoint::diag();
      l(diag);
    });
  }
  template <typename L>
  void addrValidationDiag(L &&l) const {
    auto mx = this->mx();
    if (!mx) {
      AddrValidationDiag diag;
      l(diag);
      return;
    }
    auto server = const_cast<Server *>(this);
    if (server->rxInvoked()) {
      l(server->m_addrValidationDiag);
      return;
    }
    mx->rxRun([server, l = ZuFwd<L>(l)]() mutable {
      l(server->m_addrValidationDiag);
    });
  }
  template <typename L, typename Done>
  void allLinks(L &&l, Done &&done) {
    ZiAssert(this->mx() && this->rxThread(), "Zquic", (),
      "QUIC server link iteration before app initialization", return);
    if (this->rxInvoked()) {
      if (this->state() == ZmEngineState::Running)
	allLinks_0([&l](LinkRef &ref) { l(ZuMv(ref)); });
      done();
      return;
    }
    this->rxInvoke([this, l = ZuFwd<L>(l), done = ZuFwd<Done>(done)]() mutable {
      allLinks(ZuMv(l), ZuMv(done));
    });
  }

  void allLinks_(Ztc::Hub::AllLinksFn fn) {
    allLinks_0([&fn](LinkRef &ref) { fn(ref.ptr()); });
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
  ptls_encrypt_ticket_t *ticketEncryptCB_() {
    return Ztls::Backend::ticket_encrypt_cb(m_ticketKey);
  }
  bool sendPkt_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (!this->app()->sendPkt(buf)) return true;
    return Endpoint::send(ZuMv(buf), ZuMv(addr));
  }
  bool sendPktRaw_(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
      EcnMark::T ecn = EcnMark::NotECT, bool priority = false) {
    return Endpoint::send(ZuMv(buf), ZuMv(addr), ecn, priority);
  }
  void tombstoneRoute_(const CxnID &id) {
    m_routes.tombstone(id);
  }
  void refreshLinkRoutes_(Link *link) {
    if (this->state() == ZmEngineState::Running && link)
      link->installRoutes_(m_routes);
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
  template <typename L>
  void allLinks_0(L &&l) {
    ZmRef<LinkTable> table = m_links;
    if (!table) return;
    unsigned n = table->count_();
    if (!n) return;
    auto links = ZtLocalArray(LinkRefs, n);
    {
      auto i = table->citer();
      while (LinkRef ref = i.val())
	links.push(ZuMv(ref));
    }
    links.all(ZuFwd<L>(l));
  }

  void txDrained_() {
    auto i = m_links->citer();
    while (LinkRef ref = i.val())
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
    CxnID routedDCID;
    Link *link = route_(d, &routedDCID);
    if (!link) {
      sendStatelessReset_(d);
      Endpoint::failure();
      return;
    }
    if (link->receivedRouted_(ZuMv(d), routedDCID))
      link->installRoutes_(m_routes);
    else
      link->disconnect();
  }

  Link *route_(const Datagram &d, CxnID *routedDCID = nullptr) {
    if (!d.buf || !d.buf->length) return nullptr;
    auto packet = d.buf->cspan();
    if (Pkt::isLong(packet)) return routeLong_(d, packet);
    return routeShort_(d, packet, routedDCID);
  }

  Link *routeLong_(const Datagram &d, ZuBSpan packet) {
    LongHdr h;
    if (Pkt::parseLong(packet, h) < 0) return nullptr;
    if (!VersionNeg::supported(h.version)) {
      sendVerNeg_(h, d.addr);
      return nullptr;
    }
    if (Link *link = m_routes.find(h.dcid)) return link;
    if (h.type != PktType::Initial) return nullptr;
    InitialInfo info{h, d.addr, d.buf->length};
    if (!validateInitial_(info, packet)) {
      if (app()->retryAddrValidate())
	sendRetry_(h, d.addr);
      return nullptr;
    }
    return accept_(info);
  }

  Link *routeShort_(const Datagram &, ZuBSpan packet, CxnID *routedDCID) {
    return m_routes.matchShort(packet, routedDCID);
  }

  bool sendStatelessReset_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return false;
    auto packet = d.buf->cspan();
    if (Pkt::isLong(packet)) return false;
    ResetToken token;
    if (!m_routes.resetTokenForShort(packet, token)) return false;
    ZmRef<ZiIOBuf> buf = allocTxPkt_();
    int n = StatelessReset::writeForUnknownCID(
      buf->data_(), buf->size, packet, token);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    bool sent = sendPkt_(ZuMv(buf), d.addr);
    if (sent) {
      ZquicLOG(app()->qlogTrace(), ([
	resetBytes = unsigned(n)
      ](auto &o, ZuTime time) {
	SecEvt event{
	  .value = resetBytes,
	  .kind = SecKind::StatelessReset,
	  .success = true
	};
	event.trigger = SecTrigger::Sent;
	event.reason = SecReason::UnknownCID;
	o.logSecEvt(EvtName::StatelessReset, event, time);
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
	CxnStartedEvt{.local = local, .remote = remote, .linkInfo = linkInfo},
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

  void stop_() {		// hub callback - enter Rx thread
    Base::rxRun([this]() { stop_0(); });
  }

  void stop_0() {		// Rx thread - disconnect endpoint and links
    Endpoint::disconnect();
    m_stopCount = 0;
    {
      auto i = m_links->citer();
      while (LinkRef ref = i.val()) {
	ref->disconnect();
	++m_stopCount;
      }
    }
    m_routes.clear();
    if (!m_stopCount) stop_1();
  }

  void stop_1() {		// Rx thread - clean links / resume hub stop
    m_links->clean();
    Base::stop_0();
  }

  bool sendVerNeg_(const LongHdr &h, ZiSockAddr addr) {
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
	VersionEvt event;
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
    if (!app()->retryAddrValidate()) return true;
    ZuBSpan token = initialToken_(packet, info.header);
    if (!token) {
      ++m_addrValidationDiag.retryRejected;
      ZquicLOG(app()->qlogTrace(), ([
	tokenLength = 0U
      ](auto &o, ZuTime time) {
	SecEvt event{
	  .value = tokenLength,
	  .kind = SecKind::Retry,
	  .reason = SecReason::MissingToken,
	  .success = false
	};
	event.trigger = SecTrigger::Validated;
	o.logSecEvt(EvtName::RetryValid, event, time);
	event.kind = SecKind::Token;
	o.logSecEvt(EvtName::TokenReject, event, time);
      }));
      return false;
    }
    TokenInfo tokenInfo;
    TokenStatus::T status = AddressToken::validate(
      tokenInfo, token, app()->addrValidationSecret(), info.peer,
      uint64_t(Zm::now().sec()), app()->addrValidationLifetime(),
      app()->addrValidatePeerPort());
    if (status != TokenStatus::OK) {
      ++m_addrValidationDiag.retryRejected;
      switch (status) {
	case TokenStatus::Expired: ++m_addrValidationDiag.tokenExpired; break;
	case TokenStatus::Auth: ++m_addrValidationDiag.tokenAuthFailure; break;
	case TokenStatus::Address:
	  ++m_addrValidationDiag.tokenAddressMismatch;
	  break;
	case TokenStatus::Malformed:
	  ++m_addrValidationDiag.tokenMalformed;
	  break;
	case TokenStatus::Kind: ++m_addrValidationDiag.tokenKindMismatch; break;
	case TokenStatus::ODCID: ++m_addrValidationDiag.tokenODCIDMismatch; break;
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
	SecEvt event{
	  .value = tokenLength,
	  .kind = SecKind::Token,
	  .reason = reason,
	  .success = false
	};
	event.trigger = SecTrigger::Validated;
	o.logSecEvt(EvtName::TokenReject, event, time);
      }));
      return false;
    }
    switch (tokenInfo.kind) {
      case TokenKind::Retry:
	if (!(tokenInfo.serverCID == info.header.dcid)) {
	  ++m_addrValidationDiag.retryRejected;
	  ++m_addrValidationDiag.tokenODCIDMismatch;
	  ZquicLOG(app()->qlogTrace(), ([
	    tokenLength = token.length()
	  ](auto &o, ZuTime time) {
	    SecEvt event{
	      .value = tokenLength,
	      .kind = SecKind::Retry,
	      .reason = SecReason::RetrySCID,
	      .success = false
	    };
	    event.trigger = SecTrigger::Validated;
	    o.logSecEvt(EvtName::RetryValid, event, time);
	    event.kind = SecKind::Token;
	    o.logSecEvt(EvtName::TokenReject, event, time);
	  }));
	  return false;
	}
	info.retrySCID = info.header.dcid;
	info.origDCID = tokenInfo.origDCID.length() ?
	  tokenInfo.origDCID : info.header.dcid;
	++m_addrValidationDiag.retryAccepted;
	ZquicLOG(app()->qlogTrace(), ([
	  tokenLength = token.length()
	](auto &o, ZuTime time) {
	  SecEvt event{
	    .value = tokenLength,
	    .kind = SecKind::Retry,
	    .reason = SecReason::OK,
	    .success = true
	  };
	  event.trigger = SecTrigger::Validated;
	  o.logSecEvt(EvtName::RetryValid, event, time);
	  event.kind = SecKind::Token;
	  o.logSecEvt(EvtName::TokenValid, event, time);
	}));
	break;
      case TokenKind::NewToken:
	if (!app()->newTokenAddrValidate() || tokenInfo.serverCID.length()) {
	  ++m_addrValidationDiag.retryRejected;
	  ++m_addrValidationDiag.tokenKindMismatch;
	  ZquicLOG(app()->qlogTrace(), ([
	    tokenLength = token.length()
	  ](auto &o, ZuTime time) {
	    SecEvt event{
	      .value = tokenLength,
	      .kind = SecKind::Token,
	      .reason = SecReason::NewTokenPolicy,
	      .success = false
	    };
	    event.trigger = SecTrigger::Validated;
	    o.logSecEvt(EvtName::TokenReject, event, time);
	  }));
	  return false;
	}
	info.origDCID = info.header.dcid;
	++m_addrValidationDiag.newTokenAccepted;
	ZquicLOG(app()->qlogTrace(), ([
	  tokenLength = token.length()
	](auto &o, ZuTime time) {
	  SecEvt event{
	    .value = tokenLength,
	    .kind = SecKind::Token,
	    .reason = SecReason::OK,
	    .success = true
	  };
	  event.trigger = SecTrigger::Validated;
	  o.logSecEvt(EvtName::TokenValid, event, time);
	}));
	break;
      default:
	++m_addrValidationDiag.retryRejected;
	++m_addrValidationDiag.tokenKindMismatch;
	ZquicLOG(app()->qlogTrace(), ([
	  tokenLength = token.length()
	](auto &o, ZuTime time) {
	  SecEvt event{
	    .value = tokenLength,
	    .kind = SecKind::Token,
	    .reason = SecReason::Kind,
	    .success = false
	  };
	  event.trigger = SecTrigger::Validated;
	  o.logSecEvt(EvtName::TokenReject, event, time);
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
	  token, TokenKind::Retry, app()->addrValidationSecret(), addr,
	  h.dcid, retrySCID, uint64_t(Zm::now().sec()),
	  app()->addrValidatePeerPort()))
      return false;
    ZmRef<ZiIOBuf> buf = allocTxPkt_();
    int n = Pkt::writeRetryAuth(
      buf->data_(), buf->size, h.scid, retrySCID,
      token, h.dcid);
    if (n < 0) return false;
    buf->skip = 0;
    buf->length = unsigned(n);
    bool sent = sendPkt_(ZuMv(buf), ZuMv(addr));
    if (sent) {
      ++m_addrValidationDiag.retrySent;
      ZquicLOG(app()->qlogTrace(), ([
	tokenLength = token.length(),
	packetBytes = unsigned(n)
      ](auto &o, ZuTime time) {
	SecEvt event{
	  .value = tokenLength,
	  .kind = SecKind::Retry,
	  .success = true
	};
	event.trigger = SecTrigger::Sent;
	event.reason = SecReason::AddrValid;
	o.logSecEvt(EvtName::RetrySent, event, time);
	event.kind = SecKind::Token;
	event.value = packetBytes;
	o.logSecEvt(EvtName::TokenIssued, event, time);
      }));
    }
    return sent;
  }

  void finalTicketKey_() {
    if (!m_ticketKey) return;
    Ztls::Backend::ticket_key_free(m_ticketKey);
    m_ticketKey = nullptr;
  }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmRef<LinkTable>	m_links = new LinkTable{
    ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  CxnRouter<Link>	m_routes;
  AddrValidationDiag	m_addrValidationDiag;
  Ztls::Backend::TicketKey *m_ticketKey	= nullptr;
  unsigned		m_stopCount = 0;
};

} // namespace Zquic

#include <zlib/ZquicStream.hh>
#include <zlib/ZquicLink.hh>
#include <zlib/ZquicCliLink.hh>
#include <zlib/ZquicSrvLink.hh>

#endif /* Zquic_HH */
