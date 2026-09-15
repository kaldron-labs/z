//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumMgmt.hh>

#include <zlib/ZuArray.hh>
#include <zlib/ZuCmp.hh>

namespace Zum {

ZtEnumImplNS(MgmtOp);

#define ZUM_ROUTE(ID, METHOD, PATH) \
  {MgmtOp::ID, Zhttp::Method::METHOD, PATH}

static constexpr ZuArray<MgmtRoute, MgmtOp::N> routes{
  ZUM_ROUTE(issuerQuery, GET, "/admin/issuer"),
  ZUM_ROUTE(operationQuery, GET, "/admin/operations"),
  ZUM_ROUTE(appQuery, GET, "/admin/apps"),
  ZUM_ROUTE(appEnroll, POST, "/admin/apps"),
  ZUM_ROUTE(appUpdate, PATCH, "/admin/apps/{appID}"),
  ZUM_ROUTE(appState, PUT, "/admin/apps/{appID}/state"),
  ZUM_ROUTE(userQuery, GET, "/admin/users"),
  ZUM_ROUTE(userInvite, POST, "/admin/users"),
  ZUM_ROUTE(userUpdate, PATCH, "/admin/users/{userID}"),
  ZUM_ROUTE(userState, PUT, "/admin/users/{userID}/state"),
  ZUM_ROUTE(userRecover, POST, "/admin/users/{userID}/recover"),
  ZUM_ROUTE(credentialQuery, GET, "/admin/credentials"),
  ZUM_ROUTE(credentialUpdate, PATCH,
    "/admin/credentials/{credentialID}"),
  ZUM_ROUTE(credentialState, PUT,
    "/admin/credentials/{credentialID}/state"),
  ZUM_ROUTE(membershipQuery, GET, "/admin/apps/{appID}/memberships"),
  ZUM_ROUTE(membershipAdd, POST, "/admin/apps/{appID}/memberships"),
  ZUM_ROUTE(membershipRoles, PUT,
    "/admin/apps/{appID}/memberships/{userID}/roles"),
  ZUM_ROUTE(membershipState, PUT,
    "/admin/apps/{appID}/memberships/{userID}/state"),
  ZUM_ROUTE(actionQuery, GET, "/admin/apps/{appID}/actions"),
  ZUM_ROUTE(actionAdd, POST, "/admin/apps/{appID}/actions"),
  ZUM_ROUTE(actionState, PUT,
    "/admin/apps/{appID}/actions/{actionID}/state"),
  ZUM_ROUTE(roleQuery, GET, "/admin/apps/{appID}/roles"),
  ZUM_ROUTE(roleAdd, POST, "/admin/apps/{appID}/roles"),
  ZUM_ROUTE(roleUpdate, PATCH, "/admin/apps/{appID}/roles/{roleID}"),
  ZUM_ROUTE(roleActions, PUT,
    "/admin/apps/{appID}/roles/{roleID}/actions"),
  ZUM_ROUTE(roleState, PUT,
    "/admin/apps/{appID}/roles/{roleID}/state"),
  ZUM_ROUTE(roleDelete, DELETE, "/admin/apps/{appID}/roles/{roleID}"),
  ZUM_ROUTE(clientQuery, GET, "/admin/clients"),
  ZUM_ROUTE(clientAdd, POST, "/admin/clients"),
  ZUM_ROUTE(clientUpdate, PATCH, "/admin/clients/{clientID}"),
  ZUM_ROUTE(clientState, PUT, "/admin/clients/{clientID}/state"),
  ZUM_ROUTE(clientSecretRotate, POST,
    "/admin/clients/{clientID}/rotate-secret"),
  ZUM_ROUTE(clientAccessQuery, GET,
    "/admin/apps/{appID}/client-access"),
  ZUM_ROUTE(clientAccessSet, PUT,
    "/admin/apps/{appID}/client-access/{clientID}"),
  ZUM_ROUTE(clientAccessState, PUT,
    "/admin/apps/{appID}/client-access/{clientID}/state"),
  ZUM_ROUTE(adminAccessQuery, GET,
    "/admin/apps/{appID}/admin-access"),
  ZUM_ROUTE(adminAccessSet, PUT,
    "/admin/apps/{appID}/admin-access/{actorKind}/{actorID}"),
  ZUM_ROUTE(adminAccessState, PUT,
    "/admin/apps/{appID}/admin-access/{actorKind}/{actorID}/state"),
  ZUM_ROUTE(providerQuery, GET, "/admin/providers"),
  ZUM_ROUTE(providerAdd, POST, "/admin/providers"),
  ZUM_ROUTE(providerUpdate, PATCH, "/admin/providers/{providerID}"),
  ZUM_ROUTE(providerState, PUT, "/admin/providers/{providerID}/state"),
  ZUM_ROUTE(authPolicyQuery, GET, "/admin/auth-policies"),
  ZUM_ROUTE(authPolicySet, PUT, "/admin/apps/{appID}/auth-policy"),
  ZUM_ROUTE(roleMapQuery, GET,
    "/admin/apps/{appID}/role-mappings"),
  ZUM_ROUTE(roleMapSet, PUT,
    "/admin/apps/{appID}/role-mappings/{providerID}/{valueKey}"),
  ZUM_ROUTE(roleMapDelete, DELETE,
    "/admin/apps/{appID}/role-mappings/{providerID}/{valueKey}"),
  ZUM_ROUTE(identityQuery, GET, "/admin/identities"),
  ZUM_ROUTE(evidenceQuery, GET, "/admin/evidence"),
  ZUM_ROUTE(sessionQuery, GET, "/admin/sessions"),
  ZUM_ROUTE(sessionRevoke, POST, "/admin/sessions/revoke"),
  ZUM_ROUTE(consentQuery, GET, "/admin/consents"),
  ZUM_ROUTE(consentRevoke, POST, "/admin/consents/revoke"),
  ZUM_ROUTE(grantQuery, GET, "/admin/grants"),
  ZUM_ROUTE(grantRevoke, POST, "/admin/grants/revoke"),
  ZUM_ROUTE(grantCleanup, POST, "/admin/grants/cleanup"),
  ZUM_ROUTE(signKeyQuery, GET, "/admin/signing-keys"),
  ZUM_ROUTE(signKeyAdd, POST, "/admin/signing-keys"),
  ZUM_ROUTE(signKeyRetire, POST,
    "/admin/signing-keys/{keyID}/retire"),
  ZUM_ROUTE(catalogPublish, PUT, "/admin/apps/{appID}/catalog")
};

#undef ZUM_ROUTE

ZuAssert(routes.length() == MgmtOp::N);

static bool pathComponent(ZuCSpan &path, ZuCSpan &component)
{
  if (!path || path[0] != '/') return false;
  path.offset(1);
  if (!path) return false;
  auto delimiter = path.find<"/">();
  component = path;
  if (delimiter < 0) {
    path = {};
  } else {
    component.trunc(unsigned(delimiter));
    path.offset(unsigned(delimiter));
  }
  return bool(component);
}

static bool routeMatches(const MgmtRoute &route, ZuCSpan requestPath)
{
  ZuCSpan routePath{route.path};
  auto query = requestPath.find<"?">();
  if (query >= 0) requestPath.trunc(unsigned(query));
  ZuCSpan routeComponent;
  ZuCSpan requestComponent;
  if (!pathComponent(routePath, routeComponent) ||
      routeComponent != "admin") return false;

  // The management API accepts both /admin/... and /... paths for the
  // existing daemon clients. The route catalog keeps the canonical form.
  if (requestPath) {
    auto saved = requestPath;
    if (!pathComponent(saved, requestComponent)) return false;
    if (requestComponent == "admin") requestPath = saved;
  }

  while (routePath || requestPath) {
    if (!pathComponent(routePath, routeComponent) ||
        !pathComponent(requestPath, requestComponent)) return false;
    bool wildcard = routeComponent.length() > 2 &&
      routeComponent[0] == '{' &&
      routeComponent[routeComponent.length() - 1] == '}';
    if (!wildcard && routeComponent != requestComponent) return false;
  }
  return true;
}

const MgmtRoute *managementRoute(int op)
{
  if (unsigned(op) >= MgmtOp::N || routes[op].op != op) return nullptr;
  return &routes[op];
}

int managementOperation(Zhttp::Method::T method, ZuCSpan path)
{
  if (unsigned(method) >= Zhttp::Method::N) return -1;
  for (const auto &route: routes)
    if (route.path && route.method == method && routeMatches(route, path))
      return route.op;
  return -1;
}

MgmtString managementAllow(ZuCSpan path)
{
  MgmtString allow;
  for (const auto &route: routes) {
    if (!route.path || !routeMatches(route, path)) continue;
    auto method = route.method;
    bool present = false;
    for (const auto &prior: routes) {
      if (prior.path && prior.method == method &&
	  routeMatches(prior, path) && prior.op < route.op) {
	present = true;
	break;
      }
    }
    if (present) continue;
    if (allow) allow << ", ";
    allow << Zhttp::Method::name(method);
  }
  return allow;
}

bool managementNeedsIdempotency(int op)
{
  switch (op) {
    case MgmtOp::appEnroll:
    case MgmtOp::userInvite:
    case MgmtOp::userRecover:
    case MgmtOp::membershipAdd:
    case MgmtOp::actionAdd:
    case MgmtOp::roleAdd:
    case MgmtOp::roleDelete:
    case MgmtOp::clientAdd:
    case MgmtOp::clientSecretRotate:
    case MgmtOp::providerAdd:
    case MgmtOp::sessionRevoke:
    case MgmtOp::consentRevoke:
    case MgmtOp::grantRevoke:
    case MgmtOp::grantCleanup:
    case MgmtOp::signKeyAdd:
    case MgmtOp::signKeyRetire:
    case MgmtOp::catalogPublish:
      return true;
    default:
      return false;
  }
}

bool managementAudited(int op)
{
  const auto *route = managementRoute(op);
  return route && route->method != Zhttp::Method::GET;
}

} // namespace Zum
