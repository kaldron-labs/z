# zquic GUIDELINES.md Audit Findings

Scope: `zquic`, with emphasis on `GUIDELINES.md` I/O sharding and timer
teardown rules.

## Findings

1. High: `Endpoint::send()` reads Rx-owned `m_cxn` from the caller thread,
   normally Tx.

   `zquic/src/ZquicEndpoint.cc:307` takes `Cxn_ *cxn = m_cxn`, while
   `zquic/src/ZquicEndpoint.hh:99` marks `m_cxn` Rx-exclusive. This is on the
   normal Tx packet path from client/server sends. It races Rx close/disconnect
   paths that null `m_cxn` at `zquic/src/ZquicEndpoint.cc:285` and
   `zquic/src/ZquicEndpoint.cc:347`. This does not align with the
   "post to owning shard" rule.

2. High: endpoint close drains Rx but not Tx queued `Cxn_` callbacks before the
   `Endpoint` owner can be destroyed.

   `Cxn_` holds a raw `Endpoint *` back-pointer at
   `zquic/src/ZquicEndpoint.cc:171`. Tx lambdas and send callbacks keep `Cxn_`
   alive and continue using that back-pointer, for example
   `zquic/src/ZquicEndpoint.cc:95`, `zquic/src/ZquicEndpoint.cc:140`, and
   `zquic/src/ZquicEndpoint.cc:157`. But `Endpoint::~Endpoint()` just calls
   `closeUDP()` (`zquic/src/ZquicEndpoint.hh:38`), and `closeUDP_()` only
   closes on Rx and waits for Rx disconnect in the blocking case
   (`zquic/src/ZquicEndpoint.cc:277`). This is missing the guideline's Rx
   drain -> Tx drain -> release pattern.

3. High: timer teardown sets and reads `m_timerTeardown` on different shards
   without making it Tx-owned.

   `teardownTimers()` can be called from Rx close/release paths
   (`zquic/src/Zquic.hh:1459`, `zquic/src/Zquic.hh:6367`), sets
   `m_timerTeardown`, and cancels timers immediately (`zquic/src/Zquic.hh:4237`).
   Timer callbacks read the same flag on Tx (`zquic/src/Zquic.hh:4265`). The
   flag is not atomic and sits in the Rx-exclusive block
   (`zquic/src/Zquic.hh:5959`). Per `GUIDELINES.md`, cancellation and
   callback-drain continuation need to run on the timer callback thread.

4. Medium: `cancelTimers()` is used as ordinary close/reset logic, but it is not
   a full teardown barrier.

   `Link::close()` and reset paths call `cancelTimers()`
   (`zquic/src/Zquic.hh:2423`, `zquic/src/Zquic.hh:3185`,
   `zquic/src/Zquic.hh:3197`), which only calls `del()` through
   `cancelTimers_()` (`zquic/src/Zquic.hh:4249`). That may be fine for keeping a
   live closed link quiet, but not for object release. The release paths partly
   compensate with `teardownTimers()`, but the split makes it easy to add a
   destruction path that only cancels without draining.

5. Medium: public diagnostics/accessors directly mix Rx and Tx state.

   Examples: `runtimeDiag_()` returns `{m_rxDiag, txDiag_()}`
   (`zquic/src/Zquic.hh:2632`), and `txDiag_()` reads Tx packet queues/timers
   (`zquic/src/Zquic.hh:2852`). `pathDiag_()` reads Tx-owned `m_path` at
   `zquic/src/Zquic.hh:2633`. Client/server expose these directly at
   `zquic/src/Zquic.hh:6126` and `zquic/src/Zquic.hh:6950`. These should be
   shard-specific snapshots or explicit `rxInvoke`/`txInvoke` aggregations.

6. Medium: stream public accessors are mostly unsharded despite members being
   explicitly Rx/Tx-owned.

   For example `txBytes()` reads Tx-owned `m_txBytes` and `rxBytes()` reads
   Rx-owned `m_rxDelivered` (`zquic/src/Zquic.hh:1534`,
   `zquic/src/Zquic.hh:1544`), while those members are split at
   `zquic/src/Zquic.hh:2036`. Tests use these freely, but as API shape this
   conflicts with the guideline unless documented as same-shard/test-only.

## Expected Direction

Make `Endpoint::send()` a Tx-owned path that does not read Rx-owned `m_cxn`;
either maintain a Tx-owned connection handle installed/removed by Rx posts, or
enqueue endpoint Tx work through an explicit Rx-to-Tx handoff during connection
setup/teardown.

For timers, make teardown start by posting to Tx: set the teardown flag on Tx,
`del()` all timers on Tx, then post a Tx continuation to drain late callbacks,
then continue object release back on Rx if needed.

Tests were not run; this was a source audit against `GUIDELINES.md`.
