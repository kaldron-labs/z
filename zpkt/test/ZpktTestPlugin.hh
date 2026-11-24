//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifndef ZpktTestPlugin_HH
#define ZpktTestPlugin_HH
#include <zlib/Zpkt.hh>
namespace ZpktTest {
struct Stats {
  unsigned queries = 0, creates = 0, finals = 0, destroys = 0;
  unsigned pluginFinals = 0, pluginDestroys = 0;
  unsigned spin = 0;
  unsigned failFinals = 0, finalAttempts = 0;
  unsigned events = 0, lastChildFinal = 0, pluginFinalOrder = 0;
  Zpkt::NifInfo info;
};
using StatsFn = Stats *(*)();
}
#endif
