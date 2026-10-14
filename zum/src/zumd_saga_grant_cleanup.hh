//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_grant_cleanup_HH
#define zumd_saga_grant_cleanup_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd_saga_cleanup_data.hh>
#include <zlib/zumd_saga_revoke_result.hh>

namespace Zum {

struct GrantCleanupArgs {
  GrantDeleteVec grants;
  int64_t updated = 0;
  IdemRequest request;
  ZmRef<RevokeResult> result;
};

Zdb_::SagaDef grantCleanupDef();
SagaRef grantCleanup(ZuBSpan);
SagaRef grantCleanup(GrantCleanupArgs);

} // namespace Zum

#endif /* zumd_saga_grant_cleanup_HH */
