## Requirements
- Retain old and new 1-RTT receive keys for the required key update window.
- Validate key phase transitions instead of installing next keys purely after decrypt success.
- Add key discard scheduling to the explicit key-discard timer.

## Summary
Preconditions: plans 1 through 13 complete.

`zquic` supports traffic secret updates and attempts next receive keys when short-packet decrypt fails.  The current behavior is optimistic and lacks a full old/new retention window and validated key phase state machine.  This plan hardens key update behavior while reusing existing crypto primitives.

## Architecture Documentation
New or changed components: add 1-RTT key phase state with current, next, and old receive secrets plus discard deadlines.

New or changed processes or threads: packet decrypt/update happens on Rx; key discard timer fires on the connection owner and clears obsolete state safely.

New or changed interfaces: internal crypto methods expose candidate next-key trial without immediately committing global state.

New or changed data flows: decrypt observes key phase, tries allowed key material, validates packet number ordering, then commits update.

New or changed event-driven or timer processing: key-discard timer from plan 1 schedules old-key removal.

New or changed network programming: short-header packet protection/deprotection enforces key phase rules.

New or changed data stores: retain bounded old/new secrets and discard deadlines; avoid heap churn on packet hot path.

## Detailed Design and Implementation Plan
### Phase 1: State Machine
- Define explicit key phases and allowed transitions.
- Store current and previous receive keys long enough to accept reordered packets.
- Prevent repeated toggles or invalid key phase jumps.

### Phase 2: Trial Decrypt
- Refactor next-key decrypt attempt so success returns a pending update decision.
- Commit peer key update only after packet number and phase validation.

### Phase 3: Discard
- Arm key-discard timer when old keys become discardable.
- Clear Initial/Handshake keys through the same timer ownership model where applicable.

### Phase 4: Tests and Diagnostics
- Add counters for valid peer update, invalid phase, old-key accepted, and key discard.

## Code References to Impacted Code
- `zquic/src/ZquicCrypto.cc:529` - Traffic secret update helpers.
- `zquic/src/Zquic.hh:2459` - Peer key update path currently installs after decrypt success.
- `zquic/src/Zquic.hh:3105` - Short packet protection/deprotection path.
- `zquic/src/ZquicPacketProtectionTest.cc` - Extend packet protection tests.
- `zquic/test/ZquicCryptoTest.cc` - Add key update state tests.

## Detailed Test Plan
- Test peer key update succeeds and old key remains valid for the required window.
- Test old-key packet tolerance during reordering.
- Test invalid key phase transitions are rejected.
- Test key discard timer removes obsolete keys and packets using them fail afterward.

## Non-goals
- Do not replace zpicotls integration.
- Do not support non-QUIC TLS key update semantics.
- Do not allocate per-packet key state.

## Options and Open Questions
- Option: keep key phase state in `Crypto` or in runtime packet protection.  Prefer `Crypto` if it already owns traffic secrets cleanly.
- Open question: exact discard duration should follow QUIC packet threshold/time guidance using existing RTT state.

## Acceptance Criteria
- Tests cover peer key update, old-key packet tolerance, invalid key phase, and key discard.
- Existing packet protection tests still pass.
