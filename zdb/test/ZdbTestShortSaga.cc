//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

struct ShortSaga : public ZmNHeap<ShortSaga>, public ZdbSaga<ShortSaga, LiveDB> {
friend inline ZuStringT<"Zdb.Test.ShortSaga"> ZmNHeapID(ShortSaga *);

  using Base = ZdbSaga<ShortSaga, LiveDB>;
  using Type = ZuStringT<"liveSaga">;
  enum { NPhases = 1 };
  uint64_t orderID;
  ZdbSagaPhase(0, o, Insert) { return {}; }

  ShortSaga(uint64_t orderID_) : orderID{ZuMv(orderID_)} { }
};

ZfStruct(, (ShortSaga, FB),
  (orderID, (Ctor<0>),		UInt64));
Zfb::AsTable<"zdbtest.fbs.ShortSaga"> Zfb_Fmt(ShortSaga *);

ZuAssert((Zdb_::SagaDefValid<ShortSaga>{}));

ZmRef<ZdbAnySaga<LiveDB>> shortSaga(uint64_t orderID_) {
  return new ShortSaga{ZuMv(orderID_)};
}

Zdb_::SagaDef shortSagaDef() {
  return ShortSaga::def();
}

} // zdbtest
