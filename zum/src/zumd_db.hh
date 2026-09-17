//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed server database and saga dispatch

#ifndef zumd_db_HH
#define zumd_db_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_sagas.hh>
#include <zlib/zumd_role_delete.hh>
#include <zlib/zumd_catalog.hh>
#include <zlib/zumd_revoke.hh>
#include <zlib/zumd_consent.hh>
#include <zlib/zumd_rekey.hh>

namespace Zum {

ZuDerive(SagaFn, (ZmFn<void(bool), ZmFnHeapID<"Zum.SagaFn">>));

ZdbSagaDerive(SagaCatalog,
  Enrollment, CredentialAdd, RecoveryStart, RecoveryEnroll, CodeFamily,
  AppEnrollment, ExternalProjection, AppActionAdd, MembershipChange, RoleDelete,
  CatalogPublish, MembershipAdd, RoleEdit, ActionEdit,
  AppChange, UserEdit, CredEdit, ProviderEdit, ClientEdit, KeyRetire,
  ClientAccessState, AdminAccessState, RoleMapDelete, RoleMapPut, PolicyPut,
  ClientAccessPut, AdminAccessPut, ProviderAdd, RoleAdd,
  UserInvite, ClientAdd, KeyAdd, Revoke, GrantCleanup, SessionCleanup,
  RefreshCleanup, AppCleanup, ConsentCode, KeyBinding, SecretRekey,
  SSFDeliveryAdd, GrantCleanupV2, SessionCleanupV1, RefreshCleanupV1,
  AppCleanupV1);
struct MSaga;
using MSagaBase = ZdbMSaga<SagaCatalog, MSaga>;
struct MSaga : public MSagaBase { ZuDerive_(MSaga, MSagaBase) };
ZuDerive(DB, (ZdbSagaDB<DBContext, SagaCatalog, SagaFn, MSaga>));

ZumExtern ZmRef<DBContext> registerSchema(DB *);

// Keep replay and submission in one implementation unit so both paths share
// the instantiated saga executor.
ZumExtern void sagaInit(DB *, ZmRef<DBContext>);
ZumExtern bool sagaSubmit(
  DB *, ZdbSagaID, ZmRef<MSaga>, SagaFn, SagaFn, ZuTime = {});

} // namespace Zum

#endif /* zumd_db_HH */
