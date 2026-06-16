## Requirements
- Track invalid duplicate and closed-stream activity enough to avoid unbounded work or repeated close storms.
- Map invalid stream activity to specific transport errors where applicable.
- Keep ordinary duplicate STREAM frames cheap and non-fatal.

## Summary
Preconditions: plans 1 through 11 complete.

`zquic` already validates many stream frames and ordinary duplicate STREAM data is handled by the receive queue.  The missing behavior is classification and throttling of repeated invalid activity on closed streams or suspicious peer behavior.  This plan adds a bounded glitch/rate mechanism without penalizing valid duplicates.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add per-connection suspicious-activity counters and lightweight per-stream closed-state checks.

New or changed processes or threads: Rx owns receive-side classification; close signaling posts to Tx as needed.

New or changed interfaces: internal helpers return explicit transport error classifications instead of bare failure where possible.

New or changed data flows: frame receive validates stream state, classifies invalid activity, increments counters, and either ignores, reports, or closes.

New or changed event-driven or timer processing: optional decay can use idle/lifecycle timers, but deterministic bounded counters are sufficient initially.

New or changed network programming: repeated invalid remote behavior leads to one deterministic close, not repeated close storms.

New or changed data stores: compact counters by error class; avoid per-offense heap allocation.

## Detailed Design and Implementation Plan
### Phase 1: Classify Existing Failures
- Audit stream receive paths returning failure for MAX_STREAM_DATA, RESET_STREAM, STREAM_DATA_BLOCKED, and final-size violations.
- Replace ambiguous booleans with internal error enums where needed.

### Phase 2: Suspicious Activity Counters
- Add bounded counters on the connection for invalid closed-stream activity.
- Keep ordinary duplicate STREAM frame handling in `ZmPQueue` unchanged and cheap.

### Phase 3: Close/Ignore Policy
- Define which invalid frames are ignored, which are transport errors, and which trip the suspicious threshold.
- Ensure only one close frame is queued for repeated activity.

### Phase 4: Diagnostics
- Increment diagnostics for invalid activity and threshold trips.
- Surface specific errors to plan 13 callbacks.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:1542` - STREAM receive and flow-control validation.
- `zquic/src/Zquic.hh:1573` - RESET_STREAM receive validation.
- `zquic/src/Zquic.hh:1588` - STREAM_DATA_BLOCKED validation.
- `zquic/src/Zquic.hh:1597` - STOP_SENDING receive validation.
- `zquic/src/ZquicPQueue.hh` - Duplicate STREAM reassembly should remain the fast path.
- `zquic/test/ZquicStreamTest.cc` - Add closed-stream and invalid-activity cases.

## Detailed Test Plan
- Test closed-stream MAX_STREAM_DATA classification.
- Test closed-stream RESET_STREAM and duplicate-final-size behavior.
- Test closed-stream STREAM_DATA_BLOCKED classification.
- Test ordinary duplicate STREAM frames remain non-fatal and cheap.
- Test repeated invalid activity trips a deterministic close/error path once.

## Non-goals
- Do not add heavyweight per-peer abuse tracking.
- Do not change full receive reassembly behavior.
- Do not hide protocol errors behind compatibility shims.

## Options and Open Questions
- Option: fixed threshold counters or time-decayed counters.  Prefer fixed deterministic thresholds first.
- Open question: exact threshold values; choose small named constants with comments, not unexplained literals.

## Acceptance Criteria
- Implementation is audited and aligned with `GUIDELINES.md`
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
  - test suites must run address and leak sanitized
- Tests cover closed-stream MAX_STREAM_DATA, RESET_STREAM, STREAM_DATA_BLOCKED, and duplicate-final-size cases.
- Repeated invalid activity trips a deterministic internal/protocol error path.

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
