//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Hex encode/decode

#ifndef ZuHex_HH
#define ZuHex_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSpan.hh>

struct ZuHex {

static constexpr const char upper_[] = "0123456789ABCDEF";
static constexpr const char lower_[] = "0123456789abcdef";

template <bool Upper = true>
ZuInline static constexpr uint8_t lookup(uint8_t c) {
  constexpr uint8_t alpha = Upper ? 'A' : 'a';
  return 
    (c >= alpha && c <= alpha + 5) ? (c - alpha) + 10 :
    (c >= '0' && c <= '9') ? c - '0' : 0xff;
}

template <bool Upper = true>
ZuInline static constexpr bool is(char c) {
  return lookup<Upper>(c) != 0xff;
}

// both encode and decode return count of bytes written

// does not null-terminate dst
template <bool Upper = true>
ZuInline static constexpr unsigned enclen(unsigned slen) { return slen<<1; }
template <bool Upper = true>
static inline unsigned encode(ZuSpan<uint8_t> dst, ZuBSpan src) {
  constexpr auto lookup = Upper ? upper_ : lower_;
  auto s = src.data();
  auto d = dst.data();
  auto n = src.length();
  uint8_t i;
  while (n > 0) {
    i = *s++;
    *d++ = lookup[i>>4];
    *d++ = lookup[i & 0xf];
    --n;
  }
  return d - dst.data();
}

template <bool Upper = true>
ZuInline static constexpr unsigned declen(unsigned slen) {
  return (slen + 1)>>1;
}
// does not null-terminate dst
// supports in-place-overwrite decoding (dst == src)
template <bool Upper = true>
static inline unsigned decode(ZuSpan<uint8_t> dst, ZuBSpan src) {
  auto s = src.data();
  auto d = dst.data();
  auto n = src.length();
  uint8_t i, j;
  while (n >= 2) {
    i = lookup<Upper>(*s++); if (i >= 16) break;
    j = lookup<Upper>(*s++); if (j >= 16) break;
    *d++ = (i<<4) | j;
    n -= 2;
  }
  return d - dst.data();
}

};

#endif /* ZuHex_HH */
