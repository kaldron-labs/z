//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap-identified object wrapper for Zf unit tests

#ifndef ZfHeapTest_HH
#define ZfHeapTest_HH

#include <zlib/ZuDerive.hh>
#include <zlib/ZuString.hh>

#include <zlib/ZmHeap.hh>

template <ZuString ID, typename Base_>
struct ZfHeapTest : public ZmNHeap<ZfHeapTest<ID, Base_>>, public Base_ {
friend inline ZuStringT<ID> ZmNHeapID(ZfHeapTest *);

  using Base = Base_;
  using Base::Base;
  using Base::operator =;
};

#endif /* ZfHeapTest_HH */
