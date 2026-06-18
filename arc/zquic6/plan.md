# Plan to Address `findings.md`

Scope: fix the `zquic` shard-ownership and teardown issues identified in
`arc/zquic6/findings.md`, preserving the existing Rx/Tx split and continuation style.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

## 1. Endpoint connection ownership and send path

Files:
- `zquic/src/ZquicEndpoint.hh`
- `zquic/src/ZquicEndpoint.cc`
- `zquic/test/ZquicEndpointTest.cc`
- runtime tests that exercise client/server packet flow

Steps:
1. Split endpoint connection state by shard instead of reading Rx state from Tx.
   Keep `m_cxn` Rx-exclusive and add a Tx-exclusive active connection handle,
   for example `m_txCxn`, plus a Tx generation value if needed.
2. During UDP connection creation, install the Tx handle by posting from the Rx
   connection setup path to Tx. The Tx handle should be a `ZmRef<Cxn_>` so queued
   sends can safely hold the connection object without depending on `m_cxn`.
3. Change `Endpoint::send()` so it never reads `m_cxn`.
   It should dispatch to Tx and have the Tx-side body check `m_txCxn`,
   generation, and closing state before calling `Cxn_::sendPkt()`.
4. Audit and fix adjacent public endpoint accessors that read Rx-owned state from
   arbitrary callers:
   - `connected()`
   - `diag()`
   - `sockDiag()`
   - `pathHint()`
   Prefer explicit Rx/Tx snapshots or route the work to the owning shard.
5. Keep the packet data path allocation-free apart from the existing `ZiIOBuf`
   and Tx queue nodes. Do not add locks around shard-owned state.

Acceptance:
- `Endpoint::send()` has no direct `m_cxn` read.
- Tx packet sends operate only on Tx-owned state after the initial dispatch.
- Endpoint tests and runtime client/server send tests still pass.

## 2. Endpoint close and Tx callback drain

Files:
- `zquic/src/ZquicEndpoint.hh`
- `zquic/src/ZquicEndpoint.cc`
- `zquic/test/ZquicEndpointTest.cc`

Steps:
1. Convert endpoint close into an explicit Rx drain followed by Tx drain.
   Rx should:
   - mark the active connection closing,
   - close/disconnect Rx I/O,
   - clear Rx callbacks that must not be called after close,
   - retain enough connection identity to drain Tx.
2. Add a Tx-side close/drain helper on `Cxn_` that:
   - marks `m_closing`,
   - clears `m_txBuf`,
   - drains `m_txQueue`,
   - clears the endpoint's Tx-owned connection handle only if the generation
     still matches.
3. For blocking `closeUDP()`, post the semaphore only after both Rx and Tx drain
   continuations have run. For non-blocking close from Rx/Tx, preserve the same
   ordering without waiting.
4. Ensure `Endpoint::~Endpoint()` cannot return while Tx callbacks holding a raw
   `Endpoint *` can still run. If destruction cannot synchronously wait from the
   current shard, assert or route through the same close sequence already used by
   `closeUDP()`.
5. Keep `Cxn_`'s raw `Endpoint *` back-pointer only if the owner drain rule is
   made true; otherwise replace uses that can outlive the endpoint with captured
   endpoint-owned data.

Acceptance:
- Blocking close waits for Rx disconnect and Tx queue/callback drain.
- Non-blocking close cannot leave queued Tx work dereferencing a destroyed
  endpoint.
- Tests cover close while a send is queued or in progress.

## 3. Timer teardown barrier

Files:
- `zquic/src/Zquic.hh`
- `zquic/test/ZquicTimerTest.cc`
- close/release tests in `zquic/test/ZquicStreamTest.cc` or runtime tests

Steps:
1. Move `m_timerTeardown` into the Tx-owned state block, or clearly mark it as
   Tx-owned with the timer handles if the surrounding layout is adjusted.
2. Change `teardownTimers(Fn)` so it posts to Tx first:
   - on Tx, set `m_timerTeardown = true`,
   - call `cancelTimers_()`,
   - post one Tx continuation to run after already-queued timer callbacks,
   - invoke the supplied continuation from that Tx continuation.
3. Keep release callers' existing pattern of posting back to Rx from the
   continuation, but make the continuation run only after the Tx timer drain.
4. Update timer callbacks to rely on Tx ownership, not cross-shard reads.
5. Reset `m_timerTeardown` only during Tx runtime reset/reuse, on Tx.

Acceptance:
- No Rx path writes `m_timerTeardown` directly.
- Timer cancellation and late-callback suppression run on the timer callback
  thread.
- `teardownTimers()` remains the only release barrier used before link removal.

## 4. Clarify `cancelTimers()` versus teardown

Files:
- `zquic/src/Zquic.hh`
- `zquic/test/ZquicTimerTest.cc`

Steps:
1. Keep `cancelTimers()` for live close/reset logic where the link object remains
   owned and reachable.
2. Add assertions or naming comments making it clear that `cancelTimers()` is not
   a destruction barrier.
3. Audit destruction/release paths and ensure they use `teardownTimers()`:
   - `releaseLink_()`
   - `clearLinks_()`
   - `closeEndpoint_()`
   - any future link-removal helper found during implementation.
4. If a close path can become a release path, route it through
   `teardownTimers()` instead of plain `cancelTimers()`.

Acceptance:
- All object release/removal paths use the Tx teardown barrier.
- `cancelTimers()` has no misleading use as a lifetime barrier.

## 5. Diagnostics and public accessors

Files:
- `zquic/src/Zquic.hh`
- `zquic/src/ZquicEndpoint.hh`
- tests that call `runtimeDiag()`, `pathDiag()`, `endpointDiag()`, `txBytes()`,
  and `rxBytes()`

Steps:
1. Replace mixed-shard diagnostic reads with explicit snapshot aggregation:
   - collect Rx diagnostics on Rx,
   - collect Tx diagnostics/path diagnostics on Tx,
   - merge the small POD snapshots after both are captured.
2. For public client/server `runtimeDiag()` and `pathDiag()`, choose one of:
   - blocking `rxInvoke`/`txInvoke` aggregation when called off-shard,
   - shard-specific APIs such as `rxDiagSnapshot()` and `txDiagSnapshot()`,
   - test-only direct helpers guarded by naming and assertions.
3. Keep hot-path counters non-atomic unless a public accessor truly requires
   cross-shard relaxed reads. Prefer snapshots over atomics.
4. For stream byte accessors, separate API intent:
   - `txBytes()` should assert or execute on Tx when reading `m_txBytes`,
   - `rxBytes()` should assert or execute on Rx when reading `m_rxDelivered`,
   - tests that use synchronous fake apps can keep direct calls if they are
     moved behind test-only helpers or same-shard assertions.

Acceptance:
- Public diagnostics no longer directly combine Rx-owned and Tx-owned state in
  one unsharded accessor.
- Stream public accessors either enforce shard ownership or are clearly
  documented/test-only.

## 6. Verification

Build and test after each major phase:
1. `make -C zquic/src -j8`
2. `make -C zquic/test -j8`
3. Targeted tests:
   - `./zquic/test/ZquicEndpointTest`
   - `./zquic/test/ZquicTimerTest`
   - `./zquic/test/ZquicStreamTest`
   - `./zquic/test/ZquicRuntimeTest`
4. If all targeted tests pass, run top-level `make -j8` and `make test`.

Additional checks:
- Use `rg` to confirm no remaining direct public mixed-shard reads:
  - `rg -n "m_cxn|m_timerTeardown|runtimeDiag_|pathDiag_|txBytes\\(|rxBytes\\(" zquic/src/Zquic*.hh zquic/src/Zquic*.cc`
- Review every lambda capturing `Endpoint *`, `Link *`, or `Cxn_ *` to confirm
  either the owner remains alive through the continuation or the capture is a
  `ZmRef`.
- Confirm no locks or atomics were added merely to paper over shard ownership.

## Final Acceptance Criteria

- Implementation is audited and aligned with `GUIDELINES.md`
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
