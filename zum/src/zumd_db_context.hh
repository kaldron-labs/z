//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// server database context; table definitions live with their consumers

#ifndef zumd_db_context_HH
#define zumd_db_context_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

namespace Zum {

struct MSaga;
struct IssuerTable;
struct UserTable;
struct CredTable;
struct GrantTable;
struct RefreshTable;
struct SessionTable;
struct ConsentTable;
struct AppTable;
struct MembershipTable;
struct ActionTable;
struct RoleTable;
struct ClientTable;
struct ClientAccessTable;
struct AdminAccessTable;
struct AuthPolicyTable;
struct ProviderTable;
struct ExtIdentityTable;
struct RoleMapTable;
struct EvidenceTable;
struct SignKeyTable;
struct IdemRequestTable;
struct SSFRxTable;
struct SSFDeliveryTable;

template <typename Heap = ZuVoid>
struct DBContext_ : public Heap, public ZmPolymorph  {
  Zdb			*db = nullptr;
  IssuerTable		*issuers = nullptr;
  AppTable		*apps = nullptr;
  UserTable		*users = nullptr;
  CredTable		*creds = nullptr;
  MembershipTable	*memberships = nullptr;
  ActionTable		*actions = nullptr;
  RoleTable		*roles = nullptr;
  ClientTable		*clients = nullptr;
  ClientAccessTable	*clientAccess = nullptr;
  AdminAccessTable	*adminAccess = nullptr;
  ProviderTable		*providers = nullptr;
  AuthPolicyTable	*authPolicies = nullptr;
  ExtIdentityTable	*extIdentities = nullptr;
  RoleMapTable		*roleMaps = nullptr;
  EvidenceTable		*evidence = nullptr;
  SessionTable		*sessions = nullptr;
  ConsentTable		*consents = nullptr;
  GrantTable		*grants = nullptr;
  RefreshTable		*refresh = nullptr;
  SignKeyTable		*signKeys = nullptr;
  IdemRequestTable	*requests = nullptr;
  SSFRxTable		*ssfRx = nullptr;
  SSFDeliveryTable	*ssfDeliveries = nullptr;
};
using DBContextHeap =
  ZmHeap<"Zum.zumd.db.context.DBContext", DBContext_<>>;
ZuDerive(DBContext, (DBContext_<DBContextHeap>));

} // namespace Zum

#endif /* zumd_db_context_HH */
