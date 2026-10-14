//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_db.hh>

namespace Zum {

bool sagaSubmit(DB *db, ZdbSagaID id, SagaRef saga,
    SagaFn submit, SagaFn complete, ZuTime deadline)
{
  return db && db->saga(0, id, ZuMv(saga), ZuMv(submit), ZuMv(complete),
    deadline);
}

} // namespace Zum
