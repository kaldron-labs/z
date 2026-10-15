//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

template <typename Heap = ZuVoid>
struct PayloadSaga_ : public Heap, public ZdbSaga<PayloadSaga_<Heap>, PayloadDB> {
  using Base = ZdbSaga<PayloadSaga_<Heap>, PayloadDB>;
  using Base::db;
  using Type = ZuStringT<"payloadSagaWithATypeNameThatExceedsBuiltinStringStorage">;
  enum { NPhases = 1 };

  Zdb_::SagaPayload data;

  ZdbSagaPhase(0, o, Update) {
    if (db()) {
      db()->complete = ZuMv(complete);
      db()->entered.post();
    }
    return {};
  }

  PayloadSaga_(Zdb_::SagaPayload data_) : data{ZuMv(data_)} { }
};

using PayloadSaga = PayloadSaga_<ZmHeap<"Zdb.Test.PayloadSaga", PayloadSaga_<>>>;

ZfStruct(, (PayloadSaga, FB),
  (data, (Ctor<0>),	Bytes));
Zfb::AsTable<"zdbtest.fbs.PayloadSaga"> Zfb_Fmt(PayloadSaga *);

ZuAssert((Zdb_::SagaDefValid<PayloadSaga>{}));

ZmRef<ZdbAnySaga<PayloadDB>> payloadSaga(Zdb_::SagaPayload data_) {
  return new PayloadSaga{ZuMv(data_)};
}

Zdb_::SagaDef payloadSagaDef() {
  return PayloadSaga::def();
}

} // zdbtest
