//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_user_invite_HH
#define zumd_saga_user_invite_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_saga_admin_result.hh>

namespace Zum {

struct UserInviteArgs {
  User before;
  User values;
  Grant grant;
  int64_t updated = 0;
  IdemRequest request;
  User external;
  ZmRef<AdminEditResult<User>> result;
  bool unchanged() const { return false; }
};

Zdb_::SagaDef userInviteDef();
SagaRef userInvite(ZuBSpan);
SagaRef userInvite(UserInviteArgs);

} // namespace Zum

#endif /* zumd_saga_user_invite_HH */
