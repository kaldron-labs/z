//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls buffer hook integration

#ifndef ZtlsPico_HH
#define ZtlsPico_HH

#include <stdint.h>

#include <zlib/ZtlsLib.hh>

#ifndef Ztls_Fusion
#if defined(__x86_64__) || defined(_M_X64) || \
    defined(__i386__) || defined(_M_IX86)
#define Ztls_Fusion 1
#else
#define Ztls_Fusion 0
#endif
#endif

#include <openssl/crypto.h>
#include <zpicotls.h>
#include <zpicotls/openssl.h>
#if Ztls_Fusion
#include <zpicotls/fusion.h>
#endif

#include <zlib/ZtlsPKOps.hh>
#include <zlib/ZiTx.hh>

template <>
struct ZuTraits<ptls_iovec_t> : public ZuBaseTraits<ptls_iovec_t> {
  enum { IsArray = 1, IsSpan = 1, IsPrimitive = 0 };
  using Elem = uint8_t;

  static uint8_t *data(ptls_iovec_t &v) { return v.base; }
  static const uint8_t *data(const ptls_iovec_t &v) { return v.base; }
  static size_t length(const ptls_iovec_t &v) { return v.len; }
};


namespace Ztls::Pico {

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
      OPENSSL_cleanse(m_ready->data(), m_ready->length);
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
    OPENSSL_cleanse(m_private->data() + length, m_extent - length);
    m_private->length = uint32_t(length);
    m_ready = ZuMv(m_private);
    m_extent = 0;
    return 0;
  }
  void discard() {
    if (m_private) {
      OPENSSL_cleanse(m_private->data(), m_extent);
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

struct CertStore;
struct SignCert;
struct VerifyCert;
struct TicketKey;

ZtlsAPI bool init();
ZtlsAPI ptls_cipher_suite_t *fusion_cipher_suite(uint16_t);

ZtlsAPI CertStore *cert_store_new();
ZtlsAPI void cert_store_free(CertStore *);
ZtlsAPI bool cert_store_load_path(CertStore *, const char *path);
ZtlsAPI bool cert_store_load_file(CertStore *, const char *path);
ZtlsAPI bool cert_store_add_der(CertStore *, ZuBSpan cert);
ZtlsAPI bool cert_store_add_ca(CertStore *, ZuBSpan cert);

ZtlsAPI VerifyCert *verify_cert_new(CertStore *);
ZtlsAPI void verify_cert_free(VerifyCert *);
ZtlsAPI ptls_verify_certificate_t *verify_cert_cb(VerifyCert *);

ZtlsAPI SignCert *sign_cert_new(const PK::Key &);
ZtlsAPI void sign_cert_free(SignCert *);
ZtlsAPI ptls_sign_certificate_t *sign_cert_cb(SignCert *);
ZtlsAPI bool sign_cert_async(SignCert *, bool);

ZtlsAPI bool load_certificates(ptls_context_t *ctx, const char *path);
ZtlsAPI bool load_certificates_der(
  ptls_context_t *, ZuSpan<const ZuBSpan> certs, const PK::Key &key);

ZtlsAPI TicketKey *ticket_key_new();
ZtlsAPI void ticket_key_free(TicketKey *);
ZtlsAPI ptls_encrypt_ticket_t *ticket_encrypt_cb(TicketKey *);


struct Stats {
  uint64_t	origin_alloc = 0;
  uint64_t	internal_alloc = 0;
  uint64_t	origin_free = 0;
  uint64_t	internal_free = 0;
  uint64_t	origin_align_fail = 0;
  uint64_t	origin_ensure_fail = 0;
};

ZtlsAPI void install();
ZtlsAPI Stats stats();
ZtlsAPI void reset_stats();

class ZtlsAPI AeadCtx {
public:
  AeadCtx() = default;
  ~AeadCtx() { clear(); }
  AeadCtx(const AeadCtx &) = delete;
  AeadCtx &operator =(const AeadCtx &) = delete;

  bool init(ptls_aead_algorithm_t *, bool, const void *, const void *);
  void clear();
  ptls_aead_context_t *get() const { return m_ctx; }
  bool valid() const { return m_ctx; }

private:
  void		*m_storage = nullptr;
  ptls_aead_context_t *m_ctx = nullptr;
  uint16_t	m_size = 0;
};

class ZtlsAPI CipherCtx {
public:
  CipherCtx() = default;
  ~CipherCtx() { clear(); }
  CipherCtx(const CipherCtx &) = delete;
  CipherCtx &operator =(const CipherCtx &) = delete;

  bool init(ptls_cipher_algorithm_t *, bool, const void *);
  void clear();
  ptls_cipher_context_t *get() const { return m_ctx; }
  bool valid() const { return m_ctx; }

private:
  void		*m_storage = nullptr;
  ptls_cipher_context_t *m_ctx = nullptr;
  uint16_t	m_size = 0;
};

} // namespace Ztls::Pico

#endif /* ZtlsPico_HH */
