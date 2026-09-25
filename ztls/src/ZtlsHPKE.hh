//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HPKE base mode with the MLKEM768-X25519 hybrid KEM

#ifndef ZtlsHPKE_HH
#define ZtlsHPKE_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <stddef.h>

#include <zlib/ZtlsKEM.hh>

namespace Ztls::HPKE {

  enum { TagSize = 16 }; // ChaCha20-Poly1305 authentication tag

  ZtlsAPI ZuUnion<void, ZeException> sealBase(
    Random &rng, const PK::PK_MLKEM768_X25519 &recipient,
    ZuBSpan info, ZuBSpan aad, ZuBSpan plaintext,
    ZuSpan<uint8_t> enc, ZuSpan<uint8_t> ciphertext);

  ZtlsAPI ZuUnion<size_t, ZeException> openBase(
    const PK::SK_MLKEM768_X25519 &identity,
    ZuBSpan info, ZuBSpan aad, ZuBSpan enc, ZuBSpan ciphertext,
    ZuSpan<uint8_t> plaintext);

}

#endif /* ZtlsHPKE_HH */
