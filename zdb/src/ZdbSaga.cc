//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database sagas

#include <zlib/Zdb.hh>

namespace Zdb_ {

ZtEnumImplNS(SagaOp);

void Saga::done()
{
  ZmRef<Saga> saga = this;
  auto db = this->db();
  auto epoch = this->epoch();
  db->invoke([db, saga = ZuMv(saga), epoch]() mutable {
    db->sagaRetire();
    db->sagaDone(ZuMv(saga), epoch);
  });
}

void Saga::fail(ZeException e)
{
  ZmRef<Saga> saga = this;
  auto db = this->db();
  auto epoch = this->epoch();
  db->invoke([db, saga = ZuMv(saga), epoch, e = ZuMv(e)]() mutable {
    db->sagaRetire();
    db->sagaFail(ZuMv(saga), epoch, ZuMv(e));
  });
}

void Saga::complete_(Shard shard)
{
  m_locs[m_step] = shard;
  SagaStepComplete{this, epoch(), step()}(true);
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

void SagaStepComplete::operator ()(bool ok)
{
  auto saga_ = ZuMv(saga);
  if (ZuUnlikely(!saga_)) return;
  auto db = saga_->db();
  if (ZuUnlikely(!ok)) {
    db->invoke([db, saga = ZuMv(saga_), epoch = epoch]() mutable {
      db->sagaRetire();
      db->sagaFail(ZuMv(saga), epoch,
	ZeEXCEPT(Error, "Zdb", "saga step failed"));
    });
    return;
  }
  auto shard = saga_->m_locs[step];
  db->invoke([
    db, saga = ZuMv(saga_), epoch = epoch, step = step, shard
  ]() mutable {
    db->sagaStepComplete(ZuMv(saga), epoch, step, shard);
  });
}

} // Zdb_
