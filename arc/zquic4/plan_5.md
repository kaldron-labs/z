## Requirements
- Promote `AckManager` or equivalent into the runtime receive path.
- Track largest received packet timestamp per packet space.
- Generate ACK delay for 1-RTT and zero ACK delay for Initial/Handshake.
- Support immediate ACK triggers and ACK-only packet scheduling.

## Summary
Preconditions: plans 1 through 4 complete, especially ACK delay timer ownership from plan 1.

`AckTracker` and `AckManager` already exist and tests cover ACK range behavior.  Runtime ACK generation is still snapshot/immediate-flush oriented.  This work wires ACK management into receive processing and packet assembly while preserving existing range tracking.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add per-space runtime ACK state based on `AckManager`, plus largest-received timestamps.

New or changed processes or threads: Rx records packet receipt and ACK needs; Tx emits ACK frames.  Cross-shard posts carry compact ACK snapshots.

New or changed interfaces: add internal methods to mark ACK-needed, mark immediate ACK, produce ACK frame, and commit ACK state after packet send.

New or changed data flows: packet receive updates ACK state; timer or immediate policy schedules ACK-only/control packet; send path commits after successful packet write.

New or changed event-driven or timer processing: ACK delay timer from plan 1 fires only for delayed ACK emission.

New or changed network programming: ACK-only packets are sent without retransmittable sent-packet entries.

New or changed data stores: store per-space largest receive timestamp and delayed/immediate pending flags.

## Detailed Design and Implementation Plan
### Phase 1: Integrate Existing AckManager
- Reuse `AckManager` rather than creating a new ACK range container.
- Extend it only where runtime needs immediate ACK flags or largest receive timestamp.
- Preserve `AckTracker` duplicate and range behavior.

### Phase 2: Receive Path Wiring
- On valid packet receive, add the packet number to the correct space.
- For Initial/Handshake, schedule immediate ACK with zero delay.
- For 1-RTT ACK-eliciting packets, arm ACK delay unless immediate triggers apply.

### Phase 3: Send Path Emission
- Teach packet assembly to ask ACK state for an ACK frame when due or when piggybacking.
- Commit ACK state only after the packet is successfully emitted.
- Avoid recording ACK-only packets as retransmittable/in-flight.

### Phase 4: Immediate ACK Policy
- Add immediate triggers for gaps/reordering and protocol-required ACKs.
- Keep the policy simple and testable; later ACK frequency extensions are out of scope.

## Code References to Impacted Code
- `zquic/src/ZquicRecovery.hh:36` - Existing `AckTracker`.
- `zquic/src/ZquicRecovery.hh:172` - Existing `AckManager`.
- `zquic/src/Zquic.hh:3305` - Runtime frame receive switch processes ACK and other frames.
- `zquic/src/Zquic.hh:2852` - Pending ACK append path to replace with managed ACK emission.
- `zquic/test/ZquicRecoveryTest.cc` - Existing ACK utility tests to extend.

## Detailed Test Plan
- Unit-test ACK delay deadlines and immediate ACK flags for all spaces.
- Runtime-test delayed 1-RTT ACK emission with encoded delay.
- Runtime-test Initial/Handshake ACK emission with zero delay.
- Runtime-test ACK-only packet does not create retransmittable sent-packet state.

## Non-goals
- Do not implement ACK_FREQUENCY.
- Do not implement ECN counters here; plan 6 handles ACK_ECN.
- Do not alter receive reassembly behavior.

## Options and Open Questions
- Option: keep `AckManager` timestamps in microseconds for frame encoding or `ZuTime` for scheduler integration.  Prefer `ZuTime` at runtime and convert at frame write.
- Open question: exact immediate ACK thresholds for packet reordering; start with QUIC baseline behavior.

## Acceptance Criteria
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
  - test suites must run address and leak sanitized
- Tests verify delayed ACK emission, immediate ACK emission, ACK delay encoding, and ACK state commit after send.
- ACK-only packets do not create retransmittable sent-packet entries.
- ACK ranges remain correct across duplicates and reordering.

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
