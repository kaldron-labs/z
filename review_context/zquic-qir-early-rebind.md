# Owner-final QIR early handshake/NAT failure

Read-only diagnosis of
`qir-owner-final-rebind-check/zquic-server_ngtcp2-client/logs_2026-11-11T18:36:25/zquic_ngtcp2/rebind-port`
under the caller-owned execution directory. No production, recovery, heartbeat,
fixture or runner code was changed. This is distinct from the earlier 123042-byte
bulk-stream hole and its ACK producer-servicing correction.

## Exact sequence

- The client's first Initial appears at capture-relative approximately 0.901s.
  The simulator changes its mapping at fixed simulation time 1s (packet 13),
  from client port 36720 to 57607.
- The client's final handshake flight (Finished plus its 1-RTT request) leaves
  at epoch 1794422192.333191 and reaches the server at .349374. The server logs
  handshake completion at 1794422192349826.
- The server emits HANDSHAKE_DONE once, in 1-RTT packet 1, qlog timestamp
  1794422192350130. Pcap proves actual socket egress at epoch
  1794422192.350343166, server-capture relative .989043508s, addressed to 36720.
  This and the ensuing Handshake Finished ACK / initial STREAM flight cross
  the NAT mapping transition while in flight.
- The client's last incoming packet is the earlier 1-RTT ticket packet 0 at
  epoch 1794422192.330947758. Its qlog has seven received QUIC packets total,
  no STREAM bytes and no HANDSHAKE_DONE. Its sole 1-RTT ACK acknowledges packet
  0 only; HANDSHAKE_DONE packet 1 is never acknowledged.
- From client-relative 185ms through 27262ms, the client repeatedly transmits
  Handshake CRYPTO [0,52), its Finished. These reach the server from successive
  translated source ports. There are no new-tuple 1-RTT packets.
- All server responses still target original port 36720. The server emits 48
  QUIC packets and receives 23; its greatest emitted STREAM endpoint is 13384,
  then PTO repeatedly probes the final two STREAM slices, [12606,12960) and
  [12960,13384). No passive migration starts or promotes, and no second
  HANDSHAKE_DONE is emitted. The server eventually closes around 30s; the
  client never confirms the handshake and downloads zero bytes.

Decoded wire extracts are `qir-early-server-wire.tsv` and
`qir-early-client-wire.tsv`; qlog counts and the only 1-RTT ACK are in
`qir-early-handshake-summary.json`, all outside the worktree. Pcaps were decoded
with the client's keys through tshark stdin. This proves packets left the
physical endpoint toward the original tuple; it is not evidence of a local
ZiTx admission drop, pointer-owner loss or generation change.

## Source mechanism

`SrvLink::consumeFrames_` invokes `observePathRx_` only for AppData after
establishment (`ZquicSrvLink.hh:1153-1154`). Authenticated Handshake Finished
repetitions therefore do not start passive path validation. `receivedLong`
sets `m_peerAddr` only before the handshake initially starts
(`ZquicSrvLink.hh:1099-1103`). Normal `flushTx_(addr)` uses the active
`m_peerAddr` rather than the new Handshake source (`:599-601`). The observed
repeated Finished packets can be processed but their responses stay on the
original mapping.

`emitTLS_` only requests HANDSHAKE_DONE while `m_handshakeDoneSent` is false;
`sendHandshakeDone` sets that flag after the original send. Its existing
recovery record must therefore drive retransmission. `PktTxSpace::reclaimOnPTO`
walks sent packets newest-first and selects two refs, and the normal PTO path
immediately retransmits those refs. Here those are always the newest STREAM
slices, so the unacknowledged older HANDSHAKE_DONE is never selected. The prior
ACK-credit correction cannot run after establishment because there are no new
AppData ACKs after packet 0. This is an existing pre-confirmation/control-probe
liveness edge, not the earlier bulk-credit starvation sequence.

## Interpretation and limits

RFC 9001 defines server confirmation at TLS completion and client confirmation
on receipt of HANDSHAKE_DONE. RFC 9000 assumes stable addresses through the
handshake and disallows initiated migration before confirmation; its security
section permits dropping packets on another handshake path. These rules do
not promise arbitrary pre-confirmation NAT rebinding will survive. Thus this
run is not a clean established-connection migration gate: the fixed 1s NAT
clock overtakes a late-starting handshake. NAT is involuntary, so this does not
mean ngtcp2 violated the active-migration restriction. See
[RFC 9001 §4.1.2](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.1.2) and
[RFC 9000 §9 / §21.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-9).

Separately, RFC 9000 §13.3 requires HANDSHAKE_DONE retransmission until ACK.
The observed absent replay warrants a focused stable-address loss test; the
startup race should not be used to declare that control-recovery behavior
conformant. Replaying it only toward the dead tuple would not by itself restore
this specific connection. See
[RFC 9000 §13.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.3).

The source/wire evidence supports a pre-client-confirmation NAT race plus an
existing control-probe limitation. It does not establish a new physical-owner
or completed-datagram-queue regression. Preserve this failed artifact and verify
post-confirmation migration separately; do not silently relabel this run as a
successful rebinding case or infer universal handshake-NAT support from another
green run with an earlier startup.
