//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

struct EmptyPhases { enum { NPhases = 0 }; };
struct BadOp {
  enum { NPhases = 1 };
  ZdbSagaPhase(0, order, Invalid) { return {}; }
};
struct BadRepeat {
  enum { NPhases = 1 };
  ZdbSagaRepeatPhase(0, order, Insert, 2) { return {}; }
};
ZuAssert((!Zdb_::SagaDefValid<EmptyPhases>{}));
ZuAssert((!Zdb_::SagaDefValid<BadOp>{}));
ZuAssert((!Zdb_::SagaDefValid<BadRepeat>{}));

struct RepeatSaga : public ZmNHeap<RepeatSaga>, public ZdbSaga<RepeatSaga, SagaDB> {
friend inline ZuStringT<"Zdb.Test.RepeatSaga"> ZmNHeapID(RepeatSaga *);

  using Base = ZdbSaga<RepeatSaga, SagaDB>;
  using Type = ZuStringT<"repeatSaga">;
  enum { NPhases = 4 };
  uint64_t first;
  uint64_t second;

  ZdbSagaRepeatPhase(0, order, Insert, first) { return {}; }
  ZdbSagaPhase(1, order, Update) { return {}; }
  ZdbSagaRepeatPhase(2, payment, Delete, second) { return {}; }
  ZdbSagaPhase(3, payment, Update) { return {}; }

  RepeatSaga(uint64_t first_, uint64_t second_) : first{ZuMv(first_)}, second{ZuMv(second_)} { }
};

ZfStruct(, (RepeatSaga, FB),
  (first,	(Ctor<0>),	UInt64),
  (second,	(Ctor<1>),	UInt64));
Zfb::AsTable<"zdbtest.fbs.RepeatSaga"> Zfb_Fmt(RepeatSaga *);

ZuAssert((Zdb_::SagaDefValid<RepeatSaga>{}));

ZmRef<ZdbAnySaga<SagaDB>> repeatSaga(uint64_t first_, uint64_t second_) {
  return new RepeatSaga{ZuMv(first_), ZuMv(second_)};
}

Zdb_::SagaDef repeatSagaDef() {
  return RepeatSaga::def();
}

} // zdbtest
