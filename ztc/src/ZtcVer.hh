//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// telemetry protocol version arithmetic

#ifndef ZtcVer_HH
#define ZtcVer_HH

#ifndef ZtcLib_HH
#include <zlib/ZtcLib.hh>
#endif

#include <stdint.h>

namespace Ztc::Ver {
  enum { MinorBase = 1000, MajorBase = 100000 };

  constexpr uint32_t make(unsigned major, unsigned minor, unsigned patch) {
    return major * MajorBase + minor * MinorBase + patch;
  }
  constexpr unsigned major(uint32_t version) { return version / MajorBase; }
  constexpr unsigned minor(uint32_t version) {
    return (version % MajorBase) / MinorBase;
  }
  constexpr unsigned patch(uint32_t version) { return version % MinorBase; }
  constexpr bool compatible(uint32_t receiver, uint32_t sender) {
    return major(receiver) == major(sender) && minor(receiver) >= minor(sender);
  }
}

#endif /* ZtcVer_HH */
