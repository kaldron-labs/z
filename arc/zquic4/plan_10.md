## Requirements
- Send PMTUD probes using `Path::startProbe`.
- Mark PMTUD probe packets so loss does not reduce cwnd.
- Handle probe ACK, loss, expiry, kernel/send-too-big hints, and blackhole fallback.

## Summary
Preconditions: plans 1 through 9 complete.

`Path` already implements PMTUD state transitions, probe attempts, hints, blackhole fallback, and tests.  Runtime currently does not send PMTUD probes or classify probe packets.  This plan wires the existing `Path` PMTUD utility into packet sending, ACK/loss recovery, and socket hints.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add runtime PMTUD probe scheduling and sent-packet metadata for PMTUD probes.

New or changed processes or threads: Tx starts probes and handles ACK/loss/expiry; Rx/socket path reports kernel hints as fixed data to Tx.

New or changed interfaces: add internal `schedulePMTUD_`, `sendPMTUDProbe_`, `onPMTUDProbeAcked_`, and `onPMTUDProbeLost_` helpers.

New or changed data flows: active path chooses next probe size; send path emits padded ack-eliciting probe; recovery tags sent packet; ACK/loss updates path.

New or changed event-driven or timer processing: use the `Link` PMTUD timer from plan 1 for retry/expiry cadence.

New or changed network programming: use socket path hints from `Sock::pathHint` and send-too-big errors to update path MTU state.

New or changed data stores: sent-packet record needs a PMTUD-probe flag and probe size.

## Detailed Design and Implementation Plan
### Phase 1: Probe Packet Classification
- Extend sent-packet metadata with a PMTUD probe flag and size.
- Ensure probe loss calls `NewReno::lost(..., true)` so cwnd is not reduced.

### Phase 2: Probe Send
- Use `Path::startNextProbe()` or `startProbeChecked()` to reserve a probe size.
- Emit a probe packet padded to the target UDP payload and containing an ack-eliciting frame.
- Respect anti-amplification and cwnd/path budget.

### Phase 3: ACK/Loss/Expiry
- On ACK of probe packet, call `Path::probeAcked()`.
- On loss, call `Path::probeLost()`.
- On PMTUD timer expiry, call `Path::probeExpired()` and schedule retry if requested.

### Phase 4: Hints and Blackholes
- Feed `Sock::pathHint()` and send-too-big errors into `Path::applyHint()`.
- Detect blackhole fallback and reduce active packet size.

## Code References to Impacted Code
- `zquic/src/ZquicPath.hh:91` - Existing probe sizing and PMTUD state machine.
- `zquic/src/ZquicSock.cc:194` - Socket path hints.
- `zquic/src/Zquic.hh:2860` - Sent-packet recording needs PMTUD classification.
- `zquic/src/Zquic.hh:2887` - ACK/loss path updates probe outcome.
- `zquic/test/ZquicPMTUDTest.cc` - Existing PMTUD unit tests to extend into runtime.

## Detailed Test Plan
- Runtime-test successful PMTUD probe growth.
- Runtime-test failed probe retry and final failure floor.
- Runtime-test blackhole fallback lowers active max UDP.
- Runtime-test send-too-big and kernel MTU hints update path state.
- Runtime-test probe loss does not reduce cwnd.

## Non-goals
- Do not invent a new PMTUD algorithm; reuse `Path`.
- Do not implement multipath PMTUD beyond active/candidate link paths.
- Do not add pacing; plan 19 handles paced probe cadence if needed.

## Options and Open Questions
- Option: use PING-only padded probes or include normal ack-eliciting data.  Prefer PING/padding for clear probe classification.
- Open question: platform coverage for asynchronous send-too-big errors through `Zi`.

## Acceptance Criteria
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
- `make -C zquic/test test` must `PASS`
- `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
- Tests cover successful probe growth, failed probe retry, blackhole fallback, and send-too-big hint handling.
- Active packet size follows PMTUD state.

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
