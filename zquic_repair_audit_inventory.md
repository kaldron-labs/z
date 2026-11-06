# zquic local source re-audit — 2026-11-06

This section supersedes the historical inventory below for the current worktree.
Status: source repairs applied; broader policy debt remains as recorded below.
The user subsequently authorized phase 3 resumption after all subagents finished.
No configuration, compilation, compiler-driven checks, tests, or images were run
during this source audit. Resumed build/test outcomes are recorded separately in
`zquic_status.md`; source inspection alone is not runtime validation.

Final validation update: native Clang debug suites and 40 runtime repetitions
pass. QIR core is green with the explicitly accepted third-party unsupported
cells; all 16 supported loss/long-RTT cells pass. Supplemental rebinding failures
are verified baseline debt, as detailed below. This completes plan execution,
not a clean audit or a claim that all baseline defects have been repaired.

## Snapshot, guidance, and scope

- Worktree `/home/ubuntu/src/z.zquic`, branch `zquic_repair`, HEAD
  `c63e7462c243e3f27f6f51d6b4ac33561917a92d`, plus the uncommitted changes.
- The user's preceding deletion of `testOwnedStreamAPI` and its four calls is
  retained. This deliberately removes that ownership-API regression coverage.
- Imported `AGENTS.md` and all eleven top-level `docs/*.md` from local `main`
  (`96afee36256c480e1285d598792750ee4eaae082`). All twelve files were compared
  byte-for-byte with their Git blobs. No unrelated main code was imported.
- Guidance: imported `docs/AUDIT.md`, `docs/GUIDELINES.md`, `docs/CPP.md`,
  `docs/STYLE.md`, `docs/IO.md`, and local `zquic/GUIDELINES.md`.
  `docs/AUDIT.md` was initially absent; the matching source-checkout copy was
  read before importing the requested guidance from local main.
- Scope: all 58 repository-owned C++ files under `zquic`: 33 in `src`, 23 in
  `test`, two in `example`. `util`, `itest`, `bench`, and `interop` do not exist.
  File inventory and searches covered every file; selected owner/dependency
  bodies and the integrated recovery delta were read directly. This is a
  focused source audit, not exhaustive line-by-line certification.
- External declarations/callers checked in local `zi`, `zm`, `zu`, `zhttp`,
  and `zws` for the changed recovery APIs, buffers, queues, and fixture imports.

## Repairs in this pass

| Requirement / violated invariant | Owner and evidence | Repair |
| --- | --- | --- |
| Audit reports must describe this base | Historical API prerequisite table below; local `Zquic_.hh`, `ZquicPQueue.hh`, client/server send declarations | Superseded stale prerequisites. No current C++ references to `RxRange`, `ZiMxBackend`, `TxDataDone`, `storageMemory_`, or `releaseView_` remain in zquic. Local endpoint uses `ZiIOContext` and its existing `complete()` contract; local buffer views do not declare the reported overrides. No migration or shim added. |
| Concrete allocated examples need named fixed-size heaps | `example/zquicclient.cc`: `new App::Link`; `example/zquicserver.cc`: `accepted()` directly allocates AppLink. Runtime-created streams are allocated as full `Streams::Node` objects on the existing named `Zquic.Stream.ObjectHash` heap. | Added first-base concrete `ZmNHeap` specializations and ADL heap IDs to both directly allocated example link types, with direct ZmHeap.hh includes. Corrected earlier stream false positives: contained streams must not add a competing allocator to the already named node wrapper. Buffer pool aliases unchanged. |
| Concrete test allocations need named heaps | Actual allocation sites and node wrappers across all local zquic tests, including API, runtime, loop, stream, timer and flow fixtures | Added own first-base ZmNHeap and distinct ADL IDs to eight directly allocated link types: API TestLink/ClientShapeLink/ServerShapeLink, runtime client/server links, loop TestLink, stream TestLink and TimerLink. Direct includes use local ZmHeap.hh. Two raw FlowStream allocations now use FlowLink::Streams::Node on the existing named full-node heap. Framework-created streams, TLS peers/buffers, packet/crypto buffers and queue nodes already have named outer allocators; intentionally empty inner IDs are not missing heap names. No remaining demonstrated unnamed concrete test allocations found by source tracing. No compilation or tests run. |
| UDP completion must not broadcast work to unrelated links | Queued send completion formerly crossed to server Rx, traversed every live link, then posted a flush per link; the UDP queue has no producer capacity-admission gate to resume | Removed the endpoint/client/server tx-drain notification chain. UDP completion retains existing dequeue/schedule and error continuation. Shared Link::flushCtlStreamsBatch_ preserves bounded packetization and posts only the same link after a productive full batch with scheduled streams remaining; packet progress guards prevent flow-blocked true results from reposting. Added SockTest FIFO/error/no-producer-wakeup regression source and StreamTest full/partial/blocked batch continuation regression source. No compiler or runtime result claimed. |
| Parameter-independent declarations must not be repeated per template specialization; exposed data precedes behavior | `Zquic_.hh`: `EndpointCxn_::TxData`, its queue, and `Cxn` | Hoisted endpoint Tx data and queue to namespace scope; moved Cxn fields above constructors. Field ordering and initialization preserved. No forwarding API retained. |
| Repeated equivalent switch bodies | `ZquicRecovery.hh`: `PktTxUpdate::ackd()` ECT0/ECT1/CE | Grouped labels over the existing common accounting body; retained NotECT exclusion and the late/non-flight ACK rules. |
| Redundant casts / unused access grants | `ZquicStream.hh`: byte-span payload pointer and two transferred link friendships | Removed the identity cast; removed friendships unused by local link callers, whose `txStream_()` access is public. |
| Anonymous namespaces and implicit capture packs prohibited | `ZquicCrypto.cc`; API, runtime, socket, and logger tests | Removed the crypto anonymous wrapper (all enclosed functions/data already static); replaced every `[&]` capture pack found in zquic with explicit captures. Test notification and wake behavior unchanged. Remaining anonymous wrappers are listed below. |
| New regression entry points and validation claims must be accurate | `zquic/README.md` | Documented `ZquicRttLossTest`; replaced inherited blanket test-pass claims with the current status reference. |

## Recovery integration review

- `NewReno::lostAt()` and `congestionEventAt()` accept distinct packet-send and
  event-processing times. Local loss and ECN runtime callers use `now_()` on
  Tx for the event time. Comparison is inclusive (`sentTime <= boundary`).
- Both packet ACK traversals capture `wasInFlight` before `ack_()` clears it.
  Late ACKs retain frame work, but do not retire lost bytes twice. Non-flight
  packets do not create ordinary retirement/growth credits. PMTUD has separate
  retirement totals and no ordinary growth credit.
- `applyAckUpdateTx_()` establishes ECN response before staging eligible bytes;
  ordinary and PMTUD bytes retire in each bounded batch. `AckGrowth` commits
  after packet-loss work. Its separate epoch invalidates old growth across
  intervening recovery and persistent-congestion responses.
- Continuations retain existing packet-scan cursors. Runtime generation and
  discarded-space guards precede further work. Reset/discard abandon deferred
  growth; they do not defer byte retirement. Existing frame completion,
  stream-reaping, ACK-of-ACK posts, CE-delta consumption, and flush decisions
  remain present.
- `RttEstimator::timeThreshold()` uses max(latest, smoothed), 9/8 and the
  granularity floor with initial fallback; the new test asserts exact
  99.84375 ms and one-nanosecond deadline edges. Registration is present.
- Changed NewReno and PktTxUpdate call sites were searched across zquic and
  downstream zhttp/zws; no old timestamp-free caller was found. Permanent
  source cases cover the epoch/accounting/ordering and continuation rules.
  These observations are source evidence, not executed regression results.

## Remaining unrepaired findings

| Finding / requirement | Current local evidence and implication | Disposition |
| --- | --- | --- |
| Harness completion polling | `test/ZquicRuntimeTest.cc` originally had 37 polling call sites. Thirty-one are now replaced: direct assertions after synchronous server start/stop, fixture-owned semaphore notifications for handshake/accept/stream processing/disconnect/drain expiry, and existing client disconnect continuations. The unused shared `ZquicInteropTest.hh::waitUntil()` and its Zhttp import are removed. | Source repaired; no execution performed. Six narrow polls remain accepted: UDP ready, two intermediate logical-close checks, a combined stale-route/sibling-byte diagnostic, and two NEW_TOKEN counter checks. These are lower-priority harness exceptions, not production liveness failures. |
| External-process readiness and exit poll | `ZquicInteropTest.hh::waitCaddyReady()` invokes curl with retry sleeps; `CaddyProcess::stop()` polls `waitpid(WNOHANG)`. | Accepted unchanged by explicit user instruction, including their existing timeouts, retries, and exit handling. No process lifecycle or Caddy behavior changes made. |
| Occasional and teardown link enumeration | `allLinks_0()` snapshots/traverses links for explicit running iteration, telemetry via Ztc app hub registration/telemetry capture, and HTTP/3 app-link draining during shutdown. | Accepted as-is by explicit user instruction, including allLinks_0 and its synchronous/asynchronous enumeration paths. No batching or API change is requested. This disposition is separate from the ordinary Tx-drain fan-out repair. |
| Owner-retaining callback | `ZquicLink.hh::notifyStreamTxClosed_()` retains a link ref in a queued callback. | Separate unresolved lifetime-policy concern requiring teardown proof before replacement; not a harness polling defect. |
| Broad layout / language-policy debt | Anonymous namespaces remain in both examples and seven test files (API, CID, handshake, loop, packet-protection, runtime, socket). `HubApp::Shard` and `TestClient::TokenRewrite` are named enums; `SentPkt` and `Link::TxPktRefs` expose data after methods; examples have public `m_` data below methods. | Unrepaired. Removing wrappers alone would expose helper symbols without establishing intended linkage; the remaining declaration/fixture cleanup was not performed. These are policy findings, not demonstrated recovery failures. No constexpr ZuSpan/ZuCSpan declaration was found in the local zquic C++ files. |
| Audit/storage review limits and accepted telemetry snapshots | Existing packet/range capacities, scratch fallback, queue intrusions, copies, counters, and teardown paths were searched and selected bodies inspected. Cross-thread telemetry reads are snapshots; `packetsTx` also separately affects PTO flush decisions. | Cross-thread telemetry snapshot reads are accepted as-is by explicit user instruction and exempt from this concern. No telemetry synchronization or atomic change requested. Distinguish the existing protocol use of packetsTx from telemetry-only observation. No measured latency, contention, or allocation result is claimed. |
| Required test coverage unavailable at this base | `zquic/itest` is absent; existing complete-network coverage is in RuntimeTest. The deleted owned-stream API test is absent by user instruction. | Explicit coverage limits. The user authorized native Clang debug validation, which has since built and run; current outcomes are in zquic_status.md. These missing tests are not claimed as executed. |
| Imported documentation describes newer I/O APIs | `docs/IO.md` describes ZiRxContext/ZiTxContext/IOBufRing contracts from main; local endpoint and zi still use the older ZiIOContext APIs. | Guidance imported exactly as requested. Apply sharding/lifetime rules here, but do not infer those newer declarations exist or import unrelated code to satisfy prose. |

## Phase 4 finding: silent truncation after whole-file send admission failure

Unrepaired baseline defect: the HQ endpoint submits a whole file through
SrvLink::send_; that helper (and its client counterpart) ignores tx.failed()
after flushing a finite stream queue, then sends FIN and returns success.
A 10 MiB local transfer without rebinding yields exactly 6,039,616 bytes and
success exit statuses on both baseline and repaired binaries. The QIR ngtcp2
server-direction rebinding failures show the same byte count. Evidence is in
hq-large-send-reproduction/results.json under the persistent artifact root.

The violated invariant is that an accepted completed response must contain the
whole application payload; queue admission failure must not become successful
short-stream EOF. This is separate from the repaired congestion algorithms and
UDP completion wakeups. A narrow future repair should honor stream-write failure
before FIN, signal failure through existing completion/error contracts, and
stream large HQ responses with the existing writable/completion facilities so
bounded admission makes forward progress. Raising the queue limit merely to
fit the test file would hide the error-propagation defect. No admission redesign,
queue-limit change or new production API is included here.

The remaining quiche client-direction rebinding failures reproduce as 60-second
timeouts on the comparable baseline debug image for both rebind-port and
rebind-addr. Their underlying migration mechanism is unresolved; preserve them
as baseline interop debt rather than attributing them to this recovery repair
or labeling them third-party unsupported. The four other client-direction
peer/case combinations for ngtcp2 and quic-go, plus both msquic cases, pass.
Full per-case outcomes and baseline comparison are in zquic_status.md.

## Phase 4 feedback: endpoint DNS resolver lifecycle

Initial debug-image QIR shows successful client transfers followed by exit 139:
ZiResolver_::Main::~Main asserts that its engine must be stopped. The QIR
endpoint process starts the shared resolver when requests use hostnames, but
main previously returned without stopping it. Numeric-IP smokes missed this.
The independent pre-repair endpoint reproduces both new hostname smoke failures:
all six file comparisons and server stops pass, while both client exit-status
checks fail (baseline-hostname-smoke.log). This is baseline endpoint debt that
prevents the required debug QIR core matrix, rather than a recovery regression.

The owning process main now calls existing ZiResolver::stopSync() and final()
after run(role), retaining its status. These are main-thread lifecycle calls,
matching the existing zhttp3interoptest contract; no library API, resolver
implementation, packet hot path, lock or polling is added. Existing HQ/H3
multi-file smokes now use localhost for all requests and require clean client
exit while retaining file comparisons. Numeric-IP single-file coverage remains.
Source reviewed against lifecycle/ownership and narrow-repair guidance before
rebuilding. Phase 3 revalidation and a fresh image are required before new QIR
results can supersede the initial failing image.

## Phase 3 feedback: runtime-check diagnostics

Statements of no execution in the source-audit rows above describe their
source-only handoff. Subsequent validation is recorded in zquic_status.md.

Phase 3 also reproduced a production qlog lifecycle race: the final trace-close
record could trigger writer teardown/unmapping while its producer was still in
ZmRing::push2 bookkeeping after publication. The native core proves the stopped
logger and unmapped control storage on that producer stack. ZquicLogger::close_
now holds its existing lifecycle guard through close-record admission, so
closeTrace_/stop_ cannot retire the ring before that call finishes. This affects
the close control path only; packet logging has no new lock. The existing
restart/drain regression now covers sibling close, final close→stopIdle and
reinitialization. Reviewed the ring-overflow path: log__ queues a failed tryPush
rather than waiting on the writer while holding this guard. Current build and
runtime evidence is recorded separately; the H2 conformance exception is
documented in zquic_defect.md and accepted unchanged for this repair.

The first resumed compile reported unused-expression warnings in the new
SockTest and RTT deadline helpers. Local ZuCheckRT treats its variadic arguments
as failure actions, not diagnostic strings. Wrapped all twelve bare strings in
ZuTestUtil::log_ calls, preserving runtime-scoped check semantics and adding the
intended failure output. The isolated baseline RTT source is kept byte-identical.
Rebuild/test outcomes belong in zquic_status.md; no warning-free result is claimed
from the initial compile containing these warnings.

The first test run also exposed two missing ZuTestCall registrations around
nested runtime helper scopes (SockTest exercise and RTT deadline); those calls
now register their child scopes before entry. API continuation checks previously
read the end-of-ACK cached congestion diagnostic during an intermediate batch;
the fixture recoveryDiag helper now refreshes its snapshot from the current
controller before checking it. No production telemetry synchronization or
retirement behavior was changed. Rebuild and rerun results remain in the status.

## Phase 3 feedback: qlog close/stop race

Native RuntimeTest repetition reproduced SIGSEGV after an initially passing run.
The captured core/backtrace is recorded under the phase 3 artifact directory.
ZquicLogger::close_ published the final close record; the writer consumed it,
closed the trace and idle-stopped the logger before the producer finished
ZmRing::push2 bookkeeping. Core state had configured=0, Stopped and an already
closed trace while that producer accessed the unmapped ring control memory.

The source repair holds the existing lifecycle m_lock only across close-record
configured/enabled checks and admission/publication/accounting. stop_ and
closeTrace_ already use this lock, so teardown cannot overtake that control
producer. Immediate callbacks and fallback close release the lock first.
Packet/event hot-path admission is unchanged. The existing restart/drain
regression now covers sibling close, final close→stopIdle, restart and exactly
once delivery after restart. This is a demonstrated production lifecycle repair,
not a speculative semaphore change or added synchronization of telemetry reads.
Rebuild and rerun results are pending in the status report.

## UDP completion repair — source only

Queued UDP completion already selects and schedules its next datagram. The
removed producer notification had no matching capacity-blocked producer
contract: EndpointTxQueueLimit records a back-pressure diagnostic but does not
reject or defer admission. Triggering all links on queue progress could create
O(live links) posts and packetization attempts per queued completion.

The repair removes those notifications and dead endpoint/client/server drain
hooks. Both client and server now use the shared bounded control/stream flush
helper. A full productive batch with scheduled streams remaining posts one
same-link Tx continuation. A final partial batch or flow/congestion-blocked
batch posts none; the existing ACK, credit, application-write, recovery and
protocol-timer wakeups remain. PMTUD and pending-ACK handling remain in the
client/server wrappers. No global scan or replacement producer registry added.

Permanent regression sources use the real endpoint connection queue and I/O
callback with synthetic completions, and the existing stream fixture/controller
with sufficient ACK-grown credit. Cases cover FIFO and full drain, send-error
continuation, no producer notification, full-batch same-link continuation,
partial drain, and no rescheduling spin under flow/congestion blocking. They
are registered in the existing SockTest and StreamTest binaries. No tests run.
The endpoint's diagnostic-only queue threshold remains unchanged; enforcing a
hard queue admission limit is outside this repair. allLinks_0 remains unchanged
and accepted as-is, separately from the removed ordinary-traffic broadcast.

## Harness completion repair — source only

The fixture now owns semaphore storage for its entire lifetime. Callbacks post
notifications without capturing stack wait state, so timed-out waits cannot
leave dangling stack captures. `TestEvent::wait()` rechecks completion state
only after a producer notification (or deadline expiry); it does not schedule
predicate polls. Notifications may arrive before a wait starts and remain
observable through durable counters.

Client connection attempts snapshot success/failure/disconnect counter targets
before dispatch, and a reconnect waits for any preceding disconnect continuation
to finish. Connected, failed-connect, and early-disconnect callbacks wake the
handshake waiter. Server accepted-link publication signals a fixture-owned
semaphore; `waitEstablished()` shares an absolute deadline across client,
accepted-link, and server handshake completion. Server link objects are distinct
for each accepted connection.

STREAM discovery and first payload processing signal separately; stream waits
require both counts. Explicit client close uses the existing disconnect
continuation and retains the link until that callback finishes, which keeps its
semaphore alive after a harness timeout. Multi-client close starts every close
before waiting under one absolute deadline. Idle close and forced server drain
expiry wait for their fixture disconnect callbacks. Existing fixture teardown
and scheduler barriers remain; barriers are not treated as proof of future
network input. No production API was expanded for test notifications.

## Other category dispositions

- Allocation/storage and intrusion: role-specific pooled I/O buffers and named
  queue nodes remain. Rx STREAM slices retain their original packet. No
  container of `ZmRef` was introduced, nor any extra payload allocation/copy.
- Data movement: ordinary packet unprotection decrypts in place; the existing
  PreserveCiphertext branch uses scratch intentionally. Bounded crypto/wire
  serialization copies were not removed. Borrowed public sends copy to acquire
  lifetime before asynchronous dispatch; no hidden new fallback was added.
- Framework/casts: syscall and picotls representation casts remain where the
  receiving C API requires them. Example ZiLOG error lambdas capture values.
- Cleanup: OpenSSL EVP destruction and logger singleton lifetime inspected.
  The test close continuation retains its link until it signals completion so
  fixture semaphore storage survives a timed-out main-thread wait; no production
  owning callback or cleanup path was added.
- Qlog: recovery capture expressions remain inside ZquicLOG, whose release
  macro erases both arguments and whose debug path checks enabled first.
  Deferred growth changes when cwnd is committed; pre-commit ACK metric logs
  should be checked against final metrics during phase 3, rather than assuming
  trace fidelity from a source-only pass.
- Liveness: recovery loops retain budgets and continuations; fixed protocol
  parser bounds are not equivalent to unbounded connection-list traversals.
  Remaining cases above prevent claiming all audit flags are repaired.


---

## Historical audit from the original newer checkout

The following is retained as historical evidence only; it does not describe
the current local API surface or current repair dispositions.

# zquic source audit inventory

Status: phase 2 inventory and selected repairs after coordinator notification that phase 1 source integration completed. This supports the coordinator's integrated audit; it does not independently certify phase 2 completion. No configure, regeneration, compilation, compiler-driven analysis, tests, reproductions, or image builds were run.

Scope: all repository-owned `.cc`/`.hh` files in `zquic/src`, `test`, `itest`, and `example`. `util`, `bench`, and `interop` contain no C++ files in the inspected snapshot. The new RTT regression is included when present. Each file was inventoried and its declarations/test registration and audit-sensitive expressions inspected through source searches. Selected owner/dependency bodies were read directly. This is a focused source audit, not an exhaustive proof of every line's correctness; recovery integration remains assigned to the coordinator.

## Demonstrated prerequisite incompatibilities

| Requirement / invariant | Owner and source evidence | Disposition |
| --- | --- | --- |
| Referenced APIs must exist | `Zi::RxRange` uses in `ZquicCrypto.hh` (sent/TxRange), `ZquicCrypto.cc`, `Zquic.hh`, `ZquicCliLink.hh`, `ZquicSrvLink.hh`, `ZquicStream.hh` and tests; no declaration in current `zi/src` | Existing I/O migration prerequisite. Preserve unrelated work; require reconciliation before phase 3 build. Not a newly observed compiler result. |
| Backend selector must exist | `Zquic_.hh` endpoint open and `itest/zquictxqueuetest.cc` refer to `ZiMxBackend::URing`; current `ZiMultiplex.hh` has no selector | Existing migration prerequisite; defer unrelated API design. |
| Tx completion API must match | `Zquic_.hh` send callback uses `io.flags & ZiTxContext::TxDataDone`; `ZiIOContext.hh` Tx type exposes `state`, `complete()`, and no such flags/constant | Existing migration prerequisite; do not silently alter endpoint completion semantics. |
| Virtual overrides must have base members | `ZquicPQueue.hh` `StreamRxData::releaseView_()` and `memory()` declared override; current `ZiIOBuf` supplies neither; its `memory()` body also uses absent `storageMemory_()` and `packet->memory()` | Existing queue/buffer migration prerequisite. Runtime packet-slice ownership must be checked when reconciling, rather than merely deleting override markers. |

## Additional findings and category dispositions

| Category | Evidence / invariant | Disposition |
| --- | --- | --- |
| Exposed layout | `ZquicDiag.hh`: `Diag` counters follow methods; `LinkDiag` rx/tx follow constructors. `ZquicOpenSSL.hh`: `OpenSSLCipherCtx::ctx` follows methods | Repaired in phase 2: exposed fields moved before methods in Diag, LinkDiag, OpenSSLCipherCtx. Member order, initializers, and behavior preserved. |
| Allocation and storage | `ZquicBuf.hh` role aliases use existing Zi allocators with named heaps; `ZquicPQueue.hh` nodes retain packet slices and use framework queues; example links inherit concrete `ZmNHeap` | No demonstrated extra allocation repair from these inspected owners. Buffer allocator migration remains a prerequisite above. |
| Intrusion / lifetime | Stream queue declaration and retained packet field inspected; `StreamRxData` retains original packet while view is queued | Preserve retained backing; source mismatch prevents claiming complete queue correctness. |
| Copies | `ZquicCrypto.cc` decrypt template uses scratch only for `PreserveCiphertext`; ordinary branch decrypts directly in packet. HKDF, header masks and wire serialization copies are bounded protocol operations | No speculative copy removal. `ZquicStream.hh` queueRxSlices creates views, not payload copies. |
| Control / liveness | `ZquicInteropTest.hh::waitUntil` and `ZquicRuntimeTest.cc::waitUntil` poll with sleeps; unused `ZquicAPITest.cc::waitUntil` removed in phase 2 | Demonstrated existing audit-policy violations. Runtime/external-process polling remains an unresolved demonstrated liveness defect and blocks phase 3 under the plan. An event-driven repair spans fixture notifications. Timed semaphore waits are event waits and are not treated as sleep polling. |
| Framework / logging | Example `ZiLOG` error lambdas capture error by value. Qlog writer operates on its dedicated thread; event schema conversions live in logger source | No demonstrated new logging repair; integrated recovery captures still need coordinator review. |
| Casting | `ZquicStream.hh::queueRxSlices_` casts a char-equivalent payload pointer to uint8_t; syscall casts in `ZquicSock.cc` reflect platform interfaces | Payload cast is an audit simplification candidate, not a reproduced failure. Avoid unrelated churn. |
| Diagnostic atomics | `ZquicDiag.hh` rx datagrams/bytes and tx packets/bytes are `ZmAtomic` while other counters use DiagCounter | Requirement for stable cross-thread observation has not been disproved. Unproven concern: trace consumers before any removal. |
| Cleanup | `OpenSSLCipherCtx` releases EVP context in destructor; logger singleton allocation is exempt from ordinary heap tagging; examples use owning app shutdown | No demonstrated new cleanup repair from inspected bodies. |
| Bounds / protocol loops | Packet number/varint loops have protocol widths; frame/transport parsing advances through received input; scheduler packet budget bounds payload selection | No demonstrated need for a new timer, lock, GC scan, or container. Integrated recovery cursor bounds remain coordinator scope. |

## Per-file inspection index

The evidence column records actual source declarations/registration lines selected during this pass. All files also participated in searches for allocation, intrusive containers, copies, casts, logging, polling, atomics, member access, and iteration. A clean search is not a correctness proof.

| File | Inspection evidence |
| --- | --- |
| `zquic/example/zquicclient.cc` | 29: `struct App : public Zquic::Client<App> {`; 64: `friend inline ZuStringT<"Zquic.Example.CliLink"> ZmNHeapID(Link *);` |
| `zquic/example/zquicserver.cc` | 19: `struct AppLink;`; 70: `friend inline ZuStringT<"Zquic.Example.SrvLink"> ZmNHeapID(AppLink *);` |
| `zquic/itest/zquictxqueuetest.cc` | 13: `struct Endpoint : public Zquic_::Endpoint_<Endpoint> {`; 105: `ZuTestCall(testDrain);` |
| `zquic/src/Zquic.cc` | Include/implementation body and audit-expression searches (440 lines) |
| `zquic/src/Zquic.hh` | 81: `struct InitialInfo {`; 1376: `template <typename L>` |
| `zquic/src/ZquicBuf.hh` | 26: `struct BufNode : public ZiIOBuf {`; 78: `struct TxRange {` |
| `zquic/src/ZquicCliLink.hh` | 15: `template <`; 584: `template <typename L>` |
| `zquic/src/ZquicCrypto.cc` | 166: `struct stat st;`; 799: `template <bool PreserveCiphertext>` |
| `zquic/src/ZquicCrypto.hh` | 27: `struct PKey;`; 304: `class ZquicAPI Crypto {` |
| `zquic/src/ZquicDiag.cc` | Include/implementation body and audit-expression searches (83 lines) |
| `zquic/src/ZquicDiag.hh` | 24: `struct DiagCounter {`; 268: `struct LinkDiag {` |
| `zquic/src/ZquicFrame.cc` | Include/implementation body and audit-expression searches (491 lines) |
| `zquic/src/ZquicFrame.hh` | 18: `struct AckRange {`; 73: `struct ZquicAPI FrameCodec {` |
| `zquic/src/ZquicLib.cc` | Include/implementation body and audit-expression searches (29 lines) |
| `zquic/src/ZquicLib.hh` | Include/implementation body and audit-expression searches (42 lines) |
| `zquic/src/ZquicLink.cc` | Include/implementation body and audit-expression searches (13 lines) |
| `zquic/src/ZquicLink.hh` | 26: `struct LinkCloseState {`; 8612: `template <typename EmitTLS, typename HandleControl>` |
| `zquic/src/ZquicLog.cc` | 65: `template <`; 2087: `template <template <typename> class Filter, typename S>` |
| `zquic/src/ZquicLog.hh` | 36: `struct Diag {`; 1186: `template <typename L>` |
| `zquic/src/ZquicOpenSSL.hh` | 19: `struct OpenSSLCipherCtx {` |
| `zquic/src/ZquicPQueue.hh` | 21: `struct CryptoRxData : public ZiIOBuf {`; 562: `template <typename Spans, typename Alloc, typename Enqueue>` |
| `zquic/src/ZquicPacket.cc` | Include/implementation body and audit-expression searches (437 lines) |
| `zquic/src/ZquicPacket.hh` | 35: `class PktWriter {`; 247: `class PktBuild {` |
| `zquic/src/ZquicPath.hh` | 17: `struct PathHint {`; 44: `class Path {` |
| `zquic/src/ZquicRecovery.hh` | 60: `class AckTracker {`; 1958: `class PTOBackoff {` |
| `zquic/src/ZquicSched.hh` | 26: `struct PktBudget {`; 431: `class TxScheduler {` |
| `zquic/src/ZquicSock.cc` | Include/implementation body and audit-expression searches (243 lines) |
| `zquic/src/ZquicSock.hh` | 17: `struct Datagram {`; 109: `struct ZquicAPI Sock {` |
| `zquic/src/ZquicSrvLink.hh` | 15: `template <`; 53: `template <typename L>` |
| `zquic/src/ZquicStream.hh` | 15: `class StreamData : public ZmPolymorph {`; 1037: `template <typename Stream_>` |
| `zquic/src/ZquicStreamUtil.hh` | 15: `struct StreamID {`; 227: `class StreamTxState {` |
| `zquic/src/ZquicTransport.cc` | Include/implementation body and audit-expression searches (267 lines) |
| `zquic/src/ZquicTransport.hh` | 17: `struct ZquicAPI TransportParams {` |
| `zquic/src/ZquicTypes.cc` | Include/implementation body and audit-expression searches (45 lines) |
| `zquic/src/ZquicTypes.hh` | 25: `template <typename T, unsigned N>`; 394: `struct EcnMark {` |
| `zquic/src/Zquic_.hh` | 17: `template <typename Link_>`; 717: `template <typename ImplRef_>` |
| `zquic/test/ZquicAPITest.cc` | 85: `struct TestLink;`; 2198: `ZuTestCall(testInitValidate);` |
| `zquic/test/ZquicBufferTest.cc` | 195: `ZuTestCall(testPacketAllocRoles);`; 197: `ZuTestCall(testPktBuildMetadata);` |
| `zquic/test/ZquicCIDTest.cc` | 18: `struct TestRouteLink { };`; 353: `ZuTestCall(testServerInitialBootstrap);` |
| `zquic/test/ZquicCodecTest.cc` | 162: `struct Case {`; 941: `ZuTestCall(testAddressTokenCodec);` |
| `zquic/test/ZquicCongestionTest.cc` | 269: `ZuTestCall(testSentPktTracker);`; 273: `ZuTestCall(testPTOBackoff);` |
| `zquic/test/ZquicCryptoTest.cc` | 299: `ZuTestCall(testCryptoPosture);`; 305: `ZuTestCall(testCryptoOwnedRanges);` |
| `zquic/test/ZquicFlowTest.cc` | 14: `struct FlowApp : public Zquic::Hub<FlowApp> {`; 343: `ZuTestCall(testPacing);` |
| `zquic/test/ZquicHandshakeTest.cc` | 60: `struct TempDir {`; 480: `ZuTestCall(testZeroRTTPktDrop);` |
| `zquic/test/ZquicInteropTest.hh` | 56: `template <typename L>`; 317: `struct CaddyProcess {` |
| `zquic/test/ZquicLogTest.cc` | 1942: `ZuTestCall(testQLogRestartDrain);`; 1954: `ZuTestCall(testQLogTypedCloseEvents);` |
| `zquic/test/ZquicLoopTest.cc` | 23: `struct App : public Zquic::Hub<App> {`; 590: `ZuTestCall(testDatagramPayloadHeadroom, Zm::CacheLineSize);` |
| `zquic/test/ZquicPMTUDTest.cc` | 214: `ZuTestCall(testPMTUD);`; 215: `ZuTestCall(testPathECN);` |
| `zquic/test/ZquicPQueueTest.cc` | 520: `ZuTestCall(testCryptoRxClipping);`; 535: `ZuTestCall(testTxUnackdRandomized);` |
| `zquic/test/ZquicPacketProtectionTest.cc` | 38: `template <unsigned N>`; 492: `ZuTestCall(testTrafficSecretProt);` |
| `zquic/test/ZquicRecoveryTest.cc` | 959: `ZuTestCall(testRecovery);`; 973: `ZuTestCall(testLosslessFrameUpdates);` |
| `zquic/test/ZquicRttLossTest.cc` | 94: `ZuTestCall(testFallingRTT);`; 95: `ZuTestCall(testOtherRTT);` |
| `zquic/test/ZquicRuntimeTest.cc` | 36: `template <typename Link>`; 1882: `ZuTestCall(testIdleTimeoutQLog);` |
| `zquic/test/ZquicSockTest.cc` | 17: `struct TestEndpoint : public Zquic_::Endpoint_<TestEndpoint> {`; 165: `ZuTestCall(testEndpointRebind);` |
| `zquic/test/ZquicStreamStateTest.cc` | 54: `ZuTestCall(testStreamUtilities);` |
| `zquic/test/ZquicStreamTest.cc` | 40: `struct App : public Zquic::Hub<App> {`; 3841: `ZuTestCall(testPeerKeyUpdateState);` |
| `zquic/test/ZquicTestPorts.hh` | Include/implementation body and audit-expression searches (20 lines) |
| `zquic/test/ZquicTimerTest.cc` | 31: `struct TimerApp : public Zquic::Hub<TimerApp> {`; 246: `ZuTestCall(testTimerDisconnect);` |
| `zquic/test/ZquicVersionTest.cc` | 211: `ZuTestCall(testVersionNegotiation);`; 214: `ZuTestCall(testRetryParamValidate);` |

## Polling caller trace

- `ZquicAPITest.cc::waitUntil` had no local call sites (the only source match was its declaration). Removed the unused template in phase 2.
- `ZquicInteropTest.hh::waitUntil` is imported by `zhttp/test/ZhttpTestUtil.hh:27`; preserve it until dependent fixtures are migrated.
- `ZquicRuntimeTest.cc::waitUntil` has roughly forty uses spanning server listening, accepted links, readiness, spare CIDs, promotion, stream delivery and closure. Replacing it requires notifications at each relevant owning-thread event; posting a generic semaphore without these producer notifications would deadlock or merely disguise polling.
- `ZquicInteropTest.hh::CaddyProcess::stop` polls `waitpid(..., WNOHANG)` with sleeps, and `waitCaddyReady` polls curl. These require external-process readiness/termination handling rather than a protocol recovery change.

These are demonstrated existing policy/liveness issues, not conjectured recovery failures. If left unresolved, record them in the phase 2 exit record instead of claiming all audit flags repaired.


## Phase 2 source-repair handoff

Changed only `ZquicDiag.hh`, `ZquicOpenSSL.hh`, and `ZquicAPITest.cc`: moved exposed members ahead of behavior without changing their relative declaration order or initialization, and removed an uncalled polling helper. Reviewed the resulting source diff. No build or test was run.

Unresolved demonstrated language-policy issue: `ZquicRuntimeTest.cc:30` opens an anonymous namespace, contrary to `docs/CPP.md`. It also contains default reference captures and constexpr span declarations. Removing only the namespace braces does not establish the required internal linkage of its numerous helpers; retain as an explicit policy issue requiring a separate scoped source repair, rather than claiming this audit passed.

The prerequisite API incompatibilities and remaining fixture polling listed above are unresolved. They must appear in the coordinator's exit record; phase 3 cannot be declared ready on this evidence.
