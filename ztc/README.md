# Ztc: telemetry publication, collection and routing

Ztc exposes framework resource measurements and application health through a
publisher, shared-memory transport, collector agent and authenticated hub.
Publishers observe heaps, hashes, threads, queues, I/O and database resources;
clients subscribe to the attributed telemetry through the hub. Include
`<zlib/ZtcApp.hh>` for publication and link `libZtc` with its dependencies.

## Application publication

Create a `Ztc::App`, fill `Ztc::AppCf` with the application identity and ring,
scheduler, subscription and alert limits, then call `init(config)` and
`start()`, checking both results. Keep its identity unique in the deployment's
publisher registry. `stop()` followed by `final()` completes the lifecycle;
callback-based start/stop overloads are available when the caller must not block.

`rag(state)` updates application health; the callback overload obtains the
current state. `alertSink()` supplies a `ZiLog` sink for application alerts.
Resource managers expose the underlying framework statistics, so ordinary
framework containers/heaps do not need a second application metrics registry.
Configure capacities and retain the publisher for the lifetime of publication.
See [ZtcApp.hh](src/ZtcApp.hh) for the complete configuration and public API.

## Messages, filters and clients

`ZtcMsg.hh` and `ZtcFB.hh` define native message/FlatBuffers integration;
`ZtcFilter.hh` compiles component-key filters for subscriptions. Use the typed
control protocol to subscribe/unsubscribe and process acknowledgements,
telemetry, errors and end-of-stream notifications. Clients must preserve the
source identity attached to telemetry rather than treating local IDs as globally
unique. The [client example](example/README.md) shows authentication, WSS
connection, subscription and credential renewal.

The deployment flow is publisher → shared telemetry ring → `ztcagent` →
`ztchub` → subscribed clients. The publisher owns a scheduler, not a network
multiplexer. The agent/hub own network and identity concerns. Their operational
configuration is described below.

## Collector agent

`ztcagent` owns the shared telemetry-ring reader and is intended to run inside
a deployment-owned image. It uses a Zum service identity and sends an OAuth
access token only in the WSS upgrade `Authorization` header. Runtime
credentials and transport state remain in memory.

`ztchub` starts its multiplexer in main independently of the database role.
Zdb activation starts the configured WebSocket and SSF protocol hubs; Zdb
deactivation stops them. A standby process keeps its runtime running without
opening those listeners. `Hubd::state()` reports engine lifecycle and
`readiness()` reports database activation separately. The `ztchub ready` message
is emitted after listener activation completes. Hubd shutdown joins outstanding
listener transitions before releasing service and routing state; final requires
completed stop. Listener configuration is retained across role changes.
A down transition first joins listener shutdown, then retires old routing and
browser-session state in bounded owner turns. An up arriving during retirement
waits until that state is gone before starting listeners for the new role.

Stop is best effort for these controllers: every shutdown step is attempted.
Completion leaves the engine Stopped even if its boolean result reports an
error; final can then release drained resources. A later start recreates runtime
resources from retained configuration.

`Ztc::Agent` uses `ZmEngine` for its complete runtime. `init` retains stopped
configuration without starting threads. Startup establishes its multiplex,
HTTP/WebSocket clients, telemetry reader and initial publisher inventory;
OAuth authorization and the WSS connection continue asynchronously afterward.
Complete `stop` before `final`. Stop closes provisioning ingress, retires the
telemetry producer, cancels timers, joins transport/request work and drains
routing before retiring the multiplex. `final` does not initiate shutdown or
wait. Restart recreates runtime resources from retained configuration. Blocking
lifecycle forms belong on the main thread; callback forms are asynchronous.

At startup the agent attaches the shared telemetry ring. `Ztc::App` publishers
can create the ring before the agent starts. Their `telRingSize` setting
defaults to 2 MiB and supplies its initial capacity; existing rings retain
their capacity. When a publisher creates the ring, the agent's `telSize`
setting must match that capacity.

`Ztc::App` publishers use a private `ZmScheduler`, not a network multiplex.
Their `scheduler` configuration has two isolated destinations by default:
`timerThread: 1` (`timer`) schedules subscriptions and `workerThread: 2`
(`worker`) owns subscription state, serialization and shared-ring publication.
The scheduler accepts `nThreads`, `stackSize`, `priority`, `partition`,
`quantum`, `queueSize`, `ll`, `spin`, and `timeout`; it has no Rx/Tx, socket,
epoll, or receive/send-buffer configuration. Publisher IDs must remain unique
within a shared `ZTC_DIR` registry.

Publisher lifecycle uses `ZmEngine`. `init` retains configuration while stopped;
it creates no scheduler threads. `start` completes after scheduler startup, ring
attachment, PID ownership, and telemetry registration. It does not promise an
agent connection. The scheduler owns one additional isolated native request-loop
slot beyond configured `nThreads`; timer and worker slot numbers stay unchanged.
This replaces the former separate request thread.

Complete `stop` before `final`. Stop closes alert/RAG admission, cancels timers,
removes watches, drains accepted posts and subscriptions, emits shutdown telemetry,
and retires the scheduler. Restart constructs fresh runtime scheduler resources
from retained configuration. Callback lifecycle forms are asynchronous; blocking
forms belong on the main thread. `final` releases stopped configuration without
initiating or waiting for shutdown. Never-started and rolled-back publishers can
be finalized directly. Retained alert sinks reject input after admission closes.

Configuration tuning is loaded from `ztcagent.conf` (or `--config`). Runtime
identity is supplied separately through the environment:

- `ZTC_ISSUER` is the exact application-scoped Zum issuer.
- `ZTC_CLIENT_ID` identifies the device service client.
- `ZTC_CLIENT_SECRET` provisions or replaces the confidential device-client
  secret. After a successful token exchange, the agent saves it in Vault;
  later starts can omit this variable.
- `ZTC_WSS_URL` is the validated `wss` endpoint for `ztchub`.
- `ZTCAGENT_HOME` overrides the default Vault home (`$HOME/.ztcagent`).
- `ZTC_DIR` selects the relative publisher PID registry beneath the system
  temporary directory and defaults to `ztc`.
- `ZTC_RING` selects the shared ring and defaults to `ztc`.
- `ZTC_DEVICE_ID` is the mandatory device/principal/service-account ID. It
  equals the verified service token subject; device, principal, and agent
  service account are 1:1.

The agent discovers the token endpoint from the exact issuer metadata and
requests a fresh `Agent` scope token with `client_credentials` before each WSS
connection. A failed token exchange retries with reconnect backoff. Access
tokens stay in memory and are never stored in Vault. Devices use confidential
client credentials; passkeys and refresh tokens belong to the interactive
client flow. Supplying a new `ZTC_CLIENT_SECRET` and restarting rotates it
after the first successful token exchange.

Select `vaultStore: "keyring"` in agent configuration, or use `"module"` with
`vaultModule: "PATH"` for a deployment-provided confidential backend. Direct
mode is required; there is no automatic file fallback. `file` and `ephemeral`
require `vaultTestStore: true` and an isolated `ZTCAGENT_HOME` for tests.

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
accepts `ZUM_CLIENT_SECRET` and `ZUM_SSF_AUTH` together on its first
successful start and saves them in `Ztls::Vault`. Later starts may omit both
and load the pair. Supplying both again replaces the stored pair after a
successful start. Its default Vault home is `$HOME/.ztchub`, overridable with
`ZTCHUB_HOME`; secrets do not belong in the configuration file.

`ztchub` also
publishes the `Request` and `Telemetry` actions and the `Client` and `Agent`
roles; each role name is its corresponding role-derived resource scope. A
`Client` token from interactive user authentication may open the front-end
protocol; an `Agent` token from `client_credentials` may open the agent
protocol. The hub checks both the action and the verified authentication
context supplied by `Zum::Service`. Missing authentication context fails
closed. Role assignment and token issuance remain Zum operations.

Agents and front ends use the same unversioned `ztc` WSS subprotocol and
[`Ztc::Msg`](src/ZtcMsg.hh) metadata-defined schema. Each binary WebSocket message
contains exactly one verified envelope with an independently verified nested
payload root. `Request.subscribe` selects subscribe or
unsubscribe; root metadata carries the session-scoped subscription ID, device
ID, and agent connection generation. The hub derives source attribution from
the authenticated principal and restores the front-end subscription ID on
`Ack`, `Telemetry`, `EOS`, and `Error` responses. Unsubscribe is idempotent. A device reconnect never inherits old
subscriptions, and a slow subscription receives one bounded Overflow error
while other subscriptions continue.

EOS completes a snapshot. It does not remove a publisher, clear cached
telemetry or end a continuing subscription. Receivers may use it to apply
snapshot records together or synchronize concurrent updates. The hub preserves
enqueue order within each subscription, including its snapshot records and EOS.
A successful one-shot wildcard snapshot ends with one aggregate EOS after all
publisher snapshots complete; a failed snapshot reports `SnapshotFailed`.
Agent disconnection reports `AgentGone`; confirmed publisher removal publishes
`Shutdown`. Neither event is represented by EOS.

Native clients use the bearer directly in the WSS upgrade. Browsers first
POST the bearer over TLS to the configured session path and then use the
returned host-only, Secure, HttpOnly, SameSite cookie for WSS. Refresh-family
revocation arrives through Zum's signed SSF callback; the hub has no polling
or custom recovery stream. [`ztchub_client`](example/ztchub_client.cc) is a
native OAuth example: it discovers Zum endpoints, uses authorization code with
PKCE and a system browser, subscribes, rotates its refresh token, subscribes
again, and stores the rotated refresh credential in `Ztls::Vault`. It shares the native OAuth example implementation
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

Payload serialization uses `Zfb` from ordinary `ZfStruct` metadata.
`ZtcMsg.hh` composes the body into an independent nested `Payload` root,
then writes its byte vector and routing metadata into `Msg`. Routing copies
this span unchanged and replaces only the outer routing fields; it does not
repeat field omission/default decisions. External envelopes and their nested
payloads are verified at ingress. Trusted process-local handoffs use views.
In-place sequence mutation remains at the agent boundary; alert producers
explicitly emit the mutable sequence slot even when its initial value is zero.

`ztchub` requires `ssfDeliveryURL`, the public HTTPS URL of its SSF listener
(e.g. `https://hub.example:8444/ssf`). It registers that receiver with `zumd`
using its application workload credentials. Its callback URL identifies the
receiver, so separate hub instances register independently. It renews on a timer at half the
granted lease. `ssfLease` requests a lease in seconds (default 300); `zumd`
caps it with `ssf.leaseMax`. Configure only receiver bounds and callback TLS
trust on `zumd`; destinations and callback credentials come from registration.


## Examples and tests

Run `make -C ztc test` in a configured build tree. See
[test/README.md](test/README.md) for focused protocol coverage,
[example/README.md](example/README.md) for the native client, and
[interop/README.md](interop/README.md) for canonical wire checks.
Shared test environment requirements are in [TESTING.md](../TESTING.md).
