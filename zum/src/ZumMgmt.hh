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

#include <zlib/Zum.hh>

namespace Zum {

// One operation per remote management call. Append entries: these values
// are the built-in action IDs as well as management request identities.
// Query accepts an exact key or bounded pagination; there is no separate
// Read permission. Lifecycle changes use named operations, not row writes.
ZtEnumNS(ZumAPI, MgmtOp, int16_t,
  issuerQuery,
  userQuery, userInvite, userUpdate, userRoles, userState, userRecover,
  credentialQuery, credentialUpdate, credentialState,
  roleQuery, roleAdd, roleUpdate, roleActions, roleState,
  actionQuery, actionAdd, actionState,
  scopeQuery, scopeAdd, scopeRoles, scopeState,
  clientQuery, clientAdd, clientUpdate, clientRoles, clientState,
  clientSecretRotate,
  grantQuery, grantRevoke, grantCleanup,
  signKeyQuery, signKeyAdd, signKeyRetire,
  auditQuery, auditCleanup);

// Invalid request IDs must not resolve to a usable permission name.
inline String managementAction(int op)
{
  if (unsigned(op) >= MgmtOp::N) return {};
  String name{"Zum."};
  name << MgmtOp::name(op);
  return name;
}

} // namespace Zum

#endif /* ZumMgmt_HH */
