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

#include <limits.h>

#include <zlib/ZiRing.hh>

#include <zlib/ZfCf.hh>

namespace ZiRing_ {

ZfStruct(ZvAPI, (Params, Cf),
  (((name, Fn),		(Mutable, Required)),			String),
  (((size, Fn),		(Mutable, (Range<8192U, INT_MAX>))),	UInt32),
  (((ll, Fn),		(Mutable)),				Bool),
  (((spin, Fn),		(Mutable, (Range<0, INT_MAX>))),	Int32),
  (((timeout, Fn),	(Mutable, (Range<0, 3600>))),		Int32),
  (((killWait, Fn),	(Mutable, (Range<0, 3600>))),		Int32),
  (((coredump, Fn),	(Mutable)),				Bool));

} // ZiRing_

inline ZiRingParams ZvRingParams(
    const ZfCf::AnyNode *cf, ZiRingParams params = {})
{
  if (cf) ZfCf::handler<ZiRingParams>(cf).update(params);
  return params;
}

#endif /* ZvRingParams_HH */
