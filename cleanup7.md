`Zquic` 0-RTT packet processing plan
====================================

This plan implements `audit.md` priority work item 1:

> Add 0-RTT packet processing: packet protection state, transport parameter
> compatibility checks, early stream acceptance policy, and rejection cleanup.

The target is QUIC v1 0-RTT support that is useful for zhttp while preserving
the existing zquic runtime model: packet number space accounting remains
`AppData`, TLS epoch handling remains explicit, Rx/Tx state stays on the owning
thread, and qlog-only work stays behind `ZquicLOG`.

Current state
-------------

Implemented foundations:

- `PktType::ZeroRTT` is parsed and can be written as a long-header packet.
- `PktNumSpace::AppData` already represents both 0-RTT and 1-RTT packet number
  space, consistent with QUIC.
- TLS epochs map epoch 3 and later to `AppData`.
- packet key levels now distinguish Initial, Handshake, 0-RTT, and 1-RTT while
  still mapping 0-RTT and 1-RTT to `PktNumSpace::AppData`.
- packet protection helpers can protect/unprotect long packets with
  `PktNumSpace::AppData` state.
- long-packet Handshake/0-RTT unprotect uses same-buffer AEAD decrypt; short
  AppData still preserves ciphertext while it retries current, old, and next
  keys.
- stream receive, flow control, ACK/loss/recovery, qlog packet receive, and
  application-data frame legality already exist for `AppData`.
- long-packet receive can select 0-RTT key state and pass an early-data frame
  policy into protected frame consumption.
- Tx now has separate 0-RTT snapshot/install state and a dedicated
  `sendProtZeroRTTPkt_()` helper that writes `PktType::ZeroRTT`, uses AppData
  packet numbers/recovery metadata, logs the packet as 0-RTT, and protects
  existing `PktBuild` spans directly into an uninitialized packet `ZiIOBuf`.
- client/server link wrappers expose the 0-RTT send helper without adding a new
  scheduler lane or buffer owner.
- client Tx can now choose the existing stream pending queue for 0-RTT before
  runtime establishment when early TX keys exist and the app permits the stream;
  the default app hook denies early streams, and the early flush path sends
  STREAM frames only, without ACK or queued-control coalescing.
- sent-packet recovery records now retain the actual packet type, so AppData
  recovery can distinguish long-header 0-RTT from short 1-RTT without adding a
  packet number space.
- first-pass client rejection cleanup can clear the Tx 0-RTT protector, remove
  tagged 0-RTT packet records from AppData recovery, release their in-flight
  accounting, and queue their existing STREAM refs for later 1-RTT
  retransmission.  The retransmit refs still point at the original stream
  buffers; no packet/application payload copy or new buffer owner is introduced.
- a standalone remembered/current transport-parameter compatibility helper
  exists, with focused codec coverage.
- `CryptoConfig::enable0RTT` now enables picotls early-data plumbing instead
  of immediately counting a rejection; client setup can pass an app/cache-owned
  borrowed session-ticket span and server setup can advertise a scalar
  `maxEarlyData`.
- `Crypto` tracks scalar early-data state (`Enabled`, `Offered`, `Accepted`,
  `Rejected`) and makes explicit rejection idempotent.

Implemented integration:

- zquic now has app/cache hooks for borrowed session tickets, remembered
  transport parameters, remembered application parameters, early stream policy,
  and accepted/rejected early-data notification.  The default hooks are
  conservative: no cache, no early stream send, and no server early data unless
  the app opts in.
- client TLS setup can enable 0-RTT from app-owned session state, install
  remembered transport parameters into scalar link state, and pass borrowed
  ticket bytes to picotls without introducing a zquic-owned ticket buffer.
- server TLS setup can advertise a scalar max-early-data value from app policy.
- runtime link state now tracks Rx/Tx accepted/rejected early data, validates
  remembered transport parameters against the server's current parameters,
  rejects incompatible early data while allowing the handshake to continue, and
  cleans up tagged 0-RTT recovery records on rejection.
- zhttp now has a default replay-safe early request policy: GET, HEAD, and
  OPTIONS are eligible only when they do not carry a body.  The zhttp test
  client answers zquic's early-stream hook from request metadata.

Remaining limitations:

- zquic does not own persistent session-ticket storage or cache eviction; those
  remain application/cache responsibilities behind the borrowed-span hooks.
- undecryptable/reordered 0-RTT received before usable Initial processing is
  still dropped rather than buffered, consistent with the current no
  undecryptable-packet-buffering posture.
- the first zhttp integration keeps the safe request policy intentionally small
  and does not add a production H3 session cache.

Guideline constraints
---------------------

Apply `GUIDELINES.md` and `zquic/GUIDELINES.md` throughout:

- keep all runtime packet, stream, ACK, and crypto delivery work on the owning
  Rx or Tx thread;
- do not add locks, atomics, STL containers, concepts, `requires`, virtual
  dispatch, compatibility shims, or broad policy frameworks;
- reuse existing `Zquic` packet types, packet spaces, transport parameter
  structs, stream/flow-control helpers, qlog event structs, and `ZiIOBuf`
  buffers;
- avoid heap allocation and temporary contiguous copies in packet protection
  and frame dispatch paths;
- make 0-RTT qlog additions visible at protocol decision points with
  `ZquicLOG((...))`, capture only bounded by-value data, and leave JSON/string
  mapping to the logger thread;
- delete or rewrite the old unconditional rejection tests instead of keeping
  compatibility shims for unsupported behavior.

Buffer/data-movement audit
--------------------------

This plan must not introduce a new zquic buffer type or a new packet/application
payload copy path.  The 0-RTT implementation should use these boundaries:

- packet data remains in existing pooled `ZiIOBuf` objects and existing
  `PktBuild` storage;
- Rx packet protection should decrypt into the already-received mutable packet
  buffer whenever ciphertext does not need to be preserved for retry.
  Initial, Handshake, and 0-RTT long-packet decrypt now use
  `ptls_aead_decrypt()` with `output == input == packet + payloadOffset`.  Do
  not remove ciphertext-preserving trial decrypt from short AppData while
  receive still retries current, old, and next keys on the same packet;
- Tx packet protection writes one-way from stream/application buffers into
  uninitialized packet buffers, preserving the existing source-buffer ownership
  and avoiding staging copies;
- if any current Tx packet path violates that one-way model by constructing a
  temporary plaintext packet buffer before protection, fix that path as part of
  the 0-RTT work rather than copying the pattern;
- app/session parameters are owned by the application or TLS/session cache, not
  copied into `Link` hot-path state;
- zquic runtime code passes borrowed spans only while synchronously validating
  app parameters or stream eligibility;
- qlog never captures packet, stream, ticket, PSK, or app-parameter bytes;
- if persistent session-ticket storage is needed, it belongs to the
  application/cache layer and must use existing Z array/string types with a
  named `ZmHeapID`, outside packet Rx/Tx paths.

The implementation should treat any proposed `ZtArray`/`ZtString`/custom buffer
field in `Link`, `Crypto`, stream send state, or packet processing as an audit
failure unless it replaces an existing owner and avoids an extra copy.

The current Tx packet-protection path already follows the required one-way
shape: stream packetization writes STREAM frame prefixes into `PktBuild`
scratch, `PktBuild` keeps stream payloads as `TxRange` / `ptls_iovec_t` spans,
`sendProtInitialPkt_()`, `sendProtHandshakePkt_()`, and `sendProtShortPkt_()`
allocate the destination packet `ZiIOBuf` and write only the header/packet
number there before calling `InitialPktProt::protectLongV()` or
`PktProt::protect*V()`, and those helpers encrypt the span vector directly into
the destination packet buffer.  `sendProtZeroRTTPkt_()` now reuses the same
shape for 0-RTT: it writes only the long header and packet number into the
destination packet buffer, pads the existing `PktBuild` for the header
protection sample, and calls `PktProt::protectLongV()` over the existing span
vector.  It does not introduce a packet plaintext staging buffer, a new zquic
buffer type, or a payload copy.

The current Rx packet-protection path now follows the desired in-place model
for long packets: `InitialPktProt::unprotectLong()` calls
`InitialCrypto::decrypt()` with the packet payload as both input and output,
and `PktProt::unprotectLong()` uses same-buffer `ptls_aead_decrypt()` for
Handshake and 0-RTT.  Treat `ptls_aead_decrypt()` as same-buffer safe for the
cipher set zquic uses here, excluding non-temporal variants that are outside
this scope; this is a dependency guarantee, not something the 0-RTT work needs
to revalidate.  Short AppData still uses `Zquic.PktProt.Plain` scratch because
the receive path may retry current, old, and next keys on the same packet; that
scratch can be removed only with an explicitly ciphertext-preserving key
selection design.

Design target
-------------

0-RTT should be represented as a packet type and TLS phase, not as a new QUIC
packet number space.  Runtime accounting for packet numbers, ACKs, loss,
congestion, and stream frames should use `PktNumSpace::AppData`; packet logs and
drop diagnostics should still preserve `PktType::ZeroRTT` where the header type
matters.

The server should accept early data only when all of these are true:

- TLS has derived usable 0-RTT read traffic keys and reports early-data
  acceptance;
- the client supplied a resumption/early-data credential accepted by TLS;
- remembered server transport parameters from the original connection are
  compatible with the current server transport parameters under RFC 9001 0-RTT
  rules;
- the 0-RTT frame mix is restricted to application-data-safe frames, using the
  existing `AppData` legality table plus explicit exclusions for frames that
  must not be processed before handshake confirmation;
- stream creation and flow-control limits are bounded by the remembered
  0-RTT-compatible peer view, not by later relaxed values.

The client should send 0-RTT only when it has a valid cached resumption ticket
plus remembered transport parameters for the server identity and ALPN.  If the
server rejects early data, the client must cleanly discard/requeue early stream
state according to the selected application policy instead of silently treating
the rejected bytes as delivered.

Reference alignment
-------------------

Use the four local reference implementations as alignment checks, but keep the
zquic implementation smaller:

- `../zngtcp2` keeps 0-RTT as `NGTCP2_PKT_0RTT` plus
  `NGTCP2_ENCRYPTION_LEVEL_0RTT`, while normal ACK/recovery state remains in
  the application packet namespace.  It buffers reordered server-side 0-RTT
  that arrives before Initial processing, restricts 0-RTT frame types, and has
  a dedicated early-data rejection cleanup path that removes early recovery
  records, streams, flow-control side effects, queued control frames, and
  0-RTT keys.
- `../msquic` uses a distinct `QUIC_PACKET_KEY_0_RTT` key type that maps to
  the 1-RTT/application packet space.  Its packet builder selects 0-RTT only
  while 0-RTT write keys exist and 1-RTT write keys do not.  On rejection it
  discards 0-RTT keys, notifies loss detection, and tracks per-stream 0-RTT
  send length.
- `../quiche` exposes an `ENCRYPTION_ZERO_RTT` level to crypto/framer code,
  validates remembered transport and HTTP/3 settings, reports
  `EarlyDataAccepted()`, retransmits or abandons 0-RTT packets explicitly, and
  treats a 0-RTT packet number after 1-RTT as invalid.
- `../mvfst` gates early data through cached PSK transport/app parameters and
  an application parameter validator.  zquic should take the validation shape,
  not mvfst's buffer ownership model.  It also marks 0-RTT packets lost in
  path-racing cases so application data can be resent under the eventual path
  and key.

The common shape to adopt is therefore:

- separate 0-RTT key state from 1-RTT key state;
- share application packet-number/recovery accounting;
- let application policy validate cached app parameters before sending early
  data;
- restrict 0-RTT frames to replay-safe application data;
- make early-data rejection an explicit cleanup event, not a generic packet
  protection failure.

Minimal zquic design
--------------------

The least disruptive zquic design is to add a thin "early AppData" layer around
the existing AppData packet number space.

Add a packet-key phase enum separate from `PktNumSpace`:

```
struct PktKeyLevel {
  ZtEnum(PktKeyLevel, int8_t, Initial, Handshake, ZeroRTT, OneRTT);
};
```

Do not replace `PktNumSpace`.  Instead add helpers:

```
inline PktNumSpace::T pktNumSpaceFromKeyLevel(PktKeyLevel::T);
inline bool isAppDataKeyLevel(PktKeyLevel::T);
inline PktType::T pktTypeFromKeyLevel(PktKeyLevel::T);
```

This matches msquic/quiche/ngtcp2 without forcing churn through ACK, PTO,
loss, stream, and qlog code that already keys on `PktNumSpace::AppData`.
Initial and Handshake continue to map one-to-one.  `ZeroRTT` and `OneRTT` both
map to `PktNumSpace::AppData`.

Extend `Crypto` with small fixed state, not a new subsystem:

- `m_earlyDataEnabled`: configuration/requested policy, replacing the current
  unconditional rejection behavior;
- `m_earlyDataState`: enum `Disabled`, `Offered`, `Accepted`, `Rejected`,
  `Done`;
- `m_txEarlyTrafficSecret`, `m_rxEarlyTrafficSecret`, `m_txEarlyProt`,
  `m_rxEarlyProt`, or a compact two-slot AppData key container indexed by
  `ZeroRTT`/`OneRTT`;
- `m_zeroRTTMaxData`: bounded max early-data bytes from ticket/cache policy;
- `m_zeroRTTParams`: remembered server transport parameters supplied by the
  cache for compatibility checks;
- no owned app-parameter payload buffer in `Crypto`; keep app params in the
  app/session cache and pass only borrowed validation spans or scalar results
  through TLS setup.

Keep 1-RTT readiness exactly where it is: `m_oneRTTReady` should still mean
usable 1-RTT keys and handshake completion progress.  A link can have 0-RTT
keys while `runtimeEstablished_()` is false.

Add link-level early state because cleanup touches streams, recovery, queued
frames, and app callbacks:

```
struct EarlyDataState {
  ZtEnum(State, int8_t, None, Offered, Recv, Accepted, Rejected, Done);
  State::T state = State::None;
  uint64_t sentBytes = 0;
  uint64_t recvBytes = 0;
  TransportParams rememberedParams;
  ZeroRTTReason::T reason = ZeroRTTReason::None;
};
```

Store it in `Link`, owned by the existing Rx/Tx thread transitions.  Keep
cross-thread operations as posts with by-value scalar snapshots.  Do not let
application hooks retain references to it.

Packet receive design
---------------------

Refactor `receiveProtLongPkt_()` just enough to choose a key level before
unprotecting:

```
PktKeyLevel::T keyLevel =
  h.type == PktType::Initial ? PktKeyLevel::Initial :
  h.type == PktType::Handshake ? PktKeyLevel::Handshake :
  h.type == PktType::ZeroRTT ? PktKeyLevel::ZeroRTT :
  PktKeyLevel::N;
PktNumSpace::T space = pktNumSpaceFromKeyLevel(keyLevel);
```

Then branch by `keyLevel`, not by `space`, for packet protection:

- `Initial`: existing `InitialPktProt::unprotectLong()`;
- `Handshake`: existing `PktProt::unprotectLong()` with Handshake state;
- `ZeroRTT`: `PktProt::unprotectLong()` with early AppData RX protection
  state.

Everything after successful unprotect should continue to use `space`, so
duplicate detection, ACK recording, receive qlog, stream delivery, and recovery
stay on `PktNumSpace::AppData`.

Server-side 0-RTT that arrives before the server has processed a usable Initial
should be dropped and qlogged as `MissingKeys`/`ZeroRTTBeforeInitial`, matching
zquic's current "no undecryptable buffering" posture.  Do not add
undecryptable-packet buffering for the first implementation.  If later
interoperability work requires reordered 0-RTT retention, it must reuse
existing `ZiIOBuf` datagram ownership and be covered by a separate design
review.

0-RTT packet number ordering must be checked against AppData state.  Once any
1-RTT short packet has been accepted, later 0-RTT long packets should be
discarded and diagnosed; quiche treats 0-RTT after 1-RTT as invalid because the
shared packet number space has advanced beyond early-data semantics.

Frame policy
------------

Add an explicit early-data flag to frame consumption rather than overloading
`runtimeEstablished_()`:

```
enum { EarlyData = 1, NormalData = 0 };
packetFrameLegal_(space, frame, earlyData)
```

For `earlyData == true`, allow only the minimal replay-safe set for zquic's
current feature set:

- `Padding`
- `Ping`
- `Stream`
- possibly `ResetStream` and `StopSending` only for streams opened by the same
  early-data attempt, after tests prove cleanup is correct

Reject in 0-RTT:

- `Ack`, because 0-RTT cannot acknowledge peer packets;
- `Crypto`, `HandshakeDone`, and `NewToken`;
- connection-ID, path, close, and flow-control control frames for the first
  implementation, even where the RFC allows some application-data frames, to
  keep replay side effects narrow;
- DATAGRAM until zquic implements the extension.

This is stricter than the RFC's full allowance but matches the "useful for
zhttp first" target.  The restriction can be relaxed later behind explicit
tests.

Stream and HTTP policy
----------------------

Keep zquic transport policy generic and let zhttp decide request semantics.
Expose two non-owning app hooks with side-effect-safe defaults:

```
bool earlyDataParams(ZuBSpan &params);       // borrowed app/cache-owned bytes
bool validateEarlyDataParams(ZuBSpan params);
bool allowEarlyStream(StreamID, bool fin);
```

The exact signatures should follow nearby CRTP app-hook style.  The important
contract is:

- zquic validates transport parameters and replay-safe frame shape;
- zhttp validates ALPN/H3 settings and method/request policy;
- the client only queues existing stream bytes into 0-RTT after both cache
  validation and `allowEarlyStream()` succeed;
- the server marks streams opened by 0-RTT until TLS finalizes early-data
  acceptance, so zhttp can defer irreversible application actions if needed.

Do not pass stream payload previews to app policy from the packet builder.  The
application already owns request metadata before enqueueing stream data; making
the transport inspect payload bytes would add avoidable span plumbing and tempt
temporary copies.

For zhttp, start with a conservative default:

- allow 0-RTT only for requests the application marks idempotent;
- default deny if request metadata is incomplete;
- disable QPACK dynamic-table dependencies in 0-RTT unless cached settings
  prove the encoder/decoder state is compatible;
- fall back to normal 1-RTT send on rejection.

Transport parameter compatibility
---------------------------------

Add:

```
struct ZeroRTTReason {
  ZtEnum(ZeroRTTReason, int8_t,
    None, Disabled, MissingTicket, TLSRejected, AppParams,
    TransportParams, FlowLimit, StreamLimit, ActiveCIDLimit,
    FramePolicy, MissingKeys, AfterOneRTT);
};

ZeroRTTReason::T validateZeroRTTParams(
  const TransportParams &remembered,
  const TransportParams &current);
```

Use closed enum reasons for diagnostics and qlog.  Start with RFC 9001
constraints for remembered transport parameters:

- `initialMaxData`, `initialMaxStreamDataBidiLocal`,
  `initialMaxStreamDataBidiRemote`, `initialMaxStreamDataUni`,
  `initialMaxStreamsBidi`, `initialMaxStreamsUni`, and
  `activeCxnIDLimit` must not decrease;
- `maxUDPPayloadSize` must remain usable for packets the client may send;
- `disableActiveMigration`, ACK delay parameters, idle timeout, and CID fields
  should be compared according to the RFC's remembered-parameter rules and
  rejected on ambiguity rather than guessed;
- unknown or future parameters should default to rejection until represented in
  `TransportParams`.

When accepted, initialize early stream and flow-control limits from
`remembered`, then apply current peer transport parameters after the handshake
completes.  This mirrors quiche's focus on rejecting reduced limits and
ngtcp2's explicit early-data state reset.

Send path design
----------------

Add a client Tx helper that mirrors existing packet send helpers:

```
bool sendZeroRTTPkt_(PktBuild &payload, ZiSockAddr addr,
  ZuBSpan recordFrame = {}, PktTxRecordRefs *recordRefs = nullptr,
  bool ackEliciting = true);
```

Internally it should call a shared long-header AppData protection helper or a
dedicated `sendProtZeroRTTPkt_()`:

- write `PktType::ZeroRTT` with long-header length and packet number length;
- write protected packet bytes directly into the destination `ZiIOBuf` packet
  buffer from existing `PktBuild` / stream buffer spans, without first making a
  contiguous plaintext packet copy;
- treat this as a required Tx invariant for the new 0-RTT path; if an existing
  helper cannot preserve it, split or replace the helper instead of adding a
  compatibility staging buffer;
- use `PktNumSpace::AppData` packet numbers and recovery metadata;
- use early AppData TX protection state, not 1-RTT state;
- record the packet as 0-RTT so rejection cleanup can remove or reclassify it;
- never send CRYPTO in 0-RTT.

The low-level helper exists and satisfies those packet-buffer invariants.
Client Tx selection now has a conservative first path: while runtime
establishment is still false, `flushTx_()` may call `flushEarlyStreams_()` only
if early TX protection is installed.  `flushEarlyStreams_()` reuses the
existing stream queue, flow-credit checks, `StreamPktizer`, and `PktBuild`
spans, calls the app's `allowEarlyStream(link, streamID, fin)` hook before
payload packetization, and sends only STREAM frames through
`sendZeroRTTPkt_()`.  The default app hook returns false, so existing
applications do not send early data until they opt in.

Tx selection should follow msquic's rule:

- if 1-RTT TX keys are available, send application data as short-header 1-RTT;
- else if 0-RTT TX keys are available and early stream policy allows it, send
  eligible STREAM data as long-header 0-RTT;
- otherwise queue for normal 1-RTT.

Do not add a new scheduler lane.  Use the existing app-data pending stream
machinery with an eligibility check at packet-build time.

Rejection cleanup
-----------------

Implement one idempotent link helper:

```
void rejectEarlyData_(ZeroRTTReason::T reason);
```

First-pass client Tx cleanup is implemented for replay-approved STREAM data:
0-RTT packets are tagged as `PktType::ZeroRTT` inside AppData recovery,
`rejectZeroRTTTx_()` clears the Tx early protector, removes those packet
records, releases bytes in flight, and reuses recovery's existing
`SentFrameRef` queue so stream buffers are resent later under 1-RTT.  This is
intentionally not a payload-copy path and not a new buffer type.

It should:

- mark early-data state rejected and qlog the reason;
- discard early RX/TX protection state;
- remove 0-RTT packet records from the AppData recovery table and requeue only
  replay-approved client STREAM refs for later 1-RTT retransmission, according
  to existing sent-packet record semantics;
- clear queued control frames generated only by early data;
- reset stream-open state and byte counters for streams opened only by 0-RTT,
  or move eligible unsent/unacked data back to the normal 1-RTT send queue;
- restore connection and stream flow-control counters derived from remembered
  early limits;
- notify zhttp through a narrow callback so application request state can fall
  back or fail deterministically.

For the first implementation, prefer retransmitting client 0-RTT STREAM data as
1-RTT after rejection when the application explicitly marked it replayable.
Drop non-replayable or ambiguous early data and surface a deterministic
application error.  This follows msquic/quiche's explicit retransmission/loss
handling while keeping zhttp safe by default.

Acceptance cleanup is smaller:

- when TLS confirms early-data acceptance, convert early streams to normal
  streams;
- keep their AppData packet-number/recovery records intact;
- discard early keys once 1-RTT keys are installed and no 0-RTT packet can be
  accepted;
- emit qlog accept diagnostics.

Implementation plan
-------------------

1. Audit and expose the picotls early-data hooks already linked through
   `ZquicCrypto`:
   - identify the ticket/session APIs needed for client resumption and server
     validation;
   - add `PktKeyLevel` and compact `Crypto` state for early-data intent,
     accepted/rejected result, max early-data size, and 0-RTT key
     installation;
   - keep the public API small: configuration enables 0-RTT, while runtime
     links query explicit `earlyDataAccepted()` / `earlyDataRejected()` style
     state.

2. Add a 0-RTT session cache contract at the zquic boundary:
   - keep resumption material, ALPN, server name, app parameters, and
     remembered server transport parameters in the app/session cache, outside
     `Link` and packet Rx/Tx paths;
   - make cache lookup/update explicit in client/server app hooks so zhttp can
     choose whether to persist tickets;
   - pass borrowed spans or scalar validation results into zquic setup; do not
     copy app-parameter or ticket bytes into a new zquic buffer;
   - avoid unbounded growth by requiring deterministic cache ownership and
     eviction outside the hot packet path.

3. Implement transport parameter compatibility checks:
   - add a helper beside `TransportParams::validate()` that compares remembered
     and current parameters for 0-RTT acceptance;
   - reject if any RFC-forbidden parameter changed incompatibly, especially
     `initial_max_data`, stream-data limits, stream-count limits,
     `active_connection_id_limit`, and other parameters that constrain early
     client behavior;
   - allow compatible increases where the RFC permits them;
   - return a closed enum reason for diagnostics and qlog rather than arbitrary
     strings.

4. Add link-level early-data state and cleanup hooks:
   - store only scalar/struct state in `Link`, with no heap-owned policy
     objects on the hot path;
   - mark packets and streams touched by 0-RTT so acceptance can promote them
     and rejection can clean them deterministically;
   - add one idempotent `rejectEarlyData_()` helper that handles qlog,
     protection-state discard, stream/recovery cleanup, and application
     notification.

5. Enable TLS early-data negotiation without bypassing current handshake
   validation:
   - replace the unconditional `enable0RTT` rejection in `Crypto::init()` with
     real setup;
   - configure client `max_early_data_size` and server context state from
     app/cache policy;
   - install AppData 0-RTT RX/TX traffic secrets when picotls provides them,
     while keeping 1-RTT key readiness distinct;
   - keep Initial and Handshake discard behavior unchanged.

6. Add 0-RTT packet protection send/receive paths:
   - done: add `sendProtZeroRTTPkt_()` that writes `PktType::ZeroRTT` and
     protects with AppData 0-RTT traffic keys;
   - done, first pass: allow client Tx to build STREAM-only 0-RTT packets
     before handshake completion when early TX keys exist and app stream policy
     permits it;
   - done: keep `PktProt::unprotectLong()` on the same-buffer long-packet
     decrypt path, with coverage for Handshake and 0-RTT;
   - keep short AppData trial decrypt ciphertext-preserving while current,
     old, and next key retries share the same packet buffer;
   - done: `receiveProtLongPkt_()` selects 0-RTT key state, decrypts the long
     packet in place, enforces early frame policy, records early-data Rx state,
     and rejects 0-RTT that arrives after accepted 1-RTT;
   - keep short-header 1-RTT receive gated on established 1-RTT keys.

7. Define and enforce early stream policy:
   - done: use a conservative zhttp-safe default where only bodyless GET, HEAD,
     and OPTIONS are eligible for 0-RTT unless the application opts into broader
     replay risk;
   - done, zquic boundary: add a default-deny application hook to approve or
     reject an early stream send before already-queued stream bytes enter
     0-RTT packetization, without copying or previewing payload bytes;
   - done: on receive, mark streams touched by 0-RTT until handshake acceptance
     is final;
   - done: prevent non-stream control frames that would mutate connection state
     in unsafe ways before the handshake is confirmed.

8. Implement acceptance and rejection transitions:
   - done: TLS and transport-parameter rejection discard 0-RTT keys, clear or
     reclassify early recovery records, and leave normal Handshake/1-RTT
     progress intact;
   - done: rejected 0-RTT packets are not ACKed as accepted application data;
   - done: non-replayable 0-RTT frames are never retransmitted after rejection,
     while explicitly replay-approved STREAM data is preserved for 1-RTT
     retransmission through existing stream refs;
   - done: acceptance promotes early streams to normal streams and retires the
     dedicated 0-RTT Tx protector;
   - done: diagnostics distinguish TLS rejection, missing keys, incompatible
     transport parameters, frame policy, and late 0-RTT after 1-RTT.

9. Update qlog and diagnostics:
   - done: convert the current private `zquic:zero_rtt_rejected` coverage from
     unsupported-posture logging into real rejection logging;
   - done: preserve packet receive/drop events with `packet_type` set to `0RTT`
     and `packet_number_space` set to `application_data`;
   - done: add bounded enum-mapped security reasons for 0-RTT rejection causes;
   - done: keep qlog-only state snapshots inside `ZquicLOG((...))`.

10. Update tests in focused layers:
   - crypto tests for enablement, key installation, TLS accept/reject state, and
     old unconditional rejection counter removal;
   - codec/transport tests for 0-RTT parameter compatibility, including
     compatible increases and incompatible reductions;
   - packet-protection/runtime helper tests for long-header
     `PktType::ZeroRTT` protect and unprotect using AppData early keys;
   - runtime tests for accepted 0-RTT stream delivery, rejected 0-RTT cleanup,
     duplicate/lost 0-RTT packet handling, and qlog output;
   - zhttp tests for the selected early request policy and default fallback when
     0-RTT is rejected or unavailable.

11. Update documentation and audit state:
   - done: revise `audit.md` so it no longer describes 0-RTT as header-only
     parsing;
   - done: document the new app/cache hooks in this plan and tests through
     focused call sites;
   - done: remove stale comments and assertions that say 0-RTT is intentionally
     unsupported.

Acceptance criteria
-------------------

- `make -C zquic/src -j8`
- `make -C zquic/test -j8`
- `make -C zquic/test test`
- `make -C zhttp/test -j8`
- `make -C zhttp/test test`
- New and updated tests cover accepted 0-RTT, rejected 0-RTT, transport
  parameter incompatibility, early stream policy, retransmission cleanup, and
  qlog diagnostics.
- The implementation adheres to `GUIDELINES.md` and `zquic/GUIDELINES.md`,
  especially the hot-path allocation/copy rules, Rx/Tx ownership model, and
  qlog gating/capture rules.
- `audit.md` no longer claims that 0-RTT packets are only parsed and dropped;
  any remaining 0-RTT limitations are documented precisely.

Verification
------------

Passed on the debug clang build configured with `./z.config -d -L /usr`:

- `make -C zquic/src -j8`
- `make -C zquic/test -j8`
- `make -C zquic/test test` (`Files=20, Tests=145, Result: PASS`)
- `make -C zhttp/test -j8`
- `make -C zhttp/test test` (`Files=11, Tests=122, Result: PASS`)
