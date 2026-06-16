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
