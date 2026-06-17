## Replace PTO-only timing with explicit QUIC timers
Implemented explicit QUIC runtime timer inventory while preserving the existing PTO timer instance.  `Link` now owns by-value `ZmScheduler::Timer` members for ACK delay, loss time, PTO, idle timeout, close/drain, key discard, PMTUD, and path validation; scheduler references remain raw pointers to those members.  Added typed schedule/cancel helpers and no-op semantic expiry handlers for timers whose full behavior is planned later.  Timer callbacks run on Tx and capture only the raw owning link pointer, avoiding `ZmRef` callback cycles.

PTO scheduling was generalized from 1-RTT-only behavior to choose the earliest eligible packet number space.  `PktTxSpace` now exposes latest ACK-eliciting send time for PTO deadline calculation.  Timer cleanup was expanded across runtime reset, close, server link release, server link table clearing, and link destruction.  Server shutdown now marks app-held links shut down, cancels all timers, releases TLS state, and retires routes so outstanding scheduler nodes cannot reference freed link storage.

Added `zquic/test/ZquicTimerTest.cc` and wired it into the test build.  Coverage arms all eight timer instances independently, verifies cancellation suppresses callbacks, verifies ACK delay can fire before PTO without PTO firing early, and verifies owner release with armed timers is safe under ASan.

Fixed runtime regressions found during validation.  Endpoint receive shutdown now completes the I/O context instead of rearming receive after endpoint close, preventing reuse of a freed receive buffer.  zquic and ztls certificate cleanup now frees each `ptls_iovec_t::base` allocated by `ptls_load_certificates()` before freeing the certificate list.  `ZtlsPico` uses direct aligned allocation for picotls internal buffers under AddressSanitizer so LeakSanitizer observes real frees instead of Zi heap-cache retention; non-sanitized builds keep the Zi VHeap path.

Validated the focused runtime path with `ZquicRuntimeTest`, `ZquicTimerTest`, `ZquicRecoveryTest`, and whitespace checks before completion.

## Extend sent-packet tracking to all packet number spaces
ACK, loss, PTO, and retransmit processing now operate across Initial, Handshake, and 1-RTT sent-packet spaces.  Runtime ACK handling no longer returns early for non-1RTT spaces: ACK ranges are applied to the matching `PktTxSpace`, packet-threshold losses enqueue retransmittable frames, PTO backoff resets on newly acknowledged packets in any active space, and RTT sampling remains restricted to 1-RTT ACKs where ACK delay is valid.

CRYPTO retransmission keeps sent-packet records metadata-only.  `SentFrameRef::crypto()` continues to carry offset/length, while `CryptoStream` now retains the transmitted CRYPTO stream bytes once in a Tx backing store and exposes bounded offset/length lookup for rebuilding lost CRYPTO frames.  The send path records CRYPTO metadata for Initial, Handshake, and 1-RTT packets, schedules PTO for any ACK-eliciting packet space, and rebuilds CRYPTO retransmits into the correct packet type and packet protection context.

Client and server PTO/retransmit paths no longer require runtime establishment before recovering handshake CRYPTO.  PTO selection still chooses the earliest active packet number space, but retransmission now returns both the selected crypto level and the frame metadata so Initial and Handshake CRYPTO are sent as long-header packets; STREAM and control retransmits remain 1-RTT-only after establishment.

Initial and Handshake discard is split by owner thread.  Establishment marks obsolete Rx packet spaces ignored, clears pending Rx ACK state and receive CRYPTO buffers, then posts a Tx cleanup to clear sent packets, queued retransmit refs, Tx CRYPTO backing data, and Tx ACK snapshots for those spaces.  Later obsolete Initial/Handshake packets are ignored before decrypt rather than counted as runtime failures after key discard.

Added direct recovery coverage for ACK/loss/retransmit behavior in Initial, Handshake, and AppData `PktTxSpace` instances.  Added CRYPTO stream retention checks and runtime handshake-loss coverage that drops server Initial CRYPTO and multiple server Handshake CRYPTO packets, verifying PTO-driven retransmission completes the handshake.  Validated with ASan/LSan `make -C zquic/test test`, dependent `make -C zhttp/test test`, and `git diff --check`.

## Wire time-threshold loss and persistent congestion into runtime

Added runtime loss-time integration for ACK-threshold and RTT time-threshold recovery.

`Link` now computes the next loss deadline from ACK-eliciting, in-flight packets across all non-discarded packet-number spaces via `PktTxSpace::nextLossTime()`, and schedules a dedicated loss timer from sends and ACK handling. The loss timer callback now runs time-based loss checks via `detectLoss_()`, retransmits lost work, schedules PTO, and reschedules to the next pending deadline.

ACK processing continues to use existing packet-threshold recovery (`pktThreshold`), now followed by runtime persistent congestion checks and explicit `persistentCongestion_()` hook points, and always re-arms the loss timer.

`RttEstimator::timeThreshold()` and `PktTxSpace::nextLossTime()` are now validated by `ZquicRecoveryTest` with explicit unit coverage of time-threshold loss and deadline advancement. `ZquicRuntimeTest` now verifies a dropped 1-RTT STREAM packet is recovered by the loss timer before PTO fires by asserting retransmission occurs without increasing the server PTO count.

Runtime congestion state is now fed from ACK and loss processing instead of remaining diagnostic-only. ACKed packet bytes advance NewReno, lost bytes collapse the congestion state at the lost send time, persistent congestion increments runtime diagnostics, and the public runtime diagnostic snapshot exposes cwnd, ssthresh, bytes in flight, and persistent congestion count.

Timer teardown now follows the asynchronous timer guidance. `Link` destruction asserts all scheduler timer nodes are already inactive; it no longer cancels timers itself. Owners that are about to release links call the continuation-based timer teardown path, which marks the link as tearing down, cancels timers, drains callbacks by posting the continuation on Tx, and only then lets dependent close/release work proceed. QUIC client disconnect also has a continuation overload, and the zhttp QUIC drivers use it before dropping their final link references.

## Integrate congestion control into packet budgets

Completed runtime NewReno budget integration on top of the congestion accounting added in the previous plan.

`Link` now has a Tx-owned congestion budget facade. `congestionAllowance_()` derives cwnd remaining from `NewReno::cwnd()` and `NewReno::bytesInFlight()`, and `sendBudget_()` combines that allowance with the current max UDP payload. Until path integration lands, PMTU and anti-amplification budget fields still use `app()->maxUDP()` as the active path stand-in, but normal control and stream sends now receive only `min(maxUDP, cwnd - bytesInFlight)` as their congestion allowance.

The runtime packet builders now refuse normal ACK-eliciting control and stream sends when congestion allowance is exhausted. Queued control frames are retained rather than shifted, and queued stream data remains buffered. ACK-only behavior remains outside this gate because ACK-only sends use their existing non-ACK-eliciting paths and sent-packet records only call `NewReno::sent()` for ACK-eliciting packets.

ACK processing now treats newly acknowledged in-flight bytes as a send opportunity: after feeding acknowledged bytes into NewReno and refreshing diagnostics/timers, the ACK path posts a Tx flush through the concrete client/server link. This is the pacing hook point for later work; it only retries queued work after cwnd opens and does not add pacing delays or a new controller abstraction.

Added focused runtime packetization coverage in `ZquicStreamTest`. The test initializes a direct `Link` fixture with real runtime congestion state, fills cwnd through synthetic sent-packet records, verifies stream and control packetization are blocked while congestion allowance is zero, verifies the control frame is retained, then ACKs the in-flight packet range and verifies the ACK opens cwnd and queues a Tx flush. Existing runtime loss recovery coverage in `ZquicRuntimeTest` continues to validate dropped 1-RTT STREAM retransmission without PTO, while `ZquicCongestionTest` continues to cover NewReno growth, loss, persistent congestion collapse, and PMTUD-probe loss behavior.

Validation was performed with the configured AddressSanitizer/LeakSanitizer build. `make -C zquic/test test` passed after one transient `ZquicRuntimeTest` LeakSanitizer report that did not reproduce when the runtime test was run directly or when the full zquic test target was rerun. `make -C zhttp/test test` passed after one transient `ZhttpMatrixTest` interop failure; the failing matrix case passed when rerun, and the full zhttp target then passed.

## Make ACK management timer-driven

Runtime receive ACK state now uses `AckManager` instead of the older `m_rxPkts`/`m_pendingAck` arrays.  Receive processing records every non-duplicate protected packet number in the manager, then marks the packet ACK-eliciting after frame parsing if any parsed frame requires an ACK.  The manager now carries per-space pending state, ACK-eliciting state, immediate ACK state, generation numbers, deadlines, and the timestamp of the largest received packet so receive-side duplicate detection, range tracking, and delayed ACK state share one source of truth.

ACK snapshots posted from Rx to Tx now include the receive generation and largest-received timestamp.  Tx packet builders can piggyback any pending ACK snapshot, but ACK-only emission is gated by a separate `due` bit.  Initial and Handshake ACK-eliciting packets are marked immediate and therefore sent with zero ACK delay; 1-RTT packets arm the ACK delay timer unless the simple immediate policy detects reordering/gaps.  The ACK delay timer now marks the 1-RTT snapshot due and flushes Tx, while ordinary receive-driven Tx flushes no longer turn delayed 1-RTT ACKs into immediate ACK-only packets.

ACK frame emission is now transactional with packet send.  `PktBuild` records whether an ACK frame was appended, and protected packet send commits ACK state only after packet protection and `sendPkt` succeed.  Tx clears the sent snapshot and posts the matching receive generation back to Rx; Rx clears pending state only if no newer receive generation has superseded it.  ACK-only packets continue to pass through the protected send path as non-ACK-eliciting packets, so they advance packet numbers and diagnostics but do not create retransmittable/in-flight sent-packet entries or schedule PTO.

`AckManager` tests now cover immediate Initial/Handshake ACK state, delayed 1-RTT deadlines, largest-received timestamps, generation-aware commit, and duplicate receive handling while preserving ACK range behavior across duplicates and reordering.  Validation was performed with the configured AddressSanitizer/LeakSanitizer build: `make -C zquic/test test` passed, and `make -C zhttp/test test` passed after several non-reproducing zhttp fixture/interop transients (`ZhttpMatrixTest`, `ZhttpDarkhttpdCompatTest`, `Zhttp3InteropTest`, and `ZhttpMultiRequestTest`) each passed when rerun directly or in a subsequent full target run.

## Preserve and validate ACK_ECN counts

ACK_ECN is now preserved end-to-end through the frame codec and runtime ACK path.  `Frame` carries inline `AckECN` counters for ECT0, ECT1, and CE; `Frame::reset()` clears them; ACK_ECN parsing retains all three counters instead of skipping them; and `FrameCodec::writeAckECN()` emits type `0x03` only when counters are non-zero, leaving ordinary ACK output on type `0x02`.  `PktBuild` scratch sizing now accounts for the extra ACK_ECN varints at the existing 64-range encoder cap.

Receive ACK state now tracks ECN marks per packet number space.  `Datagram` carries an optional `EcnMark` with a `NotECT` default, unsupported endpoint receive paths keep that default, and protected-packet receive records the mark into `AckManager` only for novel packet numbers so duplicates do not inflate counters.  Posted ACK snapshots carry the per-space ECN totals to Tx, and ACK frame emission includes ACK_ECN only when ECN is enabled on the active `Path` and counters have been observed.  Runtime diagnostics expose both received ECN mark totals and peer ACK_ECN feedback counters for congestion/control inspection.

Minimal peer ACK_ECN validation now runs during Tx ACK processing.  Feedback counters must be monotonic and their total must not exceed the acknowledged packet number range from zero through the largest acknowledged packet.  Validation failure disables ECN on the active path via `Path::setEcnDisabled()` and increments runtime diagnostics, but ACK range processing still continues so recovery, RTT sampling, congestion accounting, timers, and Tx flush scheduling are not lost because of bad ECN feedback.

Tests now cover ACK_ECN codec round trips with ECT0, ECT1, and CE counts, `Frame::reset()` clearing counters, `AckManager` ECN accounting across duplicate receives, runtime ACK_ECN emission from received marks, diagnostic surfacing of local and peer ECN counters, validation failure on counter regression, validation failure on impossible totals, and continued ACK processing after ECN validation failure.  Validation was performed with the configured AddressSanitizer/LeakSanitizer build: `git diff --check`, `make -C zquic/test test`, and `make -C zhttp/test test` all passed.

## Improve packet assembly/coalescing

Runtime packet assembly now fills a packet with multiple retransmittable frames instead of emitting one control or stream frame per UDP datagram.  `PktAssembly` counts stream frames, `StreamPktizer` permits multiple STREAM frames in one packet, `PlainVec` has enough iovecs for a full multi-frame runtime packet, and the runtime Tx path records all sent frame references in a single `SentPkt`.  Control and stream flushing share one packet budget so ACKs, control frames, and stream frames are coalesced until the congestion/UDP budget or sent-frame metadata cap is reached.

Long-header send paths can now coalesce Initial and Handshake packets into one datagram when the address and long-header state match.  Runtime packet numbers now use the smallest safe packet-number encoding length derived from the largest peer-ACKed packet in that packet-number space, matching the reference behavior in ngtcp2/zngtcp2 rather than keeping the fixed runtime packet-number length forever.

Recovery now suppresses duplicate retransmission of frames already acknowledged or still outstanding in a newer packet.  `PktTxSpace` tracks acknowledged STREAM/CRYPTO frame keys, filters queued retransmits against that set, avoids queueing a lost frame when an equivalent frame is still in flight, and treats PTO reclaim as one-shot per sent packet like zngtcp2's `PTO_RECLAIMED` state.  ACK packet-threshold loss now uses the largest packet newly acknowledged by this ACK frame, not the peer's largest ACK range value when that packet was already processed earlier.

The Caddy H3 stall was investigated with `zhttp --debug` against a preserved `-j10 -n1000` fixture after restoring the matrix client timeout to 20s.  Debug reproduced the stall, so pcap was not needed.  The failing run completed 999 responses and left one earlier request active while the log tail repeated immediate `loss time` timer arms without PTO or retransmit progress.  The loss timer now applies QUIC timer granularity when the computed loss deadline is already due, preventing the scheduler from repeatedly rearming the same effective instant and allowing recovery/PTO to make progress.

`ZhttpMatrixTest` still uses `timeout 20s`; its Caddy cleanup now terminates politely, waits briefly, and then kills the fixture process so Caddy's own shutdown grace period does not mask a client timeout.  Added regression coverage for multi-frame stream assembly, dynamic packet-number length, long-header coalescing, duplicate retransmit suppression, PTO one-shot reclaim, and ACK-only largest-range handling.

Validation was performed with the configured AddressSanitizer/LeakSanitizer build: `git diff --check`, focused `ZquicRecoveryTest`/`ZquicStreamTest`/`ZquicFlowTest`, `zhttp --debug -j10 -n1000 --http3=force` against Caddy in 458 ms with all 1000 bodies, 20 normal `ZhttpMatrixTest --case=ZhttpCaddy/H3/j10n1000` runs in 450-522 ms each, `make -C zquic/test test`, and `make -C zhttp/test test`.

## Integrate `Path` into runtime send/receive budgets

`Link` now owns and uses one active runtime `Path` for both client and server connections.  Runtime reset initializes a default active path using engine `maxUDP`; client endpoint readiness replaces it with the connected local/remote socket addresses and marks it validated, while server first-routed Initial installs an unvalidated path from the server local address and peer address.  Establishment validates the active path after transport parameter validation, and decoded peer `max_udp_payload_size` is posted to the Tx-owned path.

Receive byte accounting is now datagram-scoped.  `receiveDatagram_()` records `Path::received()` once per UDP datagram by posting the fixed byte count to Tx before parsing coalesced packets, so coalesced Initial/Handshake packets do not multiply receive credit.

Runtime send budgeting now derives PMTU and anti-amplification from the active path instead of using `app()->maxUDP()` directly.  `sendBudget_()` uses `Path::activeMaxUDP()` for packet assembly, `Path::sendAllowance()` for anti-amplification, and the existing NewReno allowance for congestion, preserving the minimum-of-path/congestion behavior introduced in plan 4.  Protected datagram sends are gated by `Path::canSend()` before the UDP send is queued and call `Path::reserveSend()` only after an actual endpoint send is accepted.

Server sends now respect QUIC anti-amplification until validation without breaking the existing packet-loss test hooks.  The server path wrapper distinguishes application-level simulated packet drops from actual endpoint sends: suppressed test datagrams still produce sent-packet metadata for recovery, but only datagrams accepted by the endpoint are charged to `Path::bytesTx`.  This keeps runtime recovery tests representative while making path byte accounting match real sends.

Added test hooks and public diagnostics for active path state on client and server links.  `ZquicStreamTest` now covers active path MTU controlling runtime packet budget, unvalidated server budget exhaustion at 3x received bytes, received-byte allowance growth, path byte diagnostics, and validation unlock to unlimited send allowance.

Validation was performed with the configured AddressSanitizer/LeakSanitizer build: `git diff --check`, focused `ZquicRuntimeTest` and `ZquicStreamTest`, `make -C zquic/test test`, and dependent `make -C zhttp/test test`.
