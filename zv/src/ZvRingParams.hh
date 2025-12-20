//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ring buffer configuration

#ifndef ZvRingParams_HH
#define ZvRingParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZiRing.hh>

#include <zlib/ZvCf.hh>

struct ZvRingParams : public ZiRingParams {
  ZvRingParams(const ZvCf *cf) { init(cf); }
  ZvRingParams(const ZvCf *cf, ZiRingParams deflt) :
      ZiRingParams{ZuMv(deflt)} { init(cf); }

  void init(const ZvCf *cf) {
    if (!cf) return;
    name(cf->get<true>("name"));
    size(cf->getInt("size", 8192, (1U<<30U), 131072));
    ll(cf->getBool("ll"));
    spin(cf->getInt("spin", 0, INT_MAX, 1000));
    timeout(cf->getInt("timeout", 0, 3600, 1));
    killWait(cf->getInt("killWait", 0, 3600, 1));
    coredump(cf->getBool("coredump"));
  }

  ZvRingParams() = default;
  ZvRingParams(const ZiRingParams &p) : ZiRingParams{p} { }
  ZvRingParams &operator =(const ZiRingParams &p) {
    ZiRingParams::operator =(p);
    return *this;
  }
  ZvRingParams(ZiRingParams &&p) : ZiRingParams{ZuMv(p)} { }
  ZvRingParams &operator =(ZiRingParams &&p) {
    ZiRingParams::operator =(ZuMv(p));
    return *this;
  }
};

#endif /* ZvRingParams_HH */
