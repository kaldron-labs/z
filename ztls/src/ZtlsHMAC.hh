//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Streaming libcrypto HMAC

#ifndef ZtlsHMAC_HH
#define ZtlsHMAC_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZtlsAlg.hh>

namespace Ztls {

class ZtlsAPI HMAC_ {
public:
  ~HMAC_();
  HMAC_(const HMAC_ &) = delete;
  HMAC_ &operator =(const HMAC_ &) = delete;
  HMAC_(HMAC_ &&) = delete;
  HMAC_ &operator =(HMAC_ &&) = delete;

  void start(ZuBSpan);
  void update(ZuBSpan);
  void finish(ZuSpan<uint8_t>);
  void reset();

protected:
  explicit HMAC_(MDType);

private:
  void clear();
  [[noreturn]] void fail(const char *, ZuSpan<uint8_t> = {});

  EVP_MAC_CTX	*m_ctx = nullptr;
  MDType	m_type;
  unsigned	m_size;
};

template <MDType Type = SHA256> class HMAC : public HMAC_ {
public:
  enum { Size = MDTraits<Type>::Size };

  HMAC() : HMAC_{Type} { }
};

} // Ztls

#endif /* ZtlsHMAC_HH */
