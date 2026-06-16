## Requirements
- Add scheduler paths for local RESET_STREAM and STOP_SENDING requests.
- Track ACK/loss of these frames and retransmit while still valid.
- Update stream state transitions and callbacks.

## Summary
Preconditions: plans 1 through 10 complete.

`zquic` already parses and writes RESET_STREAM and STOP_SENDING, and receive-side stream tests cover peer reset/stop behavior.  The gap is local emission: application-initiated reset/stop must enqueue frames, record sent refs, and retransmit on loss while the state remains valid.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add control-frame queue entries for local stream reset and stop-sending.

New or changed processes or threads: stream API calls dispatch to the owning shard; Tx owns frame emission and retransmission.

New or changed interfaces: expose or complete local stream methods for reset and stop-sending requests.

New or changed data flows: app request updates stream state, queues control frame, send path emits frame, ACK/loss updates validity/requeue.

New or changed event-driven or timer processing: existing loss/PTO retransmission from plans 2-3 handles retransmit scheduling.

New or changed network programming: RESET_STREAM and STOP_SENDING frames share packet budgets with other control frames.

New or changed data stores: sent frame refs need enough data to rebuild reset/stop frames.

## Detailed Design and Implementation Plan
### Phase 1: Local API and State
- Audit existing stream methods for reset/stop intent.
- Add or complete CRTP-facing local methods that set stream state and queue control frames.
- Validate final size for RESET_STREAM.

### Phase 2: Control Queue Encoding
- Extend `ControlFrame`/`SentFrameRef` as needed to carry RESET_STREAM and STOP_SENDING fields.
- Reuse `FrameCodec::writeResetStream` and `writeStopSending`.

### Phase 3: ACK/Loss Handling
- On ACK, mark frame delivery complete.
- On loss, requeue if the stream state still requires the peer to see the signal.
- Suppress stale retransmits after stream state makes the frame irrelevant.

### Phase 4: Callbacks
- Fire reset/stop callbacks added in plan 13, or add temporary internal hooks if plan 13 is not yet implemented.

## Code References to Impacted Code
- `zquic/src/ZquicFrame.hh:58` - RESET_STREAM and STOP_SENDING writers exist.
- `zquic/src/Zquic.hh:1916` - Stream control receive handling.
- `zquic/src/Zquic.hh:2516` - Control frame to sent-ref conversion.
- `zquic/src/Zquic.hh:2829` - Control retransmit builder.
- `zquic/test/ZquicStreamTest.cc:747` - Existing peer reset/stop receive tests.

## Detailed Test Plan
- Test local RESET_STREAM send from application API.
- Test local STOP_SENDING send from application API.
- Test ACK clears retransmission obligation.
- Test loss requeues and retransmits the frame.
- Test final-size validation remains intact for peer RESET_STREAM.

## Non-goals
- Do not redesign stream reassembly.
- Do not add application callback richness beyond what plan 13 defines.
- Do not preserve old APIs if a cleaner stream-control API is needed.

## Options and Open Questions
- Option: represent reset/stop as normal `ControlFrame` entries or as stream-owned pending flags pulled by scheduler.  Prefer stream-owned flags if it avoids stale queued frames.
- Open question: exact stale suppression rules for STOP_SENDING after a peer reset.

## Acceptance Criteria
- Implementation is audited and aligned with `GUIDELINES.md`
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
  - test suites must run address and leak sanitized
- Tests cover local RESET_STREAM send, peer RESET_STREAM receive, local STOP_SENDING send, peer STOP_SENDING receive, and retransmission after loss.
- Reset final-size validation remains intact.

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
