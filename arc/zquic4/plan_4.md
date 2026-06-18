## Requirements
- Make `NewReno` runtime-owned and update it on packet send, ACK, loss, and persistent congestion.
- Replace packet send budgets based on `app()->maxUDP()` with congestion-window and bytes-in-flight allowance.
- Add pacing hooks only after cwnd gating is correct.

## Summary
Preconditions: plans 1 through 3 complete.

`zquic` already has a `NewReno` utility with cwnd, ssthresh, bytes-in-flight, loss, and persistent-congestion methods.  Runtime sends do not use it; budgets usually set congestion allowance to `app()->maxUDP()`.  This plan wires `NewReno` into runtime packet accounting without adding new congestion algorithms.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add a `NewReno` member to connection/link runtime state and a thin recovery facade that updates it.

New or changed processes or threads: Tx thread owns congestion state because packet send, ACK, and loss are Tx-side events.

New or changed interfaces: add internal helpers for `onPacketSent_`, `onPacketAckd_`, `onPacketLost_`, and `sendBudget_`.

New or changed data flows: sent-packet records must expose bytes sent and in-flight status to ACK/loss paths.

New or changed event-driven or timer processing: persistent congestion from plan 3 calls into congestion state.

New or changed network programming: packet builders cap normal data/control sends by cwnd and bytes in flight.

New or changed data stores: sent-packet entries may need byte length and PMTUD-probe classification if not already retained.

## Detailed Design and Implementation Plan
### Phase 1: Runtime Ownership
- Add `NewReno m_cc` near recovery state.
- Initialize it with the active path max datagram size once plan 8 path integration exists; until then use current max UDP.
- Keep controller state mutable and Tx-owned.

### Phase 2: Send Accounting
- On successful packet send, call `m_cc.sent(bytes, inFlight)` for ACK-eliciting packets that count in flight.
- Ensure ACK-only packets do not inflate bytes in flight.
- Include packet protection overhead in bytes consistently with existing packet length accounting.

### Phase 3: ACK/Loss Accounting
- On ACK, subtract acknowledged in-flight bytes and grow cwnd.
- On loss, subtract lost bytes and reduce cwnd unless the packet is marked PMTUD probe.
- On persistent congestion, collapse cwnd.

### Phase 4: Budget Enforcement
- Replace `budget.congestion = app()->maxUDP()` with `min(path allowance, cwnd remaining)`.
- Stop scheduling normal frames when congestion allowance is exhausted.
- Defer pacing implementation to plan 19; leave a hook point for delayed sends.

## Code References to Impacted Code
- `zquic/src/ZquicRecovery.hh:258` - Existing `NewReno` utility to reuse.
- `zquic/src/Zquic.hh:2592` - Packet build budget logic for client sends.
- `zquic/src/Zquic.hh:4719` - Packet build budget logic for server sends.
- `zquic/src/Zquic.hh:2860` - Sent-packet recording can feed congestion sent accounting.
- `zquic/src/Zquic.hh:2887` - ACK processing can feed congestion ACK/loss accounting.
- `zquic/test/ZquicCongestionTest.cc` - Extend existing congestion tests.

## Detailed Test Plan
- Unit-test NewReno remains unchanged for basic cwnd/loss behavior.
- Runtime-test cwnd-limited send: queue more data than cwnd and verify only cwnd budget is emitted.
- Runtime-test ACK opens the window and loss reduces it.
- Runtime-test PMTUD probe loss does not reduce cwnd after plan 10 marks probes.

## Non-goals
- Do not implement CUBIC, BBR, ECN response, or pacing in this plan.
- Do not add locks around congestion state.
- Do not preserve any dependent behavior that assumed unlimited congestion budget.

## Options and Open Questions
- Option: store bytes-in-flight only in `NewReno` or mirror aggregate bytes in sent-packet spaces for diagnostics.
- Open question: whether server anti-amplification or cwnd should cap first when both apply.  Use the minimum effective allowance.

## Acceptance Criteria
- Implementation is audited and aligned with `GUIDELINES.md`
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
  - test suites must run address and leak sanitized
- Congestion window gates normal stream/control sends.
- ACK increases cwnd; loss reduces cwnd; PMTUD probe loss does not reduce cwnd.
- Runtime tests include cwnd-limited send and loss recovery cases.

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
