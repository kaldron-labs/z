//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZgrpcTestAllocator_HH
#define ZgrpcTestAllocator_HH

#include <zlib/ZiMultiplex.hh>

namespace ZgrpcTest {

template <typename Policy> struct Allocator;

template <
  Zi::IOBufType::T Type, typename Buf, unsigned Size, unsigned Align,
  typename HeapID, unsigned VHMaxSize, auto Grow>
struct Allocator<Zi::IOBufBuiltinAllocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>> {
  static Zi::IOBufAllocator *get() {
    static ZiMultiplex mx;
    static auto allocator = mx.allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>();
    return allocator;
  }
};

} // ZgrpcTest

#endif /* ZgrpcTestAllocator_HH */
