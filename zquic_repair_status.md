# zquic repair source handoff

Base revision: `96afee36256c480e1285d598792750ee4eaae082`, with existing local
I/O migration and unrelated edits preserved. Shared source baselines were
recorded in `/tmp/zquic-repair-baseline` before feature edits. This is an
uncommitted source snapshot, not a tested revision.

Phase 1: integrated recovery boundary, per-packet ACK eligibility and deferred
growth, plus RTT threshold correction. Two feature workers edited disjoint
sections of the recorded header snapshot concurrently. Coordinator reviewed
the combined diff and registered `ZquicRttLossTest` in `zquic/test/Makefile.am`.

Phase 2: source audit performed across all present repository-owned C++
directories, with independent review of integrated recovery. Small demonstrated
layout and unused-helper repairs applied. This is not a clean audit pass:
remaining issues and evidence are in `zquic_repair_audit_inventory.md`.

Phase 3: not started. User requested pause before this phase. No configure,
regeneration, compiler check, compilation, linking, tests, temporary
reproductions, or image builds were executed. Source diff inspection and
`git diff --check` are the only coordinator checks; they do not validate C++.

Phase 4: not started.

## Recovery invariant and source coverage

Loss and ECN supply separate packet-send and current Tx event timestamps in
microseconds. Recovery suppresses send times at or before the boundary.
ACK traversal captures original in-flight state before removal; ordinary
retirement and eligible growth are distinct 64-bit sums. PMTUD remains
retirement-only. Zero growth bypasses the minimum increment.

Each ACK batch retires bytes immediately, while `TxAckWork::growth` stages two
scalars until the ACK's ECN/loss processing finishes. A distinct congestion
epoch changes on new recovery and persistent congestion, invalidating prior
staged totals: every previously credited transmission predates that event's
processing time. Each later batch uses the current recovery boundary. Runtime
reset generation and discarded-space guards abandon old work before commit.
No packet retention, timestamp list, extra scan, lock, or timer was added.
Existing bounded cursors, frame completion/reaping, ACK-of-ACK, CE consumption,
and flush decisions remain in place.

`ZquicCongestionTest` adds old/equal/post-boundary losses, shared loss/ECN,
minimum window, PMTUD, reset/epoch behavior, all-old/all-new/mixed ACKs,
duplicate/late ACKs, same-ACK PN4/PN1 loss and ECN ordering, actual budget-one
ACK continuations around a boundary change, and split packet/time loss scans
across packet spaces. Existing recovery test callers and boundary expectations
were migrated. `ZquicAPITest::testRecoveryRuntimeOrdering` additionally
uses its existing simulated-owner HubFixture and protected recording/ACK APIs
to exercise actual processAckFrameTx_ ordering for PN4/PN1 with and without ECN,
an ECN-only PN2 ACK without packet-threshold evidence, and reset/discard
between queued ACK continuations. Fixture diagnostics are initialized, ECN
testing is explicitly enabled, CE events and validation failures are checked,
and timers are cancelled before teardown. No new production test seam
or complete-network fixture was added. Packet recording uses the runtime clock;
tracker/controller deadline and epoch assertions use synthetic timestamps.
These are source regressions, not a claimed runtime pass.

`ZquicRttLossTest` is the new test entry point: exact 99.84375 ms falling RTT,
20 ms retention, rising/constant/no samples/granularity, `nextLossTime`, and
classification one nanosecond before and exactly at deadlines, with retirement
checks and insufficient packet-number evidence. Execution is deferred.

## Remaining gates

Current source still contains I/O API mismatches against `zi` (RxRange,
backend selection, Tx flags, and buffer overrides/storage access). These were
rechecked by source inspection only. They require explicit migration work
before any successful integrated build can be claimed.

Active fixture polling and existing C++ policy violations are recorded in the
audit inventory. The audit is not clean and phase 3 must not start with
unresolved demonstrated correctness defects. No successful validation or QIR
result is claimed.
