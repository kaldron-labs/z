## Requirements
- Allow multiple control frames and multiple stream frames per packet when budget permits.
- Coalesce Initial and Handshake packets when useful during handshake.
- Use dynamic packet number length based on largest acknowledged packet.

## Summary
Preconditions: plans 1 through 6 complete.

`zquic` already has packet builders, control queues, stream frame builders, and ACK appending.  Runtime packet assembly is conservative: one queued control frame and at most one stream frame per packet, with fixed packet number length.  This plan makes packet assembly budget-driven while preserving existing frame priority.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

## Architecture Documentation
New or changed components: add a packet assembly loop that fills `PktBuild` until packet, congestion, path, or flow budget is exhausted.

New or changed processes or threads: Tx path remains the sole packet assembly owner.

New or changed interfaces: internal frame builders should report consumed budget and whether more work remains.

New or changed data flows: scheduler repeatedly pulls control/retransmit/stream work into one packet before sending.

New or changed event-driven or timer processing: no new timers; ACK/loss/PTO timers may schedule sends that now pack multiple frames.

New or changed network programming: long-header coalescing emits multiple protected packets into one UDP datagram where allowed.

New or changed data stores: track largest acknowledged packet per space for packet number length selection.

## Detailed Design and Implementation Plan
### Phase 1: Dynamic PN Length
- Add helper to choose packet number length from current PN and largest ACKed PN.
- Replace `RuntimePNLength = 2` use in runtime packet sends.
- Test edge cases around PN length expansion.

### Phase 2: Multi-Frame Assembly
- Refactor control and stream builders to be loopable and budget-aware.
- Preserve priority: retransmission/control before new stream data unless existing policy says otherwise.
- Avoid heap allocations in the hot path; use existing `PktBuild`.

### Phase 3: Coalescing
- Add handshake send path that can write Initial and Handshake packets into one UDP datagram when both are ready and budget allows.
- Keep packet protection and sent-packet records per packet, not per datagram.

### Phase 4: Test Updates
- Update tests that assumed one frame per packet to assert priority and byte validity instead.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:2592` - Client short packet builder currently sends constrained frame sets.
- `zquic/src/Zquic.hh:4719` - Server packet builder mirrors client behavior.
- `zquic/src/Zquic.hh:3006` - Sent-packet scheduling currently uses fixed runtime PN length behavior.
- `zquic/src/ZquicPacket.cc:355` - Short-header writer accepts packet number length.
- `zquic/test/ZquicRuntimeTest.cc` - Runtime packet-count assertions need priority-focused updates.

## Detailed Test Plan
- Add codec/runtime tests for multiple control frames in one packet.
- Add runtime tests for multiple stream frames in one packet across stream IDs.
- Add PN length tests for 1, 2, 3, and 4 byte encodings.
- Add handshake coalescing test that validates two protected packets inside one datagram.

## Non-goals
- Do not implement a full ngtcp2 scheduler clone.
- Do not add DATAGRAM scheduling; plan 17 covers that.
- Do not relax congestion/path/flow budgets.

## Options and Open Questions
- Option: build a small frame-priority iterator or keep explicit loops in send functions.  Prefer explicit loops first to match current code.
- Open question: whether coalescing should be limited to handshake until path budgeting is complete.

## Acceptance Criteria
- Tests cover multiple frames in one protected packet.
- Packet number length shrinks/grows correctly from ACK state.
- Existing one-frame scheduling tests are updated to assert priority, not artificial packet count.
