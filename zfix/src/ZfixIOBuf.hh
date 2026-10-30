//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Known pooled I/O buffer backing for FIX-owned retention budgets

#ifndef ZfixIOBuf_HH
#define ZfixIOBuf_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZiIOBuf.hh>

namespace Zfix {

// Known variable-heap backing; excludes allocator bookkeeping and idle caches.
inline uint64_t jumboBytes(unsigned size) {
  uint64_t bytes = uint64_t(size) + ZiIOBuf_Align;
  unsigned i = Zi::VHeap::cacheI(bytes);
  return i < Zi::VHeap::NCaches ? Zi::VHeap::blockSize(i) : bytes;
}
} // Zfix

#endif /* ZfixIOBuf_HH */
