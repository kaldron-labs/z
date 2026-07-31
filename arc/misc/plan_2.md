# Plan 2: Server Address Validation

## Goal

Implement `audit.md` Priority work item 1: server address-validation policy for
QUIC Retry token generation/validation and NEW_TOKEN handling.

Current state:

- Retry packet parsing/writing and integrity validation already exist.
- Client-side Retry bootstrap already records Retry SCID and token length.
- Server-side `ServerBootstrap::acceptInitial()` can accept Initial packets but
  has no token policy.
- NEW_TOKEN is parsed and packet legality is checked, but token production and
  client token reuse are not wired into runtime behavior.

The target is a small, deterministic policy layer comparable in shape to
`../zngtcp2`: token validation must be server-owned, explicit, and tied to the
peer address and original destination CID, while callers can keep the default
behavior disabled unless configured.

## Design

Add an address-validation token component with two token classes:

- Retry token: proves ownership of the source address for the current Initial
  attempt and carries the original destination CID needed for Retry transport
  parameter validation.
- NEW_TOKEN token: proves ownership of a client address for a future connection
  and can be emitted after the connection is established.

The token payload should be authenticated, versioned, and bounded.  It should
not require server-side per-token storage for the default implementation.

Suggested token fields:

- token kind: Retry or NEW_TOKEN
- issue timestamp or coarse epoch
- original destination CID
- client IP address and port policy data
- optional server instance/key identifier
- random nonce
- authentication tag

Keep the default public API conservative:

- Address validation is off by default unless configured.
- Retry can be enabled independently from NEW_TOKEN emission.
- Applications may override policy hooks, but the normal path should work with
  default token encode/decode helpers once a secret/key is configured.

## Implementation Steps

1. Add token codec primitives.
   - Add a compact `AddressToken` or `ValidationToken` helper in `zquic/src`.
   - Use existing Z containers/spans, not STL.
   - Provide encode/decode/validate helpers for Retry and NEW_TOKEN.
   - Bind validation to `ZiSockAddr` and original DCID.
   - Enforce token version, kind, maximum length, expiration, and authentication.

2. Extend server configuration.
   - Add server params for address validation policy:
     - validation secret/key material or generated process-local secret
     - Retry enablement
     - NEW_TOKEN enablement
     - token lifetime
     - optional validation mode for IP-only vs IP+port binding
   - Store these in `Engine`/`Server` consistently with existing params.

3. Implement Retry decision path.
   - In `Server::routeLong_()` / Initial routing, validate any Initial token
     before accepting a new link.
   - If Retry is enabled and the token is absent or invalid, send an
     authenticated Retry packet and do not create a link.
   - The Retry SCID must become the server-chosen SCID for the retried attempt.
   - Valid Retry tokens must populate `ServerBootstrap` with original DCID and
     client Initial SCID exactly as required by transport parameters.
   - Invalid, expired, wrong-address, wrong-kind, malformed, or wrong-ODCID
     tokens must not create a link.

4. Add NEW_TOKEN send path.
   - Add `FrameCodec::writeNewToken()`.
   - Add a queued control path for NEW_TOKEN after handshake establishment when
     enabled by policy.
   - Tokens must be one-shot from the client's perspective only if the policy
     implements server-side state; the default stateless policy should permit
     replay but remain address-bound and time-limited.
   - Do not send NEW_TOKEN before 1-RTT is established.

5. Add client token storage and reuse.
   - On client receipt of NEW_TOKEN, retain the token for the current server
     authority/address.
   - Include a retained token in the next Initial packet to that server.
   - Keep storage bounded and clear it on configuration/authority changes.
   - Do not treat NEW_TOKEN as proof for a different peer address.

6. Diagnostics and policy visibility.
   - Add counters for Retry sent, Retry accepted, Retry rejected, NEW_TOKEN sent,
     NEW_TOKEN received, token expired, token auth failure, and token
     address mismatch.
   - Keep diagnostic updates on the owning shard.
   - Add debug log points only where they aid field diagnosis without excessive
     packet-path formatting cost.

7. Tests.
   - Unit-test token encode/decode/authentication, expiry, kind mismatch,
     address mismatch, and ODCID mismatch.
   - Extend packet/frame tests for NEW_TOKEN write/parse round trips.
   - Add server Retry runtime tests:
     - no token -> Retry sent, no link accepted
     - valid Retry token -> link accepted and transport params validate
     - bad token/tag/address/expiry -> no link accepted
   - Add NEW_TOKEN runtime tests:
     - server emits NEW_TOKEN only after establishment
     - client stores it
     - subsequent connection includes it
     - server accepts the Initial without Retry when the token is valid
   - Cover disabled-policy defaults so existing applications keep current
     behavior unless opt-in is configured.

## Acceptance Criteria

- `Server` can be configured to require Retry address validation.
- A client without a valid token receives an authenticated Retry and no server
  link is accepted on the first Initial.
- A retried client with a valid Retry token is accepted and server transport
  parameters contain the correct original DCID, initial SCID, and Retry SCID.
- Invalid, expired, wrong-address, wrong-kind, and wrong-ODCID tokens are
  rejected without accepting a link.
- Server can emit NEW_TOKEN after 1-RTT establishment when enabled.
- Client can store NEW_TOKEN and send it in a later Initial to the same peer.
- A valid NEW_TOKEN can satisfy server address validation without Retry.
- Address-validation behavior is disabled by default or explicitly controlled by
  server params, so existing tests and apps do not silently change policy.
- `make -C zquic/test test` passes.
- `make -C zhttp/test test` passes.

## Notes

- Align the token-validation flow with `../zngtcp2` when uncertain: keep the
  policy explicit, bind tokens to peer address and ODCID, and keep Retry
  stateless on the server fast path.
- Do not add a compatibility shim for old APIs unless a current in-tree user
  needs it.
- Avoid treating Retry and NEW_TOKEN as the same semantic token even if they
  share an authenticated encoding format.
