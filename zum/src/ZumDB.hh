//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed server database and saga dispatch

#ifndef ZumDB_HH
#define ZumDB_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumSagas.hh>
#include <zlib/ZumRoleDelete.hh>
#include <zlib/ZumCatalog.hh>
#include <zlib/ZumRevoke.hh>
#include <zlib/ZumConsent.hh>
#include <zlib/ZumRekey.hh>
#include <zlib/ZumMigratePut.hh>

namespace Zum {

ZuDerive(SagaFn, (ZmFn<void(bool), ZmFnHeapID<"Zum.SagaFn">>));

struct SagaCatalog {
  using List = ZuTypeList<
    Enrollment, CredentialAdd, RecoveryStart, RecoveryEnroll, CodeFamily,
    AppEnrollment, ExternalProjection, AppActionAdd, MembershipChange, RoleDelete,
    CatalogPublish, MembershipAdd, RoleEdit, ScopeEdit, ActionEdit,
    AppChange, UserEdit, CredEdit, AudienceEdit, ProviderEdit, ClientEdit, KeyRetire,
    ClientAccessState, AdminAccessState, RoleMapDelete, RoleMapPut, PolicyPut,
    ClientAccessPut, AdminAccessPut, ProviderAdd, AudienceAdd, RoleAdd, ScopeAdd, UserInvite,
    ClientAdd, KeyAdd, Revoke, GrantCleanup, ConsentCode, MigrationStart,
    MigrationPut, MigrationFinish, KeyBinding, SecretRekey>;
  static ZumAPI int match(ZuCSpan);
};
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

#endif /* ZumDB_HH */
