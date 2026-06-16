## Requirements
- Replace the current PTO-only runtime timer coverage with explicit QUIC timers.
- Use one `ZmScheduler::Timer` instance per independent expiry: ACK delay, loss time, PTO, idle timeout, draining/closing, key discard, PMTUD, and path validation.  The PTO instance already exists as `m_ptoTimer`; retain and extend it rather than adding a second PTO timer.
- Make each timer owner explicit: connection-owned timers live on `Cxn`; active-path timers live on `Link`.
- Preserve Rx/Tx ownership: Rx state may request work, but Tx owns send, recovery, and path-probe timer callbacks.

## Summary
Preconditions: none.

`zquic` already has a `ZmScheduler::Timer m_ptoTimer`, PTO computation, and PTO cancellation paths.  This work should not replace `ZmScheduler` or duplicate the PTO timer; it should keep `m_ptoTimer`, expand PTO selection beyond the current 1-RTT-only runtime behavior, and add the other seven timer instances with precise callback ownership.

The design is to add a small timer-management layer inside the connection/link runtime that owns typed timers directly.  Each callback validates that it is running on the correct shard, checks that the owning object is still live, and delegates to one narrow handler.

## Architecture Documentation
New or changed components: add typed timer members to `Link`/connection runtime state near the existing PTO timer.  Keep timers as concrete members, not heap nodes.

New or changed processes or threads: timers are scheduled through the existing app scheduler and fire on the Tx thread for connection and path send-side actions.  Rx-originated ACK state posts fixed snapshots to Tx before arming ACK delay.

New or changed interfaces: add internal `schedule*Timer_()` and `cancel*Timer_()` methods for each expiry.  Avoid a single reason-dispatch timer.

New or changed data flows: receive path updates ACK/path state, then posts timer decisions to Tx.  Recovery and lifecycle paths arm/cancel their own timers after state transitions.

New or changed event-driven or timer processing: each timer callback handles exactly one semantic expiry and must be cancellation-safe after owner teardown.

New or changed network programming: PMTUD and path-validation timers become per-link/path.  Packet sends still use existing `ZiIOBuf` and endpoint routing.

New or changed data stores: add deadline/scheduled state only where needed to avoid redundant scheduler operations.

## Timer Specification
No timer in this plan is `Engine`-owned.  `Engine` provides scheduler access and global configuration, but all expiry state is tied to either one connection or one active/candidate path.

### ACK Delay Timer
- Purpose: delay ACK emission for 1-RTT ack-eliciting packets so ACK-only packets are not emitted immediately when QUIC permits delay.
- Owner: `Cxn`.
- Name: `m_ackDelayTimer`.
- Started: when Rx observes a 1-RTT ack-eliciting packet that does not require an immediate ACK and ACK state is not already due.  Rx posts the fixed ACK metadata to Tx; Tx arms the timer.
- Deadline calculation: `largest_received_time + peer/local max_ack_delay`, clamped by packet-space rules.  Initial and Handshake do not use this timer; their ACK delay is zero and emission is immediate.
- Expiry behavior: enqueue or trigger ACK-only/control packet generation and commit ACK state only after a packet is actually sent.  It mutates connection ACK/send scheduling state, so it runs on the Tx thread.
- Cancelled: when pending ACK state is emitted and committed, when the ACK state is cleared by connection teardown, or when the connection enters draining/closed state.
- Rescheduled: when a new delayed-ACK deadline is earlier than the armed deadline, bring it forward.  Do not push it back merely because additional packets arrive; deferring ACKs beyond the original deadline violates the delay bound.  If an immediate ACK trigger appears, cancel the delay timer and schedule immediate send work.

### Loss Time Timer
- Purpose: fire time-threshold loss detection before PTO is reached.
- Owner: `Cxn`.
- Name: `m_lossTimer`.
- Started: after sending an ack-eliciting packet, after processing an ACK, or after packet-space state changes leave at least one outstanding packet with a future loss-time deadline.
- Deadline calculation: earliest outstanding packet loss deadline across active packet number spaces, using RTT-derived time threshold from recovery state and packet sent time.
- Expiry behavior: scan active sent-packet spaces, mark time-threshold losses, enqueue retransmittable frame refs, feed loss/congestion state, then schedule retransmission/PTO as needed.  It mutates sent-packet, retransmit, and congestion state, so it runs on the Tx thread.
- Cancelled: when no active packet space has a pending time-threshold loss deadline, when that packet space is discarded, or when the connection is closed/drained.
- Rescheduled: always to the earliest current loss deadline.  Bring forward if a new earlier loss time appears.  Push back only after ACK/loss processing removes the previous earliest candidate and recomputation proves the next deadline is later.

### PTO Timer
- Purpose: recover from no ACK progress by sending probe packets in the packet number space selected by QUIC PTO rules.
- Owner: `Cxn`.
- Name: `m_ptoTimer`.
- Current state: already partially implemented, including scheduling/cancellation and 1-RTT PTO retransmission.  The remaining work is to keep this timer and make its deadline/probe selection cover Initial, Handshake, and 1-RTT behavior.
- Started: when there is ack-eliciting in-flight data or handshake anti-deadlock probe obligation and no earlier loss-time action supersedes PTO.
- Deadline calculation: `last_ack_eliciting_sent_time + smoothed_rtt + max(4*rttvar, granularity) + max_ack_delay` for 1-RTT, without max_ack_delay for Initial/Handshake, multiplied by PTO backoff.  Select the earliest eligible packet-space PTO deadline.
- Expiry behavior: increment PTO/backoff state, reclaim or synthesize probe work for the selected packet space, enqueue retransmission/probe send work, and re-arm if probes remain outstanding.  It mutates recovery and send scheduling state, so it runs on the Tx thread.
- Cancelled: when all ack-eliciting in-flight data is ACKed/lost/discarded, when packet spaces are discarded, when connection teardown begins, or when a loss-time timer is the only pending recovery action.
- Rescheduled: recompute after every packet send, ACK, loss event, packet-space discard, and PTO expiry.  Bring forward if a newly active packet space has an earlier PTO.  Push back only after ACK progress, backoff reset, or removal of the previous earliest deadline.

### Idle Timeout Timer
- Purpose: close an idle connection after the negotiated max idle timeout.
- Owner: `Cxn`.
- Name: `m_idleTimer`.
- Started: when transport parameters establish a nonzero effective idle timeout and the connection has entered a state where idle timeout applies.
- Deadline calculation: last network activity time plus the effective idle timeout, where effective timeout is the minimum nonzero local/peer idle timeout required by QUIC behavior.
- Expiry behavior: transition the connection to closed or closing as appropriate, cancel send/recovery/path timers, notify diagnostics/callbacks, and release endpoint routing after close/drain rules permit.  It mutates connection lifecycle state, so it runs on the connection owner thread; use Tx if close sends CONNECTION_CLOSE, otherwise keep a single connection lifecycle owner.
- Cancelled: when the connection closes, enters terminal drained state, or idle timeout is disabled by negotiated parameters.
- Rescheduled: on any valid packet received or sent that counts as network activity.  Normally pushed back to `now + idle_timeout`; never brought forward except when transport parameter processing lowers the effective timeout before the timer has expired.

### Draining/Closing Timer
- Purpose: retain closing/draining state long enough to absorb late packets and retransmit/ignore close behavior according to QUIC lifecycle rules before teardown.
- Owner: `Cxn`.
- Name: `m_closeTimer`.
- Started: when entering closing or draining state after local close, peer close, stateless reset handling, idle timeout close, or fatal transport error.
- Deadline calculation: closing/draining duration derived from PTO, typically three PTO periods using current recovery estimates, with a conservative fallback before RTT is sampled.
- Expiry behavior: finalize teardown, remove routes/CIDs that were retained only for draining, cancel all remaining connection/link timers, and release connection-owned resources.  It mutates lifecycle/routing cleanup state, so it runs on the connection owner thread; Tx is appropriate if close packet retransmission/state is Tx-owned.
- Cancelled: only by immediate endpoint destruction or an explicit higher-level abort that owns all dependent cleanup.
- Rescheduled: generally not pushed back by ordinary late packets; draining should have a bounded end.  It may be brought forward only for explicit abort/destruction.  If entering closing from an earlier pre-close state, arm once from that transition.

### Key Discard Timer
- Purpose: discard obsolete Initial, Handshake, or old 1-RTT keys after QUIC permits them to be removed while retaining tolerance for reordering.
- Owner: `Cxn`.
- Name: `m_keyDiscardTimer`.
- Started: when handshake progress makes Initial/Handshake keys discardable, or after a 1-RTT key update creates old receive keys that must be retained temporarily.
- Deadline calculation: immediate or near-immediate for packet spaces whose keys are no longer usable by rule; for old 1-RTT keys use the required retention window based on packet threshold/time guidance and current RTT/PTO state.
- Expiry behavior: clear obsolete key material, discard corresponding packet-space sent/received state, and cancel recovery timers that only refer to discarded spaces.  It mutates crypto and packet-space state, so it must run on the thread that owns those states; if crypto state is Rx-owned, the timer callback should dispatch to Rx or schedule a shard-local discard operation rather than mutating from Tx.
- Cancelled: when the connection closes, when the relevant key material was already discarded by a stronger state transition, or when the packet space is never established.
- Rescheduled: bring forward when a state transition makes keys discardable earlier.  Push back only for old 1-RTT key retention if key phase validation proves the reordering tolerance window must extend.

### PMTUD Timer
- Purpose: drive path MTU probe expiry/retry cadence and black-hole recovery.
- Owner: `Link`.
- Name: `m_pmtudTimer`.
- Started: when `Path::startProbe` or `Path::startProbeChecked` marks a PMTUD probe pending on the active path.
- Deadline calculation: probe sent time plus PMTUD probe timeout, derived from PTO/RTT where available or a conservative configured path-probe timeout before RTT is valid.
- Expiry behavior: call `Path::probeExpired()`, schedule retry probe work if requested, or apply black-hole/failure fallback.  It mutates `Path` PMTUD state and send scheduling for that link, so it runs on the Link Tx thread.
- Cancelled: when the probe is ACKed, declared lost by recovery, invalidated by a packet-too-big/send-too-big hint, the active path is retired, or the link closes.
- Rescheduled: after each new probe is sent.  Bring forward only if a new active probe has an earlier expiry.  Do not push back for unrelated traffic; PMTUD probe expiry is tied to the specific probe packet.

### Path Validation Timer
- Purpose: expire outstanding PATH_CHALLENGE validation for a candidate path and fall back to the previous active path if validation does not complete.
- Owner: `Link`.
- Name: `m_pathTimer`.
- Started: when a candidate path is created for migration, NAT rebinding, preferred address, or explicit validation and a PATH_CHALLENGE is sent.
- Deadline calculation: challenge sent time plus path-validation timeout, normally based on PTO/RTT with a conservative fallback before RTT is sampled.
- Expiry behavior: mark candidate path validation failed, retain or restore the previous active path, retire candidate CID/path state as appropriate, notify diagnostics/callbacks, and stop sending non-probe traffic on the failed candidate.  It mutates Link path/CID/send-routing state, so it runs on the Link Tx thread.
- Cancelled: when a matching PATH_RESPONSE validates the candidate path, when the candidate path is abandoned, when the link closes, or when migration is superseded by a newer candidate.
- Rescheduled: on retransmitted PATH_CHALLENGE for the same candidate path, set the deadline from the newest challenge send time.  This can push the deadline back only when an actual new challenge is sent; otherwise keep the original deadline.  Bring forward if policy lowers the validation timeout or a newer candidate supersedes the old one.

## Detailed Design and Implementation Plan
### Phase 1: Introduce Timer Inventory
- Add `ZmScheduler::Timer` members for ACK delay, loss time, idle timeout, draining/closing, key discard, PMTUD, and path validation.
- Keep existing `m_ptoTimer` as the PTO timer instance; rename only if the local style benefits from it and all references are updated directly.
- Add ownership comments near members so future changes do not collapse distinct timers into one object.

### Phase 2: Scheduling Helpers
- Add per-timer schedule/cancel helpers that assert Tx-thread ownership for callbacks that send or mutate recovery state.
- Use `app()->mx()->add()`/`del()` following the existing PTO path.
- Ensure repeated scheduling advances the same timer instance and cancellation is idempotent.

### Phase 3: Lifecycle Cleanup
- Cancel all connection-owned timers during close/drain/destruction.
- Cancel link-owned PMTUD/path-validation timers when the link/path is retired.
- Avoid adding locks; object ownership and scheduler callback discipline provide safety.

### Phase 4: Initial Runtime Wiring
- Move existing PTO scheduling into the new PTO helper.
- Stub handlers for timers whose functional behavior is delivered by later plans; handlers should do no harmful work until corresponding state exists.

## Code References to Impacted Code
- `zquic/src/Zquic.hh:2746` - Existing PTO scheduling entry point to generalize with explicit typed helpers.
- `zquic/src/Zquic.hh:3774` - Existing `m_ptoTimer` member; expand nearby with the other `Cxn` timers.
- `zquic/src/ZquicPath.hh:33` - Existing `Path` utility that should drive `Link` PMTUD/path-validation timer ownership.
- `zquic/src/ZquicEndpoint.cc:312` - Endpoint drain path must cancel or make safe all timers before teardown.

## Detailed Test Plan
- Add a small timer-owner test using a test app scheduler to arm/cancel all eight timers independently.
- Add runtime tests where ACK delay and PTO are both armed and only the earlier callback fires.
- Add teardown tests that close a connection/link with timers armed and verify no callback mutates freed state.
- Keep tests as standalone `zquic/test/*` binaries using `ZuTestUtil`.

## Non-goals
- Do not build a generic timer wheel or replace `ZmScheduler`.
- Do not retain dependent API compatibility if internal timer names move.
- Do not implement full ACK delay, loss-time, PMTUD, or path-validation semantics here beyond safe timer ownership.

## Options and Open Questions
- Option: store deadline metadata beside each timer for diagnostics and test assertions.
  - Resolution: unnecessary
- Open question: whether PMTUD and path validation should be per `Link` or per future path object when multiple candidate paths are active.  Current backlog asks for `Link` ownership, so start there.
  - Resolution: per `Link`

## Acceptance Criteria
- Unit tests cover timer priority ordering and cancellation.
- Runtime tests show ACK delay, loss time, PTO, idle, draining/closing, key discard, PMTUD, and path-validation timers do not clobber each other.
- Tests verify each timer callback is scoped to the owning `Cxn` or `Link` and cannot fire after owner teardown.
- Existing runtime handshake/stream tests still pass.
