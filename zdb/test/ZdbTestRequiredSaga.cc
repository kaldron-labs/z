//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

template <typename Heap = ZuVoid>
struct RequiredSaga_ : public Heap, public ZdbSaga<RequiredSaga_<Heap>, PayloadDB> {
  using Base = ZdbSaga<RequiredSaga_<Heap>, PayloadDB>;
  using Base::db;
  using Type = ZuStringT<"requiredSaga">;
  enum { NPhases = 1 };
  ZtString<> name;

  ZdbSagaPhase(0, o, Update) {
    ++db()->runs;
    complete(false);
    return {};
  }

  RequiredSaga_(ZuCSpan name_) : name{ZuMv(name_)} { }
};

using RequiredSaga = RequiredSaga_<ZmHeap<"Zdb.Test.RequiredSaga", RequiredSaga_<>>>;

ZfStruct(, (RequiredSaga, FB), (name, (Ctor<0>, Required), String));
Zfb::AsTable<"zdbtest.fbs.RequiredSaga"> Zfb_Fmt(RequiredSaga *);

ZuAssert((Zdb_::SagaDefValid<RequiredSaga>{}));

ZmRef<ZdbAnySaga<PayloadDB>> requiredSaga(const char * name_) {
  return new RequiredSaga{ZuMv(name_)};
}

Zdb_::SagaDef requiredSagaDef() {
  return RequiredSaga::def();
}

} // zdbtest
