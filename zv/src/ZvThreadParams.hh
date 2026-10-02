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

#include <limits.h>

#include <zlib/ZmThread.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZfCf.hh>

namespace ZmThreadPriority {
  using T = int8_t;
  ZtEnumMap(ZvAPI, ZmThreadPriority, Map, "RealTime", "High", "Normal", "Low");
}

ZfStruct(ZvAPI, (ZmThreadParams, Cf),
  (((stackSize, Fn),	(Mutable, (Range<16384U, INT_MAX>))),		UInt32),
  (((priority, Fn),	(Mutable, (Enum<ZmThreadPriority::Map>))),	Int32),
  (((partition, Fn),	(Mutable)),					UInt32),
  (((cpuset, Fn),	(Mutable)),					String));

inline ZmThreadParams ZvThreadParams(
    const ZfCf::AnyNode *cf, ZmThreadParams params = {})
{
  if (cf) ZfCf::handler<ZmThreadParams>(cf).update(params);
  return params;
}

#endif /* ZvThreadParams_HH */
