//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// message digest wrapper (picotls/OpenSSL backend)

#ifndef ZtlsMD_HH
#define ZtlsMD_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZmAssert.hh>
#include <zlib/ZuSpan.hh>

namespace Ztls {

template <MDType Type> struct MDTraits;
template <> struct MDTraits<SHA1> { enum { Size = 20 }; };
template <> struct MDTraits<SHA256> { enum { Size = 32 }; };
template <> struct MDTraits<SHA384> { enum { Size = 48 }; };
template <> struct MDTraits<SHA512> { enum { Size = 64 }; };

template <MDType Type = SHA256> class MD {
public:
  enum { Size = MDTraits<Type>::Size };

  MD() {
    m_ctx = Backend::hash_algorithm(Type)->create();
  }
  ~MD() { dispose_(); }

  ZuInline void update(ZuBSpan a) {
    m_ctx->update(m_ctx, a.data(), a.length());
  }

  ZuInline void finish(ZuSpan<uint8_t> output) {
    ZmAssert(output.length() >= Size);
    m_ctx->final(m_ctx, &output[0], PTLS_HASH_FINAL_MODE_SNAPSHOT);
  }

  ZuInline void reset() {
    uint8_t tmp[Size];
    m_ctx->final(m_ctx, tmp, PTLS_HASH_FINAL_MODE_RESET);
  }

private:
  void dispose_() {
    if (!m_ctx) return;
    uint8_t tmp[Size];
    m_ctx->final(m_ctx, tmp, PTLS_HASH_FINAL_MODE_FREE);
    m_ctx = nullptr;
  }

  ptls_hash_context_t	*m_ctx = nullptr;
};

}

#endif /* ZtlsMD_HH */
