## Requirements
- Make active path state part of client/server link runtime.
- Record bytes received and sent into `Path`.
- Gate server sends by anti-amplification until address validation.
- Replace raw `app()->maxUDP()` packet budget use with active path maximum UDP payload.

## Summary
Preconditions: plans 1 through 7 complete.

`zquic` already has a `Path` utility with anti-amplification accounting, active max UDP, PMTUD state, and tests.  Runtime send/receive paths do not use it.  This plan wires one active path into `Link` runtime and uses it for packet budgets and byte accounting.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

Read `arc/zquic4/backlog.md` and `arc/zquic4/log.md` to understand the starting point of this work.

## Architecture Documentation
New or changed components: add active `Path` state to client and server links.

New or changed processes or threads: Rx records received bytes on the path; Tx reserves/sends bytes.  Cross-shard updates must carry fixed metadata only.

New or changed interfaces: add internal accessors for active path allowance and path byte accounting.

New or changed data flows: datagram receive identifies path, records bytes, then processes packets; send path asks `Path` for allowance and records sent bytes after successful send.

New or changed event-driven or timer processing: PMTUD/path-validation timers from plan 1 become usable after path state is runtime-owned.

New or changed network programming: server send path enforces QUIC 3x anti-amplification until address validation.

New or changed data stores: active path stores local/remote addresses, validation flag, max UDP, bytes rx/tx, and PMTUD fields.

## Detailed Design and Implementation Plan
### Phase 1: Link Path Ownership
- Add active path construction during client/server link initialization from socket addresses.
- Initialize server path as unvalidated and client path as connected/validated where appropriate.
- Set peer/configured max UDP from transport parameters and engine config.

### Phase 2: Receive Accounting
- On datagram receive, call `Path::received(bytes)` for the matched path.
- Ensure coalesced packets count the UDP datagram once, not each packet.

### Phase 3: Send Budgeting
- Replace `app()->maxUDP()` packet payload budget with `activePath.activeMaxUDP()`.
- Apply `Path::sendAllowance()` and `reserveSend()` for server anti-amplification.
- Combine path allowance with congestion allowance from plan 4 by taking the minimum.

### Phase 4: Validation Unlock
- Mark the path validated when handshake/address validation rules allow.
- Add test hooks to inspect path diag state.

## Code References to Impacted Code
- `zquic/src/ZquicPath.hh:33` - Existing `Path` utility to reuse.
- `zquic/src/Zquic.hh:2592` - Client send budget uses max UDP directly.
- `zquic/src/Zquic.hh:4719` - Server send budget uses max UDP directly.
- `zquic/src/ZquicEndpoint.cc:120` - Routing/receive path should feed path byte accounting.
- `zquic/test/ZquicPMTUDTest.cc` - Existing path tests to extend with runtime integration.

## Detailed Test Plan
- Runtime-test unvalidated server cannot send more than 3x received bytes.
- Runtime-test receiving more bytes increases server send allowance.
- Runtime-test active path max UDP controls packet payload size.
- Runtime-test address validation unlocks full send allowance.

## Non-goals
- Do not implement migration or multiple paths; plan 9 handles candidate paths.
- Do not implement PMTUD probing; plan 10 handles probes.
- Do not add locks for path state.

## Options and Open Questions
- Option: embed `Path` directly in `Link` or store active/candidate paths in a small container.  Direct member is sufficient before migration.
- Open question: exact handshake point for server validation in all Retry/no-Retry cases; align with transport parameter validation.

## Acceptance Criteria
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
- `make -C zquic/test test` must `PASS`
- `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
- Server cannot send more than 3x received bytes before validation.
- Active path MTU controls packet payload sizing.
- Tests cover unvalidated path budget exhaustion and validation unlock.

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
