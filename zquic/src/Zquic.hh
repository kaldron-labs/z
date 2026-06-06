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

#include <zpicotls.h>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPolymorph.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtLocalArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicStream.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicPacketBuilder.hh>
#include <zlib/ZquicEndpoint.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZquicRecovery.hh>

namespace Zquic {

ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Zquic.Log">>);
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Zquic.Param">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Zquic.ParamStrings">>));
ZuDerive(ALPNData, (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.ALPNData">>));
ZuDerive(ALPN, (ZtArray<ptls_iovec_t, ZtArrayHeapID<"Zquic.ALPN">>));

using ErrorFn = ZmFn<void(ZeException)>;

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
  ref = SentFrameRef::control();
  return true;
}

struct RuntimeDiag {
	ZmAtomic<uint64_t>	endpointReady = 0;
	ZmAtomic<uint64_t>	endpointFailures = 0;
	ZmAtomic<uint64_t>	datagramsRx = 0;
	ZmAtomic<uint64_t>	bytesRx = 0;
	ZmAtomic<uint64_t>	packetsRx = 0;
	ZmAtomic<uint64_t>	initialPacketsRx = 0;
	ZmAtomic<uint64_t>	framesRx = 0;
	ZmAtomic<uint64_t>	pingFramesRx = 0;
	ZmAtomic<uint64_t>	ackFramesRx = 0;
	ZmAtomic<uint64_t>	ackFramesTx = 0;
	ZmAtomic<uint64_t>	handshakeDoneFramesRx = 0;
	ZmAtomic<uint64_t>	handshakeDoneFramesTx = 0;
	ZmAtomic<uint64_t>	packetParseErrors = 0;
	ZmAtomic<uint64_t>	packetsTx = 0;
	ZmAtomic<uint64_t>	initialPacketsTx = 0;
	ZmAtomic<uint64_t>	bytesTx = 0;
	ZmAtomic<uint64_t>	handshakePacketsRx = 0;
	ZmAtomic<uint64_t>	handshakePacketsTx = 0;
	ZmAtomic<uint64_t>	shortPacketsRx = 0;
	ZmAtomic<uint64_t>	shortPacketsTx = 0;
	ZmAtomic<uint64_t>	protectedPacketsRx = 0;
	ZmAtomic<uint64_t>	protectedPacketsTx = 0;
	ZmAtomic<uint64_t>	packetProtectionFailures = 0;
	ZmAtomic<uint64_t>	tlsFailures = 0;
	ZmAtomic<uint64_t>	cryptoFramesRx = 0;
	ZmAtomic<uint64_t>	cryptoFramesTx = 0;
	ZmAtomic<uint64_t>	cryptoBytesRx = 0;
	ZmAtomic<uint64_t>	cryptoBytesTx = 0;
	ZmAtomic<uint64_t>	streamFramesRx = 0;
	ZmAtomic<uint64_t>	streamFramesTx = 0;
	ZmAtomic<uint64_t>	streamBytesRx = 0;
	ZmAtomic<uint64_t>	streamBytesTx = 0;
	ZmAtomic<uint64_t>	handshakeComplete = 0;
};

inline void inspectRuntimeDatagram(RuntimeDiag &diag, const Datagram &d)
{
  ++diag.datagramsRx;
  if (!d.buf) {
    ++diag.packetParseErrors;
    return;
  }
  diag.bytesRx += d.buf->length;

  LongHeader h;
  if (Packet::parseLong(d.buf->cspan(), h) < 0) {
    ++diag.packetParseErrors;
    return;
  }
  ++diag.packetsRx;
  if (h.type == PacketType::Initial) ++diag.initialPacketsRx;

  if (h.length < h.pnLength || h.length > d.buf->length) {
    ++diag.packetParseErrors;
    return;
  }
  unsigned packetLength = h.length;
  if (h.pnOffset > d.buf->length - packetLength) {
    ++diag.packetParseErrors;
    return;
  }
  unsigned payloadLength = packetLength - h.pnLength;
  unsigned payloadOffset = h.payloadOffset;
  if (payloadOffset > d.buf->length ||
      payloadLength > d.buf->length - payloadOffset) {
    ++diag.packetParseErrors;
    return;
  }

  const char *payload =
    reinterpret_cast<const char *>(d.buf->data() + payloadOffset);
  unsigned offset = 0;
  while (offset < payloadLength) {
    Frame frame;
    unsigned used = 0;
    if (FrameCodec::parse(
	  ZuCSpan{payload + offset, payloadLength - offset},
	  frame, used) < 0 || !used) {
      ++diag.packetParseErrors;
      return;
    }
    ++diag.framesRx;
    if (frame.type == FrameType::Ping) ++diag.pingFramesRx;
    offset += used;
  }
}

inline bool writeInitialPingProbe(ZiIOBuf *buf, uint64_t packetNumber)
{
  if (!buf) return false;
  if (buf->size < MinUDPPayload && !buf->ensure(MinUDPPayload)) return false;

  ConnectionID dcid{"zqserv01"};
  ConnectionID scid{"zqcli001"};
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

inline ZuCSpan byteSpan(const uint8_t *data, unsigned len)
{
  return ZuCSpan{reinterpret_cast<const char *>(data), len};
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

struct EngineParams {
  EngineParams(
    ZiMultiplex *mx_ = nullptr,
    ZuCSpan rxThread_ = {},
    ZuCSpan txThread_ = {}) :
      mx{mx_}, rxThread{rxThread_}, txThread{txThread_},
      errorFn_{defaultErrorFn()} { }

  EngineParams &&caPath(ZuCSpan v) { caPath_ = v; return ZuMv(*this); }
  EngineParams &&certPath(ZuCSpan v) { certPath_ = v; return ZuMv(*this); }
  EngineParams &&keyPath(ZuCSpan v) { keyPath_ = v; return ZuMv(*this); }
  EngineParams &&asyncThread(ZuCSpan v) {
    asyncThread_ = v;
    return ZuMv(*this);
  }
  EngineParams &&maxData(uint64_t v) { maxData_ = v; return ZuMv(*this); }
  EngineParams &&maxStreamData(uint64_t v) {
    maxStreamData_ = v;
    return ZuMv(*this);
  }
  EngineParams &&maxStreamsBidi(uint64_t v) {
    maxStreamsBidi_ = v;
    return ZuMv(*this);
  }
  EngineParams &&maxStreamsUni(uint64_t v) {
    maxStreamsUni_ = v;
    return ZuMv(*this);
  }
  EngineParams &&maxUDP(unsigned v) { maxUDP_ = v; return ZuMv(*this); }
  EngineParams &&alpn(ZuSpan<ZuCSpan> v) {
    alpn_.length(0);
    alpn_.ensure(v.length());
    for (auto &s : v) alpn_.push(ParamString{s});
    return ZuMv(*this);
  }
  EngineParams &&alpn(ZuSpan<const ptls_iovec_t> v) {
    alpn_.length(0);
    alpn_.ensure(v.length());
    for (auto &p : v)
      alpn_.push(ParamString{ZuCSpan{
	reinterpret_cast<const char *>(p.base), p.len}});
    return ZuMv(*this);
  }
  EngineParams &&errorFn(ErrorFn v) { errorFn_ = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex	*mx = nullptr;
  ParamString	rxThread;
  ParamString	txThread;
  ParamStrings	alpn_;
  ParamString	caPath_;
  ParamString	certPath_;
  ParamString	keyPath_;
  ParamString	asyncThread_;
  uint64_t	maxData_ = 0;
  uint64_t	maxStreamData_ = 0;
  uint64_t	maxStreamsBidi_ = 0;
  uint64_t	maxStreamsUni_ = 0;
  unsigned	maxUDP_ = MinUDPPayload;
  ErrorFn	errorFn_;
};

struct ClientParams : public EngineParams {
  using EngineParams::EngineParams;

  ClientParams &&caPath(ZuCSpan v)
    { EngineParams::caPath(v); return ZuMv(*this); }
  ClientParams &&certPath(ZuCSpan v)
    { EngineParams::certPath(v); return ZuMv(*this); }
  ClientParams &&keyPath(ZuCSpan v)
    { EngineParams::keyPath(v); return ZuMv(*this); }
  ClientParams &&asyncThread(ZuCSpan v)
    { EngineParams::asyncThread(v); return ZuMv(*this); }
  ClientParams &&maxData(uint64_t v)
    { EngineParams::maxData(v); return ZuMv(*this); }
  ClientParams &&maxStreamData(uint64_t v)
    { EngineParams::maxStreamData(v); return ZuMv(*this); }
  ClientParams &&maxStreamsBidi(uint64_t v)
    { EngineParams::maxStreamsBidi(v); return ZuMv(*this); }
  ClientParams &&maxStreamsUni(uint64_t v)
    { EngineParams::maxStreamsUni(v); return ZuMv(*this); }
  ClientParams &&maxUDP(unsigned v)
    { EngineParams::maxUDP(v); return ZuMv(*this); }
  ClientParams &&alpn(ZuSpan<ZuCSpan> v)
    { EngineParams::alpn(v); return ZuMv(*this); }
  ClientParams &&alpn(ZuSpan<const ptls_iovec_t> v)
    { EngineParams::alpn(v); return ZuMv(*this); }
  ClientParams &&errorFn(ErrorFn v)
    { EngineParams::errorFn(ZuMv(v)); return ZuMv(*this); }
};

struct ServerParams : public EngineParams {
  using EngineParams::EngineParams;

  ServerParams &&caPath(ZuCSpan v)
    { EngineParams::caPath(v); return ZuMv(*this); }
  ServerParams &&certPath(ZuCSpan v)
    { EngineParams::certPath(v); return ZuMv(*this); }
  ServerParams &&keyPath(ZuCSpan v)
    { EngineParams::keyPath(v); return ZuMv(*this); }
  ServerParams &&asyncThread(ZuCSpan v)
    { EngineParams::asyncThread(v); return ZuMv(*this); }
  ServerParams &&maxData(uint64_t v)
    { EngineParams::maxData(v); return ZuMv(*this); }
  ServerParams &&maxStreamData(uint64_t v)
    { EngineParams::maxStreamData(v); return ZuMv(*this); }
  ServerParams &&maxStreamsBidi(uint64_t v)
    { EngineParams::maxStreamsBidi(v); return ZuMv(*this); }
  ServerParams &&maxStreamsUni(uint64_t v)
    { EngineParams::maxStreamsUni(v); return ZuMv(*this); }
  ServerParams &&maxUDP(unsigned v)
    { EngineParams::maxUDP(v); return ZuMv(*this); }
  ServerParams &&alpn(ZuSpan<ZuCSpan> v)
    { EngineParams::alpn(v); return ZuMv(*this); }
  ServerParams &&alpn(ZuSpan<const ptls_iovec_t> v)
    { EngineParams::alpn(v); return ZuMv(*this); }
  ServerParams &&errorFn(ErrorFn v)
    { EngineParams::errorFn(ZuMv(v)); return ZuMv(*this); }
};

template <typename App_> class Engine : public ZmPolymorph {
public:
  using App = App_;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  bool init(EngineParams params) {
    return init_(ZuMv(params), [](const EngineParams &) { return true; });
  }

  void final() {
    m_mx = nullptr;
    m_rxThread = m_txThread = m_asyncThread = 0;
    m_errorFn = ErrorFn{};
    m_alpn.length(0);
    m_alpnData.length(0);
    m_caPath = m_certPath = m_keyPath = ParamString{};
    m_maxData = m_maxStreamData = 0;
    m_maxStreamsBidi = m_maxStreamsUni = 0;
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
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(m_txThread, ZuFwd<Args>(args)...);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(m_txThread, ZuFwd<Args>(args)...);
  }

protected:
  template <typename Params, typename L>
  bool init_(Params params, L l) {
    if (!validate_(params)) return false;
    m_mx = params.mx;
    m_rxThread = thread_(params.rxThread, m_mx->rxThread());
    m_txThread = thread_(params.txThread, m_mx->txThread());
    m_asyncThread = params.asyncThread_ ?
      m_mx->sid(params.asyncThread_) : 0;
    m_errorFn = ZuMv(params.errorFn_);
    if (!m_errorFn) m_errorFn = defaultErrorFn();
    m_caPath = params.caPath_;
    m_certPath = params.certPath_;
    m_keyPath = params.keyPath_;
    m_maxData = params.maxData_;
    m_maxStreamData = params.maxStreamData_;
    m_maxStreamsBidi = params.maxStreamsBidi_;
    m_maxStreamsUni = params.maxStreamsUni_;
    m_maxUDP = params.maxUDP_;
    if (!init_alpn_(params.alpn_)) return false;
    return l(params);
  }

private:
  unsigned thread_(const ParamString &id, unsigned deflt) const {
    return id ? m_mx->sid(id) : deflt;
  }

  template <typename Params>
  bool validate_(const Params &params) const {
    if (ZuUnlikely(!params.mx)) {
      ZiLOG(Error, Log, "multiplexer is null");
      return false;
    }
    unsigned rxThread = params.rxThread ?
      params.mx->sid(params.rxThread) : params.mx->rxThread();
    unsigned txThread = params.txThread ?
      params.mx->sid(params.txThread) : params.mx->txThread();
    if (!rxThread || rxThread > params.mx->params().nThreads()) {
      ZiLOG(Error, Log, ([thread = LogMsg{params.rxThread}](auto &s) {
	s << "invalid QUIC Rx thread ID \"" << thread << '"';
      }));
      return false;
    }
    if (!txThread || txThread > params.mx->params().nThreads()) {
      ZiLOG(Error, Log, ([thread = LogMsg{params.txThread}](auto &s) {
	s << "invalid QUIC Tx thread ID \"" << thread << '"';
      }));
      return false;
    }
    if (rxThread == txThread) {
      ZiLOG(Error, Log, "QUIC Rx and Tx threads must differ");
      return false;
    }
    if (!params.mx->running()) {
      ZiLOG(Error, Log, "multiplexer not running");
      return false;
    }
    if (params.maxUDP_ > BufSize) {
      ZiLOG(Error, Log, ([maxUDP = params.maxUDP_](auto &s) {
	s << "maxUDP " << maxUDP << " exceeds packet buffer size " << BufSize;
      }));
      return false;
    }
    if (params.asyncThread_) {
#ifdef _WIN32
      ZiLOG(Error, Log, "asyncThread is unsupported on Windows");
      return false;
#else
      unsigned asyncThread = params.mx->sid(params.asyncThread_);
      if (!asyncThread || asyncThread > params.mx->params().nThreads()) {
	ZiLOG(Error, Log, ([thread = LogMsg{params.asyncThread_}](auto &s) {
	  s << "invalid async thread ID \"" << thread << '"';
	}));
	return false;
      }
      if (asyncThread == rxThread || asyncThread == txThread) {
	ZiLOG(Error, Log,
	  "async thread must differ from QUIC Rx and Tx threads");
	return false;
      }
      if (asyncThread == params.mx->rxThread() ||
	  asyncThread == params.mx->txThread()) {
	ZiLOG(Error, Log, "async thread must differ from I/O threads");
	return false;
      }
      if (!params.mx->params().thread(asyncThread).isolated()) {
	ZiLOG(Error, Log, "async thread must be isolated");
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

  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_rxThread = 0;
  unsigned	m_txThread = 0;
  unsigned	m_asyncThread = 0;
  ErrorFn	m_errorFn;
  ALPNData	m_alpnData;
  ALPN		m_alpn;
  ParamString	m_caPath;
  ParamString	m_certPath;
  ParamString	m_keyPath;
  uint64_t	m_maxData = 0;
  uint64_t	m_maxStreamData = 0;
  uint64_t	m_maxStreamsBidi = 0;
  uint64_t	m_maxStreamsUni = 0;
  unsigned	m_maxUDP = MinUDPPayload;
};

template <typename App_> class Client : public Engine<App_> {
public:
  using App = App_;
  using Base = Engine<App>;
  static constexpr unsigned TLSBufSize = 64 * 1024;
  static constexpr unsigned RuntimePNLength = 2;
  static constexpr unsigned RuntimeCryptoChunk = 900;

  bool init(ClientParams params) {
    return this->init_(ZuMv(params), [](const ClientParams &) { return true; });
  }

  void final() {
    close();
    Base::final();
  }

  bool connect(
    ZiIP localIP, uint16_t localPort, ZiIP remoteIP, uint16_t remotePort) {
    if (!this->mx() || !remoteIP || !remotePort) return false;
    close();
    m_runtimeDiag = {};
    m_runtimeProbePN = 0;
    return m_endpoint.openUDP(
      this->mx(), PathMode::ClientConnected,
      ZuMv(localIP), localPort, ZuMv(remoteIP), remotePort,
      Endpoint::DatagramFn{[this](Datagram d) { received_(ZuMv(d)); }},
      Endpoint::ReadyFn{[this](Endpoint *) {
	++m_runtimeDiag.endpointReady;
	startHandshake_();
      }},
      Endpoint::FailFn{[this](bool) { ++m_runtimeDiag.endpointFailures; }});
  }

  void close() {
    m_endpoint.closeUDP();
    resetRuntimeState_();
  }

  bool sendInitialProbe() {
    if (!m_endpoint.connected() || !m_endpoint.listening()) return false;
    ZmRef<ZiIOBuf> buf = m_endpoint.allocTxPacket();
    if (!writeInitialPingProbe(buf, m_runtimeProbePN)) return false;
    unsigned length = buf->length;
    if (!m_endpoint.send(ZuMv(buf), m_endpoint.remote())) return false;
    ++m_runtimeProbePN;
    ++m_runtimeDiag.packetsTx;
    ++m_runtimeDiag.initialPacketsTx;
    m_runtimeDiag.bytesTx += length;
    return true;
  }

  bool connected() const { return m_endpoint.connected(); }
  bool ready() const { return m_endpoint.listening(); }
  bool established() const { return m_established; }
  const ZiSockAddr &local() const { return m_endpoint.local(); }
  const ZiSockAddr &remote() const { return m_endpoint.remote(); }
  const RuntimeDiag &runtimeDiag() const { return m_runtimeDiag; }
  const EndpointDiag &endpointDiag() const { return m_endpoint.diag(); }
  const Crypto &crypto() const { return m_crypto; }

  bool sendBidi(ZuCSpan payload, bool fin = true) {
    return sendStream_(0, payload, fin);
  }
  bool sendUni(ZuCSpan payload, bool fin = true) {
    return sendStream_(2, payload, fin);
  }
  bool sendStream(uint64_t streamID, ZuCSpan payload, bool fin = true) {
    return sendStream_(streamID, payload, fin);
  }

private:
  void resetRuntimeState_() {
    m_established = 0;
    m_handshakeStarted = 0;
    m_initialDCID = {};
    m_localSCID = {};
    m_peerCID = {};
    m_transportParams = {};
    for (auto &s : m_txCrypto) s.reset();
    for (auto &s : m_rxCrypto) s.reset();
    memset(m_txPN, 0, sizeof(m_txPN));
    memset(m_rxLargestPN, 0, sizeof(m_rxLargestPN));
    for (auto &a : m_rxPackets) a.clear();
    for (auto &p : m_txPackets) p.clear();
    memset(m_pendingAck, 0, sizeof(m_pendingAck));
  }

  bool initRuntimeCrypto_() {
    m_initialDCID.set("zqinit01");
    m_localSCID.set("zqcli001");
    m_peerCID = m_initialDCID;
    m_transportParams.initialSCID = m_localSCID;
    m_transportParams.maxUDPPayloadSize = this->maxUDP();
    m_transportParams.initialMaxData = this->maxData();
    m_transportParams.initialMaxStreamDataBidiLocal = this->maxStreamData();
    m_transportParams.initialMaxStreamDataBidiRemote = this->maxStreamData();
    m_transportParams.initialMaxStreamDataUni = this->maxStreamData();
    m_transportParams.initialMaxStreamsBidi = this->maxStreamsBidi();
    m_transportParams.initialMaxStreamsUni = this->maxStreamsUni();
    if (!m_crypto.deriveInitial(m_initialDCID)) {
      ++m_runtimeDiag.tlsFailures;
      return false;
    }
    if (!m_crypto.initTLS(CryptoConfig{
	false, false, this->firstALPN(), this->caPath(), {}, {}, "localhost",
	&m_transportParams})) {
      ++m_runtimeDiag.tlsFailures;
      return false;
    }
    return true;
  }

  bool startHandshake_() {
    if (m_handshakeStarted) return true;
    resetRuntimeState_();
    if (!initRuntimeCrypto_()) return false;
    m_handshakeStarted = 1;
    return emitTLS_(0, {}, m_endpoint.remote());
  }

  void markEstablished_() {
    if (m_established || !m_crypto.oneRTTReady() ||
	!m_crypto.txTrafficSecretInstalled(CryptoLevel::OneRTT) ||
	!m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT))
      return;
    m_established = 1;
    ++m_runtimeDiag.handshakeComplete;
  }

  bool emitTLS_(size_t inEpoch, ZuCSpan input, ZiSockAddr addr) {
    ZmRef<ZiIOBuf> out =
      new CryptoTxBufAlloc<TLSBufSize, TLSBufSize>{this};
    size_t offsets[5] = {};
    int n = m_crypto.handleTLSMessage(out.ptr(), offsets, inEpoch, input);
    if (n < 0) {
      ++m_runtimeDiag.tlsFailures;
      return false;
    }
    if (!sendCryptoFlights_(out->data(), unsigned(n), offsets, addr))
      return false;
    markEstablished_();
    return true;
  }

  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    for (size_t epoch = 0; epoch < 4; ++epoch) {
      if (offsets[epoch + 1] <= offsets[epoch]) continue;
      CryptoLevel::T level;
      if (!cryptoLevelFromEpoch(epoch, level)) continue;
      unsigned off = unsigned(offsets[epoch]);
      unsigned remaining = unsigned(offsets[epoch + 1] - offsets[epoch]);
      while (remaining) {
	unsigned chunk = remaining > RuntimeCryptoChunk ?
	  RuntimeCryptoChunk : remaining;
	uint8_t frame[BufSize];
	uint64_t cryptoOffset = m_txCrypto[level].txOffset();
	int n = m_txCrypto[level].writeFramePrefix(
	  frame, sizeof(frame), chunk);
	if (n < 0) {
	  ++m_runtimeDiag.tlsFailures;
	  return false;
	}
	SentFrameRef ref = SentFrameRef::crypto(cryptoOffset, chunk);
	++m_runtimeDiag.cryptoFramesTx;
	m_runtimeDiag.cryptoBytesTx += chunk;
	if (!sendCryptoPacket_(
	      level, byteSpan(frame, unsigned(n)),
	      byteSpan(data + off, chunk), ref, addr))
	  return false;
	if (level == CryptoLevel::Handshake &&
	    !sendCryptoPacket_(
	      level, byteSpan(frame, unsigned(n)),
	      byteSpan(data + off, chunk), ref, addr))
	  return false;
	off += chunk;
	remaining -= chunk;
      }
    }
    return true;
  }

  bool sendCryptoPacket_(CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(level, build, frame)) return false;
    if (level == CryptoLevel::Initial)
      return sendInitialPacket_(build, ZuMv(addr), frame);
    if (level == CryptoLevel::Handshake)
      return sendHandshakePacket_(build, ZuMv(addr), frame);
    return sendShortPacket_(build, ZuMv(addr), frame);
  }

  bool sendCryptoPacket_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(level, build, prefix, payload)) return false;
    if (level == CryptoLevel::Initial)
      return sendInitialPacket_(build, ZuMv(addr), {}, &ref, true);
    if (level == CryptoLevel::Handshake)
      return sendHandshakePacket_(build, ZuMv(addr), {}, &ref, true);
    return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
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
    ++m_runtimeDiag.ackFramesTx;
    return true;
  }

  bool recordRxPacket_(CryptoLevel::T level, uint64_t pn) {
    if (m_rxPackets[level].contains(pn)) return false;
    m_rxPackets[level].add(pn);
    if (pn > m_rxLargestPN[level]) m_rxLargestPN[level] = pn;
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
    if (!frame.ackRangeCount) return;
    m_txPackets[level].ack(frame.ackRanges, frame.ackRangeCount);
  }

  bool buildPayload_(CryptoLevel::T level, PacketBuild &build, ZuCSpan frame) {
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

  bool sendInitialPacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Initial, payload, frame)) return false;
    return sendInitialPacket_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    ZmRef<ZiIOBuf> buf = m_endpoint.allocTxPacket();
    int headerLen = -1;
    unsigned targetPlainLen = payload.bytes();
    for (unsigned i = 0; i < 4; ++i) {
      headerLen = Packet::writeInitial(
	buf->data_(), buf->size, m_initialDCID, m_localSCID,
	targetPlainLen + InitialSecret::TagLen, RuntimePNLength);
      if (headerLen < 0) return false;
      unsigned minPlainLen = MinUDPPayload -
	unsigned(headerLen) - RuntimePNLength - InitialSecret::TagLen;
      if (minPlainLen <= targetPlainLen) break;
      targetPlainLen = minPlainLen;
    }
    if (!payload.padTo(targetPlainLen)) return false;
    if (PacketNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Initial], RuntimePNLength) !=
	int(RuntimePNLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Initial];
    int n = InitialPacketProtection::protectLongV(
      buf->data_(), buf->size, m_crypto.initialKeys().client,
      pn,
      byteSpan(buf->data_(), unsigned(headerLen) + RuntimePNLength),
      payload.data(), payload.count(), unsigned(headerLen), RuntimePNLength);
    if (n < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!m_endpoint.send(ZuMv(buf), ZuMv(addr))) return false;
    if (recordRef)
      recordTxPacket_(
	CryptoLevel::Initial, pn, unsigned(n), *recordRef, ackEliciting);
    else
      recordTxPacket_(CryptoLevel::Initial, pn, unsigned(n), recordFrame);
    ++m_txPN[CryptoLevel::Initial];
    ++m_runtimeDiag.packetsTx;
    ++m_runtimeDiag.initialPacketsTx;
    ++m_runtimeDiag.protectedPacketsTx;
    m_runtimeDiag.bytesTx += unsigned(n);
    return true;
  }

  bool sendHandshakePacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Handshake, payload, frame)) return false;
    return sendHandshakePacket_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_crypto.txTrafficSecretInstalled(CryptoLevel::Handshake)) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    ZmRef<ZiIOBuf> buf = m_endpoint.allocTxPacket();
    int headerLen = Packet::writeHandshake(
      buf->data_(), buf->size, m_peerCID, m_localSCID,
      payload.bytes() +
	m_crypto.txTrafficSecret(CryptoLevel::Handshake).tagLen,
      RuntimePNLength);
    if (headerLen < 0 ||
	PacketNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Handshake], RuntimePNLength) !=
	int(RuntimePNLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Handshake];
    int n = PacketProtection::protectLongV(
      buf->data_(), buf->size,
      m_crypto.txProtectionState(CryptoLevel::Handshake),
      pn,
      byteSpan(buf->data_(), unsigned(headerLen) + RuntimePNLength),
      payload.data(), payload.count(), unsigned(headerLen), RuntimePNLength);
    if (n < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!m_endpoint.send(ZuMv(buf), ZuMv(addr))) return false;
    if (recordRef)
      recordTxPacket_(
	CryptoLevel::Handshake, pn, unsigned(n), *recordRef, ackEliciting);
    else
      recordTxPacket_(CryptoLevel::Handshake, pn, unsigned(n), recordFrame);
    ++m_txPN[CryptoLevel::Handshake];
    ++m_runtimeDiag.packetsTx;
    ++m_runtimeDiag.handshakePacketsTx;
    ++m_runtimeDiag.protectedPacketsTx;
    m_runtimeDiag.bytesTx += unsigned(n);
    return true;
  }

  bool sendShortPacket_(ZuCSpan payload, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(CryptoLevel::OneRTT, build, payload)) return false;
    return sendShortPacket_(build, ZuMv(addr), payload);
  }

  bool sendShortPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_established &&
	!m_crypto.txTrafficSecretInstalled(CryptoLevel::OneRTT)) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    ZmRef<ZiIOBuf> buf = m_endpoint.allocTxPacket();
    int headerLen = Packet::writeShort(
      buf->data_(), buf->size, m_peerCID, m_txPN[CryptoLevel::OneRTT],
      RuntimePNLength);
    if (headerLen < 0) return false;
    if (!payload.padForProtectionSample(
	  unsigned(headerLen) - RuntimePNLength, RuntimePNLength,
	  m_crypto.txTrafficSecret(CryptoLevel::OneRTT).tagLen))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::OneRTT];
    int n = PacketProtection::protectShortV(
      buf->data_(), buf->size, m_crypto.txProtectionState(CryptoLevel::OneRTT),
      pn,
      byteSpan(buf->data_(), unsigned(headerLen)),
      payload.data(), payload.count(),
      unsigned(headerLen) - RuntimePNLength, RuntimePNLength);
    if (n < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!m_endpoint.send(ZuMv(buf), ZuMv(addr))) return false;
    if (recordRef)
      recordTxPacket_(
	CryptoLevel::OneRTT, pn, unsigned(n), *recordRef, ackEliciting);
    else
      recordTxPacket_(CryptoLevel::OneRTT, pn, unsigned(n), recordFrame);
    ++m_txPN[CryptoLevel::OneRTT];
    ++m_runtimeDiag.packetsTx;
    ++m_runtimeDiag.shortPacketsTx;
    ++m_runtimeDiag.protectedPacketsTx;
    m_runtimeDiag.bytesTx += unsigned(n);
    return true;
  }

  bool sendStream_(uint64_t streamID, ZuCSpan payload, bool fin) {
    if (!m_established || !payload.length()) return false;
    if (!sendShortStreamPacket_(streamID, payload, fin, m_endpoint.remote()))
      return false;
    ++m_runtimeDiag.streamFramesTx;
    m_runtimeDiag.streamBytesTx += payload.length();
    return true;
  }

  bool sendShortStreamPacket_(
    uint64_t streamID, ZuCSpan payload, bool fin, ZiSockAddr addr) {
    PacketBuild build;
    build.reset();
    if (!appendPendingAck_(CryptoLevel::OneRTT, build)) return false;
    int n = FrameCodec::writeStreamPrefix(
      build.scratch(), build.scratchAvail(), streamID, 0,
      payload.length(), fin);
    if (n < 0 ||
	!build.commitScratch(unsigned(n)) ||
	!build.add(payload))
      return false;
    SentFrameRef ref;
    ref.kind = SentFrameKind::Stream;
    ref.streamID = streamID;
    ref.length = payload.length();
    ref.fin = fin;
    ref.range = TxRange{nullptr, 0, uint32_t(payload.length()), 0};
    return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
  }

  void received_(Datagram d) {
    ++m_runtimeDiag.datagramsRx;
    if (!d.buf) {
      ++m_runtimeDiag.packetParseErrors;
      return;
    }
    m_runtimeDiag.bytesRx += d.buf->length;
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
	unsigned packetLen = h.pnOffset + h.length;
	if (packetLen > packet.length() || packetLen < h.payloadOffset) {
	  ok = false;
	  break;
	}
	if (!receivedLong_(d, offset, packetLen)) ok = false;
	offset += packetLen;
	continue;
      }
      if (!receivedShort_(d, offset, d.buf->length - offset)) ok = false;
      break;
    }
    if (!ok) ++m_runtimeDiag.packetParseErrors;
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{
      reinterpret_cast<const char *>(base), packetLen};
    LongHeader h;
    if (Packet::parseLong(packet, h) < 0) return false;
    if (h.scid.length()) m_peerCID = h.scid;
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
	base, packetLen, m_crypto.initialKeys().server,
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    else {
      if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::Handshake)) {
	++m_runtimeDiag.packetProtectionFailures;
	return false;
      }
      plainLen = PacketProtection::unprotectLong(
	base, packetLen,
	m_crypto.rxProtectionState(CryptoLevel::Handshake),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    }
    if (plainLen < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    if (!recordRxPacket_(level, pn)) return true;
    ++m_runtimeDiag.packetsRx;
    ++m_runtimeDiag.protectedPacketsRx;
    if (level == CryptoLevel::Initial) ++m_runtimeDiag.initialPacketsRx;
    else ++m_runtimeDiag.handshakePacketsRx;
    return consumeFrames_(
      level, pn, byteSpan(base + payloadOffset, unsigned(plainLen)),
      d.addr);
  }

  bool receivedShort_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT))
      return false;
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{
      reinterpret_cast<const char *>(base), packetLen};
    ShortHeader h;
    if (Packet::parseShort(
	  packet, m_localSCID.length(), h) < 0 ||
	!(h.dcid == m_localSCID))
      return false;
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = PacketProtection::unprotectShort(
      base, packetLen,
      m_crypto.rxProtectionState(CryptoLevel::OneRTT),
      m_rxLargestPN[CryptoLevel::OneRTT],
      h.pnOffset, pn, payloadOffset);
    if (plainLen < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    if (!recordRxPacket_(CryptoLevel::OneRTT, pn)) return true;
    ++m_runtimeDiag.packetsRx;
    ++m_runtimeDiag.shortPacketsRx;
    ++m_runtimeDiag.protectedPacketsRx;
    return consumeFrames_(
      CryptoLevel::OneRTT,
      pn,
      byteSpan(base + payloadOffset, unsigned(plainLen)), d.addr);
  }

  bool consumeFrames_(
    CryptoLevel::T level, uint64_t pn, ZuCSpan frames, ZiSockAddr addr) {
    unsigned offset = 0;
    while (offset < frames.length()) {
      Frame frame;
      unsigned used = 0;
      if (FrameCodec::parse(
	    ZuCSpan{frames.data() + offset, frames.length() - offset},
	    frame, used) < 0 || !used)
	return false;
      ++m_runtimeDiag.framesRx;
      if (FrameCodec::ackEliciting(frame.type)) noteAck_(level, pn);
      if (frame.type == FrameType::Ping) ++m_runtimeDiag.pingFramesRx;
      if (frame.type == FrameType::Ack) {
	++m_runtimeDiag.ackFramesRx;
	processAckFrame_(level, frame);
      }
      if (frame.type == FrameType::HandshakeDone)
	++m_runtimeDiag.handshakeDoneFramesRx;
      if (frame.type == FrameType::Crypto) {
	ZuCSpan contiguous;
	if (m_rxCrypto[level].receiveFrame(frame, contiguous) < 0)
	  return false;
	++m_runtimeDiag.cryptoFramesRx;
	m_runtimeDiag.cryptoBytesRx += frame.payload.length();
	if (contiguous) {
	  size_t epoch = level == CryptoLevel::Initial ? 0 :
	    level == CryptoLevel::Handshake ? 2 : 3;
	  if (!emitTLS_(epoch, contiguous, ZuMv(addr))) return false;
	}
      } else if (frame.type == FrameType::Stream) {
	++m_runtimeDiag.streamFramesRx;
	m_runtimeDiag.streamBytesRx += frame.payload.length();
	if constexpr (requires(App *a, uint64_t streamID, uint64_t offset,
	      ZuCSpan payload, bool fin) {
	  a->zquicStream(streamID, offset, payload, fin);
	})
	  this->app()->zquicStream(
	    frame.streamID, frame.offset, frame.payload, frame.fin);
      }
      offset += used;
    }
    return true;
  }

	Endpoint	m_endpoint;
	Crypto		m_crypto;
	TransportParams m_transportParams;
	CryptoStream	m_txCrypto[3];
	CryptoStream	m_rxCrypto[3];
	ConnectionID	m_initialDCID;
	ConnectionID	m_localSCID;
	ConnectionID	m_peerCID;
	RuntimeDiag	m_runtimeDiag;
	uint64_t	m_txPN[3]{};
	uint64_t	m_rxLargestPN[3]{};
	AckTracker	m_rxPackets[3];
	PacketTxSpace	m_txPackets[3];
	uint64_t	m_runtimeProbePN = 0;
	bool		m_pendingAck[3]{};
	ZmAtomic<unsigned>	m_handshakeStarted = 0;
	ZmAtomic<unsigned>	m_established = 0;
};

template <typename App_> class Server : public Engine<App_> {
public:
  using App = App_;
  using Base = Engine<App>;
  static constexpr unsigned TLSBufSize = Client<App>::TLSBufSize;
  static constexpr unsigned RuntimePNLength = Client<App>::RuntimePNLength;
  static constexpr unsigned RuntimeCryptoChunk = Client<App>::RuntimeCryptoChunk;

  bool init(ServerParams params) {
    return this->init_(ZuMv(params), [](const ServerParams &) { return true; });
  }

  void final() {
    close();
    Base::final();
  }

  bool listen(ZiIP localIP, uint16_t localPort) {
    if (!this->mx()) return false;
    close();
    m_runtimeDiag = {};
    return m_endpoint.openUDP(
      this->mx(), PathMode::ServerUnconnected,
      ZuMv(localIP), localPort, ZiIP{}, 0,
      Endpoint::DatagramFn{[this](Datagram d) { received_(ZuMv(d)); }},
      Endpoint::ReadyFn{[this](Endpoint *) { ++m_runtimeDiag.endpointReady; }},
      Endpoint::FailFn{[this](bool) { ++m_runtimeDiag.endpointFailures; }});
  }

  void close() {
    m_endpoint.closeUDP();
    resetRuntimeState_();
  }

  bool listening() const { return m_endpoint.listening(); }
  bool connected() const { return m_endpoint.connected(); }
  bool established() const { return m_established; }
  const ZiSockAddr &local() const { return m_endpoint.local(); }
  const RuntimeDiag &runtimeDiag() const { return m_runtimeDiag; }
  const EndpointDiag &endpointDiag() const { return m_endpoint.diag(); }
  const Crypto &crypto() const { return m_crypto; }
  bool sendBidi(ZuCSpan payload, bool fin = true) {
    return sendStream_(1, payload, fin);
  }
  bool sendUni(ZuCSpan payload, bool fin = true) {
    return sendStream_(3, payload, fin);
  }
  bool sendStream(uint64_t streamID, ZuCSpan payload, bool fin = true) {
    return sendStream_(streamID, payload, fin);
  }

private:
  void resetRuntimeState_() {
    m_established = 0;
    m_handshakeStarted = 0;
    m_initialDCID = {};
    m_localSCID = {};
    m_peerCID = {};
    m_peerAddr.null();
    m_transportParams = {};
    for (auto &s : m_txCrypto) s.reset();
    for (auto &s : m_rxCrypto) s.reset();
    memset(m_txPN, 0, sizeof(m_txPN));
    memset(m_rxLargestPN, 0, sizeof(m_rxLargestPN));
    for (auto &a : m_rxPackets) a.clear();
    for (auto &p : m_txPackets) p.clear();
    memset(m_pendingAck, 0, sizeof(m_pendingAck));
    m_handshakeDoneSent = 0;
  }

  bool initRuntimeCrypto_(const LongHeader &h) {
    m_initialDCID = h.dcid;
    m_peerCID = h.scid;
    m_localSCID.set("zqserv01");
    m_transportParams.originalDCID = m_initialDCID;
    m_transportParams.initialSCID = m_localSCID;
    m_transportParams.maxUDPPayloadSize = this->maxUDP();
    m_transportParams.initialMaxData = this->maxData();
    m_transportParams.initialMaxStreamDataBidiLocal = this->maxStreamData();
    m_transportParams.initialMaxStreamDataBidiRemote = this->maxStreamData();
    m_transportParams.initialMaxStreamDataUni = this->maxStreamData();
    m_transportParams.initialMaxStreamsBidi = this->maxStreamsBidi();
    m_transportParams.initialMaxStreamsUni = this->maxStreamsUni();
    if (!m_crypto.deriveInitial(m_initialDCID)) {
      ++m_runtimeDiag.tlsFailures;
      return false;
    }
    if (!m_crypto.initTLS(CryptoConfig{
	true, false, this->firstALPN(), {}, this->certPath(), this->keyPath(),
	{}, &m_transportParams})) {
      ++m_runtimeDiag.tlsFailures;
      return false;
    }
    m_handshakeStarted = 1;
    return true;
  }

  void markEstablished_() {
    if (m_established || !m_crypto.oneRTTReady() ||
	!m_crypto.txTrafficSecretInstalled(CryptoLevel::OneRTT) ||
	!m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT))
      return;
    m_established = 1;
    ++m_runtimeDiag.handshakeComplete;
  }

  bool emitTLS_(size_t inEpoch, ZuCSpan input, ZiSockAddr addr) {
    ZmRef<ZiIOBuf> out =
      new CryptoTxBufAlloc<TLSBufSize, TLSBufSize>{this};
    size_t offsets[5] = {};
    int n = m_crypto.handleTLSMessage(out.ptr(), offsets, inEpoch, input);
    if (n < 0) {
      ++m_runtimeDiag.tlsFailures;
      return false;
    }
    if (!sendCryptoFlights_(out->data(), unsigned(n), offsets, addr))
      return false;
    markEstablished_();
    if (m_established && !m_handshakeDoneSent &&
	!sendHandshakeDone_(addr))
      return false;
    return true;
  }

  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    for (size_t epoch = 0; epoch < 4; ++epoch) {
      if (offsets[epoch + 1] <= offsets[epoch]) continue;
      CryptoLevel::T level;
      if (!cryptoLevelFromEpoch(epoch, level)) continue;
      unsigned off = unsigned(offsets[epoch]);
      unsigned remaining = unsigned(offsets[epoch + 1] - offsets[epoch]);
      while (remaining) {
	unsigned chunk = remaining > RuntimeCryptoChunk ?
	  RuntimeCryptoChunk : remaining;
	uint8_t frame[BufSize];
	uint64_t cryptoOffset = m_txCrypto[level].txOffset();
	int n = m_txCrypto[level].writeFramePrefix(
	  frame, sizeof(frame), chunk);
	if (n < 0) {
	  ++m_runtimeDiag.tlsFailures;
	  return false;
	}
	SentFrameRef ref = SentFrameRef::crypto(cryptoOffset, chunk);
	++m_runtimeDiag.cryptoFramesTx;
	m_runtimeDiag.cryptoBytesTx += chunk;
	if (!sendCryptoPacket_(
	      level, byteSpan(frame, unsigned(n)),
	      byteSpan(data + off, chunk), ref, addr))
	  return false;
	if (level == CryptoLevel::Handshake &&
	    !sendCryptoPacket_(
	      level, byteSpan(frame, unsigned(n)),
	      byteSpan(data + off, chunk), ref, addr))
	  return false;
	off += chunk;
	remaining -= chunk;
      }
    }
    return true;
  }

  bool sendCryptoPacket_(CryptoLevel::T level, ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(level, build, frame)) return false;
    if (level == CryptoLevel::Initial)
      return sendInitialPacket_(build, ZuMv(addr), frame);
    if (level == CryptoLevel::Handshake)
      return sendHandshakePacket_(build, ZuMv(addr), frame);
    return sendShortPacket_(build, ZuMv(addr), frame);
  }

  bool sendCryptoPacket_(
    CryptoLevel::T level, ZuCSpan prefix, ZuCSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(level, build, prefix, payload)) return false;
    if (level == CryptoLevel::Initial)
      return sendInitialPacket_(build, ZuMv(addr), {}, &ref, true);
    if (level == CryptoLevel::Handshake)
      return sendHandshakePacket_(build, ZuMv(addr), {}, &ref, true);
    return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
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
    ++m_runtimeDiag.ackFramesTx;
    return true;
  }

  bool recordRxPacket_(CryptoLevel::T level, uint64_t pn) {
    if (m_rxPackets[level].contains(pn)) return false;
    m_rxPackets[level].add(pn);
    if (pn > m_rxLargestPN[level]) m_rxLargestPN[level] = pn;
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
    if (!frame.ackRangeCount) return;
    m_txPackets[level].ack(frame.ackRanges, frame.ackRangeCount);
  }

  bool buildPayload_(CryptoLevel::T level, PacketBuild &build, ZuCSpan frame) {
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

  bool sendInitialPacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Initial, payload, frame)) return false;
    return sendInitialPacket_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    ZmRef<ZiIOBuf> buf = m_endpoint.allocTxPacket();
    int headerLen = Packet::writeInitial(
      buf->data_(), buf->size, m_peerCID, m_localSCID,
      payload.bytes() + InitialSecret::TagLen, RuntimePNLength);
    if (headerLen < 0 ||
	PacketNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Initial], RuntimePNLength) !=
	int(RuntimePNLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Initial];
    int n = InitialPacketProtection::protectLongV(
      buf->data_(), buf->size, m_crypto.initialKeys().server,
      pn,
      byteSpan(buf->data_(), unsigned(headerLen) + RuntimePNLength),
      payload.data(), payload.count(), unsigned(headerLen), RuntimePNLength);
    if (n < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!m_endpoint.send(ZuMv(buf), ZuMv(addr))) return false;
    if (recordRef)
      recordTxPacket_(
	CryptoLevel::Initial, pn, unsigned(n), *recordRef, ackEliciting);
    else
      recordTxPacket_(CryptoLevel::Initial, pn, unsigned(n), recordFrame);
    ++m_txPN[CryptoLevel::Initial];
    ++m_runtimeDiag.packetsTx;
    ++m_runtimeDiag.initialPacketsTx;
    ++m_runtimeDiag.protectedPacketsTx;
    m_runtimeDiag.bytesTx += unsigned(n);
    return true;
  }

  bool sendHandshakePacket_(ZuCSpan frame, ZiSockAddr addr) {
    PacketBuild payload;
    if (!buildPayload_(CryptoLevel::Handshake, payload, frame)) return false;
    return sendHandshakePacket_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_crypto.txTrafficSecretInstalled(CryptoLevel::Handshake)) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    ZmRef<ZiIOBuf> buf = m_endpoint.allocTxPacket();
    int headerLen = Packet::writeHandshake(
      buf->data_(), buf->size, m_peerCID, m_localSCID,
      payload.bytes() +
	m_crypto.txTrafficSecret(CryptoLevel::Handshake).tagLen,
      RuntimePNLength);
    if (headerLen < 0 ||
	PacketNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[CryptoLevel::Handshake], RuntimePNLength) !=
	int(RuntimePNLength))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::Handshake];
    int n = PacketProtection::protectLongV(
      buf->data_(), buf->size,
      m_crypto.txProtectionState(CryptoLevel::Handshake),
      pn,
      byteSpan(buf->data_(), unsigned(headerLen) + RuntimePNLength),
      payload.data(), payload.count(), unsigned(headerLen), RuntimePNLength);
    if (n < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!m_endpoint.send(ZuMv(buf), ZuMv(addr))) return false;
    if (recordRef)
      recordTxPacket_(
	CryptoLevel::Handshake, pn, unsigned(n), *recordRef, ackEliciting);
    else
      recordTxPacket_(CryptoLevel::Handshake, pn, unsigned(n), recordFrame);
    ++m_txPN[CryptoLevel::Handshake];
    ++m_runtimeDiag.packetsTx;
    ++m_runtimeDiag.handshakePacketsTx;
    ++m_runtimeDiag.protectedPacketsTx;
    m_runtimeDiag.bytesTx += unsigned(n);
    return true;
  }

  bool sendShortPacket_(ZuCSpan payload, ZiSockAddr addr) {
    PacketBuild build;
    if (!buildPayload_(CryptoLevel::OneRTT, build, payload)) return false;
    return sendShortPacket_(build, ZuMv(addr), payload);
  }

  bool sendShortPacket_(
    PacketBuild &payload, ZiSockAddr addr, ZuCSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!m_established &&
	!m_crypto.txTrafficSecretInstalled(CryptoLevel::OneRTT)) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    ZmRef<ZiIOBuf> buf = m_endpoint.allocTxPacket();
    int headerLen = Packet::writeShort(
      buf->data_(), buf->size, m_peerCID, m_txPN[CryptoLevel::OneRTT],
      RuntimePNLength);
    if (headerLen < 0) return false;
    if (!payload.padForProtectionSample(
	  unsigned(headerLen) - RuntimePNLength, RuntimePNLength,
	  m_crypto.txTrafficSecret(CryptoLevel::OneRTT).tagLen))
      return false;
    uint64_t pn = m_txPN[CryptoLevel::OneRTT];
    int n = PacketProtection::protectShortV(
      buf->data_(), buf->size, m_crypto.txProtectionState(CryptoLevel::OneRTT),
      pn,
      byteSpan(buf->data_(), unsigned(headerLen)),
      payload.data(), payload.count(),
      unsigned(headerLen) - RuntimePNLength, RuntimePNLength);
    if (n < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    if (!m_endpoint.send(ZuMv(buf), ZuMv(addr))) return false;
    if (recordRef)
      recordTxPacket_(
	CryptoLevel::OneRTT, pn, unsigned(n), *recordRef, ackEliciting);
    else
      recordTxPacket_(CryptoLevel::OneRTT, pn, unsigned(n), recordFrame);
    ++m_txPN[CryptoLevel::OneRTT];
    ++m_runtimeDiag.packetsTx;
    ++m_runtimeDiag.shortPacketsTx;
    ++m_runtimeDiag.protectedPacketsTx;
    m_runtimeDiag.bytesTx += unsigned(n);
    return true;
  }

  bool sendHandshakeDone_(ZiSockAddr addr) {
    uint8_t frame[8];
    int n = FrameCodec::writeHandshakeDone(frame, sizeof(frame));
    if (n < 0) return false;
    if (!sendShortPacket_(byteSpan(frame, unsigned(n)), ZuMv(addr)))
      return false;
    m_handshakeDoneSent = 1;
    ++m_runtimeDiag.handshakeDoneFramesTx;
    return true;
  }

  bool sendStream_(uint64_t streamID, ZuCSpan payload, bool fin) {
    if (!m_established || !payload.length()) return false;
    if (!sendShortStreamPacket_(streamID, payload, fin, m_peerAddr))
      return false;
    ++m_runtimeDiag.streamFramesTx;
    m_runtimeDiag.streamBytesTx += payload.length();
    return true;
  }

  bool sendShortStreamPacket_(
    uint64_t streamID, ZuCSpan payload, bool fin, ZiSockAddr addr) {
    PacketBuild build;
    build.reset();
    if (!appendPendingAck_(CryptoLevel::OneRTT, build)) return false;
    int n = FrameCodec::writeStreamPrefix(
      build.scratch(), build.scratchAvail(), streamID, 0,
      payload.length(), fin);
    if (n < 0 ||
	!build.commitScratch(unsigned(n)) ||
	!build.add(payload))
      return false;
    SentFrameRef ref;
    ref.kind = SentFrameKind::Stream;
    ref.streamID = streamID;
    ref.length = payload.length();
    ref.fin = fin;
    ref.range = TxRange{nullptr, 0, uint32_t(payload.length()), 0};
    return sendShortPacket_(build, ZuMv(addr), {}, &ref, true);
  }

  void received_(Datagram d) {
    ++m_runtimeDiag.datagramsRx;
    if (!d.buf) {
      ++m_runtimeDiag.packetParseErrors;
      return;
    }
    m_runtimeDiag.bytesRx += d.buf->length;
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
	unsigned packetLen = h.pnOffset + h.length;
	if (packetLen > packet.length() || packetLen < h.payloadOffset) {
	  ok = false;
	  break;
	}
	if (!receivedLong_(d, offset, packetLen)) ok = false;
	offset += packetLen;
	continue;
      }
      if (!receivedShort_(d, offset, d.buf->length - offset)) ok = false;
      break;
    }
    if (!ok) ++m_runtimeDiag.packetParseErrors;
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{
      reinterpret_cast<const char *>(base), packetLen};
    LongHeader h;
    if (Packet::parseLong(packet, h) < 0) return false;
    if (!m_handshakeStarted) {
      m_peerAddr = d.addr;
      if (!initRuntimeCrypto_(h)) return false;
    }
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
	base, packetLen, m_crypto.initialKeys().client,
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    else {
      if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::Handshake)) {
	++m_runtimeDiag.packetProtectionFailures;
	return false;
      }
      plainLen = PacketProtection::unprotectLong(
	base, packetLen,
	m_crypto.rxProtectionState(CryptoLevel::Handshake),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    }
    if (plainLen < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    if (!recordRxPacket_(level, pn)) return true;
    ++m_runtimeDiag.packetsRx;
    ++m_runtimeDiag.protectedPacketsRx;
    if (level == CryptoLevel::Initial) ++m_runtimeDiag.initialPacketsRx;
    else ++m_runtimeDiag.handshakePacketsRx;
    return consumeFrames_(
      level, pn, byteSpan(base + payloadOffset, unsigned(plainLen)),
      d.addr);
  }

  bool receivedShort_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT))
      return false;
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{
      reinterpret_cast<const char *>(base), packetLen};
    ShortHeader h;
    if (Packet::parseShort(
	  packet, m_localSCID.length(), h) < 0 ||
	!(h.dcid == m_localSCID))
      return false;
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = PacketProtection::unprotectShort(
      base, packetLen,
      m_crypto.rxProtectionState(CryptoLevel::OneRTT),
      m_rxLargestPN[CryptoLevel::OneRTT],
      h.pnOffset, pn, payloadOffset);
    if (plainLen < 0) {
      ++m_runtimeDiag.packetProtectionFailures;
      return false;
    }
    if (!recordRxPacket_(CryptoLevel::OneRTT, pn)) return true;
    ++m_runtimeDiag.packetsRx;
    ++m_runtimeDiag.shortPacketsRx;
    ++m_runtimeDiag.protectedPacketsRx;
    return consumeFrames_(
      CryptoLevel::OneRTT,
      pn,
      byteSpan(base + payloadOffset, unsigned(plainLen)), d.addr);
  }

  bool consumeFrames_(
    CryptoLevel::T level, uint64_t pn, ZuCSpan frames, ZiSockAddr addr) {
    unsigned offset = 0;
    while (offset < frames.length()) {
      Frame frame;
      unsigned used = 0;
      if (FrameCodec::parse(
	    ZuCSpan{frames.data() + offset, frames.length() - offset},
	    frame, used) < 0 || !used)
	return false;
      ++m_runtimeDiag.framesRx;
      if (FrameCodec::ackEliciting(frame.type)) noteAck_(level, pn);
      if (frame.type == FrameType::Ping) ++m_runtimeDiag.pingFramesRx;
      if (frame.type == FrameType::Ack) {
	++m_runtimeDiag.ackFramesRx;
	processAckFrame_(level, frame);
      }
      if (frame.type == FrameType::HandshakeDone)
	++m_runtimeDiag.handshakeDoneFramesRx;
      if (frame.type == FrameType::Crypto) {
	ZuCSpan contiguous;
	if (m_rxCrypto[level].receiveFrame(frame, contiguous) < 0)
	  return false;
	++m_runtimeDiag.cryptoFramesRx;
	m_runtimeDiag.cryptoBytesRx += frame.payload.length();
	if (contiguous) {
	  size_t epoch = level == CryptoLevel::Initial ? 0 :
	    level == CryptoLevel::Handshake ? 2 : 3;
	  if (!emitTLS_(epoch, contiguous, ZuMv(addr))) return false;
	}
      } else if (frame.type == FrameType::Stream) {
	++m_runtimeDiag.streamFramesRx;
	m_runtimeDiag.streamBytesRx += frame.payload.length();
	if constexpr (requires(App *a, uint64_t streamID, uint64_t offset,
	      ZuCSpan payload, bool fin) {
	  a->zquicStream(streamID, offset, payload, fin);
	})
	  this->app()->zquicStream(
	    frame.streamID, frame.offset, frame.payload, frame.fin);
      }
      offset += used;
    }
    return true;
  }

  Endpoint	m_endpoint;
  Crypto	m_crypto;
  TransportParams m_transportParams;
  CryptoStream	m_txCrypto[3];
  CryptoStream	m_rxCrypto[3];
  ConnectionID	m_initialDCID;
  ConnectionID	m_localSCID;
  ConnectionID	m_peerCID;
  ZiSockAddr	m_peerAddr;
  RuntimeDiag	m_runtimeDiag;
  uint64_t	m_txPN[3]{};
  uint64_t	m_rxLargestPN[3]{};
  AckTracker	m_rxPackets[3];
  PacketTxSpace	m_txPackets[3];
  bool		m_pendingAck[3]{};
  ZmAtomic<unsigned>	m_handshakeStarted = 0;
  ZmAtomic<unsigned>	m_established = 0;
  ZmAtomic<unsigned>	m_handshakeDoneSent = 0;
};

template <typename Impl, typename TxBufAlloc_>
class Stream :
  public ZmPolymorph,
  public ZmPQRx<
    Stream<Impl, TxBufAlloc_>,
    StreamRxPQueue, ZmPQRxGapIgnore<>>,
  public ZmPQTx<
    Stream<Impl, TxBufAlloc_>,
    TxDataPQueue> {
public:
  using Self = Stream<Impl, TxBufAlloc_>;
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

  Stream(int64_t id) : m_id{id} { }

  int64_t id() const { return m_id; }
  uint64_t txBytes() const { return m_txBytes; }
  uint64_t txBufferedBytes() const { return m_txBufferedBytes; }
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
  bool rxComplete() const {
    return m_rxState.complete() && m_rxDelivered == m_rxState.finalSize();
  }
  unsigned rxQueued() { return m_rx.count_(); }

  RxStream &rxStream() { return m_rx; }

  StreamRxPQueue *rxQueue() { return &m_rxQueue; }
  TxDataPQueue *txQueue() { return &m_txQueue; }

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

  auto txStream() {
    return Zi::txStream(
      unsigned(BufSize), 0, 0,
      [this](unsigned skip) -> ZmRef<ZiIOBuf> {
	ZiAssert(skip <= BufSize, "Zquic", (skip),
	  "invalid stream headroom " << skip, return nullptr);
	ZmRef<ZiIOBuf> buf = new TxDataPQueue::Node{this};
	buf->skip = skip;
	buf->length = 0;
	return buf;
      },
      [](ZmRef<ZiIOBuf> buf) {
	auto stream = static_cast<Stream *>(buf->owner);
	stream->sent_(ZuMv(buf));
      });
  }

  void fin() { m_fin = true; }
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
    if (!receiveFrame(frame, ZuMv(packet), diag)) return -1;
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
      if (diag) {
	++diag->streamRxSliceAllocs;
	++diag->queueNodeAllocs;
      }
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

  void sent_(ZmRef<ZiIOBuf> buf) {
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
  }

  int64_t		m_id;
  uint64_t		m_txBytes = 0;
  uint64_t		m_txBufferedBytes = 0;
  uint64_t		m_rxDelivered = 0;
  uint64_t		m_appError = 0;
  StreamError::T	m_error = StreamError::None;
  bool			m_fin = false;
  bool			m_finDequeued = false;
  bool			m_resetSent = false;
  bool			m_resetReceived = false;
  bool			m_stopSent = false;
  bool			m_stopReceived = false;
  StreamRxState		m_rxState;
  RxStream		m_rx;
  StreamRxPQueue	m_rxQueue{0};
  TxDataPQueue		m_txQueue{0};
};

template <typename Stream_>
inline int64_t Stream_IDAxor(const Stream_ &s) { return s.id(); }

template <typename Stream_>
ZuDerive(Streams_,
  (ZmHash<Stream_,
    ZmHashNode<Stream_,
      ZmHashKey<Stream_IDAxor<Stream_>,
	ZmHashHeapID<"Zquic.Stream.ObjectHash">>>>));

template <typename Link_, typename LinkRef_>
class Cxn : public ZmPolymorph {
public:
  using Link = Link_;
  using LinkRef = LinkRef_;

  Cxn(LinkRef link) : m_link{ZuMv(link)} { }

  Link *link() const { return m_link; }

private:
  LinkRef	m_link = nullptr;
};

template <
  typename App, typename Impl, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_, typename Stream_>
class Link : public ZmPolymorph {
public:
  using TxBufAlloc = TxBufAlloc_;
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  using Streams = Streams_<Stream>;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer = false) :
    m_app{app}, m_isServer{isServer} { }

  App *app() const { return m_app; }
  bool isServer() const { return m_isServer; }
  bool closed() const { return m_closed; }
  uint64_t closeError() const { return m_closeError; }
  uint64_t streamCount() const { return m_streams.count_(); }
  uint64_t peerStreamLimit(StreamType::T type) const {
    return localLimit_(type).limit();
  }
  uint64_t localStreamsOpened(StreamType::T type) const {
    return localLimit_(type).opened();
  }
  uint64_t queuedLocalStreams(StreamType::T type) const {
    return queued_(type);
  }
  uint64_t localStreamLimit(StreamType::T type) const {
    return peerLimit_(type).limit();
  }
  uint64_t peerStreamsOpened(StreamType::T type) const {
    return peerLimit_(type).opened();
  }
  bool localStreamsBlocked(StreamType::T type) const {
    return queued_(type) != 0;
  }

  void setPeerStreamLimit(StreamType::T type, uint64_t limit) {
    localLimit_(type).set(limit);
  }
  void setLocalStreamLimit(StreamType::T type, uint64_t limit) {
    peerLimit_(type).set(limit);
  }

  bool applyMaxStreams(const Frame &frame) {
    if (frame.type != FrameType::MaxStreams) return false;
    localLimit_(frame.streamType).extend(frame.value);
    openQueued_(frame.streamType);
    return true;
  }

  StreamRef stream(StreamType::T type = StreamType::Bidi) {
    if (!localLimit_(type).open()) {
      ++queued_(type);
      return nullptr;
    }
    return openLocalStream_(type);
  }

  StreamRef acceptPeerStream(uint64_t id) {
    if (id > uint64_t(INT64_MAX)) return nullptr;
    if (StreamID::server(id) == m_isServer) return nullptr;
    if (auto stream = findStream(int64_t(id))) return stream;
    StreamType::T type = StreamID::uni(id) ? StreamType::Uni : StreamType::Bidi;
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
    if (frame.type == FrameType::ResetStream)
      return stream->receiveReset(frame) ? 0 : -1;
    return stream->receiveStop(frame) ? 0 : -1;
  }

  int receiveFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    if (frame.type != FrameType::Stream)
      return receiveFrame(frame, diag);
    StreamRef stream = findOrAccept_(frame.streamID);
    if (!stream) return -1;
    return stream->processFrame(frame, ZuMv(packet), diag);
  }

  void close(uint64_t errorCode = 0) {
    m_closeError = errorCode;
    m_closed = true;
  }

private:
  const StreamLimit &localLimit_(StreamType::T type) const {
    return type == StreamType::Uni ? m_peerUniLimit : m_peerBidiLimit;
  }

  StreamLimit &localLimit_(StreamType::T type) {
    return type == StreamType::Uni ? m_peerUniLimit : m_peerBidiLimit;
  }

  const StreamLimit &peerLimit_(StreamType::T type) const {
    return type == StreamType::Uni ? m_localUniLimit : m_localBidiLimit;
  }

  StreamLimit &peerLimit_(StreamType::T type) {
    return type == StreamType::Uni ? m_localUniLimit : m_localBidiLimit;
  }

  const uint64_t &queued_(StreamType::T type) const {
    return type == StreamType::Uni ? m_queuedUni : m_queuedBidi;
  }

  uint64_t &queued_(StreamType::T type) {
    return type == StreamType::Uni ? m_queuedUni : m_queuedBidi;
  }

  StreamRef openLocalStream_(StreamType::T type) {
    return newStream_(nextStreamID_(type));
  }

  unsigned openQueued_(StreamType::T type) {
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

  StreamRef newStream_(int64_t id) {
    auto node = new typename Streams::Node{id};
    StreamRef stream{node};
    m_streams.addNode(node);
    return stream;
  }

  int64_t nextStreamID_(StreamType::T type) {
    uint64_t &ordinal =
      type == StreamType::Uni ? m_nextUniOrdinal : m_nextBidiOrdinal;
    uint64_t id = (ordinal++ << 2) |
      (m_isServer ? 1U : 0U) |
      (type == StreamType::Uni ? 2U : 0U);
    ZiAssert(id <= uint64_t(INT64_MAX), "Zquic", (id),
      "stream ID overflow id=" << id, return INT64_MAX);
    return int64_t(id);
  }

  App		*m_app = nullptr;
  bool		m_isServer = false;
  bool		m_closed = false;
  uint64_t	m_closeError = 0;
  uint64_t	m_nextBidiOrdinal = 0;
  uint64_t	m_nextUniOrdinal = 0;
  StreamLimit	m_peerBidiLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_peerUniLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_localBidiLimit{uint64_t(INT64_MAX) >> 2};
  StreamLimit	m_localUniLimit{uint64_t(INT64_MAX) >> 2};
  uint64_t	m_queuedBidi = 0;
  uint64_t	m_queuedUni = 0;

  Streams	m_streams; // rx thread dedicated, used to dispatch received data
};

} // namespace Zquic

#endif /* Zquic_HH */
