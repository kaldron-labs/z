//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private bounded stop-callback dispatch

#ifndef ZdbusStop_HH
#define ZdbusStop_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZdbusConnection.hh>

namespace Zdbus_ {

void dispatchStops(ZmScheduler *, unsigned sid, unsigned budget,
  CxnStopQueue);

} // Zdbus_

#endif /* ZdbusStop_HH */
