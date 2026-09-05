//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database sagas

#include <zlib/Zdb.hh>

namespace Zdb_ {

ZtEnumImplNS(SagaOp);

void Saga::stepRecovered_(Shard shard)
{
  m_locs[m_step] = shard;
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
