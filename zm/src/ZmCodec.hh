//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// inline on-stack Base64 / Base32 / Hex encoding
// - uses ZmScratch to allocate a fixed-capacity buffer on stack, e.g.
// - ZmBase64::encode(data, [](ZuCSpan s) { ... });

#ifndef ZmCodec_HH
#define ZmCodec_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase32.hh>
#include <zlib/ZuHex.hh>

#include <zlib/ZmScratch.hh>

#define ZmCodec_Fn \
template <typename L> \
static inline decltype(auto) enc(ZuBSpan src, L &&l) { \
  unsigned n = enclen(src.length()); \
  auto buf = ZmScratch(uint8_t, n); \
  buf.length(encode(buf.span(), src)); \
  ZuFwd<L>(l)(buf.cspan()); \
} \
template <typename L> \
static inline decltype(auto) dec(ZuBSpan src, L &&l) { \
  unsigned n = declen(src.length()); \
  auto buf = ZmScratch(uint8_t, n); \
  buf.length(decode(buf.span(), src)); \
  ZuFwd<L>(l)(buf.cspan()); \
}

struct ZmBase64 : public ZuBase64 { ZmCodec_Fn };
struct ZmBase32 : public ZuBase32 { ZmCodec_Fn };
struct ZmHex : public ZuHex { ZmCodec_Fn };

#endif /* ZmCodec_HH */
