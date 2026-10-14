//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_provider_add_HH
#define zumd_saga_provider_add_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_saga_admin_result.hh>

namespace Zum {

struct ProviderAddArgs {
  Provider before;
  Provider values;
  int64_t updated = 0;
  IdemRequest request;
  ZmRef<AdminEditResult<Provider>> result;
  bool unchanged() const { return false; }
};

Zdb_::SagaDef providerAddDef();
SagaRef providerAdd(ZuBSpan);
SagaRef providerAdd(ProviderAddArgs);

} // namespace Zum

#endif /* zumd_saga_provider_add_HH */
