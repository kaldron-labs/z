# `ztchub` implementation plan

This plan implements [goal.md](goal.md), which controls scope and acceptance.
[zum/README.md](zum/README.md) defines the Zum data model and terminology.
The implementation is a small live data plane. `ztchub` authenticates agents
and front ends on WebSockets,
fans telemetry in from `ztcagent`, and forwards it to subscribed clients.
Identity, OAuth, passkeys, role assignment, token issuance, refresh, and
revocation remain Zum responsibilities.

## Ownership boundary

`zumd` and `libZum` own:

- applications, OIDC issuer integration, OAuth clients, user and service
  principals, memberships, client access grants, consent, passkeys, OAuth flows,
  tokens, refresh families, and role assignments;
- access-token verification, including the exact application issuer and its
  configured audience, signature, expiry, token type, and effective actions; and
- the shared signed SSF receiver and refresh-family notification path.

The device flow remains direct: a device uses the exact application-scoped
issuer's discovered token endpoint with Zum’s existing `client_credentials`
grant. Renew access tokens using current confidential client credentials;
devices have no passkeys or refresh tokens. Credential replacement and token
expiry are exercised through `zumd`. Interactive clients retain passkey login,
authorization code with PKCE, refresh rotation, and family revocation.
The hub never proxies these endpoints, constructs them from its WSS URL, or
stores a refresh credential.

`ztchub` owns only live connection state and protocol routing. It has no device
inventory, enrollment database, credential store, token table or denylist,
telemetry history, replay service, dashboard, or hub-specific durable schema.
Zdb is used only to control application activation and listener ownership.

`ztchub` is the protected application/service/API. Each application represents
one service and owns one immutable `audience` attribute, used as the resource
access token's `aud`. `appEnroll` requires it; `appQuery` returns it on the
application. There is no audience table, ID, or separate lifecycle. Use Zum
schema 20 and reprovision old stores without migration or compatibility code.

A principal is the user or service identity acting through an OAuth client;
the client is the credentialed software, conceptually separate from that
principal and the protected application. The token subject identifies the
principal; `client_id` identifies the OAuth client. An action is a permission,
with no separate permission object. The application publishes exactly two
actions and two roles through `Zum::Service`:

| Role | Action | Use |
|---|---|---|
| `Client` | `Request` | front-end WebSocket |
| `Agent` | `Telemetry` | `ztcagent` WebSocket |

Within the application, a scope is the runtime view of a requested role:
scope name equals role name, and its actions
are the role's actions. No scope table, scope CRUD, or separate scope binding
exists. Client access grants select application roles; consent is keyed by
user, client, and application. Identity/provider scopes such as `openid` and
the management protocol scope `zum.catalog` remain separate from role-derived
resource scopes. Role administration remains in Zum.

`deviceID` is the Zum service-principal ID and the service-account ID used by
the `ztcagent` on the device. Device, principal, and agent service account are
1:1 and use the same mandatory stable ID. Derive authenticated device identity
directly from the verified service-principal ID; use it for routing and source
attribution. Do not add an independently assigned agent/device ID or an identity
mapping. At most one connection may be active for that ID. This requires no
durable device record in the hub.

## Changes

Implement these changes and propagate the breaking contract to all dependents:

| Location | Change |
|---|---|
| existing Ztc schemas and metadata, starting with `ztc/src/fbs/ztc_msg.fbs` | Extend the shared message contract with routing and source metadata. Delete `ztc_hub.fbs`, its wrappers, version handling, and generated build inputs. |
| `ztc/src/ZtcHubd.hh/.cc` | Add the private live connection, subscription, routing, bounded-queue, and Zum adapter state. |
| `ztc/src/ztchub.cc` | Add the daemon, WSS listeners, browser session POST, and SSF callback endpoint. |
| `ztc/src/ZtcAgent.hh/.cc`, `ztcagent.cc` | Replace retired enrollment/mTLS transport with the deployment-owned OAuth token adapter and WSS. |
| all Ztc protocol dependents | Use the shared schema, production types, validators, and serializers without compatibility paths. |
| build files | Generate the shared schema and build in `src`, `test`, `util`, `itest`, `example` order, followed by other directories in dependency order. |
| `ztc/test`, `ztc/itest`, `ztc/example` | Add protocol, routing, process, restart, backpressure, and independent-client coverage. |
| `ztc/README.md`, `ztc/ztcagent.conf` | Document the direct OAuth adapter, WSS, roles, limits, and shutdown contract. |

Do not modify `zi` or the stable `zws`/`zhttp` stream APIs. Use their existing
transport and transmit-fence facilities. Do not add hub-specific protocol,
OAuth, browser-auth, queue-manager, persistence, or audit libraries.

## Wire protocol

Use one `Ztc.fbs.Msg` contract on agent and front-end connections. Extend the
existing message structures with exact device selection, session-scoped
request/subscription identity, authenticated source agent ID, and captured agent
connection generation. Retain the `Request`, `Ack`, `Telemetry`, `EOS`, and
`Error` taxonomy and existing request/filter/payload structures, including
subscribe and unsubscribe semantics. Do not add a hub envelope, duplicate
wrappers, or protocol versioning. Remove the separate versioned WSS subprotocols
and use one shared, unversioned contract.

Request identifiers are unique within a front-end session. The hub maps them
to forwarded agent request sequence numbers and restores the originating
identity on responses. Unsubscribe is idempotent. Source identity and generation
come from the authenticated agent connection, never from untrusted message
attribution. Preserve one upstream subscription per front-end subscription.

Carry exactly one verified `Ztc.fbs.Msg` root per binary WebSocket message;
there is no batching. Reject text, malformed or semantically invalid messages,
unexpected subprotocols, and oversized messages predictably. Enforce configured
limits before allocating or retaining a complete message. Use `ZfbStruct` for
serialization, loading, and verification, followed by existing Ztc semantic
validation. Direct generated FlatBuffers API use requires a concrete capability
that `ZfbStruct` cannot express. Propagate the schema change to every dependent.

Use existing `Error` messages with stable codes for missing agents, duplicate
subscriptions, invalid requests, overflow, and unauthorized operations. Agent
loss terminates its streams with `EOS`. There is no `Gap` or replay protocol.

## Runtime data path

The data path has three operations:

1. An authenticated agent is indexed by its verified device ID and a
   hub-assigned connection generation. A second live connection for that ID is
   rejected. Valid agent output is correlated by the forwarded request sequence
   number.
2. An authenticated client subscribes to an exact device ID. The hub records a
   small subscription record and forwards the existing subscribe request to the
   selected agent.
3. Agent Ack, Telemetry, EOS, and Error use the shared message metadata to reach
   that client's subscription with authenticated source attribution. On
   subscription termination or front-end disconnect, send the corresponding
   upstream unsubscribe before releasing routing state. On agent loss, send
   EOS for its streams and require fresh subscriptions after reconnect.

Keep one authoritative active-agent index and one subscription index, with
small reverse indexes for disconnect cleanup. Records contain IDs, generation,
sequence numbers, and the device string; they do not retain cross-shard session
pointers. Maintain an ephemeral index from the non-secret token identity
returned by `libZum` to live session IDs for diagnostics and teardown correlation.
Remove entries during ordered session teardown. This index is not an
access-token table, denylist, or authorization source. Use the
existing `ZiMultiplex` Rx owner for routing state and the Tx owner for transport
queues. Cross-owner callbacks capture only fixed metadata or a
`ZmRef<ZiIOBuf>`. Transfer work asynchronously; do not add locks or blocking
waits to permit non-owner access.

Bound frames and bytes at every network and cross-shard boundary. Cap work per
scheduler turn and repost remaining fan-out; preserve per-stream order and
reject stale-generation work without attributing it to a reconnected agent.

Each client connection has bounded control and telemetry queues. Enqueue pooled
`ZiIOBuf` references rather than copying payloads. Reserve capacity for control
and terminal messages while preserving per-stream order. If one subscription's telemetry queue is full, discard only that
subscription's telemetry, send one terminal `Overflow` error, unsubscribe the
agent request, and preserve the client and every other subscription. If control
delivery cannot be preserved, close only that client. Agent ingress has its own
configured bound and closes only the offending agent.

The transmit pump submits one frame through the existing WSS stream API and
uses its existing transport fence before submitting the next frame. This keeps
the hub's retained data bounded and keeps backpressure in the transport owner;
no change to `zws`, `zhttp`, `zi`, or their queues is required.

## Authentication and Zum integration

For native agents and clients, verify the bearer once at WSS upgrade through
`Zum::Service`. Require the exact application issuer and the application's
configured `audience`, and the action matching the principal's operations:
`Telemetry` for agents and `Request` for clients. Reject wrong issuer,
audience, expiry, token type, ID tokens, and missing credentials. Reject an
agent whose supplied `deviceID` differs from the verified service-principal ID.
Device and front-end clients use the same exact application-scoped Zum issuer
and the application's audience while retaining distinct
`client_id` registrations. A client-supplied `resource`, WSS URL, DNS name, or
client ID never selects the audience.

Require service principals for agent operations and user principals for
interactive front-end operations. Check the verified authentication context
returned by `Zum::Service`: `client_credentials` for agents, and `passkey` or
`oidc` for interactive clients. Actions alone do not identify the principal
kind. Reject introspection results which omit the required authentication
context. Effective actions are fixed from the token
verified at connection time; do not refresh them or re-resolve roles on an
established connection. Terminate at token expiry. Fail closed when verification
cannot complete or the SSF receiver is unavailable, including during admission.

For browsers, provide one HTTPS POST session path. The browser obtains its
token directly from the discovered Zum endpoints, posts the bearer over TLS,
and receives a random host-only `__Host-` cookie with `Secure`, `HttpOnly`, and
`SameSite=Strict`. Store only the opaque cookie, token identity, expiry,
session ID, and verified principal/actions in memory; the WSS upgrade resolves
the cookie without storing the bearer. Validate the token through `Zum::Service`
when establishing the session and enforce its expiry and fixed actions.

Start the shared `Zum::Service` SSF receiver before admitting connections.
Use the same libZum receiver implementation from `zumpingd` and `ztchub`.
Forward signed SET requests to it without a hub parser, cursor, poller, replay
stream, or access-token denylist. The receiver validates RFC 8417 SET type,
signature, transmitter issuer, receiver audience, event subject, event type,
and bounds, and acknowledges duplicates idempotently. Reuse `zumd`'s transmitter
delivery/retry lifecycle until refresh-family expiry. Use the existing optional
introspection fallback when a newly rotated signing key is not yet available
through JWKS; fail closed if verification cannot complete. A refresh-family
notification affects future renewal state; an already-issued access token
remains usable until its normal expiry.

Provision a distinct confidential management client, core management issuer,
and independent management URL. Use `Zum::Service` with `client_credentials`
and `zum.catalog` to publish the application's action and role catalog
idempotently at startup. Never alter memberships or role assignments. Resource
scopes derive from roles at runtime; do not publish or persist a scope catalog.

## Lifecycle and configuration

Use `ZfStruct` for configuration and existing `ZfURI`, `Zhttp`, `Zws`, `Zdb`,
`ZiMultiplex`, `ZiLog`, and `Zum::Service` facilities. Configuration contains:

- the complete listener set, WSS and browser paths, certificates, keys, and
  origin allowlists;
- application ID, exact application issuer and immutable audience, independent
  management URL, core management issuer and confidential client ID, and the
  two-role/two-action manifest;
- frame, control, telemetry, byte, connection, upgrade, idle, ping, close,
  and transmit limits; and
- workload counts used to derive queue bounds and scheduler turn work.

Validate all limits and URLs. Use `Zws` ping/pong and bounded upgrade, idle,
and close deadlines for both connection types. Derive queue and turn defaults
from thousands of agents, hundreds of publishers per agent, tens of front ends,
and tens of subscriptions per front end at one-second refresh intervals.

Only `ZUM_CLIENT_SECRET` and `ZUM_SSF_CALLBACK_AUTH` are injected secrets.
Tokens, refresh credentials, passkeys, and cookies are runtime state and are
never configuration.

Application state follows `Down -> Starting -> Publishing -> Listening -> Up`
and the ordered stopping/draining path back to `Down`. Activation starts Zum,
publishes the idempotent catalog, starts the configured listeners, and reports
ready only after the SSF receiver and all listeners are ready. Deactivation
stops admission, cancels timers, drains Rx routing and Tx queues, closes WSS
links, stops the service, and releases Zdb/multiplex ownership.

All dynamic records and queue nodes use named Z heaps or existing pooled
I/O buffers. Follow `AGENTS.md` and `GUIDELINES.md`, including Z containers and
existing raw back-pointer patterns where owners drain dependent work. Shutdown
must make new ingress impossible before releasing any owner. Drain routing and
database continuations, Rx and Tx work, and service callbacks before releasing
owners. Log lifecycle and security events through `ZiLog`.

## Agent migration

Remove enrollment URLs, enrollment JSON, CSR, client certificates, CA-chain
configuration, raw host/port, and length-framed TLS. `ztcagent` receives a
mandatory `deviceID`, exact issuer, client ID, credential-store reference, WSS
URL, and current access token from a deployment-owned adapter. The adapter
performs issuer discovery, `client_credentials` token acquisition and renewal,
and secure client-credential storage. The agent only validates the supplied context and
sends the token in the WSS `Authorization` header.

Reconnect uses the adapter's current token and never reuses a request from a
lost transport. A lost agent connection produces downstream EOS for its live
subscriptions. The independent skeleton under `ztc/example` uses the shared
protocol and demonstrates direct OAuth authentication, token lifecycle,
subscribe/unsubscribe, attributed telemetry, and orderly shutdown. Separate
reusable protocol/session state from platform-specific authentication carriers
and user interaction. Native public clients use authorization code with PKCE
and a system browser; they do not link `libZum` or integrate upstream providers.
`zumd` owns provider integration. Keep the contract independent of `zdash`.
Fixture clients reuse production types and serializers without a test codec or
becoming a supported end-user client library.

The device adapter and fixtures must cover `client_credentials` authentication,
access-token renewal, client-credential replacement, expiry, and recovery across
a `ztchub` restart. Interactive-client fixtures cover passkey authentication,
refresh rotation, refresh-token replay, and explicit revocation. None of those
cases adds state to the hub.

## Delivery sequence

1. Verify the existing Zum issuer/discovery, client binding, application-owned
   audience, and direct OAuth prerequisites. Extend and test the shared Ztc
   contract; delete the separate hub protocol and update every dependent.
2. Enroll the application and its separate confidential management client.
   Integrate existing `Zum::Service`, SSF reception, transmitter retry, optional
   introspection fallback, and action/role publication.
3. Implement Zdb-controlled admission, the shared authentication layer, and all
   configured listeners. Exercise device `client_credentials` login and token lifecycle.
4. Migrate agents to direct OAuth and WSS, then connect at least two real
   `ztcagent` instances with independent identities.
5. Complete front-end authentication and bounded subscription routing, source
   attribution, generation handling, overflow termination, and shutdown.
6. Add automated native/browser protocol fixtures and the independent example
   skeleton, sharing production protocol types and serializers.
7. Complete the validation gates below and update module/example documentation.

## Verification

Run the checks in this order:

1. Build source and all dependent targets with `make -j2`. Do coherent work
   between builds; do not reconfigure, run `make clean`, or add compatibility
   or migration code.
2. Run `ZtcHubTest` for the shared message contract, authentication decisions,
   duplicate devices, generation safety, terminal cleanup, bounded queues,
   overflow, and fan-out fairness.
3. Run the process fixture with real `zumd`, discovered OAuth endpoints, TLS,
   multiple listeners, native and browser clients, wrong credentials, duplicate
   agents, device ID mismatch, reconnect/EOS, SSF delivery, duplicate SETs,
   receiver recovery, signing-key introspection fallback, `zumd` unavailability,
   deactivation, and restart. Exercise the shared SSF facility with both
   `zumpingd` and `ztchub`. Verify that catalog publication remains
   idempotent and does not change memberships or role assignments.
4. Run the declared workload of thousands of agents, hundreds of publishers per
   agent, tens of clients and tens of subscriptions per client, one-second
   refresh intervals, and
   the 200 ms fan-out SLO without unbounded retention.
5. Run normal TAP targets and available memory diagnostics without reconfiguring
   the build. Verify leak-free shutdown, independent example builds and the
   representative authentication/subscription/shutdown flow. Documentation must
   describe the supported contract and direct OAuth flow.

The acceptance audit must also record evidence for the independent example,
multiple listener ownership, exact issuer/audience rejection, ID-token
rejection, one upstream request per subscription, no automatic resubscription,
per-stream ordering and attribution, bounded shutdown, and no durable
telemetry state. Verify application-owned audience enrollment/query behavior,
role-based client access grants, consent keyed by user/client/application, and
runtime resolution of `Client` and `Agent` scopes. Check that no dependent
retains the separate hub envelope, protocol versioning, audience IDs, permission
objects, resource-scope catalog, or compatibility path.
Verify that device ID, principal ID, and agent service-account ID are identical,
and that routing uses this ID without a separate device-to-principal mapping.

Use `ZdbMem` for tests by default and `ZdbSL` only when persistence across a
process restart is required. Unit tests emit TAP under `ztc/test`; process and
OAuth/TLS/restart fixtures live under `ztc/itest` and run from normal `make test`
targets without manual browser or passkey interaction. Run source-tree wrappers,
never `.libs` binaries directly; use `libtool exec` for diagnostic tools.

Completion requires evidence from the matching scope of each gate in
[goal.md](goal.md). A passing unit test does not substitute for the process,
restart, memory, or workload evidence.
