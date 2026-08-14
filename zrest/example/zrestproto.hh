//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestproto_HH
#define zrestproto_HH

#include <zlib/ZfURI.hh>

#include "zrestauth.hh"

using PingPath = ZuStringT<"/">;

template <typename Heap>
struct Ping_ : public Heap, public ZmObject {
  bool ping = false;
};
using Ping_Heap = ZmHeap<"zrest.Ping", Ping_<ZuVoid>>;
ZuDerive(Ping, (Ping_<Ping_Heap>));

template <typename Heap>
struct Pong_ : public Heap, public ZmObject {
  bool pong = false;
};
using Pong_Heap = ZmHeap<"zrest.Pong", Pong_<ZuVoid>>;
ZuDerive(Pong, (Pong_<Pong_Heap>));

ZfStruct((Ping, URI), (((ping), (Required)), (Bool)));
ZfStruct((Pong, JSON), (((pong), (Required)), (Bool)));

#endif /* zrestproto_HH */
