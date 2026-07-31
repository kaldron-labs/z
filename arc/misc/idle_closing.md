# QUIC Idle Timeout and Closing/Draining Requirements

Scope: RFC 9000 core transport behavior for idle timeout and
closing/draining state.

## Idle timeout

- `max_idle_timeout` is a transport parameter encoded in milliseconds.
- Idle timeout is disabled if both endpoints omit `max_idle_timeout` or set it
  to `0`.
- If both endpoints advertise non-zero values, the effective idle timeout is the
  minimum of the two values.
- If only one endpoint advertises a non-zero value, that value is the effective
  idle timeout.
- The effective idle timeout must be increased to at least `3 * current PTO`.
- Idle timeout expiry silently closes the connection.  Do not send
  `CONNECTION_CLOSE` just because the idle timer expired.
- On idle timeout expiry, discard connection state once local cleanup is
  complete.
- Restart the idle timer when a peer packet is received and processed
  successfully.
- Restart the idle timer when sending an ack-eliciting packet, but only if no
  ack-eliciting packet has been sent since the last successfully processed peer
  packet.
- Keepalive is optional.  An endpoint can send `PING` or another ack-eliciting
  frame before expiry if the application wants to keep the connection open.

## Closing state

- Sending a `CONNECTION_CLOSE` frame enters closing state immediately.
- Closing state should persist for at least `3 * current PTO`.
- The endpoint only needs to retain enough state to:
  - identify packets that belong to the closing connection;
  - generate a packet containing `CONNECTION_CLOSE`.
- In closing state, do not process ordinary frames.
- In closing state, respond to incoming packets attributed to the connection
  with a packet containing `CONNECTION_CLOSE`.
- Rate-limit `CONNECTION_CLOSE` responses while closing.
- It is permitted to retransmit the same closing packet rather than allocate new
  packet numbers.
- If packet protection keys are discarded, any response strategy must still
  respect anti-amplification: cumulative bytes sent cannot exceed three times
  cumulative bytes received and attributed to the connection.
- Packets received from an unvalidated address while closing must either be
  discarded or be subject to the same three-times amplification limit for that
  address.

## Draining state

- Receiving a `CONNECTION_CLOSE` frame enters draining state.
- An endpoint may send one `CONNECTION_CLOSE` packet before entering draining,
  using `NO_ERROR` if appropriate.
- After entering draining, the endpoint must not send any packets.
- Draining state should persist for at least `3 * current PTO`.
- If already in closing and a `CONNECTION_CLOSE` is received, the endpoint may
  move to draining.  It keeps the same end time it would have used for closing,
  but stops sending packets.
- Packet protection keys do not need to be retained in draining.

## Ending closing/draining

- After closing or draining ends, discard all connection state.
- Later packets for the same connection may receive a stateless reset if enough
  routing/token state exists.
- Closing/draining may end earlier only if there is another way to ensure late
  packets do not trigger responses, such as closing the UDP socket.
- Servers that keep the UDP socket open for new connections should not end
  closing or draining early.

## zquic implementation implications

- Add an active idle timer only when the negotiated effective timeout is
  non-zero.
- Track enough activity state to implement the RFC restart rules exactly:
  successful peer packet processing and first ack-eliciting send after peer
  activity.
- Use `max(negotiated_idle_timeout, 3 * current_PTO)` as the effective idle
  timeout deadline basis.
- Idle expiry should drive silent local close/cleanup, not close-frame emission.
- `CONNECTION_CLOSE` send should enter closing and arm a close timer for roughly
  `3 * current_PTO`.
- `CONNECTION_CLOSE` receive should enter draining and arm the same retention
  interval, unless transitioning from closing to draining, in which case keep
  the existing close end time.
- Implement `closeExpired_()` as the point that discards retained close/drain
  state and finalizes teardown.
- While closing, retain minimal route/CID/version/close-packet state and
  rate-limit close responses.
- While draining, suppress all sends.
