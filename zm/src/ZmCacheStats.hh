//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// cache statistics (used by ZmCache, ZmPolyCache)

#ifndef ZmCacheStats_HH
#define ZmCacheStats_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuPrint.hh>

struct ZmCacheStats {
  unsigned	size;
  unsigned	count;
  uint64_t	loads;
  uint64_t	misses;
  uint64_t	evictions;

  template <typename S> void print(S &s) const {
    s << "size=" << size << " count=" << count <<
      " loads=" << loads << " misses=" << misses <<
      " evictions=" << evictions;
  }
  friend ZuPrintFn ZuPrintType(ZmCacheStats *);
};

#endif /* ZmCacheStats_HH */
