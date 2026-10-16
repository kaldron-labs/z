# Ownership and member-placement re-audit

## Scope and rules applied

This is a static audit of the current worktree in `ztcp`, `ztls`, `zquic`,
and `zhttp`.  It re-checks the claims in `findings.md` and `findings2.md`
against the current code rather than carrying them forward automatically.

The audit treats the following as intentional exceptions, as requested:

- telemetry counters and advisory snapshots may be read or updated uncleanly;
- values initialized/published before use and stable for the relevant lifetime
  may be read by either shard;
- state with an explicit exceptional sharing mechanism is allowed, including
  the read-mostly `m_cxn` convention and genuine shared atomics/locks;
- the required physical order is stable/shared members first, then one
  cache-line-aligned Rx section, then one cache-line-aligned Tx section.  Only
  the first member of each Rx/Tx section is aligned.

The findings below therefore do not flag formal races in telemetry, the shared
`m_cxn` references, or immutable configuration merely because another thread
can observe them.

## Priority findings

### 1. QUIC's stream registry is still used as live canonical state by both shards

`Zquic::Link::m_streams` is declared Rx-exclusive
(`zquic/src/ZquicLink.hh:9428`), and Rx creates, removes, iterates, and closes
its entries.  Tx nevertheless traverses the same hash and dereferences the
same `Stream` objects in packetization, ACK/loss processing, retransmission,
control validation, and diagnostics.  Representative accesses are:

- `queuedControlFrames()` iterates the table while explicitly on Tx
  (`ZquicLink.hh:199-216`);
- sent-frame ACK/loss handling calls `findStream()`
  (`ZquicLink.hh:1169`, `1198`, `1257`, `1305`, `1350`);
- Tx control packetization and validation calls `findStream()`
  (`ZquicLink.hh:4283-4289`, `4338-4341`, `8844-8874`);
- Rx deletes entries in `removeStream_()` (`ZquicLink.hh:9005-9006`).

This is not telemetry, stable state, or protected state.  It permits Tx to
look up or dereference a node while Rx removes it.  The repair should retain
Rx ownership of the registry and publish Tx-owned per-stream transmission
state/handles explicitly.  Adding a lock to the registry would violate the
sharding rule.

### 2. QUIC link close and lifecycle state remains multi-owner

`m_appClose` and `m_linkState` are in the Rx section
(`ZquicLink.hh:9414`, `9474`), but their protocol state is read and mutated on
both shards:

- Rx close/drain paths update them (`ZquicLink.hh:498-503`, `5399-5403`);
- active migration failure is asserted Tx and calls `closeState_()`
  (`ZquicLink.hh:2340-2363`);
- Tx close-frame construction reads `m_appClose`
  (`ZquicLink.hh:505-514`, called by the client/server send paths);
- `closed()`, `established_()`, `closing_()`, and `draining_()` are used in
  both Rx and Tx control flow.

These fields affect protocol decisions and are not advisory telemetry.  Close
initiation needs an explicit Rx-to-Tx or Tx-to-Rx snapshot/event, with each
shard maintaining its own close/lifecycle state.  A shared non-atomic struct is
not an acceptable publication mechanism.

Proposed repair:

1. Replace the single protocol state with owner-local state:
   `m_rxLinkState` and `m_rxAppClose` in the Rx section, and
   `m_txLinkState` and `m_txAppClose` in the Tx section.  Rename the predicates
   accordingly (`establishedRx_()`, `establishedTx_()`, and so on), assert the
   expected owner in each, and use only the owner-local predicates in protocol
   control flow.
2. Define a small fixed-size close snapshot containing the error code,
   application/transport close kind, and target lifecycle state.  Rx-originated
   transitions apply to Rx state first and post the snapshot to a Tx apply
   function.  The Tx function updates Tx state and performs Tx-owned work such
   as close-frame construction, queue suppression, and timer cancellation.
3. Handle Tx-originated close requests, notably active-migration failure, in
   the opposite direction: update Tx state immediately, then post the fixed
   snapshot to an Rx apply function that closes Rx streams, resets Rx/TLS
   receive state, and performs the canonical lifecycle transition.  Use
   distinct `request` and `apply` functions so applying a snapshot never posts
   it back to its source shard.
4. Keep peer CONNECTION_CLOSE handling Rx-owned.  Rx enters its draining state
   and posts a draining snapshot to Tx; Tx then cancels its timers, clears its
   packet/stream queues, and starts the close timer.  Final transition to
   `Closed` remains Rx-owned after disconnect completion and is published once
   to Tx if the object remains live.
5. Emit lifecycle qlog events and call `app()->linkState_()` only from the
   canonical Rx transition functions.  Updating a Tx mirror must not emit a
   second event or adjust hub counters.
6. Do not use either owner-local state for unsharded public polling.  If the
   synchronous `closed()` and `closeError()` API must remain, back it with a
   separate atomic/advisory snapshot updated by the canonical Rx transitions;
   no protocol decision may read that snapshot.  Otherwise replace those
   methods with an Rx-posted callback snapshot.  Telemetry may similarly read
   a dedicated advisory lifecycle snapshot rather than either live state
   machine.

This keeps the frequent state tests local and lock-free.  Cross-shard work is
limited to the infrequent lifecycle transitions, and the close snapshot is
small enough to capture by value in the shard post.

### 3. QUIC stream-limit and local-stream allocation state is split across owners

`m_peerLimit` is physically in the Rx section (`ZquicLink.hh:9423-9427`), but
it is the Tx-side limit governing locally initiated streams:

- `txApplyMaxStreams_()` is asserted Tx and extends it
  (`ZquicLink.hh:952-957`);
- `openQueued_()` and control validation read/mutate it on Tx
  (`ZquicLink.hh:8608-8631`, `8864-8867`);
- transport-parameter installation writes it on Rx
  (`ZquicLink.hh:3747-3751`);
- `stream()` mutates it without an owner assertion (`ZquicLink.hh:285-293`).

The limit belongs with the Tx-owned local stream ordinals, queued counts, and
blocked controls.  Rx should publish initial/MAX_STREAMS values to Tx.  In the
opposite direction, Tx control validation currently reads Rx-owned
`m_localLimit` (`ZquicLink.hh:8852-8856`); the queued MAX_STREAMS control needs
to carry or use a Tx-owned snapshot rather than consult Rx state.

Proposed repair:

1. Leave `m_localLimit` where it is: it is Rx-owned and governs peer-initiated
   streams.  Move only `m_peerLimit` into the existing Tx section beside the
   local stream ordinals and queued-open state.  The existing names are
   adequate; section placement and owner assertions make ownership clear.
2. In `establish_()`, capture the peer's two initial stream limits by value and
   post them to a small Tx apply function instead of assigning `m_peerLimit`
   on Rx.  Received MAX_STREAMS already has the right shape—Rx validates it
   and posts `{type, value}` to `txApplyMaxStreams_()`—so retain that path.
   FIFO ordering with the existing lifecycle publication is sufficient; no
   new snapshot type, mirrored limit, or reverse notification is needed.
3. Assert Tx ownership in `stream()` and the local-allocation helpers and
   accessors that use `m_peerLimit`, `m_next*Ordinal`, or `m_queued`.
   `setPeerStreamLimit()` is test-only in practice and should simply become a
   Tx-only helper.  Keep peer-stream acceptance, `m_localLimit`, and its
   accessors Rx-owned.
4. For a queued MAX_STREAMS control, remove the Tx read of `m_localLimit` from
   `controlStillValid_()`.  The Tx-owned
   `m_maxStreamsControl[type].frame` already contains the latest monotonically
   coalesced value and is sufficient to reject an obsolete frame.  Add one
   deferred-shard test covering initial-limit publication, received
   MAX_STREAMS opening queued local streams on Tx, and MAX_STREAMS control
   validation without Rx state access.

This is a placement change, one Rx-to-Tx post carrying two scalars, owner
assertions, and removal of one unnecessary cross-shard validation read.
Reconnect queue policy, 0-RTT policy, public telemetry redesign, and general
allocation reset are separate concerns and are not required to repair this
finding.

### 4. QUIC stream error state is written by both Rx and Tx

`Stream::m_appError` and `m_error` are declared Rx-exclusive
(`zquic/src/ZquicStream.hh:844-845`).  Peer RESET_STREAM/STOP_SENDING writes
them on Rx (`ZquicStream.hh:525-536`, `549-555`), while local `reset()` and
`stop()` write the same fields with Tx-owned state (`ZquicStream.hh:461-485`).
Tests also exercise local reset after explicitly entering Tx.

The repair should split error state by stream direction, which also gives each
copy one shard owner:

1. Replace `m_appError`/`m_error` with Rx-owned
   `m_rxAppError`/`m_rxError` and Tx-owned
   `m_txAppError`/`m_txError`, placed in their existing aligned member
   sections.  Replace the ambiguous `appError()`/`error()` API with explicit,
   shard-asserted Rx and Tx accessors.  The HTTP/3 delayed Tx-completion check
   must use the Tx accessor.
2. Record errors according to the direction they terminate.  A received
   RESET_STREAM sets the Rx error; a locally initiated RESET_STREAM sets the
   Tx error.  A received STOP_SENDING applies the Tx error in the existing
   `completeStopTx_(appError)` handoff instead of writing shared state on Rx.
   A locally initiated STOP_SENDING records the Rx error through one small
   Tx-to-Rx post carrying only the application error code.
3. Give RESET_STREAM precedence over STOP_SENDING within each owner-local
   error pair.  Thus a deferred local STOP_SENDING update cannot overwrite a
   peer RESET_STREAM already processed on Rx, and a deferred peer
   STOP_SENDING completion cannot overwrite a local RESET_STREAM already
   applied on Tx.  This makes the existing STOP_SENDING/local-reset race
   deterministic without locks or atomics.
4. Extend the existing Tx-to-Rx stream-reap post with the Tx error and
   application-error scalars.  Rx qlog construction can use its Rx-owned cause
   when present and otherwise use that Tx snapshot; it need not read Tx-owned
   stream state or add a shared telemetry mirror.
5. Extend the deferred-shard stream test to cover both precedence races and
   verify that Rx and Tx accessors report their respective causes.  Retain the
   existing RESET_STREAM/STOP_SENDING frame and HTTP Tx-completion tests after
   updating them to the directional API.

This requires one additional post only for the infrequent locally initiated
STOP_SENDING transition.  The received STOP_SENDING path and stream-reap path
already cross shards and merely gain fixed scalar state.  No shared atomic,
lock, general stream-state mirror, or teardown redesign is required.

### 5. TCP and TLS server stop initialization occurs before entering Rx

Both server `stop_()` hooks mutate fields declared Rx-exclusive before posting
the continuation:

- TCP calls `stopListening()` and writes `m_stopCount`/`m_stopDrained` before
  `rxRun()` (`ztcp/src/Ztcp.hh:982-990`); the affected section is
  `Ztcp.hh:1003-1009`.
- TLS does the same (`ztls/src/Ztls.hh:2398-2406`); the affected runtime state
  is at `Ztls.hh:2425-2428`.

The entire stop initialization, including cancelling the rebind timer and
changing `m_listening`, must begin inside the configured Rx post.  The base hub
implementation already follows that pattern.

### 6. TCP/TLS listen completions assume the multiplexer Rx is the hub Rx

TCP and TLS pass `listening()` and `listenFailed()` directly to
`ZiMultiplex::listen()` (`Ztcp.hh:932-942`, `Ztls.hh:2348-2358`).  Those
callbacks execute on the multiplexer I/O Rx shard, while each hub permits a
different configured Rx thread.  The callbacks directly mutate the hub's
Rx-owned `m_listening` and `m_rebindTimer` (`Ztcp.hh:963-975`,
`Ztls.hh:2379-2391`).

Each callback should capture only fixed completion data and post to the hub's
configured Rx thread before touching those members.  This is observable only
when the configured hub Rx differs from the multiplexer default, which is why
the current common configuration can conceal it.

### 7. TLS client connect failure runs on the multiplexer shard

The TLS client failure callback calls `connectFailed()` directly
(`ztls/src/Ztls.hh:1471-1479`).  `connectFailed()` mutates the client Rx timer
(`Ztls.hh:1518-1525`).  TCP already posts its corresponding callback to the
configured hub Rx (`ztcp/src/Ztcp.hh:582-589`).

TLS should use the same fixed-data handoff to its configured Rx thread.

### 8. H2 Tx error handling dereferences the Rx stream hash

The H2 wire correctly has separate Rx `m_streams` and Tx `m_txWindows`
containers (`zhttp/src/ZhttpH2Hub.hh:1919-1958`), and most direct-Tx send paths
now use `txWindow_()`.  However, their admission-failure path calls
`streamTxError_()` from asserted Tx functions (`ZhttpH2Hub.hh:1562-1589`), and
`streamTxError_()` calls `h2Stream()`, dereferencing the Rx-owned hash and its
`txErrorFn` (`ZhttpH2Hub.hh:1896-1900`).

The Tx error callback (or a stable handle to an owner-local error route) must
be present in the Tx window.  Tx must not recover it from `m_streams`.

Proposed repair:

1. Add a `ZiTxErrorFn` to the Tx-owned `TxWindowEntry`.  Keep the existing
   callback in the Rx-owned `Stream`; errors can originate on either shard, so
   each owner needs its own callback copy.  Do not put a `Logical` or `Stream`
   handle in the Tx window and do not add a lock or atomic callback wrapper.
2. Make `logicalTxErrorFn()` the Rx-side publication point and assert Rx
   ownership.  If the Rx stream still exists, copy the callback into it and
   post `{id, callback}` to a small Tx helper.  The Tx helper asserts Tx,
   obtains `txWindow_(id)`, and installs the Tx callback copy.  Normal callback
   installation precedes transmission, so FIFO ordering publishes it before
   subsequent Rx-originated frame posts.  Replacing or clearing the callback
   uses the same path.
3. Split `streamTxError_()` into owner-specific selection functions.  The Rx
   variant looks up `Stream::txErrorFn`; the Tx variant looks up
   `TxWindowEntry::txErrorFn`.  Both pass the selected callback to a small
   common helper that constructs the exception, invokes the callback, and
   calls `disconnectNative()` if it returns false.  Add owner assertions to
   these functions and to the `h2Stream()`/`txWindow_()` accessors.
4. Change only the direct-Tx admission failures in `sendFrameDirectTx_()` and
   `sendHeadersDirectTx_()` to use the Tx variant.  Existing Rx-originated
   admission failures continue to use the Rx variant, preserving their
   synchronous behavior.
5. Retain the current teardown sequence.  `removeFramesTx_()` and
   `clearFramesTx_()` already delete Tx windows on Tx, so they naturally
   release the Tx callback copy before finalization without another handoff.
6. Extend the H2 flow/admission harness with strict shard state and
   `maxQueuedFrames(0)`.  Verify that direct-Tx rejection invokes the Tx copy
   without accessing the Rx stream hash, a handled error does not disconnect,
   an unhandled error does disconnect, callback replacement/clearing reaches
   an existing Tx window, and Rx-originated rejection still invokes the Rx
   copy.

This adds one infrequent Rx-to-Tx publication when the callback is configured
or replaced.  The error path remains local to the shard that detects it, and
the stream registry remains exclusively Rx-owned.

## Member-placement and section findings

These are concrete layout/classification defects even where the current access
pattern can be made safe by lifecycle publication.

### 9. TCP and TLS each declare two different reconnect timers

The base link owns and cancels `m_reconnTimer` on Rx
(`Ztcp.hh:435`, `505-508`; `Ztls.hh:1208`, `1328-1330`), while each derived
client declares and arms a second member with the same name
(`Ztcp.hh:593-613`; `Ztls.hh:1518-1541`).  Disconnect therefore cancels the
base timer, not the derived timer that was armed.

This is both an ownership/lifetime defect and a misleading section layout.
There should be one reconnect timer in one Rx-owned section.

### 10. Stable hub and TLS credential members are labelled Rx-exclusive

- `Ztcp::Hub::m_errorFn` is configured before use and cleared during final
  lifecycle handling, but is the first member of an Rx-exclusive section
  (`ztcp/src/Ztcp.hh:875-887`).  It belongs in stable/shared state; the
  lock-guarded `m_links` remains an allowed shared exception.
- `Ztls::Hub` places `m_errorFn`, `m_ctx`, cipher suites, ALPN, and certificate
  verification objects after the aligned `m_eventLoop` in one Rx section
  (`ztls/src/Ztls.hh:2156-2179`).  Most are stable TLS configuration/lifetime
  state, not ordinary Rx protocol state.
- `Ztls::Client::m_key`/`m_sign` (`Ztls.hh:2232-2236`) and the server key,
  signer, and ticket key (`Ztls.hh:2420-2425`) are initialized for the hub
  lifetime and freed during final destruction.  They should precede the
  aligned Rx runtime section; the server's `m_rebindTimer`, drain count, and
  listening flags remain Rx-owned.

`ZiEventLoop` is also not accurately described as simply Rx-exclusive: its
work executes on the configured async thread while Rx coordinates lifecycle.
It should be documented as an exceptional lifecycle/async coordinator, or its
owner-specific state should be split rather than hidden in the Rx section.

### 11. QUIC endpoint classes lack the required physical shard partition

`Endpoint_::Cxn_` has stable/shared `m_endpoint`, `m_generation`, and atomic
`m_closing`, followed immediately by Rx `m_rxBuf` and Tx
`m_txBuf`/`m_txAddr`/`m_txECN`/`m_txQueue`, with no section comments or cache-line
boundaries (`zquic/src/Zquic_.hh:427-437`).  The first Rx and first Tx members
must be separately aligned.

The outer `Endpoint_` likewise leaves stable/shared, Rx lifecycle/rebind state,
telemetry, and advisory snapshots in one undivided block
(`Zquic_.hh:838-862`).  The shared read-mostly `m_cxn` is exempt, but
`m_rebindCxn`, disconnect callbacks, generations, rebind parameters, and socket
diagnostics are Rx-owned and should form an aligned Rx section.  Telemetry and
advisory snapshots should remain explicitly shared.

### 12. Several QUIC members are in the wrong existing section

- `m_rtt` and `m_ptoBackoff` are in the Rx section
  (`ZquicLink.hh:9465-9466`), but their recovery sampling, expiry, and timeout
  use is Tx-owned.  Move them into the existing Tx section.
- Client `m_closeNotify`, `m_closePeer`, and `m_closeFn` are labelled shared
  (`zquic/src/ZquicCliLink.hh:1627-1632`), but all current accesses are in Tx
  posts/callbacks.  Add an aligned Tx section after Rx and move them there.
- Server `m_closePeer` is a plain shared bool (`ZquicSrvLink.hh:1245-1247`):
  Rx sets it on peer close (`ZquicSrvLink.hh:1170`), while Tx reads/clears it
  during close expiry.  This needs an Rx-to-Tx handoff and a Tx-owned member,
  not placement in shared state.
- `Stream::m_txQueueCount` is an exceptional admission atomic used by callers
  and Tx, while `m_txQueueMax` and `m_txErrorFn` are published configuration;
  all three currently sit inside the Tx cache line (`ZquicStream.hh:853-855`).
  The atomic and stable configuration should be classified before the Rx
  section, leaving actual queue/range state in Tx.

### 13. HTTP TLS server-link and top-level server members are not partitioned

`TLS_::SrvLink` mixes stable remote/policy data, Rx connection/logical state,
and H1 Tx completion state in a single unlabelled block
(`zhttp/src/ZhttpServer.hh:1020-1031`).  The H1 fence state is demonstrably Tx
state (`ZhttpServer.hh:770-849`) and needs an aligned Tx section; the Rx logical
and connection state needs its own aligned Rx section after stable members.

The top-level HTTP `Server` has the same problem
(`ZhttpServer.hh:2966-2985`).  Configuration, hub objects, and callbacks are
stable/lifecycle state; response queues and body-task state are operated on Tx;
`Stats`, `m_admitRequests`, and `m_failed` are explicit shared atomics/telemetry.
The block should be reordered as stable/shared first, then any Rx-owned state,
then an aligned Tx section containing the response queues, `m_bodyTasks`, and
`m_bodyPending`.

## Prior findings now resolved or exempt

The following earlier findings should not be repeated as current defects:

- The single read-mostly TCP/TLS `m_cxn` design is intentional and explicitly
  exempt for this audit.  No `m_rxCxn`/`m_txCxn` split is required.
- TLS application headroom no longer reads negotiated Rx state from Tx.  Tx
  uses the stable maximum overhead and receives negotiated headroom through
  the existing handoff into `m_txHeadroom`.
- QUIC stateless-reset cleanup is posted to Tx; the earlier direct Rx mutation
  of Tx queues is no longer present.
- QUIC CID path promotion uses the repaired owner-specific snapshot/candidate
  flow; the stale canonical CID issue from `findings2.md` was not reproduced.
- `SrvLink::m_peerAddr` is now correctly in the aligned Tx section
  (`zquic/src/ZquicSrvLink.hh:1254-1256`), with Rx publication performed through
  `txInvoke()`.
- `m_closedStreamBase` and `m_closedStreams` are now in the Rx section, and the
  closed-stream-ID helpers are used by Rx validation/removal paths.  They do
  not need to be shared merely because Tx has other per-stream state.
- QUIC connection-owned and active-path timers are correctly grouped in the
  Tx section (`ZquicLink.hh:9482-9505`).
- HTTP client request metadata in `ClientMessage` is intentionally stable for
  one bound operation and published before the Tx send post; its Rx parser and
  Tx commit state are correctly separated (`zhttp/src/ZhttpClient.hh:817-839`).
- HTTP `Pool` has stable/shared configuration first, then aligned Rx and Tx
  sections (`ZhttpClient.hh:7088-7145`).  Its result counters are telemetry and
  are exempt.  Request timers are Rx-owned and aligned in `RequestSlot_`.

## Verification status

This was a source audit only.  No source repair or build/test run was performed
as part of producing this report.  The only intended worktree addition is this
file.
