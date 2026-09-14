//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_ssf_db.hh>

namespace Zum {

ZmRef<DBContext> registerSchema(DB *db)
{
  ZmRef<DBContext> context = new DBContext{};
  context->issuers = db->initTable<Issuer>("zum.issuer");
  context->apps = db->initTable<App>("zum.app");
  context->users = db->initTable<User>("zum.user");
  context->creds = db->initTable<Cred>("zum.cred");
  context->memberships = db->initTable<Membership>("zum.membership");
  context->actions = db->initTable<Action>("zum.action");
  context->roles = db->initTable<Role>("zum.role");
  context->scopes = db->initTable<Scope>("zum.scope");
  context->audiences = db->initTable<Audience>("zum.audience");
  context->clients = db->initTable<Client>("zum.client");
  context->clientAccess =
    db->initTable<ClientAccess>("zum.client_access");
  context->adminAccess =
    db->initTable<AdminAccess>("zum.admin_access");
  context->providers = db->initTable<Provider>("zum.provider");
  context->authPolicies =
    db->initTable<AuthPolicy>("zum.auth_policy");
  context->extIdentities =
    db->initTable<ExtIdentity>("zum.ext_identity");
  context->roleMaps = db->initTable<RoleMap>("zum.role_map");
  context->evidence = db->initTable<Evidence>("zum.evidence");
  context->sessions = db->initTable<Session>("zum.session");
  context->consents = db->initTable<Consent>("zum.consent");
  context->grants = db->initTable<Grant>("zum.grant");
  context->signKeys = db->initTable<SignKey>("zum.sign_key");
  context->requests = db->initTable<IdemRequest>("zum.request");
  context->ssfRx = db->initTable<SSFRx>("zum.ssf_rx");
  context->ssfDeliveries = db->initTable<SSFDelivery>("zum.ssf_delivery");
  sagaInit(db, context);
  return context;
}

} // namespace Zum
