//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumSagaTestSupport.hh"
#include <zlib/zumd_saga_cred_edit.hh>
#include <zlib/zumd_saga_provider_edit.hh>
#include <zlib/zumd_saga_client_edit.hh>
#include <zlib/zumd_saga_key_retire.hh>
#include <zlib/zumd_saga_client_access_state.hh>
#include <zlib/zumd_saga_admin_access_state.hh>
#include <zlib/zumd_saga_role_map_delete.hh>
#include <zlib/zumd_saga_role_map_put.hh>
#include <zlib/zumd_saga_policy_put.hh>
#include <zlib/zumd_saga_client_access_put.hh>
#include <zlib/zumd_saga_admin_access_put.hh>
#include <zlib/zumd_saga_provider_add.hh>
#include <zlib/zumd_saga_role_add.hh>
#include <zlib/zumd_saga_user_invite.hh>
#include <zlib/zumd_saga_client_add.hh>
#include <zlib/zumd_saga_key_add.hh>

static void administration()
{
  ZuTestScope(administration);
  roundTrip(Zum::credEditDef(), Zum::credEdit(Zum::CredEditArgs{}));
  roundTrip(Zum::providerEditDef(), Zum::providerEdit(Zum::ProviderEditArgs{}));
  roundTrip(Zum::clientEditDef(), Zum::clientEdit(Zum::ClientEditArgs{}));
  roundTrip(Zum::keyRetireDef(), Zum::keyRetire(Zum::KeyRetireArgs{}));
  roundTrip(Zum::clientAccessStateDef(), Zum::clientAccessState(Zum::ClientAccessStateArgs{}));
  roundTrip(Zum::adminAccessStateDef(), Zum::adminAccessState(Zum::AdminAccessStateArgs{}));
  roundTrip(Zum::roleMapDeleteDef(), Zum::roleMapDelete(Zum::RoleMapDeleteArgs{}));
  roundTrip(Zum::roleMapPutDef(), Zum::roleMapPut(Zum::RoleMapPutArgs{}));
  roundTrip(Zum::policyPutDef(), Zum::policyPut(Zum::PolicyPutArgs{}));
  roundTrip(Zum::clientAccessPutDef(), Zum::clientAccessPut(Zum::ClientAccessPutArgs{}));
  roundTrip(Zum::adminAccessPutDef(), Zum::adminAccessPut(Zum::AdminAccessPutArgs{}));
  roundTrip(Zum::providerAddDef(), Zum::providerAdd(Zum::ProviderAddArgs{}));
  roundTrip(Zum::roleAddDef(), Zum::roleAdd(Zum::RoleAddArgs{}));
  roundTrip(Zum::userInviteDef(), Zum::userInvite(Zum::UserInviteArgs{}));
  roundTrip(Zum::clientAddDef(), Zum::clientAdd(Zum::ClientAddArgs{}));
  roundTrip(Zum::keyAddDef(), Zum::keyAdd(Zum::KeyAddArgs{}));

  Zum::RoleMapPutArgs mapping;
  mapping.app.id = 9;
  mapping.before = Zum::RoleMap{.appID = 9, .providerID = 7,
    .value = "operator"};
  mapping.roleID = 4;
  mapping.ifNoneMatch = "*";
  mapping.updated = 126;
  distinct(Zum::roleMapPutDef(), Zum::roleMapPut(ZuMv(mapping)),
    Zum::roleMapPut(Zum::RoleMapPutArgs{}));
  phase(Zum::roleMapPutDef(), 2, "zum.role_map", ZdbSagaOp::Insert);
  phase(Zum::roleMapPutDef(), 3, "zum.role_map", ZdbSagaOp::Update);

  Zum::RoleAddArgs role;
  role.app.id = 9;
  role.before = Zum::Role{.appID = 9, .id = 4};
  role.values = Zum::Role{.appID = 9, .id = 4, .name = "operator"};
  role.updated = 127;
  distinct(Zum::roleAddDef(), Zum::roleAdd(ZuMv(role)),
    Zum::roleAdd(Zum::RoleAddArgs{}));
  phase(Zum::roleAddDef(), 2, "zum.role", ZdbSagaOp::Insert);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(administration);
  return 0;
}
