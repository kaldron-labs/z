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

#include <zlib/ZtString.hh>

#include <zlib/ZhttpCore.hh>

namespace Zum {

ZuDerive(MgmtString, ZtString<ZtStringHeapID<"Zum.MgmtString">>);

// One operation per remote management call. Append entries: these values
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
  scopeQuery, scopeAdd, scopeRoles, scopeState,
  audienceQuery, audienceAdd, audienceUpdate, audienceState,
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
  retired67, retired68, // Retired audit IDs; never reuse these positions.
  catalogPublish);

// Invalid request IDs must not resolve to a usable permission name.
inline MgmtString managementAction(int op)
{
  if (unsigned(op) >= MgmtOp::N ||
      op == MgmtOp::retired67 || op == MgmtOp::retired68) return {};
  MgmtString name{"Zum."};
  name << MgmtOp::name(op);
  return name;
}

namespace CoreAction {
  enum : uint32_t {
    FacadeAuthorize = MgmtOp::N,
    FacadeToken,
    FacadeRevoke,
    N
  };
}

ZumAPI MgmtString coreAction(uint32_t);

struct MgmtRoute {
  int16_t		op = -1;
  Zhttp::Method::T	method = Zhttp::Method::GET;
  const char		*path = nullptr;
};

// The route registry is indexed by MgmtOp and is the sole source used by
// daemon dispatch and administrative clients.
ZumAPI const MgmtRoute *managementRoute(int op);
ZumAPI int managementOperation(Zhttp::Method::T, ZuCSpan path);
ZumAPI MgmtString managementAllow(ZuCSpan path);
ZumAPI bool managementNeedsIdempotency(int op);
// Mutating management operations emit outcome events through ZiLog.
ZumAPI bool managementAudited(int op);

} // namespace Zum

#endif /* ZumMgmt_HH */
