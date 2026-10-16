# `ztcagent` Goal

## Objective

Develop `ztcagent`, a cross-platform `Ztc::App` telemetry consumer and
aggregator.  It discovers telemetry-producing applications on the local host,
fans hub subscription requests out to the applicable applications, and fans
their telemetry responses back in to one long-lived, outbound TLS connection
to the Kaldron device hub.  `ztcagent` is not itself a `Ztc::App`, does not
originate telemetry, and maintains no persistent state.

The agent must run:

- natively on Linux and Windows;
- on a bare-metal operating system or VM; and
- in a container on a cluster node.

Implement it entirely against portable Z Framework interfaces.  Platform
selection, filesystem/shared-memory naming, process control, TLS integration,
and other operating-system differences belong to the framework; `ztcagent`
must not contain platform-specific branches or implementations.

The agent does not expose an Internet-facing management listener.  It always
initiates the connection to Kaldron.

## Architecture

```text
local application ----\
local application -----+-- Ztc shared-memory telemetry --> ztcagent
local application ----/                                  |
                                                            | TLS
                                                            v
                                                  Kaldron device hub
```

`ztcagent` has two distinct protocol boundaries:

1. Locally it is a consumer of the existing `Ztc::App` public-wire protocol:
   the shared telemetry ring and each application's request ring.
2. Remotely it carries the same length-prefixed FlatBuffers `Ztc::fbs::Msg`
   frames over raw TLS/TCP.  TLS is the record-security layer; the existing
   `Ztc::Hdr` remains the application framing layer.

Avoid translating telemetry into JSON or another intermediate representation
on the data path.  Verify inbound FlatBuffers frames at each trust boundary and
forward valid frames without reconstructing them where ownership and lifetime
permit.

The telemetry protocol uses semantic versions encoded in `ztcver` as
`major * 100000 + minor * 1000 + patch`.  Major versions must match.  A receiver
is compatible with a sender when its minor version is greater than or equal to
the sender's minor version; patch versions may differ and describe internal bug
fixes only.  Every `Ztc::App` reports its sending protocol version in
`AppTelemetry.ztcver` so the agent and hub can apply this rule.

## Local Fan-In

- Attach to the shared telemetry ring selected by the existing `ZTC_RING`
  convention.
- Acquire and hold a PID-file lock keyed by the local telemetry ring before
  opening it.  Refuse to start while another live `ztcagent` holds that lock,
  so at most one agent consumes any given local telemetry ring.  Recover a
  stale PID file using the repository's established `ZiPIDFile` semantics.
- The PID-file lock coordinates `ztcagent` instances; it must not claim
  exclusive ownership of the ring or prevent independent readers implemented
  for other purposes.  The deployment is guaranteed never to exceed
  `ZiRing`'s maximum of 62 readers.
- Discover each running `Ztc::App` from its application telemetry and maintain
  an application registry keyed by the `Ztc::App` ID.
- Rely on each producer's ID-keyed PID-file lock to guarantee that two live
  `Ztc::App` producers cannot use the same ID within the system or container.
- Open the corresponding per-application request ring when a hub request must
  be delivered to that application.
- Treat each publisher's receive ring as the source of truth for liveness.  An
  application is live only while that ring exists and has at least one active
  reader; a missing ring or a ring with no active readers means the application
  is dead.  Track arrival, restart, and disappearance without requiring an
  agent restart.
- Keep all queues and pending state bounded.  A slow or disconnected hub must
  not cause unbounded allocation or block local telemetry producers.
- Do not allow `ztcagent` to pin the shared telemetry ring.  It is subject to
  the same `ZiRing::kill()` slow-reader policy as every other reader and may be
  terminated if it fails to consume the oldest outstanding message promptly.
  Its in-memory state must therefore be disposable and safely rebuilt after
  process restart and hub reconnection.
- Preserve the existing maximum-frame checks and reject malformed or
  unsupported `Ztc::fbs::Msg` frames before routing them.

## Enrollment and Agent Identity

The initial bootstrap endpoint is hardcoded as:

```text
https://enroll.devices.kaldron.io:443
```

Enrollment uses a server-authenticated HTTPS `POST`, not mTLS and not an
automatic HTTP redirect.  The bearer token authenticates the agent during
enrollment.  The response is a strictly validated, size-bounded document
containing the assigned agent identity, hub URL, and operational client
certificate chain for the subsequent mTLS hub session.  It may also contain CA
certificates that extend the platform trust store specifically for validating
the enrolled hub.  The agent then establishes a new connection to that hub; it
must not forward the enrollment credential to the hub URL.

The initial enrollment request is JSON over HTTPS:

```http
POST /v1/enroll HTTP/1.1
Host: enroll.devices.kaldron.io
Content-Type: application/json
Accept: application/json
Authorization: Bearer <bootstrap-token>
Cache-Control: no-store

{
  "protocol_version": 1,
  "device_id": "<deployment-supplied-asset-metadata>",
  "nonce": "<base64url-encoded-32-random-bytes>",
  "csr": "<base64-encoded-DER-PKCS10>",
  "agent_version": "<ztcagent-version>"
}
```

Requirements for this request are:

- `Authorization` is the bootstrap authentication and authorization.  Its
  value is the bearer token read from the `ZTC_ENROLL_TOKEN` environment
  variable.  This is PSK-like application credential material but is not a TLS
  PSK.  The agent requires a non-empty value and must never place it in the URL,
  JSON body, container image, files, or logs.
- `device_id` is optional self-reported asset metadata and is never accepted as
  proof of identity or authority.  The enrollment service assigns the
  security-sensitive agent and tenant identity.
- `nonce` is 32 bytes from the operating system CSPRNG and is retained in
  memory with the pending enrollment transaction.  The service echoes it in
  the response; the agent requires an exact match.  It correlates the response
  with the transaction but does not replace or augment HTTPS authentication.
- `csr` is a base64 encoding of a DER PKCS#10 CSR.  Its signature proves
  possession of the locally held private key.  The service ignores
  caller-selected identity and authorization claims in its subject and
  extensions; enrollment authorization derives from the bearer credential.
  The private key is never transmitted.
- All string lengths, decoded lengths, and the complete HTTP body are bounded.
  Unknown required protocol versions, malformed base64, malformed CSRs, and
  unsupported key or signature algorithms are rejected.

A successful response uses HTTP 201, `Content-Type: application/json`, and
`Cache-Control: no-store`, with this logical schema:

```json
{
  "protocol_version": 1,
  "nonce": "<the-request-nonce>",
  "agent_id": "<service-assigned-ID>",
  "certificate_chain": ["<base64-encoded-DER-certificate>", "..."],
  "hub_ca_chain": ["<base64-encoded-DER-CA-certificate>", "..."],
  "hub_url": "<assigned-vendor-hub-URL>"
}
```

The agent accepts only the expected success status and media type and never
follows an enrollment redirect.  It verifies the echoed nonce and every field
and bound, verifies that the leaf certificate's public key matches the locally
generated private key/CSR, and validates the returned chain.  It accepts only
a supported TLS hub URL with a vendor-owned FQDN.  Error responses are also
bounded and must not cause credentials or private state to be logged.

Within one process lifetime, enrollment must survive an indeterminate network
failure after issuance.  Retain the nonce, pending transaction state, generated
private key, and CSR in memory and retry that same transaction.  The service
returns the same enrollment result for an exact retry during a bounded recovery
period and rejects reuse with a different request identity or CSR.  A process
restart discards the pending transaction and begins enrollment again using the
then-current environment token.

At enrollment:

- Generate a unique operational asymmetric key pair locally.  Keep the private
  key on the node and submit only a PKCS#10 CSR containing the public key and
  proof of possession.
- Authenticate bootstrap with the bearer token obtained from
  `ZTC_ENROLL_TOKEN`.
- Treat hostnames, container names, configured node IDs, cloud instance IDs,
  and cluster node IDs as asset metadata, not as authentication by themselves.
- Let the enrollment service assign all security-sensitive identity and tenant
  fields; do not trust names requested by the CSR or self-reported metadata.

Keep the assigned identity, hub assignment, supplemental hub CA certificates,
protocol state, issued client certificate, and private key only in process
memory.  Enroll afresh on every process start.  Within that process, reuse the
in-memory operational identity for hub reconnections without forwarding or
reusing the enrollment credential on the hub connection.

## TLS and Hub Connection

- Resolve the enrolled hub's vendor-owned FQDN using the operating
  environment's normal resolver.
- Validate the server certificate chain and the exact DNS name.  Use the
  platform CA store, extended for this hub by any CA certificates received
  through the authenticated enrollment response.  DNS resolution selects an
  address; it does not authenticate the hub.
- Authenticate the agent to the hub with mTLS using the operational client
  certificate issued by enrollment and the corresponding locally held private
  key.
- Use the repository's `Ztls`, `ZiMultiplex`, `ZiIOBuf`, and related facilities
  rather than introducing an independent networking or buffering stack.
- Disable TLS early data for subscription and control messages.
- Reconnect indefinitely using bounded exponential backoff with jitter.
- Make connection establishment, loss, and enrollment failures observable
  through logs.

The live mTLS channel supplies confidentiality, integrity, mutual endpoint
authentication, ordering, and transport replay protection.  Do not add a
per-message signature or application nonce unless a later design introduces
queued/offline delivery or an intermediary outside the trusted TLS boundary.
Retain protocol sequence numbers for correlation and completion, translating
between the hub and each local application as specified below.

## Subscription Routing

The hub sends ordinary `Ztc::fbs::Request` messages.  Its optional `id` member
selects an exact destination `Ztc::App`; a missing, null, or empty `id` selects
all local applications.  `ztcagent` forwards each request to every local
application selected by the request's `id`, substituting a separately allocated
application request sequence number on each local leg.  The `filter` selects
telemetry within the requested group; it is not the application-routing
mechanism.

The first hub request is expected to be the one-shot snapshot:

```text
group: App
```

With no `id`, the agent must fan this request out to all currently discovered
applications; no wildcard `filter` is needed.  It forwards their
`AppTelemetry`, acknowledgements, errors, and end-of-snapshot messages to the
hub.  The hub must be able to distinguish each application's response by its
existing application ID.

### Filter DSL

The filter is the existing `Ztc` telemetry-key matcher, not an application
routing expression.  A missing FlatBuffers member, a null string, an empty
string, and `*` all mean wildcard.  This is existing publisher behavior:
`ztcagent` forwards the filter representation unchanged and does not normalize,
compile, canonicalize, or otherwise interpret it.

A textual component is one of:

- `*` or empty: match all values;
- `text`: exact match; or
- `text*`: prefix match, only where the group permits prefixes.

Only one terminal `*` is permitted; there are no infix/suffix globs, Boolean
operators, regular expressions, or negation.  Components are separated by
`:`.  Within textual components, bytes that are control/space (`<= 0x20`),
non-ASCII (`>= 0x7f`), `%`, `:`, or `*` must use uppercase percent encoding
such as `%20`, `%25`, `%3A`, and `%2A`.  Percent encoding an ordinary unreserved
byte is invalid.

The group-specific forms are:

- `Heap`, `Hash`, `Mx`, `DB`, and `App`: one textual component supporting all,
  exact, and prefix matching; for example `orders`, `ord*`, or `*`.
- `Thread`: `*` or one exact unsigned decimal thread ID with no leading zeroes.
- `Hub`: `<link-type>:<id>`, where link type is `TCP`, `TLS`, `QUIC`, `H1`,
  `H3`, `WS`, `FIX`, or `*`, and ID is a textual component; for example
  `TLS:market*`.
- `Queue`: `<queue-type>:<owner>:<id>`, where queue type is `Thread`, `IPC`,
  `Rx`, `Tx`, or `*`, and owner and ID are textual components.  The two-part
  shorthand `<queue-type>:<id>` means `<queue-type>:*:<id>`; for example
  `Tx:hub:link*` or `Tx:link*`.
- `Alert`: wildcard only; replay position is expressed by `alert_date` and
  `alert_seq_no`, not by the filter.

Routing requirements:

- Route a non-empty `id` only to the matching application.  Route a missing,
  null, or empty `id` to all applicable local applications.
- Forward the filter unchanged to each selected `Ztc::App`.  The existing
  publisher implementation owns wildcard interpretation, parsing,
  canonicalization, validation, and application to its telemetry.
- Require nonzero hub sequence numbers that create requests or subscriptions to
  increase monotonically within a live hub session.  Maintain the next
  acceptable hub sequence number in memory.  Silently ignore such a request
  whose sequence number is less than that value; after accepting sequence
  number N, make N + 1 the next acceptable value.  Gaps are permitted.
- Allocate a fresh, monotonically increasing nonzero request sequence number
  independently for each selected application instead of passing through the
  hub sequence number.
- Maintain a live, bounded bidirectional mapping from each hub sequence number
  to its per-application request legs, and from each `(application ID,
  application sequence number)` pair back to the hub sequence number.  Use it
  for acknowledgements, telemetry, errors, EOS, cancellation, and cleanup.
- Treat an unsubscribe's hub sequence number as the handle of the request or
  subscription being cancelled, not as a newly allocated sequence number.  If
  its `id` is null, send the unsubscribe to every live application leg in that
  mapping, substituting each leg's application sequence number, and retire all
  those legs.  This reuse of an existing sequence number is exempt from the
  monotonic admission check for newly created work.
- When an application receive ring remains full through its configured writer
  timeout, use the same `ZiRing::kill()` and `gc()` policy before retrying.  A
  publisher that does not drain its receive ring is a stalled reader and may be
  terminated; retire its application and request-leg state once it is dead.
- Forward one `Ack` and `EOS` per selected application, preserving its
  application ID.  Do not generate an aggregate acknowledgement or completion
  message for a fan-out request.
- Fix the selected application set for a one-shot wildcard request when the
  agent receives and accepts it.  Applications discovered afterward do not join
  that request.
- If an exact non-empty `id` does not name a live application, return an
  agent-generated sequenced `Error` carrying that ID and the message
  `app not running`.
- Keep active subscriptions in a bounded registry.  When a new application is
  discovered, apply any active subscription whose `id` selects it.
- When an application disappears, retire its local subscription legs without
  disturbing other applications.
- When the hub connection is lost or replaced, cancel/reset the corresponding
  local subscriptions and discard the session's sequence mappings and expected
  hub sequence number so stale requests cannot continue generating telemetry.

Do not invent a second filter language or silently broaden a request after it
has been accepted.

## Telemetry Forwarding

- Consume `Telemetry`, `Ack`, `EOS`, and `Error` messages produced by local
  applications and associate them with the relevant hub request or active
  subscription.
- Forward telemetry to the hub using the existing FlatBuffers representation
  and length prefix.
- Preserve application IDs.  For every mapped local response, substitute its
  application request sequence number with the corresponding hub sequence
  number before forwarding it.  Perform any in-place FlatBuffers mutation only
  on an outbound-owned frame, never on shared-ring storage.
- Do not forward a sequenced local response for which no live mapping exists;
  it may belong to another local telemetry-ring reader or to obsolete work.
- Do not coalesce, reorder, or discard responses where doing so changes the
  observable `Ztc` protocol semantics.
- Treat `Ack`, `Error`, and `EOS` as guaranteed-delivery control/completion
  messages.  Publishers must write them to the shared telemetry ring with
  blocking `push`, never silently drop them with `tryPush`.  If a full-ring
  write times out, invoke `ZiRing::kill()` and retry after it reclaims space.
  Delivery is guaranteed to readers that remain attached and responsive; a
  reader killed for stalling intentionally forfeits delivery.
- Apply the same slow-reader eviction policy when ordinary telemetry encounters
  a full shared ring before applying any permitted telemetry-drop policy.  Ring
  fullness must not let one reader indefinitely backpressure every publisher
  and other reader.
- Use only the public `ZiRing` backpressure API.  `push()` does not call
  `kill()` automatically: after a timed-out push, distinguish a full ring with
  readers from other write failures using `writeStatus()`, call
  `ZiRing::kill()` for the full-ring case, and retry after it returns.  `ZiRing`
  owns slow-reader identification, termination, `killWait`, garbage collection,
  and every platform-specific detail; `ztcagent` must not reproduce or depend
  on those internals.
- Keep draining the shared ring independently of hub transmission.  Buffer hub
  output only within configured frame/byte bounds.  If a slow or unavailable
  hub exhausts those bounds, close the hub session and cancel its mapped local
  work rather than allowing `ztcagent` to stall on the ring.  The TLS disconnect
  is the explicit indication that outstanding delivery did not complete; the
  hub must reconnect and reissue the required requests.
- Ensure that reconnect cannot accidentally attach telemetry from an obsolete
  subscription generation to a newly issued request with the same sequence
  number.

The agent should log useful operational state, including enrollment and hub
connection failures, without registering as a telemetry application or sending
agent-originated telemetry into the local or remote telemetry streams.

## Ownership and Concurrency

- Keep local-ring receive state, hub receive state, and hub transmit state on
  their owning threads.
- Cross thread boundaries through the established `ZiMultiplex` scheduling and
  queue patterns.
- Move `ZiIOBuf` references between owners; do not capture variable-sized frame
  data by value in scheduler callbacks.
- Bound all cross-thread queues and make overload state visible.
- Keep public methods callable from arbitrary threads as thin dispatchers to
  owner-thread trailing-underscore implementations.
- Drain or cancel pending local and remote work before destroying back-pointers
  or transport state during shutdown.

## Configuration

Give every input exactly one authoritative source; do not implement an override
or precedence hierarchy in which the same setting can come from multiple
places.

Read environment variables once at startup for secrets and values that normally
vary by host or process run.  These include at least:

- the enrollment bearer token (`ZTC_ENROLL_TOKEN`);
- the local telemetry ring name (`ZTC_RING`);
- the PID-file directory (`ZTC_DIR`);
- optional deployment-supplied device/host metadata; and
- any development/test enrollment-URL override.

Do not accept these values from the configuration file.  Integration with a
secrets manager and the mechanism used by a deployment to populate environment
variables are outside the agent's scope.

Use the normal Z Framework configuration file exclusively for values tuned to
the operating environment, including at least:

- frame and queue limits;
- hub reconnect timing;
- `ZiRing` writer timeout, `killWait`, and coredump behavior; and
- `ZiMultiplex` scheduler, Rx, Tx, timer, and worker settings.

Do not expose configuration-file tuning fields as environment-variable or
command-line overrides.  Compiled values supply documented defaults only when
the corresponding configuration-file field is omitted.  Command-line options
may select the configuration file and perform process actions such as help,
version, or configuration validation, but must not duplicate operational
settings.  Read all inputs during startup; live configuration reload and
run-time mutation are out of scope.

The enrollment URL defaults to the hardcoded Kaldron URL above.  Any development
or test environment-variable override must continue to perform normal hostname
and certificate validation.  Production builds must not provide an option that
disables TLS verification.

## Deliverables

- A new `ztcagent` program integrated into the repository's Automake/autoconf
  build.
- Reusable agent implementation separated from the executable's argument and
  configuration loading where useful for testing.
- A platform-neutral implementation that builds wherever its Z Framework
  dependencies are supported, including Linux and Windows.
- Container image packaging.
- Configuration and operational documentation covering initial enrollment,
  TLS connection, and reconnect behavior.
- Unit and integration tests using `ZuTestUtil`/`ZuTest` and existing Z test
  conventions.

## Acceptance Criteria

1. Multiple local `Ztc::App` test processes can start, stop, and restart while
   one agent remains running; the agent's application registry converges to the
   live set.
2. A hub request for `Group::App` with no `id` and no filter returns one
   application snapshot from every live local application.
3. A request with an exact application `id` is delivered only to that
   application, while a request with no `id` reaches every applicable local
   application; each selected application applies the request's telemetry
   filter locally.
4. Periodic subscriptions continue to forward telemetry from all selected
   applications and are applied to a selected application that starts later.
5. Unsubscribe, EOS, application exit, hub disconnect, and agent shutdown clear
   the appropriate local subscription state without leaks or stale delivery.
6. Malformed lengths, invalid FlatBuffers, invalid filters, oversized frames,
   and unexpected message types are rejected without terminating the agent or
   contaminating another request.
7. On every process start the agent enrolls through
   `https://enroll.devices.kaldron.io:443`, keeps the resulting operational
   identity only in memory, and reconnects within that process using mTLS
   without sending its bootstrap credential to the hub.
8. A redirected enrollment response, an untrusted server certificate, a
   hostname mismatch, or a hub that cannot authenticate the client certificate
   is rejected.
9. Process or container restart leaves no agent identity, certificate, private
   key, telemetry, subscription, or pending-enrollment state on disk; the new
   process enrolls again using its current environment.
10. Hub interruption exercises bounded buffering and jittered reconnection;
    memory remains bounded and any telemetry loss is observable.
11. The same implementation and tests build through the Z Framework on Linux
    and Windows without platform-specific code in `ztcagent`.
12. A second `ztcagent` configured for the same local telemetry ring refuses to
    start while the first is live, while an independent non-agent reader can
    attach concurrently.
13. Enrollment uses the specified authenticated JSON exchange, rejects invalid
    or redirected responses, and retries an exact in-memory pending enrollment
    transaction after an indeterminate connection failure within the process
    lifetime.
14. Sequence-routing tests verify monotonically increasing hub sequence
    numbers, silent rejection of stale hub requests, distinct per-application
    sequence allocation, bidirectional mapping for fan-out, response rewriting,
    and cleanup on disconnect.
15. With a deliberately stalled telemetry-ring reader, including a stalled test
    agent, the writer invokes `ZiRing::kill()` after its timeout; after that call
    returns, a responsive reader continues receiving and publishers continue
    writing without silently losing `Ack`, `Error`, or `EOS` intended for it.
16. Configuration tests verify that secrets and per-host/per-run inputs are read
    only from environment variables, tuning is read only from the configuration
    file with compiled defaults for omitted fields, and no setting has competing
    environment, file, or command-line sources.

## Non-Goals

- An inbound Internet-facing telemetry or administration listener.
- A new telemetry schema or filter language when the existing `Ztc`
  FlatBuffers protocol suffices.
- Translating the steady-state telemetry stream to HTTP/JSON.
- Embedding fleet-wide shared credentials in the executable, container image,
  or configuration defaults.
- Direct integration with a secrets manager or deployment credential provider.
- Treating a hostname, IP address, container ID, or caller-supplied device ID as
  authenticated identity.
- Adding per-message cryptography that duplicates a direct authenticated TLS
  channel.
- Hub-side credential revocation and fleet decommissioning.
- Preserving compatibility with legacy `zcmd` telemetry code.

## Open Questions and Ambiguities

Resolved questions remain in the list and are labeled.

### Identity and Enrollment

1. **Resolved:** Permit one live `ztcagent` per local telemetry ring, enforced
  by a held PID-file lock, without excluding independent non-agent readers.
2. **Resolved:** Use the authenticated, bounded HTTP/JSON enrollment exchange
  specified above, including its request, response, redirect, validation, and
  indeterminate-retry requirements.
3. **Resolved:** Read the enrollment bearer token directly from the
  `ZTC_ENROLL_TOKEN` environment variable; no workload-identity provider
  selection or credential-file mechanism is required.
4. **Resolved:** The CSR proves possession of the locally held key; its
  caller-selected identity and authorization claims are ignored.  The
  enrollment service derives authority from the bearer credential and assigns
  the agent and tenant identity.
5. **Resolved:** Secrets supplied to the agent are read from documented
  environment variables.  Secrets-manager integration and secret injection
  are deployment concerns.  The agent-generated operational private key exists
  only in process memory.
### Hub Session and Wire Protocol

6. **Resolved:** Carry the length-prefixed FlatBuffers stream directly over raw
  TLS/TCP, without HTTP, WebSocket, or another application transport.
7. **Resolved:** Authenticate the subsequent hub session with mTLS using the
  client certificate issued by enrollment and its locally held private key.
8. **Resolved:** Use semantic versioning for the telemetry protocol.  Encode
  `ztcver` as `major * 100000 + minor * 1000 + patch` and include it in
  `AppTelemetry`.  Major versions must be equal.  A receiver is compatible when
  its minor version is greater than or equal to the sender's minor version.
  Patch versions may differ because patch increments contain internal bug fixes
  only.  Continue to restrict minor releases to backwards-compatible
  FlatBuffers table evolution.
9. **Resolved:** Use one configurable `maxFrame` limit for complete
  `Ztc::Hdr`-framed messages at both the local `Ztc::App` boundary and the
  remote hub boundary.  Default it to `Ztc::AppCf::DefltMaxFrame`, currently
  `1U << 20` bytes (1 MiB).  Do not introduce a separate remote frame limit.
10. **Resolved:** Use the platform CA store for enrollment and hub server
  authentication.  For the subsequent mTLS hub session, extend that trust for
  the enrolled hub with any additional CA certificates received in the
  authenticated enrollment response.  Continue to enforce certificate DNS-name
  and validity-period checks; do not compensate for an inaccurate host clock by
  weakening TLS verification.

### Discovery and Routing

11. **Resolved:** Each telemetry publisher maintains a receive ring, which is
  the source of truth for application liveness.  The application is dead when
  its ring does not exist or has no active readers; no heartbeat or separate
  process-registry test is required.
12. **Resolved:** Duplicate live application IDs are impossible within the
  system or container because every `Ztc::App` producer holds an ID-keyed PID
  file.  `ztcagent` may rely on that existing uniqueness invariant.
13. **Resolved:** `ztcagent` is not a `Ztc::App`, does not originate telemetry,
  and is never included in wildcard `Group::App` snapshots or subscriptions.
14. **Resolved:** Fix membership for a one-shot wildcard request when the agent
  receives and accepts it.  Applications discovered afterward do not join the
  request.
15. **Resolved:** When an exact, non-empty `id` is not currently live, the agent
  returns a sequenced `Error` for that application ID with the message
  `app not running`.
16. **Resolved:** For a fan-out request, forward one `Ack` and `EOS` per
  application using its application ID.  The agent does not emit an aggregate
  acknowledgement or completion message.
17. **Resolved:** Hub sequence numbers that create work are nonzero and
  monotonically increasing within a live hub session; numbers below the next
  acceptable value are silently ignored.  The agent does not pass them through.
  It allocates a monotonically increasing request sequence number for each
  application leg, maintains live mappings in both directions, and rewrites
  response sequence numbers back to the corresponding hub value.  Disconnect
  cancels the local legs and clears the mappings and hub-session sequence state.
18. **Resolved:** An unsubscribe with a null `id` cancels every application leg
  created for the referenced hub sequence number, including legs added after
  the subscription began.  The agent substitutes each mapped application
  sequence number when forwarding the unsubscribe and then retires the legs.
  The referenced sequence number is an existing subscription handle, so this
  operation is exempt from monotonic admission for newly created work.
19. **Resolved:** A missing `filter`, null `filter`, empty filter string, and
  `*` are synonymous with wildcard under the existing `Ztc` publisher
  behavior.  The agent passes the filter through unchanged; it performs no
  normalization or filter processing.

### Backpressure, Persistence, and Operations

20. **Resolved:** Use the existing `ZiRing` backpressure contract through its
  public API.  `Ack`, `Error`, and `EOS` use blocking `push`; after a timed-out
  full-ring write, the writer calls `ZiRing::kill()` and retries after it
  returns.  Ordinary telemetry also invokes slow-reader eviction before any
  permitted drop.  `ZiRing` owns `killWait`, reader identification,
  termination, reclamation, and all platform-specific behavior.  The policy is
  symmetric: publishers may kill a stalled `ztcagent` on the shared telemetry
  ring, and the agent may kill a publisher stalled on its receive ring.  The
  agent continuously drains the local ring into a bounded, configurable
  outbound queue.  If the hub cannot drain that queue, terminate the hub
  session and its local request legs rather than blocking the ring; the
  disconnect reports incomplete delivery and the hub must reconnect and
  reissue requests.
21. **Resolved:** `ztcagent` does not persist or durably retain telemetry across
  a hub disconnection or process restart.  Individual telemetry publishers may
  persist alerts according to their own configuration; the agent accesses such
  history only through the existing publisher alert request/replay protocol.
22. **Resolved:** `ztcagent` maintains no persistent state and therefore has no
  state-file format, migration, backup, corruption recovery, or clone behavior.
  Enrollment artifacts, identity, certificates, keys, sequence mappings,
  subscriptions, and queues exist only in memory and are discarded at process
  exit.  Its runtime PID-file lock is process coordination, not retained agent
  state.
23. **Resolved:** There is no configuration precedence because each input has
  one source of truth.  Environment variables supply secrets and values that
  normally vary by host or run; the configuration file supplies
  environment-tuned settings; compiled values are defaults for omitted
  configuration-file fields only; and command-line options do not duplicate
  operational settings.  Inputs are fixed for the process lifetime with no live
  reload.
24. **Resolved:** A `ztcagent`-specific platform matrix is out of scope.  The
  agent uses portable Z Framework interfaces and inherits their supported
  platforms and platform-specific behavior; it must not itself be
  platform-aware.
