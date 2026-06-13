//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 HPACK support

#ifndef ZhttpHPack_HH
#define ZhttpHPack_HH

#include <zlib/ZuBitStream.hh>

namespace Zhttp { namespace H3 {

namespace HPack {

  using BitWriter = ZuBitStream::BE::Writer;
  using BitReader = ZuBitStream::BE::Reader;

  ZuInline constexpr uint64_t enclen(uint64_t slen) {
    return (slen>>2)*15U + (((slen & 3U)*30U + 7U)>>3);
  }
  uint64_t encode(ZuSpan<uint8_t>, ZuBSpan);

  ZuInline constexpr uint64_t declen(uint64_t slen) {
    return ((slen / 5U)<<3) + (((slen % 5U)<<3)/5U);
  }
  int64_t decode(ZuSpan<uint8_t>, ZuBSpan);

} // HPack

}} // namespace Zhttp::H3

#endif /* ZhttpHPack_HH */
