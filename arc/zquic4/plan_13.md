## Requirements
- Add explicit callbacks or CRTP hooks for stream open, data, reset, stop-sending, flow blocked, transport close, stateless reset, path update, and migration failure.
- Ensure default hooks are harmless but diagnostics make unhandled events visible.

## Summary
Preconditions: plans 1 through 12 complete.

`zquic` already uses CRTP hooks and no-op defaults in several places.  The application event surface is incomplete and some unsupported events disappear silently.  This plan expands the CRTP callback surface using local style: safe base defaults, direct `impl()` calls, and no virtual dispatch.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

## Architecture Documentation
New or changed components: extend base app/link/stream classes with event hooks and diagnostics counters.

New or changed processes or threads: callbacks fire on the owning shard unless an existing API pattern dispatches otherwise.

New or changed interfaces: add concise CRTP hook names for stream/control/path/security events.

New or changed data flows: runtime state transitions call hooks after internal state is updated and before data is discarded.

New or changed event-driven or timer processing: timer-driven close/drain, path validation, and key events call corresponding hooks.

New or changed network programming: path and migration outcomes become visible to applications.

New or changed data stores: diagnostics add counters for unhandled/defaulted events.

## Detailed Design and Implementation Plan
### Phase 1: Hook Inventory
- Enumerate existing app/link/stream hooks and default methods.
- Add missing hooks with harmless defaults: return true, no-op, or nullptr as appropriate.
- Avoid concepts or `requires`; use established CRTP/base defaults.

### Phase 2: Stream Events
- Wire stream open, data ready, reset received/sent, stop-sending received/sent, and blocked events.
- Ensure callbacks see stable stream IDs and error/final-size data.

### Phase 3: Connection and Path Events
- Wire transport close, stateless reset, path update, and migration failure.
- Keep callbacks short and non-owning; do not pass references to temporary buffers across shards.

### Phase 4: Diagnostics
- Increment counters for events that hit default handlers.
- Add optional logging with copy-safe `ZiLOG` captures only.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:1163` - Existing base default callback style.
- `zquic/src/Zquic.hh:2164` - Stateless reset hook currently empty.
- `zquic/src/Zquic.hh:1916` - Stream reset/stop receive path.
- `zquic/src/Zquic.hh:3571` - Control receive path for path/handshake/close events.
- `zquic/src/ZquicDiag.hh` - Diagnostics counters to extend.
- `zquic/test/ZquicAPITest.cc` - Add callback coverage.

## Detailed Test Plan
- Test each callback fires with expected metadata.
- Test default hooks compile and are harmless.
- Test diagnostics increment for unhandled events.
- Test callbacks run on expected shard by using test app assertions.

## Non-goals
- Do not add virtual interfaces.
- Do not preserve dependent source compatibility if hook signatures need to change.
- Do not add DATAGRAM callbacks before plan 17.

## Options and Open Questions
- Option: group callback metadata into small structs to keep signatures short.  Prefer structs if signatures would exceed local naming/readability constraints.
- Open question: whether callbacks should be allowed to synchronously queue sends; follow existing stream API precedent.

## Acceptance Criteria
- Tests verify hooks fire on each control path.
- Existing applications compile with defaults.
