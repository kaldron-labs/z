## Requirements
- Add PATH_CHALLENGE generation, PATH_RESPONSE matching, validation timeout, and path promotion.
- Bind DCIDs/tokens to paths enough to distinguish current, validating, and retired paths.
- Handle NAT rebinding as path validation rather than packet failure.

## Summary
Preconditions: plans 1 through 8 complete.

`zquic` already parses PATH_CHALLENGE/PATH_RESPONSE and queues PATH_RESPONSE on receive.  It also has CID tracking and reset tokens.  Missing behavior is the path-validation state machine: candidate paths, challenge generation, response matching, timeout, promotion, and safe fallback.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

## Architecture Documentation
New or changed components: add candidate path state to `Link`, including challenge payload, validation deadline, associated CID, and previous active path.

New or changed processes or threads: Rx identifies remote-address changes; Tx owns challenge send, timeout, and promotion.

New or changed interfaces: add internal `startPathValidation_`, `onPathResponse_`, `promotePath_`, and `failPathValidation_` helpers.

New or changed data flows: new address receive creates/refreshes validating path; Tx sends PATH_CHALLENGE; PATH_RESPONSE validates and promotes.

New or changed event-driven or timer processing: use the `Link` path-validation timer from plan 1.

New or changed network programming: send PATH_CHALLENGE on candidate address without silently moving application traffic before validation.

New or changed data stores: maintain active, validating, and retired path/CID associations.

## Detailed Design and Implementation Plan
### Phase 1: Candidate Path Model
- Extend `Link` with active and validating `Path` state.
- Store random 8-byte PATH_CHALLENGE data in the validating path state.
- Keep old active path usable until validation succeeds or times out.

### Phase 2: Challenge/Response
- On new remote address or migration request, send PATH_CHALLENGE.
- Match incoming PATH_RESPONSE payload against the outstanding challenge.
- Reject or ignore unmatched responses without closing the connection unless protocol rules require it.

### Phase 3: CID Binding
- Associate path state with the selected destination CID/reset token sequence.
- Retire path-bound CIDs only after the path state no longer needs them.

### Phase 4: Timeout and Promotion
- Arm the path-validation timer.
- On timeout, retain old path and mark candidate failed.
- On success, promote candidate path and update send routing.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:4417` - Server currently responds to PATH_CHALLENGE.
- `zquic/src/Zquic.hh:5014` - Client/server receive handling mirrors path challenge behavior.
- `zquic/src/Zquic.hh:2208` - Local CID retirement and route association.
- `zquic/src/ZquicFrame.cc:188` - PATH_CHALLENGE/PATH_RESPONSE parsing exists.
- `zquic/src/ZquicPath.hh:33` - `Path` stores per-path accounting and validation state.

## Detailed Test Plan
- Runtime-test successful PATH_CHALLENGE/PATH_RESPONSE validation and path promotion.
- Runtime-test validation timeout keeps the old active path.
- Runtime-test short packets from a new address do not change active send address before validation.
- Unit-test unmatched PATH_RESPONSE and repeated challenges.

## Non-goals
- Do not implement full preferred-address support unless needed for path promotion primitives.
- Do not add multipath.
- Do not make migration callbacks rich; plan 13 exposes application events.

## Options and Open Questions
- Option: represent validating path as a single member first, then expand to a small container if simultaneous probes are required.
- Open question: whether NAT rebinding should preserve the existing DCID until validation or immediately switch to an unused peer CID.  Start conservative and bind a candidate CID before promotion.

## Acceptance Criteria
- Tests cover successful PATH_CHALLENGE/PATH_RESPONSE validation.
- Tests cover validation timeout and retained old path.
- Short packets on a new address do not silently replace the active path before validation.
