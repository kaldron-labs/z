//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_db.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_ssf_db.hh>

namespace Zum {

void registerSchema(DB *db)
{
  db->issuers = db->initTable<Issuer>("zum.issuer");
  db->apps = db->initTable<App>("zum.app");
  db->users = db->initTable<User>("zum.user");
  db->creds = db->initTable<Cred>("zum.cred");
  db->assignments = db->initTable<Assignment>("zum.assignment");
  db->actions = db->initTable<Action>("zum.action");
  db->roles = db->initTable<Role>("zum.role");
  db->clients = db->initTable<Client>("zum.client");
  db->clientAccess =
    db->initTable<ClientAccess>("zum.client_access");
  db->adminAccess =
    db->initTable<AdminAccess>("zum.admin_access");
  db->providers = db->initTable<Provider>("zum.provider");
  db->authPolicies =
    db->initTable<AuthPolicy>("zum.auth_policy");
  db->extIdentities =
    db->initTable<ExtIdentity>("zum.ext_identity");
  db->roleMaps = db->initTable<RoleMap>("zum.role_map");
  db->evidence = db->initTable<Evidence>("zum.evidence");
  db->sessions = db->initTable<Session>("zum.session");
  db->consents = db->initTable<Consent>("zum.consent");
  db->grants = db->initTable<Grant>("zum.grant");
  db->refresh = db->initTable<Refresh>("zum.refresh");
  db->signKeys = db->initTable<SignKey>("zum.sign_key");
  db->requests = db->initTable<IdemRequest>("zum.request");
  db->ssfRx = db->initTable<SSFRx>("zum.ssf_rx");
  db->ssfDeliveries = db->initTable<SSFDelivery>("zum.ssf_delivery");
  sagaInit(db);
}

} // namespace Zum
