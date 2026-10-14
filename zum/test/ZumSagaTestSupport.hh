//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZumSagaTestSupport_HH
#define ZumSagaTestSupport_HH

#include <zlib/ZuTestUtil.hh>
#include <zlib/zumd_saga.hh>

using namespace ZuTestUtil;

static void roundTrip_(const Zdb_::SagaDef &def,
    Zum::SagaRef saga)
{
  ZuTestScopeRT(roundTrip);
  ZuCheckRT(bool(saga));
  ZuCheckRT(saga->type() == def.type);
  ZuCheckRT(saga->nPhases() == def.nPhases);
  Zdb_::SagaPayload payload;
  ZuCheckRT(saga->save(payload) == ZiFB::Error::OK);
  auto loaded = def.load(payload);
  ZuCheckRT(bool(loaded));
  ZuCheckRT(loaded->type() == saga->type());
  ZuCheckRT(loaded->nPhases() == saga->nPhases());
  ZuCheckRT(loaded->nSteps() == saga->nSteps());
  for (unsigned step = 0; step < saga->nSteps(); ++step) {
    ZuCSpan table, loadedTable;
    ZdbSagaOp::T op, loadedOp;
    ZuCheckRT(saga->stepDef(step, table, op));
    ZuCheckRT(loaded->stepDef(step, loadedTable, loadedOp));
    ZuCheckRT(table == loadedTable && op == loadedOp);
  }
  Zdb_::SagaPayload again;
  ZuCheckRT(loaded->save(again) == ZiFB::Error::OK);
  ZuCheckRT((ZuBSpan{payload.data(), payload.length()} ==
    ZuBSpan{again.data(), again.length()}));
}

static void distinct_(const Zdb_::SagaDef &def,
    Zum::SagaRef populated, Zum::SagaRef empty)
{
  ZuTestScope(distinct);
  ZuTestCall(roundTrip_, def, populated);
  Zdb_::SagaPayload first, second;
  ZuCheck(populated->save(first) == ZiFB::Error::OK);
  ZuCheck(empty->save(second) == ZiFB::Error::OK);
  ZuCheck((ZuBSpan{first.data(), first.length()} !=
    ZuBSpan{second.data(), second.length()}));
}

static void phase_(const Zdb_::SagaDef &def,
    unsigned index, ZuCSpan table, ZdbSagaOp::T op)
{
  ZuTestScope(phase);
  Zdb_::SagaTypeStep row;
  ZuCheck(index < def.nPhases && def.phaseDef(index, row));
  ZuCheck(row.table == table && row.op == op);
}

#define roundTrip(...) ZuTestCall(roundTrip_, __VA_ARGS__)
#define distinct(...) ZuTestCall(distinct_, __VA_ARGS__)
#define phase(...) ZuTestCall(phase_, __VA_ARGS__)

#endif /* ZumSagaTestSupport_HH */
