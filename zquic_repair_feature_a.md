# Feature A source handoff

Phase 1 source changes only; no configuration, build, compiler check, image
build, temporary reproduction, or test execution was performed.

Changed sections: NewReno, new AckGrowth, PktTxUpdate ACK accounting and
PktTxSpace ACK traversals in zquic/src/ZquicRecovery.hh; TxAckWork,
applyECNAckTx_, applyAckUpdateTx_, applyLossUpdateTx_ and processAckFrameTx_
in zquic/src/ZquicLink.hh; existing ZquicRecoveryTest.cc and
ZquicCongestionTest.cc. Baseline copies are in /tmp/zquic-feature-a.
The concurrently owned RTT change is preserved.

NewReno now compares packet-send microseconds inclusively against its boundary
and records event-processing microseconds from the Tx owner's now_(). Both
ordinary loss and ECN callers migrated. Removed timestamp-free loss API.
Retirement uses uint64_t; ACK retirement and eligible growth are distinct.
Explicit eligible ACK API prevents the zero-credit minimum-increment bug.

Limited staging was chosen because same-ACK packet-threshold loss is discovered
after bounded ACK traversal and ACK metadata is deleted during traversal.
PktTxUpdate classifies each packet using the current boundary and captures
inFlight before ack_ clears it. Frame completion and retirement remain
immediate; only a scalar eligible-byte sum is deferred until loss completion.
A congestion epoch changes on successful recovery starts and persistent
congestion. AckGrowth invalidates prior staged credits when the epoch changes.
Those transmissions all precede the new event-processing time, so none could
remain eligible; no packet retention, extra scan, or timestamp list is needed.
Later batches classify against the current boundary. ECN is applied before
staging the current batch; its prior classification epoch prevents that batch
from crediting old packets. Persistent congestion precedes commit. Existing
runtime generation and discarded-space early returns abandon the work without
undoing already completed retirement. No use of runtime reset generation as a
congestion epoch. Existing ceDelta handling, cursors, reaping, ACK-of-ACK and
flush decision remain; probe retirement now also marks congestionOpened.

Source regressions (not executed):

| Requirement | Source coverage |
| --- | --- |
| Older/equal/later pre-recovery loss, new post-boundary flight | testRecoveryEpochs |
| Loss and ECN on same flight, PMTUD, minimum window, retirement/reset | testRecoveryEpochs |
| All-old/new/mixed ACK, equality, ACK-only/nonflight | testAckEpochClassification |
| Duplicate/late ACK, zero eligibility, PMTUD ACK | testAckEpochClassification |
| PN4 ACK yields PN1 loss, equivalent ECN before growth | testAckResponseOrdering, tracker + actual AckGrowth |
| Epoch change between staged batches, persistent-congestion invalidation | testAckResponseOrdering |
| Packet/time loss split over budget-one continuations, shared spaces | testSplitLossEpoch |
| Existing cursor/duplicate/late ACK/frame/PC tests | preserved; caller API migrations only |

Remaining source concerns for phase 2: the new ordering tests exercise the
tracker, controller and actual AckGrowth staging, not direct Link runtime
invocation. Reset/discard coverage demonstrates work abandonment at helper
level; direct runtime reset/discard and real ACK traversal across an epoch
change need stronger source coverage. Existing uncommitted RuntimeTest.cc was
not changed. Compilation and behavioral validation remain entirely deferred.

Phase 2 followup: moved PktTxUpdate exposed fields before its functions to
conform to struct layout guidance. Added testAckContinuationEpoch using actual
budget-one ackBatch traversal before and after a synthetic boundary change;
it checks current-boundary classification, per-turn retirement, final growth,
and the final empty continuation. This closes the tracker continuation gap.
Direct Link runtime path/reset/discard coverage remains unimplemented: the
existing runtime fixture initializes real apps/threads and private Tx packet
number/generation state cannot be injected by a derived synthetic fixture.
A narrow test seam or reuse of initialized runtime fixtures needs source design;
no private access bypass or speculative interface was introduced.

Final phase 2 followup superseding the runtime gap above: existing APITest
HubFixture provides simulated owner-thread queues and Link already provides
protected setTxPN_, recordTxPkt_, processAckFrameTx_, resetTx_ and discard APIs.
Added testRecoveryRuntimeOrdering in that file with TestLink wrappers over
those existing APIs. It checks actual same-ACK loss/ECN ordering and exactly-once
retirement, then queued ACK continuation rejection on runtime reset and packet
space discard. The tests use runtime clock for packet recording; synthetic
per-packet epoch/deadline coverage remains in CongestionTest. No new production
seam or complete-network fixture was introduced. No builds or tests were run.

Final fixture review repairs: initialize congestion diagnostics by calling the
existing resetTx_ wrapper before taking the initial window snapshot; explicitly
enter ECN testing through existing setEcnDisabled_(false) before recording ECT0
packets; assert one CE event and no ECN validation failure. Added an ECN-only
PN2 ACK variant (no packet-threshold loss possible), so window halving proves
the ECN response runs before growth independently of loss. Cancel timers on the
Tx owner before each fixture link is destroyed, including after deferred guard
queues drain. These are source-inspected changes only, not executed checks.
