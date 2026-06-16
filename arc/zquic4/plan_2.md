## Requirements
- Track PTO-eliciting, ACK-eliciting, in-flight, and retransmittable state for Initial, Handshake, and 1-RTT packet number spaces.
- Process ACK effects for all packet spaces.
- Discard Initial and Handshake sent-packet/key state after it is no longer needed.

## Summary
Preconditions: plan 1 complete enough to provide explicit PTO/key-discard timer ownership.

`zquic` already has `PktTxSpace`, sent packet records, retransmittable `SentFrameRef`, ACK processing, and 1-RTT PTO.  The gap is runtime use: ACK processing returns early for non-1RTT effects, PTO is scheduled only for 1-RTT, and handshake CRYPTO loss recovery is incomplete.

The plan is to keep the existing packet-space array and extend runtime state transitions so every packet number space has the same ACK/loss/PTO bookkeeping, while preserving packet-space-specific frame legality and key discard.

## Architecture Documentation
New or changed components: enhance `PktTxSpace` usage and add packet-space helpers for Initial, Handshake, and 1-RTT selection.

New or changed processes or threads: Tx thread owns sent-packet mutation.  Rx decrypt/parse posts ACK snapshots to Tx as today.

New or changed interfaces: add internal APIs that accept `CryptoLevel::T`/packet space and return outstanding ACK-eliciting/in-flight state.

New or changed data flows: send path records sent packets in the space that produced the packet; ACK path updates that same space; discard path clears obsolete spaces.

New or changed event-driven or timer processing: PTO timer from plan 1 chooses among spaces rather than assuming 1-RTT.

New or changed network programming: long-header Initial/Handshake retransmission must build packets with correct headers and packet protection.

New or changed data stores: retain existing arrays where possible; add per-space discard state to avoid using dropped keys.

## Detailed Design and Implementation Plan
### Phase 1: Audit Current Recording
- Confirm all long-header send helpers call `sentPacket_()` with the correct `CryptoLevel`.
- Remove any implicit 1-RTT assumptions in sent-packet accounting.
- Keep `SentFrameRef` unchanged unless plan 21 proves a lifetime issue.

### Phase 2: ACK Effects for All Spaces
- Update `processAckFrameTx_()` so Initial and Handshake ACKs acknowledge sent packets, mark packet-threshold loss, and enqueue retransmittable CRYPTO/control frames.
- Restrict RTT sampling by QUIC rules: use appropriate sent time and ACK delay only for application data where permitted.
- Preserve the existing 1-RTT immediate retransmit behavior.

### Phase 3: Space-Aware PTO Selection
- Add a helper that selects the PTO packet space from outstanding Initial, Handshake, and 1-RTT state.
- Reclaim or schedule probe packets in that selected space.
- Make sure handshake anti-deadlock probes do not require application data.

### Phase 4: Discard
- When Initial/Handshake keys are discarded, clear corresponding sent-packet state and retransmit refs.
- Hook key-discard timer from plan 1 and crypto state transitions.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:2887` - Current ACK processing returns early for non-1RTT behavior.
- `zquic/src/Zquic.hh:2991` - Sent-packet recording currently schedules PTO only for 1-RTT ACK-eliciting packets.
- `zquic/src/Zquic.hh:2791` - PTO reclaim currently uses `m_txPkts[CryptoLevel::OneRTT]`.
- `zquic/src/ZquicRecovery.hh:546` - `PktTxSpace::ack()` already supports ACK/loss marking and should be reused.
- `zquic/src/ZquicCrypto.hh` - Key state transitions should drive Initial/Handshake discard.

## Detailed Test Plan
- Add recovery unit tests for ACK/loss in Initial and Handshake spaces using `PktTxSpace` directly.
- Add runtime tests that drop client Initial CRYPTO and server Handshake CRYPTO packets, then verify PTO/loss recovery completes the handshake.
- Add tests that ACK discarded packet spaces and verify they are ignored or rejected without mutating freed key state.

## Non-goals
- Do not introduce a new sent-packet container.
- Do not add CUBIC/pacing or congestion behavior here.
- Do not implement 0-RTT sent-packet processing; that belongs to plan 17.

## Options and Open Questions
- Option: map `CryptoLevel` and QUIC packet spaces explicitly if 0-RTT later needs application packet-number handling without handshake keys.
- Open question: whether CRYPTO retransmission should stay as `SentFrameRef::crypto` or move to a richer frame chain in plan 21.

## Acceptance Criteria
- Lost Initial/Handshake CRYPTO data is retransmitted by PTO/loss recovery.
- ACK of Initial/Handshake packets updates sent-packet state and timer scheduling.
- Tests cover dropped Initial and dropped Handshake CRYPTO packet recovery.
