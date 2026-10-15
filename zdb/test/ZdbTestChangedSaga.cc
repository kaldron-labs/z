//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

template <typename Heap = ZuVoid>
struct ChangedSaga_ : public Heap, public ZdbSaga<ChangedSaga_<Heap>, LiveDB> {
  using Base = ZdbSaga<ChangedSaga_<Heap>, LiveDB>;
  using Type = ZuStringT<"liveSaga">;
  enum { NPhases = 3 };
  uint64_t orderID;
  ZdbSagaPhase(0, o, Insert) { return {}; }
  ZdbSagaPhase(1, o, Delete) { return {}; }
  ZdbSagaPhase(2, o, Delete) { return {}; }

  ChangedSaga_(uint64_t orderID_) : orderID{ZuMv(orderID_)} { }
};

using ChangedSaga = ChangedSaga_<ZmHeap<"Zdb.Test.ChangedSaga", ChangedSaga_<>>>;

ZfStruct(, (ChangedSaga, FB),
  (orderID, (Ctor<0>),		UInt64));
Zfb::AsTable<"zdbtest.fbs.ChangedSaga"> Zfb_Fmt(ChangedSaga *);

ZuAssert((Zdb_::SagaDefValid<ChangedSaga>{}));

ZmRef<ZdbAnySaga<LiveDB>> changedSaga(uint64_t orderID_) {
  return new ChangedSaga{ZuMv(orderID_)};
}

Zdb_::SagaDef changedSagaDef() {
  return ChangedSaga::def();
}

} // zdbtest
