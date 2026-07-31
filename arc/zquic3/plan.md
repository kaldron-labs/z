# zquic findings remediation plan

Goal: address every item in `zquic.md` while preserving the current public transport behavior and keeping changes reviewable. Do this as a sequence of small patches, with tests after each phase.

## Phase 1: baseline and guardrails

1. Capture the current test baseline:
   - Build `zquic`: `make -j` from the configured tree.
   - Run `make -C zquic/test test` if the local build supports it.
   - Also run the focused binaries touched by later phases: `ZquicAPITest`, `ZquicRuntimeTest`, `ZquicEndpointTest`, `ZquicCIDTest`, `ZquicRecoveryTest`, `ZquicPacketProtectionTest`, `ZquicHandshakeTest`.

2. Add regression checks where current tests are thin:
   - Server accepts more than 16 simultaneous links after the server table change.
   - Endpoint send backlog reports queue-full/back-pressure explicitly.
   - CID storage respects `active_connection_id_limit` and rejects/diagnoses policy overflow.
   - Frame parsing still accepts all currently covered frame encodings after switch refactor.

3. Keep generated files untouched:
   - Do not edit `.libs`, `.deps`, object files, generated `Makefile`, or `Makefile.in`.
   - Update only source, headers, tests, and documentation.

## Phase 2: remove `requires` from public templates

1. Introduce local detector traits in `Zquic.hh` or a small internal helper block:
   - `HasListening<App>`
   - `HasListenFailed<App>`
   - `HasAccepted<App>`
   - `HasTxInvoked<Link>`
   - `HasTxInvoke<Link, Fn>`
   - `HasRetiredLocalCID<Impl>`
   - `HasStatelessReset<Impl>`
   - `HasStreamFrame<Impl>`
   - `HasDisconnected<Impl>`

2. Implement detectors with the repository pattern:
   - `typename = void` primary template.
   - `decltype(CODE, void())` specialization.
   - `ZuIfT` or existing `Zu` trait idioms where a constrained overload is cleaner than branching.

3. Replace every `if constexpr (requires ...)` call site with detector-based dispatch:
   - Preserve optional callback semantics exactly.
   - Avoid adding virtual interfaces or concepts.
   - Keep public app/link API names unchanged.

4. Verification:
   - `rg -n "requires\\s*\\(" zquic/src zquic/test zquic/example` must return no source matches.
   - Run `ZquicAPITest` and `ZquicRuntimeTest`.

## Phase 3: replace the fixed server live-link table

1. Replace `Server::m_links[MaxLinks]` with a Z container:
   - Prefer a `ZmHash<LinkRef>` or intrusive hash/list keyed by link pointer.
   - Use a named heap such as `"Zquic.Server.LinkTable"`.
   - Set initial hash params conservatively, without a hard live-link cap.

2. Update operations:
   - `addLink_()` becomes lookup-or-insert by pointer.
   - `releaseLink_()` removes by pointer and retires routes.
   - `clearLinks_()` clears the route table and link table.

3. Preserve ownership behavior:
   - The table must hold `ZmRef<Link>` so accepted links remain alive.
   - Route retirement must remain idempotent.

4. Verification:
   - Add or extend a runtime/server test that accepts at least 17 links.
   - Run `ZquicRuntimeTest`, `ZquicAPITest`, and `ZquicCIDTest`.

## Phase 4: replace endpoint Tx fixed ring and account back-pressure

1. Replace `Endpoint::Cxn_` Tx backlog arrays with a Z container:
   - Use a compact intrusive queue/list node that owns `ZmRef<ZiIOBuf>` plus `ZiSockAddr`.
   - Use a named heap such as `"Zquic.Endpoint.TxQueue"`.
   - Keep FIFO order identical to the current ring.

2. Add explicit diagnostics:
   - Add an endpoint counter for Tx enqueue failures or back-pressure.
   - Increment it before returning `false` from send enqueue failure.
   - Document the counter in `zquic/README.md` if it is part of public diagnostics.

3. Decide on a policy limit:
   - If the queue remains bounded, make the bound configurable or clearly named as endpoint policy.
   - If unbounded, rely on heap telemetry and higher-level flow/congestion control.

4. Verification:
   - Extend `ZquicEndpointTest` to force queued sends and queue-full/back-pressure behavior if bounded.
   - Run `ZquicEndpointTest`, `ZquicRuntimeTest`, and `ZquicLoopTest`.

## Phase 5: make connection ID storage runtime-policy aligned

1. Choose the policy:
   - Either support runtime-sized CID state up to peer/local `active_connection_id_limit`, or explicitly cap local policy and advertise/enforce that cap.
   - If capped, add a named constant such as `LocalActiveConnectionIDLimit` and make transport parameter validation reject incompatible peer usage with diagnostics.

2. Replace raw arrays if runtime-sized support is chosen:
   - Use `ZtArray`/`ZtScratch` for small-vector style storage or `ZmHash` keyed by sequence/CID.
   - Use named heaps such as `"Zquic.Link.LocalCID"` and `"Zquic.Link.PeerCID"`.
   - Keep current lookup paths by sequence and by CID efficient.

3. Keep route interactions exact:
   - `installLocalCIDRoutes_()` should only associate active, unassociated CIDs.
   - `retireLocalCIDRoutes_()` should retire only associated active/retired CIDs once.
   - `receiveNewConnectionID_()` must still reject malformed frames and honor `retire_prior_to`.

4. Verification:
   - Extend `ZquicCIDTest` for more than 8 advertised CIDs if runtime-sized.
   - Add policy rejection tests if a local cap remains.
   - Run `ZquicCIDTest`, `ZquicRuntimeTest`, and `ZquicHandshakeTest`.

## Phase 6: remove per-operation OpenSSL context churn

1. Audit existing Ztls wrappers:
   - Prefer `Ztls::Pico::AeadCtx` and `Ztls::Pico::CipherCtx` if they can cover Initial AES-GCM, Retry integrity, and header protection.
   - If not, add a small RAII/reusable backend wrapper in Ztls or Zquic with explicit ownership and no per-packet heap allocation.

2. Rework Initial protection:
   - Store reusable encryption/decryption/header-protection state beside `InitialKeyMaterial` or `PacketProtectionState`.
   - Initialize once per derived Initial secret.
   - Preserve exact packet-protection outputs covered by `ZquicPacketProtectionTest`.

3. Rework Retry integrity:
   - Avoid creating an `EVP_CIPHER_CTX` for every tag calculation.
   - Keep the RFC v1 key/nonce constants as fixed protocol data.

4. Verification:
   - Run `ZquicPacketProtectionTest`, `ZquicCryptoTest`, `ZquicHandshakeTest`, and `ZquicVersionTest`.
   - Add instrumentation or a targeted test hook if needed to prove Initial packet protect/unprotect no longer allocates a new OpenSSL context per operation.

## Phase 7: refactor frame dispatch from chained `if` to `switch`

1. Convert `FrameCodec::parse()`:
   - Use `switch (t)` for single frame types.
   - Group ranges with compact helper functions for stream frames, max-streams variants, and blocked variants.
   - Keep `used` behavior byte-for-byte compatible.

2. Convert protected frame dispatch:
   - Replace the `if`/`else if` chain in `consumeProtectedFrames_()` with a `switch (frame.type)`.
   - Preserve ACK accounting before dispatch.
   - Keep optional `streamFrame()` callback behavior via the detector from Phase 2.

3. Verification:
   - Run `ZquicCodecTest`, `ZquicFlowTest`, `ZquicStreamTest`, `ZquicRuntimeTest`, and `ZquicHandshakeTest`.

## Phase 8: de-duplicate client/server runtime plumbing

1. Extract role-neutral helpers into the existing `Link` base where possible:
   - `send(StreamRef, ZuCSpan, bool)` scheduling and payload copy.
   - `send_()` stream writing and flush.
   - `sendCryptoPacket_()` overloads.
   - `flushPendingAck_()` / `flushPendingAcks_()`.
   - `received_()`, `receivedLong_()`, `receivedShort_()`, and `consumeFrames_()` shells.

2. Keep role-specific policy as small callbacks or traits:
   - Initial key direction.
   - Destination/source CID choice.
   - Tx packet allocation/send function.
   - Client `markEstablished_()` vs server `HANDSHAKE_DONE`.
   - Client endpoint address vs server peer address.

3. Avoid premature abstraction:
   - Extract only the near-identical packet/runtime logic.
   - Leave genuinely role-specific bootstrap and endpoint setup in `CliLink`/`SrvLink`.

4. Verification:
   - Run the full `zquic/test` suite.
   - Diff public examples mentally and by build: `ZquicClient.cc` and `ZquicServer.cc` should not need API changes.

## Phase 9: make retransmit-drop diagnostics real or remove the placeholder

1. Decide whether retransmit drops are possible:
   - If the retransmit queue is intentionally unbounded, remove `dropped()`/`retransmitDropped()` or document that it is always zero by construction.
   - If bounded policy is introduced, track rejected retransmit entries.

2. If tracking:
   - Add a counter in `RetransmitQueue`.
   - Increment on enqueue failure or policy drop.
   - Expose the counter through `PacketTxSpace::retransmitDropped()`.

3. Verification:
   - Add `ZquicRecoveryTest` coverage for the chosen behavior.
   - Update README diagnostics language so it matches the implementation.

## Final verification

1. Run style scans:
   - `rg -n "requires\\s*\\(" zquic/src zquic/test zquic/example`
   - `rg -n "MaxLinks|MaxTxQueue|m_links\\[|m_txQueueBuf\\[|m_txQueueAddr\\[" zquic/src`
   - Inspect remaining fixed arrays and confirm each is protocol-sized or explicitly justified.

2. Run tests:
   - `make -j`
   - `make -C zquic/test test`
   - If the aggregate target is not reliable, run every binary listed in `zquic/test/Makefile.am`.

3. Update docs:
   - `zquic/README.md` diagnostics and scope language.
   - `zquic/buffers.md` only if a new copy path or buffer ownership path is introduced.

4. Review checklist:
   - No STL containers added to `zquic/src`.
   - New heap allocations use named `ZmHeap`/`ZmVHeap` or existing named Z containers.
   - No new hot-path allocation without a measured reason.
   - Public API remains CRTP/static-polymorphism oriented.
   - Server link/CID/endpoint queue limits are either runtime-sized or explicit policy with diagnostics.
