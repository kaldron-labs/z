## Requirements
- Add configurable server Retry policy and token generator/verifier.
- Validate Initial token before accepting when policy requires Retry.
- Carry original DCID/retry SCID transport parameter validation through the handshake.

## Summary
Preconditions: plans 1 through 15 complete.

`zquic` already writes, parses, and validates Retry packet integrity, and the client handles Retry.  Server-side policy and token validation are missing.  This plan adds a configurable token service and integrates it into server Initial acceptance.

## Architecture Documentation
New or changed components: add Retry policy/config to server engine params and a token generator/verifier interface.

New or changed processes or threads: endpoint/server receive path decides whether to accept Initial or send Retry before creating full connection state.

New or changed interfaces: CRTP hooks or config callbacks generate and verify tokens without virtual dispatch.

New or changed data flows: first Initial without valid token triggers Retry; retried Initial validates token and carries original DCID/retry SCID into transport params.

New or changed event-driven or timer processing: no new timer, but token freshness may include timestamp validation.

New or changed network programming: Retry response uses existing packet writer and does not allocate full connection state until accepted.

New or changed data stores: token secret/config and optional stateless token metadata.

## Detailed Design and Implementation Plan
### Phase 1: Policy Configuration
- Add server Retry policy: disabled, always, or app-controlled.
- Add token secret/config storage with Z heap-aware containers if variable data is needed.

### Phase 2: Token Format
- Define compact authenticated token containing client address, original DCID, timestamp/nonce, and policy metadata.
- Use existing crypto/HMAC facilities where available; avoid ad hoc unauthenticated tokens.

### Phase 3: Server Accept Path
- Before accepting an Initial, validate token if policy requires it.
- If invalid/missing, send Retry with generated token.
- If valid, create connection and carry original DCID/retry SCID for transport parameter validation.

### Phase 4: Tests
- Extend version/handshake tests around server-required Retry.

## Code References to Impacted Code
- `zquic/src/ZquicPacket.cc:314` - Retry packet writer.
- `zquic/src/ZquicPacket.cc:224` - Retry integrity validation.
- `zquic/src/Zquic.hh:426` - Client Retry handling.
- `zquic/src/ZquicEndpoint.cc:120` - Server endpoint accept path.
- `zquic/test/ZquicVersionTest.cc:100` - Existing Retry packet tests.

## Detailed Test Plan
- Test no-Retry policy preserves current behavior.
- Test Retry-required sends Retry for missing token.
- Test valid retried Initial is accepted.
- Test invalid token is rejected or retried without creating connection.
- Test client Retry behavior remains compatible.

## Non-goals
- Do not implement a distributed token database.
- Do not support insecure plaintext tokens.
- Do not preserve old server accept behavior when policy explicitly requires Retry.

## Options and Open Questions
- Option: expose token callbacks to the app or provide a default stateless token.  Prefer default plus optional hooks.
- Open question: token binding to client IP under NAT/load balancers; make binding configurable.

## Acceptance Criteria
- Tests cover Retry required, valid retry, invalid token, and no-Retry policy.
- Client Retry behavior remains compatible with current tests.
