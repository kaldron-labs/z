//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZiFBReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

template <typename Heap = ZuVoid>
struct ShortSaga_ : public Heap, public ZdbSaga<ShortSaga_<Heap>, LiveDB> {
  using Base = ZdbSaga<ShortSaga_<Heap>, LiveDB>;
  using Type = ZuStringT<"liveSaga">;
  enum { NPhases = 1 };
  uint64_t orderID;
  ZdbSagaPhase(0, o, Insert) { return {}; }

  ShortSaga_(uint64_t orderID_) : orderID{ZuMv(orderID_)} { }
};

using ShortSaga = ShortSaga_<ZmHeap<"Zdb.Test.ShortSaga", ShortSaga_<>>>;

ZfStruct(, (ShortSaga, FB),
  (orderID, (Ctor<0>),		UInt64));
ZiFB::AsTable<"zdbtest.fbs.ShortSaga"> ZiFB_Fmt(ShortSaga *);

ZuAssert((Zdb_::SagaDefValid<ShortSaga>{}));

ZmRef<ZdbAnySaga<LiveDB>> shortSaga(uint64_t orderID_) {
  return new ShortSaga{ZuMv(orderID_)};
}

Zdb_::SagaDef shortSagaDef() {
  return ShortSaga::def();
}

} // zdbtest
