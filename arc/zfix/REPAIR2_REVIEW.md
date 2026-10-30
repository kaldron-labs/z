# Repair 2 source audit

Current review checkpoint: persistent association, reset outcome, Raw external
stop obligations, native cleanup/target authority, cold FIFO and combined
managed fixture changes are reviewed below. Current unit/schema 29/347 and final
integration 13/261 pass, including session TCP58/TLS64, recovery TCP19/TLS18 and
42 demo scenario results. Current consumer/example and process verification
also passes. Runtime artifacts and the independent sections 1–8 completion audit
are in REPAIR2_AUDIT.md. Earlier sections record their own source snapshots.

## Initial managed source-review snapshot

Read-only review covered current production ZfixLink.hh, ZfixTx.hh/.cc,
ZfixLinkTx.hh, ZfixRx.hh, ZfixLinkState.hh/.cc, ZfixWire.hh, and changed
example/zfixd.cc, util/load_native.hh and itest/session_io_test.cc consumers.
No new build, test or layout probe was run.

## Demonstrated repair

load_native.hh prepare() now returns wake(false) at the failed Rx prerequisite
before scheduling Tx or writing its restored history. connected() reports
failure and disconnects on Rx before Tx checkpoint/history clearing. Success
still follows the same Rx -> Tx -> Rx order, carrying the current native Cxn
handle only for the asynchronous cold restore. Failed Tx results still return
to Rx before opening transport; stale socket completions are ignored.

## State and allocation dispositions

- AdmitRequest is borrowed stack state; synchronous admission allocates nothing.
  defer() allocates one concrete ZmNHeap/HeapID AdmitReply, retained through a
  ZmPolymorph-compatible callback. Its owning Link reference is necessary for
  arbitrary-shard completion. Rx-only done suppresses repeat completions;
  m_admissions counts all outstanding replies, unlike the current ticket.
- Existing Rx message, Tx operational, control and wire nodes keep their named
  framework pools. Replacement adds no retired-connection container, lookup
  registry, payload copy, heap buffer or per-message callback object.
- Immutable Rx/Tx config copies occur at owner construction, not connection
  replacement. They preserve the existing independently owned standalone Rx/Tx
  construction contract; this repair adds no per-replacement config copies.
  Sharing immutable configuration would require a separate lifetime/API refactor,
  not a sharding lock, and is not required for displacement.
- Tx pending-load connection is rebound only for logical business work. Epoch
  and operation-ticket checks remain. Operational controls stay connection-bound.
- Wire reopening immediately releases old admissions through finish(false),
  preserving socket-owned backing references; token serials never rewind.
  These posted completions cannot alter current in-flight counts or publish
  work on the successor because LinkTx checks epoch/connection authority.
- Persistent consumer handles reuse the existing application route. The test
  TestState handle is cleared after stop and before hub finalization. Explicit
  cold retirement captures the old handle and validates its acceptance round.
- NativeCxn has one pointer-sized observer token, named concrete ZmNHeap and no
  second connection registry/generation. Physical-close observer posts before
  native cleanup; only logical stopped callback can finish the workload.
- Posted data is handles and fixed metadata; no new cross-shard payload/config
  copy or foreign mutable-owner read was found. The cold Cxn handle is moved
  through continuations and compared only on AppRx.

## Style and include corrections

- ZfixLinkTx reset-condition continuations normalized to hard tabs.
- ZfixRx reset notification/receive condition continuations normalized to tabs.
- Removed unused ZmObject.hh from ZfixRx.hh. ZfixLink.hh already includes
  ZmPolymorph.hh and has no stale ZmObject.hh include.
- Changed files remain <=98 columns (tab stop 8). Existing member columns,
  owner group alignment, types, order and definition spacing were preserved.
- Root subsequently removed the unused ZmObject.hh include from load_native.hh
  before its current utility rebuild.

No additional demonstrated functional blocker was found. This is source
review, not renewed runtime or layout evidence. In particular, no fresh ABI
size/cache-line claim is made for the lazily allocated deferred reply.

This review does not resolve the native transport prerequisites in
[TRANSPORT_REPAIR.md](TRANSPORT_REPAIR.md), or substitute for final integration
and requirement coverage in [REPAIR2_AUDIT.md](REPAIR2_AUDIT.md).


## Authorized native close and handoff changed-line review

The earlier review predates these dependency-header repairs. Subsequent native
regressions demonstrated stale cleanup, missing physical accounting, orphaned
partial output, and a TLS encryption/target race; they are not waived by that
earlier source verdict.

- One Rx-written close-authority token per native link lets each queued owner
  decide whether link-wide cleanup is still valid. It is separate from the
  current handle because ordinary close clears that handle before deferred
  cleanup. It creates no retired collection or extra connection generation.
- Each native disconnect retains its existing Cxn/Impl references through local
  owner posts and settles its physical pending count once. Stale callbacks
  cannot clean successor framing, keys, queue or readiness. Accounting follows
  local reference retirement; successor activation does not wait for it.
- Physical queue handoff is bounded I/O-Tx work posted before successor
  publication. Completing A's context precedes buffer release. It neither
  transmits the remaining bytes nor waits for the old peer/close callback.
- The `ZiTx` setup guard uses its actual context's connection, before touching
  queue state. The bound send overload removes target re-resolution for an
  existing physical attempt. Both preserve the one-buffer built-in callback
  capture and add no per-send heap envelope or queue-owner state.
- TLS's Tx-owned Cxn reference binds detached encryption keys to their socket;
  it is not an Rx-current mirror. It is installed/retired with those keys in
  the existing cache-line-separated Tx group. Rx handshake output snapshots
  its own target; fixed-metadata/reference captures retain it across posts.
  No added generation or per-record payload copy is used.
- TLS install acknowledgement validates both existing generation and captured
  socket identity on Rx. Close flags are cleared at association before an
  early refusal can request abrupt close.
- No replay/history container, new receive behavior, cancellation API, or
  compatibility shim was added. The ZiTx fake connection's direct dependency
  follows the real connection identity required by the new setup guard.

Native TCP/TLS cases and their fault-removal controls are recorded in
TRANSPORT_REPAIR.md. This native-phase review preceded the early FIX association and combined
managed verification recorded below; it alone did not establish those results.

## Association and lifetime source review

The following review covers the association split and Raw/cold-consumer
lifetime changes. It preceded reset-close outcome reconciliation; that later
repair is reviewed in the following section.

- Managed association reuses the existing connection counter and `m_live`.
  Session state describes pre-authentication/Logon readiness; there is no
  second socket-phase flag. Timer serials remain Rx-owned; scheduler timer
  nodes, Rx data and Tx data remain separate cache-line-aligned groups.
- TCP's Tx-owned producer Cxn reference is installed after the protocol bind
  post and before authenticated publication. It binds already-running Tx work
  to its original physical destination; it does not require a foreign-shard
  current-pointer read or a new per-packet reference envelope.
- Raw's aggregate Rx/Tx stop counters represent independent deferred external
  hooks. They cannot be replaced by a singleton current-connection completion
  flag. `RawStopRequest` is stack-borrowed; only `defer()` allocates one named
  pooled reply. Its immutable owner/shard designation and owner-local `done`
  permit duplicate arbitrary-shard replies without locks or a retired registry.
  Completion posts retain a handle through the scheduler's inline ring
  callback storage; no extra `ZmFn` wrapper allocation is introduced there.
- Removed the extra cold-preparation WorkBudget credit, `ColdReturn`,
  `coldBytes()` and byte captures after an owner-ordering proof. If B is accepted
  before A's `retired_` runs, the acceptedCount/round guard skips preparation.
  Otherwise A's Rx→Tx→Rx preparation posts precede B's corresponding bind/stop
  posts, so A's final I/O-Rx publication precedes B's `retired_` call and its
  final Rx return precedes B's terminal NativeState notification. This path
  contains no external asynchronous hook or disk I/O. Holding a queued
  preparation task while later same-owner tasks run would violate FIFO.
  Existing original/candidate references cover lifetime; current-link/round
  checks discard late candidates and the generation check scopes final stop.
  No new queue, flag or barrier replaces the removed credit machinery.
- RequestContext and application reply policy retain logical session/epoch
  identity. Connection metadata is still available to the application; core
  reply dispatch does not impose a connection-stale rejection on accepted
  logical work. Core replay content still comes from the application.

These are source/ownership findings. The cold consumer rerun and remaining
combined managed matrix are recorded in REPAIR2_AUDIT.md; no new layout
measurement is claimed.

## Reset outcome and native error-authority review

- Reset close retains the existing epoch/target/request/drain agreement until
  Rx acknowledges its outcome. `m_resetTarget` is the existing agreement
  sentinel, and `m_preparing` revokes further commit scheduling after close.
  `clearReset()` consolidates repeated clearing; no member or container was
  added. The existing fixed-size `SessionEvent::ticket` carries action-scoped
  request acknowledgement, and admission consumers remain guarded by the
  Admission action. Rx/Tx validate the agreement on their respective shards.
- Physical send errors only notify diagnostics: `ZiConnection::errorSend`
  already closes the failed socket. Deferred validation/crypto policy runs on
  Rx and checks its captured existing Cxn/TLS generation after the application
  callback before requesting close. This covers callbacks that themselves
  replace a connection. Captures are references and fixed metadata; there is
  no per-message error envelope or new generation field.
- Focused reset proof passes 116 scopes. Each native error-policy fault-removal
  control fails its one new scope, and restored candidates pass ten scopes per
  transport. These results establish the focused repairs rather than the
  remaining combined managed matrix.


## Current managed native and terminal-stop review

- `SrvLink::connectReady_()` chains the existing native readiness hook and the
  hub's `running()` state. This prevents authenticated FIX start for a late
  accept during terminal stop without adding a phase flag or physical registry.
  Native lifetime/accounting still completes through its existing close path.
  The negative TCP snapshot failed exactly the three new terminal scopes;
  the final three-mode TCP/TLS positives pass within session58/64.
- The managed chain fixture uses the application's documented A/SIDE/B/C
  acceptance order, selected by a switch. Each durable ID has separate retained
  server link, app history and workload observations; shared hub counters
  describe physical accepts/closes only. No lookup container is introduced.
- Link `test` pointers and scenario/role fields are initialized before owner
  publication and remain stable on Rx/Tx. AppTx owns `OwnOutput` and pending
  load; AppRx owns retained Work/context, start-once and prefix observations.
  Their existing cache-line boundary is retained. Semaphores publish fixed
  snapshots to main; observation counters used concurrently remain atomic.
- The prefix probe calls the ordinary managed `Base::process` and observes its
  retained three-byte incomplete input. It adds only a one-shot fixture guard,
  not a private framing hook, alternate parser or core buffer copy. Final C
  parsing/business and exact Work/physical counts pass with A cleanup held.
- The same application-owned two-business-record history remains the sole
  replay source; SIDE has its own instance. The original deferred Tx ticket
  and RequestContext survive both binds, with exact assigned sequence,
  session/epoch, publication count and credit return assertions.
- reloadBudget held only client Tx but formerly prohibited both peers' stop
  reports. The server is independently allowed to finish first. The repaired
  fixture adds an Rx-owned per-Peer stop counter and samples the client's
  value with its posted reservation; count/byte equality and final two-peer
  stop assertions remain unchanged. No protocol or teardown guard is relaxed.

Evidence: `/tmp/zfix-association-chain-final-summary.json`. These are focused
source/ownership and runtime findings, not a new layout or performance claim.
The cold FIFO proof and four sanitizer-clean lifecycle checks are preserved in
`/tmp/zfix-cold-fifo-audit/`; no invented asynchronous credit remains.
