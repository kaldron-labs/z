# Management operations

[ZumMgmt.hh](src/ZumMgmt.hh) is the shared wire catalog for Zum
administration. Each of its 70 `MgmtOp` values identifies exactly one remote
call and one core permission named `Zum.<operation>`. The route registry in
[ZumMgmt.cc](src/ZumMgmt.cc) is the sole method/path mapping used by `zumd`,
the `zum` client, and registry tests.

The catalog is intentionally operation-oriented rather than the Cartesian
product of CRUDQ and tables. Query operations are collection GETs with exact
indexed filters or bounded pagination. Lifecycle, assignment, cleanup,
publication, and secret rotation have distinct operations because they have
different authorization and consistency rules. Zdb saga infrastructure has no
public management operation.

## Authorization ownership

Bootstrap seeds the catalog in the core Zum application. The core
`superuser` role contains every management action. The distinct
`appService` role contains only operation-status lookup, catalog publication,
and the three service-facade actions. Enrolled services never receive
`superuser` implicitly.

A bearer token must carry the exact operation action. Current database state
must additionally prove either the active core superuser membership or an
active `admin_access` delegation for the authenticated user/service and target
application. The service which owns an application may publish that
application's available action/standard-role/scope catalog; catalog publication
never assigns roles to users or restores deleted privileges.

## Areas

| Area | Operations |
| --- | --- |
| Issuer/catalog | `issuerQuery`, `operationQuery`, `catalogPublish` |
| Applications | `appQuery`, `appEnroll`, `appUpdate`, `appState` |
| Users/credentials | `userQuery`, `userInvite`, `userUpdate`, `userState`, `userRecover`; `credentialQuery`, `credentialUpdate`, `credentialState` |
| Memberships | `membershipQuery`, `membershipAdd`, `membershipRoles`, `membershipState` |
| Actions/roles/scopes | `actionQuery`, `actionAdd`, `actionState`; `roleQuery`, `roleAdd`, `roleUpdate`, `roleActions`, `roleState`, `roleDelete`; `scopeQuery`, `scopeAdd`, `scopeRoles`, `scopeState` |
| Audiences/clients | `audienceQuery`, `audienceAdd`, `audienceUpdate`, `audienceState`; `clientQuery`, `clientAdd`, `clientUpdate`, `clientState`, `clientSecretRotate` |
| Delegation | `clientAccessQuery`, `clientAccessSet`, `clientAccessState`; `adminAccessQuery`, `adminAccessSet`, `adminAccessState` |
| Federation | `providerQuery`, `providerAdd`, `providerUpdate`, `providerState`; `authPolicyQuery`, `authPolicySet`; `roleMapQuery`, `roleMapSet`, `roleMapDelete`; `identityQuery`, `evidenceQuery` |
| Runtime state | `sessionQuery`, `sessionRevoke`; `consentQuery`, `consentRevoke`; `grantQuery`, `grantRevoke`, `grantCleanup` |
| Keys/audit | `signKeyQuery`, `signKeyAdd`, `signKeyRetire`; `auditQuery`, `auditCleanup` |

The precise method/path table, typed mutation fields, scope semantics, catalog
ownership, federation freshness, and lifecycle contracts are normative in
[zum3.md](../zum3.md). Append operations; never renumber a catalog already
seeded in a database.

## HTTP rules

All paths have the `/admin` prefix. GET carries filters in the query string
and no body. Mutations carry JSON; DELETE has no body. Known paths called with
an unsupported method return 405 with a registry-derived `Allow` header.

Existing-record writes require `If-Match`; PUT creation requires
`If-None-Match: *`. Creation, publication, rotation, and bounded bulk
mutations require `Idempotency-Key`. Secret digests, private/protected key
material, provider secrets, tokens, credential public keys, and server session
handles are never returned by ordinary queries.

After authentication and authorization, every non-GET management operation
produces one request-level `Administration` audit record. It carries the exact
catalog operation ID, authenticated actor, target application and path,
success/failure outcome, and the same opaque correlation ID returned in an
error response. Request bodies, query values, credentials, and secrets are not
copied into audit metadata. GET operations, including `auditQuery`, do not grow
the audit log. Domain workflows may additionally emit their more specific
credential, revocation, key-rotation, or principal-change events.

## Administrative client

The initial interface is:

```sh
zum --config FILE login
zum --config FILE OPERATION --json FILE
```

The non-secret configuration contains `issuerURL`, and may contain
`clientID` (default `zum-admin`), `scope` (default `zum.admin`), `callbackPort`,
`loginTimeout`, and an owner-only `credentialFile`. Login uses an external
browser, a loopback redirect, authorization code, and S256 PKCE. Without a
credential file, operation invocations perform login and retain tokens only in
memory.

Request JSON supplies normal path/query/body fields. These reserved fields are
removed from the payload and mapped to transport controls:

| Field | Meaning |
| --- | --- |
| `$ifMatch` | `If-Match` header |
| `$ifNoneMatch` | `If-None-Match` header |
| `$idempotencyKey` | `Idempotency-Key` header |
| `$secretOutput` | Explicit owner-only file for a secret-bearing response |

Potentially secret-producing application enrollment, client creation, and
client-secret rotation require `$secretOutput`; their response is never
printed to the terminal.

## Public OAuth 2.0 and OIDC endpoints

The administrative routes above are separate from the issuer's public protocol
surface:

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/.well-known/oauth-authorization-server` | OAuth authorization-server metadata. |
| GET | `/.well-known/openid-configuration` | OpenID Provider metadata. |
| GET | `/authorize` | Authorization-code initiation with S256 PKCE. |
| POST | `/token` | Authorization-code, refresh-token, and client-credentials exchange. |
| POST | `/revoke` | Token revocation. |
| GET | `/jwks` | Active public signing keys. |
| GET | `/userinfo` | Claims selected by an access token carrying `openid`. |

An interactive request may combine a resource scope with the enrolled client's
`openid`, `profile`, and `email` identity scopes. The exact granted scope string
and original nonce survive code exchange and refresh. `openid` causes an ES256
ID token to be returned; `profile` and `email` control only their corresponding
ID-token and UserInfo claims. Identity scopes are not accepted for
client-credentials grants.
