# TCP/TLS endpoints

`ZfixClient.hh` and `ZfixServer.hh` provide `Client<App, Catalog, Net>` and
`Server<App, Catalog, Net>` hubs. `Net` defaults to `Zfix::TCP`; `Zfix::TLS`
selects Ztls at compile time. Hubs expose native transport configuration and
lifecycle. `Client::Params` and `Server::Params` name the corresponding Ztcp or
Ztls parameters, including TLS trust, certificates and mTLS.

Construct hubs with `HubLimits` to configure link defaults once, before links
are created. `HubLimits::link` supplies codec, buffer, read-ahead, output, wire,
control and replay limits. `submit` and `admin` configure the hub's posted-work
quotas; `work` bounds deferred processing leases. `fixLinkLimits()` returns the
immutable link
defaults and is safe to inspect from any thread. These defaults are copied into
each new link. The opaque application ID, mode, `HeaderConfig header`, application
version and session policy remain explicit `SessionConfig` values; transport
pools retain their native compile-time/tuning configuration.

For example, set `limits.link.rx.readAheadCount = 128` and
`limits.link.rx.readAheadBytes = 2 << 20` before passing limits to the hub's base
constructor. This bounds each link to 128 queued frames and 2 MiB of charged
read-ahead; it is not an aggregate retained-byte budget. Standalone `Rx` callers
configure the same values through `RxLimits`. Queuing checks count and bytes
before insertion. The default count is 4,096, an initial small-order workload
choice corresponding to roughly 1 KiB pooled backings within the 4 MiB byte
budget. Count zero disables queued read-ahead while preserving the in-order
fast path. Duplicates do not consume count capacity; processing or gap-fill
purge releases it. Physical reconnect retains accepted read-ahead and its bounds.

Managed Logon tag 383 advertises the effective local
`LinkLimits::rx.codec.frameBytes`. `tx.codec.frameBytes` independently bounds
local output encoding, including control bodies. Setting different directional
limits does not substitute the Tx bound for advertised receive capacity.
Standalone `controlBody`/`controlCommands` callers pass receive frame capacity
explicitly, followed by their optional Tx codec limits.

An application owns session links. Derive its initiator link from
`Zfix::CliLink<Hub, Impl, Catalog, Net>` or acceptor link from
`Zfix::SrvLink<Hub, Impl, Catalog, Net>`. The hub's `LinkBase<Impl>` alias names
that type. Constructors take a hub pointer, `SessionConfig` and optional epoch;
initiators also accept a link ID. Omitting `LinkLimits` inherits the hub defaults.
An explicit `LinkLimits` before the epoch replaces that complete snapshot for
one link; copy `hub.fixLinkLimits()` first to override individual settings. Roles set the ordinary
Logon initiator flag independently of the declared FIX version. Acceptor hubs
supply their native listener/accepted-connection hooks, constructing a link's
`Cxn` with that link and the transport's connection information. A persistent
session owner must be selected by application policy; business correlation
remains application-owned.

These endpoint classes implement the three managed modes and a separate raw
engine selected through the aliases below. Stage 6 transport/lifecycle acceptance
is recorded in STATUS.md. The endpoint catalog supplies default typed receive
dispatch and typed reply. Deferred work admission is implemented; application
policy examples and the Stage 7 gate are complete on the recorded Linux toolchain.

## Typed receive and retained context

Default managed receive uses `dispatch<MessageCatalog>` and calls the selected
`fixReceiveRx_` overload. Qualified MsgTypes share the catalog's structural wire
dictionary; only the selected object is constructed. Unknown MsgTypes, missing
selectors, malformed known fields and missing hook overloads return errors to
the ordinary session Reject path. Control messages retain their existing hooks.
`fixHeaderRx_` uses the same catalog metadata for header-prefix boundaries before
sequencing; unsupported MsgTypes use conservative standalone inspection.

The decoded object exists through the hook. Copy `RequestContext` alongside any
borrowed payload spans that escape it. The context retains one enclosing pooled
message, including frame/header/trailer bytes, without retaining the link or hub.
It can survive connection and hub teardown. Owning decoded containers have their
own lifetimes; retaining context does not retain the callback's object or group
containers. Managed contexts also snapshot the selected descriptor's wire discriminator.
`is<Entry>()` compares MsgType and that selected declaration without reparsing
or retaining a field separately. Standalone dispatch callers still retain their
own frame backing.

`context.context()` exposes a runtime session-owner identity, physical connection,
epoch, inbound sequence, declared version and wire PossDup/replay indication.
The session identity remains stable across physical reconnects on that owner;
it is not a durable restart key. Header sender/target/sub/location fields expose
the current wire headers. The durable application key is `SessionConfig.id`,
independent of this runtime owner incarnation and of the header values.
`replay` reflects PossDup and does not infer business duplication or completion.
`received()` and `utc()` expose the receive timestamps. Published contexts are
read-only snapshots and do not read live Rx/Tx state off shard. Retention is an
application choice and must be bounded by the application's work policy.

## Typed send and selectors

Managed links expose `send<Entry>(const Object &, applicationID, error)` on
AppRx or AppTx. Entry must belong to the link's MessageCatalog. Conversion runs
synchronously with effective Tx codec limits, then the existing submit path
retains only the serialized body across owners. Callers may release their object
when send returns. False reports conversion failure or local handoff refusal;
`ErrorCode::Output` reports wrong-owner or handoff refusal. True means local
handoff only. `fixSubmittedTx_` reports Tx selection (including Held/Busy/refusal),
association precedes publication, and `fixCompleteTx_` remains local completion.
Send does not reserve a sequence, freeze a managed header, correlate a reply or
complete business work. Applications supply queue/retry/completion policy.

A body's emitted secondary value must equal its descriptor. Unknown body keys
are appended from the descriptor in place; known data/length keys retain their
length-aware binary meaning. Selectors inside groups or opaque bytes cannot
stand in for a root key. Empty values remain explicit. Descriptor mismatch,
recognized duplicates, invalid text and codec bounds refuse conversion. Archive
restore checks the selected key and preserves exact stored bytes, including an
unmodelled key; it never adds a missing key to history. Store concrete entry/facet
identity with application history.

Header/trailer selectors are checked against the completed current Tx frame,
before transport publication. These checks cannot be promised at submission:
sequence, timestamps and managed identity come from Tx at transmission. Their
failure follows the ordinary Tx encoding-error path. Unqualified bodies avoid
selector scanning; body-qualified output avoids rescanning a completed frame.
A nullable static validation function on retained bodies carries envelope-key
policy through history/replay without retaining caller data or a dynamic schema.

Raw links expose `send<Entry>(object, applicationID, header, TxContext, admin,
error)`. The external manager supplies all header/session/classification values.
Conversion and framing finish before handoff; `fixRawSubmittedTx_` and the raw
queue/writable hooks govern refusal/backpressure. Raw mode supplies no managed
sequencing or recovery policy. RawOutput::fromFrame also checks an attached
body's envelope selector against the supplied complete frame.

## Typed reply and application policy

Managed links expose `reply<Request, Response>(context, object, applicationID,
error)` on AppRx. Both descriptors must belong to MessageCatalog and Response
must occur in Request::Responses. The context must match the received MsgType
and selected discriminator declaration (`context.is<Request>()`). Empty or
mismatched contexts return Unresolved tag 35 before policy or conversion; calls
outside AppRx return Output. Use the Request descriptor supplied by typed receive.

The application supplies `fixReplyRx_(Request *, Response *, const
RequestContext &) -> Fmt::Error`. A missing overload refuses with Unresolved tag
35. The hook decides session/epoch/routing, replay and business-key policy;
its error returns unchanged. It may authorize retained contexts across physical
reconnect, epoch change or owner replacement under explicit application policy.
The library does not compare those values or invent business completion rules.
An authorized reply follows typed send's conversion, limits and handoff path.

Reply never consumes or invalidates context. The application may send multiple
responses, in any order, and decides which are terminal. It supplies business
keys and outgoing application identity. No incoming sequence/header is copied
into managed output: Tx assigns the current outbound header and sequence through
the existing association/publication path. Local refusal/completion remains
separate from peer progress and application completion. Retained context owns
its message bytes; it does not keep the endpoint alive.

Raw links expose `reply<Request, Response>(input, object, applicationID, header,
TxContext, admin, error)` on AppRx, where input is the retained RawInput. They
check input presence/MsgType and the same compile-time response relationships,
then call `fixReplyRx_(Request *, Response *, const RawInput &)`. The external
manager validates/discriminates its input, authorizes response policy and supplies
all outgoing header/session/classification values. Missing raw policy overloads
refuse locally. Raw reply supplies no managed session or recovery semantics.

## Deferred application work

On AppRx, managed links expose `work<Entry>(context, error, extraBytes = 0)`.
Entry must belong to MessageCatalog and match context's wire selection. The link
must be live, Established and outside draining. Raw links expose
`work(retainedRawInput, error, extraBytes = 0)` while live. Both return a retained
`Work::Ref`, or an empty reference with Output for phase/owner refusal, Unresolved
for a managed descriptor mismatch, or Size for allocation/accounting/quota refusal.

HubLimits::work configures aggregate work limits; HubLimits::link.work supplies
per-link defaults. WorkLimits defaults to 4 MiB and 4,096 admitted operations.
Both count and bytes must fit before work allocation. A zero limit disables
admission. AppRx can inspect `link.workState_()` and `hub.fixWorkRx_().state_()`;
`link.workBudget_().limits_(limits)` and the hub ledger setter permit live changes
that do not undercut outstanding count/bytes. Hubs sharing one AppRx may override
fixWorkRx_ to return a shared ledger kept alive until all links drain; separate
Rx owners require separate ledgers. Snapshots report current usage,
byte/count peaks and quota refusals.

Each admission retains one enclosing RequestContext or raw input in an identified
Zfix.Work heap object. Native input snapshots its actual originating allocation:
rounded declared Rx pool object, jumbo allocation/prefix when present, and the
rounded enclosing input node. This stays correct when a context is later routed
to another link with different pool geometry. Manually constructed contexts
without native allocation accounting cannot use this factory. The reservation
also covers the work object, queued retirement delegate and extraBytes. Shared
backing is conservatively charged per separate admission; copies of one work
reference keep the same reservation.

Use `work->input()` for the retained context/input. Move/copy the exact Work::Ref
through fixed-metadata callbacks, for example a lambda capturing `[work]`.
Borrowed payload spans remain valid through that lease. Decoded owning containers
have separate lifetimes; extraBytes must cover additional retained allocations
and worker/result callback envelopes. The application must release additional
resources before its last work reference. The work reference is processing
admission, not business correlation or a completion/cancellation claim.

Last-reference release on any owner posts only a work handle to AppRx. Rx deletes
the work/input before returning link/hub credits. An in-progress retirement
callable and idle allocator caches are separate fixed execution/cache overhead.
Work admission never acquires another shard's live state or adds locks. Synchronous
typed processing remains bounded by codec/ingress limits; typed send/reply handoffs
remain bounded by their retained-body post and Tx queue budgets. Application-owned
unsequenced queues and long-lived business registries require their own bounds.

Physical shutdown refuses new work and waits for active leases and queued
retirements before the stopped observer or link release. Applications cancel or
finish their deferred work through the existing stop hooks; no business result is
invented. A plain retained context may survive after its processing lease ends.
Managed drain stops new work and withholds its Rx drain proof until work retires,
then retries protocol synchronization. Ordinary work does not pause session
readiness/recovery. Reset preparation also checks that no deferred work remains.

Application policy examples and the remaining Stage 7 gate are still open.

## Persistent links and connection replacement

The application selects a persistent logical link by its opaque `SessionConfig.id`.
Managed hubs contain no attachment registry or duplicate-owner claim mechanism.
At accept/bind, create a new physical `Cxn` for the selected existing link.
Routing and authentication remain application policy; configuration does not
synthesize an identity from headers, FIX version, or transport mode.

A new associated connection displaces its predecessor immediately. Use the
`ZiMultiplex::close` path for abrupt close; `disconnect` performs graceful
disconnect with drain and is not the displacement operation. Queued logical
work belongs to the successor and waits only for its normal readiness, not
predecessor completion. Stale connection callbacks retire their own resources
and accounting without cleaning successor state. Partial wire output and TLS
records cannot migrate to the successor; sequence assignments survive and FIX
recovery obtains resend content from the application.

Terminal owner shutdown separately drains outstanding obligations before
releasing the persistent link. An obsolete physical close must not trigger
whole-link retirement. Implementation and verification of replacement remain
in progress under `../zfix_repair2.md`; prior STATUS results do not prove this
new contract.

Deferred admission allocates one identified pooled reply record only when
`request.defer()` is called; synchronous decisions allocate none. The reply
retains its owner and retires its outstanding obligation once on Rx. Repeated
decisions cannot admit a displaced connection or decrement the count twice.
Applications must complete and release deferred callbacks while the hub remains
running, including during terminal shutdown. Each deferred request is a distinct
obligation: retain or complete every overlapping callback instead of overwriting
a single callback slot when a new connection arrives. Completed callback handles
can still retain the owner until released. Displacement does not wait for
these callbacks; final owner stop waits for outstanding obligations.

## Owners and callbacks

`Link<Impl>` composes the existing `Rx`, `LinkTx`, incremental `RxFramer`
and `WireTx` implementations. Transport connection callbacks open the Rx
session. Fixed lifecycle events and immutable control bodies are posted to Tx;
publication notices are queued back to Rx before any frame reaches transport.
TCP borrows complete frame backing, while TLS produces tagged record buffers.
The configurable receive/output allocators preserve the underlying pools. A
custom output allocator must derive its node from `WireBuf`.

`Link` is the transport-independent managed owner. Native `CliLink` and `SrvLink`
compose that owner directly with the TCP/TLS link, stream input adapter and
internal stateless `LinkHooks_` mixin. Native posted/work credits and the
borrowed stream pointer reside in the selected engine's Rx group through
`NativeRx<Net>`; the adapter bases contain no data after the engine's Tx group.
The mixin supplies pooled transport allocation,
posted-submission quotas, Work leases and catalog-aware tracing to both managed
and raw endpoints. `Raw` remains a separate engine with no managed session,
sequencer or FIX timer. This shared hook implementation avoids
duplicating its resource accounting; it does not wrap or inherit the engine.

Application hooks have these owners:

| Hook | Owner and responsibility |
| --- | --- |
| `fixAdmissionRx_(event, msg, request)` | Rx. Return `Admission::Accepted` or `Refused` synchronously, or retain `request.defer()` and return `Pending`. Invoke the retained `AdmitFn` with the decision on any shard, then release it. Never retain the stack `AdmitRequest`. The retained input permits application-specific authentication decoding. |
| `fixLogonTx_(body)` | Tx. Prepare an owned administrative Logon body, preserving the managed fields. A null/wrong-type/non-administrative result refuses preparation and closes through the session failure path. |
| `fixHeaderRx_(frame, header, limits)` | Rx. Read headers with application binary/schema metadata where needed. The default uses `catalogHeader<MessageCatalog>` with the catalog's binary/group scope boundaries. |
| `fixReplyRx_(Request *, Response *, const RequestContext &)` | Rx. Authorize managed typed reply using application session/epoch/replay policy. Return `Fmt::Error`; a missing overload refuses locally. |
| `fixReceiveRx_(Entry *, const Object &, const RequestContext &)` | Rx. Called by default catalog dispatch after selected-type validation. Return `Fmt::Error` for refusal. Missing overloads reject with Unresolved tag 35. |
| `fixBusinessRx_(msg, limits)` | Rx. Default calls catalog dispatch and `fixReceiveRx_`. An override supplies its own checked decoding and lifetime contract. |
| `fixAssociateTx_(body, context, ticket)` | Tx. Commit application mapping before acceptance, or return `TxLoad::Pending` and later call `loaded(ticket, bool)`. The default accepts controls and refuses business. |
| `fixReleaseTx_(body, context, ticket)` | Tx. Unload an unpublished association; return false for a later `unloaded(ticket)` completion. |
| `fixRetrieveTx_(context, acknowledged, ticket)` | Tx. Always supply replay content from application storage, return no body to gap-fill, or defer through `reloaded(ticket, history)`. The app may return replacement content/type/ID. |
| `fixArchiveTx_(epoch, begin, end)` | Tx. Generic archive has completed the half-open sequence range; the app may move/delete its own records. No core payloads or release handshake. |
| `fixEnqueueTx_`, `fixDequeueTx_`, `fixSelectedTx_` | Tx. Optional application-owned queue/selection policy, unchanged from `LinkTx`. `queued()` wakes selection. |
| `fixSubmittedTx_(body, result)` | Tx. Observe Tx selection for a locally admitted `submit(body)`. Handle refusal/busy/pending according to application policy. `submit_(body)` returns that result directly on Tx. |
| `fixPublishedTx_(body, context, originalTime)` | Tx. Observe transport-admitted original/replayed frames before bytes are exposed; save immutable body/context and original SendingTime in application history. The callback does not impose a storage or commit policy. |
| `fixCompleteTx_(body, context, ok)` | Tx. One local completion per transport-admitted frame, including replay/failure. This does not acknowledge the peer or complete business work. |
| `fixRejectedTx_`, `fixAckdTx_`, `fixLimitTx_` | Tx. Peer Reject association routing, exclusive acknowledgement progress and approaching-limit policy. |
| `fixStateRx_`, `fixEventRx_`, `fixControlRx_`, `fixControlTx_` | Their named owner. State changes, session events and ordered inbound/admitted outbound controls. |
| `fixStopRx_(connection, admissionTicket)` | Rx. Cancel/finish deferred work and any pending application admission decision before destruction. A nonzero ticket requires its matching `admit` continuation. |
| `fixStopTx_(connection)` | Tx. Cancel/complete pending application load, unload or history work using its original ticket. |
| `fixStoppedRx_(peer)` | Rx. Both physical I/O and managed owner work have drained. The application may now release the link and proceed to hub shutdown. |

Publication observers run after admission and publication-notice enqueue, before
flush. The core publication cursor advances when the frame callback returns;
use the supplied context instead of reading an advanced cursor inside this hook.
Post further owner work rather than reentering the sender from an observer.

Native mapping/storage hooks may post retained handles and fixed context to
another application owner. Completion dispatchers return only to their owning
shard. Tickets are one-shot; application work must not issue continuations after
it has completed/cancelled that ticket. `snapshotRx_`, `snapshotTx_`,
`restoreRx_`, `restoreTx_`, `rxProgress_` and `txProgress_` are owner-only. An
application coordinates its own snapshot barrier and store; restore precedes
physical open. Epoch observer hooks retain the core reset protocol's ownership.

## Admission, timers and shutdown

One blocked complete input remains in the Rx owner. After the admission
continuation, the bridge retries it and revisits the borrowed ZiRxStream to
consume any remaining coalesced input using its existing extraction operations.
The adapter clears the borrowed stream reference on physical disconnect. Framing returns validated views over the
extracted buffer without repeating the checksum traversal.

The six session deadlines and the gap retry each have a dedicated Rx timer.
The idle heartbeat timer belongs to Tx. Scheduler deadlines use the scheduler's
absolute clock. Later receive deadlines reuse the earlier physical wakeup and
re-arm when it fires; cancellation and serial/context guards reject stale work.

Call `logout()` for protocol shutdown or the native transport's `disconnect()`
for physical failure/stop. The bridge cancels Rx timers, stops ingress, posts
ordered session close events to Tx and closes output only after the transport
has drained its physical I/O queue. Tx cancellation/drain waits for pending
mapping/history continuations and output completions. A final owner barrier
then delivers `fixStoppedRx_`, waiting separately for pending admission work.
One teardown reference retains the link across these phases; buffer/task
back-pointers remain raw. Keep the hub, multiplex and application owners alive
until this callback. Hub `stop()` alone does not own or drain application session
links. Reconnect is permitted after managed stop completes, preserving the same
session owners and their continuing cursors.

`ZfixLinkStateIOTest.cc` is the current concrete application example and acceptance
fixture. See [integration tests](itest/README.md) and [status](STATUS.md) for the
covered cases and remaining stage 6 work.

## Externally managed raw links

Use `RawClient<App, Catalog, Net>` / `RawServer<App, Catalog, Net>` with
`RawCliLink<Hub, Impl, Catalog, Net>` / `RawSrvLink<Hub, Impl, Catalog, Net>`.
Their `LinkBase<Impl>` aliases also select raw links. Set `SessionConfig::mode`
to `Transport::Raw`. This compile-time composition contains framing and `WireTx`,
with no managed session controller, sequence queues or FIX timers. The external
manager owns header validation, controls, admission/authentication, dictionary
selection, sequencing, storage, recovery and readiness. Transport generations
still distinguish physical connections; output requires the current generation.
The constructor's epoch argument has no raw policy effect.

`TxBody::encodeRaw<Entry>(object, id, administrative, error, limits)` and
`restoreRaw<Entry>(bytes, id, administrative, error, limits)` allow external
schemas for any MsgType, including built-in controls. The administrative flag
selects bounded reserved output capacity; the external manager supplies it.
`RawOutput::encode(body, header, context, error, limits)` produces a complete
pooled frame from supplied standalone headers. `RawOutput::fromFrame` retains
already-framed pooled output after validating exactly one envelope, checksum
and matching MsgType. It imposes no managed header requiredness and does not
compare body bytes or tracking context against the frame. Callers keep supplied
body/context/frame metadata consistent and treat shared buffers as immutable.
Neither path assigns sequence/epoch values. Zero epoch/sequence tracking metadata
is allowed; no raw sequence advances on admission. The encoder still owns
BodyLength and CheckSum.

| Raw hook / entry point | Owner and responsibility |
| --- | --- |
| `fixRawUpRx_(connection)` | Rx. Physical output is available; notify the external manager. |
| `fixRawRx_(input, limits)` | Rx. Receive a retained `RawInput` with pooled bytes, validated envelope view, connection and receive time. Return true to consume; false pauses on that exact input. Retain this one enclosing handle for borrowed values that escape the callback. |
| `resumeRaw(connection)` / `resumeRaw_(connection)` | Dispatcher / Rx. Retry paused input and wake coalesced transport input; stale generations are inert. |
| `sendRaw(output)` / `sendRaw_(output)` | AppRx/Tx dispatcher / Tx. Submit retained complete output. The owner-only form returns `RawSend::Accepted`, `Held`, `Invalid` or `Overload`; refusals publish nothing. |
| `fixRawSubmittedTx_(output, result)` | Tx. Observe Tx selection after successful handoff; retain/retry refused work under application policy. |
| `fixRawPublishedTx_(output)` | Tx. Admission succeeded, before flush. Observe supplied metadata without reentering the sender. |
| `fixRawCompleteTx_(body, context, ok)` | Tx. Exactly one local result per admitted frame, including close/failure. This does not acknowledge a peer. |
| `fixRawWritableTx_(connection)` | Tx. Output opens or capacity returns; retry application-owned queued output. |
| `fixRawFaultRx_(connection, error)` / `fixRawFaultTx_(connection, error)` | Named owner. Observe framing/limits or output failure. |
| `fixRawStopRx_(connection, request)` / `fixRawStopTx_(connection, request)` | Named owner. Finish external work or retain `request.defer()` and invoke that one-shot callback after it drains. Returning without deferral completes synchronously and allocates nothing. Replies settle their original aggregate obligation even after replacement. |
| `fixRawStoppedRx_(connection, peer)` | Rx. Physical I/O, all admitted output completions, both external stop continuations and final owner barriers have drained. Release the link or reconnect after this callback. |

The default receive hook pauses, and default stop hooks finish immediately.
`rawConnection_()` is Rx-only; `rawOutputBytes_()` and `rawInFlight_()` are
Tx-only. Framing/retained input and complete output use the same configured
bounds and pooled allocators as managed transport. Keep hubs/multiplex alive
until raw stop completes; external hooks must finish/cancel their posts before
reporting drain. `ZfixRawIOTest.cc` demonstrates a separate application owner,
paused input, external controls/headers, binary records and asynchronous stop.


## Startup failure and listener restart

Initialization failure leaves the native hub stopped. Call `final()` to release
partial initialization before correcting parameters and initializing the same
hub again; the FIX wrapper adds no rollback. Keep the multiplex running while
finalizing or retrying hubs. Native TCP/TLS validation rejects a missing/stopped
multiplex, invalid owner IDs, or one thread selected for both owners. TLS trust,
certificate and private-key failures remain Ztls errors and configuration.

A server stop removes its actual bound listener address, including an ephemeral
port requested with zero. Native listener success that arrives after stopping
has begun closes the just-created listener and drains an I/O barrier before
accepted links are stopped; late failure/retry callbacks cannot
reopen it or notify the stopped application. Start the stopped hub again to bind
another listener, or explicitly finalize/reinitialize it to change configuration.
The server owns its listener; an application still waits for each session's
stopped observer before destroying its links or finalizing hub contexts.

Native setup uses ordinary DNS/connect continuations with no cancellation or
protocol-imposed setup deadline. Stop prevents additional submissions and drains
submitted work; a late successful connection is closed normally. A failed setup
does not open the FIX Session, claim identity or assign a sequence. Its
`fixStoppedRx_` callback follows the usual owner drain (connection zero until the
first ready transport). `connectRetired_` starts this drain before the application
failure observer, including overrides of `connectFailed`. Applications wait for
stopped before retry/release and keep hubs and the multiplex alive through it.

Raw pre-readiness shutdown also drains the external manager's Rx/Tx stop
hooks. Their connection argument is zero until a transport becomes ready. A
repeated failed setup must wait for both stop acknowledgements again; a previous
attempt's completed drain cannot satisfy the next one. No raw frame, managed FIX
timer or wire sequence is introduced by these attempts.

`reset(ResetRequest)` posts a fixed connection/epoch command to Rx. Its Rx-only
`reset_(request)` counterpart returns admission directly. The dispatcher calls
`fixResetRx_(request, accepted)` after local admission; this result does not prove
that drain, turnover, transport or peer agreement has completed. Existing
`fixEventRx_` Drained/ResetReady events and epoch hooks report those transitions.
Keep the link alive through command observation and the normal stopped barrier.

`ResetOp::Drain` pauses business selection and proves the two-way drain;
`Prepare` supplies a strictly newer epoch, the agreed driver and ResetPath.
Both peers must prepare before `Start` releases live reset Logon processing.
Unsequenced application queue entries may remain queued across the turnover.
`Disconnect` releases an already prepared reconnect reset. The application
coordinates both sides before releasing transport; the library supplies no
counterparty calendar or coordination channel. All live commands require the
captured current connection and epoch, including after a physical reconnect.
`OnOpen` authorizes/disables an explicitly agreed initial reset only on a never
opened owner with matching epoch and connection zero. Native pre-readiness
failure does not consume that LinkState::open_ authorization. These APIs are
managed-only; Raw managers retain their own reset policy.

After both owners report `ResetReady`, the new epoch is committed even if the
subsequent reset Logon handshake fails. Physical disconnect, Logon timeout or
outbound Logon preparation refusal does not roll it back. Same-ID reconnect
continues that epoch with its retained sequence progress and normal recovery;
it does not implicitly authorize another reset. An already published first
reset Logon keeps its sequence assignment through reconnect. Old connection
commands remain stale. Application policy can retain bounded unsequenced work
until this continuing session becomes ready.

Epoch turnover cancels all old Rx timers before the new session event arms its
deadlines. Timer callbacks capture their epoch, so retaining an earlier timer
from the old epoch would discard its callback and lose the new deadline. The
usual earlier-deadline reuse remains available within one epoch.

`submit(body)` and `sendRaw(output)` return a boolean for local handoff admission.
They accept a const retained handle; neither moves from the caller. False means
no post, selection observer, sequence assignment or publication. Calls must run
on the link's AppRx or AppTx owner. Other threads return false and must use their
own bounded handoff to an application owner. On AppTx, the selection observer
runs inline after the owner-only operation; its result is distinct from the
boolean. On AppRx, a live link reserves its per-link `postedSubmitBytes` and the
hub's `HubLimits::submit` quota before posting to Tx. Both default to four
frame limits (4 MiB). Null handles and non-live Rx submissions return false.
The application owns retain/retry/refuse policy.

`fixSubmitRx_()` exposes the AppRx-owned hub ledger; `txPosted_()` exposes the
link snapshot on AppRx. Live tuning cannot undercut outstanding reservations.
A shared domain requires the same AppRx owner and a stable binding until drain.
Managed charges cover the rounded pool object and jumbo heap allocation,
including its prefix/cache class.
Raw charges additionally cover RawOutput, the frame's exposed capacity and its
ZiIOBuf base; opaque custom frame allocator overhead is outside that charge.
Fixed callback overhead is bounded by the admitted handle count. Repeated posts
of the same handle each consume a full reservation. Protocol control traffic
continues through its separate bounded control/output queues.

Credits return to AppRx after Tx selection and its observer, including held or
refused selections. Pending returns remain reserved. Close prevents new AppRx
handoffs and delays the final stopped observer until all submission credits
return; no per-post link reference is added. This quota covers retained bodies
waiting for Tx selection, separately from Tx output/replay limits. Application
retention after selection is the application's responsibility.

Native receive buffering uses the existing Ztcp/Ztls and ZiRxStream facilities.
FIX framing validates BodyLength and codec size limits. Zfix adds no native
receive quota, posted-receive credit ledger or transport admission hook.

## Application control and connection ownership

Applications own link population and admission in the native `accepted()`
factory, which runs on network Rx and can return null before a Zfix connection
is created. Initiators create their own links. Native TCP/TLS permit one active
setup attempt per link, drain it through its completion continuation, and gate
logical open until readiness. Native connection, hostname and reconnect
commands use the application's scheduling policy and lifetime contract.

The fixed control dispatchers `admit`, `loaded`, `unloaded`, `queued`,
`logout` and `reset` transfer control values to their owner. The existing
one-shot ticket contract bounds valid completion traffic by outstanding hooks;
application queues and controllers bound their notifications and command issue.
Applications observe/cancel this work before releasing the link. These methods
use native scheduler queues, whose heap overflow is not a hard admission bound.
`HubLimits` covers the documented untrusted ingress, retained payload handoffs,
protocol administrative commands and output/replay work. Application-owned
factories, policy commands, queues and history retain application admission.

## Deferred history completion

`reloaded(ticket, const TxHistory &)` returns local handoff admission and is
`[[nodiscard]]`. Call it after the retrieval hook has returned. On AppTx it
completes directly; on AppRx it reserves the same per-link/hub administrative
budget used by controls before posting the retained history body and fixed
metadata to Tx. Each response, including a repeated handle or stale ticket,
consumes its own full allocation/envelope reservation. Null/unavailable history
still consumes the fixed envelopes. Tx validates the ticket and epoch after
admitted delivery; `true` does not report that validation or replay completion.

Calls from other threads, AppRx calls after the stopped barrier, and calls
without administrative capacity return `false` without consuming the caller's
history, posting work or faulting the session. An external storage owner must
make its bounded return hop to an application owner. The application retains
refused history and retries when capacity returns, or delivers/cancels the
pending lookup directly on Tx. In particular, administrative overload may refuse
Rx admission during close; complete outstanding lookups on Tx so stop can drain.
Synchronous retrieval uses the hook's `TxReload` return rather than recursively
calling this completion. Application history retention and the storage-owner
return hop remain application-owned.

Accepted Rx responses hold their reservation through Tx consumption and the
ordered Rx credit return. Close waits for these credits along with controls and
outstanding retrieval work. Direct Tx delivery adds no scheduler work and
remains available for draining/cancellation independently of Rx quotas.

## Administrative owner handoffs

`HubLimits::admin` bounds the hub aggregate managed Rx-to-Tx administrative
handoffs; `HubLimits::link.postedAdminBytes` bounds each session. Both default to
4 MiB and are independent of business submission quotas. They include lifecycle
events without payload, encoded controls and generated Rejects, replay requests,
peer Reject reports retaining received wire backing, and admitted deferred
history responses. Each charge includes
rounded callable/delegate envelopes in both directions and the retained body or
native receive allocation, including its known jumbo prefix/cache class. Fixed
metadata therefore consumes quota even when no body crosses owners. Ring
capacity, idle caches, and retention by external observers remain separate.

Admission precedes posting to Tx. Bodies are encoded on Rx into bounded pooled
storage before admission; a refused body stays out of the destination queue.
Credits remain reserved through Tx processing and the return queued to Rx. Tx
control/output admission then applies its independent limits; the control queue
charges the same body allocation plus its rounded command node. A full
business submission budget cannot consume the administrative reservation.
`adminPosted_()` reports a fixed per-link snapshot on AppRx;
`fixAdminRx_().state_()` reports the hub aggregate on AppRx. As with the other
posted ledgers, live hub tuning refuses a limit below outstanding reservations.

Exhaustion during a live transition latches one managed Overload and forces
physical close. If a terminal transition already occurred, its original failure
and disconnected observers remain delivered. The failed session stops framing further coalesced input and stops posting administrative
work; its failure cannot recursively queue another command. Physical teardown
closes the Tx coordinator independently of lifecycle delivery, drains admitted
work and application continuations, and waits for all pending credit returns
before the final stopped observer. Other sessions retain their own reservations
and continue when capacity returns. Raw endpoints have no managed administrative
handoffs. Application-owned history/call queues keep their application bounds.

## Protocol and queue diagnostics

`rxStats_()` and `txStats_()` return fixed value snapshots on AppRx and AppTx
respectively, in managed and raw endpoints. Copy snapshots to a reporting owner;
never read another owner's live counters. Cumulative counters span connection
and epoch turnover. Current queue fields are sampled on their owning thread.
They do not restore session progress or provide business completion.

Rx frames/bytes count complete framed input once, including input paused across
admission/resume. Framing errors are counted separately. Managed Rx additionally
counts generated/received Rejects, gap requests (including retries), peer replay
requests, duplicates, ignored input, decode errors, business-hook errors and
session faults/overloads. Raw counts its framing and external-manager fault
notifications; it invents no managed sequencing statistics. `queuedCount` and
`queuedBytes` describe managed read-ahead. `pendingBytes` describes retained
admission/paused input backing, independently of read-ahead.

Tx snapshots contain current operational/control occupancy and outstanding local
completions, plus association refusals, application enqueue refusals, encoding
errors, frame-admission refusals and reported faults. Pending asynchronous hooks
are not failures. Invalid/stale continuations do not increment refusal counts.
Wire counters distinguish admission, publication, successful local completion
and failed/closed completion. Bytes count complete frames at publication, before
TLS splitting; they are not socket-write bytes or peer acknowledgements. Replay
counts include gap fills. Closing an admitted, unpublished frame increments
failed completion without incrementing publication. Duplicate/stale socket
completion callbacks remain inert. Wire queue bytes/count and peak charged bytes
are separate from the operational queue. Raw snapshots leave managed history/control
fields zero and use its external context's replay flag as supplied.

## Opt-in bounded wire traces

Set `HubLimits::link.traceRx` and/or `traceTx` before constructing links. Their
`bytes` default is zero (disabled); `recordBytes` defaults to 4 KiB of escaped
diagnostic text. Each enabled owner rotates oldest records to keep charged
storage within its byte budget. Charges include rounded pooled node/buffer
objects, builtin storage and separate jumbo capacity; allocator bookkeeping and
external observer retention are outside this budget. An individually oversized
record allocation is dropped before allocating. Capture never changes protocol
admission or publication results. Native pool configuration is unchanged.

`fixTraceRx_(TraceRef)` / `fixTraceTx_(TraceRef)` observe owned immutable records
on the named application owner. One record owns only escaped/redacted text,
fixed connection/epoch/sequence context and original wire length; it retains no
link, original frame or borrowed string. Raw Rx context supplies its connection
and zero epoch/sequence, while raw Tx retains the external manager's metadata.
Records may outlive queue eviction and link/hub destruction. Applications must
bound their own retained observer references and any output/storage queue.
`rxTraceStats_()` / `txTraceStats_()` expose cumulative records, rotations,
drops and truncations plus current charged bytes/count on the named owner.

Capture covers complete FIX frames: Rx after envelope validation and Tx before
transport record splitting/encryption. No TLS handshake material is captured.
Values escape control bytes, quotes and backslashes as hexadecimal escapes;
there are no raw line breaks or control bytes in a record. The representation
is diagnostic text and is not a frame for retransmission. Excerpts may end at
the configured bound; consult the record's `truncated` flag.

Logon values after the envelope are redacted. Sensitive standard tags (89, 91,
96, 553, 554, 925, 1402 and 1404) and all binary values are also redacted.
After a sensitive field, the remaining suffix is opaque, including malformed
credential text that might otherwise resemble later fields. Application-specific
sensitive fields can be added by `fixTraceSecret_(msgType, tag)`; applications
must identify custom authentication fields in their dictionaries. This hook can
add redaction and cannot override mandatory redaction.

The renderer reuses codec tag and length/data metadata from the endpoint's
catalog, built-in controls, header and trailer. Unknown fields, orphan binary
data, conflicting binary interpretations across group scopes and malformed
length/data boundaries stop rendering with an opaque/malformed marker; their
remaining bytes are never copied to the record. This diagnostic policy leaves
normal receive-side unknown-tag handling unchanged. Standalone callers can use
`WireTrace`, `traceField<Object, Facet>()` and a metadata resolver directly.
Default capture does no parsing, allocation or observer calls while disabled.

## Application-directed endpoint failover

After the managed stopped callback, an initiator can call `connect` with a new
endpoint while retaining the same logical session owner. Endpoint selection,
retry/failover schedules and durability remain application policy. A failed
TCP/setup attempt does not open a new session connection, allocate a FIX
sequence or imply a reset. Successful reconnection continues the existing
epoch/cursors and recovery rules.

A standby acceptor can restore its application-owned Rx/Tx snapshots before
publishing its link through an already-live listener's network-Rx routing.
Select the application ID and restore both owners before exposing that route.
Cold reconstruction after owner shutdown is distinct from replacing a physical
connection on the same live persistent link. Retained unsequenced application work
can be released at recovered readiness; header/sequence assignment still occurs
on Tx. Late old connection/load/wire callbacks cannot dispose current work.
The recovery integration fixtures demonstrate warm-store and file-restored
handoff over both transports in every managed mode.

## App-owned replay and archive

The repaired managed path never retains replay/resend history. Every replay key
is requested from the app even if original output is still in flight. Results
serve one attempt; refusal requires a fresh app request. Only operational
new-send records and current physical output retain core bodies.
An ordinary refused publication may retry its installed operational node; it is
not another replay attempt. A priority Logon already admitted out of order keeps
only its catch-up marker until the contiguous send cursor consumes it. Neither
that marker nor in-flight Wire output can supply a replay result.
`abortTx_(epoch,seq)` is Tx-owned and held-only, preserving the assigned number.
`probe()` posts to Rx to request an optional session watermark; ordinary Ready
does not depend on that probe. Explicit drain/reset retains its fresh proof.
`Checkpoint` restores sequence metadata, not another process's queue byte debt.
Its owned `id` is the application's session key. `stateMatches`/`stampState`
compare only ID and epoch, permitting updated `HeaderConfig` values on a new
connection with the same persistent ID. Wire headers are still checked against
that connection's current configuration.
`fixArchiveTx_` ranges let the app move records to long-term storage or delete
them without another core completion. Later retrieval may be asynchronous.
Warm hold/reconnect preserves unfinished archive chunks without repeating
already reported ranges. Cold restore initializes from the acknowledged cursor
and emits no retrospective archive events. Already-assigned ordinary catch-up
can run before Ready to advance a pending replay prefix; new unsequenced
business admission still waits for actual directional recovery.

