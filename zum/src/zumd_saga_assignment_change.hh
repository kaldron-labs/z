//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_assignment_change_HH
#define zumd_saga_assignment_change_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_saga_admin_result.hh>

namespace Zum {

struct AssignmentChangeArgs {
  AppID appID = 0;
  UserID userID = 0;
  IDVec oldRoles;
  IDVec newRoles;
  State::T oldState = State::Pending;
  State::T newState = State::Pending;
  uint64_t version = 0;
  uint64_t authVersion = 0;
  int64_t oldUpdated = 0;
  int64_t updated = 0;
  uint64_t appVersion = 0;
  uint64_t appAuthVersion = 0;
  int64_t appUpdated = 0;
  IdemRequest request;
  bool assignRoles = false;
  String ifMatch;
  ZmRef<AdminEditResult<Assignment>> result;
  bool unchanged() const { return oldRoles == newRoles && oldState == newState; }
};

Zdb_::SagaDef assignmentChangeDef();
SagaRef assignmentChange(ZuBSpan);
SagaRef assignmentChange(AssignmentChangeArgs);

} // namespace Zum

#endif /* zumd_saga_assignment_change_HH */
