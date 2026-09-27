# Zum

Zum is the Z framework's multi-application identity and access-management
component. It provides `libZum`, an embeddable service-side authentication
library, and `zumd`, the persistent OAuth 2.0/OIDC server that supplies the
identity, authorization and management services used by Zum applications.

## Optional `zumd` telemetry

`zumd` can publish local App, multiplex and database telemetry through
`Ztc::App`. Publishing is off by default. Enable it with `--ztcPublish`,
`ZUMD_ZTC_PUBLISH=1` (exactly `1`), or `ztcPublish: true` in the node
configuration. These enable inputs are additive. The CLI flag has no value and
is case-sensitive. The publisher starts before IAM preparation and continues
while the database is passive; IAM readiness and collector availability do not
control its lifetime. `--once` and `--rekey` also accept the enable inputs.

The default publisher ID is `zumd`. Set `ztc.id` when multiple local publishers
share a registry; each ID must be unique. Optional `ztc` settings use
`Ztc::AppCf` defaults for omitted fields. For example, append these root fields
to a node configuration (with a comma after the preceding field):

```text
ztcPublish: true,
ztc: {id: "zumd-local", alertPrefix: "/var/lib/zumd/alerts/zumd"}
```

Choose a writable alert prefix and parent directory. `ZTC_RING` names the
shared telemetry ring and `ZTC_DIR` names the publisher PID registry beneath
the system temporary directory; set both to the same values in `zumd` and
`ztcagent`. The agent may attach later. The publisher ID identifies a local
telemetry source; the agent's authenticated device ID is separate. See the
[collector setup](../ztc/README.md).

The canonical end-to-end example is [`zumpingd`](example/zumpingd.cc), a
small HTTP resource server which links `libZum` but has no database, local
credential store or OIDC-provider implementation of its own. Its companion
[`zumping`](example/zumping.cc) is a standalone native OAuth client. The
example configuration and setup procedure are in
[`example/README.md`](example/README.md).

## Terminology

Zum uses a deliberately small authorization vocabulary. Several OAuth and
JWT terms describe the same application boundary, but they have different
meanings in standards documents. The table below defines the canonical Zum
terms and the aliases that may appear when discussing interoperability.

| Canonical Zum term | Other terms | Precise meaning |
|---|---|---|
| **Application** | service, resource | The protected application or API. |
| **Audience** | — | The application's identifier in the token's `aud` claim. It is a token field, not a separate domain object. |
| **Principal** | user, service account | The identity acting through an OAuth client. |
| **Caller** | — | The principal as observed on a particular request; useful in prose, but not a domain-model term. |
| **Role** | — | A named set of actions. |
| **Action** | permission | A named capability or operation. In Zum, each action corresponds 1:1 with its permission, so there is no separate permission object. |
| **Scope** | — | A run-time view of a requested role. The scope name is the role name and its effective actions are the role's actions. |

The canonical model is:

```text
application = protected service/resource
aud         = application identifier
principal   = user or service identity
role        = set of actions
action      = permission
scope       = requested role view
```

The one important qualification is **client**. An OAuth client is the
credentialed software obtaining or presenting the token. It may represent a
user or a service principal, but it is conceptually separate from that
principal. In a client-credentials flow, the service identity and OAuth client
will commonly be represented by the same deployment, but the roles remain
distinct.

A **confidential client** can authenticate to the authorization server while
keeping its client credential outside the resource owner's reach, typically in
a restricted server-side deployment. A server-side service such as `zumpingd`
can authenticate with a client secret. A native or browser client remains public
even when it uses `Ztls::Vault` to protect its own tokens at rest: a local vault
does not establish confidentiality of a client credential from that device's
user. Register those clients with the `Native` or `Browser` profile. The
`Server` profile is for restricted server-side deployments, including
interactive web backends and service workloads. OAuth `ClientType` is derived
from the profile: `Browser` and `Native` are `Public`, while `Server` is
`Confidential`; neither type nor client authentication method is stored
independently. Client registration and catalog JSON use `profile` (lowercase
`browser`, `native`, or `server`), not `type`.
`ZumVaultClient::{load,save}` stores public-client tokens, whereas
`ZumVaultService::{load,save}` stores server-side credentials. Both accept
optional `account` and `service` arguments after the `Credential`; omitted
values use the vault defaults (`<Zi::username()>@localhost` and
`ZiLog::program()`). Vault storage never changes the OAuth client type.

## Audience and application

In `zumd`, `audience` is an attribute of an application and therefore belongs
as a column on the application table:

```text
Application
  appID
  issuer
  audience
  ...
```

It identifies the application when the application is the protected
service/resource. Tokens issued for the application carry this value as `aud`;
services validate it against their configured application audience.

The audience is not modeled as:

- a principal attribute;
- a role or scope attribute;
- a separate audience table; or
- a token-request-defined permission boundary.

The application owns the audience. Principals receive roles within that
application.

## Standards-facing terminology

JWT defines `aud` as the intended recipient of a token. In Zum's application
model, that recipient is the protected application. The word *audience* is
therefore retained for the wire claim and standards compatibility, while
Zum's domain documentation should use *application* when referring to the
owned object.

The token's subject identifies the principal acting through the client. The
client presents the token to the application, which verifies the issuer,
audience, expiry, token type, and signature before applying the principal's
effective actions.

## libZum

The installed library headers are:

- [`ZumLib.hh`](src/ZumLib.hh): library/export definitions and the base Zum
  object types.
- [`ZumTypes.hh`](src/ZumTypes.hh): shared IDs, state values, OAuth/RBAC data
  types and protocol records.
- [`ZumJWTVerify.hh`](src/ZumJWTVerify.hh): bounded ES256 JWT parsing and
  verification, including issuer, audience, expiry and principal claims.
- [`ZumMgmt.hh`](src/ZumMgmt.hh): the stable management-operation and
  method/path catalog used by services, `zumd` and the `zum` client.
- [`ZumService.hh`](src/ZumService.hh): the service-side client described
  below.

### `Zum::Service`

`Zum::Service` is intended for an application which protects resources with
Zum-issued bearer tokens. It supplies:

- OIDC discovery of both the application's issuer and the core management
  issuer;
- confidential-client authentication using the OAuth
  `client_credentials` grant and the `zum.catalog` scope;
- management JWKS and application JWKS retrieval, bounded caching and key
  refresh on key rotation;
- local ES256 access-token verification, including issuer, application,
  audience, expiry, actions and token ID checks;
- an optional introspection fallback for tokens signed by a newly rotated key
  which is not yet available through JWKS;
- idempotent publication of an application's action and role catalog; each
  role name is also the OAuth scope name requested at runtime;
- optional Security Event Token (SET) reception for refresh-token-family
  revocation, with replay protection, bounded retention and an application
  refresh-family callback; and
- explicit asynchronous shutdown which waits for all in-flight HTTP work.

Verification returns the principal's authentication context in `authMethod`:
`client_credentials`, `passkey`, or `oidc`. Applications can require the
appropriate context as well as an action. Introspection supplies this context
only when its response includes a recognized `amr` or an explicit
`grant_type=client_credentials`; otherwise the field is empty and applications
which require it must reject the request.

`zum.catalog` is the management client's protocol scope. Resource scopes are
derived from application roles: each role name is one same-named scope and
selects exactly that role's actions. Provider and identity scopes such as
`openid` remain separate from these role-named resource scopes.

The service owns its protocol state on the configured scheduler shard. The
application supplies `ServiceHTTPFn`, an asynchronous HTTP adapter; `libZum`
does not impose a particular HTTP client. `zumpingd`'s private
[`PingHTTP`](example/pinghttp.hh) adapter is the reference implementation.

A typical setup is:

```c++
Zum::Service service;

service.init(Zum::ServiceConfig{
  .scheduler = &mx,
  .sid = authSID,
  .issuerURL = "https://auth.example/oauth2/42",
  .managementIssuerURL = "https://auth.example/oauth2/1",
  .managementURL = "https://auth.example",
  .clientID = serviceClientID,
  .clientSecret = serviceClientSecret,
  .audience = "https://orders.example/api"
}, httpAdapter);

service.start([](int error) {
  // ServiceError::OK means discovery, authentication and key loading worked.
});

service.verify(accessToken, [](int error, Zum::ServicePrincipal principal) {
  // Check error, then authorize principal.actions for the request.
});
```

Services normally call `publish()` after `start()` and before opening their
resource listener. `publish()` sends the catalog to the application-scoped
management endpoint; it does not assign users or roles. Call `stop()`, wait
for its completion callback, and then call `final()` during teardown. The
service keeps tokens and configured secret material in memory and clears them
when stopped. See [`zumpingd.cc`](example/zumpingd.cc) for the complete
startup, request, SET callback and shutdown sequence.

`zumpingd` publishes one `ping` action and role; the runtime `ping` scope is
derived from that role. It verifies the bearer token on `GET /ping`, and returns
`{"reply":"pong"}` only when the token
contains the `ping` action. It also exposes `/health/ready` and, when SET
delivery is enabled, `/ssf`. Its listener is deliberately independent of
Zum's database: readiness and authorization come from the service connection
to `zumd`.

## zumd

`zumd` is the production Zum daemon. It embeds the server implementation from
`libZum`, registers the Zum schema with Zdb, and serves the public and
administrative HTTP surfaces. The backing store is selected through Zdb (for
example with `ZDB_MODULE` and `ZDB_CONNECT`); PostgreSQL and SQLite are
supported through their Zdb store modules. Zdb supplies sharding,
activation/deactivation, replication where configured, and durable saga
recovery for multi-record changes.

Its main features are:

- application-scoped OAuth 2.0 and OIDC issuers with discovery, authorization
  code plus S256 PKCE, refresh-token and client-credentials grants;
- ES256 access and ID tokens, JWKS publication, UserInfo, token revocation,
  consent and refresh-token-family handling;
- WebAuthn/passkey bootstrap, enrollment, login, additional credentials and
  recovery;
- multi-application RBAC: applications, users, memberships, actions, roles,
  clients and client/admin delegation. OAuth resource scopes are
  runtime views of roles and are derived from the role catalog;
- an operation-oriented `/admin` REST API with bearer authorization,
  idempotency keys, ETag preconditions, bounded queries, secret redaction and
  structured ZiLog administration events;
- OIDC federation to external identity providers, including discovery,
  authorization-code/PKCE exchange, ID-token and UserInfo validation, JWKS
  retrieval, provider-local role mapping and claim-based eligibility; and
- refresh-token-family revocation delivery to enrolled services using signed
  SETs; already-issued access tokens remain self-contained until expiry.

Refresh-token families are persisted separately in `zum.refresh`; `zum.grant`
contains only short-lived authorization, passkey, and capability protocol
state. Expired grants, sessions, and refresh families are reclaimed by the
daemon in bounded maintenance passes. The `--cleanup-interval=N` option
controls the pass period in seconds (default 300; `0` disables automatic
passes, leaving the administrative cleanup operation available).
Setting an application to `Revoked` is permanent logical deletion: the app is
made inactive first, then bounded saga batches remove its evidence and consent
rows. `Disabled` remains reversible and does not delete those records.

The Transmitter is enabled by an optional `ssf` node in the daemon's native
configuration.  It contains the SSF issuer and a receiver array with each
receiver's application ID, audience, HTTPS callback URL, revision, and a
`secretName`. `secretName` is the environment-variable name used to provision
the callback credential and its name in Vault; the value is never written to
the configuration or database. The daemon resolves it at startup, and delivery
uses the in-memory value. For example:

```text
ssf: {
  issuer: "https://auth.example/ssf",
  receivers: [{
    receiverID: "orders-rx", appID: 42,
    audience: "https://orders.example/api",
    deliveryURL: "https://orders.example/ssf",
    secretName: "ZUM_SSF_ORDERS_AUTH", revision: 1
  }]
}
```

Set `ZUM_SSF_ORDERS_AUTH` to the complete callback `Authorization` value (for
example, `Bearer ...`) through the deployment secret manager on the first run
or to replace it. Once startup succeeds, the value is saved under
`env/ssf/ZUM_SSF_ORDERS_AUTH` and later starts may omit the variable. Each
distinct receiver secret has its own name. Rotate a secret by restarting the
daemon with a new environment value; changing the environment of a running
daemon does not affect delivery. `--ssf-issuer` overrides the configured issuer.

The daemon separates listener availability from database readiness:
`/health/live` can remain available while a passive or recovering node returns
503 for database-dependent operations. The `zumd: active` event means database
activation and signing preparation completed; a new store still requires the
initial administrator enrollment before `/health/ready` is ready.

### OIDC client roles

At the system boundary, `zumd` is the OIDC provider consumed by embedded
`libZum` services and can also be the OIDC client/broker used on behalf of an
application and its dependents. Zum has two complementary OIDC-client paths.

1. A `libZum` dependent such as `zumpingd` uses `Zum::Service` as a
   confidential OIDC client. On startup it discovers the application and
   management issuers, obtains a management client-credentials token for
   catalog publication, downloads signing keys, and then verifies workload
   access tokens locally. The dependent therefore needs only its enrolled
   client ID and secret; it does not implement OAuth, token signing, JWKS
   rotation or upstream-provider integration.
2. `zumd` can itself be an OIDC client for an enrolled upstream provider.
   When an application's authentication policy selects federation, `zumd`
   performs provider discovery and the authorization-code/PKCE flow, exchanges
   the code, validates the signed ID token and (when configured) UserInfo, and
   maps the resulting claims to local Zum users and roles. A dependent still
   receives ordinary Zum access tokens and remains insulated from the upstream
   provider.

For the first path, configure `zumpingd` with the exact application issuer,
the core management issuer, the independent management URL, its enrolled
confidential client ID and the resource audience. Inject `ZUM_CLIENT_SECRET`
and `ZUM_SSF_AUTH` through the deployment secret manager; do not put
them in `zumpingd.cf` or on the command line. For the second path, configure
the provider through the authenticated management API. An optional `oidc`
section in the `zumd` node configuration can provide a private CA path;
certificate and hostname verification remain enabled.

### Starting the daemon

Build the module with the normal Z build:

```sh
./z.config /opt/z
make -j
make install
```

On the first start, supply a base64-encoded 256-bit `ZUM_DB_KEY`. A successful
start saves its raw value as `global/dbKey` in Vault; later starts may omit the
environment variable. An explicit environment key always wins, and a wrong
one fails startup without falling back to Vault. The daemon also requires a
public issuer, an initial administrator login, an owner-only bootstrap output
file, and an explicitly selected secure Vault store:

```sh
export ZUM_DB_KEY='BASE64_256_BIT_KEY'
zumd --issuer=https://auth.example \
  --vault-store=keyring \
  --admin=admin@example.test \
  --bootstrap-output=/run/secrets/zum-bootstrap-url \
  --config=/etc/zum/node.cf
```

Use `--vault-store=module --vault-module=PATH` for a deployment-provided secure
store, or put `vault: {store: "keyring"}` (or `module` plus its path) in the
node configuration. The Vault program is `zumd`, its account is the exact
issuer URL, and `ZUMD_HOME` overrides its default home. There is no automatic
fallback to an unencrypted store. `file` and `ephemeral` require the explicit
`--vault-test-store` flag and an isolated test home. A missing secret or
unavailable store prevents startup.

Open the one-time URL written to the bootstrap file in a browser to create
the administrator passkey. Use the [`zum`](src/zum.cc) administrative client
to enroll applications, users, clients, memberships and catalogs. Its basic
interface is documented in [`MANAGEMENT.md`](MANAGEMENT.md):

```sh
zum --config FILE login
zum --config FILE OPERATION --json FILE
```

The daemon also supports `--once` for activation/bootstrap maintenance. For
offline encrypted-database key rotation, stop all writers and run `--rekey`
with `ZUM_DB_KEY` set to the **new** key. The old key is loaded from Vault;
after the database change is durable, the new key replaces it there. Completion
is reported only after both steps succeed. If Vault publication fails after
the database commit, retain the same new key and retry `--rekey`; do not
generate another key. Keep the new key available until completion is reported.

## Further documentation

- [`MANAGEMENT.md`](MANAGEMENT.md) — management routes, actions, public
  OAuth/OIDC endpoints, client configuration and operational contracts.
- [`example/README.md`](example/README.md) — complete `zumpingd`/`zumping`
  enrollment and login flow.
- [`itest/README.md`](itest/README.md) — persistence, HTTP, federation,
  clustering and OIDC transport integration tests.
- [`test/README.md`](test/README.md) — unit-test coverage for the service,
  protocol and transport components.

### Application audiences

Each application represents one service and owns one immutable
`audience` string, used as the resource access token’s `aud` claim.
`appEnroll` requires `audience`; `appQuery` returns it on the application.
Client access grants select application roles. Consent is keyed by user, client
and application. Schema version 21 adds the refresh-family table; existing
databases require reprovisioning.
