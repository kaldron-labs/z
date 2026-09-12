//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumLegacy.hh>

namespace Zum::Legacy {

ZmRef<DBContext> registerSchema(Zdb *db)
{
  ZmRef<DBContext> context = new DBContext{};
  context->issuers = db->initTable<Issuer>("zum.issuer");
  context->users = db->initTable<User>("zum.user");
  context->creds = db->initTable<Cred>("zum.cred");
  context->actions = db->initTable<Action>("zum.action");
  context->roles = db->initTable<Role>("zum.role");
  context->scopes = db->initTable<Scope>("zum.scope");
  context->clients = db->initTable<Client>("zum.client");
  context->grants = db->initTable<Grant>("zum.grant");
  context->signKeys = db->initTable<SignKey>("zum.signKey");
  context->audits = db->initTable<Audit>("zum.audit");
  return context;
}

} // namespace Zum::Legacy
