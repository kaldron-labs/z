//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// common telemetry types

#ifndef ZtcTypes_HH
#define ZtcTypes_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmFn_.hh>

namespace Ztc {

namespace RAG {
  using T = int8_t;
  enum { Off = 0, Red, Amber, Green };
}

using AllFnHeapID = ZmFnHeapID<"Ztc.AllFn">;
using WatchFnHeapID = ZmFnHeapID<"Ztc.WatchFn">;

} // Ztc

#endif /* ZtcTypes_HH */
