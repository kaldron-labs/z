//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Message digest identifiers and libcrypto selection

#ifndef ZtlsAlg_HH
#define ZtlsAlg_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <openssl/types.h>

#include <zlib/ZtEnum.hh>

namespace Ztls {

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

template <MDType Type> struct MDTraits;
template <> struct MDTraits<SHA1> { enum { Size = 20 }; };
template <> struct MDTraits<SHA256> { enum { Size = 32 }; };
template <> struct MDTraits<SHA384> { enum { Size = 48 }; };
template <> struct MDTraits<SHA512> { enum { Size = 64 }; };

ZtlsAPI const EVP_MD *evpMD(MDType);

} // Ztls

#endif /* ZtlsAlg_HH */
