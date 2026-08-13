//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "zrestproto.hh"

bool parseDuration(ZuCSpan value, bool allowZero, uint64_t &seconds)
{
  unsigned length = value.length();
  if (length < 2) return false;
  uint64_t n = 0;
  for (unsigned i = 0; i + 1 < length; ++i) {
    unsigned digit = unsigned(uint8_t(value[i]) - uint8_t('0'));
    if (digit > 9 || n > (UINT64_MAX - digit) / 10) return false;
    n = n * 10 + digit;
  }
  uint64_t multiplier;
  switch (value[length - 1]) {
    case 's': multiplier = 1; break;
    case 'm': multiplier = 60; break;
    case 'h': multiplier = 60 * 60; break;
    default: return false;
  }
  if ((!n && !allowZero) || n > UINT64_MAX / multiplier) return false;
  seconds = n * multiplier;
  return true;
}
