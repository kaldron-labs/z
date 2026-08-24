//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZhttpITestPorts_HH
#define ZhttpITestPorts_HH

namespace ZhttpITestPort {
  enum {
    Matrix = 20500,
    App = 20510,
    AppEnd = 20516,
    MultiRequest = 20520,
    Lifecycle = 20530,
    LifecycleEnd = 20532,
    ClientFallback = 20540,
    ServerStream = 20550,
    ServerStreamEnd = 20569
  };
}

#endif /* ZhttpITestPorts_HH */
