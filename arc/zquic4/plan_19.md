## Requirements
- Add a congestion-controller abstraction after NewReno runtime integration is stable.
- Implement CUBIC first, then BBR if required.
- Wire `Pacer` to scheduler delays and packet bursts.

## Summary
Preconditions: plans 1 through 18 complete, especially plan 4 congestion runtime integration.

`zquic` already has `NewReno` and a `Pacer` utility appears in flow tests, but runtime pacing and alternate controllers are not integrated.  This plan adds controller selection and scheduling delays without disturbing the established NewReno path.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

## Architecture Documentation
New or changed components: add a static-dispatch congestion-controller wrapper for NewReno/CUBIC/future BBR and runtime pacer state.

New or changed processes or threads: Tx owns controller and pacer state.

New or changed interfaces: engine/link params select controller; internal send budget asks selected controller and pacer.

New or changed data flows: ACK/loss events feed selected controller; controller output feeds cwnd budget and pacing interval.

New or changed event-driven or timer processing: pacer uses scheduler delays to release bursts instead of busy looping.

New or changed network programming: packet bursts are capped by pacing and cwnd.

New or changed data stores: controller state remains inline; avoid virtual heap allocation for per-packet decisions.

## Detailed Design and Implementation Plan
### Phase 1: Controller Selection
- Add enum/config for NewReno and CUBIC.
- Use templates/CRTP or tagged union style, not virtual polymorphism.
- Keep NewReno as default.

### Phase 2: CUBIC
- Implement CUBIC state and ACK/loss transitions.
- Reuse sent-packet timestamps and RTT estimates from recovery.
- Add focused unit tests before runtime wiring.

### Phase 3: Pacing
- Wire existing `Pacer` or enhance it if needed.
- Schedule delayed Tx callbacks through `ZmScheduler` when pacing interval says wait.
- Ensure cwnd remains the hard cap.

### Phase 4: BBR Decision
- Add BBR only if required by interop/performance goals after CUBIC.
- If added, keep it behind the same controller selection interface.

## Code References to Impacted Code
- `zquic/src/ZquicRecovery.hh:258` - Existing NewReno controller.
- `zquic/test/ZquicFlowTest.cc:318` - Existing `Pacer` test reference.
- `zquic/src/ZquicSched.hh` - Scheduler/budget utilities.
- `zquic/src/Zquic.hh:2592` - Send budget integration point.
- `zquic/test/ZquicCongestionTest.cc` - Add CUBIC/pacing tests.

## Detailed Test Plan
- Unit-test controller selection.
- Unit-test CUBIC cwnd evolution and loss response.
- Runtime-test pacing interval delays packet bursts.
- Runtime-test NewReno remains default and unchanged.

## Non-goals
- Do not implement BBR unless explicitly required after CUBIC.
- Do not use virtual controller interfaces.
- Do not let pacing bypass cwnd or anti-amplification limits.

## Options and Open Questions
- Option: `ZtEnum` controller selection with switch dispatch or template policy.  Prefer switch dispatch if runtime configuration is required.
- Open question: whether pacing should be per connection or per path; start per connection and revisit for migration/multipath.

## Acceptance Criteria
- Tests cover controller selection, cwnd evolution, loss response, and pacing interval scheduling.
