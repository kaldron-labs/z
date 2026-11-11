//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// backend facade for the zpicotls back-end
// - currently OpenSSL
// - minicrypto could be a future option

#ifndef ZtlsBackend_HH
#define ZtlsBackend_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#ifndef Ztls_OpenSSL
#define Ztls_OpenSSL 1
#endif

#ifndef Ztls_Fusion
#if defined(__x86_64__) || defined(_M_X64) || \
    defined(__i386__) || defined(_M_IX86)
#define Ztls_Fusion 1
#else
#define Ztls_Fusion 0
#endif
#endif

#if Ztls_OpenSSL
#include <openssl/crypto.h>
#define Ztls_memcmp CRYPTO_memcmp
#endif

#include <zpicotls.h>
#if Ztls_Fusion
#include <zpicotls/fusion.h>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZiTx.hh>

template <>
struct ZuTraits<ptls_iovec_t> : public ZuBaseTraits<ptls_iovec_t> {
  enum { IsArray = 1, IsSpan = 1, IsPrimitive = 0 };
  using Elem = uint8_t;

  static uint8_t *data(ptls_iovec_t &v) { return v.base; }
  static const uint8_t *data(const ptls_iovec_t &v) { return v.base; }
  static size_t length(const ptls_iovec_t &v) { return v.len; }
};

namespace Ztls {

enum { X25519KeySize = 32 }; // RFC 7748 raw public and private keys

ZtEnumNS(ZtlsAPI, MDAlg, int8_t,
  SHA1,
  SHA256,
  SHA384,
  SHA512);

using MDType = MDAlg::T;
inline constexpr MDType SHA1 = MDAlg::SHA1;
inline constexpr MDType SHA256 = MDAlg::SHA256;
inline constexpr MDType SHA384 = MDAlg::SHA384;
inline constexpr MDType SHA512 = MDAlg::SHA512;

namespace Backend {

using TxPacket = ZiTxBuf;

// Holds provisional plaintext privately until the receive operation authenticates it.
// The C sink never invokes application code; the owner takes output after return.
class PlaintextSink {
public:
  PlaintextSink() = default;
  PlaintextSink(Zi::IOBufAllocator *allocator, void *owner)
  : m_allocator{allocator}, m_owner{owner} { }
  PlaintextSink(const PlaintextSink &) = delete;
  PlaintextSink &operator =(const PlaintextSink &) = delete;
  ~PlaintextSink() { clear(); }

  void reset(Zi::IOBufAllocator *allocator, void *owner) {
    clear(); m_allocator = allocator; m_owner = owner;
  }
  void clear() {
    discard();
    if (m_ready) {
      ptls_clear_memory(m_ready->data(), m_ready->length);
      m_ready = nullptr;
    }
  }
  ZmRef<ZiIOBuf> take() { return ZuMv(m_ready); }

  ptls_plaintext_sink_t sink() {
    return {
      this,
      [](void *ctx, size_t size, void **base, size_t *capacity) -> int {
	auto self = static_cast<PlaintextSink *>(ctx);
	try { return self->reserve(size, base, capacity); }
	catch (...) { self->discard(); return PTLS_ERROR_NO_MEMORY; }
      },
      [](void *ctx, size_t length) -> int {
	return static_cast<PlaintextSink *>(ctx)->commit(length);
      },
      [](void *ctx) { static_cast<PlaintextSink *>(ctx)->discard(); }
    };
  }

private:
  int reserve(size_t size, void **base, size_t *capacity) {
    if (m_private || m_ready || !m_allocator ||
	m_allocator->type != Zi::IOBufType::RxTLS || size > PTLS_RECEIVE_RECORD_CAPACITY)
      return PTLS_ERROR_LIBRARY;
    auto buf = m_allocator->allocBuf(m_owner);
    if (!buf) return PTLS_ERROR_NO_MEMORY;
    if (buf->registered() || buf->skip > buf->size ||
	m_allocator->tailRoom > buf->size - buf->skip ||
	buf->size - buf->skip - m_allocator->tailRoom < PTLS_RECEIVE_RECORD_CAPACITY)
      return PTLS_ERROR_LIBRARY;
    m_extent = size;
    m_private = ZuMv(buf);
    *base = m_private->data();
    *capacity = size;
    return 0;
  }
  int commit(size_t length) {
    if (!m_private || m_ready || length > m_extent ||
	length > PTLS_RECEIVE_APPLICATION_CAPACITY) {
      discard(); return PTLS_ERROR_LIBRARY;
    }
    // Clear authenticated padding/content type as well as discarded provisional data.
    ptls_clear_memory(m_private->data() + length, m_extent - length);
    m_private->length = uint32_t(length);
    m_ready = ZuMv(m_private);
    m_extent = 0;
    return 0;
  }
  void discard() {
    if (m_private) {
      ptls_clear_memory(m_private->data(), m_extent);
      m_private = nullptr;
    }
    m_extent = 0;
  }

  Zi::IOBufAllocator	*m_allocator = nullptr;
  void			*m_owner = nullptr;
  ZmRef<ZiIOBuf>		m_private, m_ready;
  size_t			m_extent = 0;
};

// Owns one unpublished record reservation. Packet backing is never resized.
class RecordSink {
public:
  using PublishFn = int (*)(void *, ZmRef<ZiIOBuf>);
  using ReadyFn = void (*)(void *, ZmRef<ZiIOBuf>);

  RecordSink(
    Zi::IOBufAllocator *allocator, void *owner, uint32_t mps,
    void *ctx, PublishFn publish, ReadyFn ready = nullptr)
  : m_allocator{allocator}, m_owner{owner}, m_mps{mps},
    m_ctx{ctx}, m_publish{publish}, m_ready{ready} { }

  ptls_record_sink_t sink() {
    return {
      this,
      [](void *ctx, size_t size) -> int {
	auto self = static_cast<RecordSink *>(ctx);
	try { return self->reserve(size); }
	catch (...) { self->discard(); return PTLS_ERROR_NO_MEMORY; }
      },
      [](void *ctx, void **base, size_t *capacity) -> int {
	return static_cast<RecordSink *>(ctx)->next(base, capacity);
      },
      [](void *ctx, size_t length) -> int {
	return static_cast<RecordSink *>(ctx)->commit(length);
      },
      [](void *ctx) -> int {
	auto self = static_cast<RecordSink *>(ctx);
	try { return self->finish(); }
	catch (...) { self->discard(); return PTLS_ERROR_LIBRARY; }
      },
      [](void *ctx) { static_cast<RecordSink *>(ctx)->discard(); }
    };
  }

private:
  int reserve(size_t size) {
    if (m_active || !m_mps) return PTLS_ERROR_LIBRARY;
    m_active = true;
    m_size = size;
    size_t remaining = size;
    while (remaining) {
      auto buf = m_allocator->allocBuf(m_owner, 0, m_mps);
      if (!buf) { discard(); return PTLS_ERROR_NO_MEMORY; }
      if (buf->skip > buf->size ||
	m_allocator->tailRoom >= buf->size - buf->skip) {
	discard(); return PTLS_ERROR_LIBRARY;
      }
      auto capacity = buf->size - buf->skip - m_allocator->tailRoom;
      if (capacity > remaining) capacity = unsigned(remaining);
      remaining -= capacity;
      m_packets.pushNode(ZuMv(buf));
    }
    m_current = m_packets.headPtr();
    return 0;
  }

  int next(void **base, size_t *capacity) {
    if (!m_active || !m_current || m_committed == m_size)
      return PTLS_ERROR_LIBRARY;
    auto buf = m_current;
    auto room = buf->size - buf->skip - m_allocator->tailRoom - buf->length;
    if (!room) {
      m_written.pushNode(m_packets.shift());
      buf = m_current = m_packets.headPtr();
      if (!buf) return PTLS_ERROR_LIBRARY;
      room = buf->size - buf->skip - m_allocator->tailRoom;
    }
    if (room > m_size - m_committed) room = unsigned(m_size - m_committed);
    *base = static_cast<ZiIOBuf *>(buf)->data() + buf->length;
    *capacity = m_exposed = room;
    return room ? 0 : PTLS_ERROR_LIBRARY;
  }

  int commit(size_t length) {
    if (!m_active || !m_current || !m_exposed || length > m_exposed)
      return PTLS_ERROR_LIBRARY;
    m_current->length += uint32_t(length);
    m_committed += length;
    m_exposed = 0;
    return 0;
  }

  int finish() {
    if (!m_active || m_committed != m_size) return PTLS_ERROR_LIBRARY;
    m_written += ZuMv(m_packets);
    if (m_ready) m_ready(m_ctx, ZmRef<ZiIOBuf>{m_written.tailPtr()});
    while (auto buf = m_written.shift()) {
      int n = m_publish(m_ctx, ZuMv(buf));
      if (n) { discard(); return n; }
    }
    discard();
    return 0;
  }

  void discard() {
    m_packets.clean();
    m_written.clean();
    m_current = nullptr;
    m_size = m_committed = m_exposed = 0;
    m_active = false;
  }

  Zi::IOBufAllocator *m_allocator;
  void		*m_owner;
  uint32_t	m_mps;
  void		*m_ctx;
  PublishFn	m_publish;
  ReadyFn	m_ready;
  ZiTxQueue	m_packets;
  ZiTxQueue	m_written;
  ZiTxBuf	*m_current = nullptr;
  size_t	m_size = 0;
  size_t	m_committed = 0;
  size_t	m_exposed = 0;
  bool		m_active = false;
};

struct PKey;
struct CertStore;
struct SignCert;
struct VerifyCert;
struct TicketKey;

ZtlsAPI bool init();
ZtlsAPI bool random_bytes(ZuSpan<uint8_t> data);

using RandomBytesFn = void (*)(void *, size_t);
ZtlsAPI RandomBytesFn random_bytes_cb();
ZtlsAPI ptls_key_exchange_algorithm_t **key_exchanges();
ZtlsAPI ptls_cipher_suite_t **cipher_suites();
ZtlsAPI ptls_cipher_suite_t *fusion_cipher_suite(uint16_t id);
ZtlsAPI ptls_cipher_suite_t *cipher_suite(uint16_t id);
ZtlsAPI ptls_cipher_suite_t *tls12RSA_AES128GCM();
ZtlsAPI ptls_cipher_suite_t *tls12RSA_ChaCha20();
ZtlsAPI ptls_aead_algorithm_t *chacha20poly1305();

ZtlsAPI ptls_hash_algorithm_t *hash_algorithm(MDType type);
ZtlsAPI ptls_hash_context_t *hmac_create(MDType type, ZuBSpan key);
ZtlsAPI size_t hash_size(MDType type);
ZtlsAPI bool shake256(ZuBSpan input, ZuSpan<uint8_t> output);
ZtlsAPI bool sha3_256(
  ZuSpan<const ZuBSpan> input, ZuSpan<uint8_t> output);
ZtlsAPI bool scrypt(
  ZuBSpan password, ZuBSpan salt, uint64_t n,
  unsigned r, unsigned p, uint64_t maxMem, ZuSpan<uint8_t> output);

ZtlsAPI size_t format_error(int err, char *buf, size_t len);

ZtlsAPI PKey *pkey_new();
ZtlsAPI void pkey_free(PKey *);

ZtlsAPI size_t pkey_rsa_size(const PKey *);
ZtlsAPI bool pkey_rsa_generate(PKey *, unsigned bits);
ZtlsAPI bool pkey_rsa_import_public(PKey *, ZuBSpan modulus, ZuBSpan pubExp);
ZtlsAPI bool pkey_rsa_import_private(
  PKey *, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan prime1, ZuBSpan prime2, ZuBSpan exp1, ZuBSpan exp2, ZuBSpan coeff);
ZtlsAPI bool pkey_rsa_import_ssh_private(
  PKey *, ZuBSpan modulus, ZuBSpan pubExp, ZuBSpan prvExp,
  ZuBSpan coeff, ZuBSpan prime1, ZuBSpan prime2);
ZtlsAPI bool pkey_rsa_export_public(
  const PKey *, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp);
ZtlsAPI bool pkey_rsa_export_private(
  const PKey *, ZuSpan<uint8_t> modulus, ZuSpan<uint8_t> pubExp,
  ZuSpan<uint8_t> prvExp, ZuSpan<uint8_t> prime1, ZuSpan<uint8_t> prime2,
  ZuSpan<uint8_t> exp1, ZuSpan<uint8_t> exp2, ZuSpan<uint8_t> coeff);
ZtlsAPI bool pkey_rsa_oaep_encrypt(
  const PKey *, ZuBSpan plaintext, ZuBSpan label,
  ZuSpan<uint8_t> ciphertext, size_t *length);
ZtlsAPI bool pkey_rsa_oaep_decrypt(
  const PKey *, ZuBSpan ciphertext, ZuBSpan label,
  ZuSpan<uint8_t> plaintext, size_t *length);

ZtlsAPI size_t pkey_ec_key_size(const PKey *);
ZtlsAPI size_t pkey_ec_public_size(const PKey *);
ZtlsAPI size_t pkey_ec_oid_size(const PKey *);
ZtlsAPI bool pkey_ec_generate(PKey *, ZuBSpan oid);
ZtlsAPI bool pkey_ec_import_public(PKey *, ZuBSpan oid, ZuBSpan pubKey);
ZtlsAPI bool pkey_ec_import_private(PKey *, ZuBSpan oid, ZuBSpan key);
ZtlsAPI bool pkey_ec_export_private(const PKey *, ZuSpan<uint8_t> key);
ZtlsAPI bool pkey_ec_export_public(const PKey *, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_ec_export_oid(const PKey *, ZuSpan<uint8_t> oid);

ZtlsAPI bool pkey_ed25519_generate(PKey *);
ZtlsAPI bool pkey_ed25519_import_public(PKey *, ZuBSpan pubKey);
ZtlsAPI bool pkey_ed25519_import_private(PKey *, ZuBSpan key);
ZtlsAPI bool pkey_ed25519_export_public(const PKey *, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_ed25519_export_private(const PKey *, ZuSpan<uint8_t> key);
ZtlsAPI bool ed25519_to_x25519(
  ZuBSpan edPublic, ZuSpan<uint8_t> xPublic);

ZtlsAPI bool pkey_x25519_generate(PKey *);
ZtlsAPI bool pkey_x25519_import_public(PKey *, ZuBSpan pubKey);
ZtlsAPI bool pkey_x25519_import_private(PKey *, ZuBSpan key);
ZtlsAPI bool pkey_x25519_export_public(const PKey *, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_x25519_export_private(const PKey *, ZuSpan<uint8_t> key);
ZtlsAPI bool pkey_x25519_agree(
  const PKey *, ZuBSpan peerPublic, ZuSpan<uint8_t> secret);

ZtlsAPI bool pkey_mlkem768_generate(PKey *);
ZtlsAPI bool pkey_mlkem768_import_seed(PKey *, ZuBSpan seed);
ZtlsAPI bool pkey_mlkem768_import_private(PKey *, ZuBSpan prvKey);
ZtlsAPI bool pkey_mlkem768_import_public(PKey *, ZuBSpan pubKey);
ZtlsAPI bool pkey_mlkem768_export_private(
  const PKey *, ZuSpan<uint8_t> prvKey, size_t *length);
ZtlsAPI bool pkey_mlkem768_export_public(
  const PKey *, ZuSpan<uint8_t> pubKey);
ZtlsAPI bool pkey_mlkem768_encapsulate(
  const PKey *, ZuSpan<uint8_t> ciphertext, ZuSpan<uint8_t> secret);
ZtlsAPI bool pkey_mlkem768_decapsulate(
  const PKey *, ZuBSpan ciphertext, ZuSpan<uint8_t> secret);

ZtlsAPI bool pkey_sign(const PKey *, MDType md, ZuBSpan data,
  ZuSpan<uint8_t> sig, size_t *siglen);
ZtlsAPI bool pkey_verify(const PKey *, MDType md, ZuBSpan data, ZuBSpan sig);
using CSRFn = bool (*)(void *, ZuBSpan);
ZtlsAPI bool pkey_csr(const PKey *, void *ctx, CSRFn);

ZtlsAPI CertStore *cert_store_new();
ZtlsAPI void cert_store_free(CertStore *);
ZtlsAPI bool cert_store_load_path(CertStore *, const char *path);
ZtlsAPI bool cert_store_load_file(CertStore *, const char *path);
ZtlsAPI bool cert_store_add_der(CertStore *, ZuBSpan cert);
ZtlsAPI bool cert_store_add_ca(CertStore *, ZuBSpan cert);

ZtlsAPI VerifyCert *verify_cert_new(CertStore *);
ZtlsAPI void verify_cert_free(VerifyCert *);
ZtlsAPI ptls_verify_certificate_t *verify_cert_cb(VerifyCert *);

ZtlsAPI SignCert *sign_cert_new(PKey *);
ZtlsAPI void sign_cert_free(SignCert *);
ZtlsAPI ptls_sign_certificate_t *sign_cert_cb(SignCert *);
ZtlsAPI bool sign_cert_async(SignCert *, bool);

ZtlsAPI PKey *pkey_load_pem(const char *path);
ZtlsAPI bool load_certificates(ptls_context_t *ctx, const char *path);
ZtlsAPI bool load_certificates_der(
  ptls_context_t *, ZuSpan<const ZuBSpan> certs, const PKey *key);

ZtlsAPI TicketKey *ticket_key_new();
ZtlsAPI void ticket_key_free(TicketKey *);
ZtlsAPI ptls_encrypt_ticket_t *ticket_encrypt_cb(TicketKey *);

} // Backend

} // Ztls

#endif /* ZtlsBackend_HH */
