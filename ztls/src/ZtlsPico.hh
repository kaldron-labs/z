//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zpicotls buffer hook integration

#ifndef ZtlsPico_HH
#define ZtlsPico_HH

#include <stdint.h>

namespace Ztls::Pico {

struct Stats {
  uint64_t	origin_alloc = 0;
  uint64_t	internal_alloc = 0;
  uint64_t	origin_free = 0;
  uint64_t	internal_free = 0;
  uint64_t	origin_align_fail = 0;
  uint64_t	origin_ensure_fail = 0;
  uint64_t	internal_alloc_fail = 0;
};

void install();
Stats stats();
void reset_stats();

} // namespace Ztls::Pico

#endif /* ZtlsPico_HH */
