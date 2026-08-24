//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZhttpTestPorts_HH
#define ZhttpTestPorts_HH

namespace ZhttpTestPort {
  enum {
    H1Hub = 20400,
    H1HubEnd = 20411,
    H3Hub = 20412,
    H3HubEnd = 20416,
    H2Hub = 20417,
    H2HubEnd = 20422,
    MsgH1TCP = 20423,
    MsgH1TLS = 20424,
    MsgH2 = 20425,
    MsgH3 = 20426,
    ServerIdle = 20427,
    ServerIdleEnd = 20436,
    ClientCancel = 20437,
    ClientCancelEnd = 20445,
    ClientPool = 20446,
    ClientPoolEnd = 20479
  };
}

#endif /* ZhttpTestPorts_HH */
