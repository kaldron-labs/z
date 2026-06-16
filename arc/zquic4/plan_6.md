## Requirements
- Preserve ACK_ECN counters when parsing and writing ACK frames.
- Track received ECN marks per packet space.
- Add minimal ECN validation state and disable ECN on validation failure.

## Summary
Preconditions: plan 5 complete so ACK state has runtime ownership and commit semantics.

`zquic` currently parses ACK_ECN only enough to skip the counters; `Frame` does not retain them.  `Path` already has an `ecnDisabled` flag.  This plan carries ECN counts through frame parsing, ACK generation, and minimal validation without adding a full ECN congestion response.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: extend `Frame` with ACK_ECN counters and add per-space ECN counters to ACK state.

New or changed processes or threads: Rx observes ECN marks from packet/socket metadata and updates per-space counters; Tx emits and validates ACK_ECN.

New or changed interfaces: add optional ECN mark input to packet receive and ACK generation.

New or changed data flows: received packet ECN marks accumulate in ACK state; outgoing ACK frames include counts; received ACK_ECN validates sent ECN use.

New or changed event-driven or timer processing: none beyond ACK timer behavior from plan 5.

New or changed network programming: endpoint/socket layer must expose ECN bits where supported; unsupported platforms keep ECN disabled.

New or changed data stores: per-space ECT0, ECT1, and CE counters plus validation state.

## Detailed Design and Implementation Plan
### Phase 1: Frame Representation
- Add ECT0, ECT1, and CE fields to `Frame`.
- Update `Frame::reset()` and parse/write tests.
- Keep storage inline; no heap allocation.

### Phase 2: Codec Preservation
- Modify ACK parsing so ACK_ECN counters are retained.
- Add `FrameCodec::writeAckECN` or extend `writeAckRanges` with optional counters without hurting the common non-ECN path.

### Phase 3: Runtime Counters
- Track received ECN marks per packet space in ACK state.
- Include counters in ACK frames only when ECN is enabled/observed.

### Phase 4: Validation
- Add minimal validation that disables ECN on impossible counter regressions or inconsistent feedback.
- Use `Path::setEcnDisabled()` for path-level disablement.

## Code References to Impacted Code
- `zquic/src/ZquicFrame.hh:20` - `Frame` structure needs ACK_ECN fields.
- `zquic/src/ZquicFrame.cc:13` - ACK frame parsing/writing currently handles ACK types.
- `zquic/src/ZquicPath.hh:48` - Existing `ecnDisabled` state to reuse.
- `zquic/src/ZquicRecovery.hh:172` - ACK manager should own outgoing counters.
- `zquic/test/ZquicCodecTest.cc` - Add ACK_ECN codec tests.

## Detailed Test Plan
- Add codec tests for ACK_ECN parse/write with ECT0, ECT1, and CE.
- Add runtime tests where received ECN marks appear in outgoing ACK_ECN.
- Add validation tests that impossible ACK_ECN feedback disables ECN.
- Confirm non-ECN ACKs remain byte-for-byte compatible with existing tests.

## Non-goals
- Do not implement full ECN congestion response here.
- Do not require ECN socket support on platforms that do not expose it.
- Do not allocate per-packet ECN objects.

## Options and Open Questions
- Option: treat ECN as path-specific only or connection-plus-path specific.  Prefer path-specific because validation can fail per path.
- Open question: how much socket ECN plumbing is already available in `Zi`; inspect before adding any platform wrappers.

## Acceptance Criteria
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
  - test suites must run address and leak sanitized
- ACK_ECN parse/write tests cover ECT0, ECT1, and CE counts.
- ECN counters are surfaced to congestion/control logic.
- Non-ECN behavior remains unchanged when ECN is disabled.

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
