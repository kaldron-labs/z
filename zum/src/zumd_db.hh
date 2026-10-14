//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed server database and saga submission

#ifndef zumd_db_HH
#define zumd_db_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/Zdb.hh>
#include <zlib/zumd_saga.hh>

namespace Zum {

ZuDerive(SagaFn, (ZmFn<void(bool), ZmFnHeapID<"Zum.SagaFn">>));

struct IssuerTable;
struct UserTable;
struct CredTable;
struct GrantTable;
struct RefreshTable;
struct SessionTable;
struct ConsentTable;
struct AppTable;
struct AssignmentTable;
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

struct DB : public ZdbSagaDB {
  IssuerTable		*issuers = nullptr;
  AppTable		*apps = nullptr;
  UserTable		*users = nullptr;
  CredTable		*creds = nullptr;
  AssignmentTable	*assignments = nullptr;
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

ZumExtern void registerSchema(DB *);

ZumExtern Zdb_::SagaDefs sagaDefs();
ZumExtern void sagaInit(DB *);
ZumExtern bool sagaSubmit(
  DB *, ZdbSagaID, SagaRef, SagaFn, SagaFn, ZuTime = {});

} // namespace Zum

#endif /* zumd_db_HH */
