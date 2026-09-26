//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// remote management operation and permission catalog

#ifndef ZumMgmt_HH
#define ZumMgmt_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumTypes.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZhttpCore.hh>

namespace Zum {

ZuDerive(MgmtString, ZtString<ZtStringHeapID<"Zum.MgmtString">>);

struct CatalogAction {
  String label;
  String name;
};
ZfStruct(, (CatalogAction, JSON),
  (((label),		(JSON::Opt)),	(String)),
  (((name),		(Required)),	(String)));
ZuDerive(CatalogActionArray, (ZtArray<CatalogAction,
  ZtArrayHeapID<"Zum.Catalog.Actions">>));
struct CatalogActionVec : public CatalogActionArray {
  ZuDerive_(CatalogActionVec, CatalogActionArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(CatalogActionVec *);
};

struct CatalogRole {
  StringVec actions;
  String label;
  String name;
};
ZfStruct(, (CatalogRole, JSON),
  (((actions),		(Required)),	(StringVec)),
  (((label),		(JSON::Opt)),	(String)),
  (((name),		(Required)),	(String)));
ZuDerive(CatalogRoleArray, (ZtArray<CatalogRole,
  ZtArrayHeapID<"Zum.Catalog.Roles">>));
struct CatalogRoleVec : public CatalogRoleArray {
  ZuDerive_(CatalogRoleVec, CatalogRoleArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(CatalogRoleVec *);
};

struct CatalogClient {
  String id;
  String label;
  String profile;
  StringVec redirectURIs;
  uint8_t grants = 0;
  bool refreshAllowed = false;
  StringVec identityScopes;
  StringVec roles;
};
ZfStruct(, (CatalogClient, JSON),
  (((id),		(Required)),	(String)),
  (((label),		(JSON::Opt)),	(String)),
  (((profile),		(Required)),	(String)),
  (((redirectURIs),	(JSON::Opt)),	(StringVec)),
  (((grants),		(JSON::Opt)),	(UInt8)),
  (((refreshAllowed),	(JSON::Opt)),	(Bool)),
  (((identityScopes),	(JSON::Opt)),	(StringVec)),
  (((roles),		(JSON::Opt)),	(StringVec)));
ZuDerive(CatalogClientArray, (ZtArray<CatalogClient,
  ZtArrayHeapID<"Zum.Catalog.Clients">>));
struct CatalogClientVec : public CatalogClientArray {
  ZuDerive_(CatalogClientVec, CatalogClientArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(CatalogClientVec *);
};

struct CatalogData {
  CatalogActionVec actions;
  CatalogRoleVec roles;
  CatalogClientVec clients;
};
ZfStruct(, (CatalogData, JSON),
  (((actions),		(Required)),	(UDT)),
  (((roles),		(Required)),	(UDT)),
  (((clients),		(JSON::Opt)),	(UDT)));

// One operation per remote management call. These values
// are the built-in action IDs as well as management request identities.
// Query accepts an exact key or bounded pagination; there is no separate
// Read permission. Lifecycle changes use named operations, not row writes.
ZtEnumNS(ZumAPI, MgmtOp, int16_t,
  issuerQuery, operationQuery,
  appQuery, appEnroll, appUpdate, appState,
  userQuery, userInvite, userUpdate, userState, userRecover,
  credentialQuery, credentialUpdate, credentialState,
  membershipQuery, membershipAdd, membershipRoles, membershipState,
  actionQuery, actionAdd, actionState,
  roleQuery, roleAdd, roleUpdate, roleActions, roleState, roleDelete,
  clientQuery, clientAdd, clientUpdate, clientState, clientSecretRotate,
  clientAccessQuery, clientAccessSet, clientAccessState,
  adminAccessQuery, adminAccessSet, adminAccessState,
  providerQuery, providerAdd, providerUpdate, providerState,
  authPolicyQuery, authPolicySet,
  roleMapQuery, roleMapSet, roleMapDelete,
  identityQuery, evidenceQuery,
  sessionQuery, sessionRevoke,
  consentQuery, consentRevoke,
  grantQuery, grantRevoke, grantCleanup,
  signKeyQuery, signKeyAdd, signKeyRetire,
  catalogPublish);

// Invalid request IDs must not resolve to a usable permission name.
inline MgmtString managementAction(int op)
{
  if (unsigned(op) >= MgmtOp::N) return {};
  MgmtString name{"Zum."};
  name << MgmtOp::name(op);
  return name;
}

namespace CoreAction {
  enum : uint32_t { N = MgmtOp::N };
}

inline MgmtString coreAction(uint32_t id) { return managementAction(id); }

struct MgmtRoute {
  int16_t		op = -1;
  Zhttp::Method::T	method = Zhttp::Method::GET;
  const char		*path = nullptr;
};

// The route registry is intentionally immutable and pointer-stable: the
// public API returns records by address and the runtime router walks the same
// records when constructing its dispatch graph. It is the sole source used by
// daemon dispatch and administrative clients.
ZumAPI const MgmtRoute *managementRoute(int op);
ZumAPI int managementOperation(Zhttp::Method::T, ZuCSpan path);
ZumAPI MgmtString managementAllow(ZuCSpan path);
ZumAPI bool managementNeedsIdempotency(int op);
// Mutating management operations emit outcome events through ZiLog.
ZumAPI bool managementAudited(int op);

} // namespace Zum

#endif /* ZumMgmt_HH */
