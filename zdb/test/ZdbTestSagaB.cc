//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

template <typename Heap = ZuVoid>
struct SagaB_ : public Heap, public ZdbSaga<SagaB_<Heap>, SagaDB> {
  using Base = ZdbSaga<SagaB_<Heap>, SagaDB>;
  using Base::db;
  using Type = ZuStringT<"sagaB">;
  enum { NPhases = 1 };

  uint32_t value;

  ZdbSagaPhase(0, payment, Update) { return {}; }

  SagaB_(uint32_t value_) : value{ZuMv(value_)} { }
};

using SagaB = SagaB_<ZmHeap<"Zdb.Test.SagaB", SagaB_<>>>;

ZfStruct(, (SagaB, FB),
  (value, (Ctor<0>),		UInt32));
Zfb::AsTable<"zdbtest.fbs.SagaB"> Zfb_Fmt(SagaB *);

ZuAssert((Zdb_::SagaDefValid<SagaB>{}));

ZmRef<ZdbAnySaga<SagaDB>> sagaB(uint32_t value_) {
  return new SagaB{ZuMv(value_)};
}

Zdb_::SagaDef sagaBDef() {
  return SagaB::def();
}

} // zdbtest
