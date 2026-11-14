//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Streaming libcrypto message digests

#ifndef ZtlsMD_HH
#define ZtlsMD_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZtlsAlg.hh>

namespace Ztls {

class ZtlsAPI MD_ {
public:
  ~MD_();
  MD_(const MD_ &) = delete;
  MD_ &operator =(const MD_ &) = delete;
  MD_(MD_ &&) = delete;
  MD_ &operator =(MD_ &&) = delete;

  void update(ZuBSpan);
  void finish(ZuSpan<uint8_t>);
  void reset();

protected:
  explicit MD_(MDType);

private:
  void clear();
  [[noreturn]] void fail(const char *, ZuSpan<uint8_t> = {});

  EVP_MD_CTX	*m_ctx = nullptr;
  EVP_MD_CTX	*m_snapshot = nullptr;
  MDType	m_type;
  unsigned	m_size;
};

template <MDType Type = SHA256> class MD : public MD_ {
public:
  enum { Size = MDTraits<Type>::Size };

  MD() : MD_{Type} { }
};

ZtlsAPI bool shake256(ZuBSpan, ZuSpan<uint8_t>);
ZtlsAPI bool sha3_256(ZuSpan<const ZuBSpan>, ZuSpan<uint8_t>);

} // Ztls

#endif /* ZtlsMD_HH */
