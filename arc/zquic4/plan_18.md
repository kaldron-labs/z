## Requirements
- Add qlog writer hooks for packet sent/received, frames, ACK/loss, congestion metrics, path validation, PMTUD, key update, close, and stateless reset.
- Keep existing counters.
- Make qlog optional and zero-cost when disabled.

## Summary
Preconditions: plans 1 through 17 complete.

`zquic` has diagnostics counters and debug log points but no qlog event stream.  This plan adds an optional structured event sink designed to compile and run with no hot-path allocation when disabled.

## Architecture Documentation
New or changed components: add qlog event structs and a CRTP/app-provided writer hook.

New or changed processes or threads: event emission occurs on the owning shard; writer must copy data it needs if it crosses threads.

New or changed interfaces: add optional hooks such as `qlogEvent(...)` with safe base no-op defaults.

New or changed data flows: packet/frame/recovery/path/security events produce compact event records.

New or changed event-driven or timer processing: timer callbacks emit qlog events for expiry/firing outcomes.

New or changed network programming: packet sent/received events include addresses/path IDs without retaining transient buffers.

New or changed data stores: optional writer state belongs to the app; runtime stores only enable flags or no state.

## Detailed Design and Implementation Plan
### Phase 1: Event Model
- Define compact event types for packet, frame, ACK/loss, congestion, path, PMTUD, keys, close, and reset.
- Use `ZtString`/`ZeString` only in cold formatting paths.

### Phase 2: Disabled Fast Path
- Implement compile-time or runtime disabled checks so no allocations occur when qlog is off.
- Avoid constructing large event payloads unless enabled.

### Phase 3: Writer Hook
- Add CRTP default no-op writer.
- Provide a simple JSON/qlog writer helper if it can be kept out of hot paths.

### Phase 4: Coverage
- Emit events at packet send/receive, frame parse/write, ACK/loss transitions, congestion updates, path validation, PMTUD, key updates, close, and stateless reset.

## Code References to Impacted Code
- `zquic/src/ZquicDiag.hh` - Existing diagnostics counters to keep and extend.
- `zquic/src/Zquic.hh:2860` - Sent-packet event point.
- `zquic/src/Zquic.hh:3184` - Received packet event point.
- `zquic/src/ZquicFrame.cc:11` - Frame parse/write event boundaries.
- `zquic/test/ZquicRuntimeTest.cc` - Add qlog handshake/stream coverage.

## Detailed Test Plan
- Test qlog emits parseable events for handshake and stream transfer.
- Test ACK/loss and congestion events appear under induced loss.
- Test disabled qlog performs no heap allocations in packet hot path, using available allocation instrumentation or a test writer counter.
- Test event ordering around close/stateless reset.

## Non-goals
- Do not replace existing diagnostics counters.
- Do not require qlog for normal operation.
- Do not add heavy JSON formatting to packet hot paths.

## Options and Open Questions
- Option: event structs plus external JSON writer, or direct JSON emission.  Prefer event structs with optional writer.
- Open question: whether repository already has a preferred JSON streaming helper beyond `ZtJSON`.

## Acceptance Criteria
- Tests verify qlog emits parseable events for a handshake and stream transfer.
- Disabled qlog adds no allocations in the packet hot path.
