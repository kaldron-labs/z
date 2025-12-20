//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Base64 encode/decode

#ifndef ZuBase64_HH
#define ZuBase64_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSpan.hh>

namespace ZuBase64 {

// UTF8 / ASCII, which is all we care about
static constexpr const uint8_t lookup_[] = {
  62, 0xff, 0xff, 0xff, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
  39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

ZuInline constexpr uint8_t lookup(uint8_t c) {
  c -= 43;
  return c > 122 ? 0xff : lookup_[c];
};

ZuInline constexpr bool is(char c) {
  return lookup(c) != 0xff;
}

// both encode and decode return count of bytes written

ZuInline constexpr uint64_t enclen(uint64_t slen) {
  return ((slen + 2) / 3)<<2;
}
// does not null-terminate dst
inline uint64_t encode(ZuSpan<uint8_t> dst, ZuBSpan src) {
  static constexpr const char lookup[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
  };
  auto s = src.data();
  auto d = dst.data();
  auto n = src.length();
  uint8_t i, j;
  while (n >= 3) {
    i = *s++;
    *d++ = lookup[i>>2];
    j = *s++;
    *d++ = lookup[((i & 0x3)<<4) | (j>>4)];
    i = *s++;
    *d++ = lookup[((j & 0xf)<<2) | (i>>6)];
    *d++ = lookup[i & 0x3f];
    n -= 3;
  }
  if (n > 0) {
    i = *s++;
    *d++ = lookup[i>>2];
    if (n == 1) {
      *d++ = lookup[(i & 0x3)<<4];
      *d++ = '=', *d++ = '=';
    } else { // n == 2
      j = *s++;
      *d++ = lookup[((i & 0x3)<<4) | (j>>4)];
      *d++ = lookup[(j & 0xf)<<2];
      *d++ = '=';
    }
  }
  return d - dst.data();
}

ZuInline constexpr uint64_t declen(uint64_t slen) {
  uint64_t dlen = (slen>>2) * 3;
  if (slen &= 3) dlen += slen - 1;
  return dlen;
}
// does not null-terminate dst
// supports in-place-overwrite decoding (dst == src)
// permits truncated padding
inline uint64_t decode(ZuSpan<uint8_t> dst, ZuBSpan src) {
  auto s = src.data();
  auto d = dst.data();
  auto n = src.length();
  uint8_t i, j;
  while (n >= 2) {
    i = lookup(*s++); if (i >= 64) break;
    j = lookup(*s++); if (j >= 64) break;
    *d++ = (i<<2) | (j>>4);
    if (!(n -= 2)) break;
    i = lookup(*s++); if (i >= 64) break;
    *d++ = (j<<4) | (i>>2);
    if (!--n) break;
    j = lookup(*s++); if (j >= 64) break;
    *d++ = (i<<6) | j;
    --n;
  }
  return d - dst.data();
}

}

#endif /* ZuBase64_HH */
