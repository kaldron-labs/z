//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// thread configuration

#ifndef ZvThreadParams_HH
#define ZvThreadParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZmThread.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZfCf.hh>

namespace ZmThreadPriority {
  using T = int8_t;
  ZtEnumMap(ZmThreadPriority, Map, "RealTime", "High", "Normal", "Low");
}

struct ZvThreadCf {
  unsigned	stackSize = 0;
  int		priority = ZmThreadPriority::Normal;
  unsigned	partition = 0;
  ZtString<>	cpuset;
};

ZfStruct((ZvThreadCf, Cf),
  (((stackSize),	((Range<16384U, 2U<<20U>))),	(UInt32)),
  (((priority),		(Enum<ZmThreadPriority::Map>)),	(Int32,
      ZmThreadPriority::Normal)),
  (((partition)),				(UInt32)),
  (((cpuset)),					(String)));

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

  ZvThreadParams(const ZfCf::AnyNode *cf) { init(cf); }
  ZvThreadParams(const ZfCf::AnyNode *cf, ZmThreadParams deflt) :
      ZmThreadParams{ZuMv(deflt)} { init(cf); }

  void init(const ZfCf::AnyNode *cf) {
    if (!cf) return;
    auto patch = ZfCf::handler<ZvThreadCf>(cf).ctor();
    static unsigned ncpu = Zm::getncpu();
    if (cf->resolve("stackSize")) stackSize(patch.stackSize);
    if (cf->resolve("priority")) priority(patch.priority);
    if (cf->resolve("partition")) {
      if (ZuUnlikely(patch.partition >= ncpu))
	throw ZeEXCEPT(Error, "ZvThreadParams", ([
	  key = ZfCfError::fullKey(cf, "partition"), max = ncpu - 1
	](auto &s) {
	  s << '"' << key << "\": expected range [0, " << max << ']';
	}));
      partition(patch.partition);
    }
    if (cf->resolve("cpuset")) cpuset(patch.cpuset);
  }

};

#endif /* ZvThreadParams_HH */
