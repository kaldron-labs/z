## Requirements
- Update examples for new callbacks, 0-RTT, Retry policy, qlog, and path events.
- Document timer, recovery, and path-state ownership.
- Remove stale comments and APIs left by the work stack.

## Summary
Preconditions: plans 1 through 22 complete.

After the implementation stack, public examples and internal architecture notes need to match the new runtime behavior.  This plan cleans up documentation and APIs after behavior is complete, with dependent compatibility explicitly not a goal.

## Architecture Documentation
New or changed components: add or update documentation for timers, recovery, ACK management, path state, Retry, 0-RTT, qlog, and callbacks.

New or changed processes or threads: document Rx/Tx ownership and callback shard expectations.

New or changed interfaces: remove obsolete internal APIs and update examples to the final CRTP hook signatures.

New or changed data flows: document packet receive/send, ACK/loss, path validation, and PMTUD flows at a high level.

New or changed event-driven or timer processing: document the eight explicit `ZmScheduler::Timer` instances and their `Cxn`/`Link` ownership.

New or changed network programming: document anti-amplification, migration, Retry, and 0-RTT operational constraints.

New or changed data stores: document runtime-owned recovery/path/CID/key state where useful for maintainers.

## Detailed Design and Implementation Plan
### Phase 1: API Cleanup
- Remove stale shims, forwarders, or compatibility methods introduced temporarily during the work stack.
- Rename internal helpers for concise local style where necessary.
- Update dependents directly.

### Phase 2: Examples
- Update or add examples covering callbacks, Retry policy, qlog enablement, 0-RTT policy, and path events.
- Keep examples operational rather than marketing-oriented.

### Phase 3: Architecture Notes
- Document timer ownership and shard ownership.
- Document recovery packet-space behavior and retransmit ref lifetime.
- Document path/PMTUD/migration state transitions.

### Phase 4: Final Review
- Scan comments for stale statements like PTO-only recovery or one-frame packet assembly.
- Run tests and update build/test notes.

## Code References to Impacted Code
- `zquic/src/Zquic.hh` - Public/internal API surface and ownership comments.
- `zquic/src/ZquicRecovery.hh` - Recovery utilities and packet-space docs.
- `zquic/src/ZquicPath.hh` - Path/PMTUD state docs.
- `zquic/test/*` - Examples may reuse test app patterns.
- `README.md` and module docs if present - User-facing build/API notes.

## Detailed Test Plan
- Build all examples.
- Run `make test` or the relevant `zquic/test/*` binaries.
- Add a documentation lint/check only if the repo already has one.
- Compile downstream examples after removing stale APIs.

## Non-goals
- Do not preserve dependent compatibility through shims.
- Do not add broad tutorial content unrelated to implemented behavior.
- Do not document speculative features not implemented in plans 1-22.

## Options and Open Questions
- Option: add an architecture markdown file under `zquic/` or keep comments near code.  Prefer both: concise architecture doc plus local ownership comments.
- Open question: where project maintainers prefer API examples to live if no current examples directory exists.

## Acceptance Criteria
- Examples compile against the final API.
- Timer, recovery, and path ownership are documented.
- Stale comments/APIs from intermediate work are removed.
