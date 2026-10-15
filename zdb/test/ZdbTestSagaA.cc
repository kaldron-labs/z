//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

template <typename Heap = ZuVoid>
struct SagaA_ : public Heap, public ZdbSaga<SagaA_<Heap>, SagaDB> {
  using Base = ZdbSaga<SagaA_<Heap>, SagaDB>;
  using Base::db;
  using Type = ZuStringT<"sagaA">;
  enum { NPhases = 3 };

  uint64_t value;

  ZdbSagaPhase(0, order, Insert) {
    complete(true);
    return {};
  }
  ZdbSagaPhase(1, order, Update) { return {}; }
  ZdbSagaPhase(2, payment, Delete) { return {}; }

  SagaA_(uint64_t value_) : value{ZuMv(value_)} { }
  ~SagaA_() override;
};

using SagaA = SagaA_<ZmHeap<"Zdb.Test.SagaA", SagaA_<>>>;

ZfStruct(, (SagaA, FB),
  (value, (Ctor<0>),		UInt64));
Zfb::AsTable<"zdbtest.fbs.SagaA"> Zfb_Fmt(SagaA *);

ZuAssert((Zdb_::SagaDefValid<SagaA>{}));

static ZmAtomic<unsigned> destroyed = 0;
template <typename Heap>
SagaA_<Heap>::~SagaA_() { ++destroyed; }
unsigned sagaADestroyed() { return destroyed.load_(); }

ZmRef<ZdbAnySaga<SagaDB>> sagaA(uint64_t value_) {
  return new SagaA{ZuMv(value_)};
}

Zdb_::SagaDef sagaADef() {
  return SagaA::def();
}

} // zdbtest
