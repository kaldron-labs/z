## Requirements
- Add 0-RTT packet protection state.
- Validate transport parameter compatibility before accepting early data.
- Accept or reject early stream data under application control.
- Clean up early data state on rejection.

## Summary
Preconditions: plans 1 through 16 complete.

`zquic` recognizes 0-RTT packet type and has a reject helper/test, but runtime long-header receive does not process 0-RTT packets.  This plan adds real 0-RTT packet handling for stream data while keeping replay-sensitive policy under application control.

## Architecture Documentation
New or changed components: add early-data crypto state, remembered transport parameter compatibility checks, and early stream admission policy.

New or changed processes or threads: Rx decrypts and classifies 0-RTT; app policy determines whether early data is accepted.

New or changed interfaces: add CRTP/config hooks for early data acceptance and replay-sensitive operation gating.

New or changed data flows: 0-RTT packets decrypt with early secrets, pass frame legality checks, then feed stream receive state if accepted.

New or changed event-driven or timer processing: key discard/early-data cleanup uses timer/lifecycle hooks from plan 1.

New or changed network programming: long-header receive path accepts `PktType::ZeroRTT` where keys and policy allow.

New or changed data stores: retain early transport parameters and cleanup state until handshake confirms or rejects 0-RTT.

## Detailed Design and Implementation Plan
### Phase 1: Crypto State
- Extend crypto setup for 0-RTT read/write secrets where zpicotls exposes early data.
- Keep packet protection APIs parallel to Initial/Handshake/1-RTT.

### Phase 2: Transport Parameter Compatibility
- Store prior session transport parameters needed for early data.
- Reject 0-RTT if current parameters are incompatible with remembered limits.

### Phase 3: Runtime Receive
- Accept `PktType::ZeroRTT` in long-header receive when early keys exist.
- Enforce 0-RTT frame legality and stream/flow limits.
- Drop or buffer according to QUIC rules when handshake state is not ready.

### Phase 4: Rejection Cleanup
- On rejection, discard early keys and early stream state.
- Notify app through plan 13 callbacks/diagnostics.

## Code References to Impacted Code
- `zquic/src/ZquicPacket.cc:135` - 0-RTT packet type parsing exists.
- `zquic/src/ZquicCrypto.cc:804` - Existing 0-RTT rejection diagnostic.
- `zquic/src/ZquicCrypto.cc:1131` - `rejectZeroRTT()` helper.
- `zquic/src/Zquic.hh:3184` - Long-header decrypt/receive path.
- `zquic/test/ZquicHandshakeTest.cc:390` - Existing 0-RTT drop/reject test.

## Detailed Test Plan
- Test accepted 0-RTT stream data under app policy.
- Test rejected 0-RTT discards early state and reports diagnostics.
- Test incompatible transport parameters reject early data.
- Test illegal 0-RTT frames are rejected.

## Non-goals
- Do not guarantee replay safety for applications; expose policy hooks and document requirements.
- Do not add QUIC DATAGRAM early data unless a future backlog item adds DATAGRAM.
- Do not implement session ticket persistence beyond what is required for tests.

## Options and Open Questions
- Option: server may drop 0-RTT until handshake confirms support or process immediately under strict limits.  Prefer strict immediate processing when keys and policy exist.
- Open question: how much session resumption state zpicotls integration already exposes.

## Acceptance Criteria
- Implementation is audited and aligned with `GUIDELINES.md`
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
  - test suites must run address and leak sanitized
- Tests cover accepted 0-RTT, rejected 0-RTT, transport parameter incompatibility, and early packet discard.

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
