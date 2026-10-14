//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_app_action_add_HH
#define zumd_saga_app_action_add_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>

namespace Zum {

struct AppActionAddArgs {
  AppID appID = 0;
  ActionID actionID = 0;
  String name;
  String label;
  int64_t created = 0;
  uint64_t oldAppVersion = 0;
  uint64_t oldAuthVersion = 0;
  int64_t oldUpdated = 0;
  IdemRequest request;
};

Zdb_::SagaDef appActionAddDef();
SagaRef appActionAdd(ZuBSpan);
SagaRef appActionAdd(AppActionAddArgs);

} // namespace Zum

#endif /* zumd_saga_app_action_add_HH */
