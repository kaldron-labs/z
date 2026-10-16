# Zquic Rx/Tx Sharding Review

No. The diff contains necessary sharding work, but also unnecessary
synchronization, semantic regressions, and several unresolved ownership
violations.

## Findings

1. **High: CID ownership becomes inconsistent after migration.**

   Tx promotes a CID by mutating only `m_txCIDs` in
   `zquic/src/ZquicLink.hh:3182`. Rx's `m_peerCID`/`m_peerCIDs` remain
   unchanged. Any later `NEW_CONNECTION_ID` or retirement posts a fresh
   snapshot from the stale Rx state at `zquic/src/ZquicLink.hh:2976`,
   overwriting the promoted Tx CID. Subsequent packets can revert to the old
   DCID.

   Promotion must post the selected CID/sequence back to the Rx-owned canonical
   CID state, or CID selection must become wholly Tx-owned with Rx sending
   deltas.

2. **High: `Endpoint_` still shares a non-primitive reference across socket
   shards.**

   Socket Rx replaces/releases `m_cxn` and updates its generation at
   `zquic/src/Zquic_.hh:688`, while socket Tx copies the same `CxnRef` and
   generation at `zquic/src/Zquic_.hh:801`. This is precisely the kind of
   compound ownership the diff is intended to eliminate.

   Maintain a Tx-owned connection reference/generation installed and cleared
   by ordered Rx-to-Tx posts.

3. **High: server path promotion still writes Rx state on Tx.**

   `promotePath_()` runs on Tx, but `SrvLink::pathPromoted_()` writes Rx-owned
   `m_peerAddr` directly at `zquic/src/ZquicSrvLink.hh:1212`. Capture the
   promoted address and post it to Rx.

4. **High: stateless-reset handling still mutates Tx containers on Rx.**

   `checkStatelessReset_()` is receive-side processing, but directly cleans
   `m_streamQueue` and `m_txPkts` at `zquic/src/ZquicLink.hh:3954`. Those
   members are now explicitly Tx-owned. The cleanup needs an Rx-to-Tx
   continuation.

5. **Medium: stream atomics mask unresolved multi-writer ownership.**

   `m_error` and `m_appError` are written by local `reset()`/`stop()` at
   `zquic/src/ZquicStream.hh:462` and by Rx `receiveReset()`/`receiveStop()` at
   `zquic/src/ZquicStream.hh:532`. Atomics make individual accesses coherent
   but leave the resulting error nondeterministic.

   Moreover, public `fin()`, `reset()`, and `stop()` directly mutate members
   now labelled Tx-exclusive. They need Tx dispatch, or an explicitly defined
   application-producer command path.

6. **Medium: removing `Endpoint::connected()` checks is unnecessary and
   weakens send semantics.**

   PTO, flush, and every packet sender now test only whether the Tx-owned
   address is non-null, for example `zquic/src/ZquicCliLink.hh:295` and
   `zquic/src/ZquicCliLink.hh:1144`. A path address remains populated after the
   socket connection is down, so packets can be recorded as submitted/sent
   before socket Tx drops them.

   Under the stated primitive-sharing rule, `connected()` is a valid advisory
   single-writer flag. Use the Tx path address for addressing, but retain the
   connection-state guard.

7. **Medium: off-Rx `disconnect(errorCode)` loses its return value.**

   Both client and server discard `disconnectError_()`'s result and
   unconditionally return `true`; see `zquic/src/ZquicCliLink.hh:101` and
   `zquic/src/ZquicSrvLink.hh:229`. Repeated or rejected disconnects therefore
   report success only when called off-shard. Since `rxInvoke` is synchronous,
   capture and return the actual result.

## Unnecessary Changes

Given the stated primitive-sharing rule, these atomics are not required:

- `Endpoint_::{m_connected,m_listening,m_open}` at
  `zquic/src/Zquic_.hh:885`.
- `Link::m_linkState` at `zquic/src/ZquicLink.hh:9364`.
- Monotonic, single-writer stream handshakes `m_txTerminal`, `m_rxTerminal`, and
  `m_txReapable` at `zquic/src/ZquicStream.hh:826`.

Plain primitive snapshots are sufficient if their writer and stability
assumptions are documented. Keep existing atomics that genuinely have multiple
mutators, such as queue admission counts and disconnect arbitration.

The address lock at `zquic/src/Zquic_.hh:869` is not required for internal
Rx/Tx sharding once protocol Rx owns `m_rxLocal`/`m_rxPeer` and protocol Tx owns
`m_path`. It is only supporting synchronous cross-thread public getters and
conflicts with the repository's "do not lock to legitimize non-owner access"
guidance. Prefer shard-owned snapshots or an explicitly documented API-only
publication mechanism.

`postTxCIDs_()` also re-derives Initial keys on every CID addition/retirement at
`zquic/src/ZquicLink.hh:2989`. Initial-key publication should be separate from
routine peer-CID snapshot updates.

The extra Rx wrapper in `zquic/test/ZquicSockTest.cc:117` is redundant because
`openUDP()` now dispatches itself.

## Changes That Are Genuinely Necessary

The following parts are directionally correct and should remain:

- Dispatching server startup and client connect/disconnect mutations onto
  protocol Rx.
- Moving stream limits, stream allocation ordinals, and closed-stream tracking
  to Rx.
- Posting control-frame mutation and flow updates to Tx.
- Splitting packet reset, timers, congestion, path, and protection state onto
  Tx.
- Publishing transport-parameter, crypto-key, and CID snapshots to Tx.
- Using the Tx-owned active path address instead of socket-Rx endpoint
  addresses.
- Two-phase stream reaping: Tx drains sent state, then Rx removes the stream.
- Moving `receiveStop()`'s Tx completion callback onto Tx.
- Keeping close/handshake flags on Tx when all meaningful consumers are Tx.

## Verdict

The diff is directionally useful but should not be accepted as a completed
sharding correction. The CID split is currently functionally unsafe, and the
endpoint connection reference, server promotion, stateless-reset cleanup, and
stream application APIs still violate the intended ownership model.

No rebuild was performed; this conclusion is from static review.
