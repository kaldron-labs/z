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

#include <zlib/ZmAtomic.hh>
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
#include <zlib/ZquicSched.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicPacketBuilder.hh>
#include <zlib/ZquicConn.hh>
#include <zlib/ZquicCrypto.hh>
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

class LinkBase : public ZmPolymorph {
public:
  virtual void receivedRouted_(Datagram) { }
  virtual void serverRoutes_(CxnIDRouter &) const { }
  virtual void serverRoutesClosed_(CxnIDRouter &) const { }
};
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
      uint8_t frame[BufSize];
      uint64_t cryptoOffset = txCrypto[level].txOffset();
      int n = txCrypto[level].writeFramePrefix(frame, sizeof(frame), chunk);
      if (n < 0) {
	++diag.failures;
	return false;
      }
      SentFrameRef ref = SentFrameRef::crypto(cryptoOffset, chunk);
      diag.cryptoBytesTx += chunk;
      if (!send(level, byteSpan(frame, unsigned(n)),
	    byteSpan(data + off, chunk), ref, addr))
	return false;
      if (level == CryptoLevel::Handshake &&
	  !send(level, byteSpan(frame, unsigned(n)),
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
  while (offset < payloadLength) {
    Frame frame;
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
  uint64_t	m_maxData = 0;
  uint64_t	m_maxStreamData = 0;
  uint64_t	m_maxStreamsBidi = 0;
  uint64_t	m_maxStreamsUni = 0;
  unsigned	m_maxUDP = MinUDPPayload;
  ErrorFn	m_errorFn;
};

using ClientParams = EngineParams;
using ServerParams = EngineParams;

template <typename App_> class Engine : public ZmPolymorph {
public:
  using App = App_;
template <typename, typename, typename, typename, typename, typename>
friend class Link;
template <typename, typename, typename, typename, typename, typename>
friend class CliLink;
template <typename, typename, typename, typename, typename, typename>
friend class SrvLink;

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

  void connected(const char *alpn, int quicver); // Zquic Rx thread
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

template <typename Owner_, typename OwnerRef_> class Cxn;

template <typename Server_>
using SrvCxn = Cxn<Server_, Server_ *>;

// CRTP - aligned server implementation should conform to this interface:
#if 0
struct App : public Zquic::Server<App> {
  struct Link;
  struct Stream;

  // Optional: create or reject a logical QUIC connection.
  ZmRef<Link> accepted(const Zquic::InitialInfo &);
};

struct App::Stream : public Zquic::SrvStream<Link, Stream> {
  // Zquic Rx thread.
  // >0 consumed progress, 0 leave queued data, <0 reset/close per policy.
  int process(Zquic::RxStream &);
};

struct App::Link : public Zquic::SrvLink<App, Link, App::Stream> {
  Link(App *);

  void connected(const char *alpn, int quicver); // Zquic Rx thread
  void disconnected(); // Zquic Rx thread
  void streamed(ZmRef<Stream>); // Zquic Rx thread
};
#endif
template <typename App_> class Server : public Engine<App_> {
public:
  using App = App_;
  using Base = Engine<App>;
  using ListenerCxn = SrvCxn<Server>;
  using Base::app;
  static constexpr unsigned TLSBufSize = Client<App>::TLSBufSize;
  static constexpr unsigned RuntimePNLength = Client<App>::RuntimePNLength;
  static constexpr unsigned RuntimeCryptoChunk = Client<App>::RuntimeCryptoChunk;

template <typename, typename> friend class Cxn;
template <typename, typename, typename, typename, typename, typename>
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
    m_local.init(localIP, localPort);
    m_remote.null();

    ZiCxnOptions options;
    options.udp(true);

    this->mx()->udp(
      ZiConnectFn{this, [](Server *self, const ZiCxnInfo &ci) -> ZiConnection * {
	return self->newCxn_(ci);
      }},
      ZiFailFn{this, [](Server *self, bool transient) {
	self->failed_0(transient);
      }},
      localIP, localPort, ZiIP{}, 0, options);

    return true;
  }

  void close() {
    m_listening = false;
    clearLinks_();
    ListenerCxn *cxn = m_cxn;
    m_cxn = nullptr;
    if (!cxn) return;
    cxn->detachOwner();
    cxn->close();
  }

  bool listening() const { return m_listening; }
  bool connected() const { return m_cxn; }
  const ZiSockAddr &local() const { return m_local; }
  const EndpointDiag &endpointDiag() const { return m_diag; }

  ZiIP localIP() const { return ZiIP{}; }
  uint16_t localPort() const { return 0; }

  ZmRef<ZiIOBuf> allocTxPacket_() {
    return new PacketTxBufAlloc<>{this};
  }
  bool sendPacket_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (!m_cxn) return false;
    return m_cxn->sendPacket(ZuMv(buf), ZuMv(addr));
  }

private:
  ListenerCxn *newCxn_(const ZiCxnInfo &ci) {
    m_cxn = new ListenerCxn(this, ci);
    return m_cxn;
  }

  void connected_0(ListenerCxn *cxn, ZiIOContext &io) {
    if (cxn != m_cxn) return;
#ifndef _WIN32
    ZiSockAddr local;
    socklen_t len = local.len();
    if (::getsockname(cxn->info().socket, local.sa(), &len) == 0)
      m_local = local;
#endif
    m_listening = true;
    cxn->armRecv_(io);
    if constexpr (requires(App *app_) { app_->listening(); })
      this->app()->listening();
  }

  void disconnected_0(ListenerCxn *cxn, Server *) {
    if (cxn != m_cxn) return;
    m_cxn = nullptr;
    m_listening = false;
  }

  void received_0(Datagram d) {
    ++m_diag.datagramsRx;
    if (d.buf) m_diag.bytesRx += d.buf->length;
    this->rxRun([this, d = ZuMv(d)]() mutable { received_(ZuMv(d)); });
  }

  void sent_0(unsigned bytes) {
    ++m_diag.datagramsTx;
    m_diag.bytesTx += bytes;
  }

  void ioError_0() {
    ++m_diag.failures;
  }

  ZmRef<ZiIOBuf> allocRxPacket_() {
    return new PacketRxBufAlloc<>{this};
  }

  void failed_0(bool transient) {
    ++m_diag.failures;
    if constexpr (requires(App *app_, bool transient_) {
      app_->listenFailed(transient_);
    })
      this->app()->listenFailed(transient);
    else
      this->error_(ZeEXCEPT(Error, "Zquic",
	([transient](auto &s) {
	  s << "QUIC server listen failed transient=" << transient;
	})));
  }

  void received_(Datagram d) {
    LinkBase *link = route_(d);
    if (!link) {
      ++m_diag.failures;
      return;
    }
    link->receivedRouted_(ZuMv(d));
    link->serverRoutes_(m_routes);
  }

  LinkBase *route_(const Datagram &d) {
    if (!d.buf || !d.buf->length) return nullptr;
    ZuCSpan packet{
      reinterpret_cast<const char *>(d.buf->data_()), d.buf->length};
    if (Packet::isLong(packet)) return routeLong_(d, packet);
    return routeShort_(d, packet);
  }

  LinkBase *routeLong_(const Datagram &d, ZuCSpan packet) {
    LongHeader h;
    if (Packet::parseLong(packet, h) < 0) return nullptr;
    if (!VersionNegotiation::supported(h.version)) {
      sendVersionNegotiation_(h, d.addr);
      return nullptr;
    }
    if (uintptr_t token = m_routes.find(h.dcid))
      return link_(token);
    if (h.type != PacketType::Initial) return nullptr;
    return accept_(InitialInfo{h, d.addr, d.buf->length});
  }

  LinkBase *routeShort_(const Datagram &, ZuCSpan packet) {
    ShortHeader h;
    if (Packet::parseShort(packet, CxnIDGen::InitialLength, h) < 0)
      return nullptr;
    return link_(m_routes.find(h.dcid));
  }

  LinkBase *accept_(const InitialInfo &info) {
    ZmRef<LinkBase> link;
    if constexpr (requires(App *app_, const InitialInfo &info_) {
      app_->accepted(info_);
    })
      link = this->app()->accepted(info);
    else {
      this->error_(ZeEXCEPT(Error, "Zquic",
	"QUIC server App must provide accepted(const InitialInfo &)"));
      return nullptr;
    }
    if (!link) return nullptr;
    LinkBase *ptr = link.ptr();
    if (!addLink_(ZuMv(link))) return nullptr;
    return ptr;
  }

  bool addLink_(ZmRef<LinkBase> link) {
    if (!link) return false;
    for (unsigned i = 0; i < CxnIDRouter::Max; ++i)
      if (m_links[i].ptr() == link.ptr()) return true;
    for (unsigned i = 0; i < CxnIDRouter::Max; ++i) {
      if (m_links[i]) continue;
      m_links[i] = ZuMv(link);
      return true;
    }
    this->error_(ZeEXCEPT(Error, "Zquic",
      "QUIC server active SrvLink table is full"));
    return false;
  }

  void releaseLink_(LinkBase *link) {
    if (!link) return;
    link->serverRoutesClosed_(m_routes);
    for (unsigned i = 0; i < CxnIDRouter::Max; ++i) {
      if (m_links[i].ptr() != link) continue;
      m_links[i] = nullptr;
      return;
    }
  }

  LinkBase *link_(uintptr_t token) const {
    if (!token) return nullptr;
    auto ptr = reinterpret_cast<LinkBase *>(token);
    for (unsigned i = 0; i < CxnIDRouter::Max; ++i)
      if (m_links[i].ptr() == ptr) return ptr;
    return nullptr;
  }

  void clearLinks_() {
    m_routes = {};
    for (unsigned i = 0; i < CxnIDRouter::Max; ++i)
      m_links[i] = nullptr;
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

  ListenerCxn		*m_cxn = nullptr;
  ZmRef<LinkBase>	m_links[CxnIDRouter::Max];
  CxnIDRouter		m_routes;
  ZiSockAddr		m_local;
  ZiSockAddr		m_remote;
  ZmAtomic<unsigned>	m_listening = 0;
  EndpointDiag		m_diag;
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
      if constexpr (AppThread)
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
    if constexpr (requires(Link *link) { link->app()->txInvoked(); }) {
      if (ZuUnlikely(!m_link || !m_link->app() || !m_link->app()->mx()))
	return true;
      return m_link->app()->txInvoked();
    } else {
      return true;
    }
  }

  template <typename Fn>
  void txInvoke_(Fn &&fn) {
    if constexpr (requires(Link *link, Fn fn_) {
      link->app()->txInvoke(ZuMv(fn_));
    }) {
      if (ZuLikely(m_link && m_link->app() && m_link->app()->mx())) {
	m_link->app()->txInvoke(ZuFwd<Fn>(fn));
	return;
      }
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

template <typename Owner_, typename OwnerRef_>
class Cxn : public ZiConnection {
public:
  using Owner = Owner_;
  using OwnerRef = OwnerRef_;
  using Link = Owner;
  using LinkRef = OwnerRef;
  static constexpr unsigned MaxTxQueue = 32;

  Cxn(OwnerRef owner, const ZiCxnInfo &ci) :
    ZiConnection(mx_(owner), ci), m_owner{ZuMv(owner)} { }

  Owner *owner() const { return m_owner; }
  Owner *link() const { return owner(); }

  void connected(ZiIOContext &io) override {
    if (auto owner = this->owner()) owner->connected_0(this, io);
  }
  void disconnected() override {
    OwnerRef ownerRef = ZuMv(m_owner);
    m_owner = nullptr;
    if (Owner *owner = ownerRef)
      owner->disconnected_0(this, ZuMv(ownerRef));
  }

  void detachOwner() { m_owner = nullptr; }

  ZmRef<ZiIOBuf> allocTxPacket() {
    if (auto owner = this->owner()) {
      if constexpr (requires(Owner *owner_) { owner_->allocTxPacket_(); })
	return owner->allocTxPacket_();
    }
    return new PacketTxBufAlloc<>{this};
  }

  bool sendPacket(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (!buf) return false;
    if (m_txBuf) return enqueueTx_(ZuMv(buf), ZuMv(addr));
    m_txBuf = ZuMv(buf);
    m_txAddr = ZuMv(addr);
    send(ZiIOFn{this, ZmFnPtr<&Cxn::sendStart_>{}});
    return true;
  }

  void armRecv_(ZiIOContext &io) {
    m_rxBuf = allocRxPacket_();
    io.init(
      ZiIOFn{this, ZmFnPtr<&Cxn::recvDone_>{}},
      m_rxBuf->data_(), m_rxBuf->size, 0);
  }

private:
  static ZiMultiplex *mx_(const OwnerRef &owner) {
    Owner *ptr = owner;
    if constexpr (requires(Owner *owner_) { owner_->mx(); })
      return ptr->mx();
    else
      return ptr->app()->mx();
  }

  ZmRef<ZiIOBuf> allocRxPacket_() {
    if (auto owner = this->owner()) {
      if constexpr (requires(Owner *owner_) { owner_->allocRxPacket_(); })
	return owner->allocRxPacket_();
    }
    return new PacketRxBufAlloc<>{this};
  }

  bool enqueueTx_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (m_txQueueCount >= MaxTxQueue) return false;
    unsigned i = m_txQueueTail;
    m_txQueueBuf[i] = ZuMv(buf);
    m_txQueueAddr[i] = ZuMv(addr);
    m_txQueueTail = (m_txQueueTail + 1) % MaxTxQueue;
    ++m_txQueueCount;
    return true;
  }

  bool dequeueTx_() {
    if (!m_txQueueCount) return false;
    unsigned i = m_txQueueHead;
    m_txBuf = ZuMv(m_txQueueBuf[i]);
    m_txAddr = ZuMv(m_txQueueAddr[i]);
    m_txQueueHead = (m_txQueueHead + 1) % MaxTxQueue;
    --m_txQueueCount;
    return true;
  }

  bool recvDone_(ZiIOContext &io) {
    if (io.length < 0) {
      if (auto owner = this->owner()) {
	if constexpr (requires(Owner *owner_) { owner_->ioError_0(); })
	  owner->ioError_0();
      }
      io.disconnect();
      return true;
    }
    if (io.length > 0 && m_rxBuf) {
      m_rxBuf->skip = 0;
      m_rxBuf->length = unsigned(io.length);
      auto buf = ZuMv(m_rxBuf);
      if (auto owner = this->owner())
	owner->received_0(Datagram{ZuMv(buf), io.addr});
    }
    armRecv_(io);
    return true;
  }

  bool sendStart_(ZiIOContext &io) {
    if (!m_txBuf) {
      io.complete();
      return true;
    }
    io.init(
      ZiIOFn{this, ZmFnPtr<&Cxn::sendDone_>{}},
      m_txBuf->data(), m_txBuf->length, 0, m_txAddr);
    return true;
  }

  bool sendDone_(ZiIOContext &io) {
    if (io.length < 0) {
      if (auto owner = this->owner()) {
	if constexpr (requires(Owner *owner_) { owner_->ioError_0(); })
	  owner->ioError_0();
      }
      m_txBuf = nullptr;
      io.complete();
      return true;
    }
    if ((io.offset += io.length) < io.size) return true;
    if (auto owner = this->owner()) {
      if constexpr (requires(Owner *owner_, unsigned bytes) {
	owner_->sent_0(bytes);
      })
	owner->sent_0(io.size);
    }
    m_txBuf = nullptr;
    if (dequeueTx_()) {
      io.init(
	ZiIOFn{this, ZmFnPtr<&Cxn::sendDone_>{}},
	m_txBuf->data(), m_txBuf->length, 0, m_txAddr);
      return true;
    }
    io.complete();
    return true;
  }

  OwnerRef		m_owner = nullptr;
  ZmRef<ZiIOBuf>	m_rxBuf;
  ZmRef<ZiIOBuf>	m_txBuf;
  ZiSockAddr		m_txAddr;
  ZmRef<ZiIOBuf>	m_txQueueBuf[MaxTxQueue];
  ZiSockAddr		m_txQueueAddr[MaxTxQueue];
  unsigned		m_txQueueHead = 0;
  unsigned		m_txQueueTail = 0;
  unsigned		m_txQueueCount = 0;
};

template <typename Link>
using CliCxn = Cxn<Link, Link *>;

template <
  typename App, typename Impl, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_, typename Stream_>
class Link : public LinkBase {
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

  unsigned scheduledStreamCount_() const {
    return m_streamScheduler.count();
  }

  bool runtimeEstablished_() const { return m_established; }
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
    m_transportParams = {};
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

  void setRuntimeCIDs_(
    const CxnID &initialDCID,
    const CxnID &localSCID,
    const CxnID &peerCID) {
    m_initialDCID = initialDCID;
    m_localSCID = localSCID;
    m_peerCID = peerCID;
  }

  void setPeerCIDFromHeaderSCID_(const LongHeader &h) {
    if (h.scid.length()) m_peerCID = h.scid;
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

  bool validateServerTransportParams_(const ClientBootstrap &bootstrap) const {
    return m_crypto.peerTransportParamsReceived() &&
      bootstrap.validateServerTransportParams(
	m_crypto.peerTransportParams(), m_peerCID);
  }

  void establishRuntime_() {
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

  template <typename AppendAck, typename SendPacket>
  bool sendQueuedStreamPacket_(
    StreamRef stream, ZiSockAddr addr, AppendAck appendAck,
    SendPacket sendPacket) {
    PacketBuild build;
    build.reset();
    PacketBudget budget;
    budget.pmtu = budget.congestion = budget.antiAmplification =
      app()->maxUDP();
    PacketAssembly assembly;
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
    SentFrameRef ref;
    ref.kind = SentFrameKind::Stream;
    ref.streamID = info.streamID;
    ref.offset = info.offset;
    ref.length = info.length;
    ref.fin = info.fin;
    ref.range = TxRange{nullptr, 0, uint32_t(info.length), info.offset};
    if (!sendPacket(build, ZuMv(addr), ref)) return false;
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
    if (!frame.ackRangeCount) return;
    m_txPackets[level].ack(frame.ackRanges, frame.ackRangeCount);
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
    ZmRef<ZiIOBuf> buf = allocTxPacket();
    int headerLen = Packet::writeShort(
      buf->data_(), buf->size, runtimeCID_(dcid),
      m_txPN[CryptoLevel::OneRTT], pnLength);
    if (headerLen < 0) return false;
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
	unsigned packetLen = h.pnOffset + h.length;
	if (packetLen > packet.length() || packetLen < h.payloadOffset) {
	  ok = false;
	  break;
	}
	if (!receiveLong(d, offset, packetLen)) ok = false;
	offset += packetLen;
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
    if (!m_crypto.rxTrafficSecretInstalled(CryptoLevel::OneRTT))
      return false;
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuCSpan packet{
      reinterpret_cast<const char *>(base), packetLen};
    ShortHeader h;
    if (Packet::parseShort(packet, m_localSCID.length(), h) < 0 ||
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
      ++m_diag.failures;
      return false;
    }
    if (!recordRxPacket_(CryptoLevel::OneRTT, pn))
      return true;
    ++m_diag.packetsRx;
    return consumeFrames(
      CryptoLevel::OneRTT, pn,
      byteSpan(base + payloadOffset, unsigned(plainLen)), d.addr, d.buf);
  }

  template <typename EmitTLS>
  bool consumeProtectedFrames_(
    CryptoLevel::T level, uint64_t pn, ZuCSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, bool countHandshakeDone,
    EmitTLS emitTLS) {
    unsigned offset = 0;
    while (offset < frames.length()) {
      Frame frame;
      unsigned used = 0;
      if (FrameCodec::parse(
	    ZuCSpan{frames.data() + offset, frames.length() - offset},
	    frame, used) < 0 || !used)
	return false;
      ++m_diag.framesRx;
      if (FrameCodec::ackEliciting(frame.type))
	noteAck_(level, pn);
      if (frame.type == FrameType::Ack) {
	processAckFrame_(level, frame);
      }
      if (countHandshakeDone && frame.type == FrameType::HandshakeDone)
	;
      if (frame.type == FrameType::Crypto) {
	ZuCSpan contiguous;
	if (m_rxCrypto[level].receiveFrame(frame, contiguous) < 0)
	  return false;
	m_diag.cryptoBytesRx += frame.payload.length();
	if (contiguous) {
	  size_t epoch = level == CryptoLevel::Initial ? 0 :
	    level == CryptoLevel::Handshake ? 2 : 3;
	  if (!emitTLS(epoch, contiguous, addr)) return false;
	}
      } else if (frame.type == FrameType::Stream) {
	m_diag.streamBytesRx += frame.payload.length();
	if (receiveFrame(frame, ZmRef<ZiIOBuf>{packetBuf}) < 0)
	  return false;
	if constexpr (requires(Impl *impl_, uint64_t streamID,
	      uint64_t offset_, ZuCSpan payload, bool fin) {
	  impl_->streamFrame(streamID, offset_, payload, fin);
	})
	  impl()->streamFrame(
	    frame.streamID, frame.offset, frame.payload, frame.fin);
      } else if (frame.type == FrameType::ResetStream ||
	  frame.type == FrameType::StopSending) {
	if (receiveFrame(frame) < 0) return false;
      }
      offset += used;
    }
    return true;
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
    auto node = new typename Streams::Node{impl(), id};
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
  StreamScheduler m_streamScheduler;

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
  uint64_t		m_txPN[3]{};
  uint64_t		m_rxLargestPN[3]{};
  AckTracker		m_rxPackets[3];
  PacketTxSpace		m_txPackets[3];
  bool			m_pendingAck[3]{};
  unsigned		m_handshakeStarted = 0;
  unsigned		m_established = 0;
  LinkState::T		m_linkState = LinkState::Starting;
  CloseState::T		m_runtimeCloseState = CloseState::Open;
  uint64_t		m_runtimeCloseError = 0;
  unsigned		m_drainPTOs = 0;
};

template <
  typename App, typename Impl, typename Stream_,
  typename TxBufAlloc_ = StreamTxBufAlloc<>,
  typename Cxn_ = CliCxn<Impl>,
  typename CxnRef_ = ZmRef<Cxn_>>
class CliLink :
  public Link<App, Impl, TxBufAlloc_, Cxn_, CxnRef_, Stream_> {
public:
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using Base = Link<App, Impl, TxBufAlloc_, Cxn_, CxnRef_, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  static constexpr unsigned TLSBufSize = 64 * 1024;
  static constexpr unsigned RuntimePNLength = 2;
  static constexpr unsigned RuntimeCryptoChunk = 900;
  using Base::Base;
  using Base::app;
  using Base::impl;

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
  Cxn *cxn() const { return m_cxn; }
  const ZiSockAddr &local() const { return m_local; }
  const ZiSockAddr &remote() const { return m_remote; }
  const EndpointDiag &cxnDiag() const { return m_cxnDiag; }
  const RuntimeDiag &runtimeDiag() const { return Base::runtimeDiag_(); }
  const Crypto &crypto() const { return Base::crypto_(); }

  bool send(StreamRef stream, ZuCSpan payload, bool fin = true) {
    if (!stream || (!payload.length() && !fin))
      return false;
    if (app()->txInvoked()) return send_(ZuMv(stream), payload, fin);
    ZtBytes payload_;
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
	ZuCSpan{
	  reinterpret_cast<const char *>(payload.data()),
	  payload.length()},
	fin);
    });
    return true;
  }
  bool send_(StreamRef stream, ZuCSpan payload, bool fin = true) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client send_ outside Tx thread", return false);
    if (Base::closed() || !stream || !Base::runtimeEstablished_() ||
	(!payload.length() && !fin))
      return false;
    if (fin) stream->fin();
    if (payload.length()) {
      auto tx = stream->txStream_();
      tx.append(
	reinterpret_cast<const uint8_t *>(payload.data()), payload.length());
      tx.flush();
    }
    return Base::flushWritableStreams_(
      m_remote,
      [this](StreamRef stream, ZiSockAddr addr) {
	return sendQueuedStreamPacket_(stream, ZuMv(addr));
      });
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

    ZiCxnOptions options;
    options.udp(true);

    app()->mx()->udp(
      ZiConnectFn{[link = ZmMkRef(impl())](
	  const ZiCxnInfo &ci) -> ZiConnection * {
	return link->newCxn_(ci);
      }},
      ZiFailFn{[link = ZmMkRef(impl())](bool transient) {
	link->connectFailed_0(transient);
      }},
      ZiIP{}, 0, ip, m_port, options);
  }

  void connected_0(Cxn *cxn, ZiIOContext &io) {
    cxn->armRecv_(io);
    app()->rxRun([link = ZmMkRef(impl()), cxn = ZmMkRef(cxn)]() mutable {
      link->connected_(ZuMv(cxn));
    });
  }

  template <typename OwnerRef>
  void disconnected_0(Cxn *cxn, OwnerRef) {
    app()->rxRun([link = ZmMkRef(impl()), cxn = ZmMkRef(cxn)]() mutable {
      link->disconnected_(cxn.ptr());
    });
  }

  void received_0(Datagram d) {
    ++m_cxnDiag.datagramsRx;
    if (d.buf) m_cxnDiag.bytesRx += d.buf->length;
    app()->rxRun([link = ZmMkRef(impl()), d = ZuMv(d)]() mutable {
      link->received_(ZuMv(d));
    });
  }

  void sent_0(unsigned bytes) {
    ++m_cxnDiag.datagramsTx;
    m_cxnDiag.bytesTx += bytes;
  }

  void ioError_0() {
    ++m_cxnDiag.failures;
  }

  ZmRef<ZiIOBuf> allocRxPacket_() {
    return new PacketRxBufAlloc<>{this};
  }

  ZmRef<ZiIOBuf> allocTxPacket_() {
    return new PacketTxBufAlloc<>{this};
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

  Cxn *newCxn_(const ZiCxnInfo &ci) {
    return new Cxn(impl(), ci);
  }

  void closeCurrent_(bool notify) {
    m_udpReady = 0;
    auto cxn = ZmRef<Cxn>{ZuMv(m_cxn)};
    m_cxn = nullptr;
    if (!cxn) return;
    if (notify) {
      m_closingCxn = cxn.ptr();
      m_disconnectRef = ZmMkRef(impl());
    } else
      cxn->detachOwner();
    cxn->close();
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
	"localhost"}))
      return false;
    return true;
  }

  bool startHandshake_() {
    if (Base::runtimeHandshakeStarted_()) return true;
    resetRuntimeState_();
    if (!initRuntimeCrypto_()) return false;
    if (!Base::startRuntimeHandshake_()) return false;
    return emitTLS_(0, {}, m_remote);
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
    auto alpn = Base::negotiatedProtocol_();
    if constexpr (requires(Impl *impl_, const char *alpn_, int ver_) {
      impl_->connected(alpn_, ver_);
    })
      impl()->connected(alpn.data(), int(Version1));
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
    if (!m_cxn) return false;
    return Base::sendProtectedInitialPacket_(
      InitialKeyDir::Client, RuntimeCID::Initial, RuntimeCID::Local,
      RuntimePNLength, true, payload, ZuMv(addr), recordFrame,
      recordRef, ackEliciting,
      [this]() { return m_cxn->allocTxPacket(); },
      [this](auto buf, ZiSockAddr addr_) {
	return m_cxn->sendPacket(ZuMv(buf), ZuMv(addr_));
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
    if (!m_cxn) return false;
    return Base::sendProtectedHandshakePacket_(
      RuntimeCID::Peer, RuntimeCID::Local, RuntimePNLength,
      payload, ZuMv(addr), recordFrame, recordRef,
      ackEliciting,
      [this]() { return m_cxn->allocTxPacket(); },
      [this](auto buf, ZiSockAddr addr_) {
	return m_cxn->sendPacket(ZuMv(buf), ZuMv(addr_));
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
    if (!m_cxn) return false;
    return Base::sendProtectedShortPacket_(
      RuntimeCID::Peer, RuntimePNLength, payload, ZuMv(addr), recordFrame,
      recordRef, ackEliciting,
      [this]() { return m_cxn->allocTxPacket(); },
      [this](auto buf, ZiSockAddr addr_) {
	return m_cxn->sendPacket(ZuMv(buf), ZuMv(addr_));
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

  void received_(Datagram d) {
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong_(d_, packetOffset, packetLen);
      },
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort_(d_, packetOffset, packetLen);
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
      level, pn, frames, ZuMv(addr), packetBuf, true,
      [this](size_t epoch, ZuCSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      });
  }

  void connected_(ZmRef<Cxn> cxn) {
    if (m_cxn == cxn.ptr()) return;
    if (m_cxn) {
      auto old = ZmRef<Cxn>{ZuMv(m_cxn)};
      old->close();
    }
    m_cxn = ZuMv(cxn);
    m_local.init(m_cxn->info().localIP, m_cxn->info().localPort);
    m_remote.init(m_cxn->info().remoteIP, m_cxn->info().remotePort);
#ifndef _WIN32
    {
      ZiSockAddr local;
      socklen_t len = local.len();
      if (::getsockname(m_cxn->info().socket, local.sa(), &len) == 0)
	m_local = local;
    }
#endif
    m_udpReady = 1;
    ++m_udpReadyCount;
    Base::endpointReady_();
    startHandshake_();
  }

  void disconnected_(Cxn *cxn) {
    bool closing = m_closingCxn == cxn;
    if (m_cxn != cxn && !closing) return;
    if (m_cxn == cxn) m_cxn = nullptr;
    if (closing) m_closingCxn = nullptr;
    m_udpReady = 0;
    if constexpr (requires(Impl *impl_) { impl_->disconnected(); })
      impl()->disconnected();
    m_disconnectRef = nullptr;
    cxn->mx()->txRun([cxn = ZmMkRef(cxn)]() { });
  }

  void connectFailed_0(bool transient) {
    ++m_cxnDiag.failures;
    Base::endpointFailure_();
    if (!app() || !app()->mx()) {
      impl()->connectFailed(transient);
      return;
    }
    app()->rxRun([link = ZmMkRef(impl()), transient]() {
      link->connectFailed(transient);
    });
  }

  CxnRef		m_cxn;
  Cxn			*m_closingCxn = nullptr;
  ZmRef<Impl>		m_disconnectRef;
  Host			m_server;
  uint16_t		m_port = 0;
  ZiSockAddr		m_local;
  ZiSockAddr		m_remote;
  EndpointDiag		m_cxnDiag;
  ClientBootstrap	m_bootstrap;
  bool			m_peerParamsValidated = false;
  ZmAtomic<uint64_t>	m_udpReadyCount = 0;
  ZmAtomic<unsigned>	m_udpReady = 0;
};

template <
  typename App, typename Impl, typename Stream_,
  typename TxBufAlloc_ = StreamTxBufAlloc<>,
  typename Cxn_ = SrvCxn<App>,
  typename CxnRef_ = Cxn_ *>
class SrvLink :
  public Link<App, Impl, TxBufAlloc_, Cxn_, CxnRef_, Stream_> {
public:
  using Cxn = Cxn_;
  using CxnRef = CxnRef_;
  using Base = Link<App, Impl, TxBufAlloc_, Cxn_, CxnRef_, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  static constexpr unsigned TLSBufSize = 64 * 1024;
  static constexpr unsigned RuntimePNLength = 2;
  static constexpr unsigned RuntimeCryptoChunk = 900;
  using Base::Base;
  using Base::app;
  using Base::impl;

template <typename> friend class Server;

  SrvLink(App *app) : Base{app, true} { }

  bool established() const { return Base::runtimeEstablished_(); }
  const RuntimeDiag &runtimeDiag() const { return Base::runtimeDiag_(); }
  const Crypto &crypto() const { return Base::crypto_(); }
  const ZiSockAddr &peer() const { return m_peerAddr; }

  bool send(StreamRef stream, ZuCSpan payload, bool fin = true) {
    if (!stream || (!payload.length() && !fin))
      return false;
    if (app()->txInvoked()) return send_(ZuMv(stream), payload, fin);
    ZtBytes payload_;
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
	ZuCSpan{
	  reinterpret_cast<const char *>(payload.data()),
	  payload.length()},
	fin);
    });
    return true;
  }
  bool send_(StreamRef stream, ZuCSpan payload, bool fin = true) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server send_ outside Tx thread", return false);
    if (Base::closed() || !stream || !Base::runtimeEstablished_() ||
	(!payload.length() && !fin))
      return false;
    if (fin) stream->fin();
    if (payload.length()) {
      auto tx = stream->txStream_();
      tx.append(
	reinterpret_cast<const uint8_t *>(payload.data()), payload.length());
      tx.flush();
    }
    return Base::flushWritableStreams_(
      m_peerAddr,
      [this](StreamRef stream, ZiSockAddr addr) {
	return sendQueuedStreamPacket_(stream, ZuMv(addr));
      });
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
      static_cast<Server<App> *>(app())->releaseLink_(
	static_cast<LinkBase *>(this));
  }

  void resetRuntimeState_() {
    Base::resetRuntime_();
    m_peerAddr.null();
    m_bootstrap = {};
    m_handshakeDoneSent = 0;
  }

  bool initRuntimeCrypto_(const LongHeader &h, unsigned datagramLen) {
    if (h.type != PacketType::Initial ||
	!m_bootstrap.acceptInitial(
	  h, datagramLen, uintptr_t(static_cast<LinkBase *>(this)))) {
      Base::packetParseFailure_();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.originalDCID(), m_bootstrap.localInitialSCID(),
      m_bootstrap.clientInitialSCID());
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
    Base::establishRuntime_();
    auto alpn = Base::negotiatedProtocol_();
    if constexpr (requires(Impl *impl_, const char *alpn_, int ver_) {
      impl_->connected(alpn_, ver_);
    })
      impl()->connected(alpn.data(), int(Version1));
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
      [this](auto buf, ZiSockAddr addr_) {
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
      [this](auto buf, ZiSockAddr addr_) {
	return app()->sendPacket_(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendHandshakeDone_(ZiSockAddr addr) {
    uint8_t frame[8];
    int n = FrameCodec::writeHandshakeDone(frame, sizeof(frame));
    if (n < 0) return false;
    if (!sendShortPacket_(byteSpan(frame, unsigned(n)), ZuMv(addr)))
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

  void received_(Datagram d) {
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong_(d_, packetOffset, packetLen);
      },
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort_(d_, packetOffset, packetLen);
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
      level, pn, frames, ZuMv(addr), packetBuf, false,
      [this](size_t epoch, ZuCSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      });
  }

  void receivedRouted_(Datagram d) override {
    received_(ZuMv(d));
  }

  void serverRoutes_(CxnIDRouter &routes) const override {
    routes.add(m_bootstrap.initialDCIDs());
    routes.add(m_bootstrap.localCIDs());
  }

  void serverRoutesClosed_(CxnIDRouter &routes) const override {
    m_bootstrap.initialDCIDs().all([&routes](const CxnIDSlot &slot) {
      routes.tombstone(slot.cid);
    });
    m_bootstrap.localCIDs().all([&routes](const CxnIDSlot &slot) {
      routes.retire(slot.cid);
    });
  }

  ServerBootstrap	m_bootstrap;
  ZiSockAddr		m_peerAddr;
  ZmAtomic<unsigned>	m_handshakeDoneSent = 0;
};

} // namespace Zquic

#endif /* Zquic_HH */
