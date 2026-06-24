//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// safe alloca() smart pointer that stack allocates if requested size
// is less than 50% of the remaining stack space, falling back to ZmVHeap
//
// WARNING: ZmVAlloc(T, N) is a macro that evaluates N multiple times

#ifndef ZmVAlloc_HH
#define ZmVAlloc_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmVHeap.hh>

#define ZmVAlloc(A, T, n) \
  ZmAlloc_<T>{static_cast<T *>(!(n) ? nullptr : \
    (((ZmStackAvail()>>1) < ((n) * sizeof(T) + alignof(T))) ? \
      A::VHeap::valloc((n) * sizeof(T)) : \
	ZuAlloca((n) * sizeof(T), alignof(T))))}

#endif /* ZmVAlloc_HH */
