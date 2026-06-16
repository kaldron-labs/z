## Requirements
- Add targeted packet-loss handshake tests, ACK delay tests, anti-amplification tests, migration tests, PMTUD tests, and close/draining tests.
- Add negative tests for malformed frames and transport parameter violations.
- Ensure each P0-P5 feature has at least one regression test.

## Summary
Preconditions: plans 1 through 19 complete.

`zquic` already has broad unit and runtime tests, including recovery, PMTUD, streams, handshake, packet protection, and endpoint tests.  This plan fills gaps left by feature work and organizes regressions as standalone `zquic/test/*` binaries consistent with repository practice.

## Architecture Documentation
New or changed components: add test helpers for lossy datagrams, timer control, path migration, and malformed packet/frame injection.

New or changed processes or threads: tests should use existing runtime loop patterns and `libtool exec` where needed.

New or changed interfaces: expose narrow test hooks only if public behavior cannot be observed otherwise; prefer existing diagnostics.

New or changed data flows: test harness can drop/reorder/duplicate packets and inspect callbacks/diagnostics.

New or changed event-driven or timer processing: deterministic timer tests should avoid wall-clock flakiness where possible.

New or changed network programming: interop/runtime tests should use local UDP sockets or in-memory shims already present.

New or changed data stores: no production data stores.

## Detailed Design and Implementation Plan
### Phase 1: Test Matrix
- Map each plan 1-19 feature to at least one regression test.
- Identify existing tests that can be extended instead of adding new binaries.

### Phase 2: Harness Utilities
- Reuse runtime loss shim patterns from `ZquicRuntimeTest`.
- Add helpers for timer advancement, packet corruption, and transport parameter mutation.

### Phase 3: Negative Coverage
- Add malformed frame tests for illegal encodings and frame-space violations.
- Add transport parameter violation tests for invalid limits, Retry fields, and 0-RTT incompatibility.

### Phase 4: Integration
- Update `zquic/test/Makefile.am`.
- Keep tests deterministic and platform-aware for Linux and Windows via msys2/mingw.

## Code References to Impacted Code
- `zquic/test/ZquicRuntimeTest.cc` - Existing runtime handshake/loss patterns.
- `zquic/test/ZquicRecoveryTest.cc` - Recovery tests to extend.
- `zquic/test/ZquicPMTUDTest.cc` - PMTUD tests to extend.
- `zquic/test/ZquicStreamTest.cc` - Stream lifecycle tests.
- `zquic/test/Makefile.am` - Add new standalone test binaries.

## Detailed Test Plan
- Add dropped Initial/Handshake packet recovery tests.
- Add ACK delay and ACK-only packet tests.
- Add anti-amplification budget exhaustion tests.
- Add migration validation success/failure tests.
- Add PMTUD success/failure/blackhole tests.
- Add close/draining duration tests.
- Add malformed frame and transport parameter negative tests.

## Non-goals
- Do not add a top-level coverage target.
- Do not depend on external network services for default tests.
- Do not weaken production APIs just to make tests simpler.

## Options and Open Questions
- Option: one large runtime regression binary or several focused binaries.  Prefer focused binaries if build time remains reasonable.
- Open question: whether timer determinism requires a scheduler test mode; add one only if existing scheduler cannot be controlled reliably.

## Acceptance Criteria
- New tests run as standalone `zquic/test/*` binaries.
- Each P0-P5 feature has at least one regression test.
