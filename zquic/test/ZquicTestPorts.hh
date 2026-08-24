//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZquicTestPorts_HH
#define ZquicTestPorts_HH

namespace ZquicTestPort {
  enum {
    Sock = 20300,
    SockRebind = 20301,
    Runtime = 20310,
    Interop = 20320,
    InteropEnd = 20329
  };
}

#endif /* ZquicTestPorts_HH */
