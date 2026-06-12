## Summary

Goal: remediate every finding in `zquic.md` while preserving the current public QUIC transport behavior, CRTP/static-polymorphism API shape, and reviewability of the change set.

The remediation should proceed as a sequence of small, vertically useful changes. Each phase either removes one concrete guideline violation end-to-end or prepares a narrow helper that later phases consume immediately. The plan keeps generated build outputs untouched and confines edits to source, headers, tests, and documentation.

The main product/engineering requirements are:

- Remove all public-template `requires` usage from `zquic/src/Zquic.hh` and express optional callbacks with local detector traits/SFINAE.
- Replace fixed runtime tables/queues that carry live server links, endpoint Tx backlog, and connection ID state with either runtime-sized Z containers or explicit, advertised, tested local resource policy.
- Remove per-operation OpenSSL context allocation from Initial packet protection and Retry integrity paths, reusing existing `Ztls::Pico` context wrappers where their API matches.
- Refactor frame dispatch to `switch`-style logic without changing frame parse/dispatch behavior.
- De-duplicate client/server runtime plumbing once the smaller behavioral fixes have stable tests.
- Make retransmit-drop diagnostics true by construction: either expose real drops from a bounded policy or document/remove the always-zero placeholder.

External requirements checked:

- RFC 9000 says endpoints advertise `active_connection_id_limit`, must not provide more connection IDs than the peer limit, and must close with `CONNECTION_ID_LIMIT_ERROR` if active IDs exceed the advertised limit after NEW_CONNECTION_ID processing. It also defines a default/minimum of 2 and requires `TRANSPORT_PARAMETER_ERROR` for values below 2: https://datatracker.ietf.org/doc/html/rfc9000#section-5.1.1 and https://datatracker.ietf.org/doc/html/rfc9000#section-18.2
- RFC 9001 defines Retry integrity as AEAD_AES_128_GCM over the Retry pseudo-packet with fixed v1 key/nonce, empty plaintext, and the pseudo-packet as associated data: https://datatracker.ietf.org/doc/html/rfc9001#section-5.8
- quiche documents using the minimum of local and peer active CID limits for issuing source CIDs, which supports treating CID count as negotiated policy rather than unbounded storage: https://docs.rs/quiche/latest/quiche/struct.Connection.html#method.scids_left
- quiche had a real advisory for unbounded storage around CID retirement, so any runtime-sized CID change must still include policy bounds and retirement accounting rather than blindly making queues unbounded: https://github.com/cloudflare/quiche/security/advisories/GHSA-xhg9-xwch-vr7x

## Architecture Documentation

### New Or Changed Components

- `zquic/src/Zquic.hh`
  - Add internal optional-callback detectors near the public template declarations.
  - Replace `Server::m_links[MaxLinks]` with a Z container keyed by link pointer.
  - Replace fixed `LinkCID m_localCIDs[MaxConnectionIDs]` and `m_peerCIDs[MaxConnectionIDs]` with a CID store that enforces negotiated/local policy.
  - Add shared role-neutral helpers in `Link` after behavioral phases have tests.

- `zquic/src/ZquicEndpoint.hh` / `zquic/src/ZquicEndpoint.cc`
  - Replace `Endpoint::Cxn_` fixed Tx ring arrays with a named-heap queue implementation.
  - Extend `EndpointDiag` in `zquic/src/ZquicSock.hh` with explicit Tx back-pressure/drop accounting if send enqueue remains bounded.

- `zquic/src/ZquicCrypto.hh` / `zquic/src/ZquicCrypto.cc`
  - Add reusable Initial packet protection state beside `InitialKeyMaterial`, or extend `InitialCrypto` APIs to accept reusable contexts.
  - Continue using `PacketProtectionState` for Handshake/1-RTT traffic.

- `zquic/src/ZquicPacket.cc`
  - Rework Retry integrity to avoid `EVP_CIPHER_CTX_new()` per call. Prefer `Ztls::Pico::AeadCtx` if it can represent AES-128-GCM with fixed key/nonce and AAD-only input; otherwise add a tiny reusable backend wrapper with explicit initialization and cleanup.

- `zquic/src/ZquicFrame.cc`
  - Refactor `FrameCodec::parse()` into switch-based dispatch plus small helpers for range-encoded frame families.

- `zquic/src/ZquicRecovery.hh`
  - Replace placeholder retransmit-drop reporting with either real accounting or remove/document the always-zero diagnostic.

### New Or Changed Processes Or Threads

No new threads are planned. Existing Rx/Tx scheduler ownership must remain intact:

- Endpoint send enqueue/dequeue remains within the existing `ZiConnection` send completion flow.
- Public `send()` still invokes or schedules onto the app Tx side using existing `txInvoked()` / `txInvoke()` semantics.
- Server receive routing remains on the existing Rx path; route/link tables need appropriate Z locks only if current access can cross scheduler threads.

### New Or Changed Interfaces

- Public application callbacks remain source-compatible:
  - `listening()`
  - `listenFailed(bool)`
  - `accepted(const InitialInfo &)`
  - `txInvoked()`
  - `txInvoke(Fn)`
  - `retiredLocalCID_(uint64_t, const CxnID &)`
  - `statelessReset()`
  - `streamFrame(uint64_t, uint64_t, ZuCSpan, bool)`
  - `disconnected()`

- Diagnostics may gain fields:
  - `EndpointDiag::txBackPressure` or similarly succinct name.
  - CID-policy failure counters in `RuntimeDiag` only if they are useful to tests/docs and do not duplicate existing `failures`.
  - Retransmit-drop counter if a bounded retransmit queue policy is introduced.

- Transport parameter encoding should advertise the actual local active CID capacity. If the chosen policy remains capped, set `TransportParams::activeConnectionIDLimit` from a named local policy constant rather than the current implicit default.

### New Or Changed Data Flows

- Server accepted links:
  - `Server::accept_()` obtains `LinkRef` from the app, inserts it in a Z container keyed by link pointer, then returns the raw pointer for routing.
  - `SrvLink::close_()` calls `Server::releaseLink_()`, which retires routes and removes the link from the live table.

- Endpoint Tx:
  - `Endpoint::send()` calls `Cxn_::sendPacket()`.
  - If a send is already active, the datagram is appended to the queue.
  - Send completion dequeues FIFO and continues; enqueue failure increments diagnostics before returning `false`.

- CID processing:
  - `receiveNewConnectionID_()` validates frame shape, applies `retire_prior_to`, enforces active count against negotiated/local policy, and returns false or initiates close on protocol violation.
  - Route installation only associates active local CIDs once; route retirement remains idempotent.

### New Or Changed Event-Driven Or Timer Processing

No new timers are planned. Existing ACK flush, retransmission, and endpoint send completion callbacks remain the event boundaries. If retransmit queue bounding is introduced, drops must be accounted at enqueue time on the existing recovery path.

### New Or Changed Network Programming

No socket or wire-format changes are intended. The plan preserves:

- UDP endpoint modes: `ClientConnected` and `ServerUnconnected`.
- QUIC packet protection output.
- Version negotiation, stateless reset, Retry integrity, and ACK behavior.
- Public example behavior in `zquic/example/ZquicClient.cc` and `zquic/example/ZquicServer.cc`.

### New Or Changed Data Stores

- Server live-link table: replace fixed array with `ZmHash` or an intrusive hash/list structure using a named heap such as `"Zquic.Server.LinkTable"`.
- Endpoint Tx backlog: replace fixed ring arrays with either `ZmList` intrusive nodes or `ZtArray`-backed queue storage, using a named heap such as `"Zquic.Endpoint.TxQueue"`.
- CID store: prefer a small Z container with explicit policy cap and named heap(s), such as `"Zquic.Link.LocalCID"` and `"Zquic.Link.PeerCID"`.

## Detailed Design and Implementation Plan

### Phase 1: Baseline, API Audit, And Focused Guardrails

- Capture the current build/test baseline before behavior changes:
  - `make -j`
  - `make -C zquic/test test`
  - If the aggregate test target is unreliable, run each binary listed in `zquic/test/Makefile.am`.

- Record the current source scans:
  - `rg -n "requires\\s*\\(" zquic/src zquic/test zquic/example`
  - `rg -n "MaxLinks|MaxTxQueue|m_links\\[|m_txQueueBuf\\[|m_txQueueAddr\\[|MaxConnectionIDs|EVP_CIPHER_CTX_new|retransmitDropped|dropped\\(\\)" zquic/src`

- Add narrowly targeted regression checks before each risky refactor where practical:
  - SFINAE callback coverage in `ZquicAPITest`.
  - Endpoint queue/back-pressure behavior in `ZquicEndpointTest`.
  - CID limit/policy handling in `ZquicCIDTest`.
  - Frame parse coverage in `ZquicCodecTest`.

- Keep generated files untouched:
  - Do not edit `.libs`, `.deps`, object files, generated `Makefile`, or `Makefile.in`.
  - Do not reformat unrelated zquic code.

### Phase 2: Replace `requires` With Local Detectors

- Add detector traits in `zquic/src/Zquic.hh`, close to the optional callback users so the template surface remains easy to audit.

- Use the repository idiom:
  - Primary template with `typename = void`.
  - Specialization using `decltype(CODE, void())`.
  - `ZuIfT` or small helper overloads where it avoids noisy branches.

- Proposed detectors:

```c++
template <typename App, typename = void>
struct HasListening : ZuFalse { };
template <typename App>
struct HasListening<App, decltype(ZuDeclVal<App *>()->listening(), void())> :
  ZuTrue { };
```

Repeat the pattern for:

- `HasListenFailed<App>`
- `HasAccepted<App>`
- `HasTxInvoked<Link>`
- `HasTxInvoke<Link, Fn>`
- `HasRetiredLocalCID<Impl>`
- `HasStatelessReset<Impl>`
- `HasStreamFrame<Impl>`
- `HasDisconnected<Impl>`

- Replace the current call sites:
  - `zquic/src/Zquic.hh:1007`
  - `zquic/src/Zquic.hh:1040`
  - `zquic/src/Zquic.hh:1104`
  - `zquic/src/Zquic.hh:1500`
  - `zquic/src/Zquic.hh:1511`
  - `zquic/src/Zquic.hh:1797`
  - `zquic/src/Zquic.hh:1988`
  - `zquic/src/Zquic.hh:2486`
  - `zquic/src/Zquic.hh:3103`

- Preserve optional callback semantics exactly. Missing callbacks remain no-ops or existing fallback errors as today.

- Verification:
  - `rg -n "requires\\s*\\(" zquic/src zquic/test zquic/example` returns no source matches.
  - Run `ZquicAPITest` and `ZquicRuntimeTest`.

### Phase 3: Remove The Server Live-Link Cap End-To-End

- Replace `Server::m_links[MaxLinks]` and `MaxLinks = 16`.

- Recommended implementation:
  - Define a small intrusive/ref-owning table entry that stores `LinkRef`.
  - Key by raw link pointer, because current add/release semantics are pointer identity.
  - Use `ZmHash` with a named heap/ID, for example `"Zquic.Server.LinkTable"`.
  - Initialize with conservative params such as `ZmHashParams().bits(5).loadFactor(1).cBits(3)`, matching nearby route table defaults.

- Avoid storing only raw pointers. The table must own `ZmRef<Link>` so accepted links remain alive after `accepted()` returns.

- Update operations:
  - `addLink_()` becomes lookup-or-insert by pointer and returns true for an already-present link.
  - `releaseLink_()` calls `link->retireRoutes_(m_routes)` first, then removes the table entry by pointer.
  - `clearLinks_()` clears `m_routes` and then clears the live-link table.

- Add runtime test coverage:
  - Extend `ZquicRuntimeTest` or `ZquicCIDTest` with a server app that accepts at least 17 simultaneous links.
  - Verify all accepted links can be retained until explicitly closed/released.

- Verification:
  - `rg -n "MaxLinks|m_links\\[" zquic/src` has no matches.
  - Run `ZquicRuntimeTest`, `ZquicAPITest`, and `ZquicCIDTest`.

### Phase 4: Replace Endpoint Tx Ring And Expose Back-Pressure

- Replace the fixed arrays in `Endpoint::Cxn_`:
  - `m_txQueueBuf[MaxTxQueue]`
  - `m_txQueueAddr[MaxTxQueue]`
  - `m_txQueueHead`, `m_txQueueTail`, `m_txQueueCount`

- Recommended implementation:
  - Introduce `TxNode` with `ZmRef<ZiIOBuf> buf`, `ZiSockAddr addr`, and an intrusive `ZmList` node.
  - Allocate nodes from a named heap such as `"Zquic.Endpoint.TxQueue"`.
  - Preserve FIFO behavior exactly.

- Policy:
  - Keep a bounded queue initially to preserve back-pressure behavior, but make the bound an explicit endpoint policy constant, for example `EndpointTxQueueLimit`.
  - Increment a new `EndpointDiag` counter before returning `false` because the queue is full.
  - Do not make this queue unbounded without a separate memory/back-pressure design, since it is packet-send-path storage.

- Add tests:
  - Extend `ZquicEndpointTest` to force queued sends while one send is outstanding.
  - Verify FIFO drain order.
  - Verify queue-full returns `false` and increments the diagnostic counter.

- Verification:
  - `rg -n "MaxTxQueue|m_txQueueBuf\\[|m_txQueueAddr\\[" zquic/src` has no matches unless the remaining name is the explicit policy constant.
  - Run `ZquicEndpointTest`, `ZquicRuntimeTest`, and `ZquicLoopTest`.

### Phase 5: Align CID Storage With Negotiated Runtime Policy

- Treat CID state as a negotiated bounded resource, not as an implicit fixed array.

- Recommended policy:
  - Introduce `LocalActiveConnectionIDLimit = 8` or a similarly succinct named policy constant as the first implementation step.
  - Advertise that value in `configureLocalTransportParams_()` by setting `m_transportParams.activeConnectionIDLimit`.
  - Enforce the advertised local limit for peer-issued CIDs.
  - Enforce the peer-advertised limit for local CIDs if/when issuing additional local CIDs.

- Then replace fixed arrays with a small Z container:
  - Preserve `LinkCID` fields: `id`, `sequence`, `resetToken`, `state`, `associated`.
  - Use named heaps `"Zquic.Link.LocalCID"` and `"Zquic.Link.PeerCID"`.
  - Keep lookup by sequence and by CID efficient. With the current small policy cap, a `ZtArray<LinkCID>` with bounded active count is acceptable and simpler than a two-index hash. If later CID counts become large, split into hashes keyed by sequence and CID.

- Correct protocol behavior:
  - `receiveNewConnectionID_()` must reject malformed frames.
  - Apply `retire_prior_to` before counting active peer CIDs.
  - If active peer CIDs exceed the local advertised limit, close with `TransportError::ConnectionIDLimit` where the runtime close path supports it; until then, return failure and increment diagnostics as an interim step.
  - Do not allow unbounded tracking of retired-but-unacknowledged CID state. RFC 9000 recommends limiting retired local IDs awaiting `RETIRE_CONNECTION_ID`; quiche's advisory shows why this matters.

- Route behavior:
  - `installLocalCIDRoutes_()` only associates active, unassociated CIDs.
  - `retireLocalCIDRoutes_()` retires only associated active/retired CIDs once.
  - `receiveRetireConnectionID_()` still calls the optional `retiredLocalCID_()` callback through the Phase 2 detector.

- Add tests:
  - `ZquicCIDTest` for the advertised active CID limit.
  - Duplicate sequence/different CID rejection.
  - Duplicate CID/different sequence rejection.
  - `retire_prior_to` retirement before active count enforcement.
  - Policy overflow close/failure path.

- Verification:
  - Run `ZquicCIDTest`, `ZquicRuntimeTest`, and `ZquicHandshakeTest`.

### Phase 6: Reuse Initial And Retry Crypto Contexts

- Audit actual available crypto APIs:
  - `Ztls::Pico::AeadCtx::init(ptls_aead_algorithm_t *, bool, const void *, const void *)`
  - `Ztls::Pico::CipherCtx::init(ptls_cipher_algorithm_t *, bool, const void *)`
  - `PacketProtectionState` already stores reusable `AeadCtx`, `CipherCtx hp`, and `CipherCtx hpSupp` for non-Initial traffic.

- Rework Initial packet protection:
  - Add reusable Initial Tx/Rx protection state rather than passing only raw `InitialSecret`.
  - Initialize AES-128-GCM and AES-ECB header protection once per derived Initial secret.
  - Keep `InitialCrypto::encrypt`, `encryptV`, `decrypt`, and `headerMask` wrappers if useful for tests, but have runtime code use reusable state.
  - Preserve packet bytes verified by `ZquicPacketProtectionTest`.

- Rework Retry integrity:
  - Prefer a reusable `Ztls::Pico::AeadCtx` initialized with the RFC 9001 v1 Retry key/nonce if picotls AEAD supports the AAD-only, empty-plaintext tag calculation needed here.
  - If picotls cannot express this exact Retry API cleanly, add a local RAII `RetryIntegrityCtx` wrapper around OpenSSL that initializes once and resets/reuses the context without per-call heap allocation.
  - Keep the fixed v1 key/nonce constants as protocol data in `ZquicPacket.cc` unless a shared helper makes the code clearer.

- Add proof of improvement:
  - Use `Ztls::Pico::reset_stats()` / `stats()` if picotls wrappers are used.
  - Otherwise add a small test hook or source-level scan assertion to show no `EVP_CIPHER_CTX_new()` remains in Initial/Retry per-operation functions.

- Verification:
  - `rg -n "EVP_CIPHER_CTX_new" zquic/src/ZquicCrypto.cc zquic/src/ZquicPacket.cc` should either be empty or limited to one-time context initialization.
  - Run `ZquicPacketProtectionTest`, `ZquicCryptoTest`, `ZquicHandshakeTest`, and `ZquicVersionTest`.

### Phase 7: Refactor Frame Parse And Protected Dispatch To `switch`

- Refactor `FrameCodec::parse()` in `zquic/src/ZquicFrame.cc`:
  - Use `switch (t)` for exact frame types.
  - Use compact helper predicates/functions for frame-type ranges:
    - stream frames `0x08..0x0f`
    - max-streams variants
    - blocked variants
  - Keep `used` and error returns byte-for-byte compatible.

- Refactor `Link::consumeProtectedFrames_()`:
  - Keep parse and ACK-eliciting accounting before dispatch.
  - Replace the `if`/`else if` chain with `switch (frame.type)`.
  - Preserve optional `streamFrame()` callback via the Phase 2 detector.
  - Keep no-op behavior for frame types currently ignored after parsing.

- Add/confirm tests:
  - `ZquicCodecTest` should cover every known `FrameType`.
  - Add one test for stream-frame flags if current coverage does not verify offset/length/fin combinations.

- Verification:
  - Run `ZquicCodecTest`, `ZquicFlowTest`, `ZquicStreamTest`, `ZquicRuntimeTest`, and `ZquicHandshakeTest`.

### Phase 8: De-Duplicate Client/Server Runtime Plumbing In Vertical Helpers

- Only start this phase after phases 2-7 pass. The duplicated client/server code will be easier to move once callback dispatch, CID policy, endpoint back-pressure, and frame dispatch are stable.

- Extract role-neutral helpers into `Link`:
  - `sendAppData_(StreamRef, ZuCSpan, bool, ZiSockAddr, SendQueued)`
  - payload copy/scheduling from public `send()`
  - `sendCryptoPacket_()` overload shells
  - `sendPacketByLevel_()` helper for Initial/Handshake/OneRTT dispatch
  - `flushPendingAck_()` / `flushPendingAcks_()`
  - `received_()` datagram shell
  - `receivedLong_()` / `receivedShort_()` shells
  - `consumeFrames_()` shell

- Keep role-specific policy small and explicit:
  - Initial key direction.
  - Destination/source CID choice.
  - Tx packet allocator and send function.
  - Client `markEstablished_()` vs server `HANDSHAKE_DONE`.
  - Client endpoint address vs server peer address.
  - Server first Initial bootstrap and app-level release.

- Avoid over-abstraction:
  - Do not introduce virtual interfaces.
  - Do not hide protocol differences behind opaque lambdas if a small role helper is clearer.
  - Leave bootstrap and endpoint setup in `CliLink`/`SrvLink`.

- Verification:
  - Run full `zquic/test`.
  - Build examples.
  - Review `ZquicClient.cc` and `ZquicServer.cc` for no public API changes.

### Phase 9: Resolve Retransmit-Drop Diagnostics

- Decide implementation truth:
  - Current `RetransmitQueue::dropped()` returns `0`, and `PacketTxSpace::retransmitDropped()` exposes that placeholder.
  - If retransmit queues are intentionally unbounded under current `ZmPQueue` policy, document that no drop policy exists and remove misleading README wording.
  - If bounded retransmit policy is added, count drops where enqueue fails or policy rejects an entry.

- Preferred minimal fix:
  - Keep the queue behavior unchanged.
  - Rename/document the diagnostic as always zero by construction, or remove it from public diagnostic language if not contractually needed.
  - Add a `ZquicRecoveryTest` assertion that captures the chosen behavior.

- If tracking is chosen:
  - Add `m_dropped` to `RetransmitQueue`.
  - Increment only on an actual failed enqueue/policy drop.
  - Return that value through `PacketTxSpace::retransmitDropped()`.

- Verification:
  - Run `ZquicRecoveryTest`.
  - Update `zquic/README.md` diagnostics language to match implementation.

### Phase 10: Final Documentation, Style Scans, And Full Verification

- Run style/resource scans:
  - `rg -n "requires\\s*\\(" zquic/src zquic/test zquic/example`
  - `rg -n "MaxLinks|MaxTxQueue|m_links\\[|m_txQueueBuf\\[|m_txQueueAddr\\[" zquic/src`
  - `rg -n "EVP_CIPHER_CTX_new" zquic/src/ZquicCrypto.cc zquic/src/ZquicPacket.cc`
  - Inspect remaining fixed arrays and confirm each is protocol-sized or explicitly justified.

- Run tests:
  - `make -j`
  - `make -C zquic/test test`
  - If needed, run each binary listed in `zquic/test/Makefile.am`.

- Update docs:
  - `zquic/README.md` diagnostics and policy language.
  - `zquic/buffers.md` only if endpoint/CID/crypto changes introduce a new buffer ownership or copy path.

- Review checklist:
  - No STL containers added to `zquic/src`.
  - New heap allocations use named `ZmHeap`, `ZmVHeap`, or named Z containers.
  - No hot-path unbounded allocation without explicit policy and diagnostics.
  - Public API remains CRTP/static-polymorphism oriented.
  - Server link/CID/endpoint queue limits are either runtime-sized or explicit policy with diagnostics.
  - Packet protection outputs remain unchanged.

## Code References to Impacted Code

- `zquic/src/Zquic.hh:1007` - Replace optional `App::listening()` `requires` check with detector.
- `zquic/src/Zquic.hh:1040` - Replace optional `App::listenFailed(bool)` `requires` check with detector.
- `zquic/src/Zquic.hh:1104` - Replace optional/required `App::accepted(const InitialInfo &)` detection with detector.
- `zquic/src/Zquic.hh:1121-1162` - Replace server `MaxLinks` fixed live-link table.
- `zquic/src/Zquic.hh:1500` - Replace stream Tx-thread optional callback detection.
- `zquic/src/Zquic.hh:1511` - Replace stream Tx invoke optional callback detection.
- `zquic/src/Zquic.hh:1682-1855` - Replace `MaxConnectionIDs` fixed CID arrays with explicit local policy and Z-backed CID store.
- `zquic/src/Zquic.hh:1797` - Replace optional `retiredLocalCID_()` callback detection.
- `zquic/src/Zquic.hh:1988` - Replace optional `statelessReset()` callback detection.
- `zquic/src/Zquic.hh:2452-2510` - Refactor protected frame dispatch to `switch`.
- `zquic/src/Zquic.hh:2708-3108` - Client send/crypto/ACK/receive helpers targeted for role-neutral extraction.
- `zquic/src/Zquic.hh:3155-3505` - Server send/crypto/ACK/receive helpers targeted for role-neutral extraction.
- `zquic/src/ZquicEndpoint.cc:21-132` - Replace fixed endpoint Tx ring and add back-pressure diagnostics.
- `zquic/src/ZquicEndpoint.hh:58-96` - Expose any new endpoint diagnostics.
- `zquic/src/ZquicSock.hh:32` - Add endpoint diagnostic fields if needed.
- `zquic/src/ZquicTransportParams.hh:18-36` - Ensure `activeConnectionIDLimit` is set from actual local policy.
- `zquic/src/ZquicTransportParams.cc:24-225` - Preserve validation/encoding; add policy tests around decode values below 2 and advertised value.
- `zquic/src/ZquicCrypto.hh:32-123` - Add reusable Initial protection state or overloads.
- `zquic/src/ZquicCrypto.cc:274-344` - Remove per-operation OpenSSL context creation for Initial encrypt/decrypt/header mask.
- `zquic/src/ZquicPacket.cc:198-218` - Remove per-operation OpenSSL context creation for Retry integrity.
- `zquic/src/ZquicFrame.cc:25-263` - Refactor frame parser dispatch.
- `zquic/src/ZquicRecovery.hh:400-543` - Resolve retransmit-drop placeholder.
- `zquic/test/Makefile.am:17-41` - Existing focused test binary list; add new test sources only if a new standalone binary is warranted.
- `zquic/test/ZquicAPITest.cc` - Add detector/optional callback compile coverage.
- `zquic/test/ZquicEndpointTest.cc` - Add Tx queue FIFO/back-pressure diagnostics coverage.
- `zquic/test/ZquicCIDTest.cc` - Add CID policy/storage/route coverage.
- `zquic/test/ZquicCodecTest.cc` - Add full frame parser switch parity coverage.
- `zquic/test/ZquicPacketProtectionTest.cc` - Keep Initial/Retry packet protection vectors stable.
- `zquic/test/ZquicRecoveryTest.cc` - Add retransmit-drop chosen-behavior coverage.
- `zquic/test/ZquicRuntimeTest.cc` - Add 17+ simultaneous server link acceptance coverage.
- `zquic/README.md` - Update diagnostics and resource policy language.
- `zquic/buffers.md` - Update only if buffer ownership/copy behavior changes.

## Detailed Test Plan

- Baseline:
  - `make -j`
  - `make -C zquic/test test`

- SFINAE/API tests:
  - `ZquicAPITest` compile/runtime cases for apps/links with and without every optional callback.
  - Confirm missing required `accepted(const InitialInfo &)` still produces the existing runtime error path.

- Server live-link tests:
  - Extend `ZquicRuntimeTest` with at least 17 accepted links.
  - Verify links remain alive while table holds refs.
  - Verify close/release removes live-link entries and retires routes.

- Endpoint Tx queue tests:
  - Force a queued send by holding an active send.
  - Verify FIFO ordering.
  - Fill to policy limit and verify enqueue returns `false`.
  - Verify `EndpointDiag` back-pressure counter increments exactly once per failed enqueue.

- CID tests:
  - Decode rejects `active_connection_id_limit < 2`.
  - Local transport params advertise the implementation's actual local CID policy.
  - NEW_CONNECTION_ID duplicate sequence/different CID is rejected.
  - NEW_CONNECTION_ID duplicate CID/different sequence is rejected.
  - `retire_prior_to` retires lower sequence IDs before active count enforcement.
  - Active peer CID overflow triggers the chosen close/failure path.
  - Route association/retirement remains idempotent.

- Crypto tests:
  - Existing RFC/sample packet protection vectors remain unchanged.
  - Retry integrity sample remains unchanged.
  - Add a stat/hook assertion if available to show Initial/Retry paths do not allocate a fresh OpenSSL context per operation.

- Frame dispatch tests:
  - Existing `ZquicCodecTest` frame round trips continue to pass.
  - Add coverage for stream frame flag combinations if missing.
  - Confirm invalid/truncated encodings still return errors.

- Runtime de-dup tests:
  - Full `zquic/test`.
  - `ZquicRuntimeTest` two-client flow.
  - `ZquicLoopTest`, `ZquicFlowTest`, and `ZquicStreamTest`.

- Recovery diagnostics:
  - `ZquicRecoveryTest` covers real drop accounting or always-zero-by-construction documentation.

## Acceptance Criteria

- `plan.md` remediation items from `zquic.md` are all addressed by implementation phases with tests.
- No source matches remain for forbidden `requires` usage in zquic.
- Server no longer has a hard 16 live-link cap.
- Endpoint Tx backlog no longer uses manual fixed arrays with separately maintained head/tail/count, and back-pressure is explicitly diagnosed.
- CID storage and transport parameter advertisement are aligned with RFC 9000 and local policy.
- Initial/Retry packet crypto no longer allocates OpenSSL contexts per packet operation, or any remaining context allocation is one-time/reusable and documented.
- Frame dispatch uses `switch`/helper dispatch while preserving parse and frame-consume behavior.
- Client/server runtime duplication is reduced without public API changes.
- Retransmit-drop diagnostics match implementation truth.
- Full zquic tests pass, or any environmental failure is documented with the exact command and failure.
- Generated files remain untouched.

## Non-goals

- No QUIC feature expansion beyond remediating `zquic.md`.
- No public application API redesign.
- No migration to C++ concepts, virtual transport interfaces, STL containers, or exceptions in hot paths.
- No broad reformatting or unrelated cleanup.
- No changes to generated build artifacts.
- No change to wire-visible packet protection, frame encoding, version negotiation, Retry integrity, or stateless reset behavior except for mandated error handling around CID policy.
- No new top-level test runner.

## Options and Open Questions

- CID store implementation:
  - Preferred now: named local active CID policy plus small `ZtArray<LinkCID>`/Z container with explicit active-count enforcement.
  - Later option: two-index `ZmHash` keyed by sequence and CID if policy limits become large enough to justify more complexity.
  - No user decision required for the remediation pass; choose the preferred bounded policy unless implementation evidence shows it is inadequate.

- Endpoint Tx queue bound:
  - Preferred now: retain bounded back-pressure semantics with an explicit policy constant and diagnostic counter.
  - Alternative: unbounded named-heap queue, but this weakens packet-send-path resource control and should not be chosen without a broader memory policy.
  - No user decision required.

- Retry/Initial crypto backend:
  - Preferred: reuse `Ztls::Pico::AeadCtx`/`CipherCtx` where the API fits the AES-GCM/AES-ECB operations.
  - Alternative: local reusable OpenSSL RAII wrapper if picotls cannot express Retry integrity's AAD-only tag calculation cleanly.
  - Implementation should decide after compiling a small local use of the Pico API; no product decision is required.

- Retransmit-drop diagnostic:
  - Preferred minimal fix: document/remove the placeholder if the queue is unbounded by construction.
  - Alternative: add bounded policy and real counter.
  - No open requirement forces bounded retransmit drops, so do not add a new drop policy unless tests reveal actual enqueue failure behavior.

- No unresolved legacy open questions remain. The implementation can proceed with the preferred options above.
