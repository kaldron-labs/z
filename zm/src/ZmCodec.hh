//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// inline on-stack Base64 / Base32 / Hex encoding
// - uses ZmAlloc to allocate a buffer on stack, e.g.
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

#include <zlib/ZmAlloc.hh>

#define ZmCodec_Fn \
template <typename L> \
inline decltype(auto) enc(ZuBSpan src, L &&l) { \
  auto n = enclen(src.length()); \
  auto buf_ = ZmAlloc(uint8_t, n); \
  ZuSpan<uint8_t> buf(&buf_[0], n); \
  buf.trunc(encode(buf, src)); \
  ZuFwd<L>(l)(buf); \
} \
template <typename L> \
inline decltype(auto) dec(ZuBSpan src, L &&l) { \
  auto n = declen(src.length()); \
  auto buf_ = ZmAlloc(uint8_t, n); \
  ZuSpan<uint8_t> buf(&buf_[0], n); \
  buf.trunc(decode(buf, src)); \
  ZuFwd<L>(l)(buf); \
}

namespace ZmBase64 { using namespace ZuBase64; ZmCodec_Fn }
namespace ZmBase32 { using namespace ZuBase32; ZmCodec_Fn }
namespace ZmHex { using namespace ZuHex; ZmCodec_Fn }

#endif /* ZmCodec_HH */
