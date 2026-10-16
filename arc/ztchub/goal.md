# `ztchub` goal

## Outcome

Implement `ztchub` as the central telemetry service for a fleet of independent
`ztcagent` instances.  It must admit agents enrolled by `zumd`, authenticate
their subsequent connections, aggregate their live telemetry without losing
source identity, and serve authorized telemetry front ends.  `zdash` is a
conceptual example, not the source of the client contract for this goal.

`ztchub` is a Zum application: the protected telemetry service/API.  It
uses `libZum` for authentication and authorization of both device services and
interactive front-end users.  It does not implement its own identities,
passkeys, tokens, roles, credential store, or role assignments.

This goal depends on the application-scoped OAuth model already implemented by
`zumd` and `libZum`. Treat its hierarchical `Zrest` routing, typed issuer
paths, discovery, application/client binding, token semantics, and removal of
the private OAuth façade as prerequisites, not work to reproduce in `ztchub`.
Each device agent or browser, desktop, CLI, or other front-end software client
has a distinct OAuth client registration beneath the `ztchub` application.
Reuse the existing `Zum::Service` authentication,
action/role catalog publication, and Shared Signals Framework (SSF) signed
Security Event Token (SET) reception used by `zumpingd`.

## Zum data model and terminology

[`zum/README.md`](zum/README.md) is authoritative for Zum's data model and
terminology in this goal.

| Term | Meaning in this goal |
|---|---|
| Application | The protected service/resource/API: `ztchub`. Each application represents one service. |
| Audience | The application's immutable `audience` attribute, carried as the resource access token's `aud` claim. |
| Principal | The user or service identity acting through an OAuth client. A device agent acts as a service principal; an interactive front end acts for a user principal. |
| OAuth client | The credentialed software obtaining or presenting tokens, conceptually separate from its principal and the protected application. |
| Caller | The principal observed on a request; prose only, not a domain object. |
| Role | A named set of actions within the application. |
| Action | A named capability, synonymous with permission; there is no separate permission object. |
| Scope | A runtime view of a requested role: its name is the role name and its effective actions are that role's actions. |

The application owns its issuer and audience.  `appEnroll` requires `audience`,
and `appQuery` returns it on the application.  There is no audience table, ID,
or independent lifecycle, and no audience attribute on principals, roles, or
scopes.  A token request does not define a new application boundary.

Principals receive roles within the application.  Client access grants select
application roles; consent is keyed by user, client, and application.  There
is no separate resource-scope catalog or scope assignment.  The role names
`Client` and `Agent` are the corresponding resource scopes.  Identity/provider
scopes such as `openid` and the management protocol scope `zum.catalog` remain
separate from these role-derived resource scopes.

For devices, device ID = Zum service-principal ID = the service-account ID
used by the `ztcagent` on that device.  Device, principal, and agent service
account are 1:1.  Use this same ID for device selection and authenticated source
attribution; there is no separate device identity or device-to-principal mapping.

Use the current Zum schema (version 20); existing stores require reprovisioning,
with no compatibility or migration path for the removed audience model.

## System boundary

| Peer | Authentication | Application protocol |
|---|---|---|
| Unauthenticated `ztcagent` device service | Confidential OAuth client directly through `zumd` | Existing `client_credentials` grant yielding an access token |
| Authenticated `ztcagent` | OAuth 2.0 access token issued by `zumd` | One `Ztc.fbs.Msg` FlatBuffers message per binary WebSocket message over WSS |
| Native telemetry front end | OAuth 2.0/OIDC access token issued by `zumd` | One `Ztc.fbs.Msg` FlatBuffers message per binary WebSocket message over WSS |
| Browser telemetry front end | OIDC authorization-code flow with PKCE through `zumd` | One `Ztc.fbs.Msg` FlatBuffers message per binary WebSocket message over WSS, authenticated without putting a bearer token in a URL |

The existing `Ztc.fbs.Msg` application messages are the starting contract.
Replace the bespoke agent enrollment and mTLS identity protocol with the same
Zum-backed OAuth machinery used for front-end users, treating each device as a
service identity.  Migrate the operational agent transport from the existing
length-framed TLS stream to WebSockets so both agent and front-end connections
use `Zws`.  This keeps one authentication/authorization path and one WebSocket
transport implementation in `ztchub`.  Agent and front-end connections use the
same `Ztc.fbs.Msg` contract, extended with the routing and source metadata needed
for multiple agents and subscriptions.

## Responsibilities

### Device identity and authentication

- Require a non-empty device ID equal to the Zum service-principal ID and the
  `ztcagent` service-account ID.  Exactly one `ztcagent` instance may be active
  for that device/principal at a time.
- Enroll each device in `zumd` as a service identity with its own OAuth client
  registration beneath the `ztchub` application. Use Zum’s existing
  `client_credentials` grant to obtain access tokens; renew by obtaining a new
  access token with the current client credentials. Devices do not use passkeys
  or refresh tokens.  Do not retain the bespoke
  enrollment bearer token, CSR, client-certificate, enrollment receipt, or
  signing-CA protocols.
- Have device services call the token endpoint
  discovered from `ztchub`'s exact application-scoped `zumd` issuer directly.
  The issuer path's application ID selects the `ztchub`
  application; the device's registered `client_id` identifies its OAuth client,
  while the token subject identifies the principal.  Role-named resource scopes
  select roles and their effective actions.  `ztchub` does not proxy these
  authorization-server endpoints, and the device does not construct them from
  either the authorization base or the `ztchub` WSS URL.
- Keep confidential client credentials at the device under the secure-storage
  rules of the Zum service-account flow.  `ztchub` must not store or log them.  Send access
  tokens only in authorization headers, never in URLs.
- Derive the authenticated device ID directly from the verified Zum
  service-principal ID.  Reject any peer-provided device ID that differs from
  it; do not add an identity mapping or an independently assigned agent ID.
- Delegate service enrollment, client-credential replacement, token issuance,
  expiry, revocation, and recovery semantics to `zumd`; do not
  reproduce any of that state or logic in `ztchub`.

### Agent connectivity and telemetry aggregation

- Replace `ztcagent`'s operational `host` and `port` configuration with one
  validated `wss` URL.  Do not retain the raw TLS configuration or transport as
  a compatibility path.
- Permit `ztchub` to expose multiple configured listeners and accept many
  concurrently connected agents across them.  All listeners use the same
  authentication, authorization, routing, and connection-generation handling.
- Authenticate each WebSocket upgrade with a Zum-issued bearer access token and
  bind all messages on that connection to its verified device-service principal
  and mandatory device ID.  Require the `Telemetry` action.
- Carry exactly one `Ztc.fbs.Msg` root in each WebSocket binary message.  Verify
  it with the existing Ztc validation path and enforce the configured message
  limit before allocating or retaining a complete message.  Reject text,
  malformed, and oversized messages predictably.
- Use `Zws` ping/pong and bounded idle, upgrade, and close deadlines for agent
  connections.
- Route front-end telemetry requests to the selected agent or agents and route
  acknowledgements, telemetry, errors, and end-of-stream messages back to the
  originating front-end request.  Request identifiers are scoped by session;
  one client must not collide with another client's identifiers.
- Preserve one upstream agent subscription per front-end subscription.  Do not
  coalesce identical requests in the initial implementation.  When a front-end
  subscription ends or its connection closes, send the corresponding
  unsubscribe to the agent before releasing the routing state.
- Preserve agent ID, agent connection generation, and per-stream ordering on
  all telemetry delivered to front ends.  A reconnect must not make stale
  frames appear to belong to the new connection generation.
- Maintain bounded frame and byte queues at every network and cross-shard
  boundary.  A slow agent or front end must not block unrelated peers or cause
  unbounded memory growth.
- Never silently discard telemetry or splice a stream across queue overflow.
  Discard any queued telemetry for the affected subscription, deliver the
  existing terminal `Error` with a stable overflow code, and require a fresh
  subscription.  Retain the front-end WebSocket and its other subscriptions;
  reserve bounded control capacity so the terminal error does not depend on
  space in the overflowing telemetry queue.
- On agent disconnect or reconnect, terminate its existing front-end streams
  with `EOS`.  Do not recreate upstream subscriptions automatically; front ends
  must resubscribe.
- Aggregate live telemetry only in the initial implementation.  Durable
  telemetry history, analytics, and background retention are not part of this
  goal.

### Client-facing service

- Serve TLS WebSocket connections for both native and browser telemetry clients.
  Keep the contract client-independent; do not derive it from or depend on the
  stale work-in-progress `zdash` implementation.
- Have registered public front-end clients discover and call `zumd`'s
  application-scoped `/v1/authorize`, `/v1/token`, and `/v1/revoke` endpoints
  directly.  A native client uses authorization code with PKCE and a system
  browser; it does not link to `libZum` or implement provider integration
  itself.  Clients are configured with the exact issuer and consume absolute
  endpoints from validated RFC 8414/OIDC metadata rather than constructing
  endpoint paths.
- Use the shared `Ztc.fbs.Msg` contract for front-end requests and responses.
  The minimum request set is subscribe and unsubscribe by exact device ID.  The
  minimum response set uses the existing `Ack`, `Error`, `Telemetry`, and `EOS`
  taxonomy.  Device inventory, enrollment, removal, and credential
  administration are Zum operations, not `ztchub` FlatBuffers operations.
- Extend the existing Ztc message contract with device selection, session-scoped
  request/subscription identity, authenticated source agent ID, and agent
  connection generation.  Reuse the existing request, filter, telemetry,
  acknowledgement, error, and end-of-stream structures.  The hub derives source
  identity and generation from the authenticated agent connection and preserves
  them when routing responses.  Do not add a separate hub envelope, duplicate
  message wrappers, or protocol versioning.  Propagate the breaking contract
  change to all dependents without compatibility paths.
- Carry exactly one verified `Ztc.fbs.Msg` root in each WebSocket binary
  message.  Reject text messages and malformed or oversized binary messages
  without hand-rolled parsing.
- Use WebSocket ping/pong and bounded idle/close deadlines supplied by `Zws`.
- Authenticate before accepting telemetry operations.  Native clients use a
  bearer access token in the WebSocket upgrade request.  A browser flow must
  use an HTTPS-established secure session or another standards-aligned carrier
  supported by browser WebSockets; access tokens must not appear in URLs.
- Require every interactive client to use `ztchub`'s application-scoped issuer,
  identify its registered public `client_id`, and select only scopes allowed
  for that client under the `ztchub` application.  `zumd` resolves the
  application ID from the issuer endpoint path before resolving the client and
  binds the authorization code and tokens to both.  The issuer selects the
  application, whose stored `audience` supplies the access-token `aud` claim;
  a client-supplied `resource` selector does not select the application.
- Require the `Request` action for every front-end protocol request.  The
  effective actions used for a WebSocket are fixed from the access token verified at
  connection time; do not refresh them or re-resolve role assignments while
  that connection remains established.
- Provide fixture-oriented protocol clients under `ztc/test` or `ztc/itest` for
  automated self-test.  They must drive the supported native and browser
  authentication carriers, WebSocket lifecycle, FlatBuffers operations, and
  negative cases without manual interaction.  Reuse production protocol types
  and serializers; do not create a parallel test codec or treat these fixtures
  as a supported end-user client library.
- Provide a minimal, non-test skeleton client under `ztc/example`.  It must
  demonstrate direct authentication through `zumd`, token lifecycle,
  WebSocket connection, subscribe/unsubscribe by device ID, attributed
  telemetry, and orderly shutdown.  Separate the reusable protocol/session
  state from platform-specific user interaction and authentication carriers so
  the skeleton is the foundation for future CLI, browser, and native clients.
  It must follow the client contract rather than depend on `zdash` or link to
  `libZum`.

## Zum ownership and authorization contract

- Enroll one `ztchub` application in `zumd`, representing the protected telemetry
  service/API.  It owns one stable application-scoped issuer and one immutable
  `audience` attribute used as access-token `aud`.
- Provision `ztchub` itself with distinct confidential workload credentials for
  its authorization-management relationship with `zumd`.  Do not conflate the
  management identity, client ID, or issuer with the application's ID, exact
  issuer, or access-token audience.  These credentials are used only for
  service authentication and catalog management.
- Register each browser, desktop, CLI, or other interactive front end as a
  separate public OAuth client beneath the `ztchub` application, with its own
  `client_id`, redirect URIs, origins, and grant policy.  This includes any
  future conforming `zdash`; none receives the confidential workload secret.
- Enroll each device as a service principal with its own OAuth client beneath
  the same application.  Its principal ID is the device ID and the service-account
  ID used by its `ztcagent`. Device client credentials, access tokens,
  credential rotation, and revocation remain owned by `zumd`. Device tokens
  use `client_credentials`; interactive clients retain authorization code
  with PKCE and refresh-token rotation.
- Every device or interactive client calling the `ztchub` resource uses the
  same exact application issuer, while its own `client_id` identifies that
  particular device, browser, CLI, desktop, or native client.  The issued
  access token carries the application's configured `ztchub` audience.  Neither
  `client_id`, scopes, a client-supplied `resource`, nor DNS selects or
  determines that audience.
- Keep the `zumd` authorization base, stable application ID, exact `ztchub`
  issuer, confidential management client ID and issuer, access-token audience,
  listener addresses, limits, and timeouts in normal configuration.  Inject
  the confidential workload client secret through `ZUM_CLIENT_SECRET` and the
  SSF callback credential through `ZUM_SSF_CALLBACK_AUTH`.
- Configure `libZum` separately for the authorization-management relationship
  and for validation against the exact application issuer and access-token
  audience.  Use it to discover the issuer-specific signing keys, verify access
  tokens against exact `iss`, `aud`, signature, expiry, and token type, and
  obtain the principal's effective actions; do not parse or validate JWTs
  independently.  Never accept an OIDC ID token as an access token.
- Activate and validate `libZum`'s SSF Receiver endpoint before `ztchub` reports
  ready or admits authenticated connections.  Use the existing optional
  introspection fallback when a newly rotated signing key is not yet available
  through JWKS; fail closed if verification cannot be completed.  SSF
  notifications concern refresh-token families, not access-token revocation.
  Never recover by polling.
- On startup, use `Zum::Service` to authenticate its confidential management
  client against the core management issuer with `client_credentials` and
  `zum.catalog`, then idempotently publish the `ztchub` action and role catalog
  before reporting ready.  Resource scopes are derived from those roles at
  runtime.  `ztchub` owns the catalog; `zumd` owns principals, application
  membership, client access grants, and role assignments.  Catalog publication
  must never create, restore, or alter memberships or role assignments.
- Publish exactly two Zum actions, each representing an individual permission:
  `Request` for front-end protocol requests and `Telemetry` for agent connection
  and telemetry delivery.  Do not generate a Cartesian CRUD/query taxonomy or
  separate permissions for individual message variants.  Zum's service
  inventory, enrollment, removal, and credential operations retain their own
  Zum authorization model.
- Publish exactly two standard roles: `Client`, containing `Request`, and
  `Agent`, containing `Telemetry`.  Keep role assignment out of `ztchub`.
- Fail closed when token verification or required authorization cannot be
  completed.  Transient `zumd` failure must not become an authentication bypass.
  Existing authenticated streams may continue only while their established
  access-token credential remains within its expiry and the SSF Receiver
  remains operational; refresh-family notifications affect renewal state only.

Device services and interactive users are distinct Zum principals presented to
one `ztchub` authentication and authorization layer.  A device access token
authenticates an agent WebSocket upgrade; an interactive-user access token
authenticates front-end operations.  The verified principal type determines
which operations and actions are available, so a device token cannot act as a
front-end user or vice versa.

## Runtime and framework requirements

- Implement `ztchub` within the `ztc` module and preserve the standard module
  directory order: `src`, `test`, `util`, `itest`, `example`, then any other
  directories in dependency order.
- Implement `ztchub` as a Zdb application.  Zdb activation controls admission:
  activate the complete configured listener set only while the application is
  active, and stop admission and drain all listener work on deactivation before
  relinquishing ownership.
- Do not add a `ztchub`-specific Zdb schema in this goal.  Authentication and
  credential persistence are delegated to `zumd` through `libZum`; subsequent
  work may add `StoreTbl`s when hub-owned durable state is required.  Use
  `ZdbMem` for tests unless state must survive a process restart, in which case
  use `ZdbSL`.
- Follow `AGENTS.md` and `GUIDELINES.md` throughout.  Reuse `Zws`, `Zhttp`,
  `Ztls`, `Zdb`, `libZum`, `ZfStruct`, `ZfbStruct`, `ZiIOBuf`, `ZiLog`, and
  `ZmScheduler`; do not create parallel protocol, persistence, JSON,
  FlatBuffers, buffer, logging, or threading facilities.
- Extend and use the shared Zum/libZum OAuth types for device and front-end
  authentication and revocation, and use `ZfbStruct` for all FlatBuffers
  serialization, loading, and verification.  Direct generated FlatBuffers API
  use requires a concrete capability that `ZfbStruct` cannot express.
- Give Rx, Tx, routing, and any persistent-state work explicit shard ownership.
  Transfer work with asynchronous continuations and bounded queues, not locks
  or blocking waits.  Use pooled `ZiIOBuf` objects for network data and avoid
  copies between ingress, routing, and egress.
- Index active sessions by the non-secret token identity returned by `libZum`
  for diagnostics and connection correlation only.  A refresh-family
  notification invalidates locally held renewal state; it does not close an
  already-issued access-token session.  Remove the index entry during the same
  ordered session teardown.
- Cap work per scheduler turn and repost continuations for remaining fan-out so
  a large fleet or subscription cannot starve network I/O.
- Size and validate the implementation for thousands of concurrently connected
  agents, hundreds of telemetry publishers per agent, tens of active front
  ends, and tens of subscriptions per front end.  Support refresh intervals as
  low as one second and meet a 200 millisecond fan-out latency SLO under that
  workload.  Derive queue and scheduler-turn defaults from this envelope rather
  than unexplained fixed capacities.
- Teardown is asynchronous and ordered: stop admission, cancel timers, drain Rx,
  routing/database continuations, and Tx, close sessions, then release owners.
  If `ztchub` has local durable state, drain the Zdb write queue before shutdown
  completes.
- Log security and lifecycle events to `ZiLog`.  Do not build a duplicate audit
  database or persistence/replay facility for logs.
- All message counts, byte limits, scheduler-turn limits, deadlines, listener
  sets, server certificate paths, WSS URLs, and Zum identifiers are validated
  configuration with workload-appropriate defaults.  Secrets and tokens are
  not configuration.

## Delivery sequence

1. Verify the existing application-scoped issuer and discovery model, client
   binding, application-owned audience, and direct-OAuth prerequisites in
   `zumd` and `libZum`, then freeze and test the Zum-backed device OAuth flow
   and the shared `Ztc.fbs.Msg` contract
   over WebSockets for agents and front ends, including routing and source
   metadata.
2. Register the `ztchub` application and provision its
   separate confidential management relationship with `zumd`, integrate
   `Zum::Service`, reuse the shared SSF/SET revocation push, retry, and on-demand
   introspection path required by `zumpingd` and `ztchub`, and idempotently
   publish the action/role catalog and enforce its effective actions.
3. Implement Zdb-controlled activation of the shared device/user authentication
   layer and configurable listener set; enroll device services with mandatory
   device IDs and exercise `client_credentials` token acquisition, renewal,
   and access-token expiry termination. Exercise interactive-client passkey
   login, refresh rotation, family notification, push retry, and recovery
   after Receiver unavailability.
4. Migrate `ztcagent` from `host` plus `port` to a `wss` URL and from its bespoke
   enrollment/mTLS identity to Zum tokens, then connect routing to multiple real
   `ztcagent` instances.
5. Implement the authenticated front-end WebSocket server and bounded routing
   between front-end subscriptions and agents.
6. Provide deterministic native and browser protocol test clients that
   authenticate directly through `zumd` and exercise the `ztchub` WebSocket
   FlatBuffers protocol as automated self-tests.  Also provide the independent
   `ztc/example` skeleton client as the reusable starting point for future CLI,
   browser, and native clients.  Adapting the stale `zdash` implementation is
   not required to define or prove the client contract.
7. Complete unit, integration, restart, backpressure, malformed-input, and
   shutdown testing; update `ztc/README.md` and example configuration.

Do enough coherent implementation between incremental builds to keep build
cost low.  Use `make -j2` for builds.  Do not reconfigure the build, run `make clean`, add compatibility or
migration code, or broaden the task into durable telemetry storage unless a
specific implementation need is established.

## Acceptance criteria

- A device with a mandatory, unique device ID is enrolled in `zumd` as a service
  identity with a distinct OAuth client beneath the `ztchub`
  application, authenticates with `client_credentials` through the exact
  issuer's discovered token endpoint, obtains an access token, and connects to its
  configured `ztchub` WSS URL with the access token.
- Device access-token renewal, expiry, and client-credential replacement follow
  Zum semantics across `ztchub` restart. Interactive-client refresh-token
  rotation, replay rejection, and revocation retain Zum semantics.
- Revoking a refresh token, detecting refresh-token replay, or revoking its
  refresh family in `zumd` pushes the family notification through `libZum` to
  configured consumers before family expiry.  Already-issued access tokens
  remain valid until their ordinary expiry; no access-token table or denylist is
  maintained.  Unrelated sessions and connections remain unaffected.
- `zumpingd` and `ztchub` exercise the same shared `libZum` revocation facility.
  Receiver startup, signed SET validation, retry after Receiver unavailability,
  duplicate delivery, on-demand introspection fallback, and `zumd`
  unavailability are covered and fail closed.  No polling is used.
- Device ID, verified service-principal ID, and `ztcagent` service-account ID
  are identical, with no separate identity mapping.  A second live `ztcagent`
  claiming that ID, or a supplied device ID that differs from the verified
  principal ID, is rejected predictably.
- At least two agents can be connected concurrently.  Their identical local
  telemetry identifiers remain distinguishable by authenticated agent ID, and
  disconnecting or backpressuring one does not disrupt the other.
- Authenticated native and browser protocol test clients with the `Request`
  action can subscribe by exact device ID, receive attributed telemetry,
  unsubscribe, and observe end-of-stream.  A user lacking the required
  action is forbidden, and an invalid, expired, wrong-issuer,
  wrong-audience, absent, or OIDC ID token is rejected.
- The protocol test clients run from the normal automated test targets without
  manual browser or passkey interaction and cover both successful operation and
  authentication, authorization, framing, reconnect, and shutdown failures.
- The `ztc/example` skeleton client builds independently of the test fixtures,
  completes the representative authentication/subscribe/unsubscribe flow and
  cleanly isolates reusable protocol/session behavior from the adapters a
  future CLI, browser, or native UI will replace.
- Device and interactive-user access tokens traverse the same verification
  layer on every configured listener while remaining confined to their
  respective operations and actions.
- Agent and front-end connections use the same `Ztc.fbs.Msg` schema and
  serializers, including routing and source metadata, with no separate hub
  envelope or protocol versioning.  All dependents use the updated contract.
- Device and front-end OAuth clients use the same exact application issuer and
  access-token audience with distinct `client_id` values.  A token or signing
  key from a second application's issuer is rejected.
- OAuth authorization, token, revocation, keys, and discovery operations remain
  on `zumd`; `ztchub` exposes no private OAuth façade, and clients discover
  rather than construct those endpoint URLs.
- Catalog publication is idempotent across restart and does not change any
  principal's application membership or assigned roles.  Only actions and roles
  are published; `Client` and `Agent` scopes resolve directly to their roles.
- Enrollment and queries use the application's `audience` attribute.  Client
  access grants select roles, and consent remains keyed by user, client, and
  application.  No dependent introduces audience IDs, a separate audience
  lifecycle, permission objects, or a resource-scope catalog.
- The configured listener set admits connections only while the Zdb application
  is active.  Deactivation stops admission and drains connections, routing work,
  and database writes before another activation can own the service.
- Text WebSocket messages from agents or front ends, invalid FlatBuffers,
  oversized messages, stale request generations, and unauthorized operations
  are rejected predictably without process failure or unbounded retention.
- Front-end queue overflow discards queued telemetry only for the affected
  subscription, delivers its terminal `Error`, and retains the WebSocket and
  other subscriptions.  It never silently corrupts stream continuity or
  introduces a new `Gap` message type.
- Load tests cover thousands of agents, hundreds of telemetry publishers per
  agent, tens of active front ends, tens of subscriptions per front end, and
  one-second refresh intervals, and demonstrate the 200 millisecond fan-out
  latency SLO without unbounded queues or starvation.
- Graceful shutdown drains any database writes and all I/O shards, cancels
  timers, closes clients, and releases buffers, sessions, and secrets without
  leaks.
- New unit tests emit TAP under `ztc/test`; multi-process, `zumd`, OAuth/passkey,
  TLS, multiple-listener, restart, and end-to-end tests live under `ztc/itest`
  and run from their normal `make test` targets.  Use `ZdbMem` by default and
  `ZdbSL` only for cases that require persistence across process restart.

## Explicit non-goals

- Replacing `zumd` as the authority for principals, applications, OAuth clients,
  memberships, client access grants, or role assignments, or introducing
  separate audience, permission, or resource-scope domain objects.
- Implementing a parallel device enrollment, credential, token, or revocation
  system in `ztchub`; devices are Zum service identities, not Zum users.
- Retaining the existing `ztcagent` enrollment JSON, CSR, client-certificate, or
  raw TLS protocols.
- Device or fleet inventory, enrollment, removal, credential administration,
  dashboards, alert evaluation, or any other fleet orchestration.  These
  identity and lifecycle operations belong to `zumd`.
- Durable telemetry history or query analytics.
- Backward-compatibility shims, legacy data migration, hand-rolled protocol
  codecs, or a second audit/event persistence subsystem.
- Delivering finished CLI, browser, native, or `zdash` products beyond the
  automated protocol clients and reusable example skeleton required here.
- High-availability topology or failover policy beyond clean restart and
  reliance on `zumd`'s persisted identity and token semantics.

## Open questions and ambiguities

The goal above uses explicit working assumptions where necessary.  Resolve the
following questions before freezing the affected schema or public protocol;
they should not be answered by silently adding infrastructure.  Question
numbers are stable identifiers.  When a question is answered or otherwise
resolved, retain its entry and original number, record the resolution in place,
and do not renumber later questions.

### Enrollment and device identity

1. **Resolved:** Device ID = Zum service-principal ID = `ztcagent` service-account
  ID.  Device, principal, and agent service account are 1:1, with no separate
  device identity or mapping.  The ID is mandatory and stable.  There is exactly
  one `ztcagent` per device and at most one active connection for that ID.
2. **Resolved:** Delegate identity enrollment, re-enrollment, passkey replacement,
  and credential rotation to `zumd`.  The device remains the same Zum service
  identity through authorized credential changes.
3. **Resolved:** Remove the issued client key/certificate lifecycle. The device
  uses the existing confidential OAuth client credentials with the
  `client_credentials` grant to obtain short-lived access tokens. It renews by
  requesting a new token with the current credentials; devices do not use
  passkeys or refresh tokens.
4. **Resolved:** Remove `ztchub` enrollment tokens.  `zumd` owns service
  admission, token lifetime, issuance, binding, rotation, replay rejection, and
  revocation.
5. **Resolved:** `ztchub` does not operate an enrollment CA or issue device
  certificates.  WSS uses the configured server TLS identity; device
  authentication uses a Zum-issued access token.
6. **Resolved:** There is no custom certificate-signing or enrollment-response
  transaction to replay.  Device enrollment and OAuth recovery use `zumd`'s
  persistence and protocol semantics.

### Service topology and persistence

7. **Resolved:** One `ztchub` process may expose multiple configured listeners
   and therefore multiple WSS URLs.  Each `ztcagent` is configured with its WSS
   URL; URL assignment is not part of device enrollment.  High availability
   remains a non-goal.
8. **Resolved:** This goal needs no `ztchub`-specific Zdb schema because user and
  device authentication and credential persistence are delegated to `zumd`
  through `libZum`.  Subsequent work will add `StoreTbl`s for hub-owned durable
  state as needed.  Tests use `ZdbMem` unless persistence across process restart
  is required, in which case they use `ZdbSL`.
9. **Resolved:** `ztchub` is a Zdb application, and Zdb activation controls the
  active server and its complete configured listener set.  Replication and
  failover policy remain outside this goal.
10. **Resolved:** Permit multiple configurable listeners.  Agent WebSocket,
  front-end WebSocket, and any browser-session carrier routes may be assigned
  among them, but all listener instances feed the same authentication,
  authorization, routing, and lifecycle implementation.  Authorization-server
  endpoints remain on `zumd`.  Listener-specific WSS paths, server certificates,
  and public hostnames remain ordinary validated configuration.

### Front-end protocol

11. **Resolved:** `ztchub` has no agent inventory or administration API.
  Because an agent is 1:1 with a device, its service principal and OAuth client
  are enrolled, removed, and credentialed through `zumd`.  The
  `ztchub` FlatBuffers/WebSocket protocol
  is only for live telemetry operations.
12. **Resolved:** Client-facing APIs support clients in general, including both
  browser and native clients.  `zdash` is only a conceptual example; its current
  repository implementation is stale work in progress and must not be relied on
  as the client contract.  Native clients can use an Authorization header;
  browser authentication must use a secure carrier supported by browser
  WebSockets, without tokens in URLs.
13. **Resolved:** Follow the standard OAuth role separation: clients discover
  and call `zumd`'s authorization, token, and revocation endpoints directly;
  `ztchub` is the resource server and does not proxy them.  Each device service
  or interactive front end has its own registered `client_id` beneath the
  `ztchub` application.  All use its exact application-scoped
  issuer: the application-ID path component tells `zumd` which protected
  service the authorization targets, `client_id` identifies the requesting
  client, while the token subject identifies the principal.  Resource scopes
  select roles and their actions.  The selected application's stored `audience`
  supplies access-token `aud`; this goal does not use
  `resource` to select the application, and clients do not construct endpoint
  paths.
14. **Resolved:** Agent and front-end connections share the existing
  `Ztc.fbs.Msg` contract, extended with device selection, session-scoped
  request/subscription identity, authenticated source agent ID, and agent
  connection generation.  There is no separate hub envelope or protocol
  versioning.  Reuse the existing `Request`, `Ack`, `Telemetry`, `EOS`, and
  `Error` taxonomy, including subscribe and unsubscribe semantics, without
  adding a `Gap` variant.  There are no agent-listing or administration
  operations.  Propagate the breaking change to every dependent.
15. **Resolved:** `ztchub` does not expose agent lists, pagination, or an agent
  query language.  A telemetry subscription selects the 1:1 device/agent by
  exact device ID; device inventory and discovery belong to `zumd`.
16. **Resolved:** Each WebSocket binary message contains exactly one verified
  FlatBuffers root.  There is no application-level message batching.

### Authorization lifecycle

17. **Resolved:** The authorization catalog contains exactly two Zum actions,
  each representing an individual permission: `Request` and `Telemetry`.  The
  `Client` role contains `Request`; the `Agent` role contains `Telemetry`.
18. **Resolved:** Each application represents one service and owns one immutable
  `audience` attribute used as access-token `aud`.  The `ztchub` application is
  the telemetry service/API; a different client type does not receive a
  different audience.  There is no separate audience table, ID, or lifecycle.
  The standard roles are `Client` and `Agent`, and their single actions are
  `Request` and `Telemetry` respectively. Their names are the two derived
  resource scopes; there is no separate scope catalog. Whether separate
  deployments or tenants are separate applications remains a
  deployment decision.
19. **Resolved:** A WebSocket's effective actions are fixed from the access token
  accepted at connection time.  They are not refreshed or re-resolved after a
  role assignment changes while the connection remains established.  Token
  expiry remains the access-token termination mechanism; refresh-family SETs
  notify renewal holders and do not revoke an already-issued access token.
20. **Resolved:** `ztchub` has no disable operation.  Removing the device's
  service principal or OAuth client, or revoking its credentials, is a `zumd`
  operation.  Refresh-family
  revocation uses standard SSF/SET push delivery through `libZum`; existing
  access-token sessions continue until access-token expiry.

### Telemetry semantics and limits

21. **Resolved:** The initial implementation performs live multiplexing only.
  It retains neither durable history nor a latest-value snapshot for new
  subscribers.
22. **Resolved:** An agent disconnect or reconnect ends affected streams with an
  `EOS`.  `ztchub` does not recreate upstream subscriptions; front ends must
  resubscribe.  When a front end disconnects, `ztchub` sends unsubscribes for
  its live requests to the corresponding agents.
23. **Resolved:** Do not coalesce identical telemetry requests in the initial
  implementation.  Preserve one upstream agent subscription per front-end
  subscription and its independent lifecycle.
24. **Resolved:** There is no `Gap` message.  On front-end queue overflow,
  `ztchub` discards queued telemetry for the affected subscription, delivers a
  terminal `Error` with a stable overflow code, and retains the WebSocket and
  its other subscriptions.  Terminal-control delivery remains bounded.
25. **Resolved:** Size for thousands of concurrently connected agents, hundreds
  of telemetry publishers per agent, tens of active front ends, and tens of
  subscriptions per front end.  The minimum supported refresh interval is one
  second and the fan-out latency SLO is 200 milliseconds.  Queue and
  scheduler-turn defaults must be derived and tested against this workload.
