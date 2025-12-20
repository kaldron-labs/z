//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// thread configuration

#ifndef ZvThreadParams_HH
#define ZvThreadParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZmThread.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZvCf.hh>

namespace ZmThreadPriority {
  using T = int8_t;
  ZtEnumMap(ZmThreadPriority, Map, "RealTime", "High", "Normal", "Low");
}

struct ZvThreadParams : public ZmThreadParams {
  ZvThreadParams(const ZmThreadParams &p) : ZmThreadParams{p} { }
  ZvThreadParams &operator =(const ZmThreadParams &p) {
    ZmThreadParams::operator =(p);
    return *this;
  }
  ZvThreadParams(ZmThreadParams &&p) : ZmThreadParams{ZuMv(p)} { }
  ZvThreadParams &operator =(ZmThreadParams &&p) {
    ZmThreadParams::operator =(ZuMv(p));
    return *this;
  }

  ZvThreadParams(const ZvCf *cf) { init(cf); }
  ZvThreadParams(const ZvCf *cf, ZmThreadParams deflt) :
      ZmThreadParams{ZuMv(deflt)} { init(cf); }

  void init(const ZvCf *cf) {
    ZmThreadParams::operator =(ZmThreadParams{});

    if (!cf) return;

    static unsigned ncpu = Zm::getncpu();
    stackSize(cf->getInt("stackSize", 16384, 2<<20, stackSize()));
    priority(cf->getEnum<ZmThreadPriority::Map, int>(
	  "priority", ZmThreadPriority::Normal));
    partition(cf->getInt("partition", 0, ncpu - 1, 0));
    cpuset(cf->get("cpuset"));
  }
};

#endif /* ZvThreadParams_HH */
