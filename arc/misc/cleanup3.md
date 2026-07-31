# Zquic Naming and Shutdown Cleanup Plan

## Objective

Make `Zquic` lifecycle and shutdown follow the Z Framework engine model:

- `init()` initializes data members only.
- `start()` starts asynchronous work.
- `stop()` stops asynchronous work and completes asynchronously through
  `ZmEngine`.
- `final()` clears data members before destruction and requires the engine to
  already be stopped.
- link ownership teardown is centralized in `Link::disconnect()` instead of
  being split across `Server::releaseLink_()`, `Server::clearLinks_()`, route
  retirement, and external timer teardown.

This is a breaking API cleanup.  Update dependents rather than adding legacy
wrappers.

## Current Problems

`Zquic::Engine::init_()` currently performs post-constructor setup and also
starts asynchronous work through `warmup_()`.  `final()` blocks by posting an Rx
continuation which posts a Tx continuation; this duplicates lifecycle control
that should be owned by `ZmEngine`.

`Server::init()` initializes and opens endpoint state indirectly through
`Endpoint::init()`, while `listen()` closes/reinitializes the endpoint and opens
UDP.  `Server::final()` calls `close()`, so destruction currently performs
runtime stop work instead of requiring an already stopped object.

server link teardown is split:

- `SrvLink::close_()` marks link runtime closed, optionally calls the link
  app's `disconnected()`, then calls `Server::releaseLink_()`.
- `Server::releaseLink_()` calls `link->shutdown_()`, retires routes, tears
  timers down, then removes the link from `m_links`.
- `Server::clearLinks_()` repeats the same external teardown loop and clears
  `m_routes`.

That division makes ownership hard to audit.  The owner knows the table and
routes, the link knows its timers/runtime state, and neither has a single
authoritative disconnect sequence.

## Target Engine Model

`Zquic::Engine<App>` should derive from `ZmEngine<App>` and expose the
standard engine control API:

```cpp
using ZmEngine<App>::start;
using ZmEngine<App>::stop;
using ZmEngine<App>::state;
```

The `ZmEngine` control thread is the QUIC Rx thread:

```cpp
template <typename L>
bool spawn(L l) {
  if (!mx() || !mx()->running()) return false;
  app()->rxRun(ZuMv(l));
  return true;
}
```

`wake()` should also post `stopped()` to Rx when the multiplexer is live:

```cpp
void wake() {
  if (!mx() || !mx()->running()) return;
  rxRun([this]() { this->stopped(); });
}
```

Add the necessary `friend ZmEngine<App>;` declarations so `ZmEngine` can call
private/protected `start_()`, `stop_()`, `spawn()`, and `wake()` through the
actual application type.

`Engine::init(params)`:

- is only valid while `ZmEngine` state is `Stopped`
- validates parameters
- records `m_mx`, Rx/Tx/async thread IDs, TLS paths, ALPN, limits, and error
  callback
- does not call `warmup_()`
- does not open endpoints
- does not post work

`Engine::start_()`:

- runs on the Rx thread
- performs base asynchronous startup work, currently `warmup_()`
- posts or invokes any required Tx-thread warmup
- calls `started(true)` only after the base startup sequence has completed
- can be reused by `Client` and as the first step of `Server::start_()`

`Engine::stop_()`:

- runs on the Rx thread
- drains any base Tx work that must complete before the engine is stopped
- calls `stopped(true)` asynchronously, never via caller blocking
- remains small; server/client-specific work belongs in `Server::stop_()` or
  client/link teardown

`Engine` should provide the default owner-release callback used by base
`Link` tests and non-owning clients:

```cpp
template <typename Link>
void disconnected(Link *) { }
```

`Server` overrides this for its concrete link type to remove links from
`m_links` or advance `m_stopCount`.

`Engine::final()`:

- requires `state() == ZmEngineState::Stopped`
- clears ALPN arrays, path strings, callback state, thread IDs, and `m_mx`
- does not call `stop()`
- does not post to Rx/Tx
- returns `bool` if following `ZmEngine::lock()` style, or keeps `void` with a
  hard assertion; prefer the existing local API shape only if consistency with
  dependents is more valuable than the return value

## Server Lifecycle

`Server::init(params)` should only validate server-specific TLS requirements
and initialize base engine state.  It must not call `Endpoint::init()` or open
UDP.

Delete `listen()` as the lifecycle API.  Starting the server is `start()`.
Update zhttp and tests from:

```cpp
server.init(params);
server.listen();
...
server.close();
server.final();
```

to:

```cpp
server.init(params);
server.start();
...
server.stop();
server.final();
```

`Server::start_()`:

- runs on Rx as the `ZmEngine` control continuation
- resets stop bookkeeping: `m_stopCount = 0`
- performs base startup/warmup
- initializes the endpoint with `Endpoint::init(mx())`
- opens UDP using `PathMode::ServerUnconnected`, `app()->localIP()`, and
  `app()->localPort()`
- if synchronous endpoint initialization/open fails, calls `started(false)`
- otherwise completion is driven by endpoint callbacks:
  - `endpointReady_()` calls `started(true)` when engine state is
    `Starting` or `StopPending`, then calls `app()->listening()`
  - `endpointFailed_(transient)` calls `started(false)` if startup is still
    pending, otherwise calls `app()->listenFailed(transient)`

`Server::stop_()`:

- runs on Rx
- is idempotent under `ZmEngine`; the implementation should still tolerate a
  second call during `Stopping`/`StartPending`
- calls `Endpoint::disconnect()` up front before link disconnects.  Endpoint
  disconnect is local endpoint I/O teardown and is not part of `m_stopCount`
- sets `m_stopCount = m_links->count_()`
- calls `disconnect()` on each link
- clears `m_links` immediately after dispatching disconnects so server
  ownership is released
- clears `m_routes` when stopping, because route retirement for each link is
  unnecessary once the entire route table is being discarded
- if the links table is empty, calls `stopped(true)` immediately
- otherwise calls `stopped(true)` from the last stopping-path
  `disconnected(Link *)`

Use one stop counter.  Do not add an `m_stopping` flag; the `ZmEngine` state is
the source of truth:

```cpp
unsigned m_stopCount = 0;
bool stopping_() const {
  switch (this->state()) {
    case ZmEngineState::Stopping:
    case ZmEngineState::StartPending:
      return true;
  }
  return false;
}
```

At stop start:

```cpp
Endpoint::disconnect();
m_stopCount = m_links->count_();
auto i = m_links->citer();
while (LinkRef ref = i.val())
  if (ref) ref->disconnect();
m_links->clear();
m_routes.clear();
if (!m_stopCount) stopped(true);
```

`Server::disconnected(Link *link)` becomes the single owner callback for a
server link that has completed its link-local disconnect sequence:

```cpp
void disconnected(Link *link) {
  if (stopping_()) {
    if (!--m_stopCount) stopped(true);
    return;
  }
  m_links->del(link);
}
```

During server stop, `Server::stop_()` has already dispatched every link
disconnect and then cleared `m_links`, so `Server::disconnected()` must not try
to delete the link from the table when `ZmEngine` is in `Stopping` or
`StartPending`.  It only advances the link stop count.  Outside stop,
`m_links->del(link)` remains the normal owner release path.

Route retirement should have happened before this callback when the engine is
running.  During server stop, `m_routes.clear()` makes per-link route retirement
redundant.

Do not reintroduce a `+ 1` endpoint item into `m_stopCount`.  Endpoint
disconnect has its own I/O callback path; server stop link accounting is only
for owned QUIC links.

Delete:

- `Server::close()`
- `Server::close_()`
- `Server::releaseLink_()`
- `Server::clearLinks_()`

Replace their callers with `stop()` or `link->disconnect()` as appropriate.
Current dependent server lifecycle call sites to update include
[ZquicRuntimeTest listen](/home/count0/src/z/zquic/test/ZquicRuntimeTest.cc:294),
[ZquicRuntimeTest close](/home/count0/src/z/zquic/test/ZquicRuntimeTest.cc:422),
[Zhttp3InteropTest listen](/home/count0/src/z/zhttp/test/Zhttp3InteropTest.cc:1104),
and [zhttpd H3 listen](/home/count0/src/z/zhttp/test/zhttpd.cc:1220).

## Client Lifecycle

`Client::init(params)` remains a data-member initialization step only.

`Client::start_()` can use the base engine start path and complete without
opening a UDP endpoint.  Individual `CliLink` instances still initiate outbound
connections.

`Client::stop_()` should complete the base engine stop path.  The client engine
does not own a server-style link table today, so link lifetime remains
application-owned unless a later cleanup introduces a client link registry.

`Client` can rely on the base `Engine::disconnected(Link *)` no-op or provide
an explicit typed no-op:

```cpp
void disconnected(Link *) { }
```

Client link ownership is external, so the engine owner callback is a no-op.

Client public `connect()` and `disconnect()` should require the engine to be
running.  Tests and zhttp clients must call `client.start()` after `init()` and
`client.stop()` before `final()`.

## Link Teardown Vocabulary

Separate protocol close from ownership teardown:

- an internal/protected close-state helper records QUIC close intent.
- `disconnect()` tears down local runtime ownership and prevents further work.

`disconnect()` is the hard lifetime transition.  It should not depend on
successfully sending a CONNECTION_CLOSE frame.  If a graceful close packet is
desired, that work must happen before final disconnect, or through a separate
protocol-close path that eventually calls `disconnect()`.

Rename `Link::shutdown_()` to `Link::disconnect()` and make it the common
teardown primitive.

Do not leave current public `Link::close(uint64_t)` as an inherited public API
on `SrvLink`.  If the close-state mutation is still needed, rename it to an
internal/protected helper (for example an app/transport close-state helper
around `m_appClose`) and update client/server internals and unit tests to call
that helper through intentional test accessors.  Otherwise deleting
`SrvLink::close()` would still leave `s0->close()` binding to the base
`Link::close()`.

Replace `m_up` with `m_disconnecting`:

- `m_up == true` becomes `!m_disconnecting`
- initial state is not disconnecting
- public and posted work checks become `if (link->disconnecting_()) return`
- reset/reconnect paths must explicitly clear `m_disconnecting` before new
  async work is allowed

Suggested helpers:

```cpp
bool disconnecting_() const { return m_disconnecting.load_(); }
bool live_() const { return !disconnecting_(); }
```

Then convert existing `up_()` call sites to one of these forms.  Do not keep
both `m_up` and `m_disconnecting`.

## Link Disconnect Sequence

`Link::disconnect()` must be idempotent.  Use an atomic exchange/CAS so only the
first caller starts teardown:

```cpp
if (m_disconnecting.xch(1)) return;
```

The first phase must prevent more work before any continuation can run:

1. Set `m_disconnecting = true`.
2. Schedule Tx timer disarm as the first Tx continuation.
3. Call `disconnected()`, the Rx-dispatch wrapper for disconnect completion.
4. `disconnected_()` posts an empty Tx continuation holding a `ZmRef<Impl>` so
   the link remains instantiated until older Tx work has drained.

Shape:

```cpp
void disconnect() {
  if (m_disconnecting.xch(1)) return;
  app()->txRun([link = ZmMkRef(impl())]() mutable {
    link->delTimers_();
    link->disconnected();
  });
}
```

`delTimers_()` must use `ZmScheduler::del`, not `cancel`, because this is final
timer teardown.  Include every connection-owned and path timer:

- ACK delay
- loss
- PTO
- close
- key discard
- PMTUD
- path validation

The current `delTimers_()` already deletes the ACK delay, loss, PTO, close, key
discard, PMTUD, and path timers; preserve that complete set when moving final
teardown into `disconnect()`.

`cancelTimers_()` remains live-state cleanup and should continue to use
`cancel()`.  `disconnect()`/final teardown uses `del()`.

Delete `teardownTimers(Fn)` entirely unless implementation proves a concrete
remaining need for it.  In the target design, final timer teardown is not a
generic callback-taking helper; it is the first fixed phase of
`Link::disconnect()`.  The current server callers disappear when
`releaseLink_()` and `clearLinks_()` are deleted.  The current client
`closeEndpoint_()` use should be replaced by a direct transition into the
common `Base::disconnect()` sequence, with the public client disconnect
callback invoked from that sequence instead of being threaded through
`teardownTimers(Fn)`.

Delete `m_timerTeardown` with `teardownTimers(Fn)` unless a new concrete need
appears.  Its current job is to suppress timer scheduling/callbacks during the
generic teardown helper; after this cleanup, `m_disconnecting` is the teardown
liveness gate and `delTimers_()` is called only from the fixed disconnect
sequence.

## Link Disconnected Completion

Link disconnect completion has one owner-release point.  `disconnected()` is
the public/internal wrapper that can be called off Rx; it `rxInvoke`s the
Rx-thread implementation `disconnected_()`.  Callers that are already on the Rx
thread may call `disconnected_()` directly.  This follows the Z convention:
the unsuffixed function is the cross-thread wrapper, and the trailing-underscore
function is the on-shard implementation.

Shape:

```cpp
void disconnected() {
  app()->rxInvoke(impl(), [link = impl()]() {
    link->disconnected_();
    return link;
  });
}
```

`disconnected_()` is the only function that calls
`app()->disconnected(impl())`.  It completes link-local teardown:

- assert Rx thread
- clear or reset runtime state that must not survive ownership release
- reset TLS/runtime crypto as needed
- clear application callbacks only when the link is permanently closed
- retire server routes if this is a server link and the server engine is still
  running
- post the empty Tx drain continuation while holding a `ZmRef<Impl>`
- notify the owner engine with `app()->disconnected(impl())`

`app()->disconnected(impl())` must have exactly one call site in
`disconnected_()`.  No client/server variant, CRTP hook, wrapper, or
caller-side release path should call the owner callback directly.

For server links, route retirement should be conditional:

```cpp
if (app()->ZmEngine<App>::state() == ZmEngineState::Running)
  retireRoutes_(server->routes());
```

In practice, avoid exposing `routes()` broadly.  Prefer a narrow server method:

```cpp
void retireLinkRoutes_(Link *link) {
  if (this->state() == ZmEngineState::Running)
    link->retireRoutes_(m_routes);
}
```

Then `disconnected_()` can call the narrow server route retirement hook before
it calls `app()->disconnected(impl())`.
Server-specific hooks may prepare link-local state for release, but they must
not call `app()->disconnected(impl())` themselves.

Post the empty Tx drain continuation from `disconnected_()` before owner
release, so the continuation is queued while the Rx completion still holds a
link reference:

```cpp
void disconnected_() {
  ZiAssert(rxInvoked_(), "Zquic", (),
    "QUIC link disconnect completion outside Rx thread", return);
  auto ref = ZmMkRef(impl());
  // route retirement, runtime cleanup, optional app link event...
  app()->txRun([link = ZuMv(ref)]() mutable { });
  app()->disconnected(impl());
}
```

Keep these two callback meanings distinct:

- `impl()->disconnected()` is the application link event used by existing link
  implementations.
- `app()->disconnected(impl())` is the engine owner callback used to release
  ownership and advance server stop, and is called only by `disconnected_()`.

Do not accidentally call the application link event for every server stop
unless that semantic change is intended.  Preserve existing notify/no-notify
behavior while moving owner release into the common disconnect completion.

## Server Receive Path Changes

`Server::received_()` currently calls `releaseLink_(link)` when routed packet
processing returns false.  Replace that with:

```cpp
link->disconnect();
```

If the server is not running or is stopping, incoming datagrams should be
dropped without stateless reset generation.  During stop, the endpoint is
closing and the route table is being cleared; emitting resets from half-torn
state is not useful.

`addLink_()` should reject new links unless the engine state is `Running` or
startup has completed far enough to accept traffic.  The conservative rule is
to accept only in `Running`; endpoint readiness should call `started(true)`
before datagrams can create links in normal operation.

`allLinks()` must account for the lifecycle:

- reject or return empty if the engine is not running
- continue to run on Rx
- iterate with `i.val()` as currently required by local hash iteration style
- snapshot link refs into a `ZtLocalArray` before invoking external
  diagnostics/iteration code, so the caller sees a stable set of links without
  keeping the server table live or using a heap `ZtArray` for the common debug
  path

The only current `Zquic::Server::allLinks()` user is
[H3Server::printDiag](/home/count0/src/z/zhttp/test/zhttpd.cc:822), which uses
a heap `ZtArray` solely to snapshot links for diagnostic aggregation.  Move
that snapshot responsibility into `allLinks()` with `ZtLocalArray`.

## Client Link Changes

`CliLink` already has public `disconnect()` overloads and endpoint close logic.
Do not let the new base `Link::disconnect()` silently change the meaning of
client graceful close.

Recommended migration:

1. Rename the current client graceful path internally to `closeCurrent_()` /
   `closeEndpoint_()` as it already mostly is.
2. Keep public `CliLink::disconnect(Fn)` as the user-facing close API for now,
   but make it call `Endpoint::disconnect()` and then enter the common
   `Base::disconnect()` path.
3. If `Base::disconnect()` blocks all Tx sends through `m_disconnecting`, do
   not set it before sending an intended CONNECTION_CLOSE packet.  Protocol
   close work must precede final teardown.
4. Ensure the callback passed to public `disconnect(Fn)` runs after endpoint
   disconnect has been initiated and the common disconnect sequence has at
   least reached Rx `disconnected_()`.

`endpointDatagram_()`, `endpointReady_()`, `endpointFailed_()`,
`endpointDown_()`, and `endpointTxDrained_()` should test
`disconnecting_()` instead of `up_()`.

Reconnect support must explicitly clear `m_disconnecting` in the path that
opens a new endpoint/runtime, before any new endpoint callback can be accepted.

## Server Link Changes

Delete public `SrvLink::close()` unless implementation introduces a distinct
graceful protocol-close API that sends CONNECTION_CLOSE before local teardown.
Current server-link `close()` usage is app/test initiated local shutdown, and
should become `disconnect()`.  Keeping both names for the same local teardown
path would preserve the old owner-release confusion.

Because current `Link::close(uint64_t)` is public in the base, deleting the
`SrvLink` override is not sufficient by itself.  The base close-state helper
must be made non-public or explicitly hidden so `SrvLink` does not retain an
accidental inherited `close()` API.

Current server-link close call sites to update include
[zhttpd H3ServerLink::disconnect](/home/count0/src/z/zhttp/test/zhttpd.cc:748),
and [ZquicRuntimeTest server link close](/home/count0/src/z/zquic/test/ZquicRuntimeTest.cc:636).
Generic `Link::close()` unit tests in `ZquicStreamTest` are base runtime-close
coverage and should not be treated as `SrvLink::close()` dependents unless the
implementation also renames or removes the base API.

If a future graceful protocol close is needed, name it separately from local
teardown and make it eventually enter `disconnect()` after any intended close
packet work has been queued.

Delete `SrvLink::close_()` with `SrvLink::close()`.  It currently has three
concerns:

- mark base runtime closed
- optionally notify `impl()->disconnected()`
- release server ownership

Those concerns should be split without preserving `close_()`:

- runtime close state stays in base/internal helpers used by protocol frame
  handling
- application notification remains controlled by the existing `notify`
  decisions
- owner release is always performed by common `disconnect()` -> Rx-thread
  disconnect completion; only that completion function calls
  `app()->disconnected(impl())`

Remove `m_releaseDeferred` if it becomes redundant.  Its current purpose is to
avoid releasing while still inside receive processing after a peer
CONNECTION_CLOSE/APPLICATION_CLOSE.  A deferred common `disconnect()` should
cover that directly: mark disconnecting, return false from receive, and let the
queued teardown continuation release the link after the current receive stack
has unwound.

Peer `CONNECTION_CLOSE`/`APPLICATION_CLOSE` handling should call the base
runtime-close helper, notify the application link event if the current behavior
requires it, and then enter `disconnect()`.  It should not route through
`SrvLink::close_()`.

## Endpoint Implications

`Endpoint::openUDP()` returns after scheduling/starting UDP setup; readiness is
reported later through `endpointReady_()`.  Therefore `Server::start_()` should
not call `started(true)` immediately after `openUDP()` succeeds.  Startup
success belongs in `endpointReady_()`.

Replace the current three-connection endpoint state with one connection
reference:

```cpp
using CxnRef = ZmRef<Cxn_>;
CxnRef m_cxn;
```

`m_cxn` is mutated only on the Rx thread.  Tx may access the same ref for
sending; do not maintain separate `m_txCxn`, `m_closingCxn`,
`m_txGeneration`, `m_closingGeneration`, or `m_txClosing` state.
Cross-shard sends should snapshot the `CxnRef` and verify it is still the
current `m_cxn` by pointer comparison before sending, matching the stale
notification pattern below; Tx must not own a parallel connection identity.

`Endpoint::disconnect()` is the local endpoint teardown API used by
`Server::stop_()` and `CliLink::disconnect_()`.  It should call
`m_cxn->disconnect()` when `m_cxn` is non-null.  The disconnect notification
then follows the `Ztcp` pattern:

```cpp
void Cxn_::disconnected() override {
  m_endpoint->disconnected_0(this, ZmMkRef(m_endpoint->impl()));
}

template <typename ImplRef>
void disconnected_0(Cxn_ *cxn, ImplRef impl) {
  auto endpoint = this;
  m_mx->rxRun([
    endpoint,
    cxn = ZmMkRef(cxn),
    impl = ZmRef<Impl>{ZuMv(impl)}
  ]() mutable {
    endpoint->disconnected_(cxn.ptr());
    endpoint->m_mx->txRun([
      cxn = ZuMv(cxn), impl = ZuMv(impl)
    ]() mutable { });
  });
}
```

`Endpoint::disconnected_(Cxn_ *cxn)` runs on Rx and filters stale
notifications with the same technique used by `Ztcp`:

```cpp
void disconnected_(Cxn_ *cxn) {
  ZiAssert(endpointRxInvoked_(), "Zquic", (),
    "QUIC endpoint disconnect outside Rx thread", return);
  if (m_cxn == cxn) {
    m_cxn = nullptr;
    m_listening = false;
    m_connected = false;
    impl()->endpointDown_(this);
  }
}
```

This removes the current bridge state where Rx stores an active raw
`m_cxn`, a closing raw `m_closingCxn`, and Tx stores an active `m_txCxn` ref.
The single `CxnRef m_cxn` is the endpoint connection identity; stale
notifications are ignored by pointer comparison.

`Server::final()` should not call endpoint disconnect.  If endpoint state is
still active during `final()`, that is a lifecycle bug: the server was
finalized without a successful `stop()`.

## State and Race Cases

`ZmEngine` owns start/stop interleaving.  If `stop()` is called while starting,
or `start()` is called while stopping, `ZmEngine` records the pending transition
and sequences `stop_()` after `start_()` completes, or `start_()` after
`stop_()` completes.  `Zquic::Engine` and `Server` do not need special pending
state handling beyond observing `ZmEngine::state()` where needed, completing
the current phase with `started(ok)` or `stopped(ok)`, and letting `ZmEngine`
call the next phase.

Each `Server::start_()` still initializes fresh runtime state for that start,
including `m_stopCount`, endpoint state, and route/link tables before opening
UDP.

Endpoint failure while starting:

- call `started(false)`
- do not call `app()->listening()`
- ensure endpoint state is inactive or disconnect has been initiated

Endpoint failure while running:

- call `app()->listenFailed(transient)`
- decide whether this should also call `stop()`; preserve current behavior
  initially unless tests show a leak or zombie running state

Duplicate link disconnect:

- only the first caller schedules timer teardown and owner notification
- later callers return immediately
- if public APIs need per-caller completion callbacks, keep that at the public
  client close layer rather than in base `Link::disconnect()`

Queued work after disconnect:

- all Rx and Tx posted lambdas must check `disconnecting_()`
- timer callbacks must check `disconnecting_()`
- packet send/receive helpers must check `disconnecting_()` at their existing
  `up_()` guard sites

Current `Zquic.hh` guard sites to convert/audit:

- base `Link` Rx->Tx posts:
  [rxApplyMaxStreams_](/home/count0/src/z/zquic/src/Zquic.hh:2988),
  [scheduleStreamWritable_](/home/count0/src/z/zquic/src/Zquic.hh:3194),
  [queueControl_](/home/count0/src/z/zquic/src/Zquic.hh:3352),
  [rxApplyMaxData_](/home/count0/src/z/zquic/src/Zquic.hh:3448),
  [rxApplyMaxStreamData_](/home/count0/src/z/zquic/src/Zquic.hh:3480),
  [initClientPath_](/home/count0/src/z/zquic/src/Zquic.hh:3887),
  [initServerPath_](/home/count0/src/z/zquic/src/Zquic.hh:3897),
  [updatePeerPathMaxUDP_](/home/count0/src/z/zquic/src/Zquic.hh:3927),
  [validatePath_](/home/count0/src/z/zquic/src/Zquic.hh:3933),
  [recordPathRx_](/home/count0/src/z/zquic/src/Zquic.hh:3940),
  [observePathRx_](/home/count0/src/z/zquic/src/Zquic.hh:3951),
  [receivePathResponse_](/home/count0/src/z/zquic/src/Zquic.hh:4017),
  [resetRuntime_](/home/count0/src/z/zquic/src/Zquic.hh:4174),
  [txInstallPeerKeyUpdate_ post](/home/count0/src/z/zquic/src/Zquic.hh:4642),
  and [schedulePeerKeyDiscard_](/home/count0/src/z/zquic/src/Zquic.hh:4650).

- base ACK/loss/recovery posts:
  [postAckSnapshot_](/home/count0/src/z/zquic/src/Zquic.hh:5139),
  [ackSentTx_](/home/count0/src/z/zquic/src/Zquic.hh:5289),
  [detectLossTx_ continuation](/home/count0/src/z/zquic/src/Zquic.hh:5365),
  [schedulePTO](/home/count0/src/z/zquic/src/Zquic.hh:5406),
  [schedulePMTUD](/home/count0/src/z/zquic/src/Zquic.hh:5416),
  [keyDiscardExpired_](/home/count0/src/z/zquic/src/Zquic.hh:5612),
  [discardPktSpace_](/home/count0/src/z/zquic/src/Zquic.hh:5663),
  [processAckFrame_](/home/count0/src/z/zquic/src/Zquic.hh:5884),
  [applyAckOfAckTx_](/home/count0/src/z/zquic/src/Zquic.hh:5929),
  and the batched
  [processAckFrameTx_](/home/count0/src/z/zquic/src/Zquic.hh:5987)
  continuations, including the loss-batch continuation at
  [processAckFrameTx_](/home/count0/src/z/zquic/src/Zquic.hh:6004).

- base stream-open posts:
  [streamQueuedOpen_](/home/count0/src/z/zquic/src/Zquic.hh:6780)
  and [scheduleOpenQueued_](/home/count0/src/z/zquic/src/Zquic.hh:6791).

- timer scheduling and timer callbacks:
  [scheduleCxnTimer_](/home/count0/src/z/zquic/src/Zquic.hh:7173)
  must test `disconnecting_()` before arming, and the persistent timer callback
  inside it must test `disconnecting_()` before calling `cxnTimer_()`.

- `CliLink` posts and endpoint callbacks:
  [send](/home/count0/src/z/zquic/src/Zquic.hh:7503),
  [queueRetransmit_](/home/count0/src/z/zquic/src/Zquic.hh:7566),
  [sendCryptoFlights_](/home/count0/src/z/zquic/src/Zquic.hh:7840),
  [queueTxFlush_](/home/count0/src/z/zquic/src/Zquic.hh:7917),
  [queueTxFlush_ with address](/home/count0/src/z/zquic/src/Zquic.hh:7923),
  [endpointDatagram_](/home/count0/src/z/zquic/src/Zquic.hh:8281),
  [endpointReady_](/home/count0/src/z/zquic/src/Zquic.hh:8288),
  [endpointFailed_](/home/count0/src/z/zquic/src/Zquic.hh:8296),
  [endpointDown_](/home/count0/src/z/zquic/src/Zquic.hh:8303),
  [endpointTxDrained_](/home/count0/src/z/zquic/src/Zquic.hh:8310),
  and [connectFailed_0](/home/count0/src/z/zquic/src/Zquic.hh:8343).

- `SrvLink` posts:
  [send](/home/count0/src/z/zquic/src/Zquic.hh:8393),
  [queueRetransmit_](/home/count0/src/z/zquic/src/Zquic.hh:8456),
  [send HANDSHAKE_DONE post](/home/count0/src/z/zquic/src/Zquic.hh:8632),
  [sendCryptoFlights_](/home/count0/src/z/zquic/src/Zquic.hh:8659),
  [queueTxFlush_](/home/count0/src/z/zquic/src/Zquic.hh:8734),
  and [queueTxFlush_ with address](/home/count0/src/z/zquic/src/Zquic.hh:8740).

Multiplexer not running:

- `spawn()` returns false, so `start()` fails through `started(false)`
- `wake()` cannot post; avoid trying to stop asynchronously after the
  multiplexer has already been stopped
- normal ownership order should stop QUIC engines before stopping the
  multiplexer

## Implementation Order

1. Add `#include <zlib/ZmEngine.hh>` to `Zquic.hh`.

2. Make `Engine<App>` derive from `ZmEngine<App>`, add friends/usings, and add
   `spawn()`/`wake()`.

3. Change base `Engine::init_()` to use `ZmEngine::lock(Stopped, ...)` and
   remove `warmup_()` from initialization.

4. Add base `Engine::start_()`, `Engine::stop_()`, and stopped-only
   `Engine::final()`.

5. Convert `Server::init()` so it no longer initializes/opens the endpoint.

6. Add `Server::start_()` using endpoint open plus endpoint callback-driven
   `started(true/false)`.

7. Add `Server::stop_()` with up-front endpoint disconnect and link disconnect stop
   counting.

8. Rework `Endpoint_` to use a single Rx-owned `CxnRef m_cxn`, add
   `Endpoint::disconnect()`, and delete `m_txCxn`, `m_closingCxn`,
   `m_txGeneration`, `m_closingGeneration`, and `m_txClosing`.

9. Add `Server::disconnected(Link *)` and `retireLinkRoutes_(Link *)`.

10. Replace `Server::releaseLink_()` and `clearLinks_()` callers with
   `link->disconnect()`, then delete those functions.

11. Add common `Link::disconnect()`, `disconnected()`, and `disconnected_()`
    using the 3-phase teardown sequence and `ZmScheduler::del`.

12. Replace `m_up` with `m_disconnecting` and convert all `up_()` checks.

13. Move route retirement and timer teardown out of server owner code into the
    link disconnect path, then delete `teardownTimers(Fn)` unless a concrete
    remaining caller still justifies it.

14. Delete `SrvLink::close()`/`close_()` and remove `m_releaseDeferred` if the
    deferred common `disconnect()` path covers peer close handling.

15. Make current public base `Link::close(uint64_t)` non-public/internal, or
    otherwise hide it from `SrvLink`, so deleting `SrvLink::close()` does not
    leave an accidental inherited public close API.

16. Reconcile `CliLink::disconnect(Fn)` with common `Link::disconnect()`
    without breaking its graceful endpoint-close callback ordering.

17. Update zhttp and zquic tests to call `start()`/`stop()` around engines and
    remove `listen()`/`close()` server lifecycle calls.

18. Delete or rewrite tests that directly exercise removed teardown helpers
    such as `teardownTimers(Fn)`.
    Current target: [ZquicTimerTest](/home/count0/src/z/zquic/test/ZquicTimerTest.cc:74).

## Acceptance Criteria

- `init()` does not post work, warm up threads, initialize endpoints, or open
  sockets.
- `start()` is the only server path that opens UDP.
- `stop()` is the only server path that closes UDP and disconnects all owned
  links.
- `final()` does not stop anything; it only succeeds on a stopped engine.
- `Server::releaseLink_()` and `Server::clearLinks_()` are gone.
- `Link::shutdown_()` is gone.
- `SrvLink` has no accidental public `close()` API inherited from base `Link`;
  close-state mutation is internal/protected or exposed only by deliberate test
  accessors.
- `m_up` is gone; all former liveness checks use `m_disconnecting` or a helper
  derived from it.
- `m_timerTeardown` is gone unless a new concrete non-`teardownTimers()` need
  is identified.
- final timer teardown uses `ZmScheduler::del` for every link timer.
- server route retirement is skipped during server stop because `m_routes` is
  cleared as a whole.
- endpoint state uses a single `CxnRef m_cxn`; endpoint disconnect
  notifications are marshalled through `disconnected_0()` to Rx and stale
  notifications are ignored by comparing `m_cxn == cxn`.
- a server link removes itself from `m_links` only through
  `Server::disconnected(Link *)`.
- stopping a server with zero links still calls `stopped(true)`.
- stopping a server with active links calls `stopped(true)` only after endpoint
  disconnect has been initiated and every link disconnect completion has been
  observed.
- stop/start pending transitions are handled by `ZmEngine` without ad hoc
  semaphores or blocking on I/O threads.
- top-level `make -j8`, `make -C zquic/test test`, and
  `make -C zhttp/test test` pass after the implementation phase.
