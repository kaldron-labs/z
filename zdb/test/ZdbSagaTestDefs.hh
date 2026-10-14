//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbSagaTestDefs_HH
#define ZdbSagaTestDefs_HH

#include <zlib/Zdb.hh>
#include <zlib/ZmSemaphore.hh>
#include "ZdbTest.hh"

namespace zdbtest {
struct SagaDB : public ZdbSagaDB { };

struct OtherDB : public ZdbSagaDB { };

namespace Trace {
  enum { Intent, Enter, Commit, Return, Probe, Next, Read };
}

using PauseFn = ZmFn<void(bool), ZmFnHeapID<"Zdb.Saga.Test.Pause">>;

struct LiveDB : public ZdbSagaDB {
  ZmFn<void(unsigned, unsigned)> trace;
  ZdbTable<Order>	*orders = nullptr;
  ZmSemaphore	*done = nullptr;
  ZmSemaphore	*paused = nullptr;
  ZmSemaphore	*reverseFailed = nullptr;
  unsigned	runs = 0;
  unsigned	inserts = 0;
  unsigned	updates = 0;
  unsigned	deletes = 0;
  unsigned	completed = 0;
  unsigned	errors = 0;
  unsigned	reads = 0;
  ZuTime	deadline;
  bool		read = false;
  bool		secondary = false;
  uint32_t	pauseStep = UINT32_MAX;
  uint32_t	skipStep = UINT32_MAX;
  bool		pausedOnce = false;
  bool		pauseAll = false;
  uint32_t	failStep = UINT32_MAX;
  bool		absentReverse = false;
  bool		failReverse = false;
  ZtArray<int> dirs;
  ZtArray<PauseFn> pausedCompletes;

  template <typename Complete>
  void pause(Complete &&complete) {
    pausedCompletes.push(PauseFn{ZuFwd<Complete>(complete)});
    paused->post();
  }
  PauseFn resume() { return pausedCompletes.pop(); }
  void release() {
    while (pausedCompletes.length()) pausedCompletes.pop()(false);
  }
};

struct PayloadDB : public ZdbSagaDB {
  ZmSemaphore entered;
  Zdb_::SagaCompleteFn complete;
  unsigned runs = 0;
};

ZmRef<ZdbAnySaga<SagaDB>> sagaA(uint64_t value = 0);
Zdb_::SagaDef sagaADef();
ZmRef<ZdbAnySaga<SagaDB>> sagaB(uint32_t value = 0);
Zdb_::SagaDef sagaBDef();
ZmRef<ZdbAnySaga<SagaDB>> repeatSaga(uint64_t first = 0, uint64_t second = 0);
Zdb_::SagaDef repeatSagaDef();
ZmRef<ZdbAnySaga<LiveDB>> liveSaga(uint64_t orderID = 0);
Zdb_::SagaDef liveSagaDef();
ZmRef<ZdbAnySaga<LiveDB>> shortSaga(uint64_t orderID = 0);
Zdb_::SagaDef shortSagaDef();
ZmRef<ZdbAnySaga<LiveDB>> changedSaga(uint64_t orderID = 0);
Zdb_::SagaDef changedSagaDef();
ZmRef<ZdbAnySaga<PayloadDB>> payloadSaga(Zdb_::SagaPayload data = {});
Zdb_::SagaDef payloadSagaDef();
ZmRef<ZdbAnySaga<PayloadDB>> requiredSaga(const char * name = nullptr);
Zdb_::SagaDef requiredSagaDef();

ZmRef<ZdbAnySaga<LiveDB>> repeatLiveSaga(uint64_t id = 0, uint64_t repeats = 0);
Zdb_::SagaDef repeatLiveSagaDef();
unsigned sagaADestroyed();

} // zdbtest

#endif /* ZdbSagaTestDefs_HH */
