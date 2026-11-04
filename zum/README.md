# Zum

Zum is the Z framework's multi-application identity and access-management
component. It provides `libZum`, an embeddable service-side authentication
library, and `zumd`, the persistent OAuth 2.0/OIDC server that supplies the
identity, authorization and management services used by Zum applications.

`zumd` loads `Z_HEAPTUNE` at startup through `ZiHeapTune`. Transient scope
selection trees use the `Zum.ScopeNames` and `Zum.ScopeIDs` node heaps;
configure their recycling capacities by heap ID, partition and `vshift`
in that tuning file. One scope-name node holds the identity, granted,
requested and selected membership flags. Zdb indexes resolve role records.

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
`ztcagent`. `ztc.telRingSize` supplies the initial capacity when the publisher
creates the ring and defaults to 2 MiB, matching the agent's default `telSize`.
An existing ring retains its capacity. The agent may attach later. The publisher
ID identifies a local telemetry source; the agent's authenticated device ID is
separate. See the
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
  app_id
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
Initialize and start this adapter before starting its service consumers. After
stopping those consumers, stop the adapter before calling its `final`. Its
engine covers resolver startup, destination clients and accepted request
lifetimes; `init` leaves it stopped and `final` performs no shutdown wait.
The adapter supports restart and retains configuration after stop. A resolver
owned by the application must already be running when adapter startup begins.

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

service.start([&service](bool ok) {
  // Success includes discovery, authentication, key loading and optional SSF registration.
  // On failure, service.startError() supplies the separate startup diagnostic.
});

service.verify(accessToken, [](int error, Zum::ServicePrincipal principal) {
  // Check error, then authorize principal.actions for the request.
});
```

Services normally call `publish()` after `start()` and before opening their
resource listener. `publish()` sends the catalog to the application-scoped
management endpoint; it does not assign users or roles. Call `stop()`, wait
for its completion callback, and then call `final()` during teardown. The
service clears runtime tokens and key/dedup state when stopped, retaining
configuration secrets for restart. Final clears those configuration secrets.
See [`zumpingd.cc`](example/zumpingd.cc) for the complete startup, request, SET callback and shutdown sequence.

Service uses a parent `ZmEngine` for the complete operation. Parameterless
`start()` and `stop()` are synchronous main-thread calls; callback overloads
report booleans and use canonical idempotence and pending opposite operations.
A completed stop permits start with the retained configuration, without init.
Stop is best effort: errors do not skip remaining cleanup. Completion leaves
the engine Stopped even when its boolean reports failure. Daemon, provider and
HTTP-adapter controllers aggregate dependency errors through their complete
drain boundary; they do not restart stopped resources to undo shutdown.
Startup failure rolls back owned work before its false completion; a failed
start or an initialization that never started can be finalized directly.
Final asserts Stopped and drained owner work, and never starts shutdown.

Stop seals admission, cancels both timers, waits for HTTP callbacks and queued
application input to return, and drains runtime containers in bounded owner
turns. The HTTP adapter must invoke each completion exactly once. It and the
scheduler remain valid through completed stop. Application operations accepted
while Running complete on the configured owner; calls rejected outside Running
complete immediately with ServiceError::Stopped. Stop/final must not race an
external call that has not returned. A stop callback can finalize the service
when no opposite lifecycle operation is pending.

`setRefreshRevocationFn` can configure a quiescent Stopped service directly;
serialize this configuration with start/final. During Running it posts the
callback replacement to the owner and its post is covered by stop. The hook
is retained across restart.

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
- multi-application RBAC: applications, users, assignments, actions, roles,
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

### OIDC component lifecycle

OIDC and its OIDCHTTP adapter each own a complete engine. Init retains
configuration while Stopped. Start opens protocol or transport admission;
OIDC start does not promise discovery or authentication readiness. Complete
stop before final; final releases quiescent configuration without dispatching
shutdown or waiting. Completed stop permits restart without init.

OIDC stop cancels pending login timers/state in bounded owner turns and waits
for accepted calls, discovery, token/JWKS/UserInfo responses, DB helpers and
application callbacks. A transport must invoke each supplied completion once.
Stop waits for outstanding external responses; keep the HTTP adapter, database
and scheduler running until that drain completes. Callback invocation consumes
its work handle, so retaining an already-fired HTTP completion does not delay
shutdown. Cancelled pending state cannot be revived by a late response.

OIDCHTTP start starts its owned resolver. An externally owned resolver must
already be Running and remains externally controlled. Stop drains accepted
requests and origin clients, releases native runtime clients in bounded turns,
and stops its owned resolver. Origin storage capacity and configuration survive
stop; clients are reconstructed lazily on restart. A retained fn() handle owns
the adapter state and rejects sends after final without scheduler access.

The embedded Server provider also exposes canonical start/stop/state. Start
covers its OIDC child and opens public admission, independently of Requests
activation or authentication readiness. Final requires Stopped. The daemon
starts the provider before its HTTP listener and rolls it back if listener
startup fails. Provider callback graphs carry an admission credit. DB helpers retain that
same credit through destruction, including work surviving application
cancellation. Daemon now has asynchronous engine hooks for its provider, HTTP,
SSF, Requests and maintenance dependencies; final requires Stopped and never
initiates shutdown. Signing prepare runs on the main thread with the daemon
Stopped and request admission closed, followed by restart. Administrative admission and its callback/DB/saga graphs now retain a parent
credit through actual helper destruction. Stop drains producers before stopping
the SSF consumer, preserving accepted revocation side effects. Rx correlation
entropy uses the existing stateless backend RNG wrapper. Full shard/capture/budget audits and executable
lifecycle validation remain in progress; this batch has not been built or run.

### Request admission and SSF lifecycle

`Requests::activate` and `deactivate` accept canonical boolean completion
callbacks. Activation opens admission on its configured owner thread; callers
requiring readiness wait for that callback. Deactivation reports a superseded
queued stop as false, separately from a completed stop. The daemon joins an
actual Stopped admission state before accepting that dependency as drained.
Production Zdb activation serializes preparation after deactivation and checks
the active-role generation before and after asynchronous admission startup.


The server's Requests controller uses a canonical engine for its admission,
deadline and cancellation resources. Database activation remains a separate
role decision that calls activate/deactivate. Activation opens admission on the
owner continuation; activation during a drain is queued until that drain
completes. Multiple deactivate callbacks share the completed boundary.

The admission limit and count include deadline records awaiting their owner
retirement barrier. A logically completed request can therefore briefly retain
its slot; run returns false while that bound is full. Cancellation and node
retirement use bounded owner turns. Keep Requests alive until deactivation
completes. A retained cancelled completion token can outlive that controller;
its done check suppresses owner access. Keep the scheduler valid through any
external completion call that has not returned.

SSFTransmitter is a separate child engine. Start launches its deadline/retry
work and opens notification/registration admission; it does not promise that
the durable outbox has finished replaying. Stop seals new inputs, cancels both
timers, and drains DB, signing and HTTP callbacks and their continuations.
Callbacks consume their graph credit when invoked, so an adapter can retain
a fired completion without delaying shutdown. Selector callbacks finish at
their explicit terminal result. Adapters must complete each request once.

Completed stop permits transmitter restart with retained configuration. Final
only releases quiescent configuration; it never starts shutdown. The owning
daemon drains a transmitter before replacing its signing configuration. DB,
Requests, scheduler and adapter resources remain valid through child stop.

SSF sends refresh-family revocations to clients that register a callback.
There are no statically configured receivers. A service authenticates with its
workload bearer token and calls `POST /admin/apps/{app_id}/ssf` using its existing
application catalog publication authority:

```json
{"receiver_id":"orders-rx","delivery_url":"https://orders.example/ssf",
 "callback_auth":"Bearer ...","expires_in":300}
```

The response is `200` with `expires_in` (the granted lease in seconds) and
`expires` (Unix expiry time). The receiver ID is scoped to the authenticated
client and application. Re-registering the same ID renews its lease and updates
its callback; the notification audience comes from the application record.
Callbacks use HTTPS, with HTTP permitted for loopback development. Callback
authorization is encrypted with the database key and included in offline key
rotation; it is never written into the notification outbox.

`Zum::Service` registers on startup and renews on a timer at half the granted
lease, refreshing its workload token as needed. Failed renewals retry after one
second; stopping the service cancels its timer. Configure `ssf.deliveryURL` and
`ssf.lease` on the client. The hub exposes these as `ssfDeliveryURL` and
`ssfLease`. The callback URL must be reachable from `zumd`.

The daemon's optional `ssf` configuration controls bounds, not destinations:

```text
ssf: {receiverMax: 1024, leaseMax: 300, errorMax: 5}
```

`receiverMax` bounds live registrations globally. `leaseMax` caps each requested
lease. Receivers expire at their lease deadline, including across daemon
restarts. An expiry-ordered index drives deadline cleanup without periodic
receiver scans. `errorMax` removes a receiver on that many consecutive failed
deliveries across its notifications; successful delivery clears the counter.
Renewing an unchanged callback preserves the counter. Removed or replaced
registrations invalidate queued delivery attempts. Notification retries are
bounded by refresh-family expiry and use exponential backoff.

The daemon starts its multiplexer and Zdb runtime independently of cluster
activation. Zdb's `up` callback starts the network listener after bootstrap and
signing preparation; `down` stops it. A standby keeps its runtime and native
telemetry available but has no HTTP listener, including health routes.
`zumd: active` means activation and preparation completed. An active node exposes
`/health/live`; a new store still needs initial-admin enrollment before
`/health/ready` succeeds, returning 503 until then.

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
one fails startup without falling back to Vault. The daemon requires a public
issuer and an explicitly selected secure Vault store. Initial bootstrap also
requires an administrator login and an owner-only enrollment URL output file:

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
the administrator passkey. Normal restarts can omit `--admin` and
`--bootstrap-output`. `--bootstrap-reissue` requires `--bootstrap-output` and uses the persisted
administrator identity. Before initial enrollment, it replaces the pending
capability. After enrollment, it issues a capability to add another passkey,
preserving the user's handle, existing credentials and assignments. Reissue
invalidates the previous capability or unfinished ceremony; the new capability
expires and can be used only once. It does not reset IAM or suspend the user.
Use the [`zum`](src/zum.cc) administrative client
to enroll applications, users, clients, assignments and catalogs. Its basic
interface is documented in [management operations](#management-operations):

```sh
zum --config FILE login
zum --config FILE app add NAME AUDIENCE > enrollment.json
zum --config FILE RESOURCE [SUBRESOURCE] VERB REQUIRED_ARGS... [OPTIONS]
```

The daemon also supports `--once` for activation/bootstrap maintenance.
For databases affected by dropped administrator client string vectors, stop the
daemon and add `--once --repair-admin-client` to its normal startup command
(without `--bootstrap-reissue`). This restores missing seeded native callback
redirects and `openid`, `profile`, `email` identity scopes through Zdb, preserving
enrolled credentials and populated client settings. Restart normally afterward.

For offline encrypted-database key rotation, stop all writers and run `--rekey`
with `ZUM_DB_KEY` set to the **new** key. The old key is loaded from Vault;
after the database change is durable, the new key replaces it there. Completion
is reported only after both steps succeed. If Vault publication fails after
the database commit, retain the same new key and retry `--rekey`; do not
generate another key. Keep the new key available until completion is reported.

## Further documentation

- [Management operations](#management-operations) — routes, actions, public
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
and application. Schema version 24 uses assignment terminology throughout the
database and management API; existing databases require reprovisioning.


---

## Management operations

[ZumMgmt.hh](src/ZumMgmt.hh) is the shared wire catalog for Zum
administration. Request and response JSON fields and management query
parameters use snake_case (`app_id`, `client_id`, `role_ids`, etc.).
C++ fields and native configuration retain their existing names. Its 61
`MgmtOp` values each identify exactly one remote call and one core action named
`Zum.<operation>`. The route registry in
[ZumMgmt.cc](src/ZumMgmt.cc) is the sole method/path mapping used by `zumd`,
the `zum` client, and registry tests.

The catalog is intentionally operation-oriented rather than the Cartesian
product of CRUDQ and tables. Query operations are collection GETs with exact
indexed filters or bounded pagination. Lifecycle, assignment, cleanup,
publication, and secret rotation have distinct operations because they have
different authorization and consistency rules. Zdb saga infrastructure has no
public management operation.

### Authorization ownership

Bootstrap seeds the catalog in the core Zum application. The core
`zum.admin` role contains every management action. The distinct
`zum.catalog` role contains only operation-status lookup and catalog
publication. Enrolled catalog publishers never receive `zum.admin` implicitly.

A bearer token must carry the exact operation action. Current database state
must additionally prove either the active core superuser assignment or an
active `admin_access` delegation for the authenticated user/client and target
application. The service which owns an application may publish that
application's available action/standard-role catalog. Each role name is also
the corresponding OAuth resource-scope name at runtime; catalog publication
never assigns roles to users or restores deleted privileges.

### Areas

| Area | Operations |
| --- | --- |
| Issuer/catalog | `issuerQuery`, `operationQuery`, `catalogPublish` |
| Applications | `appQuery`, `appEnroll`, `appUpdate`, `appState` |
| Users/credentials | `userQuery`, `userInvite`, `userUpdate`, `userState`, `userRecover`; `credentialQuery`, `credentialUpdate`, `credentialState` |
| Assignments | `assignmentQuery`, `assignmentAdd`, `assignmentRoles`, `assignmentState` |
| Actions/roles | `actionQuery`, `actionAdd`, `actionState`; `roleQuery`, `roleAdd`, `roleUpdate`, `roleActions`, `roleState`, `roleDelete` |
| Clients | `clientQuery`, `clientAdd`, `clientUpdate`, `clientState`, `clientSecretRotate` |
| Delegation | `clientAccessQuery`, `clientAccessSet`, `clientAccessState`; `adminAccessQuery`, `adminAccessSet`, `adminAccessState` |
| Federation | `providerQuery`, `providerAdd`, `providerUpdate`, `providerState`; `authPolicyQuery`, `authPolicySet`; `roleMapQuery`, `roleMapSet`, `roleMapDelete`; `identityQuery`, `evidenceQuery` |
| Runtime state | `sessionQuery`, `sessionRevoke`; `consentQuery`, `consentRevoke`; `grantQuery`, `grantRevoke`, `grantCleanup` |
| Keys | `signKeyQuery`, `signKeyAdd`, `signKeyRetire` |

The route registry defines the method/path table.
[Zum terminology](../zum_terminology.md) defines the application, principal,
role, action and scope vocabulary. Schema version 24 requires reprovisioning existing databases. It renames
the assignment table, saga identities, authority-version fields and management
operations; previous schemas and REST names are unsupported.

### HTTP rules

External identities remain bound to provider/issuer/subject. Their projected
names are synthetic `oidc:<provider>:<user_id>` identifiers; a browser login hint
does not rename or link that identity. Inviting a local user with the same
stored projected name invalidates the external user's existing session/grant
authority versions within the invitation saga. A local account's presence also
blocks new external issuance for that name, including when the local account is
pending or suspended. No identity, assignment or role assignment is transferred.
Issued access tokens retain their documented expiry boundary. The current
UserInvite definition and physical schema are covered by the SQLite staged
recovery suite. Prior schemas and saga definitions are unsupported; backward
compatibility and data migration are non-goals.

Multiplexer and Zdb startup are independent of database role activation. Zdb's
`up` callback starts network listeners after bootstrap and signing preparation;
`down` stops them and drains admitted work. A standby process keeps its runtime
and native telemetry available without an HTTP listener or HTTP health routes.
The active listener exposes `/health/live`, while `/health/ready` returns 503
until initial-admin enrollment completes on a new store. `zumd: listening` means
the active listener was bound; `zumd: active` means active-node preparation and
admission startup completed. Invalid-key or schema failure must not activate
listeners. See the implementation ledger for runtime verification status.

Administrative paths have the `/admin` prefix. GET carries filters in the query string
and no body. Mutations carry JSON; DELETE has no body. Known paths called with
an unsupported method return 405 with a registry-derived `Allow` header.

Existing-record writes require `If-Match`; PUT creation requires
`If-None-Match: *`. Creation, publication, rotation, and bounded bulk
mutations require `Idempotency-Key`. Secret digests, private/protected key
material, provider secrets, tokens, credential public keys, and server session
handles are never returned by ordinary queries.

After authentication and authorization, non-GET management operations
emit request-level `Administration` events through ZiLog. An event carries the exact
catalog operation ID, authenticated actor, target application and path,
success/failure outcome, and the same opaque correlation ID returned in an
error response. Request bodies, query values, credentials, and secrets are not
copied into event metadata. Domain workflows may additionally emit their more specific
credential, revocation, key-rotation, or principal-change events.

Logging is outside business sagas; compensation restores application state, not
log history. Alert persistence, distribution, replay, and retention use existing
Ztc facilities. There is no Zum audit table or audit query/cleanup API in the
data model. The HTTP fixture checks ZiLog attribution, outcome, correlation
and secret redaction.

### Administrative client

The administrative interface is:

```sh
zum --config FILE login
zum --config FILE app add NAME AUDIENCE > enrollment.json
zum --config FILE RESOURCE [SUBRESOURCE] VERB REQUIRED_ARGS... [OPTIONS]
```

Offline database-secret key rotation is a server maintenance operation, not a
REST administrative action. Stop all writers and run
`zumd --rekey --issuer=URL` with the normal Zdb connection/configuration and
the same secure Vault store and account as the running daemon. The old key is
loaded from Vault; supply the new base64-encoded random 256-bit key as
`ZUM_DB_KEY`, never on the command line. Maintenance does not start an HTTP
listener. An incomplete rotation blocks normal readiness; retry with the same
new key. Completion means both the database store drain and Vault publication
succeeded. Retain the new key until completion, and retain old keys separately
for old backups. Afterward, normal starts can omit `ZUM_DB_KEY`. A completed
retry verifies ciphertext without rewriting it. This changes encrypted secret
fields only, not signing keys, OAuth client-secret values, user assignments or
ordinary database fields.
See IMPLEMENTATION.md for the current validation status of this operation.

The non-secret configuration contains the exact application `issuerURL`
(`https://auth.example/oauth2/APP_ID`) and the independent administrative
`managementURL` origin. It may also contain
`clientID` (default `zum-admin`), `scope` (default `zum.admin`), `callbackPort`,
`loginTimeout`, and optional `caPath` for a private issuer CA. Empty `caPath`
retains native system trust. Login uses an external browser, a loopback
redirect, authorization code, and S256 PKCE. The CLI stores credentials through
`Ztls::Vault` under `$HOME/.zum` (or `ZUM_HOME`) and opens the vault only for
each load/save. A missing stored credential triggers browser login.

Required request fields are positional, in the order shown by `zum --help`.
Commands form a hierarchy, such as `zum user add`, `zum user list`, and
`zum client access set`. Use `zum user --help` or `zum client access --help`
to show a command group's syntax. The `assign` commands call the
`assignmentQuery`, `assignmentAdd`, `assignmentRoles` and `assignmentState`
operations under `/admin/apps/{app_id}/assignments`.
Optional fields use named options (`--label`, `--redirect-uris`,
`--refresh-allowed`, etc.). Lists are comma-separated; an empty argument denotes
an empty list. Boolean values are `true` or `false`. `--grants` takes named
flags separated by commas: `AuthCode`, `ClientCredentials`, and `Refresh`.
For example, `--grants AuthCode,Refresh` selects authorization code and refresh.
The REST `grants` field uses the same comma-separated names. The nested catalog
supplied to `catalog publish` is an individual JSON object argument. `zum` constructs the
request JSON, quotes strings, and encodes IDs without floating-point conversion.
`app add` enrolls an application (`appEnroll` is its wire operation).
`user add NAME` invites a user to enroll a passkey. `user list [NAME]` accepts
an optional positional name filter; omit it to list users or filter by `--id`.
The `assign` group manages a user's application assignment: `assign add APP_ID
USER_ID` creates it, `assign roles APP_ID USER_ID ROLE_IDS` replaces its roles,
and `assign state APP_ID USER_ID STATE` changes its lifecycle state.

Configuration is selected by `--config FILE`, then `ZUM_CONFIG`, then
`$ZUM_HOME/zum.cf` (default `$HOME/.zum/zum.cf`). Browser-login prompts and
diagnostics go to stderr; HTTP response JSON goes to stdout, including
secret-bearing enrollment, invitation, recovery and client responses. Use
`umask 077` before redirecting those responses to a new file.

Transport controls are command-line options:

| Option | Meaning |
| --- | --- |
| `--if-match ETAG` | `If-Match` header; preserve the ETag's embedded quotes |
| `--if-none-match '*'` | `If-None-Match` header for creation |
| `--idempotence KEY` | Optional explicit `Idempotency-Key` for retrying a request |

For operations which need idempotency, `zum` generates a random 128-bit key once
per invocation and retains it through token refresh and request replay. A failed
HTTP request reports that key on stderr for an explicit retry. A new invocation
without `--idempotence` is a new operation; retain successful responses and do
not blindly rerun creates. Use `zum operation list --operation appEnroll
--idempotence KEY` to look up an enrollment's status with its key.

For example:

```sh
export ZUM_CONFIG=/path/to/admin.cf
umask 077
zum app add ztchub-local http://localhost:8090/admin > hub-enrollment.json
zum role list APP_ID --name Client
zum assign add APP_ID USER_ID > assignment.json
zum assign roles APP_ID USER_ID ROLE_ID --if-match '"ETAG"'
zum client add APP_ID native --label "Local zdash" \
  --redirect-uris http://127.0.0.1:8081/callback \
  --grants AuthCode,Refresh --refresh-allowed true > dashboard-client.json
```

#### Manual browser/passkey acceptance

For the required manual acceptance run, use a fresh PostgreSQL store and a real
HTTPS issuer origin (normally a TLS reverse proxy to the loopback `zumd`
listener). Inject `ZUM_DB_KEY`, `ZDB_MODULE`, and `ZDB_CONNECT` through the test
deployment, then start the daemon with the intended issuer, RP ID, administrator
login, and an owner-only bootstrap output file, for example:

```sh
zumd --issuer=https://iam.example.test \
  --rp-id=iam.example.test --admin=admin@example.test \
  --bootstrap-output=/run/secrets/zum-bootstrap-url
```

Open the one-time URL from that file in a normal browser and create the initial
platform passkey. Configure `zum` with the core application's issuer URL shown
by discovery, the `zumd` origin as `managementURL`, the `zum-admin`
client, a loopback callback, and (when applicable) the private test CA, then run
`zum --config FILE login`. The browser must complete the passkey assertion and
return to the state-bound loopback callback. Use the authenticated CLI to enroll
the ping application and user as described in the example README, inject only
`ZUM_CLIENT_SECRET` for its same-name default client into `zumpingd`, and run
`zumping` without `--no-browser`.
Record the browser/platform authenticator and observed login/pong result in the
implementation ledger. The virtual authenticator fixtures do not satisfy this
manual gate.

### Public OAuth 2.0 and OIDC endpoints

Outbound OIDC-provider connections use system TLS trust by default. For a private
CA, add `oidc: {caPath: "/path/to/ca.pem"}` to the `zumd --config` node file.
The path can name a CA bundle or native CA directory; it is non-secret deployment
configuration. Certificate and hostname verification remain enabled. OIDC-provider
client secrets remain encrypted in the database, not in this section.

The administrative routes above are separate from the issuer's public protocol
surface:

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/.well-known/oauth-authorization-server/oauth2/{app_id}` | OAuth authorization-server metadata. |
| GET | `/oauth2/{app_id}/.well-known/openid-configuration` | OpenID Provider metadata. |
| GET | `/oauth2/{app_id}/v1/authorize` | Authorization-code initiation with S256 PKCE. |
| POST | `/oauth2/{app_id}/v1/token` | Authorization-code, refresh-token, and client-credentials exchange. |
| POST | `/oauth2/{app_id}/v1/revoke` | Token revocation. |
| GET | `/oauth2/{app_id}/v1/keys` | Active public signing keys for that issuer. |
| GET/POST | `/oauth2/{app_id}/v1/userinfo` | Claims selected by an access token carrying `openid`. |

An interactive request may combine a role-derived resource scope with the enrolled client's
`openid`, `profile`, `email`, and (when refresh tokens are enabled)
`offline_access` identity scopes. `offline_access` always requires interactive
consent; `prompt=consent` explicitly selects that path, while `prompt=none`
fails with `consent_required` when consent is needed. The exact granted scope
string and original nonce survive code exchange and refresh. An approved
`offline_access` request creates the refresh-token family; an ordinary code
exchange does not. `openid` causes an ES256 ID token to be returned; `profile`
and `email` control only their corresponding ID-token and UserInfo claims.
Identity scopes are not accepted for client-credentials grants.

### Application audience

`appEnroll` requires `audience`, the immutable token audience of the
application/service. `appQuery` returns this field. Application state controls
its availability. `clientAccessSet` accepts `role_ids`; the URL identifies the
target application. `consentRevoke` can filter by user, client and application.

`ssfRegister` (`POST /admin/apps/{app_id}/ssf`) registers or renews a client's
SSF receiver. It uses the application's existing `catalogPublish` delegation
and requires a workload token. The JSON fields are `receiver_id`, `delivery_url`,
`callback_auth`, and `expires_in` (default 300 seconds). The response supplies the
server-capped `expires_in` and Unix `expires`. No idempotency key is required:
each successful call extends the lease. Receiver IDs are scoped by application
and authenticated client; the notification audience is the application's
immutable audience. Clients must renew before expiry. `Zum::Service` handles
registration and timer renewal automatically.
