# Z telemetry collector

`ztcagent` owns the shared telemetry-ring reader and is intended to run inside
a deployment-owned image. It uses a Zum service identity and sends an OAuth
access token only in the WSS upgrade `Authorization` header. Runtime
credentials and transport state remain in memory.

At startup the agent attaches the shared telemetry ring. Publishers open that
ring with size zero and do not create or resize it.

`Ztc::App` publishers use a private `ZmScheduler`, not a network multiplex.
Their `scheduler` configuration has two isolated destinations by default:
`timerThread: 1` (`timer`) schedules subscriptions and `workerThread: 2`
(`worker`) owns subscription state, serialization and shared-ring publication.
The scheduler accepts `nThreads`, `stackSize`, `priority`, `partition`,
`quantum`, `queueSize`, `ll`, `spin`, and `timeout`; it has no Rx/Tx, socket,
epoll, or receive/send-buffer configuration. Publisher IDs must remain unique
within a shared `ZTC_DIR` registry.

Configuration tuning is loaded from `ztcagent.conf` (or `--config`). Runtime
identity is supplied separately through the environment:

- `ZTC_ISSUER` is the exact application-scoped Zum issuer.
- `ZTC_CLIENT_ID` identifies the device service client.
- `ZTC_CREDENTIAL_STORE` identifies the deployment's secure credential store.
- `ZTC_WSS_URL` is the validated `wss` endpoint for `ztchub`.
- `ZTC_ACCESS_TOKEN` is the short-lived bearer currently supplied by the
  deployment's credential-store adapter. It is required by the current
  transport bootstrap; the adapter is responsible for obtaining and renewing
  it with `client_credentials` through the discovered token endpoint.
- `ZTC_DIR` selects the relative publisher PID registry beneath the system
  temporary directory and defaults to `ztc`.
- `ZTC_RING` selects the shared ring and defaults to `ztc`.
- `ZTC_DEVICE_ID` is the mandatory device/principal/service-account ID. It
  equals the verified service token subject; device, principal, and agent
  service account are 1:1.

The deployment adapter discovers the token endpoint from the exact issuer
metadata and uses Zum’s existing `client_credentials` grant. It supplies the
resulting bearer to the agent. Devices use confidential client credentials;
passkeys and refresh tokens belong to the interactive-client flow. The current transport validates the issuer, client, device, and WSS
inputs and carries that bearer only in the upgrade header. It retains no
enrollment state.

Interactive front ends establish browser sessions by POSTing their bearer over
TLS to the configured browser-session path (served by the listener's HTTPS
service port). `ztchub` returns a host-only `__Host-ztc_session` cookie with
`Secure`, `HttpOnly`, and `SameSite=Strict`; a subsequent browser WSS upgrade
uses that cookie and does not put an access token in the WebSocket request.

The telemetry-to-network handoff has a configured frame limit. The agent
copies a ring record before handing it to the WSS transmit owner, so a slow hub
does not retain a shared-ring record while waiting on the network. The pending
telemetry handoff is bounded by `telFrames` and `telBytes`; when that bound is
exhausted the agent closes its connection so it cannot silently lose stream
data. Publisher control frames use the ring's normal stalled-reader eviction
policy.

The deployment must provide shared-memory access, runtime libraries, CA trust,
secure credential storage, log collection, secret rotation, and watchdog
policy. Logs must never contain tokens or credential material. Shutdown
prevents new ingress, stops the WSS client, detaches the reader, and releases
transport resources. Container layout, watchdogs, cluster topology, and
secret-manager integration remain deployment concerns.

## `ztchub`

`ztchub` is the live resource server for the same Zum service application. It
publishes the `Request` and `Telemetry` actions and the `Client` and `Agent`
roles; each role name is its corresponding role-derived resource scope. A
`Client` token from interactive user authentication may open the front-end
protocol; an `Agent` token from `client_credentials` may open the agent
protocol. The hub checks both the action and the verified authentication
context supplied by `Zum::Service`. Missing authentication context fails
closed. Role assignment and token issuance remain Zum operations.

Agents and front ends use the same unversioned `ztc` WSS subprotocol and
[`Ztc.fbs.Msg`](src/fbs/ztc_msg.fbs) schema. Each binary WebSocket message
contains exactly one verified root. `Request.subscribe` selects subscribe or
unsubscribe; root metadata carries the session-scoped subscription ID, device
ID, and agent connection generation. The hub derives source attribution from
the authenticated principal and restores the front-end subscription ID on
`Ack`, `Telemetry`, `EOS`, and `Error` responses. Unsubscribe is idempotent. A device reconnect never inherits old
subscriptions, and a slow subscription receives one bounded Overflow error
while other subscriptions continue.

Native clients use the bearer directly in the WSS upgrade. Browsers first
POST the bearer over TLS to the configured session path and then use the
returned host-only, Secure, HttpOnly, SameSite cookie for WSS. Refresh-family
revocation arrives through Zum's signed SSF callback; the hub has no polling
or custom recovery stream. [`ztchub_client`](example/ztchub_client.cc) is a
native OAuth example: it discovers Zum endpoints, uses authorization code with
PKCE and a system browser, subscribes, rotates its refresh token, subscribes
again, and revokes on exit. It shares the native OAuth example implementation
with `zumping` and does not link `libZum`.

The Zum application owns one immutable `audience` string. Client access grants
select roles; consent is keyed by user, client, and application. There are no
separate audience IDs, scope catalogs, or permission objects. Management uses
its own confidential client, core issuer, and `zum.catalog` protocol scope.

Routing and bounded per-subscription queues belong to the multiplex Rx thread.
Each connection hands one pooled frame at a time to Tx and resumes on its
transport fence. Subscriptions share the connection fairly, with reserved
terminal-control capacity. Token expiry closes the connection; unused browser
session cookies expire automatically. Refresh-family SETs affect renewal only.

The default sizing is 2048 agents, 256 publishers per agent, 32 front ends and
32 subscriptions per front end. Each subscription has 1 MiB each of control
and telemetry capacity under a configured 4 GiB queue budget. Limits and
scheduler-turn work are tunable in the native configuration.

Build with `make -C ztc -j2`. Run `make -C ztc -j2 test`; set `ZDB_MODULE` to
an available persistent store module to include the Zum/WSS process fixture.
The fixture supplies a fresh `ZDB_CONNECT`. Unit and integration tests use
source-tree executable wrappers; use `libtool exec` with diagnostic tools.
Set `ZTC_VALGRIND=1` to run both hub lifetimes in the process fixture under
Memcheck. The fixture checks shutdown exit status and retains diagnostics on
failure.

Run the separate network workload with `ZTC_LOAD=1` and `ZDB_MODULE` set:
`make -C ztc/itest -j2 test`. It provisions 2048 independent agent accounts,
opens 32 clients with 32 subscriptions each, and sends 256 synthetic publisher
records per subscription per second for three rounds. It checks source IDs,
generations, ordering, exact delivery counts, and the maximum 200 ms scheduled
emission-to-receipt latency. `ZTC_LOAD_AGENTS`, `ZTC_LOAD_CLIENTS`,
`ZTC_LOAD_SUBS`, `ZTC_LOAD_PUBLISHERS`, and `ZTC_LOAD_ROUNDS` select smaller
diagnostic runs; those do not replace the declared workload gate.

### FlatBuffers boundary

Payload serialization uses `ZfbStruct`. `ZtcMsg.hh` contains the two generated
union-composition calls for already serialized body offsets, which do not have
a built-in `ZfbStruct` field representation. In-place sequence mutation remains
at the agent routing boundary to avoid rebuilding telemetry. These are the
low-level exceptions; ordinary payload loading and saving use metadata.
