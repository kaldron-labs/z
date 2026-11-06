# zquic recovery repair plan

Status: source changes integrated and source audit recorded; paused before
phase 3 at the user's request. No build or test was run. Remaining audit and
dependency blockers are recorded in `zquic_repair_status.md` and
`zquic_repair_audit_inventory.md`; this is not a validation pass.

Repair the three validated defects in [zquic_findings.md](zquic_findings.md):
the congestion recovery boundary, ACK growth eligibility, and the falling-RTT
loss threshold. Preserve the existing ownership, batching, accounting, and
retransmission machinery. This plan concerns QUIC recovery, independently of
any proposed reuse for TCP.

## Evidence and validation limits

Source tracing and an isolated executable using the unchanged recovery header
reproduced these results:

| Case | Current result | Required result for the test |
| --- | --- | --- |
| Two separately reported losses, both sent before recovery began | `cwnd` goes from 12000 to 6000 to 3000 | Second loss leaves `cwnd` at 6000 |
| ACK of a 1200-byte packet sent before the recovery boundary | `cwnd` grows from 6000 to 6240 | Retire bytes in flight; leave `cwnd` at 6000 |
| RTT samples of 100 ms then 10 ms, without ACK delay | Loss threshold is 11.25 ms | Threshold is 99.84375 ms |
| A 20 ms-old packet with a later packet acknowledged | Packet is declared lost | Retain it under the corrected threshold |
| One packet-threshold loss scan split into two batches | Two window reductions | One reduction for those pre-recovery packets |

The five test cases failed their intended assertions; their setup and
accounting controls passed. These were algorithm-level reproductions, not a
successful full runtime test run. Temporary files under `/tmp` are investigation
artifacts, not permanent regression coverage.

Normal compilation was blocked by current `zi`/`zquic` API mismatches involving
`Zi::RxRange`, `ZiMxBackend`, and `ZiTxContext::flags`. A later attempt to add a
same-ACK ordering reproduction encountered additional incompatibilities in
`ZquicPQueue.hh` involving `override`, `storageMemory_()`, and buffer `memory()`.
Recheck these blockers against the working tree before implementation; do not
silently include the ongoing I/O migration in this repair.

## Existing mechanisms to retain

The implementation was reexamined before proposing changes. In
[ZquicRecovery.hh](zquic/src/ZquicRecovery.hh) and
[ZquicLink.hh](zquic/src/ZquicLink.hh), the following already exist:

| Mechanism | Existing behavior | Repair implication |
| --- | --- | --- |
| `PktAckBatch` and `PktLossBatch` | Preserve scan progress across bounded continuations | Extend existing work state only where needed; keep the scan budget |
| `PktTxUpdate::lost()` | Aggregates ordinary losses and retains their latest send time | Keep this timestamp as the congestion-event eligibility input |
| `PktTxUpdate::ackd()` | Excludes previously lost packets from fresh ACK-byte totals while retaining frame completion work | Preserve late-ACK handling and avoid double retirement |
| PMTUD accounting | Separates probe ACK/loss bytes and suppresses their ordinary window response | Keep this separation |
| `TxAckWork::ceDelta` | Is carried across batches and consumed after a successful ECN congestion response | Do not add a duplicate per-ACK ECN suppression mechanism |
| `TxAckWork::gen` | Rejects ACK work from an earlier runtime reset | Preserve it; it is not a congestion-epoch identifier |
| `congestionOpened` | Requests a send flush after ACK processing completes | It does not defer or filter congestion-window growth |
| `NewReno::release()` | Retires bytes without changing the window | Reuse this operation when separating retirement from growth |

`PktAckBatch::latestSentTime` supports RTT sampling. It cannot classify the
bytes in a mixed old/new ACK batch. Likewise, `ecnAckdSentTime` covers ECN-marked
packets and is not a substitute for ordinary ACK eligibility.

The current `inRecovery()` merely tests whether a boundary has ever been set.
It is not used to gate ACK growth. Do not add an unconditional return based on
that boolean: it would also suppress eligible ACKs of later transmissions.

## Phase 1 — Concurrent feature implementation; source changes only

The coordinator assigns one feature to each of two subagents and runs them
concurrently. Recovery-boundary correction and ACK eligibility form one feature:
they share `NewReno`, congestion-event ordering, and the ACK/loss pipeline.
Splitting those changes between workers would introduce a dependency and
competing edits. RTT loss-threshold correction is independently implementable.

| Subagent | Feature | Exclusive edit ownership |
| --- | --- | --- |
| A | Recovery epochs and ACK congestion accounting | `NewReno`, `PktTxUpdate`, and ACK traversal in `ZquicRecovery.hh`; affected runtime functions/work state in `ZquicLink.hh`; existing recovery/congestion test sources and required caller migrations |
| B | RTT time-threshold correction | `RttEstimator::timeThreshold()` in `ZquicRecovery.hh`; new `zquic/test/ZquicRttLossTest.cc` |

Use isolated checkouts or apply narrowly scoped patches against a recorded
common source snapshot. Both features touch `ZquicRecovery.hh`, but their
owned sections are disjoint. Do not replace or reformat the whole file. The
coordinator integrates both patches, registers the new test in
`zquic/test/Makefile.am`, and resolves any integration conflicts by source
inspection. Preserve the existing uncommitted work in the source snapshot.

Allowed work is source inspection and source edits, including regression-test
sources and their build registration. Do not configure, regenerate build files,
compile, link, run syntax-only compiler checks, run tests, or build images.
Do not execute the temporary reproductions in this phase. Read the relevant
repository and module guidance before edits.

### Feature A — Recovery epochs and ACK accounting

Change the congestion-event API to accept both the triggering packet's send
timestamp and the time the congestion event is processed. Compare the former
against the saved boundary; store the latter only when starting a new recovery
period. Keep the comparison inclusive at the boundary. This follows
[RFC 9002 B.6](https://www.rfc-editor.org/rfc/rfc9002.html#appendix-B.6).

Apply this contract to both `lostAt()` and the ECN caller. Use the existing
clock domain and consistent units. Obtain the event time on the owning Tx
thread; do not substitute a lost-packet timestamp or an arbitrarily old scan
snapshot. A timer scan's fixed `now` for deciding which packets are overdue
has a different purpose from the time a new recovery period actually starts.

Retain the maximum ordinary lost-packet send time already accumulated in
`PktTxUpdate`. Suppressed congestion responses must still retire newly lost
bytes. Preserve the minimum congestion window and PMTUD exceptions.

Migrate all callers and tests together. Remove or reshape timestamp-free
entry points that would bypass the corrected contract; do not retain a
zero-timestamp compatibility path merely to avoid updating tests.

Required cases:

- Older, equal-boundary, and later-but-still-pre-recovery packet losses.
- A subsequent event for a packet transmitted after the boundary.
- Packet-threshold and time-threshold scans split across continuations.
- Loss and ECN signals referring to the same pre-recovery flight.
- Different packet-number spaces sharing the same congestion controller.
- PMTUD loss, byte retirement, minimum-window behavior, and runtime reset.

Classify ordinary newly acknowledged bytes using each packet's send time
while that metadata is still available. Keep total bytes to retire distinct
from bytes eligible to grow the window. A packet sent at or before the
recovery boundary contributes no growth; a later packet can contribute.
This is the distinction in
[RFC 9002 B.5](https://www.rfc-editor.org/rfc/rfc9002.html#appendix-B.5).

Prefer adding the necessary scalar accounting to the existing update/work
types and passing the boundary through the existing traversal. Do not retain
packet objects, payloads, or an unbounded timestamp list solely for this test.
Inspect the placement carefully: `ack_()` currently clears `p.inFlight`
before `PktTxUpdate::ackd()` runs. Any classification requiring the original
in-flight state must capture it before that mutation.

Reuse `release()` for retirement, or reshape the ACK API to take separate
retirement and eligible-growth totals. Use widths consistent with the existing
64-bit aggregates. Zero eligible bytes must leave the window unchanged,
including the congestion-avoidance minimum-increment branch.

Account for these ordering requirements in the source changes and test sources:

- A boundary change before a later batch is processed must affect that batch's
  eligibility. Do not freeze an obsolete boundary for the whole continuation
  chain without accounting for changes.
- `applyAckUpdateTx_()` currently grows the window before calling
  `applyECNAckTx_()`. The packet-loss phase runs after the ACK phase. Establish
  congestion responses before committing growth attributable to the same ACK,
  following the dependency order in
  [RFC 9002 A.7](https://www.rfc-editor.org/rfc/rfc9002.html#appendix-A.7).
- If the smallest implementation defers eligible-byte totals until the existing
  ACK/loss work completes, put them in `TxAckWork`. Prove that any intervening
  boundary change invalidates or reclassifies those totals correctly; do not
  reuse the reset generation as an epoch guard. Cover reset, packet-space
  discard, and persistent-congestion interactions before adopting this design.
- Preserve frame completion, stream reaping, ACK-of-ACK processing, and the
  existing flush decision. Retiring old bytes can permit transmission even
  when the congestion window does not grow.

Choose between local reordering and limited staging from the demonstrated
caller dependencies; record the choice and its invariant in the handoff.
Write the ordering regressions now and defer their execution to phase 3.
A broad ACK-pipeline rewrite is not a prerequisite.

Required cases include all-old, all-new, and mixed-epoch ACKs; equality at the
boundary; duplicate and late ACKs; zero eligible bytes; PMTUD; same-ACK loss
and ECN; and a boundary change between two ACK continuations. Verify both
window behavior and exactly-once byte retirement.

Write permanent regression sources using `ZuTestUtil` and synthetic timestamps.
Port the relevant isolated cases into the existing recovery/congestion tests.
Correct the existing assertion that equates the recovery boundary with the
lost packet's send time. Preserve existing duplicate, late-ACK, PMTUD,
persistent-congestion, and cursor tests.

Include the same-ACK ordering case: ACKing PN 4 also provides packet-threshold
loss evidence for PN 1. The current order grows an initial window of 12000 to
13200 for a 1200-byte ACK and then halves it to 6600. The corrected response
must not credit pre-recovery ACK bytes ahead of that congestion response.
Include the equivalent ECN case and continuation interleavings. These are
source-level requirements in this phase; their execution belongs to phase 3.
New complete-network fixtures belong under `zquic/itest`; reuse suitable
existing fixtures without relocating unrelated tests.

### Feature B — RTT time-threshold correction

In `RttEstimator::timeThreshold()`, use the maximum of the available latest
and smoothed RTT, apply the existing 9/8 multiplier, and clamp to the existing
granularity. Preserve the initial-RTT fallback before measurements exist.
Minimum RTT is not a substitute for the smoothed RTT in this calculation.
See [RFC 9002 6.1.2](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.1.2).

Keep the calculation in `ZuTime` precision. Assert the exact 99.84375 ms result
for the falling-RTT example rather than rounding the test expectation to whole
microseconds. Cover rising RTT, constant RTT, no samples, and the granularity
floor. Verify both `nextLossTime()` and packet classification immediately
before and at the deadline, with a later packet acknowledged and without
satisfying the packet-number threshold.

The current runtime already routes loss deadlines and time-based detection
through `timeThreshold()`. Reuse those paths; do not add a second threshold
calculation or a new timer. Broader RTT/ACK-delay estimation changes require
their own demonstrated failure and are outside these three repairs.

Write the RTT and deadline regressions in the independently owned
`ZquicRttLossTest.cc`; leave existing test files to subagent A. Reuse existing
packet-tracker APIs and explicit timestamps. Do not alter `NewReno`, the ACK
pipeline, or general RTT sampling/ACK-delay behavior for this feature.

### Phase 1 handoff

Each worker reports changed files/sections, the violated invariant and repair,
new test cases, caller migrations, and unresolved source concerns. Explicitly
state that no build or test was run. The coordinator reviews the combined
source diff and preserves both features before phase 2 begins. No worker may
start rebuilding early while another is still editing.

## Phase 2 — Source audit and repair; no rebuild or retest

After feature integration, audit and repair `zquic` under
[docs/AUDIT.md](docs/AUDIT.md), together with applicable engineering, C++, style,
framework, I/O, and module guidance. Cover all repository-owned C++ directories
in `zquic`: `src`, `test`, `util`, `itest`, `example`, `bench`, and `interop`
where present. Inspect relevant declarations and uses, including external
callers affected by API changes. Audit scope is broader than edit scope.

For each issue, record the requirement or observed failure, violated invariant,
owner, evidence, and disposition. Apply the smallest demonstrated repair;
report unproven concerns rather than adding speculative machinery. Follow all
applicable audit categories, including layout, allocation/storage, intrusion,
control flow, framework fit, casting, lifetime, cleanup, copies, and liveness.

Specifically review the integrated recovery changes for:

- Distinct packet-send and event timestamps, consistent units, and boundary
  equality behavior across both loss and ECN paths.
- Exactly-once byte retirement, mixed-epoch ACK classification, zero eligible
  bytes, PMTUD exceptions, and the mutation of `p.inFlight` during ACK removal.
- ACK/ECN/loss ordering, bounded continuation state, reset and discard handling,
  and persistent-congestion interactions.
- Preservation of the existing cursors, `ceDelta` consumption, reset guard,
  frame completion, stream reaping, ACK-of-ACK processing, and flush behavior.
- Tx-thread ownership; no unnecessary locks, packet retention, heap allocations,
  extra scans, or counters; qlog compile-out and value-capture rules.
- Source coverage for each repaired invariant and complete migration of callers.

Source and test-source repairs are allowed; configuration, compilation,
compiler-driven analysis, test execution, and image builds remain prohibited.
Resolve feature integration problems here. Do not absorb unrelated ongoing I/O
work or repair outside-module issues without establishing their necessity.

Exit with an audited source snapshot and a record of repaired issues and any
remaining blockers. Unresolved demonstrated correctness defects block phase 3;
known environment/build prerequisites must be explicit rather than described
as successful validation.

## Phase 3 — Rebuild and retest; Clang debug only

Only after phases 1 and 2 finish, configure and validate the integrated source.
Use native x86-64 Clang debug throughout. Do not run release, ASan, LSan, MinGW,
ARM, GCC, or an additional Valgrind validation matrix. This user-selected scope
supersedes the broader validation matrix in the earlier plan.

1. Recheck current dependency/API compatibility and record the source snapshot,
   local patch state, compiler version, dependencies, and configuration. The
   earlier build blockers are historical observations, not assumed current
   failures. Preserve unrelated work when resolving necessary prerequisites.
2. Configure through `z.config` with `-L -d -Q` and `/usr/local`, retaining any
   required dependency/environment settings and appropriate optional-module
   switches. `/usr/local` allows phase 4 to reuse this build for QIR images.
   Verify actual compiler flags and debug defines; a report label is not proof
   of build type. Exclude sanitizer and cross-compilation flags.
3. Perform a top-level clean when changing build configuration, then a
   top-level `make -j8` to rebuild dependencies consistently. Do not mix stale
   release or sanitizer objects into the debug build. Require a warning-free
   build.
4. Build each dependent test directory's default target before running its
   tests. At minimum run the complete `zquic/test` and `zquic/itest` suites,
   and the affected `zhttp`/`zws` suites against the rebuilt QUIC library.
   Include the QIR endpoint's local tests in `zhttp/interop`; Docker/upstream
   QIR execution follows in phase 4.
5. Check the exact recovery/ACK/RTT expectations and continuation cases from
   phase 1. Where a coherent pre-fix snapshot is available, demonstrate the
   new regressions fail there using a separate Clang debug build. Never revert
   fixes in the active workspace or claim a pre-fix run that was blocked.
6. Diagnose failures against the recorded baseline. Necessary source repairs
   return through phase 2's audit before rebuilding and rerunning affected
   tests. Any broader rerun must be justified by changed dependencies or risk.

Representative commands, after confirming the required local configuration:

```sh
./z.config -c -L -d -Q /usr/local
make clean
make -j8
make -C zquic/test -j8
make -C zquic/test test
make -C zquic/itest -j8
make -C zquic/itest test
make -C zhttp/interop -j8
make -C zhttp/interop test
```

Use `libtool exec` for debugging source-tree executables. Retain build logs,
TAP output, exact commands, and remaining blockers. Phase 3 passes when the
required regressions and affected suites pass on the same audited Clang debug
source snapshot. Do not substitute isolated-header tests for runtime coverage.

## Phase 4 — QIR interoperability testing; Clang debug only

Run QIR after phase 3 passes. Use the existing endpoint and lifecycle documented
in [zhttp/interop/README.md](zhttp/interop/README.md) and implemented by
`scripts/qir-build-image`, `scripts/qir-run-local`, `scripts/qir-collect-results`,
and `scripts/qir-report`. This phase validates interoperability; the deterministic
recovery assertions remain covered by phase 3.

### Build and identify the debug image

Reuse phase 3's audited Clang debug configuration and `/usr/local` prefix.
The ordinary `qir-build-image` configuration path currently invokes
`z.config -c -Q -L` without `-d`; do not allow it to replace the debug
configuration. Build through the top-level image target, which stages the
current artifacts and invokes the helper with `--root --no-configure`:

```sh
ZHTTP_QIR_IMAGE=zhttp-qir:repair-clang-debug make -j8 zhttp-qir-image
```

If invoking the helper directly against the configured tree, explicitly use
`--no-configure`; do not use a stale staged root. Confirm the endpoint and
staged Z libraries come from the phase 3 debug build. Record the image ID or
digest, source revision and local patch identity, compiler/configuration, and
runner revision. The `--build-type clang-debug` argument below labels results;
it does not select compiler flags. The Clang restriction applies to the Z
endpoint and its build, not the toolchains used by third-party peer images.

Check Docker, runner dependencies, and the managed environment's container and
network requirements before execution. Run the documented image smoke tests
for `hq-interop` and HTTP/3, including file-content comparison and clean
endpoint termination. Use a dedicated runner checkout/output area so wrapper
cleanup does not remove another run's artifacts. Keep images local; registry
publication is outside this plan.

### Execute the peer matrix

Run zquic as both client and server against `ngtcp2`, `quiche`, `quic-go`, and
`msquic`. The wrapper already runs both directions. Require the core
`handshake`, `transfer`, and `http3` matrix, then run `rebind-port` and
`rebind-addr` separately to distinguish known baseline gaps from regressions.

```sh
scripts/qir-run-local --runner /path/to/dedicated/quic-interop-runner \
  --image zhttp-qir:repair-clang-debug \
  --peers ngtcp2,quiche,quic-go,msquic \
  --tests handshake,transfer,http3 \
  --build-type clang-debug \
  --out /path/to/new/qir-repair-core

scripts/qir-run-local --runner /path/to/dedicated/quic-interop-runner \
  --image zhttp-qir:repair-clang-debug \
  --peers ngtcp2,quiche,quic-go,msquic \
  --tests rebind-port,rebind-addr \
  --build-type clang-debug \
  --out /path/to/new/qir-repair-rebind
```

Inspect the pinned runner and endpoint's actual supported cases before adding
loss, reordering, RTT-change, or ECN scenarios. Run applicable supported
recovery scenarios when available and record their exact impairment settings;
do not invent testcase names or count unsupported exit status `127` as a pass.
If the runner cannot exercise a desired recovery mechanism, record that limit
and retain its deterministic coverage from phase 3.

The repository currently documents rebinding failures for zquic-as-client
against quiche, quic-go, and msquic, and zquic-as-server against msquic. Verify
those against comparable retained results or a Clang debug baseline rather
than assuming every failure is pre-existing. Do not broaden this repair into
an unrelated migration project.

### Results and completion

Retain the generated `report.md`, runner results, stdout/stderr, endpoint logs,
and available qlog/packet traces under unique output directories. Inspect
per-test outcomes as well as wrapper exit status. Distinguish passes, skips,
unsupported cases, infrastructure failures, known baseline failures, and new
regressions. Restore the runner registration and clean up only this run's
containers/networks through the existing lifecycle.

QIR passes when the supported core matrix succeeds in both directions and
there are no unexplained new failures in the additional supported cases.
A blocked runner or unavailable peer is incomplete coverage, not a pass.
Any source repair triggered by QIR returns through phase 2 and phase 3, then
requires a fresh debug image and rerun of affected QIR cases.

The final record must identify the exact tested source/image, all four phase
outcomes, and any unresolved limits. Update the findings' repair/validation
status only with checks actually completed.
