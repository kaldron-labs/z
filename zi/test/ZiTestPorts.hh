//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZiTestPorts_HH
#define ZiTestPorts_HH

namespace ZiTestPort {
  enum {
    IP = 20000,
    Resolver = 20010,
    EventLoop = 20020,
    MxLoopTCP4 = 20030,
    MxLoopTCP6 = 20031,
    MxLoopUDP4 = 20032,
    MxLoopUDP6 = 20033
  };
}

#endif /* ZiTestPorts_HH */
