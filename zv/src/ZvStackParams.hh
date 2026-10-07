//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmStack configuration

#ifndef ZvStackParams_HH
#define ZvStackParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZmStack.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvCSV.hh>

ZfStruct(ZvAPI, (ZmStackParams, Cf),
  ((initial, Fn),	(Mutable, (Range<2U, 28U>)),		UInt32),
  ((maxFrag, Fn),	(Mutable, (Range<1.0, 256.0>)),		Float));

inline ZmStackParams ZvStackParams(
    const ZfCf::AnyNode *cf, ZmStackParams params = {})
{
  if (cf) ZfCf::handler<ZmStackParams>(cf).update(params);
  return params;
}

#endif /* ZvStackParams_HH */
