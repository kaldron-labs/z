//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls buffer hook integration

#ifndef ZtlsPico_HH
#define ZtlsPico_HH

#include <stdint.h>

#include <zpicotls.h>

namespace Ztls::Pico {

struct Stats {
  uint64_t	origin_alloc = 0;
  uint64_t	internal_alloc = 0;
  uint64_t	origin_free = 0;
  uint64_t	internal_free = 0;
  uint64_t	origin_align_fail = 0;
  uint64_t	origin_ensure_fail = 0;
  uint64_t	internal_alloc_fail = 0;
};

void install();
Stats stats();
void reset_stats();

class AeadCtx {
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

class CipherCtx {
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
