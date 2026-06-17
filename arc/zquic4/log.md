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

## Implement path validation and migration basics

Runtime path validation is now represented explicitly in `Link` with one Tx-owned validating path slot alongside the active path.  The validating slot retains the previous active path, candidate local/remote socket addresses, selected peer CID sequence, generated challenge payload, and validation deadline.  New remote addresses observed after 1-RTT packet protection succeeds are posted from Rx to Tx as fixed address snapshots; Tx starts validation instead of replacing the active send path.

Added a dedicated `PathChallenge` protocol value for the RFC 8-byte PATH_CHALLENGE/PATH_RESPONSE payload, with random generation implemented beside the existing reset-token generator.  PATH_CHALLENGE is now a normal control frame in `ControlFrame`, packet assembly, sent-frame metadata, and retransmit metadata, mirroring PATH_RESPONSE payload preservation.  PATH_RESPONSE receive handling posts the 8-byte payload to Tx, where unmatched responses are ignored and matching responses promote the candidate path.

Promotion replaces the active `Path`, marks it validated, binds the selected peer-issued CID to the promoted path, cancels the validation timer, and lets concrete client/server links update send routing.  Server links update `m_peerAddr` only from the promoted active path, so short packets from a new address no longer silently move application traffic before validation.  Timeout/failure clears only the validating slot, cancels the path-validation timer, queues Tx flush, and leaves the old active path intact.

CID handling remains conservative for this first migration step.  Runtime setup marks the initial peer CID associated with the active path.  Starting validation chooses the first active unassociated peer-issued CID when available, otherwise it continues with the current peer CID; promotion clears stale peer-CID associations and marks the promoted CID associated.  Local CID route retirement behavior remains unchanged.

`ZquicStreamTest` now covers the path-validation state machine directly: 8-byte challenge generation, candidate creation from a new remote address, active-path retention before validation, unmatched PATH_RESPONSE ignore, duplicate candidate observation suppression, matching PATH_RESPONSE promotion, and timeout retaining the old active path.  The direct unit fixture uses a test-only no-scheduler entry point so it exercises state transitions without arming raw-pointer scheduler timers on stack-owned links; runtime timer firing remains covered by the existing path-validation timer inventory test.

Validation was performed with the configured AddressSanitizer/LeakSanitizer build: `git diff --check`, focused `ZquicStreamTest`, `make -C zquic/test test`, and dependent `make -C zhttp/test test`.  zhttp interop showed non-reproducing transient failures in `ZhttpMatrixTest` and `Zhttp3InteropTest`; direct/rerun evidence isolated one direct nonzero exit to missing out-of-Makefile LSan suppressions, and the final `make -C zhttp/test test` passed with the Makefile sanitizer environment.

## Wire PMTUD into runtime

Runtime PMTUD is now connected to protected packet sending and recovery.  Sent packet metadata carries a PMTUD probe size, ACK/loss processing reports probe outcomes back to the active `Path`, and NewReno accounting releases PMTUD probe bytes without using probe loss to reduce cwnd.  Packet threshold loss, time threshold loss, and ACK handling share the same `PktTxUpdate` path so probe ACK/loss handling remains aligned with normal sent-packet retirement.

Tx now schedules PMTUD probes from the active path and emits padded ack-eliciting probe packets when path, anti-amplification, and congestion budgets allow.  Probe packets are marked at send time, padded to the target UDP payload size, and use the active path PMTUD state to grow or retry the usable payload size.  Runtime PMTUD timer expiry calls the path probe-expiry state machine and requeues work when another probe is due.

Endpoint UDP setup now enables the socket diagnostics needed for path hints where supported, and runtime exposes endpoint path hints/diagnostics for tests and future production handling.  Send-too-big and kernel MTU hint plumbing feeds the active path PMTUD state without inventing a second PMTU algorithm.

Coverage was added to `ZquicLoopTest` for successful probe growth, failed probe retry, blackhole fallback, send-too-big/kernel hint handling, and the rule that PMTUD probe loss must not collapse congestion state.  Focused validation covered `ZquicLoopTest`, `ZquicRecoveryTest`, and `ZquicPMTUDTest` before continuing to the later plans.

## Emit RESET_STREAM and STOP_SENDING frames

Local stream reset and stop-sending now use the normal scheduler, sent-frame, ACK, and loss machinery.  `ControlFrame` and `SentFrameRef` carry RESET_STREAM and STOP_SENDING fields, and sent-frame keys include enough control metadata to distinguish stream control obligations from other frames during ACK/loss filtering.

Application-initiated `Stream::reset()` and `Stream::stop()` update stream state, queue the corresponding control frame, and suppress stale stream/FIN output once reset state makes normal stream data irrelevant.  Loss processing rebuilds reset/stop frames while they remain valid, and ACK processing clears the retransmission obligation through the existing sent-reference retirement path.

The implementation keeps the frame writers in `FrameCodec` as the single encoding path and extends control retransmit building rather than adding a parallel stream-control sender.  This keeps local reset/stop behavior consistent with PATH_RESPONSE, MAX_DATA, and other retransmittable control frames.

`ZquicStreamTest` now covers local RESET_STREAM send, local STOP_SENDING send, ACK clearing, loss-triggered requeue/retransmit, and preservation of peer RESET_STREAM final-size validation.

## Add closed-stream and suspicious-remote rate limiting

Receive-side stream validation now distinguishes ordinary duplicate stream data from suspicious closed-stream or invalid stream activity.  Duplicate STREAM frames still go through the existing receive-queue fast path and remain non-fatal, while closed-stream MAX_STREAM_DATA, STREAM_DATA_BLOCKED, duplicate RESET_STREAM, and invalid final-size cases are classified explicitly.

Runtime diagnostics gained receive counters for invalid stream frames, closed-stream frames, and suspicious stream closes.  A small named threshold bounds repeated invalid remote activity so the connection emits one deterministic close/error path instead of repeatedly doing expensive work or queuing repeated closes for the same class of peer behavior.

Closed local send-stream MAX_STREAM_DATA and STREAM_DATA_BLOCKED frames are ignored after counting.  Duplicate RESET_STREAM with the same final size is ignored after counting, while a duplicate reset that changes final size remains a transport `FinalSize` error.  Flow-control and final-size protocol errors still close immediately; the new classification does not hide real protocol violations.

`ZquicStreamTest` now exercises closed-stream MAX_STREAM_DATA, closed-stream STREAM_DATA_BLOCKED, duplicate RESET_STREAM with matching and mismatched final size, ordinary duplicate STREAM data, and repeated suspicious activity tripping the deterministic close threshold.

## Improve application callback/error surface

The CRTP application surface now exposes explicit hooks for stream and connection events that previously disappeared into internal state transitions.  Base `Link` defaults were added for stream open, stream data, reset received/sent, stop-sending received/sent, flow-blocked notifications, transport close, stateless reset, path update, and migration failure.

Default handlers are intentionally harmless but no longer silent: runtime diagnostics now include unhandled application-event counters so applications can discover when an event path is using the base default.  Concrete applications can override only the hooks they care about; no virtual dispatch or compatibility wrapper layer was added.

Callbacks are wired after internal state has been updated and with stable metadata.  Stream callbacks receive stream references or stable stream IDs plus error/final-size data, flow-blocked callbacks identify the blocked frame class and limit, transport close callbacks report frame type and transport error, stateless reset detection calls the hook on detection, and path callbacks report promoted or failed local/remote addresses after validation state changes.

`ZquicAPITest` now verifies callback coverage for local and peer stream open/data, peer reset and stop-sending, local reset and stop-sending, flow blocked, transport close, stateless reset, path promotion, migration failure, and default unhandled-event diagnostics.  Existing stream, loop, recovery, and runtime tests were rebuilt and run directly while this surface was integrated.

## Harden HTTP/3 loss testing and scheduler fairness

The remaining scheduler hot spots were bounded or moved back through the appropriate shard scheduler.  Stream receive queue continuations now post through `rxRun`, ACK/crypto dequeue callbacks are installed by runtime links, queued local stream opens are capped per turn and reposted, control-frame enqueue dedup scans are bounded, and recovery sent-packet scans process work in batches before rescheduling.  Test-only scheduler deficiencies were fixed in the mock scheduler instead of compromising runtime `Zquic.hh` paths.

Debug-only QUIC packet-drop hooks now live on `ZiMultiplex` as mutable `rxFilter(FilterFn)` and `txFilter(FilterFn)` setters, with clearing done by passing `{}`.  `zhttp` and `zhttpd` expose `--quic-rx-drop=N%`, `--quic-tx-drop=N%`, and `--quic-diag=N`; client/server filters are installed after engine initialization using the stable engine mux from `this->mx()`, not by passing a mux through endpoint-specific code.

The Caddy 5% receive-drop stall was reproduced with pcap and one-second QUIC diagnostics.  The stalled connection had active H3 requests, no packet movement, no local bytes in flight, no PTO timer, and no retransmit backlog, so the multi-request H3 client now treats that quiet state as a reconnectable stall before the 15s hard timeout.  The same diagnostic path prints active request counters and stream state so future stalls are visible without attaching a debugger.

Validation was performed with the configured AddressSanitizer/LeakSanitizer build: `git diff --check`, focused `ZquicRecoveryTest`, `ZquicRuntimeTest`, and `ZquicStreamTest`, `zhttp`/`zhttpd` rebuilds, `ZhttpCaddy/H3/j10n1000` with 5% Rx drop, repeated Caddy 5% Rx+Tx drop, and `ZhttpZhttpd/H3/j10n1000` with 5% Rx-only and combined 5% Rx+Tx drops.  The Caddy combined-loss runs completed under 8s after reconnect handling.
