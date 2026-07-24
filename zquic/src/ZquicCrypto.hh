//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC crypto integration shell

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <zpicotls.h>

#include <zlib/ZmFn.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>

#include <zlib/ZtlsPico.hh>

namespace Ztls {
namespace Backend {
  struct PKey;
  struct CertStore;
  struct SignCert;
  struct TicketKey;
  struct VerifyCert;
}
}

namespace Zquic {

struct InitialSecret {
  static constexpr unsigned SecretLen = 32;
  static constexpr unsigned KeyLen = 16;
  static constexpr unsigned IVLen = 12;
  static constexpr unsigned TagLen = 16;
  static constexpr unsigned HPMaskLen = 5;

  uint8_t	secret[SecretLen] = {};
  uint8_t	key[KeyLen] = {};
  uint8_t	iv[IVLen] = {};
  uint8_t	hp[KeyLen] = {};
};

struct InitialKeyMaterial {
  uint8_t	initial[InitialSecret::SecretLen] = {};
  InitialSecret	client;
  InitialSecret	server;
};

struct InitialCrypto {
  static bool derive(InitialKeyMaterial &, const CxnID &);
  static int encrypt(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuBSpan, ZuBSpan);
  static int encryptV(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuBSpan,
    const ptls_iovec_t *, unsigned);
  static int decrypt(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuBSpan, ZuBSpan);
  static bool headerMask(
    uint8_t *, unsigned, const InitialSecret &, ZuBSpan);
};

struct InitialPktProt {
  static int protectLong(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    ZuBSpan, ZuBSpan, unsigned, unsigned);
  static int protectLongV(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    ZuBSpan, const ptls_iovec_t *, unsigned, unsigned, unsigned);
  static int unprotectLong(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    unsigned, uint64_t &, unsigned &);
};

struct TrafficSecret {
  static constexpr unsigned MaxSecretLen = 64;
  static constexpr unsigned MaxKeyLen = 32;
  static constexpr unsigned MaxIVLen = 16;
  static constexpr unsigned MaxHPLen = 32;

  void clear();
  bool valid() const { return installed; }

  uint8_t			secret[MaxSecretLen] = {};
  uint8_t			key[MaxKeyLen] = {};
  uint8_t			iv[MaxIVLen] = {};
  uint8_t			hp[MaxHPLen] = {};
  unsigned			secretLen = 0;
  unsigned			keyLen = 0;
  unsigned			ivLen = 0;
  unsigned			hpLen = 0;
  unsigned			tagLen = 0;
  ptls_aead_algorithm_t		*aead = nullptr;
  ptls_hash_algorithm_t		*hash = nullptr;
  ptls_cipher_algorithm_t	*hpCipher = nullptr;
  ptls_cipher_algorithm_t	*hpSuppCipher = nullptr;
  bool				installed = false;
};

struct PktProtState {
  PktProtState() = default;
  PktProtState(const PktProtState &) = delete;
  PktProtState &operator =(const PktProtState &) = delete;

  bool init(const TrafficSecret &, PktNumSpace::T, bool);
  void clear();
  bool valid() const { return installed; }

  Ztls::Pico::AeadCtx	aead;
  Ztls::Pico::CipherCtx hp;
  Ztls::Pico::CipherCtx hpSupp;
  TrafficSecret		secret;
  PktNumSpace::T	level = PktNumSpace::Initial;
  bool			tx = false;
  bool			installed = false;
};

struct PktProt {
  static bool deriveSecret(
    TrafficSecret &, ptls_cipher_suite_t *, ZuBSpan);
  static int protectLongV(
    uint8_t *, unsigned, PktProtState &, uint64_t,
    ZuBSpan, const ptls_iovec_t *, unsigned, unsigned, unsigned);
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
    ZuBSpan, const ptls_iovec_t *, unsigned, unsigned, unsigned);
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

class CryptoStream :
  public ZmPQRx<CryptoStream, CryptoRxPQueue, CryptoStreamRxNTP> {
public:
  static constexpr unsigned MaxBufSize = (64<<10); // 64K
  // Covers mainstream TLS 1.3 handshakes without packet loss while avoiding
  // heap allocation on the common path; larger certificate chains fall back.
  static constexpr unsigned BuiltinBufSize = (8<<10); // 8K
  using Queue = CryptoRxPQueue;
  using Rx = ZmPQRx<CryptoStream, Queue, CryptoStreamRxNTP>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using DequeueFn = ZmFn<void(), ZmFnHeapID<"Zquic.Crypto.DequeueFn">>;
  using DeliveryFn = ZmFn<void(ZuBSpan),
    ZmFnHeapID<"Zquic.Crypto.DeliveryFn">>;

  CryptoStream() :
    m_dequeueFn{this, [](CryptoStream *s) { s->dequeueRx_(); }} { }

  uint64_t txOffset() const { return m_txOffset; }
  uint64_t rxOffset() const { return m_rxOffset; }
  unsigned rangeCount() const { return m_rxQueue.count_(); }

  void reset();
  bool sent(uint64_t, ZuBSpan);
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
    m_asyncDelivery = true;
  }
  void dequeueRx_() { Rx::dequeue(); }

private:
ZuDerive(Delivery,
(ZtBuiltin<
  ZtArray<uint8_t,
    ZtArrayHeapMax<MaxBufSize,
      ZtArrayHeapID<"Zquic.CryptoDelivery">>>,
  BuiltinBufSize>));
ZuDerive(TxData,
(ZtBuiltin<
  ZtArray<uint8_t,
    ZtArrayHeapMax<MaxBufSize,
      ZtArrayHeapID<"Zquic.CryptoTx">>>,
  BuiltinBufSize>));

  void appendDelivery_(const uint8_t *, uint64_t);
  void resumeReadyDequeue_();

  uint64_t		m_txOffset = 0;
  uint64_t		m_rxOffset = 0;
  CryptoRxPQueue	m_rxQueue{0};
  Delivery		m_delivery;
  TxData		m_txData;
  DequeueFn		m_dequeueFn;
  DeliveryFn		m_deliveryFn;
  bool			m_asyncDelivery = false;
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
  ptls_encrypt_ticket_t	*encryptTicket = nullptr;
  void			*saveSessionTicketArg = nullptr;
  void			(*saveSessionTicket)(void *, ZuBSpan) = nullptr;
  ZuBSpan		sessionTicket;
};

ZuDerive(TLSTransportParams,
  (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Crypto.TLSTransportParams">>));

class Crypto {
public:
  static constexpr unsigned TLSOutputMax = (64<<10); // 64K
  // QUIC uses TLS 1.3 cipher suites with defined header protection. TLS 1.3
  // currently has a small suite set; keep one extra slot for picotls' null
  // terminator.
  static constexpr unsigned TLSMaxCiphers = 15;

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
  bool tlsReady() const { return m_tls; }
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
  PktProtState &txProtState(PktNumSpace::T level) {
    return m_txProt[level];
  }
  PktProtState &rxProtState(PktNumSpace::T level) {
    return m_rxProt[level];
  }
  PktProtState &txKeyProtState(PktKeyLevel::T level) {
    return level == PktKeyLevel::ZeroRTT ?
      m_txEarlyProt : txProtState(spaceFromKeyLevel(level));
  }
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
  }

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
  int handleTLSMessage(
    ZiIOBuf *, size_t[5], size_t, ZuBSpan);

private:
  void resetTLS_();
  bool initTLSContext_(const CryptoConfig &);
  bool initTLSProperties_(const CryptoConfig &);
  int updateTrafficKey_(int, size_t, const void *);
  void keyLog_(int, PktKeyLevel::T, ZuBSpan);
  void syncEarlyDataState_();
  int onClientHello_(ptls_on_client_hello_parameters_t *);
  int saveSessionTicket_(ptls_iovec_t);
  int collectedExtensions_(ptls_raw_extension_t *);
  static int updateTrafficKeyCB_(
    ptls_update_traffic_key_t *, ptls_t *, int, size_t, const void *);
  static int onClientHelloCB_(
    ptls_on_client_hello_t *, ptls_t *, ptls_on_client_hello_parameters_t *);
  static int saveSessionTicketCB_(
    ptls_save_ticket_t *, ptls_t *, ptls_iovec_t);
  static int collectExtensionCB_(
    ptls_t *, ptls_handshake_properties_t *, uint16_t);
  static int collectedExtensionsCB_(
    ptls_t *, ptls_handshake_properties_t *, ptls_raw_extension_t *);

  bool				m_isServer = false;
  bool				m_earlyDataEnabled = false;
  EarlyDataState::T		m_earlyDataState = EarlyDataState::Disabled;
  bool				m_oneRTTReady = false;
  bool				m_secretInstalled[PktNumSpace::N] = {};
  TrafficSecret 		m_txTrafficSecrets[PktNumSpace::N];
  TrafficSecret 		m_rxTrafficSecrets[PktNumSpace::N];
  TrafficSecret 		m_txEarlySecret;
  TrafficSecret 		m_rxEarlySecret;
  PktProtState			m_txProt[PktNumSpace::N];
  PktProtState			m_rxProt[PktNumSpace::N];
  PktProtState			m_txEarlyProt;
  PktProtState			m_rxEarlyProt;
  ParamString			m_alpn;
  Host				m_serverName;
  ParamString			m_keyLogPath;
  ZquicLogger::Trace		*m_qlogTrace = nullptr;
  void				*m_saveSessionTicketArg = nullptr;
  void				(*m_saveSessionTicket)(void *, ZuBSpan) = nullptr;
  InitialKeyMaterial 		m_initialKeys;
  CryptoDiag			m_diag;
  TransportParams 		m_localParams;
  TransportParams 		m_peerParams;
  bool				m_peerParamsSet = false;
  ptls_context_t 		m_tlsCtx{};
  ptls_cipher_suite_t		*m_tlsCipherSuites[TLSMaxCiphers + 1]{};
  ptls_t			*m_tls = nullptr;
  ptls_handshake_properties_t 	m_tlsProps{};
  ptls_raw_extension_t 		m_tlsExtensions[2]{};
  TLSTransportParams 		m_tlsParams;
  ptls_iovec_t			m_alpnVec{};
  size_t			m_maxEarlyData = 0;
  int				m_tlsResult = PTLS_ERROR_IN_PROGRESS;
  Ztls::Backend::CertStore	*m_certStore = nullptr;
  Ztls::Backend::VerifyCert	*m_verify = nullptr;
  Ztls::Backend::PKey		*m_key = nullptr;
  Ztls::Backend::SignCert	*m_sign = nullptr;
};

} // namespace Zquic
