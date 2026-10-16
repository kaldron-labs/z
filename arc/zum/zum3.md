# Zum: multi-application OIDC provider and SSO

## Objective

Evolve Zum into a shared, OIDC-capable identity provider for multiple
applications. Users authenticate once at the provider and receive access to
individual applications according to their memberships and application roles.
Authentication is shared; authorization remains application-specific.

`zumd` becomes the core server: a Zdb application that owns database reads and
writes, serves administration through `zrest`, and implements the OIDC provider
and SSO service. It supports standalone and replicated cluster deployments,
with activation controlled by Zdb.

The built-in `zum` application owns identity and application administration.
Its initial superuser can add users, register applications, assign memberships,
and configure application audiences, actions, roles, and clients. Resource
scopes are derived from the application's role names.

This document is an implementation plan, not a description of completed
functionality. The management operation catalog currently defines a request
and permission contract; its remote transport and dispatch remain to be built.
Existing upstream OIDC authentication is separate from becoming an OIDC
provider for downstream applications.

`zumd` also acts as an OIDC proxy (identity broker): it can delegate user SSO
to an upstream provider such as Okta while retaining local ownership of
applications, memberships, actions, roles, and access-token issuance; resource
scopes are derived from roles at request time.
Administrators configure delegation and upstream-to-local role mappings through
the `zum` REST client. Zum-native applications integrate only with `zumd`.

Support two downstream integration paths with the same authoritative IAM model:

- Zum-native services such as `zumpingd` use service-side `libZum` to delegate
  authn/authz to `zumd`. Their end clients use ordinary OAuth/REST endpoints on
  the service, without `libZum` or an upstream OIDC integration.
- Other applications, including non-Z and non-`libZum` applications, can be
  enrolled as ordinary OIDC clients of `zumd` and use its provider endpoints
  directly with their own standards-based client implementation.

In either path, optional upstream OIDC client work belongs to `zumd`: discovery,
authorization transactions, callbacks, code exchange, identity validation, and
upstream role interpretation. Neither `libZum` nor `zumpingd` implements that
upstream client. With no upstream configured, both downstream paths work against
Zum's local identity store; OIDC provider functionality does not require federation.

## Authority modes

In local IAM mode, `zumd` independently provides authentication and authorization
for numerous applications. Its database is the only authoritative source of
users, application memberships, and role assignments. No upstream is required.

OIDC proxy mode retains local users and their local application/role assignments,
including the bootstrap superuser and administrators who enroll applications.
Authentication is local-first: if the user exists as a local identity, authenticate
locally and use local assignments. Only if that local user does not exist does
`zumd` delegate to upstream OIDC SSO, obtain the configured scopes and validated
role claims, and map them to local role IDs for the requested application using
the mappings stored in its database. The upstream is authoritative for delegated
users and assignments; it does not replace local identity authority.

`zumd` retains ownership of application integration configuration, audiences,
actions, role definitions and their action bundles, mappings, and downstream
token issuance. A role definition specifies what may be done; a role assignment
specifies who receives it. Each active role name is also the name of its
role-derived resource scope at request time.

Both modes expose the same downstream interfaces. Applications continue using
`libZum` and `zumd`, including authorized REST calls to manage locally owned
actions and configuration, without integrating with the upstream themselves.

Authority mode is separate from topology: both modes support a standalone Zdb
node or a replicated cluster. Application policy configures whether upstream
fallback is available and which provider and mappings it uses. Authority source
is selected per authenticated identity; never silently combine local and
upstream assignments.

## Components and build layout

| Component | Responsibility | Build location |
| --- | --- | --- |
| `zumd` | Core IAM and REST administration server; OIDC provider for enrolled clients and optional upstream OIDC client on behalf of its apps; owns the database and automatic bootstrap. | `zum/src` |
| `zum` | Administrative REST client of `zumd`; manages users, applications, memberships, roles, and the other cataloged operations. | `zum/src` |
| `libZum` | Service-side integration library encapsulating authentication and authorization against `zumd` for services such as `zumpingd`; not an end-client SDK. | `zum/src` |
| `zumpingd` | Example application server using `libZum` to integrate with `zumd`; exposes a protected ping/pong REST API. | `zum/example` |
| `zumping` | Pure REST client of `zumpingd`; authenticates through that service's REST contract, with no `libZum` dependency or direct `zumd` integration. | `zum/example` |

Build and install `zumd` and `zum` alongside the library from `zum/src`.
Replace the current example `zumd`/`zumc` pair with `zumpingd`/`zumping`.

Refactor the current server-oriented library implementation into `zumd` server
components. The public `libZum` interface becomes the service-side interface
for authentication and authorization against `zumd`, including protocol
integration and token validation required by application servers. It is a client
of `zumd`, not a library for end clients of those application servers.
It supplies the service's OAuth endpoint integration and authorization checks,
not an upstream OIDC relying-party implementation. Upstream configuration,
protocol state, callbacks, and claim mapping remain in `zumd`.
Keep server database operations and bootstrap out of that public service API.
Share protocol contracts and management operation identities where needed
without requiring application clients to open Zdb or host the identity provider.

The built-in `zum` application is an authorization owner; the `zum` executable
is its administrative client. Neither is the `zumd` server process. `libZum`
obtains and validates Zum-issued authority; application handlers still enforce
the required action and resource-specific business rules.

## Core server and clustering

Promote `zumd` from an example program to the installed core server, using
`zrest` for its REST and provider endpoints and `zdb` for persistence,
replication, and activation.

`zumd` owns all online reads and writes to the identity database. Applications,
administrative clients, and user interfaces access it through its APIs.
Administration and OIDC requests use the same authoritative model and lifecycle
rules. The server loads persistent configuration and automatically bootstraps
the system on an empty database. Normal startup preserves existing data;
bootstrap creates the core Zum records, not demo application data or fixed
signing keys.

Separate node startup configuration (store/cluster settings, listeners, public
issuer, database-secret encryption key, protected signer access, and initial administrator) from replicated
configuration managed via REST. Zdb activation is necessary but not sufficient
for readiness: bootstrap/recovery must finish and signing must be available
before normal authentication and administration requests are admitted. Initial
enrollment becomes available once its bootstrap records are committed.

Run the server as a Zdb application using Zdb replication and activation
callbacks. Zdb decides when a node is active; `zumd` must not implement a
separate election or independently promote a node. Admit database-dependent
requests only while activated by Zdb. On deactivation, close admission and
drain or cancel in-flight operations according to the Zdb lifecycle before
releasing their state. Define an explicit unavailable response or routing
behavior for inactive nodes; do not serve authorization decisions from stale
replicas.

`zumd --config FILE` loads native `zdb` and `mx` configuration sections for
node-specific store/host/replication settings. Without it, retain the standalone
defaults. The request scheduler thread is resolved by the name `shard`, not a
fixed numeric index; `rx` and `tx` remain the HTTP transport threads. `MODULE`,
`CONNECT`, and `DEBUG` substitutions come from the existing startup inputs.
The database-secret encryption key remains environment-only and is never a
configuration-file substitution. Cluster settings use Zdb's native schema and
semantics; this option introduces no Zum election or replication protocol.

Persist users, configuration, memberships, grants, provider sessions, and
recoverable multi-record operations through Zdb so failover preserves their
invariants. Replication, commit durability, and failover guarantees are Zdb's
responsibility, not a second protocol implemented by Zum. Zum implements its
single-use code, capability, and refresh transitions using Zdb operations and
obeys activation and completion callbacks. A persistence failure must not be
reported as a successful authentication or mutation.

Cluster nodes serve one logical issuer. Configure a stable public issuer URL
and consistent access to protected signing providers and any secrets needed
to validate browser cookies. Node addresses and leadership changes must not
change token issuer or browser-session identity. Preserve Zdb shard ownership
and the framework's Rx/Tx ownership rules when connecting REST handlers to
database operations.

## Database secret encryption

This is selective field-level encryption of sensitive secrets only, not
whole-database, table, page, or storage-volume encryption. Ordinary records and
fields such as application IDs, users' non-secret profile data, memberships,
action and role definitions remain in their normal queryable
representation. Never include plaintext secrets in diagnostic events.
If whole-database encryption is required, it is a PostgreSQL deployment/storage
concern, outside Zum's encryption responsibilities. Zum does not implement it.

`zumd` requires `ZUM_DB_KEY`, injected through its process environment by the
deployment's secrets manager. It contains a base64-encoded, cryptographically
random 256-bit key, not a password. This is a server-only secret: it is not one
of the `libZum` service connection settings and must never be supplied
to `zumpingd`, `zumping`, or other enrolled applications.
`ZUM_DB_KEY` is the only supported source of this key: do not add a command-line
option, configuration-file field, database fallback, or generated default.

The key protects recoverable secrets persisted in Zdb, including upstream client
secrets and retained upstream tokens, credential material that must be recovered
for use, and private signing material if stored in the database. External signer
references and public keys do not require secret encryption. Keep one-way
verifiers for inbound client secrets and other credentials where recovery is
unnecessary; encryption is not a reason to store recoverable copies of them.

Encrypt only the sensitive secret fields before passing records to Zdb so
replication, snapshots, and database backups contain ciphertext for those fields,
not plaintext secrets; the rest of each record remains unchanged. Use authenticated
encryption, initially AES-256-GCM, with a versioned envelope containing algorithm,
key identifier, nonce, ciphertext, and authentication tag. Bind issuer, record
identity, and field purpose as associated data to reject ciphertext substitution.
Nonce generation must meet GCM uniqueness and usage limits across all writers
sharing the key and across restarts; never derive nonces solely from a restartable
process counter. Follow [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final).

Never persist the encryption key alongside ciphertext or return it through REST,
diagnostics, or logs. Keep it separate from OAuth client secrets and token-signing
keys. Bootstrap requires the supplied key and creates an authenticated key-check
record; it must not silently generate a replacement on restart. Missing, malformed,
or incorrect keys block readiness. Authentication-tag failure is an error, never
a reason to accept plaintext or reset the affected record.

Every cluster node that may activate must receive the same applicable key
material externally; Zdb replicates encrypted records, not the key. Operators
must retain separately protected key backups matching retained database backups.
Loss of the key makes the encrypted secrets unrecoverable. This protects stored
secrets, not all database metadata or a compromised running server.

Initially rotate this key through an explicit offline maintenance operation:
stop server writers, supply old and new keys through protected environment
injection, re-encrypt with fresh nonces, and verify completion before restarting
nodes with the new `ZUM_DB_KEY`. Keep versioned, resumable progress and both keys
until completion; never treat a changed startup variable as automatic rotation.
Retain old keys separately for old backups. Online multi-key rotation is a later
extension, not implied by ordinary client-secret rotation.

The maintenance invocation is `zumd --rekey --issuer=URL` with the normal Zdb
connection/configuration settings. `ZUM_DB_KEY` supplies the old key and
`ZUM_DB_NEW_KEY` supplies the new base64-encoded 256-bit key; neither has a
command-line or configuration-file equivalent. This mode does not start HTTP
or bootstrap an empty database. Stop every service writer before invoking it.
Persist a pending target-key check before replacing ciphertext and reject
normal startup while that marker remains. Resume with the same two keys;
native saga replay recovers committed steps, and already-rewrapped fields are
authenticated under the target key. Verify every encrypted field under the
new key before clearing the marker and switching the issuer key check. Drain
store writes before reporting completion. A repeated completed invocation
verifies without replacing ciphertext. This is not an online rotation API.

## Ownership and identity

An application is the ownership boundary for authorization configuration.
User identities and their credentials remain global to the provider.

Keep three distinct entities:

- **Application:** the organizational authorization boundary.
- **OAuth client:** a registered frontend, native app, or backend integration.
- **Audience:** the protected API or service intended to accept a token.

One application may own several clients and audiences. Several clients may
access an audience when explicitly authorized. Client ownership does not
implicitly authorize access to other applications.

### Application enrollment and credentials

Enrolling a server application through `zum` must create its application record
and initial confidential OAuth client registration as one recoverable operation.
Return `appID`, `client_id`, and a generated `client_secret`, together with the
issuer and configured integration settings. `appID` identifies the authorization
owner; `client_id` identifies the authenticating client registration. Keep these
fields explicit because an application may have multiple clients. The initial
client is the application's service identity, so a normal `zumpingd` enrollment
immediately supplies the ID and secret needed for subsequent service startup.

Use OAuth client credentials, not an additional proprietary app-password
mechanism. `zumpingd` and other services configure `libZum` using ordinary service
configuration for non-secret connection settings and environment injection only
for the secret:

| Setting | Source | Purpose |
| --- | --- | --- |
| `zum.issuerURL` | Service configuration | Trusted `zumd` issuer URL; not secret. |
| `zum.clientID` | Service configuration | Enrolled service's OAuth client ID; not secret. |
| `ZUM_CLIENT_SECRET` | Environment variable | Service's OAuth client secret. |

The dotted configuration names denote fields in the service's `zum` configuration
section. Do not require `ZUM_ISSUER_URL` or `ZUM_CLIENT_ID` environment variables.
The service passes the loaded settings and injected secret to `libZum`.

Inject `ZUM_CLIENT_SECRET` into the process environment through the deployment's
secrets manager. Do not require a proprietary secret-file or secret-reference
configuration mechanism. Do not log or dump the secret or the full environment.
Missing or empty required values fail service initialization without disclosing
their contents. Secret rotation requires refreshing the injected environment
and restarting the service; updating a secrets-manager value alone does not
change an already-running process's environment.

There is no `ZUM_APP_ID` runtime variable. `zumd` resolves and enforces application
ownership from the authenticated configured client ID; the application ID remains an
administrative/schema identifier, not redundant service connection configuration.
`libZum` authenticates that service to
`zumd`; for authorized service REST operations it obtains a short-lived token
using `client_credentials` with `client_secret_basic`. That token is limited to
the approved audience, scopes, and target-app administrative delegation. The
service may publish its own catalog only when enrollment explicitly grants that
permission; possession of the secret does not imply superuser authority or user
role assignments. The service identity used to authenticate a facade request
does not replace the end-client identity bound to its authorization code.

Issue the same standard client ID/secret credentials for enrolled confidential
non-Z OIDC clients. Register allowed grants explicitly: an interactive OIDC
registration does not automatically receive `client_credentials` management
access. Public native/browser clients, including `zumping`, receive a client ID
but no usable shared secret; they authenticate user authorization flows with
PKCE. Never distribute `zumpingd`'s service secret to `zumping` or the browser.

Generate high-entropy secrets at `zumd`. Return plaintext only in the protected
creation or rotation response, never in ordinary queries, diagnostic events, or
logs. `zum` must deliver newly issued credentials to the administrator's protected
provisioning workflow for storage in a secrets manager and runtime environment
injection; avoid command-line secret arguments. Store a verifier rather
than recoverable plaintext for inbound client-secret authentication. Outbound
upstream credentials are separate protected secrets because `zumd` must use them.

Enrollment retries must resolve to the same application/client IDs rather than
duplicate records or silently regenerate credentials. If the creation response
is lost, the administrator queries the operation and explicitly rotates the
secret to obtain a replacement. Rotation preserves IDs, issues a new secret,
and explicitly retires the old credential; any overlap is bounded and configured.
Restart and catalog publication never rotate or reset secrets. Client/application
suspension blocks new service tokens and client-authenticated operations;
already-issued tokens follow the documented expiry/revocation contract.

This follows the confidential-client enrollment model documented by
[Okta](https://developer.okta.com/docs/guides/find-your-app-credentials/-/main/),
[Auth0](https://auth0.com/docs/secure/application-credentials), and
[FusionAuth](https://fusionauth.io/docs/get-started/core-concepts/applications).
Their identifiers are not universally modeled alike: FusionAuth uses the same
value for application ID and OAuth client ID. Zum retains its explicit
application/client distinction while exposing standard OAuth credentials.

| Record | Ownership and purpose |
| --- | --- |
| Application | Stable ID, unique name, display name, lifecycle state, action-ID counter, authorization version. |
| User | Global local-authoritative identity, or a provider-qualified projection of an upstream identity. |
| Credential | Global user authentication material and credential lifecycle. |
| Membership | Application + user; local-authoritative assignments for local identities, or upstream-derived assignment evidence for delegated identities. |
| Action | Application-owned operation with a stable local bitmap position. |
| Role | Application-owned bundle of actions; its stable name is also its derived resource scope. |
| Resource scope | Runtime view of one application role; no persisted scope record. |
| Audience | Application-owned protected resource with a unique token audience identifier. |
| OAuth client | Application-owned registration with client ID, public/confidential type, authentication method, secret verifier/version lifecycle, allowed grants, and explicitly permitted target audiences and identity scopes. |
| Client application access | Target-application approval for a client, with allowed audiences and, for workloads, local role assignments and authority version. |
| Grant | Issuer, requesting client, purpose, target application, optional resource audience, and applicable identity/authority snapshot. |
| Browser session | Provider authentication session, separate from application sessions and grants. |
| Issuer | Provider identity and issuer-level infrastructure. |
| Signing key | Issuer-level signing infrastructure and lifecycle. |
| OIDC provider | Issuer-owned upstream connection: provider identity, client registration, protected credential reference, requested scopes, claim configuration, and lifecycle. |
| Application authentication policy | Local-first login with optional upstream fallback, provider and mapping configuration, and policy version. |
| External identity | Binding of a configured provider and validated upstream `(iss, sub)` to a global Zum user. |
| OIDC role mapping | Application + provider + upstream role value mapped to one local application role ID. |

Add `zum.app`, `zum.membership`, and `zum.audience`. Introduce browser-session
persistence as part of the SSO design. Internal saga records remain persistence
machinery rather than remotely editable application data.

Add replicated records for client application access, upstream OIDC connections,
application authentication policies, external identity bindings, and role mappings.
Use the concrete table names and keys in the implementation contract below.
Replace reliance on a single unqualified
`User::oidcSub` with provider-qualified identity bindings.

Move `User::roleIDs` into membership records keyed by `(appID, userID)`.
Include `appID` in authorization-record keys and lookups. Names may repeat
across applications; role/action references must stay within
the owning application. Each audience resolves to exactly one application.

Resource scope resolution is `(appID, role.name)`; the target audience is
selected and authorized independently. OIDC identity scopes such as `openid` are
requests for identity claims, not audience-bound action bundles. Process them
separately; identity-only OIDC login does not require inventing a business-API
audience or resource scope.

Use application-local action IDs and bitmaps, with `nextActionID` on the
application. Carry application identity wherever a bitmap is persisted or
passed between components. Never interpret an action position without its
application context, and never reuse allocated positions.

## Application-specific authority

Interactive access requires an eligible identity and application assignment from
the selected identity authority, plus an active local application. Local identities
use local users and memberships in both modes. For an absent local identity,
proxy mode validates upstream identity and application assignment and maps upstream
roles, without requiring an independent local membership grant. Intersect the
resulting authority with local role/action
state, resource scopes, client restrictions, and server policy.

Workload `client_credentials` access has no user membership or browser session.
Represent its assignments as explicit client-to-target-application authority,
with lifecycle/version checks equivalent to interactive membership. Its role
references belong to the target application, including when the client is
owned by another application.

For local identities in either mode, user suspension affects applications using
that identity; membership suspension/removal affects the selected application.
For delegated identities, upstream identity and assignment changes govern
eligibility. Local application,
role, or action suspension can restrict access in either mode. Authority
resolution and refresh check the appropriate source and local policy.

Persist target application identity, authority source, and relevant local
versions or upstream assignment evidence/freshness in codes and refresh families.
Refresh must revalidate assignments from that source and preserve
the original authority ceiling: a later role expansion must not enlarge an
existing grant. Resource access tokens remain restricted to one target audience.
Changes are enforced during new authorization and refresh. Already-issued
self-contained tokens remain usable until expiry unless an explicit online
revocation check is introduced; document the same boundary in `libZum`.

Cross-application client access requires explicit authorization. A client
administrator must not gain access to another application merely by editing
the client's audience list. Store target-application approval in addition to
client-side configuration; expansion requires authority over the target.
Disabling either application's relevant access or the approval prevents further
issuance and refresh.

For resource authorization, the audience's owner is the target application and
determines membership, role mapping, and required authentication policy. Enforce
any additional client authentication requirements too; reject incompatible
requirements rather than weakening either policy. For identity-only
OIDC login, the client's owner is the target application. An ID token's audience
is always the requesting OAuth client, not the resource audience.

## Built-in Zum application and bootstrap

The `zum` application owns the remote management operations. Each operation
has one corresponding permission in the shared management catalog. Bootstrap
derives its action records from that catalog rather than generating CRUD
combinations from table names.

Move the bootstrap implementation from the `zum` utility into `zumd`. Once
activated by Zdb, `zumd` detects an empty database and automatically initializes
the system. Only the active server may execute bootstrap; an inactive replica
must not interpret its local startup state as permission to initialize.

System bootstrap creates:

1. The issuer and built-in `zum` application.
2. Its management audience, scopes, and operation-derived actions.
3. Its `superuser` administrative role.
4. The initial administrator's pending user record and membership in `zum`,
   assigned that role.
5. The administrative OAuth client registration needed by the `zum` executable.
6. A signing-key registration backed by configured protected signing material.
7. The bound capability for enrolling the administrator's first credential.

Enrollment completes the precreated pending user rather than inserting another
user. Activate that user only when the first credential is committed. Define
resumable bootstrap state, including capability expiry and protected reissuance,
without resetting an enrolled administrator.

Configure the initial administrator identity and issuer through `zumd` startup
configuration. Define a protected delivery mechanism for the initial enrollment
capability, since no administrator can authenticate before enrollment. Automatic
initialization must not expose an unauthenticated public bootstrap operation.

Specify repeat startup, partial failure, failover during initialization, and
interrupted enrollment behavior. Initialization must be recoverable without
duplicating authority records, replacing an existing administrator, or treating
a partially populated database as a new empty database.

`zum` is the administrative REST client, not an offline bootstrap or database
tool. After the initial administrator enrolls, it authenticates to `zumd` and
invokes the authorized management calls. Backward compatibility and data
migration are non-goals; unsupported stores are rejected.

The Zum superuser administers identities and application authorization
configuration. That administrative authority does not automatically grant
access to other applications' business APIs.

The bootstrap administrator remains a local user when upstream delegation is
enabled. Local administrative login must remain available during upstream outages;
test this recovery path as part of proxy configuration. Enabling delegation must
not convert or replace the local bootstrap identity or its role assignments.

## Remote administration

`zumd` serves the REST administration API using `zrest`. Every cataloged remote
management operation must have an implemented call, request/response contract,
permission check, and ZiLog event behavior. Cover applications, users, memberships,
roles, actions, audiences, clients, credentials, grants, signing keys,
through their identified operations. Logging, alert persistence, distribution,
replay, and retention use ZiLog/Ztc facilities, not Zum administrative endpoints.

Define the HTTP method and resource path for each operation, including named
lifecycle operations. Keep request identity and permission naming in the shared
catalog so bootstrap and dispatch cannot maintain divergent action lists.
Use `zrest` request parsing and response construction, with handlers invoking
the Zdb-owned operations through the existing request admission mechanism.

Extend the shared operation catalog with application creation and lifecycle,
membership assignment/removal, membership role assignment, and audience
management. Make action, role, audience, and client operations application-aware;
resource scopes are derived from roles rather than managed independently.
Replace global `userRoles` with `membershipRoles`.

Include upstream provider registration/query/update/state, application
delegation policy configuration, external identity binding management, and
role-mapping query/add/update/remove calls. Give each remote call its own
catalog permission and ZiLog event behavior. `zum` exposes these operations, including
configuring Okta as the upstream provider for an application. Protected upstream
credentials are not returned by query operations.

Local user and assignment mutations continue to manage local-authoritative
records in both modes. Delegated assignments are managed upstream; REST queries
may expose their provenance and current projection, but local writes must not
override upstream authority. Upstream assignment administration would require an explicit
provider management integration, separate from OIDC login. Locally owned action,
role-definition and application configuration remain manageable through
the same Zum REST operations in either mode.

Keep meaningful lifecycle operations distinct: invitation, recovery, role
assignment, suspension, revocation, secret rotation, and key retirement each
need an explicit request and permission. Queries support exact lookup or
bounded pagination and return projections that exclude protected material.

Separate permission to configure roles from permission to assign them. Define
delegated administration boundaries and prevent administrators from assigning
authority beyond their delegation. Derive logged actors from authentication,
not caller-supplied identity strings.

Remote dispatch must enforce operation permission, application ownership, and
delegation constraints before invoking trusted internal mutation functions.
Internal persistence APIs are not authorization checks.

Management permissions are resolved in the built-in `zum` application even when
the call modifies another application. The target application is a separate
authorization parameter constrained by administrative delegation. Role assignment
and upstream role-mapping changes both grant authority and must obey delegation
ceilings. A business-application role never implicitly authorizes management.

## OIDC provider support

Use `zum` to enroll external OIDC applications as well as Zum-native services.
External enrollment creates the owning application and its OAuth client settings,
including redirect URIs, public/confidential client type, allowed flows/scopes,
and applicable local-first authentication policy. Resource audiences and access
approvals are configured when the app also needs API access. These applications
need neither Z Framework components nor `libZum`, nor an intermediary `zumpingd`.
They use `zumd` discovery, authorization, token, JWKS, and UserInfo endpoints.
Local users and optional delegated users follow the same downstream OIDC contract.

`zumd` is the OIDC-compliant provider for downstream applications. Implement
its provider endpoints through `zrest`, backed by Zdb, in addition to the
existing OAuth and optional upstream OIDC authentication paths:

- OIDC discovery and provider metadata.
- Authorization requests using the `openid` scope.
- ID-token issuance with client audience, subject, nonce, authentication time,
  and the other claims required by the supported profile.
- UserInfo with explicitly permitted identity claims.
- Client registration settings for redirect URIs, supported flows, identity
  claims, and authentication policy.

Use the stable public subject identifiers specified below.
Keep identity claims separate from application permissions. An ID token is
intended for its client; an access token is intended for a resource audience.
Do not treat them as interchangeable credentials.

The initial profile is authorization code with PKCE (`S256`), including for
confidential interactive clients. Do not support implicit or password grants.
Use asymmetric signing with an explicit algorithm allowlist, initially `ES256`,
and publish public keys through the discovery-advertised JWKS endpoint. Keep
retired verification keys available for the maximum lifetime of tokens they
signed. Select new-token signing keys from the issuer's active `ES256` records:
the latest eligible `notBefore` wins, with lexicographically greatest key ID
breaking ties. Exclude future keys and keys whose retirement deadline precedes
the requested access-token expiry. Key selection uses the configured JWKS
catalog bound and fails closed on overflow or no eligible key. Retirement stops
new signing immediately but preserves verification until `retireAfter`.
Public clients use no client secret; initially confidential clients use
`client_secret_basic` over TLS, with protected, rotatable credentials.

Use stable, opaque public subjects within the issuer; do not use email as `sub`.
Pairwise subjects are outside the initial profile. Support `openid`, `profile`,
and `email` as identity scopes, with claim release constrained by client policy.
Support `offline_access` only for clients explicitly permitted refresh grants.
Publish only implemented capabilities in discovery; ordinary OIDC clients must
not require `libZum` to authenticate.

### Interactive login contract

1. Distinguish the administrative client from application end clients. `zum` is
   a CLI REST administration client of `zumd`; its own provider login may use
   native authorization code with PKCE and a registered loopback callback, without
   depending on `libZum`. `zumping` is exclusively a REST client of `zumpingd`:
   it implements the ordinary OAuth authorization-code/PKCE flow, following
   today's `zum/example/zumc.cc`, without depending on `libZum`. It opens
   `zumpingd`'s `/authorize`, receives the code at its loopback callback, and
   posts to `zumpingd`'s `/token`. Register it as a public OAuth client with a
   client ID, permitted redirect URI, audience, and scopes; it has no client
   secret. `zumpingd` uses service-side `libZum` to back these endpoints with
   `zumd`, which remains the authority for authentication, grants, and issuance.
2. Validate client, redirect URI, response type, scopes, and requested resource
   before login. Use the OAuth `resource` parameter to select exactly one
   registered resource audience. Reject unsupported or ambiguous targets. Never
   redirect an error to an unvalidated redirect URI.
3. Reuse an eligible provider session or show a Zum-hosted identifier-first
   login page. Use the identifier only for local-first routing. A local identity
   must complete Zum credential authentication; absence selects the configured
   upstream. Neither application receives local or upstream passwords. A login
   hint is not proof of identity; validate the authenticated subject separately.
4. For upstream login, `zumd` creates a separate code-flow transaction with its
   own state, nonce, PKCE, provider binding, and registered callback. Validate
   issuer, signature, algorithm, audience/authorized party, expiry, nonce, and
   transaction binding before using claims. UserInfo, when configured, must have
   the same subject as the validated ID token. Bind the result by provider and
   `(iss, sub)`, never by an email match to a local account.
5. Resolve target-app eligibility, mapped or local roles, requested scopes,
   and client approvals. First-party enrolled apps may use explicit admin
   preauthorization; other clients require recorded consent. Neither consent
   nor a requested scope creates membership or role assignments. Issue a short-
   lived single-use code bound to client, redirect URI, PKCE challenge, subject,
   target, and granted authority. Return state unchanged to the client.
6. `zumping` checks callback state and posts `grant_type=authorization_code`,
   code, client ID, redirect URI, and PKCE verifier to `zumpingd`'s `/token`.
   `zumpingd` uses `libZum` to request redemption from `zumd`, preserving those
   bindings rather than substituting its own workload identity for the client.
   Recheck current eligibility and narrow authority before issuing tokens.
   Return the OAuth token response with access token, token type, expiry, granted
   scope, and refresh token when authorized. An OAuth-only ping client need not
   request an ID token; OIDC clients requesting one must validate it and its nonce.
7. `zumping` presents the access token as `Authorization: Bearer` when calling
   the ping REST API. It refreshes by posting `grant_type=refresh_token`, the
   refresh token, and client ID to the same `/token` endpoint, replacing stored
   tokens after successful rotation. `libZum` encapsulates token validation and app-qualified
   authority for the service, including issuer, signature/algorithm, token type,
   audience, and expiry checks. The handler requires `ping` and returns `pong`;
   missing/invalid authentication returns 401, valid authentication without the
   action returns 403. API calls do not silently redirect to a login page.

The service exposes an OAuth endpoint facade, not a new pending-login/polling or
opaque service-session protocol. `/authorize` accepts the standard code-flow
parameters; `/token` accepts form-encoded code and refresh grants and returns
standard OAuth responses/errors. `libZum` encapsulates the service-to-`zumd`
transport and preserves client, redirect URI, PKCE, scope, and audience bindings.
Constrain the facade to the enrolled app and its approved clients/resources;
it must not become an arbitrary issuer or token proxy. Tokens remain Zum-issued.
Browser redirects may visit `zumd` and an upstream IdP, but `zumping` sends its
token and resource REST requests only to `zumpingd`. Direct OIDC integration with
`zumd` remains available to other clients; it is not required of `zumping`.

The browser handles both local and upstream login, so changing provider policy
does not change the end client's OAuth/REST integration. Access and refresh
tokens obey the authority and expiry contracts below.
Use TLS except for native loopback callbacks. Keep tokens out
of URLs and logs. Use bounded,
expiring login transactions and one-time enrollment capabilities; enrollment
establishes the precreated user's credential and then enters the normal login
flow rather than serving as an indefinitely reusable administrator credential.

These protocol choices follow [OIDC Core](https://openid.net/specs/openid-connect-core-1_0.html),
[OAuth security BCP](https://www.rfc-editor.org/rfc/rfc9700.html),
[native-app OAuth](https://www.rfc-editor.org/rfc/rfc8252.html), and
[resource indicators](https://www.rfc-editor.org/rfc/rfc8707.html).

### Scope and authority contract

Resource scopes use the standard OAuth request and response vocabulary, but
each application resource scope is the name of exactly one active application
role. The role supplies the action set. A requested scope does not assign the
role; the granted role set is the intersection of requested role-named scopes
with the principal's assigned roles and the client's target-app approval.
Multiple granted scopes union the actions of their granted roles. Identity
scopes never enter this calculation.

Unknown or client-disallowed scopes fail with `invalid_scope`; an invalid or
unapproved resource fails with `invalid_target`. Return the actual granted
scope set. A scope name alone is insufficient to authorize an operation: an
issued resource token also carries the effective app-qualified action snapshot
as Zum-specific claims. `libZum` checks that snapshot, not a fresh expansion of
role names embedded in the token. A scope with no effective actions grants no
API permission. The ping example uses the role name `ping` as its derived
resource scope; there is no separate scope record.

Persist the original granted scope set, selected role IDs, and action snapshot
in the grant. At code redemption and refresh, intersect that snapshot with
current authority and client policy; never enlarge it. Adding actions to a
standard role or adding an upstream mapping can expand only a newly authorized
grant. Removing permissions restricts subsequent issuance/refresh. Already-
issued tokens retain their snapshot until expiry. A role name is a public scope
name, so renaming a role is also a scope-name change and must follow the role
versioning and authorization-state rules.

## Upstream OIDC proxy and role mapping

`zumd` serves two OIDC roles: provider for enrolled downstream OIDC clients,
including non-Z applications, and optional client to an upstream provider such
as Okta. It also handles that upstream client work on behalf of Zum-native
services using `libZum`; those services need not become OIDC clients of Okta
or implement a separate OIDC client of `zumd` to use the native service API.
With fallback enabled for
an application and no matching local user, `zumd` redirects to the configured upstream,
validates the returned identity, resolves local authorization, and completes
its downstream login flow. It issues its own tokens under its own issuer: ID
tokens target the requesting client, and resource access tokens target the
application audience. Upstream tokens are not substituted for Zum tokens.

Keep the upstream connection and its application policy centrally configured
through `zum`. This includes issuer/discovery settings, upstream client
registration and credential reference, requested scopes, and the claim that
contains upstream role values. Applications do not configure Okta endpoints,
Okta credentials, or role translation themselves.

When supported by the upstream provider, its configured `roles` scope requests
role information; mapping operates on validated role claim values. Do not assume
a universal `roles` scope or claim. Make scope names, claim name, and validated
claim source configurable for provider-specific conventions. Upstream role
scopes and claims are distinct from the local application OAuth scopes issued
by `zumd`.

Each upstream role value maps to exactly one local application role. Multiple
upstream values may independently map to the same local role, but no mapping
value may expand to several roles. Use a unique key consisting of application
ID, provider ID, and upstream role value, with a reference to one role in that
application. For example:

| Application | Provider role value | Local role |
| --- | --- | --- |
| zumpingd | `Ping-Operators` | `ping` (local role ID) |
| zumpingd | `Platform-Support` | `ping` (same local role ID) |

Resolve all mapped values in the authenticated identity, deduplicate target
role IDs, then apply local role/action state, upstream application eligibility, audience, scope,
client, and authorization-policy restrictions. Unknown values grant no roles.
Reject mapping targets in another application. External role strings are never
interpreted directly as trusted local role IDs.

Local-first routing must distinguish a local account from a stored projection
of an upstream identity. An upstream projection does not count as a local user
on subsequent login. Persist explicit identity provenance; the mere existence
of a User row must not suppress upstream SSO.

An existing local account stays on the local path. Failed authentication, a
disabled account, or missing local application membership/roles must not trigger
upstream fallback. Only absence of a local identity selects upstream SSO. Local
database errors are errors, not evidence that the user does not exist.

Define how `zumd` obtains the user identifier before choosing the path, for
example through its own login page or an untrusted login hint. The identifier
only selects a path; it is not authentication. An eligible existing SSO session
can supply the identity, subject to current provenance and policy checks.

Delegated users and assignments may be persisted as projections or caches with
provenance and freshness; they do not require manual local provisioning. Missing
or stale upstream evidence must not cause fallback to local assignments. Local
role definitions and per-application mapping configuration remain authoritative
for translating validated upstream claims into permitted actions.

Configure how validated upstream evidence establishes assignment to each local
application: an application claim, a configured group/role mapping, or a dedicated
upstream client whose admission policy represents that application. Login to a
shared upstream client alone must not imply access to every local application.
OIDC does not itself provide a universal application-assignment directory; define
the provider-specific evidence and its refresh mechanism explicitly. If that
evidence cannot be obtained, reject access rather than assume assignment.

Bind upstream identities using the configured provider and validated issuer
and subject, not an email-address match. Define projection creation and explicit
account-linking rules, including conflicts and changes of provider. Changing
authority mode must invalidate or revalidate old grants and must not merge
local assignments into upstream-derived authority.

Do not attach an upstream login to an existing local user merely because the
email or login name matches. Define explicit conflict handling when a local
account is created after an upstream identity has been projected, preserving
local-first precedence without silently transferring sessions or authority.

Version mappings and delegation policies. Carry enough provider, identity,
policy, and role-source context in sessions and grants to revalidate access
after membership or mapping changes. Mapping expansion must not enlarge an
existing grant beyond its original ceiling. Specify when fresh upstream claims
are required and how upstream role removal affects local sessions and refresh;
cached login claims must not imply indefinite role validity.

For the initial upstream client, fetch the provider's JWKS for each completed
authorization-code exchange, within the configured response/key-count limits.
Do not retain an unbounded or indefinitely trusted upstream signing-key cache.
Missing keys and failed JWKS retrieval fail the exchange closed. This is
independent of `libZum`'s bounded service-side cache of `zumd` public keys.
Bound cached upstream HTTP origins separately (`zumd --upstream-origins`, default
32). Reuse matching origins; on capacity pressure replace an idle origin only
after native Zhttp shutdown drains it. Retiring origins count toward capacity;
if none can be replaced, fail the request without growing a waiting queue.
Eviction is driven by incoming requests, not a timer or background scanner.
Keep origin/request bookkeeping on a non-I/O owner shard and use Zhttp's existing
asynchronous start/stop callbacks. Finalization requires successful native stop
completion and must not initiate another blocking stop. A borrowed resolver is
started before worker use; owned resolver shutdown and the outer synchronous
drain run on the main thread while the scheduler remains active.
Use native system TLS trust by default. A non-secret node configuration section
`upstream: {caPath: "/path/to/ca.pem"}` selects a CA bundle or native CA directory
for private upstream providers. It is not a provider credential, is not stored
in the identity DB, and does not disable certificate or hostname validation.
Likewise, optional non-secret `caPath` in the `zum` and `zumpingd` client config
selects trust for their connection to `zumd`, defaulting to system trust. This is
independent of upstream OIDC delegation and requires no provider integration in
the downstream service.
Discover upstream OIDC metadata by appending `/.well-known/openid-configuration`
to the issuer after removing its terminating slash, retaining any issuer path
(for example `/oauth2/default/.well-known/openid-configuration`). This follows
[OIDC Discovery §4](https://openid.net/specs/openid-connect-discovery-1_0.html#ProviderConfig),
not the OAuth authorization-server metadata path-insertion rule. Keep exact
issuer matching on the returned metadata and ID token.
Request `auth_time` as an essential upstream ID-token claim and require it in
the verified response; never substitute `iat` for the user's authentication
time. Forward downstream `max_age` and `prompt=login` (with `max_age=0`), enforce
the requested age with the configured clock skew, and retain the verified
`auth_time` through downstream session/token issuance.

Upstream connection configuration, policy, mappings, identities, and necessary
authentication transaction state are owned by `zumd` and persisted through Zdb.
Callback handling obeys Zdb activation and persistence completion; replication
and failover guarantees remain Zdb concerns. Upstream reauthentication and
logout follow the session and freshness contracts below.

### Upstream assignment freshness contract

Each application/provider policy must specify an assignment source, eligibility
predicate, role claim source, and finite `assignmentMaxAge`. Enabling delegation
without these settings is invalid. Record provider, issuer, subject, target app,
validated role values, eligibility evidence, observation time, evidence expiry,
and policy version. Authentication time and assignment observation time are
different: reusing an SSO session does not refresh assignment evidence.

Evidence is usable only before the earlier of its source expiry (where supplied)
and observation time plus `assignmentMaxAge`, and only under a compatible current
policy. New authorization, code redemption, and refresh check that deadline and
re-evaluate current local mappings and role/action state. Mapping changes can
reuse still-valid raw claims; changes to provider, claim source, or eligibility
rules invalidate the affected evidence and require collection under the new policy.

When evidence expires, obtain a new authoritative response from the configured
source. The initial implementation uses a new upstream authorization transaction
and validated claims. A provider-specific refresh/UserInfo mechanism may be
enabled only when its documented semantics supply current assignments; merely
receiving a new access token or replaying old claims does not refresh evidence.
An upstream SSO session may make that authorization transaction silent, but the
response must still meet the configured assignment-source contract.

If fresh evidence requires a browser during refresh, return `invalid_grant` and
require a new authorization flow. For authorization with `prompt=none`, return
the applicable OIDC interaction/login-required error instead of displaying UI.
An upstream outage permits use of unexpired evidence only; it never extends the
deadline or switches a delegated identity to local assignments. No usable
evidence means no new delegated tokens. Local users remain independent of this.

Cap delegated access-token expiry at the evidence deadline as well as the normal
token lifetime. Subject to the provider's actual claim-update latency and bounded
clock skew, removal is enforced no later than that deadline; do not promise
instant upstream deprovisioning. Explicit negative evidence invalidates the
projection and blocks further issuance immediately, but existing self-contained
tokens remain valid until expiry. Push events or online checks can later shorten
this window; they are not assumed in the initial contract.

Expose provenance, observation time, deadline, and last refresh outcome through
authorized queries, without exposing upstream tokens. Persist only protected
upstream credentials/tokens needed by an explicitly configured refresh mechanism.

## Catalog ownership and update contract

An application's catalog consists of its available actions and standard roles
(bundles of those actions). It does not include assignment of roles to users.
Application startup populates definitions; `zum` administers local user
memberships and role assignments, while upstream evidence supplies delegated
assignments. Creating or restoring a standard role never creates a membership
or adds that role to a user's assignments.

Tag definitions with owner and origin: application-standard or administrator-
custom. The enrolled application's catalog publisher has core management
permissions constrained to its own standard definitions and declared scope
bindings. It cannot create users, assign roles, change upstream mappings, or
grant itself wider administrative authority. This is a distinct workload client
from the public OAuth registration for `zumping`. The workload credential stays
at `zumpingd`; `zumping` has a public client ID but no service or upstream secret.

Publish a versioned catalog manifest through a dedicated catalog-publication
REST operation with a matching core permission. Use stable application-qualified
keys for actions and standard roles, a monotonic revision, and a content digest.
An identical revision/digest is an idempotent success; a conflicting digest at
the same revision or a lower revision is rejected. Concurrent publication checks
the stored revision. Incomplete publication is resumable and is not advertised
as ready until its definition updates are committed.

Startup ensures the current manifest's definitions exist. Restoring a deleted
standard definition is intentional catalog initialization, not restoration of
user assignments. Keep stable-ID tombstones and never recycle action positions.
Explicit role deletion removes its assignment references and mapping references
as an authorized lifecycle operation; restoring its definition does not
reconstruct any of those references. Ordinary publishers do not delete roles
or mutate assignments: removed manifest entries are retired, not destructively
deleted. Retirement and administrative suspension survive unchanged startup
publication; reactivation requires an explicit authorized lifecycle operation.

A higher manifest revision may update standard role bundles and add or retire
definitions. Existing assignments to a retained role continue to refer to that
role, so a deliberate bundle change affects future authority resolution. Log
the definition delta and increment the application's authorization version;
the original-grant ceiling still prevents expansion of existing grants.
Administrators create custom roles rather than editing publisher-owned bundles;
publication leaves custom roles and all user assignments untouched. Role state
and deletion control which names can be requested as resource scopes. Normal
startup does not reset lifecycle state or administrator policy.

The publication body is an outer object containing `catalog`, `revision`, and
`digest`. `catalog` is the nested object containing only `actions` and `roles`;
it is serialized by `ZfJSON`, and the digest is SHA-256 over those exact
nested-object bytes before the outer fields are appended by `Zrest::signBody`.
Object fields follow their declared canonical order, array order is preserved,
audience IDs are decimal strings, optional empty labels are omitted, and the
normal `ZfJSON` Unicode escaping rules apply. Encode the digest using unpadded
base64url. The server reserializes the normalized parsed `catalog` with `ZfJSON`
and verifies the digest; it does not trust the submitted digest as proof that two
different manifests are identical.

Core bootstrap follows the same separation: the built-in catalog evolves as
definitions, but the initial superuser membership and role assignment are created
only by resumable first-time bootstrap. Later startups never recreate a removed
administrator assignment. The core bootstrap initializer has the special initial
assignment authority; ordinary application catalog publishers do not.

## SSO sessions and logout

Introduce a secure provider browser session that allows subsequent application
authorization requests to reuse authentication. The existing short-lived
transaction cookie is not an SSO session. Application sessions, provider
sessions, and OAuth grants have separate lifecycles.

Store opaque provider session handles server-side and send them only in Secure,
HttpOnly browser cookies with a SameSite policy compatible with the selected
redirect flow. Rotate the handle on authentication and account/security changes.
Require finite configured idle and absolute lifetimes; activity may move the idle
deadline but never the absolute deadline. `prompt=login` requires reauthentication;
`max_age` checks authentication age independently of assignment freshness.
`prompt=none` never displays interaction. Recheck app eligibility on every new
authorization even when the authentication session is reused.

Refresh families have finite idle/absolute lifetimes and rotate on successful
use. Reuse of a consumed refresh token revokes its family. Clients serialize
refreshes; a lost response may require login rather than replaying a consumed
token. Credential recovery or local user suspension revokes provider sessions
and refresh families for that identity. Membership removal blocks issuance for
that app; it does not log the user out of unrelated applications.

A reusable session records authentication provenance and freshness, not a global
effective role set. Reuse requires authentication that meets the target
application's policy. A change of application or provider may require fresh
authentication. Never use one provider's claims with another provider's mappings
or silently change the session user when an upstream callback identifies a
different principal.

Logout boundaries are explicit:

- Application logout deletes that application's local session/token storage;
  revoke its refresh family when present. It does not implicitly end provider SSO.
- Provider logout invalidates the provider browser session and clears its cookie.
  It does not silently revoke every independent application grant.
- Explicit grant revocation can target one application or all grants for a user.
- Cross-application logout notification requires a separately implemented and
  advertised OIDC logout profile; do not claim it in the initial profile.
- Upstream logout is not implied by Zum logout. A later upstream login may reuse
  the upstream session, subject to requested reauthentication and freshness rules.

Document the limits of logout and revocation for already-issued access tokens
and independently maintained application sessions. Define the effect of user
suspension and credential recovery on provider sessions and outstanding grants.

## Schema compatibility

Backward compatibility and data migration are non-goals. `zumd` initializes a
new empty store or opens a store whose schema version exactly matches the
running implementation. It must reject any other nonempty store without
rewriting it, synthesizing authority, translating identifiers, or advertising
a migration path. Operators provision a new database when adopting an
incompatible schema.

## Ping/pong example

The example demonstrates an application using the shared provider:

```text
zumping  --->  zumpingd  --->  zumd
 client        REST app       authn/authz + OIDC/SSO
               libZum         zrest + Zdb
```

`zumping` authenticates to `zumpingd`, whose authentication and authorization
integration uses `libZum` and `zumd`. `zumpingd` serves the protected ping/pong
resource; `zumd` supplies identity and application authorization. `zumping`
communicates only with `zumpingd` using REST and never links `libZum`. Provider
integration belongs to `zumpingd` through `libZum`; any browser interaction is
separate from the end client's REST traffic.

Use `zum` to enroll the `zumpingd` application and obtain its confidential service
client ID/secret. Configure `zum.issuerURL` and `zum.clientID`, inject
`ZUM_CLIENT_SECRET` from the secrets manager, and configure its client,
audience, and narrowly scoped application-administration authority. On startup,
`zumpingd` uses `libZum` and the `zumd` REST API to populate its own action and
role catalog: exactly one action, `ping`, and one role, also `ping`, granting
that action. The role name automatically supplies the corresponding resource
scope at runtime. This
initialization is idempotent and confined to the enrolled application; it must
not require the core superuser credential or direct database access. Keep it
separate from core empty-database bootstrap.

To run `zumping`, both `zumd` and `zumpingd` must be running. Document the
complete sequence: start `zumd`, enroll the initial administrator, enroll
`zumpingd` using `zum`, and start `zumpingd` to populate its catalog. Then use
`zum` to create a local user named `user`, assign that user to `zumpingd`, and
grant the application's `ping` role. Complete the user's credential enrollment,
then run `zumping` as `user`: authentication must succeed and a `ping` request
must return `pong`. Include a denied request from a user without the example
membership or ping permission. Restart both servers and repeat the successful
request to verify that initialization preserves identities and assignments.

Also demonstrate delegated authentication:

```text
zumping  --->  zumpingd  --->  zumd  <-- OIDC -->  Okta
               libZum         local roles        upstream identity/roles
```

Configure the ping application's upstream delegation and N:1 role mappings
using `zum`. `zumpingd` continues accepting logins through the same `libZum`
integration and the same `zumd` issuer. `zumd` handles Okta authentication and
role translation, returning local application authority that `zumpingd` uses
to authorize ping/pong. Switching between native and delegated authentication
must not require an Okta integration or role-mapping configuration in `zumpingd`.

## Concrete implementation contract

This section resolves earlier descriptive placeholders. Follow it when earlier
text says to define a schema, API, bootstrap detail, or protocol. Do not invent
another login protocol, move upstream OIDC into `libZum`, or broaden the delivery
scope. Existing mechanisms may be reused only after adapting their ownership and
authority checks. These are proposed contracts, not claims about existing code.

### Source responsibilities

| Current source | Implementation destination/responsibility |
| --- | --- |
| `zum/src/Zum.hh`, `fbs/zum_*.fbs`, `ZumDB.*` | Expanded server records, metadata, Zdb tables, and sagas; DB definitions become server-private. |
| `zum/src/zum.cc` | Move existing bootstrap into server-private `ZumBootstrap.*`; replace executable with administrative REST client. |
| `zum/example/zumd.cc` | Extract server wiring into installed `zum/src/zumd.cc`; replace demo with `zum/example/zumpingd.cc`. |
| `ZumServer.*`, `ZumAuthorize.*`, `ZumToken.*`, `ZumPasskey.*`, `ZumOIDC.*` | Server-owned login, provider protocol, authority resolution, signing, and upstream OIDC. |
| `ZumMgmt.hh`, `ZumRequest.*`, `ZumHTTP.hh` | Separate shared wire/operation types from server dispatch and persistence; retain one catalog. |
| `zum/example/zumc.cc` | Reference for `zumping.cc`: external browser, callback, PKCE, token/refresh POSTs, bearer resource request; remove `ZumOAuth.hh` dependency by using framework form parsing. |
| New public `ZumService.hh` and implementation | `libZum` service connection, OAuth facade, token validation, and catalog publication. |

Keep server implementation in a private convenience library linked by `zumd`
and server tests, not installed as the application integration API. `libZum`
must not link Zdb for its service integration. `zum` and `zumping` use `zrest`
and framework protocol utilities, not `libZum` server integration. Share wire
declarations without bringing DB records into end-client dependencies.

### Schema types, keys, and invariants

Reuse existing `Zum::String`, `Bytes`, `IDVec`, `ZtBitmap`, Zfb metadata, and Zdb
table patterns. IDs for apps, users, roles, audiences, and providers are
nonzero `uint64_t`; action positions remain `uint32_t`. Client IDs and issuer
URLs are strings. JSON represents 64-bit IDs/versions as decimal strings, not
floating-point numbers; byte strings use base64url. Timestamps are signed Unix
seconds, durations are seconds. Keep existing credential ID bytes and opaque
grant ID bytes. Allocate entity IDs with the established RNG plus unique insert
retry; action IDs alone use the monotonic per-app counter.

Use one logical issuer per store initially; additional independent issuers require
separate stores. Multiple applications do not imply multiple tenants or issuers.

`PK` below is unique. `U` denotes another unique index; `I` a grouped lookup
index. Every mutable configuration row has `version:uint64_t`, initially 1,
and creation/update timestamps. Keep saga `owner` fields internal/hidden.
References are validated in the same server operation that writes them; Zdb
sagas serialize multi-record changes and publication completion.

| Table | Keys/indexes | Required payload beyond keys and common metadata |
| --- | --- | --- |
| `zum.issuer` | PK `id` (issuer URL) | schema version, core app ID, bootstrap phase, initial user/client IDs, crypto key-check envelope |
| `zum.app` | PK `id`; U `name` | label, state, `nextActionID`, `authVersion`, catalog revision/digest, service client ID |
| `zum.user` | PK `id`; U `(source,name)`; U `handle` | login name, profile/email, `source=Local/External`, state, auth version; remove global role IDs and `oidcSub` |
| `zum.cred` | PK `id`; I `userID` | Existing passkey fields and state; local users only |
| `zum.membership` | PK `(appID,userID)`; I `userID` | local role IDs, state, authority version; source derived from user; external assignments exposed from evidence, not writable here |
| `zum.action` | PK `(appID,id)`; U `(appID,name)` | label, state, origin, catalog revision, tombstone |
| `zum.role` | PK `(appID,id)`; U `(appID,name)` | label, action bitmap, state, origin, catalog revision, tombstone |
| `zum.audience` | PK `id`; U `uri`; I `appID` | owning app ID, name, state |
| `zum.client` | PK `id`; I `appID` | owning app ID, label/type, redirect URIs, grant mask, auth method, secret verifier/version, state, refresh permission, approved identity scopes |
| `zum.client_access` | PK `(clientID,appID)`; I app-grouped `clientID` | target-approved audience IDs, workload role IDs, state, authority version |
| `zum.admin_access` | PK `(actorKind,actorID,targetAppID)` | permitted core operation IDs, assignable target-role IDs, state; actor is user or service client |
| `zum.provider` | PK `id`; U `name` | issuer, client ID, encrypted client secret, requested scopes, role claim/source, state; endpoints from validated discovery |
| `zum.auth_policy` | PK `appID` | optional provider ID, local-first flag fixed true, eligibility rule, assignment age, session/token limits, consent policy |
| `zum.ext_identity` | PK `(providerID,issuer,subject)`; U `userID` | projected user ID; no implicit local account link |
| `zum.role_map` | PK `(appID,providerID,value)`; I app-grouped `(providerID,value)` | one target role ID, state |
| `zum.evidence` | PK `(appID,userID,providerID)` | raw validated role values, eligibility, observation/deadline, source and policy version, encrypted refresh token only if needed |
| `zum.session` | PK handle digest; I `userID`; I expiry | subject/provenance, auth time, idle/absolute deadline, state; no global role snapshot |
| `zum.consent` | PK `(userID,clientID,appID,audienceID)`; I `userID` | approved role IDs, state; audience ID zero for identity-only requests |
| `zum.grant` | Existing opaque PK; I `(userID,appID)`; I `appID`; I expiry | Existing ceremony/code/refresh data plus target app/audience, facade client ID, source/evidence version, requested/granted role IDs, scope string, action snapshot, client/membership/policy versions |
| `zum.sign_key` | Existing PK and issuer lookup | algorithm, public key, protected external signer reference or encrypted private material, activation/retirement metadata |
| `zum.request` | PK `(actorKind,actorID,operation,idempotence)` | request digest, saga ID, status, non-secret result IDs; client-visible idempotency status |

There are 21 Zum application tables. Saga persistence is Zdb infrastructure,
not an additional `zum.saga` table. Extend the existing `ZdbSagaDB` catalog and
Zum-specific payloads in `zum_saga.fbs`; use Zdb's internal `saga`, `saga_step`,
and `saga_type` tables without registering or exposing replacements. Existing
The physical signing-key table is `zum.sign_key`; no legacy table alias exists.
`zum.request` stores client-visible idempotency/status, not a second saga engine.
Namespace its actor key by actor kind as well as ID to avoid user/client collisions.
Do not cache or mirror application authority back into issuer, user, client or
derived-scope state. Authorization may join these tables into transient runtime carriers,
but those carriers are not additional persisted authority or compatibility state.

### Saga results, compensation, and logging

Use the existing asynchronous, continuation-based `ZdbTable` and `ZdbSagas`
APIs directly. Generally, one REST API operation is one saga, with its business
logic embedded in that saga. A saga step is not restricted to a database mutation
or query: it can perform validation, computation, asynchronous work, and select
the next applicable business path. Use the native runtime step-skipping facility
for conditional branches, including optional application records and differing
authentication/enrollment paths. Zdb provides the idempotency guarantee for saga
replay; Zum does not implement or independently ensure that guarantee. Steps are
replayed on recovery. Put computation and asynchronous preparation in the step body, and
reconstruct needed transient values from the saved intent or durable state.
Do not require all business decisions to be made before saga submission. Use
native branching and preserve business before-images needed for compensation;
do not add application replay guards, effect receipts, or duplicate-effect
tracking. Zdb can suppress an already-applied mutation callback during
replay; distinguish that callback from the enclosing step body, which runs again.
Do not split an operation into multiple sagas
or an external callback state machine merely because it branches or includes
non-database work. Keep transport parsing, authentication/admission, and response
formatting at the HTTP boundary; keep operation-specific business decisions and
effects in the operation saga. External effects still need explicit retry and
compensation semantics; skipping a step is not permission to ignore those.

Zdb saga persistence carries an optional absolute deadline as one additional
field of the native `saga` row. The field is a `ZuTime`; its sentinel null value
means that the saga never expires. The saga submission/run API accepts this as
an optional defaulted parameter. A caller may calculate the deadline once from
the initial submission time and a configured interval, but only the resulting
absolute `ZuTime` is passed to and persisted by Zdb. Never persist or reconstruct
a relative TTL, and never extend the deadline during replay.

The deadline is irrelevant to initial submission and forward execution. In
particular, zero or any other non-null time in the past is valid: the saga runs
normally when first submitted and is thereby one-shot, because any later
recovery must abort it rather than replay it forward. Only the `ZuTime` null
sentinel means never expires.

On activation recovery, a saga whose non-null deadline has passed proceeds
directly into the native abort/rollback/compensation path. Zdb reconciles the
last durable step intent sufficiently to determine the already-applied prefix;
it must not resume later forward business steps. Compensation then restores the
state that preceded that applied prefix and removes the native saga intent and
step rows through the existing state machine. This is a general Zdb lifecycle
facility, not a Zum payload field, application replay guard, timer, audit event,
or replacement transaction protocol.

Business mutations and their successful, non-secret idempotency
results belong in the same business saga. Do not commit the business operation
and then repair its request record from an HTTP callback or a separate outcome
saga. A response callback reports the result; it does not complete persistence.

Compensation reverses the saga's effects: after rollback, application state must
be consistent with that saga never having run. Delete rows inserted by the saga
and restore before-images for rows it changed, including request status/result
fields. Do not turn compensation into a new business mutation that preserves a
failed receipt, advances a version, or retains an audit row. Zdb owns its internal
recovery bookkeeping. A failed attempt can be reported to the caller and logged
separately; it does not require a durable failure record in `zum.request`.
`operationQuery` reports retained request state, not a historical failure journal;
a request removed by rollback is absent rather than a retained failed receipt.

Emit authentication, authorization, administration, and operation outcome events
through `ZiLog`; use existing Ztc alert persistence, distribution, and replay
where needed. Include only appropriate non-secret issuer/app identifiers,
authenticated actor, operation, target, outcome, and correlation metadata.
Capture logger inputs by value with framework-owned lifetime (`ZeString` for
scratch text), never pointers/references to request or saga state. Never log
passwords, tokens, bootstrap/recovery capabilities, keys, or raw secret-bearing
requests/responses. Operational events are outside the business saga and are
not reversed by compensation. Logging must not introduce an extra transactional
commit condition or an application-level exactly-once delivery mechanism.

Remove the bespoke `zum.audit` model/table, issuer audit counter, audit persistence
and cleanup machinery, and `auditQuery`/`auditCleanup` permissions, handlers, and
CLI commands. Do not replace them with a Zum outbox, receipt hook, journal, or
logging service. Update schema metadata, bootstrap catalogs, and tests
consistently; do not reuse retired numeric operation IDs. Tests verify
ZiLog events and secret redaction, not persisted Zum audit rows.

Do not add opt-in saga finalization/receipt hooks, persisted rollback-direction
or terminal-outcome extensions, or other Zdb machinery merely to support Zum
logging or request results. Remove the corresponding speculative changes while
preserving unrelated framework work. Failover and recovery guarantees remain
Zdb responsibilities, not a new Zum transaction protocol.

The REST `Idempotency-Key` contract distinguishes client requests and exposes
their non-secret results; it is not the mechanism that makes saga replay
idempotent. Keep that protocol concern separate from Zdb's guarantees. Remove
application safeguards whose only purpose is to duplicate native replay handling;
retain actual business constraints such as name uniqueness and API preconditions.

Use a synthetic opaque name for External user projections, with upstream display
name/email in profile fields, so unrelated providers' usernames do not collide.
The normalized local login name is trimmed UTF-8 with ASCII case folding only;
preserve a separate display name. Apply the same normalization on creation and
lookup. Do not rewrite email provider-specific punctuation. External `(iss,sub)`
remains exact and case-sensitive. Initial linking is deliberately unsupported:
conflicting local names never transfer identity; identity linking is outside the
first release. Reject local credential creation on External users.

Use existing states with explicit allowed transitions: pending local user becomes
active only after credential enrollment; active/suspended may toggle; disabled
configuration requires explicit reactivation; revoked/consumed grants are terminal.
Tombstones reserve catalog IDs/names and are never used for authority. App/user/
membership/client disabling invalidates subsequent issuance as described earlier.
Do not hard-delete users or apps in the initial API. Role deletion is explicit,
removes all live assignment/mapping/scope references in a saga, and retains its
tombstone; publication may restore the definition, never the deleted references.

Core superuser bypasses target administrative-delegation checks, not business
API checks. Other administrators need both core operation authority and an active
`admin_access` row for the target. Role assignments/maps must fit its role ceiling.
Operations on global identities/providers/keys require core superuser initially;
do not infer global power from an app-scoped delegation. Service catalog enrollment
creates only its own target delegation. `admin_access` is not user app membership.

### Management wire contract

Use resource-oriented routes: GET with query parameters for queries, POST for
creation and named lifecycle operations, PATCH for partial field updates, PUT
for replacement of assignment/configuration sets, and DELETE for removal.
Do not expose POST `/admin/<operation>` aliases. Preserve standard OAuth/OIDC
endpoint methods separately. Operation name, stable numeric ID, method/path,
permission, typed input/output, and dispatch entry are one shared catalog.
Seed `Zum.<operation>` only in the core app; the registry test must prove 1:1
coverage. Retire `userRoles` in favor of `membershipRoles`; never reinterpret
existing numeric permission IDs in an old store.

All requests authenticate with Bearer management access tokens. GET has no body;
named query parameters carry key fields, indexed filters, cursor, and limit.
Exact-key filtering and cursor are mutually exclusive. Credential queries support
`id` (base64url exact key) or `userID` (indexed group), never both; repeat the
`userID` filter with its user-bound cursor when paging that group. Use one collection GET
per Query operation for exact and paginated lookup, e.g. `/admin/users?id=123`
or `/admin/users?name=user`. Do not add item GET routes without separate catalog
operations. Allow indexed filters only. Default page size 100, maximum 1000;
return `{items, nextCursor?}` with cursor bound to operation/filter/last key.
Pagination is keyset-based, not a snapshot promise; absent exact matches return
an empty items array. Mutations identify targets in paths, accept JSON fields,
and return `{item?, operationID?}`. DELETE has no body. Existing-row mutations
require `If-Match` with the row ETag: missing precondition returns 428, mismatch
412. PUT creation requires `If-None-Match: *`. Query projections include per-item
ETags; collection ETags are not row versions. Create/publication/rotation require
a caller-generated `Idempotency-Key` header. A repeated
key with a different request digest returns 409; replay never returns plaintext
secrets a second time. Asynchronous sagas return 202 with operation ID, queried
by `operationQuery`. Successful completion is 200 (201 for creation).

Errors use `{error, message, correlationID}`; status 400 malformed/invalid input,
401 invalid authentication, 403 forbidden, 404 absent target within authorized
scope, 409 state/idempotency conflict, 412 version precondition failure, 428
missing precondition, 429 capacity/rate limit, 503 inactive/not ready.
Never expose secret fields or internal exceptions. Authenticate/authorize before
disclosing existence. OAuth endpoints retain standard OAuth error bodies.

The complete initial management catalog is below. Comma-separated names are
separate operations/permissions, not aliases. `Query` returns the public projection;
`State` accepts `{state}` with the target in its path. `Update` accepts only the listed mutable
fields; it never changes IDs, ownership, assignments, or protected material.

| Operations | Mutation fields/semantics |
| --- | --- |
| `issuerQuery`, `operationQuery` | Read-only issuer metadata or actor-authorized request status |
| `appQuery`, `appEnroll`, `appUpdate`, `appState` | Enroll: name, label, integration (`nativeService` or `oidc`), client type, redirect URIs, optional audience URI; creates app and initial client. Native service receives approved core role-named scopes; business-role approvals follow catalog creation. Update: label |
| `userQuery`, `userInvite`, `userUpdate`, `userState`, `userRecover` | Invite: local name/profile, optional protected enrollment delivery; update profile only; recover issues bound one-time capability |
| `credentialQuery`, `credentialUpdate`, `credentialState` | User/credential key; update label only; credential enrollment uses ceremony endpoints, not raw admin key insertion |
| `membershipQuery`, `membershipAdd`, `membershipRoles`, `membershipState` | Add local user/app link; Roles replaces role-ID set with delegation check; no External writes |
| `actionQuery`, `actionAdd`, `actionState` | Add app/name/label as custom definition, allocate action position |
| `roleQuery`, `roleAdd`, `roleUpdate`, `roleActions`, `roleState`, `roleDelete` | Add custom app/name/label; Update label; Actions replaces app-local action set; Delete reference-cleanup saga |
| `audienceQuery`, `audienceAdd`, `audienceUpdate`, `audienceState` | Add app/name/URI; Update name only, URI immutable |
| `clientQuery`, `clientAdd`, `clientUpdate`, `clientState`, `clientSecretRotate` | Add app/type/label/redirects/grants; Update label, redirects, grants, identity scopes with authority checks; Rotate explicit old-key retirement/overlap |
| `clientAccessQuery`, `clientAccessSet`, `clientAccessState` | Target-approved audience and workload-role sets |
| `adminAccessQuery`, `adminAccessSet`, `adminAccessState` | Actor/target and operation/role ceilings; core-superuser only initially |
| `providerQuery`, `providerAdd`, `providerUpdate`, `providerState` | Provider connection fields, write-only secret; Update endpoint-affecting fields invalidates evidence |
| `authPolicyQuery`, `authPolicySet` | App/provider/eligibility/limits/consent; complete replacement at expected version |
| `roleMapQuery`, `roleMapSet`, `roleMapDelete` | App/provider/value and target role ID; delegation ceiling enforced |
| `identityQuery`, `evidenceQuery` | Read-only external bindings and assignment freshness/provenance |
| `sessionQuery`, `sessionRevoke`, `consentQuery`, `consentRevoke` | User/client/app selectors; explicit bounded revocation |
| `grantQuery`, `grantRevoke`, `grantCleanup` | Revoke exact grant/family or indexed user/app set; Cleanup bounded expired records only |
| `signKeyQuery`, `signKeyAdd`, `signKeyRetire` | Algorithm/public key and protected signer/private-material input; no private data in output |
| `catalogPublish` | App inferred from authenticated publisher; revision/digest plus named actions and standard roles with action names. Each role name is its derived resource scope |

For the initial ES256 `signKeyAdd` implementation, `publicJwk` is a JSON-encoded
string containing the public P-256 JWK, with `kid` matching `id`.
`privateMaterial` is base64url-encoded raw big-endian P-256 private scalar
(32 bytes), not PEM or a DER envelope. Validate that the public and private keys
match before encrypting the private material with `ZUM_DB_KEY` for storage.
Never return the private input or its stored ciphertext in administrative
responses. A protected external signer reference is an alternative only when
the configured signer implementation supports it; it is not a plaintext-key
fallback. The initial implementation has no external signer implementation and
must reject `providerRef` rather than enrolling an unusable active key.

The following mapping is normative. Every path has prefix `/admin`.
Semicolon-separated entries are individual operations, each with its own catalog
permission. Braces denote path parameters. Query filters use public schema keys.

| Area | Operation → method and path |
| --- | --- |
| System | `issuerQuery` → GET `/issuer`; `operationQuery` → GET `/operations` |
| Apps | `appQuery` → GET `/apps`; `appEnroll` → POST `/apps`; `appUpdate` → PATCH `/apps/{appID}`; `appState` → PUT `/apps/{appID}/state` |
| Users | `userQuery` → GET `/users`; `userInvite` → POST `/users`; `userUpdate` → PATCH `/users/{userID}`; `userState` → PUT `/users/{userID}/state`; `userRecover` → POST `/users/{userID}/recover` |
| Credentials | `credentialQuery` → GET `/credentials`; `credentialUpdate` → PATCH `/credentials/{credentialID}`; `credentialState` → PUT `/credentials/{credentialID}/state` |
| Memberships | `membershipQuery` → GET `/apps/{appID}/memberships`; `membershipAdd` → POST `/apps/{appID}/memberships`; `membershipRoles` → PUT `/apps/{appID}/memberships/{userID}/roles`; `membershipState` → PUT `/apps/{appID}/memberships/{userID}/state` |
| Actions | `actionQuery` → GET `/apps/{appID}/actions`; `actionAdd` → POST `/apps/{appID}/actions`; `actionState` → PUT `/apps/{appID}/actions/{actionID}/state` |
| Roles | `roleQuery` → GET `/apps/{appID}/roles`; `roleAdd` → POST `/apps/{appID}/roles`; `roleUpdate` → PATCH `/apps/{appID}/roles/{roleID}`; `roleActions` → PUT `/apps/{appID}/roles/{roleID}/actions`; `roleState` → PUT `/apps/{appID}/roles/{roleID}/state`; `roleDelete` → DELETE `/apps/{appID}/roles/{roleID}` |
| Audiences | `audienceQuery` → GET `/audiences`; `audienceAdd` → POST `/audiences`; `audienceUpdate` → PATCH `/audiences/{audienceID}`; `audienceState` → PUT `/audiences/{audienceID}/state` |
| Clients | `clientQuery` → GET `/clients`; `clientAdd` → POST `/clients`; `clientUpdate` → PATCH `/clients/{clientID}`; `clientState` → PUT `/clients/{clientID}/state`; `clientSecretRotate` → POST `/clients/{clientID}/rotate-secret` |
| Client access | `clientAccessQuery` → GET `/apps/{appID}/client-access`; `clientAccessSet` → PUT `/apps/{appID}/client-access/{clientID}`; `clientAccessState` → PUT `/apps/{appID}/client-access/{clientID}/state` |
| Admin access | `adminAccessQuery` → GET `/apps/{appID}/admin-access`; `adminAccessSet` → PUT `/apps/{appID}/admin-access/{actorKind}/{actorID}`; `adminAccessState` → PUT `/apps/{appID}/admin-access/{actorKind}/{actorID}/state` |
| Providers | `providerQuery` → GET `/providers`; `providerAdd` → POST `/providers`; `providerUpdate` → PATCH `/providers/{providerID}`; `providerState` → PUT `/providers/{providerID}/state` |
| Auth policy | `authPolicyQuery` → GET `/auth-policies`; `authPolicySet` → PUT `/apps/{appID}/auth-policy` |
| Role maps | `roleMapQuery` → GET `/apps/{appID}/role-mappings`; `roleMapSet` → PUT `/apps/{appID}/role-mappings/{providerID}/{valueKey}`; `roleMapDelete` → DELETE `/apps/{appID}/role-mappings/{providerID}/{valueKey}` |
| External identities | `identityQuery` → GET `/identities`; `evidenceQuery` → GET `/evidence` |
| Sessions | `sessionQuery` → GET `/sessions`; `sessionRevoke` → POST `/sessions/revoke` |
| Consent | `consentQuery` → GET `/consents`; `consentRevoke` → POST `/consents/revoke` |
| Grants | `grantQuery` → GET `/grants`; `grantRevoke` → POST `/grants/revoke`; `grantCleanup` → POST `/grants/cleanup` |
| Signing keys | `signKeyQuery` → GET `/signing-keys`; `signKeyAdd` → POST `/signing-keys`; `signKeyRetire` → POST `/signing-keys/{keyID}/retire` |
| Catalog | `catalogPublish` → PUT `/apps/{appID}/catalog` |

Percent-encode path parameters. For role-mapping `valueKey`, use base64url of the
upstream value's UTF-8 bytes to avoid embedded path separators; this is not secret
encoding. Credential IDs already use base64url. State/role/action subresource
writes use the containing row's ETag. Catalog uses its revision ETag, initially
`"catalog-0"`, and existing revision/digest idempotency rules. After authentication
and authorization, resolve a matching idempotency replay or identical catalog
revision/digest before checking a now-stale If-Match: retries are no-ops, not
spurious conflicts. A genuinely different write still requires current If-Match.
The catalog path app ID must match the authenticated publisher's resolved app.

Bulk revoke/cleanup POST bodies contain explicit exact/indexed selectors and
bounded limits. They use idempotency keys, not a fictional aggregate row ETag.
Per-row recovery/rotation/retirement POSTs require `If-Match`. Creation returns
Location and ETag; Location may be a collection URI with exact-key parameters.
GET never performs cleanup, rotates secrets, or starts mutation sagas. Return
405 and Allow for unsupported methods. No route is exposed for Zdb saga internals.

In addition to conventional OAuth endpoints, core bootstrap/user enrollment
uses the existing passkey ceremony endpoints moved from the demo server. Preserve
their validated credential parsing; bind capabilities to an already-created user.
Expose exact ceremony paths from the existing `ZumHTTP.hh` route definitions in
the generated API reference rather than maintaining another hand-written list.
The provider page submits WebAuthn JSON using `fetch`. Successful authorization
completion returns HTTP 200 JSON `{redirectURI}` with any session cookie; the
page navigates to that validated destination. Do not use a fetch-visible 302 or
read `Location` from a manual fetch redirect. A required consent step returns
the provider's HTML form, which the page displays for ordinary form submission.
Enrollment completion retains its JSON success response. Normal navigational
OAuth authorization and consent responses still use HTTP redirects.

Fix the remaining provider route names: GET
`/.well-known/openid-configuration`, GET
`/.well-known/oauth-authorization-server`, GET `/jwks`, GET/POST `/userinfo`,
GET `/authorize`, POST `/token`, POST `/revoke`, GET `/oidc/callback`, and
POST `/passkey/begin` and `/passkey/finish`. UserInfo requires a Zum-issued
access token with explicit `openid` claim-release authority from its grant;
never accept an ID token or infer identity scopes from business permissions.
These protocol routes are not extra administrative CRUD permissions.

GET `/health/live` reports process liveness; GET `/health/ready` returns 200 only
when active, initialized, signing/key access is available, and bootstrap is Ready;
otherwise 503. Responses expose status only, not configuration or secrets.
GET `/login` displays the provider identifier form; POST `/login` selects the
local-first path within an existing cookie-bound authorization transaction.
POST `/consent` records approve/deny for that transaction's fixed client/target/
scope request; it cannot substitute another subject or expand requested scopes.
POST `/logout` ends the current provider session, returning a local confirmation
rather than following an arbitrary return URL. Login, consent, and logout forms
require cookie-bound CSRF protection. These browser operations do not bypass
administrative authentication or grant independent administrative authority.

### Native-service OAuth facade protocol

The concrete facade uses three authenticated Zum-native calls. They are protocol
operations, not user-admin catalog operations; require an enrolled service token
with the matching `facade.authorize`, `facade.token`, or `facade.revoke` scope and
an app binding. Seed these service-protocol actions in the core app independently
of the management catalog; no user role assignment is implied.

1. `zumpingd` receives browser GET `/authorize`. `libZum` posts
   `/service/authorize` to `zumd` with `{clientID, redirectURI, responseType,
   scope, resource?, state?, codeChallenge, codeChallengeMethod, nonce?,
   prompt?, maxAge?}`. `zumd` resolves the service app from its authenticated
   workload token and validates the public client and target against that app.
2. `zumd` persists a bounded authorization transaction containing those exact
   bindings and the facade service client ID. It returns
   `{authorizationURL, expiresIn}` pointing to its own `/authorize` with an opaque
   transaction reference. `zumpingd` sends a 302 to that URL. Do not copy browser
   cookies between hosts or authenticate the user from the service credential.
   The browser session and all local/upstream credential work occur at `zumd`.
3. After login, the code goes directly to the public client's registered loopback
   redirect, with its original state. `zumping` posts form-encoded redemption to
   `zumpingd` `/token`. `libZum` posts `/service/token` with the original OAuth
   form string in `{form}` plus service Bearer authentication. `zumd` checks the
   facade service binding, public client ID, redirect URI, PKCE, and one-time code
   state. Return the standard token JSON/status unchanged to the end client.
4. Refresh follows the same service call and binds the family to both public
   client and facade service. Do not allow a different service to redeem it.
   `/service/revoke` accepts `{form}` for the facade's `/revoke` endpoint. Token
   and revoke responses use `Cache-Control: no-store`; never log request bodies.
5. Direct OIDC clients use `/authorize`, `/token`, and `/revoke` on `zumd` without
   facade binding. A facade-bound code/family cannot bypass that binding by using
   the direct token endpoint. The issuer remains `zumd` in both cases.

The transaction reference is not user authentication and is single-entry,
short-lived browser state; consume it into a cookie-bound login transaction.
Repeated service initiation can produce a new login transaction; it cannot
reuse a completed code. Bound active transactions by configured service quotas.
Standard protocol fields pass through typed parsing, never arbitrary proxy URLs.

Service tokens additionally carry `zum_app_id` for the authenticating client's
owning app, distinct from the core audience and permission context. `libZum`
uses that verified claim for service context. The server derives ownership from
the client record, never a caller-provided app ID or the token audience alone.

Use a distinct core workload role `appService` granting these facade operations,
`catalogPublish`, and `operationQuery` for its own requests only, with target-app
delegation created during enrollment. Operation-status access never requires
global superuser authority when the authenticated actor owns the request.
Its management token has the core management audience. The service may request
only the narrow scopes approved for its client; it cannot use a client's secret
to synthesize user authority. External confidential OIDC registrations receive
no `appService` role unless explicitly enrolled as native services.

### Service API and async lifetime

Public `Zum::Service` exposes `init(config, transport)`, `start(done)`,
`authorize(request, done)`, `token(form, done)`, `revoke(form, done)`,
`verify(accessToken, done)`, `publish(manifest, done)`, `stop(done)`, and `final()`.
Use framework `ZmFn` callbacks and typed value results, not futures or blocking
calls. `verify` returns `{subject, appID, audience, actions, expires}` or a typed
authentication error; handlers check actions themselves. Requests/results use
shared protocol types, never Zdb row references. Names/types may be factored to
match local conventions without changing this behavioral surface.

Config contains issuer URL, client ID, injected secret bytes, request timeout,
and tunable transport/cache limits. `start` authenticates the service, verifies
issuer metadata, resolves app context, and prepares JWKS; it does not publish a
caller-specific catalog automatically. `zumpingd` calls `publish` before readiness.
Cache the short-lived workload token and coalesce simultaneous renewals. Unknown
JWT key IDs trigger at most one coalesced JWKS refresh per configured interval;
never accept a key from an untrusted token URL. Token validation never expands
actions from current role names.

Each accepted operation completes exactly once, including cancellation/error.
Copy or move variable input into owner-shard storage before returning; input
spans cannot outlive their call without owned backing. Invoke completions on the
configured owning shard. `stop` closes admission, cancels/drains requests and
timers, then calls done; `final` follows completed stop. The app owns Service and
transport for that lifetime; do not prolong owner lifetime through child cycles.

### Bootstrap, defaults, and readiness

Initial local authentication uses the existing WebAuthn/passkey implementation,
not a new password database. Server configuration requires issuer URL, listener,
existing Zdb/store settings, RP ID/name, initial administrator login, and protected
enrollment-output path. `ZUM_DB_KEY` remains required. On an empty store generate
the initial asymmetric signer using existing crypto facilities and encrypt its
private material, or use an explicitly configured external signer.

Bootstrap phases are `Empty -> Core -> AdminPending -> Ready`; persist phase and
stable object IDs under the issuer. `Core` means all core definitions and initial
client/signing records are committed. `AdminPending` means the pending local user,
superuser membership, and capability digest exist. Only readiness/health, discovery,
and bound enrollment are admitted until the administrator activates. Readiness
returns 503 before Ready; liveness can return 200 while startup/enrollment waits.
On restart verify/resume the recorded phase; nonempty unknown-schema stores fail
with an unsupported-schema error rather than initializing again.

Deliver the one-time capability URL to the configured owner-only file (0600 on
this Linux run); never to logs or process arguments. This initial browser-user
capability delivery is separate from program secret injection. Store only its
digest. If delivery is lost/expired while AdminPending, an explicit startup
`bootstrap.reissue` flag may revoke/reissue it to that protected path. Reject the
flag once the administrator is active. Completion consumes the capability and
activates the existing user; it never creates a second administrator. Later
restart cannot recreate removed administrator membership.

Use explicit configurable defaults: authorization transaction 300 seconds, code
60, resource token 300, refresh idle 86400 and absolute 2592000, provider session
idle 1800 and absolute 43200, enrollment capability 900, request timeout 15, clock
skew 30. These are initial operational policy choices, not protocol constants.
Provider assignment maximum age must be explicitly configured; fixture uses 300.
Tests inject clock advancement instead of sleeping. Normal GC remains deterministic
and indexed/bounded; expiry checks are synchronous authority checks.

Define `ping` scope to reference the standard `ping` role. Its URI audience is
the configured stable HTTPS resource identifier for `zumpingd`; do not derive it
from a transient listener port. Core audience is issuer URL plus `/admin`.
Public client IDs are independently enrolled stable strings; configure `zum` and
`zumping` with their issued IDs, never hard-code the old example's `client_id=zum`.

### Execution and acceptance ledger

The first upstream implementation supports eligibility modes `claimValues` and
`mappedRole`. The former requires a configured claim name and accepted string
values; exact matching of a string or string-array claim establishes eligibility.
The latter requires at least one active app-specific mapping of a validated role
claim. Reject wrong claim types; unknown values grant nothing. Select ID token
or UserInfo explicitly as source; UserInfo must match the validated subject.
No missing-claim fallback, expression language, script hooks, directory sync, or
dedicated-client admission inference is required initially.

On detecting that a local login now matches a previously projected external
login, preserve local-first routing and revoke affected external sessions/refresh
families; require fresh login, never transfer identity or assignments. Existing
access tokens keep their expiry boundary. Account merging is not implemented.

At the start of implementation, inspect existing configured targets and record
the working baseline without running a build. Keep a compact ledger in
`zum/IMPLEMENTATION.md`: milestone status, touched files, contract decisions,
exact build/test commands and results, and any unverified requirement. Do not
mark a milestone complete from source inspection alone when its gate requires
a running program. Work in the ten-step order, with implementation and test code
batched before infrequent incremental builds.

| Gate | Required observable result |
| --- | --- |
| 1: schema | Metadata/FlatBuffers roundtrip for every new row; key uniqueness, cross-app reference rejection, ID allocation, and authority resolution unit tests pass. |
| 2: skeleton | Core executable links zrest/Zdb, starts/stops using current config, and admits DB-dependent work only on activation. |
| 3–4: bootstrap | Fresh disposable PostgreSQL/Zdb fixture reaches AdminPending. Repeated restart preserves IDs and pending assignments; interrupted phase resumes; wrong DB key fails readiness. Browser enrollment/Ready is verified at gates 5–6, not required before the HTTP auth implementation. |
| 5: REST | Every catalog operation has dispatch and permission coverage; real HTTP tests verify success, invalid input, forbidden target, version conflicts, and secret redaction. Service facade preserves code/PKCE/client bindings. |
| 6: admin CLI | Administrator logs in using browser/PKCE and performs enrollment, local user invitation, membership, and role assignment through REST only. |
| 7–8: example | `zumpingd` receives configured issuer/client ID plus injected secret, authenticates, and publishes exactly one action and standard role named ping; repeated publication is unchanged. |
| 9–10: user ping | Enrolled local user `user` has only app membership and ping role; OAuth login through zumpingd succeeds and GET `/ping` returns JSON `{"reply":"pong"}`. Removing assignment yields 403 after new issuance; no token yields 401. |
| Provider completion | An independent OIDC client works locally; repeat with upstream federation, local admin during outage, N:1 maps, stale-evidence denial, and unchanged native client. |

Provide `zum --config FILE login` plus `zum --config FILE OPERATION --json FILE`
as the complete initial administrative command surface. The latter resolves the
catalog's HTTP method/path: extract path parameters, encode GET fields as query
parameters without a body, and encode mutation fields as JSON with version and
idempotency controls in headers. DELETE has no body. Print redacted JSON; do not
send every CLI operation as POST. There is no need for a large friendly subcommand
grammar before all operations work. Creation/rotation secrets go only to explicit
protected output files for the provisioning workflow. Persist admin refresh tokens
only in an explicitly configured owner-only credential file, not the non-secret
config or logs; otherwise keep tokens in memory and require login per invocation.

Use `zumpingd --config FILE` and `zumping --config FILE [--no-browser]`. End-client
config contains service origin, public client ID, scope `ping`, and loopback
callback configuration. The CLI browser flow follows `zumc.cc`; fix form escaping,
token response optional fields, callback validation, and finite login deadlines.
An integration driver may use virtual WebAuthn fixtures to automate enrollment;
also document one manual real-browser/passkey run. Never add a production auth
bypass just to make the test noninteractive. Integration fixtures create isolated
databases and key material; never run destructive fixtures against a user store.

If external OIDC credentials, a real browser, or an unavailable dependency is
needed, finish safe in-scope code/tests and record the precise unverified gate.
Do not fabricate successful interoperability or replace required authentication
with a test shortcut. Outstanding gates remain outstanding, not implicitly waived.

## Implementation requirements and execution scope

Implementation must comply with [GUIDELINES.md](GUIDELINES.md), which extends
`AGENTS.md` and is authoritative for engineering style, framework usage,
performance, ownership, and review. Read applicable module-local guidelines
before changing those modules. This plan defines behavior and architecture, not
an exception to framework conventions. Descriptive model labels are not proposed
C++ identifiers; use concise project naming and the guideline's 28-byte limit.

In particular:

- Do not overengineer. Skeptically pressure-test every proposed feature, helper,
  abstraction, schema field, and framework change against an actual requirement
  and existing Z capabilities. Prefer removing duplication over adding a new
  mechanism. The contracts above do not authorize bespoke transaction, audit,
  concurrency, or recovery infrastructure.
- Use GNU C++2b, CRTP/compile-time dispatch and SFINAE, explicit lambda captures,
  framework enums, direct dependencies, and existing header/indentation style.
  Do not introduce concepts, compatibility shims, or parallel legacy APIs;
  propagate breaking changes through affected in-scope code. Downstream
  dependents beyond `zum` are WIP and excluded from this task.
- Model schemas and conversions using `ZuStruct`, `ZfStruct`, and `ZfbStruct`;
  use framework JSON, URI, CLI, and formatting facilities rather than ad hoc
  serializers or direct generated FlatBuffers builders/accessors. Use `zrest`
  for REST and Zdb operations/sagas for persistence and multi-record integrity.
- Minimize allocation and copying, use identified/tunable Z heaps and intrusive
  containers, and preserve pooled `ZiIOBuf` ownership. Use mutable spans and
  in-place processing where appropriate, including secret-field crypto.
- Keep Rx/Tx and database state on their owning shards. Use asynchronous
  continuations, thin cross-shard dispatchers, bounded work per scheduler turn,
  and orderly timer/I/O teardown; do not repair ownership violations with locks
  or block service threads waiting for results.
- Rely on Zdb's sharding and sagas for concurrent-update safety and transactional
  integrity. Do not introduce a parallel locking or transaction layer in `zumd`.
  Application preconditions enforce business rules and API versions within
  native Zdb continuations.
- Do not wrap production persistence in synchronous `insertRecord`-style APIs.
  Blocking orchestration is acceptable at test-driver boundaries only; service
  code remains continuation-based. Use `ZdbTableDerive` and the corresponding
  framework derive macros; keep hidden template instantiations in `.cc` files
  rather than hand-rolling an alternative instantiation scheme.
- Parse numeric IDs with the framework (`ZuBox::scan`), checking full input
  consumption and the null sentinel. Do not hand-roll integer parsing, add wider
  accumulators, or impose redundant digit/overflow checks. Preserve valid zero
  values where the schema permits them (notably action IDs).
- Reclaim completed transactions and other short-lived state deterministically.
  Bound long-lived containers and caches; do not introduce periodic full-container
  GC scans. Enforce protocol expiry through lifecycle/deadline handling and
  authority checks, not by treating background cleanup as a security boundary.
- Preserve necessary authentication, authorization, cryptographic, and ownership
  checks. Avoid redundant validation and gratuitous conformance policing; unknown
  optional protocol fields alone are not a reason to reject interoperable clients.

The following task-specific build and verification limits override the broader
build-matrix and acceptance prescriptions in `GUIDELINES.md` for this work:

- Continue using the existing clang debug configuration. Release, ASan/LSan,
  GCC, and MinGW builds or verification are out of scope.
- Stop the build at `zum`. Its required upstream dependencies and its own
  library, programs, examples, and tests are in scope; downstream dependent
  modules are WIP and must not be built, tested, or repaired for this task.
  Do not use an unrestricted top-level build that traverses beyond `zum`.
- Do not run `make clean`, equivalent clean targets, or delete build artifacts
  to force a fresh build. Do not explicitly invoke `z.config`, `configure`,
  `config.status`, or a regeneration workflow that reconfigures the build.
  Ordinary `make` may follow the configured Automake dependency chain after a
  `Makefile.am` edit. Do not switch compilers or build modes. If required
  build-system changes cannot be exercised within this constraint, report that
  specific limitation instead of bypassing the established build configuration.
- Rebuild incrementally and infrequently, batching coherent changes before a
  build. The current priority is code completion of the full REST server,
  `zum`, `zumpingd`, and `zumping`, then build and test the coherent batch. Use
  source inspection between builds, not repeated rebuild/test cycles after
  individual edits. Preserve all acceptance gates; record them as pending until
  actually exercised, without stopping implementation at each pending gate.
  For `zum` and every later module, recompile with at most
  `make -j3` because greater parallelism can exhaust memory. Rebuild changed
  lower-layer `src` targets before dependent directories, since cross-directory
  dependencies are not automatically refreshed. Use explicit directory targets
  in dependency order to bring required upstream dependencies and `zum` up to
  date without building downstream modules. Do not use an unrestricted top-level
  build.
- Build a test directory's default `all` target before its `make test`. Run
  focused tests after the code-completion batch; keep the ten-step sequence's
  validation gates as acceptance requirements, not mandatory interruptions to
  implementation. Do not substitute stale binaries for verification to reduce
  build count.
- Use Valgrind only when needed to investigate a concrete memory/lifetime issue,
  not as a mandatory pass for every milestone. Use `libtool exec` for source-tree
  debugger/Valgrind runs; never execute `.libs` binaries directly.
- Work autonomously through in-scope implementation and verification, making
  reasonable local decisions without routine approval requests. Preserve unrelated
  work and do not expand the build/platform scope. Report genuine blockers or
  decisions requiring new authority, along with completed work and tests run.

Repository-owned isolated unit tests belong in `zum/test`; persistence,
multi-process, network, bootstrap, and complete login scenarios belong in
`zum/itest`. Use `ZuTest`/`ZuTestUtil` and TAP through the appropriate `make test`
target. External OIDC/provider conformance harnesses belong in `zum/interop`;
`zum/example` contains the demonstration programs, not the integration-test
driver. Keep concurrent tests deterministic using completion signaling rather
than polling or arbitrary sleeps.
Restart/persistence acceptance tests must use PostgreSQL, not `ZdbMemStore`
or a preserved in-memory substitute. Close admission, await request completion,
and await the Zdb/store write-queue and callback drain before closing and
reopening the database. A staged saga-recovery image is not a process-kill test;
identify the boundary and the actual backend in recorded results.

`Makefile.am` is the build-system source of truth. Add new targets and sources
there and use ordinary `make -j3` commands; allow Automake's configured dependency
chain to regenerate `Makefile` from a newer `Makefile.am`. Do not use supplemental
makefiles, hand-written target recipes, `-f`/`-o` workarounds, or suppress normal
Makefile regeneration. This does not authorize reconfiguration, `make clean`, or
a build-mode/toolchain change.

## Implementation sequence

1. Implement the expanded data model schema, including application ownership,
   memberships, identity provenance, keys, references, and versioning. Include
   the records needed for local and delegated authentication and per-app role
   mappings, even though the first runnable acceptance flow uses local users.
2. Make a skeleton `zumd` in `zum/src` with the correct `zrest` and Zdb
   dependencies, configuration, replication support, activation-controlled
   database ownership, and graceful shutdown. Separate server internals from
   the service-facing `libZum` integration library.
3. Move the existing `zum` bootstrap code into `zumd`, adapting it to the
   expanded schema and core management-operation catalog. Seed the core app,
   actions, roles, initial administrator, and associated records described above.
4. Validate that `zumd` starts and bootstraps an empty database correctly.
   Initialization must be idempotent on every run: resume interrupted bootstrap,
   preserve stable identifiers, and never duplicate records, reset credentials,
   or overwrite administrative changes on restart. Verify activation-controlled
   initialization and inspect persisted results before claiming this gate passed.
   The code-completion batching rule permits implementing later steps while this
   verification remains pending. This is the
   server/bootstrap milestone, not yet the full authenticated-service milestone.
5. Implement the full `zumd` REST API server: every identified management
   operation with its matching permission, application-aware authority checks,
   and the authentication, enrollment, token/session, and authorization paths
   needed by administrative and application clients. Keep the planned OIDC
   provider/proxy configuration and mapping operations in the API contract.
6. Make the new `zum` an authenticated REST API client of `zumd`, with no direct
   database access or bootstrap ownership. Validate initial-admin enrollment,
   login, application enrollment, user creation, memberships, and role assignment
   through this client.
7. Make `zumpingd` and `zumping`, replacing the old examples, and provide the
   `libZum` service integration needed by `zumpingd`; `zumping` remains a pure
   REST client with no library dependency. Use `zum` to enroll `zumpingd` and
   obtain its service client ID/secret, configure the issuer URL and client ID,
   inject `ZUM_CLIENT_SECRET` through the environment, and configure its
   application/client access and limited catalog-management authority before
   starting the example server. Deliver the secret only to `zumpingd`.
8. Run `zumpingd`; have it populate its one action (`ping`) and one role (`ping`,
   granting the action) through `zumd`'s REST API. Make this application
   initialization idempotent like core bootstrap. Verify retries and restarts
   preserve identifiers and assignments and cannot modify another app's catalog.
9. Use `zum` to create local user `user`, assign `zumpingd` to that user, and
   grant the app's `ping` role. Complete credential enrollment so the user can
   authenticate. Do not grant core administrative authority to this user.
10. With both servers running, run `zumping` as `user`. Verify successful
    authentication to `zumpingd` through `zumd` and successful `ping`/`pong`.
    Repeat after server restarts and verify denial without membership or the
    `ping` role. This is the first complete standalone IAM acceptance milestone.

Keep these milestones in this order. Establish the management-operation catalog
before seeding its permissions, and supply signing and local enrollment before
declaring the authenticated service ready. The skeleton/bootstrap milestone
does not claim OIDC readiness or completion of the administrative workflow.

The local ping milestone does not replace the remaining provider requirements.
Complete and verify the downstream OIDC profile, multi-application SSO, upstream
local-first delegation and N:1 mapping, and session/logout/revocation behavior
where not already delivered in the REST/authentication stage. Reject existing
stores whose schema does not exactly match, and complete the
interoperability, replication, and delegated-authentication tests below.

## Verification

Run the following checks within the existing clang debug build and the execution
constraints above; this list does not authorize reconfiguration, clean builds,
other toolchains/build modes, or unconditional Valgrind runs.

Exercise standalone and replicated `zumd` deployments as Zdb integration tests.
Verify that only Zdb activation enables database-dependent request handling,
deactivation closes admission, and handlers obey persistence completion before
returning success. Test single-use enrollment/code/refresh transitions and
graceful shutdown. Zdb owns replication and failover guarantees; do not introduce
a separate Zum election, durability, or failover protocol.

Test every management operation through its actual REST call, including
authorization failures and inactive-node behavior. Verify catalog-to-dispatch
coverage and the corresponding seeded permission for each operation.

Test two applications with identical role names and action positions to prove
isolation. Cover rejected cross-application references, explicit client access
to another application's audience, and independent memberships for one user.

Exercise membership removal, application suspension, global user suspension,
role changes, refresh authority ceilings, and delegated administration without
privilege escalation. Verify that Zum administrative authority alone does not
produce business-API permissions.

Test identity-only login separately from resource authorization, workload access
without user membership, ambiguous scopes, and management permissions versus
target-application delegation. Check the documented expiry boundary for issued
resource access tokens.

Test automatic empty-store bootstrap through first-admin enrollment and login,
then application registration, user assignment, and application-role population
through the `zum` REST client. Include retries and failures during bootstrap,
verify unsupported schemas are rejected unchanged, and verify that restart does
not reinitialize populated stores.

Run the full `zumd` + `zumpingd` + `zumping` example. Verify successful ping/pong,
rejection without membership or permission, and defined failure behavior when
either server is unavailable. Application integration must use `libZum` and
must not access the identity database directly.

Verify SSO across two independent clients, OIDC discovery, ID-token and UserInfo
validation, nonce checks, authentication freshness, session expiry, recovery,
and each supported logout path. Use independent OIDC clients and conformance
tests for the selected provider profile.

Enroll a non-Z OIDC application through `zum` and exercise it alongside
`zumpingd`. First run both with no upstream configuration or upstream network
dependency, using local users and assignments. Then enable delegation and
verify that the same downstream integrations work unchanged. Confirm upstream
OIDC discovery, callback handling, exchanges, and claim mapping occur only in
`zumd`, and that local administrator login remains usable during upstream outage.

Run the ping/pong example with `zumd` delegating to an independent upstream
OIDC provider and include an Okta integration scenario. Verify N:1 mapping,
deduplication, unknown-role denial, application/provider isolation, disabled
roles, mapping changes, membership removal, and refresh authority ceilings.
Test identity-linking conflicts, upstream outages, reauthentication, logout,
and activation loss during an upstream callback. Verify that `zumpingd` contains no
upstream-provider credentials or role-mapping configuration and accepts only
the expected local Zum authority.

Verify session reuse across compatible application policies and reauthentication
when provenance or freshness is insufficient. Cover local-authoritative and
local-first proxy mode, upstream projection creation without manual local
assignment, upstream application removal, and rejection of local overrides of
delegated assignments. Verify local bootstrap/admin login during upstream outages,
absence-only fallback, no fallback after local authentication or authorization
failure, and repeat delegated login after a projection has been persisted.

Test evidence deadlines independently of browser-session age, including refresh
requiring renewed upstream authorization and access-token expiry capped at the
deadline. Test role-named scope selection versus role assignments, role expansion
during an existing grant, and identical role names at different audiences. Verify provider
interoperability with an independent OIDC client and service-side `libZum` login.
Verify `zumping` has no `libZum` link dependency and sends REST requests only to
`zumpingd`, including code redemption and refresh. Test callback state, PKCE,
redirect/client/audience binding through the facade, token expiry, refresh
rotation, and provider refresh failures. Use today's `zumc.cc` as the client-flow
reference, removing any remaining Zum-specific helper dependency in `zumping`.

Test identical and conflicting catalog revisions, partial publication recovery,
and older publishers restarting after an upgrade. Restore a deleted standard
role and verify that its deleted assignments remain absent. Verify standard-role
updates leave custom roles untouched, and publishers cannot assign roles or
change mappings. Removing a user's `ping` assignment must remain effective after
either server restarts; restoring a missing catalog definition must not recreate it.

Test enrollment credential issuance, public-client no-secret behavior, invalid
service secrets, rotation/retirement, suspension, and lost-response recovery.
Verify retries preserve app/client IDs, ordinary queries never disclose secrets,
and `zumpingd` can obtain only its approved service authority with its issued
credentials. Confirm neither end-client tokens nor catalog publication can
retrieve the service secret or grant additional management authority.
Verify service startup loads `zum.issuerURL` and `zum.clientID` from configuration
and `ZUM_CLIENT_SECRET` from the environment for the Zum connection, resolves app ownership without a
separate app ID, and rejects missing values without logging the secret. Test
rotation by injecting the replacement secret into a restarted service.

Test database-secret encryption before bootstrap and in persisted records:
missing/wrong keys block readiness, tampered or substituted ciphertext is rejected,
and replicas/backups contain no recoverable plaintext secret fields. Test key
rotation interruption/resumption and restoring a backup with its matching key.
Verify normal restart preserves the key binding and never resets secret records.
