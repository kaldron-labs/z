//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database sagas

#include <zlib/Zdb.hh>

namespace Zdb_ {

ZtEnumImplNS(SagaOp);

bool Saga::prepare_(
    AnyTable *table, Shard shard, SagaOp::T op,
    bool effect, UN &un, bool &saved)
{
  return db()->sagaPrepare(this, table, shard, op, effect, un, saved);
}

void Saga::intent_(Shard shard, UN un)
{
  auto db = this->db();
  SagaStep step{
    .type = this->type(), .id = this->id(), .step = this->step(),
    .shard = shard, .un = un
  };
  ZmRef<Row<SagaStep>> intent =
    new Row<SagaStep>{db->sagaStepTable(), shard};
  bool committed = false;
  db->sagaStepTable()->insert(intent,
    [&step, &committed](Row<SagaStep> *row) {
      if (ZuUnlikely(!row)) return;
      new (row->ptr()) SagaStep{ZuMv(step)};
      committed = bool(row->commit());
    });
  ZiAssert(committed, "Zdb", (), "saga intent did not commit", ::abort());
  shards[m_step] = shard;
}

void Saga::replay_(OpResult::T result, Shard shard)
{
  if (result == OpResult::Skipped) {
    shards[m_step] = shard;
    stepRecovered_(shard);
  } else
    result_(result, shard);
}

void Saga::stepRecovered_(Shard shard)
{
  shards[m_step] = shard;
  ZmRef<Saga> saga = this;
  auto db = this->db();
  auto epoch = this->epoch();
  auto step = this->step();
  db->invoke([db, saga = ZuMv(saga), epoch, step, shard]() mutable {
    db->sagaStepRecovered(ZuMv(saga), epoch, step, shard);
  });
}

void Saga::result_(OpResult::T result, Shard shard)
{
  ZmRef<Saga> saga = this;
  auto db = this->db();
  auto epoch = this->epoch();
  db->invoke([db, saga = ZuMv(saga), epoch, result, shard]() mutable {
    db->sagaResult(ZuMv(saga), epoch, result, shard);
  });
}

} // Zdb_
