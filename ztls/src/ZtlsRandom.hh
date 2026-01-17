//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// backend RNG wrapper

#ifndef ZtlsRandom_HH
#define ZtlsRandom_HH

#include <zlib/ZtlsLib.hh>

#include <zlib/ZtlsBackend.hh>

namespace Ztls {

class Random {
public:
  Random() { }
  ~Random() { }

  bool init() {
    return Backend::init();
  }

  bool random(ZuSpan<uint8_t> data) {
    return Backend::random_bytes(data);
  }
};

}

#endif /* ZtlsRandom_HH */
