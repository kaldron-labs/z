//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZhttpInteropPorts_HH
#define ZhttpInteropPorts_HH

namespace ZhttpInteropPort {
  enum {
    QIRHQSingle = 20900,
    QIRH3Single = 20901,
    QIRHQMulti = 20902,
    QIRH3Multi = 20903,
    ClientCaddyHTTP = 20910,
    ClientCaddyH1 = 20911,
    ClientCaddyH3 = 20912,
    ServerHTTP = 20913,
    ServerH1 = 20914,
    DarkZ = 20920,
    DarkD = 20921
  };
}

#endif /* ZhttpInteropPorts_HH */
