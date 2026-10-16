# Zum

Zum is an embedded IAM library for one application or service. It implements a
small OAuth authorization server, local RBAC, passkey authentication, and an
optional upstream OIDC login such as Okta. It is not an application catalog,
directory synchronizer, generic federation broker, or standalone IAM daemon.

The supported OAuth profile is deliberately closed:

- authorization code with mandatory PKCE `S256`;
- `client_credentials` for confidential workload clients;
- rotating refresh tokens for interactive clients;
- asymmetric RFC 9068 access tokens;
- authorization-server metadata, JWKS, and RFC 7009 refresh revocation; and
- exactly one configured interactive method: built-in passkey or upstream OIDC.

There are no passwords, TOTP secrets, API-key login, implicit flow, device flow,
dynamic registration, introspection, local login sessions, SAML, SCIM, generic
claim expressions, or management HTTP API.

## Authorization

Zum distinguishes durable authority from delegated authority:

- an `Action` is an operation checked by a resource service;
- a `Role` is a reusable bitmap-backed bundle of actions; and
- a `Scope` is an audience-bound OAuth label referencing roles.

Users and workload clients receive roles. A client registration limits the
scopes it may request. A token receives only the intersection:

```text
principal roles -> role actions ------------------\
                                                    -> token actions
selected scopes -> referenced roles -> actions ---/
```

All selected scopes must name one client-allowed audience. Roles are not
accepted as OAuth scopes and are not emitted in tokens. The action bitmap saved
with an authorization code or refresh family is an authority ceiling: later IAM
expansion cannot enlarge that grant without a new browser authorization.

The same resolver implements passkey users, OIDC users, and
`client_credentials`. Disabled users, credentials, clients, roles, scopes, or
actions grant nothing. Resource ownership and business predicates remain in the
host resource handler.

## Interactive authentication

`ServerConfig::authMethod` selects one method for the issuer.

### Passkey

`GET /authorize` validates the local OAuth request, creates a bound authorization
ceremony, and returns the host-rendered WebAuthn page. Zum creates and verifies
the challenge, parses the assertion, resolves the user and credential, applies
the common authorization path and host policy, and redirects with a local code.

`POST /passkey/begin` also starts server-authorized enrollment, bootstrap,
additional-credential, or recovery ceremonies. The JSON request names a
purpose, but the host admission callback supplies every identity and role value.
`POST /passkey/finish?id=...` derives the actual ceremony type from the stored
`Grant`; the browser cannot select a finish path.

### Upstream OIDC

OIDC mode still makes Zum the local OAuth authorization server. After validating
the local `/authorize` request, Zum redirects to one configured provider using
authorization code, state, nonce, and PKCE `S256`. It exchanges the provider
code asynchronously, refreshes a bounded JWKS cache on an unknown `kid`, and
validates the ES256 ID token signature, issuer, audience, authorized party,
nonce, issued time, and expiry. Provider access tokens are never accepted as
Zum tokens.

The provider `sub` resolves `User::oidcSub`; email is never an account-binding
key. Authorization has exactly two modes:

- `OIDCRoles::Local` uses the resolved user's `roleIDs`; or
- `OIDCRoles::Mapped` maps values from one configured ID-token claim to local
  role IDs.

Mapped roles are request-local and are saved only in the resulting grant. They
do not mutate `User::roleIDs`. Unknown or repeated values add no extra role.
Provider request scopes such as `openid profile email groups` only request
claims from the provider; they are unrelated to Zum's local OAuth `Scope` rows.

## HTTP integration

`Zum::Server` owns protocol state and exposes operations for:

| Method and path | Operation |
| --- | --- |
| `GET /authorize` | Start the configured interactive authorization. |
| `POST /token` | Redeem code, rotate refresh token, or issue a workload token. |
| `POST /revoke` | Revoke a refresh family with RFC 7009 response semantics. |
| `GET /.well-known/oauth-authorization-server` | Return implemented metadata. |
| `GET /jwks` | Return active and still-valid retiring public keys. |
| `POST /passkey/begin` | Start an admitted passkey ceremony. |
| `POST /passkey/finish` | Finish the server-selected passkey ceremony. |
| `GET /oidc/callback` | Finish upstream OIDC authentication. |

Hosts derive from `Zum::HTTP<App>` and add the appropriate Zrest request types
to their parser. Use `PasskeyHTTPRequests<App>` or `OIDCHTTPRequests<App>` as the
standard surface so routes for the inactive authentication method are not
mounted. The host supplies configuration, page rendering, passkey admission,
authorization policy, clock, signing, and—only for OIDC—a small asynchronous
HTTP transport. It does not parse OAuth, WebAuthn, or OIDC messages.

Zum creates a random short-lived transaction cookie for each browser flow and
stores only its digest with the grant. The cookie is `Secure`, `HttpOnly`, and
`SameSite=Lax`, and is cleared at a terminal response. It is request binding,
not a login session.

## Tokens

Access tokens use ES256 and include `typ=at+jwt`, `kid`, `iss`, opaque `sub`,
`aud`, `client_id`, `iat`, `nbf`, `exp`, `jti`, `scope`, and `actions`.
Interactive tokens also carry `auth_time` and exactly one AMR value, `passkey`
or `oidc`. Resource services call `jwtVerify()` to obtain a bounded `Principal`
without a database lookup.

Authorization codes and refresh tokens are opaque. Only complete-token digests
are persisted. Refresh rotation retains a bounded set of spent digests; proven
reuse revokes the family. `/revoke` returns success for unknown and already
revoked tokens, as RFC 7009 requires. Existing self-contained access tokens
remain usable only until their short expiry.

## Persistence

Zum retains ten Zdb tables:

| Table | Responsibility |
| --- | --- |
| `Issuer` | Issuer state, action-ID and audit-ID high-water marks, auth version. |
| `User` | Human identity, passkey handle, OIDC subject, roles, lifecycle. |
| `Cred` | Passkey public material, counter, label, lifecycle, user version. |
| `Action` | Stable named operation and never-reused bitmap position. |
| `Role` | Named action bitmap. |
| `Scope` | Audience-bound OAuth name and referenced roles. |
| `Client` | OAuth registration, credentials, grants, redirects, scopes and roles. |
| `Grant` | Ceremonies, capabilities, codes, refresh families and ceilings. |
| `SignKey` | Public JWK lifecycle and protected signer reference. |
| `Audit` | Bounded security-event history without bearer material. |

One-row transitions use conditional commits. Sagas are used only for invariants
that span rows, such as user-plus-first-credential activation and code-to-refresh
activation. Expired grants and audits are deleted through indexes in bounded
batches; Zum does not scan tables or run timer-based garbage collection.

`User::oidcSub` and the revised `Grant` schema are persistent schema changes.
Existing deployments must settle sagas and perform an explicit offline data
migration before opening the new schema. The library never deletes existing
identity data as an upgrade shortcut.

Public operations are admitted through `Requests`, dispatched to the owner
shard, bounded, deadline-controlled, and completed exactly once. Shutdown stops
HTTP ingress, deactivates and drains requests, cancels OIDC timers on their owner
shard, stops Zdb, and only then releases the owning objects.

See [zum2.md](zum2.md) for the implementation rationale and its references to
the repository engineering guidelines.
