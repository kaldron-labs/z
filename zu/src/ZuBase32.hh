//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Base32 encode/decode

#ifndef ZuBase32_HH
#define ZuBase32_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSpan.hh>

struct ZuBase32 {

// UTF8 / ASCII, which is all we care about
static constexpr const uint8_t lookup_[] = {
  26, 27, 28, 29, 30, 31, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25
};

ZuInline static constexpr uint8_t lookup(uint8_t c) {
  c -= 50;
  return c > 40 ? 0xff : lookup_[c];
};

ZuInline static constexpr bool is(char c) {
  return lookup(c) != 0xff;
}

// both encode and decode return count of bytes written

// does not null-terminate dst
ZuInline static constexpr uint64_t enclen(uint64_t slen) {
  return ((slen + 4)/5)<<3;
}
static inline uint64_t encode(ZuSpan<uint8_t> dst, ZuBSpan src) {
  static constexpr const char lookup[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '2', '3', '4', '5', '6', '7'
  };
  auto s = src.data();
  auto d = dst.data();
  auto n = src.length();
  uint8_t i, j;
  while (n >= 5) {
    i = *s++;
    *d++ = lookup[i>>3];
    j = *s++;
    *d++ = lookup[((i & 0x7)<<2) | (j>>6)];
    *d++ = lookup[(j & 0x3e)>>1];
    i = *s++;
    *d++ = lookup[((j & 0x1)<<4) | (i>>4)];
    j = *s++;
    *d++ = lookup[((i & 0xf)<<1) | (j>>7)];
    *d++ = lookup[(j & 0x7c)>>2];
    i = *s++;
    *d++ = lookup[((j & 0x3)<<3) | (i>>5)];
    *d++ = lookup[i & 0x1f];
    n -= 5;
  }
  if (n > 0) {
    i = *s++;
    *d++ = lookup[i>>3];
    if (n > 1) {
      j = *s++;
      *d++ = lookup[((i & 0x7)<<2) | (j>>6)];
      *d++ = lookup[(j & 0x3e)>>1];
      if (n > 2) {
	i = *s++;
	*d++ = lookup[((j & 0x1)<<4) | (i>>4)];
	if (n > 3) {
	  j = *s++;
	  *d++ = lookup[((i & 0xf)<<1) | (j>>7)];
	  *d++ = lookup[(j & 0x7c)>>2];
	  *d++ = lookup[(j & 0x3)<<3];
	  *d++ = '=';
	} else {
	  *d++ = lookup[((i & 0xf)<<1)];
	  *d++ = '=', *d++ = '=', *d++ = '=';
	}
      } else {
	*d++ = lookup[(j & 0x1)<<4];
	*d++ = '=', *d++ = '=', *d++ = '=', *d++ = '=';
      }
    } else {
      *d++ = lookup[(i & 0x7)<<2];
      memcpy(d, "======", 6);
      d += 6;
    }
  }
  return d - dst.data();
}

// does not null-terminate dst
ZuInline static constexpr uint64_t declen(uint64_t slen) {
  return ((slen + 7)>>3)*5;
}
// supports in-place-overwrite decoding
static inline uint64_t decode(ZuSpan<uint8_t> dst, ZuBSpan src) {
  auto s = src.data();
  auto d = dst.data();
  auto n = src.length();
  uint8_t i, j, k;
  while (n >= 8) {
    i = lookup(*s++); if (i >= 32) break;
    j = lookup(*s++); if (j >= 32) break;
    *d++ = (i<<3) | (j>>2);
    k = lookup(*s++); if (k >= 32) break;
    i = lookup(*s++); if (i >= 32) break;
    *d++ = (j<<6) | (k<<1) | (i>>4);
    j = lookup(*s++); if (j >= 32) break;
    *d++ = (i<<4) | (j>>1);
    k = lookup(*s++); if (k >= 32) break;
    i = lookup(*s++); if (i >= 32) break;
    *d++ = (j<<7) | (k<<2) | (i>>3);
    j = lookup(*s++); if (j >= 32) break;
    *d++ = (i<<5) | j;
    n -= 8;
  }
  return d - dst.data();
}

};

#endif /* ZuBase32_HH */
