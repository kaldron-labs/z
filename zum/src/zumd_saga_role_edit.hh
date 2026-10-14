//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_role_edit_HH
#define zumd_saga_role_edit_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd_saga_admin_result.hh>
#include <zlib/zumd.hh>

namespace Zum {

struct RoleEditArgs {
  enum { Actions, Label, Status };
  App app;
  Role before;
  ActionIDVec actionIDs;
  String ifMatch;
  int64_t updated = 0;
  IdemRequest request;
  int8_t kind = Actions;
  String label;
  State::T state = State::Pending;
  ZmRef<AdminEditResult<Role>> result;
  bool unchanged() const { return kind == Status && state == before.state; }
};

Zdb_::SagaDef roleEditDef();
SagaRef roleEdit(ZuBSpan);
SagaRef roleEdit(RoleEditArgs);

} // namespace Zum

#endif /* zumd_saga_role_edit_HH */
