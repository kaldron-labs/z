//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private client serial probe loop

#ifndef ZdbusSerial_HH
#define ZdbusSerial_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <stdint.h>

namespace Zdbus_ {

// The client owns the counter and pending table; this is only its probe loop.
template <typename Busy>
uint32_t serialProbe(uint32_t &next, unsigned probes, Busy &&busy)
{
  while (probes--) {
    uint32_t serial = next++;
    if (!next) next = 1;
    if (serial && !busy(serial)) return serial;
  }
  return 0;
}

} // Zdbus_

#endif /* ZdbusSerial_HH */
