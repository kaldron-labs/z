//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap-identified map containers for Zf unit tests

#ifndef ZfMapTest_HH
#define ZfMapTest_HH

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>

template <typename Base_, typename Heap = ZuVoid>
struct ZfMapTest_ : public Heap, public Base_ {
  using Base = Base_;
  using Base::Base;
  using Base::operator =;
};

template <ZuString ID, typename Base>
using ZfMapTest =
  ZfMapTest_<Base, ZmHeap<ID, ZfMapTest_<Base>>>;

template <typename Base_>
struct ZfRefMapTest : public ZmObject, public Base_ {
  using Base = Base_;
  using Base::Base;
  using Base::operator =;
};

#endif /* ZfMapTest_HH */
