## Requirements
- Audit `SentFrameRef` shallow range references and retransmit queue lifetime.
- Ensure retransmission never references freed packet or stream buffers.
- Add bounded queue/drop or stale-filter policy.

## Summary
Preconditions: plans 1 through 20 complete.

`zquic` retransmission uses compact `SentFrameRef` records, including stream ranges and small payload copies for path response.  This is efficient, but the audit flagged shallow references and unbounded retransmit queue risk.  This plan validates lifetime assumptions after recovery/scheduler work and hardens stale retransmission filtering.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add retransmit queue bounds, stale checks, and possibly stronger frame-ref ownership for risky frame kinds.

New or changed processes or threads: Tx owns retransmit queue and stream Tx buffers.

New or changed interfaces: internal helpers decide whether a `SentFrameRef` is still valid before rebuild.

New or changed data flows: loss/PTO enqueues refs; retransmit builder validates against current stream/control state before writing.

New or changed event-driven or timer processing: PTO/loss timers may enqueue many refs; queue bounds prevent unbounded growth.

New or changed network programming: retransmitted frames are rebuilt from live state, not stale packet memory.

New or changed data stores: bounded retransmit queue with counters for dropped stale refs.

## Detailed Design and Implementation Plan
### Phase 1: Lifetime Audit
- Trace each `SentFrameRef::kind` from send record to retransmit rebuild.
- Verify stream ranges refer to stream-owned Tx buffers that remain live until ACK/drop.
- Verify control payloads are copied when source memory is transient.

### Phase 2: Stale Filtering
- Strengthen `controlStillValid_()` and stream range validation.
- Drop refs for closed streams, obsolete flow updates, retired CIDs, expired path responses, or discarded crypto spaces.

### Phase 3: Queue Bounds
- Add a named maximum or byte-based bound for retransmit queue growth.
- Prefer dropping stale/duplicate refs before rejecting new valid refs.
- Add diagnostics for bounded drops.

### Phase 4: Tests
- Create loss storms with stream close/reset and verify no stale retransmits.

## Code References to Impacted Code
- `zquic/src/ZquicRecovery.hh:339` - `SentFrameRef` structure.
- `zquic/src/ZquicRecovery.hh:673` - Lost packet ref enqueue.
- `zquic/src/Zquic.hh:2803` - Runtime retransmit dequeue.
- `zquic/src/Zquic.hh:2818` - Stream retransmit rebuild.
- `zquic/src/Zquic.hh:2829` - Control retransmit rebuild.

## Detailed Test Plan
- Unit-test each frame ref kind for valid and stale rebuild behavior.
- Runtime-test stream buffer freed/acked before queued retransmit is skipped safely.
- Runtime-test queue bound under repeated loss.
- Runtime-test discarded Initial/Handshake crypto refs are not retransmitted.

## Non-goals
- Do not replace compact refs with wholesale packet copies unless audit proves it necessary.
- Do not make retransmission lossless for obsolete control frames.
- Do not add locks around Tx-owned queues.

## Options and Open Questions
- Option: frame-chain ownership like ngtcp2 or compact validated refs.  Prefer compact refs if lifetime tests prove them safe.
- Open question: correct bound should be packet-count, frame-count, or byte-estimate based.  Start with named frame-count bound and diagnostics.

## Acceptance Criteria
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
- `make -C zquic/test test` must `PASS`
- `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
- Tests prove stream retransmit refs remain valid or are skipped after stream close/reset/free.
- Retransmit queue growth is bounded under repeated loss.
- No ASAN/valgrind lifetime errors in retransmission tests.

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
