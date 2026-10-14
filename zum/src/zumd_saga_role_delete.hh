//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_role_delete_HH
#define zumd_saga_role_delete_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>

namespace Zum {

struct RoleDeleteArgs {
  App app;
  Role role;
  BytesVec members;
  BytesVec clients;
  BytesVec admins;
  BytesVec maps;
  int64_t updated = 0;
  IdemRequest request;
};

Zdb_::SagaDef roleDeleteDef();
SagaRef roleDelete(ZuBSpan);
SagaRef roleDelete(RoleDeleteArgs);

} // namespace Zum

#endif /* zumd_saga_role_delete_HH */
