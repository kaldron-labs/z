## Requirements
- Preserve the existing 1-RTT ACK-triggered packet-threshold retransmit path.
- Add RTT-derived time-threshold loss through the loss timer.
- Extend packet-threshold loss/retransmit coverage to all enabled packet number spaces.
- Detect persistent congestion and feed congestion state.

## Summary
Preconditions: plans 1 and 2 complete.

`zquic` already performs ACK-triggered packet-threshold loss for 1-RTT and immediately calls `retransmit_()` when loss is detected.  `PktTxSpace` also has utility behavior for time-threshold loss and retransmit queueing.  The remaining work is runtime integration: use a loss-time timer, apply loss detection across spaces, and connect persistent congestion to the congestion controller from plan 4.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add loss-deadline computation over the existing sent-packet spaces.

New or changed processes or threads: Tx thread owns loss detection and retransmit queue mutation.

New or changed interfaces: add internal `detectLoss_(space, now)` and `nextLossTime_()` helpers.

New or changed data flows: ACK updates RTT and packet-threshold state, then schedules loss time/PTO; loss timer scans spaces and queues retransmits.

New or changed event-driven or timer processing: loss-time timer is distinct from PTO and must be armed when a sent packet is old enough to have a pending time-threshold deadline.

New or changed network programming: retransmits continue through existing packet builders and packet-space protection.

New or changed data stores: add per-space persistent congestion episode tracking if existing utility state is insufficient.

## Detailed Design and Implementation Plan
### Phase 1: Preserve Existing ACK Loss Path
- Add tests around the current 1-RTT ACK path before modifying it.
- Keep `if (lost) impl()->retransmit_();` semantics for packet-threshold ACK loss.

### Phase 2: Add Time-Threshold Loss
- Compute the QUIC time threshold from RTT estimator values.
- Scan outstanding packets in each active space and mark packets lost when their sent time crosses the threshold.
- Schedule the loss-time timer to the earliest pending loss deadline.

### Phase 3: Space Ordering
- Align loss-time and PTO ordering with QUIC packet-space priorities.
- Do not let Application loss recovery starve Initial/Handshake recovery.

### Phase 4: Persistent Congestion
- Detect persistent congestion from lost packet intervals and PTO duration.
- Feed the result into `NewReno::persistentCongestion()` once plan 4 owns runtime congestion state.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:2887` - Existing immediate retransmit on ACK loss.
- `zquic/src/ZquicRecovery.hh:585` - Packet-threshold loss marking.
- `zquic/src/ZquicRecovery.hh:673` - `lose_()` enqueues retransmittable frames.
- `zquic/src/ZquicRecovery.hh:706` - PTO backoff utility used to derive persistent congestion periods.
- `zquic/test/ZquicRecoveryTest.cc` - Add targeted loss detection cases here.

## Detailed Test Plan
- Unit-test packet-threshold loss for 1-RTT to prove no regression.
- Unit-test time-threshold loss with controlled sent times and RTT values.
- Runtime-test that loss timer fires without an ACK and retransmits eligible frames.
- Test persistent congestion collapses cwnd after plan 4 wires `NewReno`.

## Non-goals
- Do not replace `PktTxSpace`.
- Do not implement spurious loss recovery.
- Do not make PMTUD probe loss affect cwnd; plan 10/4 handle probe classification.

## Options and Open Questions
- Option: compute persistent congestion in `PktTxSpace` or in a runtime recovery facade.  Prefer runtime facade if it needs RTT/PTO context.
- Open question: exact storage for lost interval endpoints if existing packet records do not retain enough metadata.

## Acceptance Criteria
- Implementation is audited and aligned with `GUIDELINES.md`
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
  - test suites must run address and leak sanitized
- Tests cover ACK-triggered packet-threshold retransmit, time-threshold loss, and persistent-congestion collapse.
- Loss processing enqueues retransmittable frames exactly once.
- PTO and loss-time deadlines choose the same packet space ordering as ngtcp2.

## Completion
- Append a change log to `arc/zquic4/log.md` in this format:
    ```
    ## [title]
    [change log]
    ```
  - `[title]` is the `###` title of this plan in `arc/zquic4/backlog.md`, without the number
    - example: `Replace PTO-only timing with explicit QUIC timers`
  - the change log should be detailed for agents to establish the starting point for further work
- `git commit` with one-line commit log `[title]`
