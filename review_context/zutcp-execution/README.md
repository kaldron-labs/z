# Zutcp execution evidence

The objective remains the entire `zutcp_plan.md`. The independent Zxdp veth gate
has **passed**, including current release and ZDEBUG ASan/LSan suites, current
Memcheck checks and successful clean full-tree builds with zero warnings/errors.
[independent-gate-passed.json](independent-gate-passed.json) records its scope;
the [gate ledger](zxdp-gate.md) maps its eight requirements to evidence.

Zutcp now exists as the verified 67-file working-source copy and case-preserving
rename. The original Zquic bytes remain unchanged. The verbatim archive, rename
patch, replay verification and renamed archive are separate review checkpoints.
The renamed baseline is registered Linux-only and passes its full top-level ASan
build, 21 unit programs / 205 subtests and the four-subtest endpoint integration
suite with leak detection. `renamed-baseline/results.json` records this checkpoint.
All 67 copied source files still match the pure case-preserving rename.
Phase-1 adaptation and all eight cumulative working-pair slices remain to execute.

The current Clang release build and suites pass: eight unit programs / 33 TAP
subtests, and five privileged programs / 20 subtests / 258 checks. The configured
clean full-tree build was followed by Autotools regeneration and top-level builds
for subsequent changes. `release-current/` retains the logs, all eight unit
Memcheck results, the native Nif Memcheck run, and the directly instrumented
kernel-coexistence probe. Those memory runs report zero errors and lost bytes.

All three wait modes completed 10,000 standalone request/replies with a 1ms
scheduler timer firing during traffic. Finite-wait stress exposed temporary Tx
reserve exhaustion; the utility now retains one pending original request/packet
and retries after CQ progress. The finite-wait server completed with 3,278 such
retries and no pool increase. The pre-fix failure is retained separately.

The diagnostic header and renamed Ownership/RxChain targets are configured and
executed. Seven isolated fatal-Tx probes, queued-RX telemetry, real quantum-one
re-entry, kernel coexistence and fixed registered stream rollover pass in release.
Current debug/ASan/LSan execution remains outstanding. A 10,000-message utility
Memcheck attempt stalled at 24 replies and failed its application deadline with
zero memory errors; its cause is not established, and the complete logs are
retained. The 32-message Memcheck runs pass in all three modes with zero errors
and lost bytes. A later utility edit moves the captured request into Tx and frees
it after reply construction, before submission; it also captures final Rx state
for diagnosis. Those edits now pass all eight unit programs / 33 subtests, all five privileged
programs / 20 subtests / 258 checks and the direct kernel probe under ASan with
leak detection and ZDEBUG. The full-tree build remains active. The matching current-source release suites now pass in the isolated worktree,
as do all eight unit and four native Memcheck programs, the direct kernel probe,
and both packet endpoint processes in each wait mode. The formerly stalled
10,000-message Memcheck case passes four consecutive times under its original
15-second application deadline, with unchanged two-frame pools. Logs are in
`isolated-release-validation/`. The original clean full-tree ASan build completed with zero warnings/errors.
The configure probe is now applied there, configures successfully, and its
follow-up top-level build passes. SHA-256 checks confirm all 21 observed packet
binaries and dependency libraries remain byte-identical to the tested sanitizer
build. The isolated full-tree release build also completed successfully with zero
warnings/errors. That closes the independent veth prerequisite; physical operation
and the full Zutcp deliverables remain separate execution work.

## Recorded baseline

`baseline.json` records the initial revision, compiler/configuration and SHA-256
of every tracked working-source file under zquic, including existing local edits.
`zquic-baseline.txt` records 21 passing programs / 205 TAP subtests. None of those
baseline files was changed by this implementation.

## Implemented and checked so far

* Linux-only Zxdp Autotools traversal and library/test targets, independently of
  any Zutcp source or build target.
* Private SWSR ring views driven by queried UAPI offsets, checked mapping
  bounds/alignment, release/acquire publication, complete vector reservation,
  cancellation and wraparound. Unit tests use synthetic peers; privileged tests
  now register actual UMEM, discover offsets and map all four real kernel rings.
* Checked, single-chunk packet geometry, page-isolated physical regions,
  per-request counts, separate ingress/Tx/control reserves, optional NUMA placement
  with setup failure reporting, and bounded descriptor-capacity calculations.
* Complete-batch mapping/pool activation with the required non-sharded ZmHeap
  payload-overlay slist, null exhaustion and off-thread recycling. No Zi provider
  inheritance or pending allocator list lives in Zxdp.
* Instance provisioning requires mx and enforces its physical ingress count.
  Identical batches preserve pool identity/free lists and held bytes. Changed
  batches fail busy with retained backing and retire old pools after release.
  These are arena tests, not evidence of socket stop/restart or DMA retirement.
* Direct BPF endpoint/XSK map creation, bounded C++ instruction emission with
  live map FD relocation, program loading and verifier diagnostics. Endpoint
  keys select concrete local addresses, protocol and port; claims use NOEXIST,
  and action/removal checks preserve the owning instance token.
* Rtnetlink Ethernet MAC/MTU/queue queries and XDP identity queries. Attachment
  prefers native with no-replace protection; only EOPNOTSUPP permits generic
  fallback. Detach supplies the expected program FD and preserves replacements.
* ZmSingleton-managed shared classifier ownership keyed by retained current-thread
  network namespace identity and interface index. Bindings have exclusive queues,
  intrusive bounded endpoint lists, no-replace claims, range rollback, and stop
  exclusion. Last-member destruction retires only the recorded attachment. Pending
  retirement retains identity and refuses new membership/admission until retry.
  Configuration uses a nonwaiting atomic gate; packet service does not enter it.
* Private XSK registration/binding over the fixed arena, ordinary zero-copy-first
  bind flags, queried capability reporting, partial-open rollback and idle
  re-registration on the same backing. EBUSY retains a prepared socket for
  owner-directed retry rather than rebuilding registrations in a loop.
* Private Tx admission/completion with bounded descriptor associations retaining
  original ZiIOBuf atomic references. Repeated submissions retain independently;
  exact CQ addresses release one hold. Clearing lookup precedes final registered
  return. Consumed TX entries alone restore neither DMA holds nor admission
  credits. Application admission preserves the configured control reservation.
* Private Rx FILL/RX ownership with original IOBuf associations, normalized slot
  IDs, final-return reconciliation before FILL, packet bounds/continuation checks,
  reentrancy guards and mx quantum enforcement including rejected descriptors.
  Public Nif composition and scheduler service are checked separately below.
* Private physical ingress allocation without builtin payload or a Zi plugin
  provider implementation. Original IOBuf metadata retains its allocator via
  the existing allocator pointer, with no additional per-buffer fields/backing
  counter or connection owner. Final registered return and ordinary Zi heap
  growth/free remain valid after external allocator/pool/arena ownership ends.
* Private kernel queue-retirement observation before pending packet release.
  An empty observer over the same arena preserves all holds through setup error
  or retry. Namespace cookies and original socket generations guard the result;
  successful queue reuse permits Tx/Rx to release their own pending holds.
  [Protocol and pinned source basis](kernel-retirement.md) describe the ordering.
* Normalized public Nif/query/open/start/close/final API over those engines.
  Rx callbacks carry original buffers and frame IDs, Tx returns an accepted prefix
  and pumps bounded completion work on mx Tx. Close keeps endpoint exclusion and
  membership; restart preserves arena/pool identity and old frame-return flags.
  Final drains raw callbacks before owner release. Startup cancellation and
  failed-open cleanup use the same owner continuation paths.
* Pool-lifetime eventfd notification on an ingress free-list transition from
  empty to nonempty. It retains no Nif/connection owner. Namespace/attachment
  identity checks also run on restart; quiescence tolerates kernel removal of
  closed sockets from XSK maps.

`verification.json` identifies the verified source hashes, exact configuration,
successful top-level `make -j8` with zero warnings, and test commands/results.
`zxdp-tests.txt` records eight programs / thirty-two TAP subtests. The eight final
`*-memcheck.txt` files record zero errors and zero lost allocations. Framework
allocations still reachable at process exit are reported separately by Memcheck.

The initial ring Memcheck run was terminated because synthetic peers busy-spun
under Valgrind's serialized scheduling. Its output is retained in
`ring-memcheck-aborted.txt`; it is not a passing check. The test now yields when
empty/full, and the final ring Memcheck run passes.

Privileged probes succeeded for AF_XDP socket creation and for veth creation
inside a temporary network namespace. The namespace and its links disappeared
when the probe exited. No existing interface, classifier or network reservation
was changed. These probes establish environment readiness only.

`classifier-kernel.txt` records four actual privileged TAP subtests / fifty
checks: fixed map ABI/capacity, kernel verifier and packet decisions (including
all truncated prefixes), native and generic attachment, occupied-program refusal,
kernel no-replace/expected-FD protection, replacement preservation, and partial
map-creation rollback/retry under an isolated FD limit. Veth selected mode 4
(native); dummy selected mode 2 (generic). All maps are unpinned and fixture
interfaces live only in the anonymous test network namespace. No production
interface was modified. `classifier-unprivileged-skip.txt` proves the ordinary
unprivileged invocation visibly skips this coverage.

The first attachment fixture incorrectly read ifindex from /sys, which still
represented the original network namespace. The failure is retained in
`classifier-fixture-failure.txt`; querying through `ip`/rtnetlink fixed setup.
These kernel tests prove classifier/attachment behavior, not packet exchange.

`registry-kernel.txt` adds actual registry and idle XSK verification: nine TAP
subtests / 104 checks, including two-queue membership, range rollback, foreign
replacement, pending-retirement permission failure/retry, same ifindex in two
namespaces, exact key accounting under contention, real ring mappings and
classifier activation with a registered/bound socket. Rebind retries run on mx
Rx against one prepared socket; held backing remains unchanged. The tested veth
reports zero-copy false. No TX descriptor or FILL frame is published.

`registry-privileged-memcheck.txt` records zero errors and zero lost allocations
for the entire privileged suite, including its child processes. The TEST_RUN
helper captures returned packet bytes and verifies they are unchanged. The
earlier run omitted its optional output buffer and Valgrind reported an invalid
output range; that failed run is retained in `registry-memcheck-before-output.txt`.
Suppression was not used; providing/checking actual output resolved it.

Two earlier implementation failures are retained. `registry-gate-failure.txt`
records the x86 ZmPLock trylock return mismatch encountered by the initial gate;
registry configuration now uses ZmAtomic CAS and leaves that unrelated primitive
unchanged. `xsk-immediate-rebind-failure.txt` records kernel EBUSY after close.
The socket layer now separates prepare/bind and the test retries the same handle
through mx; it does not assume fd closure ends deferred kernel pool retirement.
Outstanding-DMA retirement remains a required Nif implementation/verification
item; these idle tests do not establish that contract.

`tx-unit.txt` records four Tx ownership subtests / 29 checks. They exercise
original-object identity, independent application lifetime, off-owner final
release, repeated descriptor holds, out-of-order completions, association reuse,
control-credit reservation, rejected-tail ownership and malformed completion
addresses. `tx-memcheck.txt` reports zero errors and zero lost allocations.

`tx-wire.txt` records both privileged programs passing ten TAP subtests in total,
followed by the detailed Tx result (one subtest / fourteen checks). An actual veth
peer receives an unchanged Ethernet frame constructed directly in registered
backing; mx Tx kicks the driver and observes the actual CQ address. An application
reference preserves the original object/bytes after CQ until final registered
return. Veth reports zero-copy false. `tx-wire-memcheck.txt` reports zero errors
and zero lost allocations. The experimental EtherType checks one-way Tx only;
no FILL frame is published, no receive callback runs and the required raw UDP
request/reply gate remains open. No cancellation of outstanding DMA is claimed.

`rx-unit.txt` records four Rx subtests / eighteen checks. Synthetic ring peers
verify original object/backing identity, MTU/full and short adjusted views,
application retention, off-owner return, before-publication recycle observation,
bounded malformed-descriptor work, impossible-address isolation and Rx stubs
publishing no FILL frames. `rx-memcheck.txt` reports zero errors and zero lost
allocations. These checks publish synthetic FILL entries, not device DMA. Actual
AF_XDP receive delivery and Nif integration of pending-FILL retirement are checked
separately below.

`rx-wire.txt` records one actual receive subtest / twenty-two checks. A dedicated
single-queue veth pair and external AF_PACKET peer deliver two valid UDP frames
through the owned endpoint classifier and actual XSK. Original registered object
identity, exact bytes, retained-frame exhaustion, off-owner final release,
normalized recycle notification and same-slot reuse are checked. After normal
RX consumes every FILL hold, retained application bytes remain valid across
registration/ring closure. `rx-wire-memcheck.txt` reports zero errors and zero
lost allocations; veth reports zero-copy false and the need-wakeup branch was
not requested in that run. No pending-FILL cancellation is claimed. This now uses
the library's physical allocator, with a fixture observer recording object
identity. Retained bytes survive removal of its external allocator references
after socket closure. The bounded timer is a fixture implementation; public Nif
composition, scheduler waiting and stop evidence are recorded separately below.

`ingress-unit.txt` records four physical-allocator subtests / sixteen checks.
They cover invalid setup/requests, exact pool count and geometry, registered
buffers outliving external allocator/pool/arena and mx lifetime, final release
on another thread, generic Rx heap growth returning registered backing while
preserving the original object, and exclusive transfer into an application Rx
stub whose existing provider routes final return to the physical region.
`ingress-memcheck.txt` records zero errors and zero lost allocations, including
allocator destruction through the final buffer and post-growth heap release.

`kernel-retirement.txt` records ten lifecycle subtests / 122 checks, including
eighteen new checks for pending FILL/Tx retirement. Three FILL frames and two Tx
holds remain live through preparation permission denial and namespace mismatch;
the latter is checked through actual socket namespace cookies. An empty same-arena
observer then retries bind on mx Tx until queue reuse proves old-pool retirement.
Tx drops its associations and posts Rx to drop pending FILL holds. A queue-only Tx
frame returns at retirement, while the application-held original frame stays
valid until its later release. No new packet/service worker is created. The
endpoint discard fence persists until explicit final leave. Veth reports copy
mode. `kernel-retirement-memcheck.txt` records zero errors/lost allocations in the
parent and both child processes. Nif now uses the helper for close/final; physical
NIC DMA validation remains open.

`nif-unit.txt` records four subtests / fifteen checks for the wake resource,
1024 check/wait-boundary notifications, invalid-open control handles and
foreign/Tx/retired pool validation. `nif-memcheck.txt` reports zero errors/lost
allocations. All eight unit Memcheck results were refreshed for the pool wake
and normalized Nif integration.

`nif-pair.txt` records five subtests / seventy-one checks. A real two-Nif AF_XDP
pair exchanges three UDP request/replies using registered Tx backing and original
Rx objects. Checks cover bounded completion pumps, retained Rx across close and
same-layout restart, endpoint reactivation, conflicting membership, old-epoch
return observation, continued transfers and changed-layout busy/retry. All Nif
handles are dropped before final application-buffer return while mx still runs.
A queued start is cancelled with both callbacks completed, retried, and finalized
directly from running state. `nif-pair-memcheck.txt` reports zero errors/lost
allocations. The basic lifecycle fixture drives nonblocking `pollRx_()` with timers.
A packet-only plugin additionally checks the production receive hook in indefinite,
finite-spin/wait and low-latency modes, with no receive-polling timer. Each mode
checks pool-return-only recovery, packet quantum one, timer progress, 512 queued
scheduler notifications and final/whole-mux shutdown. Scheduler notifications
retain one token per queued mux receive callback; pool eventfd reads coalesce
notifications in one bounded read. Tx now applies its own spin/timeout/low-latency
controls to the bounded completion pump. Timeout zero queues outstanding work
without a periodic timer; idle Tx has no pump. Finite waits use scheduler timers,
and CQ descriptors remain bounded at 64 per turn. The native pair exercises the
three configurations, but delayed hardware CQ timing and sustained-load fairness
remain open.

The public pair now also uses both real queues on each two-queue interface. Four
independent instances share the two interface classifiers while keeping their mx,
arena, endpoint tuples and pool ownership separate. The queue-one pair exchanges
32 further request/replies while the first members retire, and continues after
their owners are destroyed. Its packet service stays within quantum one.

Actual Tx pressure uses four descriptor entries over two backing slots. One
normal completion offsets both real cursors, then a four-packet batch crosses
the ring boundary and retains each original object twice. A further submission
is refused while the completion pump is queued behind that Tx owner turn. Actual
CQ consumption releases all four holds and returns only unreferenced backing;
the retained application buffer stays unchanged until its off-owner final return.
The full independent gate has not passed and Zutcp remains uncreated.

`nif-build-before-review-fix.txt` preserves an intermediate compile failure from
a misplaced cancellation guard; it was corrected before the final build/checks.
`nif-wait-build-before-fix.txt` preserves the test adapter's incorrect atomic-load
spelling. `nif-wait-before-fix.txt` and `nif-wait-hang.txt` preserve the first wait
failure; the adapter had lost its initial continuing receive turn after opening
independently. Scheduler wake tokens were also corrected to avoid coalescing
distinct queued mux receive calls. Final native and Memcheck runs include the
corrected startup dispatch and all three modes.
`nif-tx-wait-build-before-fix.txt` retains the diagnosed nanosecond conversion
narrowing error, corrected with an explicit bounded conversion before verification.

`packet-loop.txt` records one standalone integration subtest / eleven checks.
The scoped helper runs independent client/server processes for 32 UDP exchanges
per mode, with full-MTU, odd-length and smaller payloads, IP/UDP checksums and every
Tx completion. Oversized geometry fails before binding, and the caller's interface
identities remain unchanged. `packet-loop-artifacts/` retains each mode's state,
link/address/neighbor snapshots and both endpoint logs. Six endpoint Memcheck runs
across the three modes are retained under `packet-memcheck-*/`, with zero errors
or lost allocations. The helper supports caller-owned physical interfaces, but no
physical-host run is claimed.

The first standalone run exchanged all 32 messages but hung during plugin stop;
`packet-stop-before-fix.txt` preserves the thread stacks. A pending retirement
timer could not dispatch while mux's control thread waited for plugin stop. Nif
now queues bounded owner retries during mux stop, and bind retries do likewise
during startup. `packet-loop-before-address-fix.txt` retains a subsequent shutdown
assertion from unnecessarily starting the resolver for an already parsed numeric
address; the utility now constructs ZiIP from its binary in_addr value.

`privileged-suite.txt` records all five privileged programs passing eighteen TAP
subtests / 240 checks in the documented two-pair anonymous namespace fixture.
The recorded normalized packet pair is functional evidence; current-source,
sustained-readiness/timer and build-mode checks remain required.

## Remaining required work

1. Finish independent Zxdp: delayed completion timing, sustained-load
   fairness and further wait races, and full pressure/coexistence/lifecycle coverage. The normalized
   Nif packet path and basic close/restart/final pair now work. Verify the remaining
   pending-DMA and physical behavior, and complete release/sanitizer gates.
   Verify the new diagnostic header, aligned Rx-chain/ownership test targets and
   registered-stream rollover cases before the independent gate closes.
2. Only after that gate, capture the fresh source manifest, make the mandatory
   byte-identical Zquic copy, and perform separately inspectable mechanical
   renaming before phase-1 adaptation.
3. Execute all eight cumulative working-pair slices in the plan. Phase 2 must
   separately consume/verify the `ztls_fix2.md` prerequisite. No slice is complete.
4. Complete actual independent kernel TCP peers in both roles, packet captures,
   physical-host runs, release and sanitizer/leak checks, Clang `-pg` profiles,
   optimization and comparable latency/throughput measurements.

No unavailable-environment skip or current implementation blocker has been
established. The next implementation work is release/sanitizer gates and remaining
sustained-load fairness/coexistence scenarios. The full
plan's acceptance scope is unchanged.
