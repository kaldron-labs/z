//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZuID is a by-value short string
// - intended for human-readable unique identifiers and names
// - used by Z to name threads, hash tables, heaps, queues, multiplexers, ...

#ifndef ZuID_HH
#define ZuID_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuArray.hh>

#define ZuIDSize 60
using ZuID = ZuCArray<ZuIDSize>;

#endif /* ZuID_HH */
