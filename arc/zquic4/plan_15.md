## Requirements
- Enforce token uniqueness for peer-issued CIDs.
- Associate stateless reset tokens with path/DCID state.
- Retire and tombstone CIDs with timer-based cleanup.

## Summary
Preconditions: plans 1 through 14 complete.

`zquic` already parses NEW_CONNECTION_ID/RETIRE_CONNECTION_ID, stores local/peer CIDs, and verifies stateless reset tokens.  The missing work is stronger lifecycle validation: uniqueness checks, path/DCID association, and timer-based cleanup of retired/tombstoned CIDs.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

## Architecture Documentation
New or changed components: enhance CID store entries with token uniqueness state, path association, and retire cleanup deadline.

New or changed processes or threads: route table updates remain owner-thread operations; Tx sends retire frames as needed.

New or changed interfaces: internal helpers validate CID/token combinations before installing them.

New or changed data flows: NEW_CONNECTION_ID updates peer CID store; path selection binds a CID; retirement queues frame and later tombstones routes.

New or changed event-driven or timer processing: use lifecycle/key-style timer ownership for CID cleanup if a dedicated stale-CID timer is not added.

New or changed network programming: stateless reset lookup uses path/DCID-associated tokens for unknown short packets.

New or changed data stores: CID entries store sequence, CID, reset token, state, association, and cleanup deadline.

## Detailed Design and Implementation Plan
### Phase 1: Token Validation
- On NEW_CONNECTION_ID, reject duplicate reset tokens associated with different CIDs where QUIC forbids it.
- Validate duplicate sequence/CID/token combinations deterministically.

### Phase 2: Path Association
- Bind selected peer DCID/reset token to active or validating path.
- Keep route entries alive while path state can receive packets for them.

### Phase 3: Retire/Tombstone Lifecycle
- Add cleanup deadlines for retired CIDs.
- Use timer-based tombstone cleanup rather than immediate route removal when late packets could arrive.

### Phase 4: Stateless Reset
- Ensure unknown short CID reset detection uses the correct token store.
- Surface stateless reset via plan 13 callback/diagnostics.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:2208` - Local CID retirement.
- `zquic/src/Zquic.hh:2290` - CID add/update helper.
- `zquic/src/Zquic.hh:2399` - Peer reset token transport parameter handling.
- `zquic/src/Zquic.hh:2479` - Stateless reset handling.
- `zquic/test/ZquicCIDTest.cc` - Extend CID lifecycle tests.

## Detailed Test Plan
- Test duplicate sequence/CID/token combinations.
- Test token uniqueness violations are rejected.
- Test retired CID routing and later tombstone cleanup.
- Test stateless reset on unknown short CID maps to the expected connection event.

## Non-goals
- Do not implement multipath CID pools beyond what plan 9 needs.
- Do not keep legacy route behavior for compatibility.
- Do not add heap-heavy CID maps if existing arrays suffice.

## Options and Open Questions
- Option: preserve current fixed/builtin CID storage or move to a small `ZmHash`.  Prefer existing storage until scaling requires a hash.
- Open question: cleanup timer ownership could be a separate CID timer or folded into draining/lifecycle timer.  Avoid overloading key-discard semantics.

## Acceptance Criteria
- Tests cover duplicate sequence/CID/token combinations, retired CID routing, and stateless reset on unknown short CID.
