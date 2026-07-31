# Plan 1: Idle Timeout and Closing/Draining

Scope: implement `audit.md` priority work item 1:

> Implement active idle timeout and complete closing/draining timer behavior.

Acceptance criteria are defined by `idle_closing.md`.  Where local design is
ambiguous, align behavior with `../zngtcp2`: compute idle expiry as
`idle_ts + max(negotiated_idle_timeout, 3 * PTO)` and retain closing/draining
state for `3 * PTO`.

## Current state

- `ZquicTransport` parses and serializes `max_idle_timeout`, but `zquic`
  does not actively enforce an idle timeout.
- `LinkState` already has `Closing` and `Draining`.
- A close timer hook exists (`scheduleCloseTimer_()`, `closeTimeout_()`), but
  no call site currently arms it.
- `closeExpired_()` is empty.
- Existing `3 * PTO` logic is used for path validation, key discard, and
  persistent congestion; it is not yet wired to close/drain retention.

## Design

### Idle timeout state

Add per-link idle state in the runtime base:

- negotiated idle timeout in `ZuTime` or equivalent microsecond duration;
- last idle restart time;
- whether an ack-eliciting packet has been sent since the last successfully
  processed peer packet;
- idle timer handle, either a new `CxnTimer::Idle` value or a dedicated timer
  that follows the same `scheduleCxnTimer_()` pattern.

Prefer a dedicated `CxnTimer::Idle` if it keeps timer dispatch consistent with
ACK/loss/PTO/key-discard/path timers.

### Idle timeout negotiation

Compute the negotiated idle timeout after peer transport parameters are known:

- if both local and peer values are zero, disable idle timeout;
- if only one value is non-zero, use that value;
- if both are non-zero, use the minimum;
- convert transport parameter milliseconds to the local time type without
  truncating below millisecond precision.

When the timer is armed, use:

```text
effective_idle_timeout = max(negotiated_idle_timeout, 3 * current_PTO)
deadline = idle_restart_time + effective_idle_timeout
```

This matches RFC 9000 and the zngtcp2 shape in
`ngtcp2_conn_get_idle_expiry()`.

### Idle timer restart rules

Implement the RFC restart rules directly:

- when a peer packet is received and processed successfully:
  - set `idle_restart_time = now`;
  - clear the "ack-eliciting sent since peer activity" flag;
  - reschedule idle timeout if enabled;
- when sending an ack-eliciting packet:
  - if no ack-eliciting packet has been sent since the last successfully
    processed peer packet, set `idle_restart_time = now`;
  - set the flag;
  - reschedule idle timeout if enabled.

Do not restart the timer for failed packet processing, duplicate packets that
are ignored before successful processing, pure ACK sends, or close/drain
activity.

### Idle timeout expiry

On idle expiry:

- silently close the connection;
- do not send `CONNECTION_CLOSE`;
- cancel live timers;
- drain/cancel dependent stream and packet state through the existing shutdown
  path;
- notify the application as a local timeout/error in the existing callback style
  if such a hook exists; otherwise add the smallest local hook needed for tests;
- discard connection state when cleanup completes.

The result should behave like zngtcp2's idle close path: no close frame and no
closing period just because the idle timer fired.

### Closing state

There are three distinct close sources:

- application-initiated graceful close via `disconnect()`;
- application-initiated abort via `close()`;
- peer-initiated close via a received `CONNECTION_CLOSE`.

When local `disconnect()` sends `CONNECTION_CLOSE`:

- enter `LinkState::Closing` immediately;
- preserve enough state to identify connection packets and send a close packet;
- arm the minimum close timer used by `zquic`, not the RFC `3 * PTO` retention
  interval; this is the local application-close policy, not the peer/RFC
  closing-period policy;
- stop normal send scheduling, retransmission, stream output, path validation,
  PMTUD, and PTO work;
- retain or rebuild a close packet suitable for retransmission;
- on incoming attributed packets while closing, send `CONNECTION_CLOSE` again,
  but rate-limit responses.

When local `close()` is used:

- treat it as an abort;
- do not drain;
- do not arm a close delay;
- cancel timers, suppress further sends, and tear down immediately.
- implement this as the abrupt counterpart to `disconnect()`: no waiting for
  transmit drain and no close/drain retention timer.

Use zngtcp2's example behavior as the peer/RFC baseline: store the next
`CONNECTION_CLOSE` packet and use a `3 * PTO` close-wait timer.  This baseline
does not override the local `disconnect()`/`close()` policy above.

### Draining state

When a peer `CONNECTION_CLOSE` is received:

- optionally send one final `CONNECTION_CLOSE` before entering draining if the
  local path already does this and it is useful;
- enter `LinkState::Draining`;
- arm close timer for `now + 3 * current_PTO`, unless transitioning from
  `Closing`, in which case preserve the existing close end time;
- suppress all sends after entering draining;
- stop normal scheduling and retransmission work.

While draining, incoming packets are attributed and discarded without response.
Peer-initiated close should conform to RFC 9000 even if local application close
paths use shorter local policy.

### Close timer expiry

Implement `closeExpired_()` as the final close/drain retention expiry:

- discard retained close packet/state;
- remove route/CID state that should no longer identify this connection;
- cancel all remaining timers;
- drain stream state;
- transition to `LinkState::Closed`;
- complete endpoint/app cleanup using existing `disconnect`/`disconnected`
  semantics without generating more wire traffic.

### Anti-amplification and rate limiting

If closing retains packet protection keys and validates incoming packets before
responding, the current path allowance can remain the primary guard.

If closing drops keys or responds to unauthenticated attributed datagrams:

- track cumulative close-period bytes received and sent;
- enforce the RFC three-times amplification limit;
- apply the same rule per unvalidated source address.

Even when keys are retained, add a simple close-response rate limiter so a peer
cannot force a close packet for every received packet.  A minimal policy is
acceptable: for example, respond to the first packet and then only after a
short interval or after a progressively increasing packet count.

## Implementation steps

1. Add idle timer state and `CxnTimer::Idle` dispatch.
2. Add helper methods:
   - `negotiatedIdleTimeout_()`;
   - `effectiveIdleTimeout_()`;
   - `idleDeadline_()`;
   - `scheduleIdleTimer_()`;
   - `cancelIdleTimer_()`;
   - `notePeerPacketProcessed_()`;
   - `noteAckElicitingSent_()`;
   - `idleExpired_()`.
3. Wire peer-packet activity after packet/frame processing succeeds in both
   client and server receive paths.
4. Wire ack-eliciting send activity from the existing packet recording path,
   because it already knows whether a sent packet is ack-eliciting.
5. Compute and arm idle timeout once peer transport parameters are available,
   and re-arm when PTO changes enough to affect the effective deadline.
6. Add close/drain deadline state and helpers:
   - `closeDrainDeadline_()`;
   - `armCloseDrainTimer_()`;
   - `enterClosing_()` with a close-source/minimum-delay policy;
   - `enterDraining_()`.
7. Replace direct `closeLinkState_()`/manual draining assignments with the new
   helpers where `CONNECTION_CLOSE` is sent or received.
8. Store or rebuild the close packet needed for closing-state responses.
9. Implement close-response rate limiting and ensure draining suppresses all
   sends.
10. Ensure local `close()` bypasses close/drain retention and tears down
    immediately.
11. Implement `closeExpired_()` teardown.
12. Add tests for idle negotiation, idle expiry, close retention, draining
    suppression, and timer cleanup.

## Tests

Add focused unit/runtime tests under `zquic/test` where possible.

Required coverage:

- local idle timeout disabled when both endpoints use zero;
- effective idle timeout chooses the minimum non-zero advertised value;
- effective idle timeout is raised to at least `3 * PTO`;
- successful peer packet processing restarts the idle timer;
- first ack-eliciting send after peer activity restarts the idle timer;
- pure ACK send does not restart the idle timer;
- idle expiry closes silently without sending `CONNECTION_CLOSE`;
- local `disconnect()` sends `CONNECTION_CLOSE`, enters closing, and arms the
  minimum close timer;
- local `close()` aborts without drain or delay;
- peer `CONNECTION_CLOSE` enters draining and arms close timer for `3 * PTO`;
- closing responds to an attributed packet with a close packet and rate-limits
  repeated responses;
- peer `CONNECTION_CLOSE` enters draining and suppresses subsequent sends;
- RFC closing-to-draining transition keeps the original close end time;
- close timer expiry reaches `Closed`, cancels timers, and releases retained
  close state.

Run:

```sh
make -C zquic/test test
make -C zhttp/test test
```

## Acceptance criteria

- All requirements in `idle_closing.md` are represented by code or an explicit
  non-goal in comments/tests.
- Idle timeout is active only when negotiated non-zero.
- Idle expiry is silent on the wire.
- Peer-initiated close/draining retention is `3 * current PTO`.
- Application `disconnect()` uses the minimum local close timer.
- Application `close()` is abortive and has no close/drain delay.
- `scheduleCloseTimer_()` has real call sites.
- `closeExpired_()` performs final teardown.
- Draining sends no packets after entry.
- Closing sends only close packets and rate-limits them.
- Existing `zquic` and `zhttp` tests pass.
