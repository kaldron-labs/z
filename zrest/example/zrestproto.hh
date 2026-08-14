//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestproto_HH
#define zrestproto_HH

#include "zrestauth.hh"

using PingPath = ZuStringT<"/">;

struct Ping : public ExampleObject, public ZmObject {
  bool ping = false;
};
struct Pong : public ExampleObject, public ZmObject {
  bool pong = false;
};

ZfStruct((Ping, URI), (((ping), (Required)), (Bool)));
ZfStruct((Pong, JSON), (((pong), (Required)), (Bool)));

#endif /* zrestproto_HH */
