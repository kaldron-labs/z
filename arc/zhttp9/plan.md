# zquic I/O Sharding Fix Plan

This plan implements the ownership rules in `iosharding.md` for `zquic`.
The goal is to remove cross-thread queue/state access, not to paper over it
with locks.

Align all work with `GUIDELINES.md`

## Phase 0: Stabilize the Baseline

1. Rebuild from the current tree and record the exact failing interop case.
2. Keep the focused reproducer first:
   `curl > zhttpd`, H3/QUIC, `-j10 -n1000`, 20s timeout.
3. Keep retained temp dirs/logs for failures until the relevant phase is fixed.
4. Do not broaden to the full 27-case interop matrix until the focused case is
   stable across repeated runs.

Exit criteria:

- ASan build completes.
- Focused reproducer result is known and documented.

## Phase 1: Make Ownership Visible

1. Group `Zquic::Stream` members into explicit blocks:
   `Shared identity`, `Rx-owned`, `Tx-owned`.
2. Group `Zquic::Link` members into explicit blocks:
   `Shared immutable/config`, `Rx-owned`, `Tx-owned`, `Diagnostics`.
3. Group `Zquic::Endpoint` and `Endpoint::Cxn_` members into explicit Rx/Tx
   blocks.
4. Add assertions to shard-only internal functions:
   Rx-only functions assert `rxInvoked()`.
   Tx-only functions assert `txInvoked()`.
5. Keep this phase mostly comment/assertion-only. Avoid behavior movement until
   the ownership map is readable.

Exit criteria:

- Data-member ownership can be audited at declaration sites.
- No new behavior changes beyond assertions/comments.
- Build passes.

## Phase 2: Endpoint Tx Boundary

1. Make `Endpoint::Cxn_::sendPkt()` Tx-only.
2. Make `Endpoint::send()` a dispatcher:
   if already on Tx, call `sendPkt()` directly;
   otherwise capture the packet buffer/address and post to `txRun`.
3. Keep receive callbacks, close routing, and Tx-drained notification on Rx.
4. Ensure Tx-drained posts back to Rx before link-level flushing.
5. Decide explicitly how `m_cxn` is shared:
   either stable enough to capture under the existing connection lifetime, or
   split into Rx/Tx connection handles.

Exit criteria:

- Endpoint send queue and active send buffer are touched only on Tx.
- Focused H3 concurrent case does not regress due to endpoint dispatch.

## Phase 3: Split Stream Rx and Tx APIs

1. Keep `Stream` inheriting both `ZmPQRx` and `ZmPQTx`.
2. Rename or group methods by shard:
   Rx methods: receive validation, Rx queue insertion, delivery to `RxStream`.
   Tx methods: Tx buffer publish, Tx queue inspection, packet range consume,
   FIN dequeue, Tx credit consume.
3. Remove Rx-side access to Tx queue helpers:
   `txRangeCount()`, `finReady()`, `nextTxRange()`, `commitTxRange()`.
4. Replace those with Tx-owned packetization entry points.
5. If Rx needs to know "there may be Tx work", it should post a Tx flush request
   instead of inspecting `ZmPQTx`.

Exit criteria:

- No Rx-only function reads or mutates `Stream::Tx` / `m_txQueue`.
- No Tx-only function reads or mutates `Stream::Rx` / `m_rxQueue`.
- Stream tests pass.

## Phase 4: Split Link Flush Into Rx Preparation and Tx Packetization

1. Replace `flushControlAndStreams_()` as a mixed-owner operation.
2. Rx-owned preparation:
   pending ACK generation, receive-side flow-control updates, path responses,
   control frames caused by received data.
3. Tx-owned packetization:
   stream Tx queue consumption, stream FIN dequeue, Tx flow-credit consumption,
   packet protection, packet number assignment, sent-packet recording,
   endpoint send submission.
4. Use fixed-size snapshots for small Rx-to-Tx control metadata.
5. For variable-size data, write into destination-owned buffers and capture only
   handles/metadata in posted lambdas.
6. Remove `withTxLock_()` from this path after the split is complete.

Exit criteria:

- Stream packetization runs only on Tx.
- Control/ACK preparation does not inspect stream Tx queues.
- Focused H3 concurrent case passes repeatedly.

## Phase 5: Move Recovery State Fully to Tx

`PktTxSpace` inherits `ZmPQTx`; therefore it is Tx-owned in its entirety.

1. Make sent-packet record, ACK processing, loss marking, retransmit enqueue,
   PTO reclaim, and retransmit dequeue Tx-only.
2. When Rx receives an ACK frame, snapshot the bounded ACK information and post
   it to Tx.
3. The Tx handler validates ACK ranges against Tx packet number state, updates
   RTT/PTO, marks loss, immediately enqueues retransmittable frames, and drains
   retransmission work.
4. PTO timers that touch recovery state should fire on Tx.
5. Remove any Rx-side direct access to `m_txPkts`.

Exit criteria:

- `m_txPkts`, `m_txPN`, `m_rtt`, `m_ptoBackoff`, and Tx PTO timer mutation are
  Tx-only.
- Loss-triggered retransmittable frames are still enqueued immediately, matching
  the intended `ZiTx` pattern.
- Focused H3 concurrent case passes repeatedly.

## Phase 6: Crypto and Key-Update Ownership

1. Split crypto state by direction:
   receive crypto reassembly and RX traffic secret handling are Rx-owned;
   transmit crypto flights, TX traffic secret handling, and key phase are
   Tx-owned.
2. Replace `installPeerKeyUpdate_()` locking with an Rx-to-Tx handoff:
   Rx validates/derives the incoming next secret as needed, then posts fixed
   metadata or a destination-owned secret object to Tx for Tx key update.
3. Ensure packet protection functions that mutate Tx packet number/key state are
   Tx-only.

Exit criteria:

- No `m_txLock` use remains for crypto/key state.
- Packet protection and packet recording are on the same Tx-owned path.

## Phase 7: Diagnostics

1. Split diagnostics into Rx and Tx counters, or make mixed counters atomic.
2. Prefer per-shard diagnostic structs and merge summaries when read.
3. Keep diagnostic reads on a snapshot API rather than reading live foreign
   shard state.

Exit criteria:

- Diagnostics do not require violating shard ownership.
- Existing debug logging still reports enough endpoint, packet, stream, and
  recovery activity for interop failures.

## Phase 8: Tests and Interop Hardening

1. Update unit tests that inspect queues directly to run inspection on the
   owning shard or through snapshot helpers.
2. Add/adjust tests for:
   stream Rx/Tx ownership boundaries,
   endpoint Tx dispatch,
   ACK snapshot to Tx recovery,
   immediate retransmit enqueue on loss.
3. Run focused interop repeatedly:
   `curl > zhttpd`, H3/QUIC, `-j10 -n1000`, 20s timeout.
4. Run all `curl > zhttpd` combinations:
   H1/TCP, H1/TLS, H3/QUIC across `-j1 -n1`, `-j1 -n1000`,
   `-j10 -n1000`.
5. Run the full 27-case matrix only after the focused and 9-case curl matrix
   are stable.

Exit criteria:

- Focused H3 concurrent case passes repeatedly.
- `curl > zhttpd` 9-case matrix passes.
- Full 27-case interop suite passes with each case under 20s.

## Phase 9: Cleanup

1. Remove obsolete lock-based helpers such as `withTxLock_()` after all callers
   are gone.
2. Remove stale comments that imply mixed ownership is acceptable.
3. Keep `iosharding.md` updated with any ownership decisions made during the
   refactor.
4. Summarize remaining risks, if any, in the final change notes.

Exit criteria:

- No lock remains as a substitute for owner-thread execution.
- Ownership comments match the code.
- Interop suite is stable.
