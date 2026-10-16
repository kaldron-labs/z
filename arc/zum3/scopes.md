# Zum scope simplification plan

## Purpose

Simplify resource scopes so that a scope is a run-time view of one application
role. A scope has no persisted identity, independent permission set, or
role-binding policy. It only names a role requested for a particular token.

The existing app-scoped authorization model remains authoritative:

```text
application -> roles -> actions
subject      -> assigned roles
token request -> requested role-named scopes
token         -> granted scopes and effective action snapshot
```

The change preserves application-defined OAuth scopes because they are the
standard request and response vocabulary at the authorization boundary. It
removes the separate scope table and makes the application's roles the sole
durable authorization catalog.

This does not remove upstream OIDC role mapping. An external provider such as
Okta may use role values that do not match the local application's role names.
The provider integration must continue to maintain an explicit one-to-one
mapping from each validated upstream role value to the corresponding local
application role. That mapping determines local role assignment; it is
separate from the resource scope alias described here.

## Contract

For every run-time resource scope:

- Its name is an application role's stable machine name.
- It resolves to exactly one active role in the target application.
- Its action view is exactly that role's current action set, subject to the
  token's authorization snapshot.
- It cannot reference roles from another application or aggregate roles.
- It has no state, origin, catalog revision, lifecycle, or actions of its own.

The application owns its token audience. Requested roles belong to that
application; scopes carry no audience or independent authorization boundary.

Role assignment and scope selection remain separate operations:

```text
assigned roles = user membership, external role mapping, or client access
requested roles = resource scope names in the OAuth request
granted roles = assigned roles intersected with requested roles
actions = union(actions(granted roles)) intersected with active app actions
```

Requesting a scope never creates a membership or role assignment. A client
that is authorized for one role may request that role's scope and receive all
actions in the role. A client that requests several role-named scopes receives
the union of the actions in the authorized requested roles.

When no resource scope is requested, Zum uses the documented default for the
grant flow. Identity protocol scopes remain independent and are not resolved
through application roles. In particular, `openid`, `profile`, `email`, and
`offline_access` are not resource-role scopes.

The OAuth request and response use the standard space-delimited `scope`
parameter. For JWT access tokens, the `scope` claim contains the granted
resource scope names and any applicable identity scopes. The `actions` claim
contains the final action snapshot and is the canonical authorization input
for `libZum` resource services. A resource service does not expand roles or
look up scopes locally.

## Data model

Delete the persisted `Scope` record and table. `Role` is the only durable
application authorization object that contains the role name and action set.
The run-time scope view is constructed from `(appID, role.name)` and
the current request/assignment context.

Remove all scope-specific state, origin, catalog revision, lifecycle, role
vectors, role IDs, table indexes, saga types, and management routes. Role state
controls whether its name can be requested; application state controls whether
the protected service can be used. The application owns its token audience.

Make the role machine name a stable public identifier. Role action membership
and labels may be updated administratively. If role renaming remains allowed,
treat it as a public scope-name change: invalidate or version the affected
authorization state and ensure existing token snapshots remain internally
consistent. Do not retain an old scope alias after the role is renamed.

Replace persisted scope references as follows:

- `ClientAccess` stores `roleIDs` within an application.
- `Consent` stores approved `roleIDs`; remove `scopeIDs`.
- `Grant` stores the exact OAuth `scope` string and explicit role snapshots;
  remove `scopeIDs`. Rename ambiguous role fields to distinguish authenticated
  principal roles from roles selected by the request.
- `ScopeSelection` retains the selected role IDs and normalized scope string,
  but has no scope IDs.
- provider configuration continues to store provider-requested scope strings;
  those are upstream protocol scopes, not local resource scopes.

Update the schema version and all generated/persisted representations,
including:

- `zum/src/zumd.hh` and its `ZfbStruct` declarations;
- `zum/src/fbs/zum_grant.fbs` and `zum/src/fbs/zum_saga.fbs` where scope IDs
  are serialized;
- all database table declarations, keys, indexes, and row construction for
  `zum.scope`;
- saga images and database operation payloads;
- JSON management representations and query results.

If an in-place database upgrade is supported, migrate existing scope rows into
the corresponding client-access, consent, and grant role references only when
the old row has exactly one role whose name matches the scope name and belongs
to the same application. Reject multi-role, missing-role, cross-application,
and name-mismatched rows with an actionable migration error. Do not guess a
replacement role. If the repository has no supported migration path, reject
the old schema version and require a clean rebuild.

## Catalog and management API

Remove scopes from the application catalog. On application creation, the
application defines its roles and their actions; every role automatically
defines a same-named resource scope at run time. Subsequent role insertion,
action updates, state changes, and deletion automatically change the set of
resource scopes available for new requests.

Catalog validation must enforce the existing action and role invariants:

- every role name is unique within the application;
- every role action exists in the same application;
- role action references are unique;
- role and action lifecycle states are valid for publication.

Remove `CatalogScope`, the catalog `scopes` array, scope publication loops,
`catalogScopeRoles()`, and any catalog digest semantics that depend on a
separate scope list. Catalog publication creates or updates roles only.

Remove direct scope administration entirely:

- delete `scopeQuery`, `scopeAdd`, `scopeRoles`, and `scopeState` routes and
  saga operations;
- remove scope-specific management request/response structures;
- query role names and actions as the source from which clients derive the
  application's resource scopes;
- use role state and role deletion cleanup to control future scope requests.

The client-access API assigns roles within the target application. Requested
scope names resolve directly to those role IDs. `client_access` has no audience
set or second scope-specific action or role policy.

## Authority resolution

Replace the current scope expansion path in `zumd_core.cc` and dependent
loaders with direct role-name resolution:

1. Parse the requested resource scope names using the existing OAuth syntax.
2. Resolve each name as one active `Role` by `(appID, role.name)`.
3. Reject unknown, inactive, or client-disallowed role names with the existing
   OAuth error behavior; validate the target audience independently.
4. Append each resolved role ID once to the run-time scope selection.
5. Intersect those requested role IDs with the subject's assigned roles.
6. Union the actions from the resulting roles and intersect active action
   records before issuing the token.

Preserve the existing distinction between local memberships, validated
external role mappings, and workload `client_access`. Each path supplies
assigned local role IDs; none supplies scope-owned permissions. External role
mapping remains a one-to-one provider-value-to-local-role translation and is
not replaced by scope-name matching.

Use `Role` directly in `selectScopes()` and `selectGrantedScopes()`.
`ScopeSelection` retains only the normalized scope string, selected role IDs,
and identity-scope flag. Token construction takes the audience from `App`.

Preserve the action snapshot rules:

- authorization-code issuance snapshots the resolved granted scope names and
  effective actions;
- refresh-token rotation cannot enlarge either snapshot;
- role/action changes affect newly authorized tokens according to the existing
  version checks;
- already-issued access tokens retain their signed action snapshot until
  expiry or the existing revocation mechanism applies.

## Grants, consent, and tokens

Keep the OAuth `scope` request and response fields. Change their semantics in
Zum documentation and code comments to say “granted role-named scopes,” not
“scope role ceilings” or “scope bundles.”

Consent records store the approved local role IDs. Consent accumulation and
revocation remain unchanged apart from resolving the role-named OAuth scopes
directly against the current application roles.

Grant records continue to snapshot:

- the exact scope string;
- the roles selected by the request;
- the authenticated/request-local role IDs;
- the effective action bitmap;
- the authority and policy versions needed for refresh validation.

Remove all scope-ID persistence and any code that computes an action ceiling
from a separate scope object. The only resource-scope contribution is the set
of roles selected by the request.

JWT issuance and verification must continue to emit and validate the standard
claims plus Zum's action snapshot. Verify that a token's `scope` and `actions`
are both derived from the same resolved role selection and cannot disagree.
`Zum::Service::verify()` and `ServicePrincipal.actions` remain the resource
service interface; no role expansion belongs in `libZum`.

## Documentation

Update all documentation that currently describes scopes as independent named
delegation bundles or arbitrary role sets:

- `zum/README.md`: state the app → role → action model, user/client role
  assignment, derived role-named OAuth scopes, and the granted `actions`
  snapshot;
- `zum/MANAGEMENT.md`: describe roles as the sole application authorization
  catalog and explain that scopes are derived at request time;
- `arc/zum/zum3.md`: replace the current scope-ceiling, scope-table, and
  multi-role scope contract with this derived-scope contract;
- Zum examples and integration fixtures: define roles, then request their
  role-named scopes without publishing separate scope records;
- source comments and API documentation for role resolution, grants, consent,
  client access, and JWT claims.

Keep protocol identity scopes explicitly separate from application resource
scopes in every document. Do not describe the `scope` JWT claim as the
identity's durable role assignment; it is the granted role selection for that
token.

## Tests

Update unit tests to cover the new invariants and remove tests that assert a
persisted scope table or independent/multi-role scope behavior.

Required unit coverage includes:

- deriving a scope from one same-named application role;
- rejecting unknown, inactive, and cross-application role-named scopes;
- ensuring role state controls scope availability;
- ensuring audience validation is independent of role-name resolution;
- selecting one or several role-named scopes and unioning their role actions;
- intersecting requested scopes with local membership roles;
- applying the same resolution to external role mappings and client access;
- preserving identity-only scopes without resource-role resolution;
- preserving the exact granted scope string and action snapshot through code,
  refresh, and JWT issuance;
- preventing refresh or role changes from expanding an existing grant;
- migrating or rejecting legacy scope rows and scope-ID references according
  to the chosen schema upgrade policy;
- ensuring `libZum` exposes the final actions without requiring role lookup.

Update integration fixtures and examples to define roles and exercise one or
multiple requested role-named scopes. Include negative token requests for
unknown, retired, cross-application, and client-disallowed roles. Verify JWT
`scope`, JWT `actions`, introspection, and `ServicePrincipal.actions` agree.

Run the Zum source build before its tests, then run the Zum unit and integration
targets. Search the repository after the change for `Zum::Scope`, `scopeIDs`,
`catalogRoleIDs`, scope-management routes, and documentation describing scope
ceilings; each remaining occurrence must either be deleted or explicitly refer
to a non-resource identity/provider scope.

## Non-goals

This plan does not change:

- application, role, or action ownership and lifecycle;
- the app → role → action cardinalities;
- user membership, external role mapping, or client-access assignment;
- the explicit one-to-one upstream OIDC role-value mapping used to translate
  provider roles into an application's local roles;
- OAuth grant types, client authentication, issuer/audience validation, or
  token rotation;
- identity protocol scopes;
- SSF revocation delivery;
- the `libZum` resource-service verification boundary.
