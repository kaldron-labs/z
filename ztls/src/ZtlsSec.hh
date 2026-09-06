//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// security-sensitive byte comparisons

#ifndef ZtlsSec_HH
#define ZtlsSec_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

namespace Ztls {

class Random;

// constant-time equal
ZtlsExtern bool ctEqual(ZuBSpan, ZuBSpan);

namespace SecretHash {
  enum {
    Version = 1,
    SaltSize = 16,
    DigestSize = 32,
    Size = 1 + SaltSize + DigestSize
  };
}

ZtlsExtern bool secretHash(Random &, ZuBSpan, ZuSpan<uint8_t>);
ZtlsExtern bool secretVerify(ZuBSpan, ZuBSpan);

}

#endif /* ZtlsSec_HH */
