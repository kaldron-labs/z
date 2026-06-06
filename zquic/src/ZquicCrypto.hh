//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC crypto integration shell

#ifndef ZquicCrypto_HH
#define ZquicCrypto_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zpicotls.h>

#include <zlib/ZtArray.hh>

#include <zlib/ZtlsPico.hh>

#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicPQueue.hh>
#include <zlib/ZquicTransportParams.hh>

namespace Ztls { namespace Backend {
struct PKey;
struct CertStore;
struct SignCert;
struct VerifyCert;
} }

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
  static bool derive(InitialKeyMaterial &, const ConnectionID &);
  static int encrypt(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuCSpan, ZuCSpan);
  static int encryptV(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuCSpan,
    const ptls_iovec_t *, unsigned);
  static int decrypt(
    uint8_t *, unsigned, const InitialSecret &, uint64_t, ZuCSpan, ZuCSpan);
  static bool headerMask(
    uint8_t *, unsigned, const InitialSecret &, ZuCSpan);
};

struct InitialPacketProtection {
  static int protectLong(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    ZuCSpan, ZuCSpan, unsigned, unsigned);
  static int protectLongV(
    uint8_t *, unsigned, const InitialSecret &, uint64_t,
    ZuCSpan, const ptls_iovec_t *, unsigned, unsigned, unsigned);
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

  uint8_t	secret[MaxSecretLen] = {};
  uint8_t	key[MaxKeyLen] = {};
  uint8_t	iv[MaxIVLen] = {};
  uint8_t	hp[MaxHPLen] = {};
  unsigned	secretLen = 0;
  unsigned	keyLen = 0;
  unsigned	ivLen = 0;
  unsigned	hpLen = 0;
  unsigned	tagLen = 0;
  ptls_aead_algorithm_t *aead = nullptr;
  ptls_hash_algorithm_t *hash = nullptr;
  ptls_cipher_algorithm_t *hpCipher = nullptr;
  ptls_cipher_algorithm_t *hpSuppCipher = nullptr;
  bool		installed = false;
};

struct PacketProtectionState {
  PacketProtectionState() = default;
  PacketProtectionState(const PacketProtectionState &) = delete;
  PacketProtectionState &operator =(const PacketProtectionState &) = delete;

  bool init(const TrafficSecret &, CryptoLevel::T, bool);
  void clear();
  bool valid() const { return installed; }

  Ztls::Pico::AeadCtx	aead;
  Ztls::Pico::CipherCtx hp;
  Ztls::Pico::CipherCtx hpSupp;
  TrafficSecret		secret;
  CryptoLevel::T	level = CryptoLevel::Initial;
  bool			tx = false;
  bool			installed = false;
};

struct PacketProtection {
  static bool deriveTrafficSecret(
    TrafficSecret &, ptls_cipher_suite_t *, ZuCSpan);
  static int protectLongV(
    uint8_t *, unsigned, PacketProtectionState &, uint64_t,
    ZuCSpan, const ptls_iovec_t *, unsigned, unsigned, unsigned);
  static int protectLong(
    uint8_t *, unsigned, PacketProtectionState &, uint64_t,
    ZuCSpan, ZuCSpan, unsigned, unsigned);
  static int protectLong(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    ZuCSpan, ZuCSpan, unsigned, unsigned);
  static int unprotectLong(
    uint8_t *, unsigned, PacketProtectionState &, uint64_t,
    unsigned, uint64_t &, unsigned &);
  static int unprotectLong(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    unsigned, uint64_t &, unsigned &);
  static int protectShortV(
    uint8_t *, unsigned, PacketProtectionState &, uint64_t,
    ZuCSpan, const ptls_iovec_t *, unsigned, unsigned, unsigned);
  static int protectShort(
    uint8_t *, unsigned, PacketProtectionState &, uint64_t,
    ZuCSpan, ZuCSpan, unsigned, unsigned);
  static int protectShort(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    ZuCSpan, ZuCSpan, unsigned, unsigned);
  static int unprotectShort(
    uint8_t *, unsigned, PacketProtectionState &, uint64_t,
    unsigned, uint64_t &, unsigned &);
  static int unprotectShort(
    uint8_t *, unsigned, const TrafficSecret &, uint64_t,
    unsigned, uint64_t &, unsigned &);
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
  uint64_t	cryptoFramesTx = 0;
  uint64_t	cryptoFramesRx = 0;
  uint64_t	cryptoBytesTx = 0;
  uint64_t	cryptoBytesRx = 0;
  uint64_t	packetProtectionContextInits = 0;
};

using CryptoStreamRxNTP = ZmPQRxGapIgnore<>;

class CryptoStream :
  public ZmPQRx<CryptoStream, CryptoRxPQueue, CryptoStreamRxNTP> {
public:
  static constexpr unsigned MaxBuffered = 64 * 1024;
  using Queue = CryptoRxPQueue;
  using Rx = ZmPQRx<CryptoStream, Queue, CryptoStreamRxNTP>;
  using Msg = Queue::Node;
  using Span = Queue::Span;

  uint64_t txOffset() const { return m_txOffset; }
  uint64_t rxOffset() const { return m_rxOffset; }
  unsigned rangeCount() const { return m_rxQueue.count_(); }

  void reset();
  int writeFramePrefix(uint8_t *, unsigned, unsigned, CryptoDiag * = nullptr);
  int writeFrame(uint8_t *, unsigned, ZuCSpan, CryptoDiag * = nullptr);
  int receiveFrame(const Frame &, ZuCSpan &, CryptoDiag * = nullptr);
  int receive(uint64_t, ZuCSpan, ZuCSpan &, CryptoDiag * = nullptr);

  Queue *rxQueue() { return &m_rxQueue; }
  void process(Msg *);
  void request(const Span &, const Span &) { }
  void scheduleDequeue() { Rx::dequeue(); }
  void rescheduleDequeue() { Rx::dequeue(); }
  void idleDequeue() { }

private:
  ZuDerive(Delivery,
    (ZtArray<uint8_t, ZtArrayHeapID<"Zquic.CryptoDelivery">>));

  void appendDelivery_(const uint8_t *, uint64_t);

  uint64_t	m_txOffset = 0;
  uint64_t	m_rxOffset = 0;
  CryptoRxPQueue m_rxQueue{0};
  Delivery	m_delivery;
};

struct CryptoConfig {
  bool		isServer = false;
  bool		enable0RTT = false;
  ZuCSpan	alpn;
  ZuCSpan	caPath;
  ZuCSpan	certPath;
  ZuCSpan	keyPath;
  ZuCSpan	serverName;
  const TransportParams *localTransportParams = nullptr;
};

class Crypto {
public:
  static constexpr unsigned TLSOutputMax = 64 * 1024;

  ~Crypto();

  Crypto() = default;
  Crypto(const Crypto &) = delete;
  Crypto &operator =(const Crypto &) = delete;

  bool init(const CryptoConfig &);

  bool earlyDataEnabled() const { return m_earlyDataEnabled; }
  bool oneRTTReady() const { return m_oneRTTReady; }
  bool tlsReady() const { return m_tls; }
  ZuCSpan alpn() const {
    return ZuCSpan{reinterpret_cast<const char *>(m_alpn), m_alpnLength};
  }
  ZuCSpan negotiatedProtocol() const;
  bool secretInstalled(CryptoLevel::T level) const {
    return m_secretInstalled[level];
  }
  bool txTrafficSecretInstalled(CryptoLevel::T level) const {
    return m_txTrafficSecrets[level].valid();
  }
  bool rxTrafficSecretInstalled(CryptoLevel::T level) const {
    return m_rxTrafficSecrets[level].valid();
  }
  const TrafficSecret &txTrafficSecret(CryptoLevel::T level) const {
    return m_txTrafficSecrets[level];
  }
  const TrafficSecret &rxTrafficSecret(CryptoLevel::T level) const {
    return m_rxTrafficSecrets[level];
  }
  PacketProtectionState &txProtectionState(CryptoLevel::T level) {
    return m_txProtection[level];
  }
  PacketProtectionState &rxProtectionState(CryptoLevel::T level) {
    return m_rxProtection[level];
  }
  const CryptoDiag &diag() const { return m_diag; }
  int tlsResult() const { return m_tlsResult; }
  size_t tlsReadEpoch() const;
  const TransportParams &localTransportParams() const {
    return m_localTransportParams;
  }
  const TransportParams &peerTransportParams() const {
    return m_peerTransportParams;
  }
  bool peerTransportParamsReceived() const {
    return m_peerTransportParamsReceived;
  }

  int encodeTransportParams(uint8_t *, unsigned, const TransportParams &);
  int decodeTransportParams(ZuCSpan, TransportParams &);
  void installSecret(CryptoLevel::T, ZuCSpan);
  bool discardSecret(CryptoLevel::T);
  bool deriveInitial(const ConnectionID &);
  const InitialKeyMaterial &initialKeys() const { return m_initialKeys; }
  bool rejectZeroRTT();
  bool completeHandshake();
  bool initTLS(const CryptoConfig &);
  int handleTLSMessage(
    ZiIOBuf *, size_t[5], size_t, ZuCSpan);

private:
  void resetTLS_();
  bool initTLSContext_(const CryptoConfig &);
  bool initTLSProperties_(const CryptoConfig &);
  int updateTrafficKey_(int, size_t, const void *);
  int onClientHello_(ptls_on_client_hello_parameters_t *);
  int collectedExtensions_(ptls_raw_extension_t *);
  static int updateTrafficKeyCB_(
    ptls_update_traffic_key_t *, ptls_t *, int, size_t, const void *);
  static int onClientHelloCB_(
    ptls_on_client_hello_t *, ptls_t *, ptls_on_client_hello_parameters_t *);
  static int collectExtensionCB_(
    ptls_t *, ptls_handshake_properties_t *, uint16_t);
  static int collectedExtensionsCB_(
    ptls_t *, ptls_handshake_properties_t *, ptls_raw_extension_t *);

  bool		m_isServer = false;
  bool		m_earlyDataEnabled = false;
  bool		m_oneRTTReady = false;
  bool		m_secretInstalled[3] = {};
  TrafficSecret m_txTrafficSecrets[3];
  TrafficSecret m_rxTrafficSecrets[3];
  PacketProtectionState m_txProtection[3];
  PacketProtectionState m_rxProtection[3];
  uint8_t	m_alpn[255] = {};
  uint8_t	m_alpnLength = 0;
  uint8_t	m_serverName[255] = {};
  uint8_t	m_serverNameLength = 0;
  InitialKeyMaterial m_initialKeys;
  CryptoDiag	m_diag;
  TransportParams m_localTransportParams;
  TransportParams m_peerTransportParams;
  bool		m_peerTransportParamsReceived = false;
  ptls_context_t m_tlsCtx{};
  ptls_cipher_suite_t *m_tlsCipherSuites[16]{};
  ptls_t	*m_tls = nullptr;
  ptls_handshake_properties_t m_tlsProps{};
  ptls_raw_extension_t m_tlsExtensions[2]{};
  uint8_t	m_tlsTransportParams[512]{};
  unsigned	m_tlsTransportParamsLen = 0;
  ptls_iovec_t	m_alpnVec{};
  size_t	m_maxEarlyData = 0;
  int		m_tlsResult = PTLS_ERROR_IN_PROGRESS;
  Ztls::Backend::CertStore *m_certStore = nullptr;
  Ztls::Backend::VerifyCert *m_verify = nullptr;
  Ztls::Backend::PKey *m_key = nullptr;
  Ztls::Backend::SignCert *m_sign = nullptr;
};

} // namespace Zquic

#endif /* ZquicCrypto_HH */
