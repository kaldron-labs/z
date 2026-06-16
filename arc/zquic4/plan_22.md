## Requirements
- Decide whether fixed half-window replenishment is sufficient.
- If not sufficient, add configurable adaptive receive windows.
- Preserve existing flow-control correctness and reassembly behavior.

## Summary
Preconditions: plans 1 through 21 complete.

`zquic` already implements connection and stream receive credit, transmit credit, MAX_DATA, MAX_STREAM_DATA, MAX_STREAMS, and BLOCKED validation/queueing.  The audit divergence is policy: replenishment is fixed half-window, while ngtcp2 can scale windows.  This plan first evaluates whether adaptive scaling is worth the complexity, then implements it only if justified.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: optional adaptive flow-window policy attached to receive flow state.

New or changed processes or threads: Rx owns receive credit consumption and update decisions; Tx emits MAX_* frames.

New or changed interfaces: engine params may expose max receive window and adaptive enable/disable.

New or changed data flows: receive progress and RTT samples feed window growth decisions; flow updates queue through existing control-frame path.

New or changed event-driven or timer processing: no required new timer; RTT input comes from recovery state.

New or changed network programming: larger windows affect peer send allowance but not packet parsing.

New or changed data stores: per-connection and per-stream window policy state if adaptive mode is enabled.

## Detailed Design and Implementation Plan
### Phase 1: Evaluation
- Measure/test fixed half-window behavior under high BDP simulated transfers.
- Decide whether adaptive windows are needed for target workloads.

### Phase 2: Configuration
- Add params for adaptive receive-window enablement and maximum caps.
- Defaults should preserve conservative current behavior unless performance data justifies a new default.

### Phase 3: Adaptive Policy
- Grow connection and stream windows based on consumption rate and RTT.
- Cap growth at configured max.
- Keep updates sparse enough to avoid control-frame spam.

### Phase 4: Validation
- Preserve flow-control error detection and duplicate reassembly accounting.

## Code References to Impacted Code
- `zquic/src/ZquicStream.hh:92` - `ReceiveFlow` window update logic.
- `zquic/src/Zquic.hh:3613` - Connection MAX_DATA update path.
- `zquic/src/Zquic.hh:3633` - Stream MAX_STREAM_DATA update path.
- `zquic/src/Zquic.hh:3652` - MAX_STREAMS update path.
- `zquic/test/ZquicFlowTest.cc` - Existing flow-control tests.

## Detailed Test Plan
- Test current fixed half-window behavior remains correct when adaptive is disabled.
- Test adaptive window growth under sustained receive progress.
- Test configured max caps are honored.
- Test flow-control violation detection is unchanged.

## Non-goals
- Do not change STREAM receive reassembly.
- Do not add adaptive transmit credit; peer controls that.
- Do not optimize beyond measured or clearly expected workload needs.

## Options and Open Questions
- Option: leave fixed windows if performance testing shows no meaningful gap for target deployments.
- Open question: default adaptive policy should depend on product workload expectations not yet encoded in repo.

## Acceptance Criteria
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
- `make -C zquic/test test` must `PASS`
- `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
- Decision is documented with test/measurement rationale.
- If adaptive windows are added, tests cover growth, caps, and unchanged violation handling.
- Existing flow-control tests still pass.

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
