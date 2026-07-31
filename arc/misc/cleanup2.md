Zquic.hh Link cleanup candidates
================================

1. `m_runtimeCloseState` and `m_runtimeCloseError` look removable.
`m_runtimeCloseError` is written/reset but never read. `m_runtimeCloseState` is
only used for `isDraining_()`, while `m_linkState` is already set to
`LinkState::Draining` at the same site. Collapse this into `m_linkState` plus
one close error field.

2. `m_handshakeStarted` and `m_established` overlap heavily with `m_linkState`.
`m_linkState` already transitions `Starting -> Handshaking -> Established ->
Closing/Draining`, but the booleans are maintained separately. Replace
public/internal checks with helpers over `m_linkState`, then delete the booleans
if semantics line up.

3. `m_closed` / `m_closeError` should be consolidated with runtime close state.
There is a separate permanent close path using `m_closed` and `m_closeError`,
plus runtime close state via `m_linkState` and `m_runtimeClose*`. If these
represent distinct API-close vs QUIC-runtime-close states, encode that
explicitly in one small state struct. If not, remove the duplicate path.

4. `m_txTrafficSecrets[3]` in `Link` can likely be removed.
`PktProtState` already owns a `TrafficSecret secret`; `Link` keeps a parallel
`m_txTrafficSecrets[3]` only to check validity and read `tagLen` / derive next
key. `txTrafficSecret_()` could return `m_txProt[level].secret`, and installed
state could use `m_txProt[level].valid()`. This removes three large
`TrafficSecret` copies from `Link`.

5. `m_rxDataWindow` is probably redundant.
It is set from `m_transportParams.initialMaxData`, reset to zero, and
`maybeExtendMaxData_()` falls back to `m_transportParams.initialMaxData` when
zero. Unless future tuning requires an independent runtime receive window,
remove it and use `m_transportParams.initialMaxData` directly.

6. Consolidate stream-limit bookkeeping.
The current pieces are separate arrays: `m_peerLimit`, `m_localLimit`,
`m_queued`, `m_openQueuedPending`, `m_lastStreamsBlocked`. Group these into two
small structs, probably one Rx-owned peer-open state and one Tx-owned local-open
state. This does not necessarily remove fields, but it makes ownership and enum
indexing clearer.

7. Consolidate packet-space state.
The `[3]` arrays for crypto, packet numbers, ACKs, packet spaces, ECN,
discarded flags, and protection state are scattered across the member list. A
`RxPktSpaceState[3]` and `TxPktSpaceState[3]` would cut member count and make
resets/discards less error-prone.

8. `m_peerResetToken` may be derivable from peer CID state.
It is stored separately, but peer CIDs also carry reset tokens. If stateless
reset validation only needs the active peer CID's token, fold this into
`m_peerCIDs` / active path CID selection and remove the standalone token. Needs
care around the initial CID path.

Highest-confidence removals: `m_runtimeCloseError`, `m_runtimeCloseState`,
`m_txTrafficSecrets[3]`, and probably `m_rxDataWindow`. The lifecycle booleans
are worth doing, but only after deciding the exact semantics of permanent
application close vs QUIC runtime close.
