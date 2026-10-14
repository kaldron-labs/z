//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_recovery_start_HH
#define zumd_saga_recovery_start_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

struct RecoveryStartArgs {
  Bytes		capabilityID;
  Bytes		digest;
  String	issuer;
  UserID	userID = 0;
  uint64_t	userVersion = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  String	actor;
  uint64_t	version = 0;
  State::T oldState = State::Active;
  int64_t oldUpdated = 0;
  IdemRequest request;
};

Zdb_::SagaDef recoveryStartDef();
SagaRef recoveryStart(ZuBSpan);
SagaRef recoveryStart(RecoveryStartArgs);

} // namespace Zum

#endif /* zumd_saga_recovery_start_HH */
