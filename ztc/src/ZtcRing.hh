//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// shared telemetry and control ring

#ifndef ZtcRing_HH
#define ZtcRing_HH

#ifndef ZtcLib_HH
#include <zlib/ZtcLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZiRing.hh>

#include <zlib/ZtcMsg.hh>

namespace Ztc {

inline unsigned ringSize(const void *ptr) {
  auto hdr = static_cast<const Hdr *>(ptr);
  return sizeof(Hdr) + unsigned(hdr->length);
}

ZuDerive(Ring, (ZiRing<ZmRingSizeAxor<ringSize, ZmRingMW<true>>>));

} // Ztc

#endif /* ZtcRing_HH */
