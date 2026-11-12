# QIR rebind stream-gap diagnosis

Read-only analysis of the final-image `zquic-server_ngtcp2-client` artifacts
under `/home/ubuntu/zquic-zirx-execution/qir-recovery-rebind/`, run directory
`logs_2026-11-11T18:01:06/zquic_ngtcp2`. No recovery, heartbeat, protocol or
production code was changed for this diagnosis. The matched old-pump image
comparison is a separate gate; these logs alone do not establish regression
attribution.

## Wire/application evidence

Merged every received STREAM frame for stream 0 in ngtcp2's qlog, independently
of its contiguous application delivery. The requested body is 10485760 bytes.

| Case | Unique bytes received | Missing bytes | Missing ranges | First missing range |
| --- | ---: | ---: | ---: | --- |
| rebind-port | 10340469 | 145291 | 50 | [123042,125298) |
| rebind-addr | 10360130 | 125630 | 47 | [121942,123070) |

The port case's first missing interval explains the runner's exact 123042-byte
contiguous download, despite receiving 10.9 MB of UDP bytes. Server packet 118
carries [123042,124170), and 119 carries [124170,125298). Neither packet appears
in ngtcp2's received-packet or transmitted ACK union. Both ranges are marked
for retransmission at server timestamp 1794420082935217. The first half is
re-emitted only in packet 13102 at 1794420093676428: **10.741211 seconds after
marking**, and **0.800399 seconds after the final new-data packet** (13004,
[10485272,10485760), timestamp 1794420092876029). Packet 13102 is neither received
nor ACKed by the client. The second half has no re-emission before termination.

The address case's first interval appears in packet 117 and is marked at
1794420129454112; it is never re-emitted before termination. Final new-data
packet 13060 covers [10485080,10485760) at 1794420139398914.

Both cases therefore retain holes in received stream coverage; this is not a
runner file-comparison error. The port case's eventual retransmission also
proves the original plaintext range was retained, rather than retired merely
by local UDP completion. Decrypted server-side pcap additionally shows actual
UDP socket output for original packets 118/119 (destination port 36571) and
retransmission 13102 (destination 59022, capture-relative 11.651314241 seconds).
The matching client-side capture contains packet 13041 but none of 118,119 or
13102. Thus the first-hole originals and eventual retransmission actually left
the server socket; a local ZiTx admission/completion drop does not explain
these particular missing packets. Wire extracts are `qir-gap-wire-check.tsv`
and `qir-gap-wire-client-check.tsv` in the execution directory. Server `packets_acked` does not report the port gap's
118/119/13102 either. Aggregate server `packet_lost` qlog events use packet
number zero; the diagnosis uses their exact marked STREAM ranges instead of
pretending that aggregate field identifies original packet numbers.

Reproducible analysis script and complete gap/overlapping-send lists:
`qir-gap-audit.py`, `qir-gap-rebind-port.json`, `qir-gap-rebind-addr.json` in the
caller-owned execution directory. The script treats omitted offset fields as
zero and unions overlaps, so retransmitted/duplicate bytes are not counted as
new application bytes.

## Existing protocol servicing order

The source directly permits starvation of these pending ranges:

1. `ZquicLink.hh:7146-7148` synchronously invokes normal `flushTx_()` when an
   ACK opens congestion credit, then calls deferred `queueRetransmit()` only
   when that ACK's work newly detected loss. Existing queued loss is not by
   itself a reason to schedule another attempt there.
2. `ZquicSrvLink.hh:142-148` posts the retransmission attempt to app Tx.
   `flushTx_` at lines 602-612 first emits ordinary control/new STREAM work
   through `flushCtlStreamsBatch_`, consuming the reopened credit immediately.
3. When the posted retransmitter executes with no remaining credit,
   `ZquicSrvLink.hh:181-183` requeues the selected reference and breaks.
   Lines 224-225 repost only after a successful full 16-reference batch;
   an unsent congestion-blocked attempt is not self-reposted.
4. Continuous newly acknowledged data resets PTO (`ZquicLink.hh:7138`).
   Once new data ends, PTO eventually takes the existing bypass-congestion
   recovery path, explaining the late old-range emissions.

The first-gap loss period's qlog metrics give cwnd 69941 and bytes in flight
131720 (timestamp 1794420082935237), i.e. no credit immediately after the loss
response. Other retransmissions do occur earlier, but the first hole remains
unsent through the bulk transfer. This matches reference requeue/rotation and
new-data-first credit consumption; the trace does not instrument every failed
retransmit call, so the exact individual invocation sequence remains an
inference from source plus packet history.

This ACK/flush/retransmit ordering is unchanged in the migration diff. QIR
runs protocol Rx/Tx on threads 3/4 and socket mx Rx/Tx on 1/2
(`zhttpqirimpl.cc:192,1079`). Thus this is retained-plaintext recovery policy
on protocol Tx, above the completed-datagram queue on socket Tx. A changed
socket servicing schedule can alter ACK timing and expose the policy, but the
above does not show that a delayed callback in the new ZiTx queue removed or
forgot the missing plaintext. The qlog's maximum observed cwnd is 138711 bytes;
there is no endpoint-capacity-drop diagnostic in these artifacts, so no claim
of an observed 4096-packet rejection is warranted.

## Why later retransmissions stop reaching ngtcp2

The simulator output records NAT port rebinding at 1, 6, 11, 16, ... seconds.
The server validates/promotes the first two passive tuples (36571->57607 and
57607->59022). The third simulator rebind changes 59022->18108 at 11 seconds.
This coincides with the client's last received packet, 13041, at about 10.307
seconds connection-relative. The late missing-range retransmissions occur
after that mapping change. There is no third observed server promotion or
new client input advertising the next tuple; the client has no outstanding
ack-eliciting request data and eventually reports idle close. Both observed
promotions have `local_rebind=false`: they do not replace the physical UDP
EndpointCxn, so stale physical-connection admission is not involved in these
passive NAT changes.

## Minimal correction locations, if separately authorized

A protocol-level correction should ensure a bounded recovery attempt receives
ACK-reopened credit before ordinary new STREAM output and should resume
already-pending lost ranges, not only newly detected loss. This could be owned
by `processAckFrameTx_` sequencing or an explicit retransmit-first protocol
flush policy. Merely swapping the textual order of `queueRetransmit()` and
`flushTx_()` is insufficient: queueRetransmit is deferred while flush is
synchronous. Preserve the existing bounded batch, pacing and congestion
contract; no completed-packet wrapper queue or generic Zi scheduling API is
required to fix that ordering. Heartbeat/path-discovery behavior is a separate
policy and was not changed here.

## Matched baseline and authorized servicing correction

The immediate pre-task old-pump comparison now passes transferloss, rebind-port
and rebind-addr in both endpoint roles (six matched cases). The server-role
result is recorded in
`qir-pre-task-diagnostic/zquic-server_ngtcp2-client/results.json` beneath the
execution directory. The failing migrated-server cases therefore require a
servicing correction, even though the starvation-prone recovery ordering itself
predates this migration.

The authorized correction changes only the final ACK producer-service sequence:
when actual congestion allowance is available, query the three existing packet
spaces for pending retransmit references and invoke the existing bounded direct
`impl()->retransmit_()` before normal producer flush. This includes loss queued
by earlier ACKs. Preserve the existing deferred new-loss notification when direct
service did not send, and avoid posting a redundant notification after successful
direct service. There is no new helper/callback signature, queue state, packet
scheduler, Zi API, retained-record mutation, loss-detection change, congestion
math change, crypto change or heartbeat change.

Focused regression coverage in the existing `ZquicStreamTest` runs both
already-pending-loss and newly-detected-loss ACK cases on the real deferred app
Tx scheduler. It uses actual retained STREAM backing, actual sent-packet loss
and ACK processing and actual congestion credit. The producer hook can consume
all ACK-opened credit; recovery notification truly posts to the owner and then
requeues when credit is exhausted. This rejects merely swapping the textual
order of deferred notification and synchronous bulk flush. It also checks
original backing identity/bytes, retained unacknowledged ownership before the
retransmit ACK and release afterward.

Before the production correction, only the two new cases fail in the 54-case
StreamTest run (`recovery-service-before.log`); setup assertions pass. The
newly-lost case demonstrates the deferred recovery task being blocked after
ordinary bulk consumes the credit. The final corrected source was handed to
the coordinator for one library/focused rebuild batch; after-fix and full QIR
results are recorded by that coordinator, not inferred from this source review.
