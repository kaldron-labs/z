//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls buffer hook integration

#ifndef ZtlsPico_HH
#define ZtlsPico_HH

#include <stddef.h>
#include <stdint.h>

#include <zlib/ZtlsLib.hh>

#include <zpicotls.h>

namespace Ztls::Pico {

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
// Export the dependency operations used by public Ztls templates.
ZtlsAPI ptls_t *create(ptls_context_t *ctx, int server);
ZtlsAPI void free(ptls_t *tls);
ZtlsAPI void free(ptls_tx_t *tx);
ZtlsAPI ptls_async_job_t *asyncJob(ptls_t *tls);
ZtlsAPI ptls_cipher_suite_t *cipher(ptls_t *tls);
ZtlsAPI ptls_cipher_suite_t *cipher(ptls_tx_t *tx);
ZtlsAPI void **data(ptls_t *tls);
ZtlsAPI uint16_t version(ptls_t *tls);
ZtlsAPI size_t overhead(ptls_t *tls);
ZtlsAPI const char *alpn(ptls_t *tls);
ZtlsAPI int alpn(ptls_t *tls, const char *name, size_t length);
ZtlsAPI int serverName(ptls_t *tls, const char *name, size_t length);
ZtlsAPI int handshook(ptls_t *tls);
ZtlsAPI int psk(ptls_t *tls);
ZtlsAPI int handshake(ptls_t *tls, ptls_buffer_t *buf, const void *input, size_t *length,
  ptls_handshake_properties_t *props);
ZtlsAPI int receive(ptls_t *tls, ptls_buffer_t *buf, const void *input, size_t *length);
ZtlsAPI int send(ptls_t *tls, ptls_buffer_t *buf, const void *input, size_t length);
ZtlsAPI int send(ptls_tx_t *tx, ptls_buffer_t *buf, const void *input, size_t length);
ZtlsAPI int detachTx(ptls_t *tls, ptls_tx_t **tx);
ZtlsAPI int takeKeyUpdate(ptls_t *tls);
ZtlsAPI int updateKey(ptls_tx_t *tx, ptls_buffer_t *buf, int request);
ZtlsAPI int sendAlert(ptls_tx_t *tx, ptls_buffer_t *buf, uint8_t level, uint8_t description);
ZtlsAPI ptls_get_time_t *time();

ZtlsAPI int sendv(ptls_tx_t *, ptls_buffer_t *, ptls_iovec_t *, size_t);

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
