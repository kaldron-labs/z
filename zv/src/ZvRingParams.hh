//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ring buffer configuration

#ifndef ZvRingParams_HH
#define ZvRingParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZiRing.hh>

#include <zlib/ZfCf.hh>

struct ZvRingCf {
  ZtString<>	name;
  unsigned	size = 131072;
  bool		ll = false;
  int		spin = 1000;
  int		timeout = 1;	// milliseconds
  int		killWait = 1;	// seconds
  bool		coredump = false;
};

ZfStruct(ZvAPI, (ZvRingCf, Cf),
  (((name),		(Required)),			(String)),
  (((size),		((Range<8192U, 1U<<30U>))),	(UInt32, 131072)),
  (((ll)),					(Bool, false)),
  (((spin),		((Range<0, INT_MAX>))),		(Int32, 1000)),
  (((timeout),		((Range<0, 3600>))),		(Int32, 1)),
  (((killWait),		((Range<0, 3600>))),		(Int32, 1)),
  (((coredump)),				(Bool, false)));

struct ZvRingParams : public ZiRingParams {
  ZvRingParams(const ZfCf::AnyNode *cf) { init(cf); }
  ZvRingParams(const ZfCf::AnyNode *cf, ZiRingParams deflt) :
      ZiRingParams{ZuMv(deflt)} { init(cf); }

  void init(const ZfCf::AnyNode *cf) {
    if (!cf) return;
    auto patch = ZfCf::handler<ZvRingCf>(cf).ctor();
    if (cf->resolve("name")) name(patch.name);
    if (cf->resolve("size")) size(patch.size);
    if (cf->resolve("ll")) ll(patch.ll);
    if (cf->resolve("spin")) spin(patch.spin);
    if (cf->resolve("timeout")) timeout(patch.timeout);
    if (cf->resolve("killWait")) killWait(patch.killWait);
    if (cf->resolve("coredump")) coredump(patch.coredump);
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
