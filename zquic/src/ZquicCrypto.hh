//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC crypto integration shell

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <string.h>

#include <zlib/ZmFn.hh>
#include <zlib/ZiRxStream.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>

#include <zlib/ZtlsCxn.hh>
#include <zlib/ZtlsCredentials.hh>

namespace Zquic {

template <unsigned Max>
class SecretBytes {
public:
  unsigned length() const { return m_data.length(); }
  bool length(unsigned n) {
    if (n > Max) return false;
    m_data.length(n);
    return true;
  }
  uint8_t *data() { return m_data.data(); }
  const uint8_t *data() const { return m_data.data(); }
  uint8_t &operator [](unsigned i) { return m_data[i]; }
  const uint8_t &operator [](unsigned i) const { return m_data[i]; }
  ZuBSpan bspan() const { return m_data.cspan(); }
  void clear() {
    memset(m_data.data(), 0, Max);
    m_data.length(0);
  }

private:
  ZuBArray<Max>	m_data;
};

struct InitialSecret {
  enum { SecretLen = 32 };
  enum { KeyLen = 16 };
  enum { IVLen = 12 };
  enum { TagLen = 16 };
  enum { HPMaskLen = 5 };


  ZuBArray<SecretLen>	secret = ZuBArray<SecretLen>(SecretLen, true);
  ZuBArray<KeyLen>	key = ZuBArray<KeyLen>(KeyLen, true);
  ZuBArray<IVLen>	iv = ZuBArray<IVLen>(IVLen, true);
  ZuBArray<KeyLen>	hp = ZuBArray<KeyLen>(KeyLen, true);

  void clear() {
    memset(secret.data(), 0, secret.length());
    memset(key.data(), 0, key.length());
    memset(iv.data(), 0, iv.length());
    memset(hp.data(), 0, hp.length());
  }
};

struct InitialKeyMaterial {
  using InitialBytes = ZuBArray<InitialSecret::SecretLen>;

  InitialBytes		initial = InitialBytes(InitialSecret::SecretLen, true);
  InitialSecret		client;
  InitialSecret		server;

  void clear() {
    memset(initial.data(), 0, initial.length());
    client.clear();
    server.clear();
  }
};

struct ZquicAPI InitialCrypto {
  static bool derive(InitialKeyMaterial &, const CxnID &);
  static int encrypt(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuBSpan, ZuBSpan);
  static int encryptV(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuBSpan,
    const Ztls_::Bytes *, unsigned);
  static int encryptV_(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuBSpan,
    const Ztls_::Bytes *, unsigned, unsigned);
  static int decrypt(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuBSpan, ZuBSpan);
  static bool headerMask(
    uint8_t *, unsigned, const InitialSecret &, ZuBSpan);
};

struct ZquicAPI InitialPktProt {
  static int protectLong(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    ZuBSpan, ZuBSpan, unsigned, unsigned);
  static int protectLongV(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    ZuBSpan, const Ztls_::Bytes *, unsigned, unsigned, unsigned);
  static int protectLongV_(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    ZuBSpan, const Ztls_::Bytes *, unsigned, unsigned, unsigned, unsigned);
  static int unprotectLong(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    unsigned, uint64_t &, unsigned &);
};

struct ZquicAPI TrafficSecret {
  enum { MaxSecretLen = 64 };
  enum { MaxKeyLen = 32 };
  enum { MaxIVLen = 16 };
  enum { MaxHPLen = 32 };

  TrafficSecret() { clear(); }

  void clear();
  bool valid() const { return installed; }

  SecretBytes<MaxSecretLen>	secret;
  SecretBytes<MaxKeyLen>	key;
  SecretBytes<MaxIVLen>		iv;
  SecretBytes<MaxHPLen>		hp;
  unsigned			tagLen = 0;
  const Ztls_::AeadAlgo		*aead = nullptr;
  const Ztls_::HashAlgo		*hash = nullptr;
  const Ztls_::CipherAlgo	*hpCipher = nullptr;
  bool				installed = false;
};

struct ZquicAPI PktProtState {
  PktProtState() = default;
  ~PktProtState() { clear(); }
  PktProtState(const PktProtState &) = delete;
  PktProtState &operator =(const PktProtState &) = delete;

  bool init(const TrafficSecret &, PktNumSpace::T, bool);
  void clear();
  bool valid() const { return installed; }

  Ztls_::AeadContext		*aead = nullptr;
  Ztls_::CipherContext		*hp = nullptr;
  TrafficSecret			secret;
  PktNumSpace::T		level = PktNumSpace::Initial;
  bool				tx = false;
  bool				installed = false;
};

struct ZquicAPI PktProt {
  static bool deriveSecret(
    TrafficSecret &, const Ztls_::CipherSuite *, ZuBSpan);
  static int protectLongV(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    ZuBSpan, const Ztls_::Bytes *, unsigned, unsigned, unsigned);
  static int protectLongV_(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    ZuBSpan, const Ztls_::Bytes *, unsigned, unsigned, unsigned, unsigned);
  static int protectLong(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    ZuBSpan, ZuBSpan, unsigned, unsigned);
  static int protectLong(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    ZuBSpan, ZuBSpan, unsigned, unsigned);
  static int unprotectLong(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    unsigned, uint64_t &, unsigned &);
  static int unprotectLong(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    unsigned, uint64_t &, unsigned &);
  static int protectShortV(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    ZuBSpan, const Ztls_::Bytes *, unsigned, unsigned, unsigned);
  static int protectShortV_(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    ZuBSpan, const Ztls_::Bytes *, unsigned, unsigned, unsigned, unsigned);
  static int protectShort(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    ZuBSpan, ZuBSpan, unsigned, unsigned);
  static int protectShort(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    ZuBSpan, ZuBSpan, unsigned, unsigned);
  static int unprotectShort(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    unsigned, uint64_t &, unsigned &);
  static int unprotectShort(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    unsigned, uint64_t &, unsigned &);
  static bool deriveNextSecret(TrafficSecret &, const TrafficSecret &);
};

struct CryptoDiag {
  uint64_t	transportParamsEncoded = 0;
  uint64_t	transportParamsDecoded = 0;
  uint64_t	zeroRTTRejected = 0;
  uint64_t	secretsInstalled = 0;
  uint64_t	secretsDiscarded = 0;
  uint64_t	initialKeysDerived = 0;
  uint64_t	tlsMessagesHandled = 0;
  uint64_t	tlsMessagesEmitted = 0;
  uint64_t	cryptoBytesTx = 0;
  uint64_t	cryptoBytesRx = 0;
};

using CryptoStreamRxNTP = ZmPQRxGapIgnore<>;

class ZquicAPI CryptoStream :
  public ZmPQRx<CryptoStream, CryptoRxPQueue, CryptoStreamRxNTP> {
public:
  enum { MaxBufSize = (64<<10), PrefixMax = 1 + 8 + 8 }; // 64K
  using Queue = CryptoRxPQueue;
  using Rx = ZmPQRx<CryptoStream, Queue, CryptoStreamRxNTP>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using DequeueFn = ZmFn<void(), ZmFnHeapID<"Zquic.Crypto.DequeueFn">>;
  using DeliveryFn = ZmFn<void(ZuBSpan),
    ZmFnHeapID<"Zquic.Crypto.DeliveryFn">>;

  CryptoStream(Zi::IOBufAllocator *rx = nullptr, Zi::IOBufAllocator *tx = nullptr) :
    m_rxAllocator{rx}, m_txAllocator{tx},
    m_dequeueFn{this, [](CryptoStream *s) { s->dequeue(); }} { }

  void allocators(Zi::IOBufAllocator *rx, Zi::IOBufAllocator *tx) {
    m_rxAllocator = rx;
    m_txAllocator = tx;
  }
  Zi::IOBufAllocator *txAllocator() const { return m_txAllocator; }



  uint64_t txOffset() const { return m_txOffset; }
  bool drainTx(unsigned &budget) {
    while (budget && m_txRanges.length()) {
      (void)m_txRanges.pop();
      --budget;
    }
    if (m_txRanges.length()) return false;
    m_txOffset = m_txBytes = 0;
    return true;
  }
  uint64_t rxOffset() const { return m_rxOffset; }
  bool drainRx(unsigned &budget) {
    while (budget && m_rxQueue.shift()) --budget;
    if (m_rxQueue.count_()) return false;
    Rx::rxReset(0);
    m_rxOffset = 0;
    m_delivery.length(0);
    return true;
  }
  unsigned rangeCount() const { return m_rxQueue.count_(); }

  void reset();
  bool sent(uint64_t, ZuBSpan);
  bool sent(uint64_t, ZmRef<ZiIOBuf>);
  bool sent(uint64_t, ZmRef<ZiIOBuf>, unsigned, unsigned);
  bool txPayload(uint64_t, uint64_t, ZuBSpan &) const;
  int writeFramePrefix(uint8_t *, unsigned, unsigned, CryptoDiag * = nullptr);
  int writeFrame(uint8_t *, unsigned, ZuBSpan, CryptoDiag * = nullptr);
  int receiveFrame(const Frame &, ZuBSpan &, CryptoDiag * = nullptr);
  int receive(uint64_t, ZuBSpan, ZuBSpan &, CryptoDiag * = nullptr);

  Queue *rxQueue() { return &m_rxQueue; }
  void process(Msg *);
  void request(const Span &, const Span &) { }
  void scheduleDequeue() { m_dequeueFn(); }
  void rescheduleDequeue() { m_dequeueFn(); }
  void idleDequeue() { }
  void dequeueFn(DequeueFn fn) { m_dequeueFn = ZuMv(fn); }
  void deliveryFn(DeliveryFn fn) {
    m_deliveryFn = ZuMv(fn);
  }
  using Rx::dequeue;

private:
ZuDerive(Delivery,
(ZtArray<uint8_t,
  ZtArrayHeapMax<MaxBufSize,
    ZtArrayHeapID<"Zquic.CryptoDelivery">>>));
  struct TxRange {
    ZmRef<ZiIOBuf>	data;
    uint64_t		offset;
    unsigned		begin, length;
  };
  using TxRanges = ZtArray<TxRange, ZtArrayHeapID<"Zquic.CryptoTx.Ranges">>;

  unsigned txRangeIndex(uint64_t) const;
  bool matches_(uint64_t, ZuBSpan) const;
  void appendDelivery(const uint8_t *, uint64_t);
  void resumeReadyDequeue();

  Zi::IOBufAllocator	*m_rxAllocator;
  Zi::IOBufAllocator	*m_txAllocator;
  uint64_t		m_txOffset = 0;
  uint64_t		m_rxOffset = 0;
  CryptoRxPQueue	m_rxQueue{0};
  Delivery		m_delivery;
  TxRanges		m_txRanges;
  uint64_t		m_txBytes = 0;
  DequeueFn		m_dequeueFn;
  DeliveryFn		m_deliveryFn;
};

struct CryptoConfig {
  bool			isServer = false;
  bool			enable0RTT = false;
  ZuCSpan		alpn;
  ZuCSpan		caPath;
  ZuCSpan		certPath;
  ZuCSpan		keyPath;
  ZuCSpan		keyLogPath;
  ZuCSpan		serverName;
  const TransportParams	*localParams = nullptr;
  ZquicLogger::Trace	*qlogTrace = nullptr;
  Ztls_::EncryptTicket	*encryptTicket = nullptr;
  void			*saveSessionTicketArg = nullptr;
  void			(*saveSessionTicket)(void *, ZuBSpan) = nullptr;
  ZuBSpan		sessionTicket;
  bool			chachaOnly = false;
};

ZuDerive(TLSTransportParams,
  (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Crypto.TLSTransportParams">>));

class ZquicAPI Crypto {
public:
  enum { TLSOutputMax = (64<<10) }; // 64K
  // QUIC uses TLS 1.3 cipher suites with defined header protection. TLS 1.3
  // currently has a small suite set; keep one extra slot for the null
  // terminator.

  ~Crypto();

  Crypto() = default;
  Crypto(const Crypto &) = delete;
  Crypto &operator =(const Crypto &) = delete;

  bool init(const CryptoConfig &);

  bool earlyDataEnabled() const { return m_earlyDataEnabled; }
  EarlyDataState::T earlyDataState() const { return m_earlyDataState; }
  bool earlyDataOffered() const {
    return m_earlyDataState == EarlyDataState::Offered;
  }
  bool earlyDataAccepted() const {
    return m_earlyDataState == EarlyDataState::Accepted;
  }
  bool earlyDataRejected() const {
    return m_earlyDataState == EarlyDataState::Rejected;
  }
  size_t maxEarlyData() const { return m_maxEarlyData; }
  bool oneRTTReady() const { return m_oneRTTReady; }
  bool tlsReady() const { return m_tls.rxActive && !m_tlsFailed; }
  ZuCSpan alpn() const { return m_alpn; }
  ZuCSpan negotiatedProtocol() const;
  bool secretInstalled(PktNumSpace::T level) const {
    return m_secretInstalled[level];
  }
  bool txSecretInstalled(PktNumSpace::T level) const {
    return m_txTrafficSecrets[level].valid();
  }
  bool rxSecretInstalled(PktNumSpace::T level) const {
    return m_rxTrafficSecrets[level].valid();
  }
  bool txKeySecretInstalled(PktKeyLevel::T level) const {
    return level == PktKeyLevel::ZeroRTT ?
      m_txEarlySecret.valid() :
      txSecretInstalled(spaceFromKeyLevel(level));
  }
  bool rxKeySecretInstalled(PktKeyLevel::T level) const {
    return level == PktKeyLevel::ZeroRTT ?
      m_rxEarlySecret.valid() :
      rxSecretInstalled(spaceFromKeyLevel(level));
  }
  const TrafficSecret &txSecret(PktNumSpace::T level) const {
    return m_txTrafficSecrets[level];
  }
  const TrafficSecret &rxSecret(PktNumSpace::T level) const {
    return m_rxTrafficSecrets[level];
  }
  const TrafficSecret &txKeySecret(PktKeyLevel::T level) const {
    return level == PktKeyLevel::ZeroRTT ?
      m_txEarlySecret : txSecret(spaceFromKeyLevel(level));
  }
  const TrafficSecret &rxKeySecret(PktKeyLevel::T level) const {
    return level == PktKeyLevel::ZeroRTT ?
      m_rxEarlySecret : rxSecret(spaceFromKeyLevel(level));
  }
  PktProtState &rxProtState(PktNumSpace::T level) {
    return m_rxProt[level];
  }
  uint64_t txKeyGen(PktKeyLevel::T level) const { return m_txKeyGen[level]; }
  PktProtState &rxKeyProtState(PktKeyLevel::T level) {
    return level == PktKeyLevel::ZeroRTT ?
      m_rxEarlyProt : rxProtState(spaceFromKeyLevel(level));
  }
  bool txSecret(PktNumSpace::T, const TrafficSecret &);
  bool rxSecret(PktNumSpace::T, const TrafficSecret &);
  bool txKeySecret(PktKeyLevel::T, const TrafficSecret &);
  bool rxKeySecret(PktKeyLevel::T, const TrafficSecret &);
  const CryptoDiag &diag() const { return m_diag; }
  int tlsResult() const { return m_tlsResult; }
  bool tlsKeyUpdateRejected() const { return m_tlsKeyUpdateRejected; }
  size_t tlsReadEpoch() const;
  const TransportParams &localParams() const {
    return m_localParams;
  }
  const TransportParams &peerParams() const {
    return m_peerParams;
  }
  bool peerParamsSet() const {
    return m_peerParamsSet;
  }
  void peerParams(const TransportParams &params) {
    m_peerParams = params;
    m_peerParamsSet = true;
    ++m_peerParamsGen;
  }
  uint64_t peerParamsGen() const { return m_peerParamsGen; }

  int encodeParams(uint8_t *, unsigned, const TransportParams &);
  int decodeParams(ZuBSpan, TransportParams &);
  void installSecret(PktNumSpace::T, ZuBSpan);
  bool discardSecret(PktNumSpace::T);
  bool deriveInitial(const CxnID &);
  const InitialKeyMaterial &initialKeys() const { return m_initialKeys; }
  bool rejectZeroRTT();
  bool completeHandshake();
  bool initTLS(const CryptoConfig &);
  void resetTLS() { resetTLS_(); }
  void failTLS() { m_tlsFailed = true; }
  int handleTLSMessage(ZiIOBuf *, CryptoOffsets &, size_t, ZuBSpan);

private:
  void resetTLS_();
  bool initTLSContext(const CryptoConfig &);
  bool initTLSProperties(const CryptoConfig &);
  int updateTrafficKey_(int, size_t, const void *);
  void keyLog(int, PktKeyLevel::T, ZuBSpan);
  void syncEarlyDataState();
  int onClientHello_(Ztls_::CliHelloParams *);
  int saveSessionTicket_(Ztls_::Bytes);
  int collectedExtensions(Ztls_::RawExtension *);
  static int updateTrafficKeyCB(
    Ztls::Cxn *, int, size_t, const void *);
  static int onClientHelloCB(
    Ztls::Cxn *, Ztls_::CliHelloParams *);
  static int saveSessionTicketCB(
    Ztls::Cxn *, Ztls_::Bytes);
  static int collectExtensionCB(
    Ztls::Cxn *, uint16_t);
  static int collectedExtensionsCB(
    Ztls::Cxn *, Ztls_::RawExtension *);

  using SecretFlags = ZuArray<bool, PktNumSpace::N>;
  using TrafficSecrets = ZuArray<TrafficSecret, PktNumSpace::N>;
  using KeyGenerations = ZuArray<uint64_t, PktKeyLevel::N>;
  using PktProtStates = ZuArray<PktProtState, PktNumSpace::N>;

  bool			m_isServer = false;
  bool			m_earlyDataEnabled = false;
  EarlyDataState::T	m_earlyDataState = EarlyDataState::Disabled;
  bool			m_oneRTTReady = false;
  SecretFlags		m_secretInstalled = fixedArray<bool, PktNumSpace::N>();
  TrafficSecrets	m_txTrafficSecrets{PktNumSpace::N};
  TrafficSecrets	m_rxTrafficSecrets{PktNumSpace::N};
  TrafficSecret		m_txEarlySecret;
  TrafficSecret		m_rxEarlySecret;
  KeyGenerations	m_txKeyGen = fixedArray<uint64_t, PktKeyLevel::N>();
  PktProtStates		m_rxProt{PktNumSpace::N};
  PktProtState		m_rxEarlyProt;
  ParamString		m_alpn;
  Host			m_serverName;
  ParamString		m_keyLogPath;
  ZquicLogger::Trace	*m_qlogTrace = nullptr;
  void			*m_saveSessionTicketArg = nullptr;
  void			(*m_saveSessionTicket)(void *, ZuBSpan) = nullptr;
  InitialKeyMaterial	m_initialKeys;
  CryptoDiag		m_diag;
  TransportParams	m_localParams;
  TransportParams	m_peerParams;
  bool			m_peerParamsSet = false;
  uint64_t		m_peerParamsGen = 0;
  Ztls_::Context	m_tlsCtx{};
  Ztls::Cxn		m_tls;
  Ztls_::RawExtension	m_tlsExtensions[2]{};
  TLSTransportParams	m_tlsParams;
  Ztls_::Bytes		m_alpnVec{};
  size_t		m_maxEarlyData = 0;
  int			m_tlsResult = Ztls_ErrorInProgress;
  bool			m_tlsKeyUpdateRejected = false;
  bool			m_tlsFailed = false;
  Ztls::CertStore	*m_certStore = nullptr;
  Ztls::VerifyCert	*m_verify = nullptr;
  Ztls::PK::Key		m_key;
  Ztls::SignCert	*m_sign = nullptr;
};

} // namespace Zquic
