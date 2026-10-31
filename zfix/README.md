# Zfix

Zfix follows [the implementation plan](../zfix_plan.md)
and [resolved decisions Q001–Q018](../zfix.md). The earlier Stage 1–9 results describe the pre-repair snapshot. The current
source implementation and consumer migration of the breaking repair in
[zfix_repair.md](../zfix_repair.md) and persistent-link replacement in
[zfix_repair2.md](../zfix_repair2.md) are complete. Current-build verification passes
with the existing configuration and compatible dependencies. See
[STATUS.md](STATUS.md) for checks, repaired failures and verification scope
and [CONTRACTS.md](CONTRACTS.md) for current source contracts.
The [state audit](STATE_AUDIT.md) records member ownership, retained-state
rationale and historical link footprints. The [enum audit](ENUM_AUDIT.md)
records closed configuration domains and intentionally raw wire strings.

`libZfixFmt` contains standalone conversion and scanner code and depends only
on the foundation through Zf. `libZfix` contains the control messages, owner-shard
sequence queues, Rx lifecycle and Tx association/framing/replay, and links Ztcp/Ztls. Shared managed TCP/TLS endpoints now compose those owners; raw endpoints select
a separate framing/output engine for an external session manager; earlier transport/lifecycle gates are recorded in STATUS.md and default catalog-based
typed receive is implemented. Typed send/reply, bounded deferred processing and [native application examples](example/README.md) are implemented. Application dictionaries, authentication, business
correlation, archive availability and
durability policy belong to the application.

Build with `make -C zfix -j8` using the existing
compatible foundation and transport libraries; do not rebuild dependencies.
Build each directory's `all` before `make test`. The module traverses `src`,
`test`, `util`, `itest`, `example`, `interop`, `bench` in dependency order.
[Native workload tools](util/README.md) provide separate TLS-default `zfix` and
`zfixd` processes with count/duration/rate/session controls and JSON results.
QuickFIX is an optional fixture dependency confined to `interop` and `bench`;
earlier independent codec and all four live TCP/TLS peer pairings passed canonical,
fault, security, duplicate-session and checkpoint/restart/reset cases. See
[interop/README.md](interop/README.md) for exact scope and reproducible commands.

The source directories have separate README files describing their roles.
Installed headers use `<zlib/Zfix*.hh>`. `Zfix::Fmt` does not include session or
network headers. ADL `Zfix_Fmt(T *)` selects object/array/union/string handling;
`Zfix_Config(Facet *)` selects facet configuration and `Zfix_StringFmt(T *)`
supplies checked custom scalar handling. Managed and raw endpoint hubs/links are described in [ENDPOINTS.md](ENDPOINTS.md). The shared `RxFramer` adapter extracts
one complete pooled frame from a TCP/TLS `ZiRxStream`. Retain that frame across
application admission pauses and resume extraction after consumption; reset the
adapter together with its physical stream on disconnect. Actual TCP/TLS loopback
fixtures are documented in [itest/README.md](itest/README.md). `WireTx` admits
complete frames before publication, borrows TCP frame backing without a byte
copy, and splits TLS output into tagged records. Its completion hook posts back
to `LinkTx::sent_`, followed by `writable_` on success; local completion never
advances peer acknowledgement. Close the adapter after physical I/O Tx drains,
then drain its posted completions before releasing the enclosing owner.

## Catalog decoding

`ZfixDispatch.hh` provides `dispatch<Catalog>(frameView, callback, limits)`.
Supply a successful `Fmt::frame`/`Framer` view and keep its backing alive through
the call. The callback has the shape
`Fmt::Error(Entry *, Object &, const Fmt::Header &, const Fmt::Trailer &)`; it runs
once after selected-type validation and can return an application error unchanged.
Decoded spans borrow the input. Retain its backing for deferred use, or copy the
values the application keeps. Moving owning fields out of the callback is an
application choice.

Framing requires the leading fields 8, 9 and 35, in that order, and exposes
MsgType directly in `FrameView::msgType`. Primary MsgType and secondary values
use immutable static hash indexes. A qualified MsgType selects by one unique,
non-repeating root field; that tag must not also occur in a repeating group.
Catalog metadata rejects modeled group selectors at compile time.

Secondary selection scans SOH-delimited tag/value fields without allocation or
candidate decoding. It records only the selector position, rejects duplicate
selectors, and compares the exact value. Substrings in other field values do not
select an entry. Empty selector values and unmodeled root selectors are supported.
Binary Data and Length/Data inspection are outside this selection contract.

Every selected entry uses its ordinary `MessageHandler<Object, Facet>`, including
entries sharing MsgType with different objects, facets or group layouts. Only
the selected schema performs required-field checks, scalar conversion and group
validation. Selection does not merge schemas or project position trees. Its
extra field scan trades input traversal for simpler code and lower metadata
storage; `parseBytes` covers only the ordinary selected position tree.
Frame/field bounds apply to selection; group/depth/storage bounds apply in the
selected codec. Standalone binary codec support is independent of this contract.

Standalone dispatch is caller-owned. Managed endpoints bind the same catalog to
`fixReceiveRx` and provide a copyable `RequestContext` retaining pooled backing
for escaped spans. See [ENDPOINTS.md](ENDPOINTS.md) for context and object
lifetimes, typed send/selector and reply-policy contracts. The
[application examples](example/README.md) demonstrate explicit correlation,
multi-response completion, sequence-keyed rejection and replay policy.

## Current standalone body API

`Zfix::Fmt::load(object, input, limits)` and `update` return an `Error` with
code, wire tag and byte offset. A zero error means success. Inputs are bounded
borrowed spans; standalone callers retain backing storage for any borrowed
fields. Converting into owning strings/objects retains only their own values.
Missing selected fields receive metadata/type defaults, while required presence
is checked independently and unselected/read-only fields remain untouched.
Updates include primary keys and mutable fields, including flattened children.

`handler<O, Facet>(input, limits)` supplies `ctor`, `alloc`, placement `new_`,
`load` and `update`. Check `valid()`/`error()` after conversion as well as initial
scanning. `alloc` uses the object's normal identified heap and returns null on
decode failure after cleaning up; placement `new_` returns false and destroys
a constructed object on failure. Application constructor exceptions retain
normal C++ behavior. Loads may partially modify objects on failure; there is no
rollback guarantee.

`Sink` writes directly into a caller-provided mutable span. Use
`save<Facet, Filter>(sink, object)` or `Handler::save<Filter>`, then check
`flush()`. Failure is sticky. A body output failure can leave a prefix in the
buffer, and that buffer must not be published as a successful frame. The sink
never sends/flushes to a network transport. Complete-envelope APIs are described below; the stage 3 acceptance gate
is recorded in STATUS.md.

Implemented conversion includes scalar integers, booleans, decimal/fixed/float,
FIX time, characters, strings, binary length/data pairs, custom scalar formatters
and flattened components. Facet configuration supplies number/time defaults;
field-specific properties override them. Scoped indexes reject tag collisions,
including after flattening, and body schemas reject reserved envelope tags.
Counted/nested groups use scoped membership and a delimiter inferred from the
first recognized field; cumulative entry/depth and parse-storage limits are
configurable. Explicit zero counts clear groups, and empty groups emit zero.

The `FIX` field properties parallel the JSON formatting properties, except
numeric `String` formatting does not apply to FIX:

- `ID<N>` selects an integer tag; `Optional<true>` (`Opt`) omits unset values
  on output. `Optional<false>` retains them. Required input presence remains
  controlled by `Required`; optional boolean fields still emit `Y` or `N`.
- `NumberFmt<Fmt>` and its `Number<Fmt>` shorthand use a Z numeric formatter
  directly, without a JSON-style `String` flag. Field properties override the
  facet's `NumberFmt` default for both parsing and printing.
- `BytesFmt<I>` has `Base64`, `Base64URL`, `Base32`, `Hex`, and `Raw`
  shorthands. FIX defaults to raw bytes. Raw data requires `Len<N>`; encoded
  bytes may omit it. When present, the length tag describes encoded wire bytes.
  Encoders write directly to the sink; decoders write into owning field storage
  such as `ZtArray<uint8_t>`. Borrowed byte spans use `Raw`: immutable input
  cannot retain an additional decoded representation. Validation does not
  allocate a decoded buffer, and raw byte borrowing is unchanged.
- `TimeFmt<Fmt>` overrides the facet's time format. The default is
  `Zfix::TimeFmt<>`, which uses FIX timestamps with up to nine fractional digits.
  `ISO<Scale, NDP>`, `FIX<Scale, NDP>`, `CSV<Scale, NDP>`, and
  `Unix<Scale, NDP>` select the corresponding input and output formats.
  Unix scales are `Zfix::Time::Sec`, `MSec`, `USec`, or `NSec`; the scale
  parameter is unused by the other styles. FIX and Unix honor `NDP`; a negative
  precision trims trailing zeroes. Direct `ZuDateTimeFmt` FIX/ISO/CSV overrides
  are also supported. Properties containing template commas need parentheses,
  for example `(FIX::Unix<Zfix::Time::MSec, 3>)` in a `ZfStruct` property list.

`AsUnion` supports ordinary application unions. A root union can carry a
`ParseView` alternative borrowing the complete bounded input. Select exactly
one alternative with `resolve<Alternative>(object)` and check its returned
`Error`, or use a live union handler's `resolve<Alternative>(object)`. The caller
retains the input until resolution; resolved owning values follow their ordinary
lifetimes. No alternative is guessed. Saving a deferred/void alternative fails
unless a custom scalar handler explicitly supplies a valid wire representation.
A flattened union requires the chosen facet's configuration to declare
`template <typename U> using Union = Alternative;`; the selected alternative's
fields supply the parent scope, including its binary/group boundaries. Choose
the facet through application/catalog discrimination before decoding.

`AsMultiple<Code, Props>` explicitly selects space-delimited primitive elements
in one UDT field. Vector type codes use the field property `FIX::Multiple`.
Empty input produces an empty container. Empty tokens,
trailing separators and malformed elements fail; string elements cannot contain
spaces. Primitive `AsArray` is rejected as a repeating-group schema.

Default pointer-to-object UDTs flatten their pointee's fields. `ZuPtr<T>` is the
preferred owning form; pointees use their normal identified `new`/`delete`.
A present component creates a missing pointee or loads/updates an existing one;
an absent selected component clears and releases it. Raw pointer fields have the
same single-owner contract: the adapter may delete them when clearing, and the
application must delete the final pointer. Borrowed raw object pointers are not
valid load/update destinations. A const/value getter needs a setter and an
ownership-compatible value type. Null optional pointers emit no child fields;
null required pointers fail output.

The built-in FIX timestamp formatter's null output remains its default empty
value; a present empty timestamp is rejected by the ordinary timestamp parser.
Non-finite numeric values have no FIX numeric representation and fail output.
CString values use the framework C ABI convention: the adapter allocates
NUL-terminated destination storage with malloc, releases the prior selected
value with free on assignment/clearing, and transfers the final pointer to the
application. Fields must start null or point to independently malloc-owned
storage. A setter receives ownership and must not free the prior value again;
the application destructor frees its final pointer. Embedded NUL input is
rejected; explicit empty input creates an owned empty string. Missing defaults
are copied into destination-owned storage. Unselected CString validation does
not allocate. Prefer framework owning strings when C ABI storage is unnecessary.
The stage 2 body-conversion gate is recorded in STATUS.md; complete-message
framing and sessions have separate acceptance gates.

## Complete frames

`frame(input, limits)` returns `Incomplete`, `Complete` with one frame's consumed
length and borrowed `FrameView`, or `Error`. Append bytes and retry incomplete
input; on completion advance by `consumed` to inspect coalesced frames. The caller
owns retention through the existing stream and transport facilities. `Framer::scan` keeps
progress across append-only input, including storage relocation; `reset()` starts
the next frame. Once known, `expected` reports the bounded total frame length.
Only offsets and fixed metadata are retained. The stateless `frame` facade uses
the same validation parser. The parser allocates
nothing, validates leading 8/9/35, derives the checksum boundary from BodyLength,
and checks the modulo-256 checksum over original octets. It does not search
binary content for trailer-like bytes. `FrameView::content` starts after MsgType
and still includes any remaining header fields. `MessageHandler<O, Facet>` consumes a successful FrameView using one scoped
metadata index for standard header, selected body, nested groups and trailer.
It exposes checked header/trailer values and body ctor/alloc/new_/load/update.
Recognized header/trailer binary pairs remain available during inspection,
including unselected body data. Header fields after body, or body after a
signature, fail ordering validation. Application/catalog discrimination chooses
a concrete body schema before constructing the complete-message handler.

`writeFrame(storage, header, body, limits)` writes a typed standard header,
selected body fields and computed envelope. Check the returned `FrameOutput`;
its data is empty on failure. `Header` spans borrow their source values through
the call. PossDup requires OrigSendingTime. Standalone/raw managers supply these
values; managed sessions will supply owner-shard snapshots.

For explicit raw header/body production, construct `FrameWriter(storage,
beginString, limits)`, write into its `body()` sink, then call `finish()` once.
The first body field must be MsgType(35). Both storage and BeginString backing
must remain alive through finish; a temporary inline string is not retained.
A failed writer can leave partial bytes in storage and returns no successful
frame view. Publish only after successful completion and admission. This API
has no network side effects and promises no atomicity after TCP publication.

The current headroom writer converts the body once, then moves it in place only
when the actual BodyLength needs fewer digits than the reserved prefix. The
reservation uses the largest body that can fit the supplied capacity, including
digit transitions, so an exactly sized frame buffer remains usable. See the
comparison and remaining gate work in CONTRACTS.md and STATUS.md.

`Handler::raw<Field>()` and `MessageHandler::raw<Field>()` validate and borrow a
selected top-level scalar field, for example `raw<ZfField(Order, id)>()`.
This reuses parsed positions and pair/group boundaries; it does not copy an
owning destination string. Missing and explicit empty are distinguished by a
null/non-null span pointer. Check error after inspection and after full body
conversion; a routing inspection does not complete semantic validation of the
remaining body. Requiredness follows the selected operation. Custom handlers
retain their own ordinary conversion/allocation behavior.

Known text uses the TagValue single-byte profile and rejects C0/C1 control
bytes. Paired Bytes fields preserve arbitrary octets; a Bytes declaration must
supply Len metadata. Unknown values receive no semantic text validation.
Header encoding carries MessageEncoding(347) for application-declared encoded
fields; encoding-specific business validation belongs to the selected dictionary.
Header secureData and Trailer signature are opaque length/data spans. Their
presence can be represented by a non-null span, including explicit zero length.

Native vector codes require `FIX::Multiple`. Raw CStringVec elements use the
same malloc/free ownership contract as CString fields, with every old selected
element released before assignment and the final elements released by the
application. Owning string elements use their ordinary container ownership.
Defaults are cloned, and failed list conversion releases elements already
allocated. Read-only list inspection validates spans without allocating strings.
Prefer StringVec/framework strings when C ABI storage is unnecessary.

## Controls and receive sequencing

`ZfixSession.hh` contains `Zfix::Session` administrative payloads: `Logon`,
`HeartBt`, `TestReq`, `ResendReq`, `Reject`, `SeqReset` and `Logout`, their `Msg`
union and compile-time `MTs` list. `Session::read/save/write` use the same codec
and compile-time facets as application messages. Runtime ROE registration is
not provided. `Transport` selects FIX4/FIX44A/FIXT11/Raw session behavior;
TCP/TLS bindings live separately in `ZfixNet.hh`.

`RxMsg` in `ZfixRxMsg.hh` retains one received backing buffer and its frame,
header, trailer, control and receive metadata. `RxFramer` extracts one pooled
complete frame from a native stream; it is not the message representation.
`SeqRx` uses `ZmPQRx` and the queue's first gap. Restore records a known logical
tail without inventing payload nodes. Input/queue/work bounds and retry tickets
remain on Rx. Unknown application fields are skipped by the declared schema;
malformed known fields are reported through ordinary Reject handling.

`LinkState` in `ZfixLinkState.hh` owns the Rx lifecycle. It begins after native
TCP establishment/TLS authentication, enters initiator Logon or acceptor
Accepting and starts the Logon deadline. Native connection establishment is not
a FIX state or timer. Admission, identity claims, epoch and connection generations
still protect asynchronous work and teardown.

Ready requires actual post-disposition Rx completion and a Tx recovery notice.
No mandatory TestRequest round trip precedes ordinary business readiness. A gap
pauses Ready; resumed directional recovery restores it. Send-idle Heartbeats,
receive-idle probes and exact opaque TestReqID matching remain. `Link::probe()`
allows application-selected watermark requests; explicit drain/reset still
requires a fresh published challenge/echo covering its captured boundary.
An ordinary liveness challenge does not block otherwise recovered readiness.
Already-assigned ordinary catch-up also proceeds while Ready is false; only
new unsequenced business admission remains gated. This lets contiguous progress
make a pending replay prefix eligible without a circular readiness dependency.

## Tx bodies, association and replay

`TxBody` in `ZfixTxMsg.hh` is independently owned encoded content, without a
new wire sequence or SendingTime. `TxFrame` builds a complete output attempt;
the body and mutable transport frame have separate operational lifetimes.
`MT<Object, MsgType, Facet, Selector, Responses>` describes a type, and
`MTList<...>` supplies the explicit compile-time dictionary.

`Tx` associates new work on Tx before committing its sequence. A refused
association consumes no number; a Pending association completes through its
ticket. Successful contiguous publication removes its operational sequence
node. Wire owns admitted frames/body handles until local completion. A priority
reconnect Logon marker remains only until contiguous catch-up consumes it.
It can never supply replay content.

Every replay attempt requests the app through
`fixRetrieveTx(TxContext, acknowledged, ticket)`. The context identifies epoch,
connection, exact sequence and replay versus ordinary missing-send retrieval.
An immediate `TxReload` supplies a body, no body, or Pending. Deferred results
complete through `reloaded(ticket, history)` after the hook returns. Replay
results are never inserted into a queue or retained for another attempt: output
refusal drops the result and a subsequent attempt requests the app again.
Ordinary output refusal is different: its installed operational node remains
charged and may be retried without a new app lookup until publication or abort.

The app may suppress stale orders or return a repriced quote/different type or
ID. Zfix preserves requested sequence/epoch and replay-header rules rather than
comparing against original payloads. No body means a protocol gap-fill. Adjacent
missing keys coalesce only within the current bounded turn. All request ranges,
lookup tickets, actual directional recovery and new-send progress remain distinct.
`SeqTx` retains generic `ZmPQTx` for new send and ordered archive work, while a
Zfix-only bounded driver implements always-app replay without queue lookups.

`abortTx_(epoch, seq)` exposes held-only removal of unpublished work. It releases
the operational charge while preserving the assignment; subsequent retrieval
can supply replacement content or gap-fill. Actual ordinary replacement loads
are charged when installed, not from another process's saved memory occupancy.

## Managed control commands

`LinkTx` in `ZfixLinkTx.hh` coordinates unsequenced controls, association, Tx
publication and notices to Rx. `controlCommands` snapshots Rx decisions into
bounded owner handoffs. Publication notices precede transport flush; completion
returns through the existing posted owner barrier. Required controls do not
become an application replay cache.

`fixPublishedTx(body, context, originalTime)` informs the app of the actual
original or replay output before flush. `fixCompleteTx` reports local completion;
`fixAckdTx` reports independently established session progress. Neither proves
business completion. All payload retention, history availability and durability
belong to the app.

Generic ordered archive completion emits `fixArchiveTx(epoch, begin, end)` for
each nonempty half-open `[begin,end)` range, even though core message nodes have
already retired. The app can move its own records to longer-term storage or
delete them. There is no `archived()` core-payload-release handshake. Archive
notifications do not originate from socket completion or checkpoint restore;
app persistence is not a core drain barrier. Later replay always asks the app,
including after archive. Pending archive work still runs on Tx in bounded turns.
Warm hold/reconnect resumes unfinished archive chunks without repeating ranges
already reported. A fresh owner restored from a checkpoint starts at restored
acknowledged progress and emits no retrospective archive callbacks.

## Link progress and startup restoration

Sequence progress and pending recovery belong to the link's embedded Rx/Tx
queues and survive transport reconnects. There is no separate session key or
checkpoint registry. Select the logical link directly through the application's
existing ownership and transport association.

`rxProgress_()` and `txProgress_()` read each owner's cursors and pending range.
`restoreRx_(epoch, progress)` and `restoreTx_(epoch, progress)` restore those
queues while inactive, before opening transport-dependent FIX activity. The
explicit epoch must match the selected link. Cursor ordering, ranges and
sequence limits are validated before mutation; cursors and range ends are
exclusive, and a zero range pair clears pending recovery. Each operation runs
on its owning shard.

Applications persist the fields they need with their existing link record and
restore each direction explicitly. `HeaderConfig header` contains version,
CompIDs and optional sub/location IDs; mode and application version remain
connection configuration. Current wire headers undergo normal validation.

Physical queue memory is not persisted progress: `TxBytes` and restored byte debt
are removed. An empty restored operational queue begins charged at zero, even
with loaded-but-unpublished sequence assignments. Retrieval installs ordinary
replacement content under current admission limits; replay content is transient.
Live operational count/bytes belong to Tx statistics. Restoring acknowledged
progress does not synthesize archive notifications or a durability guarantee.

## Drain frontiers before reset

`Rx::drainBegin_(connection, now)` starts an owner-local drain request. Session
pauses readiness and emits a ticketed `Drain` event. Once Tx applies it,
`candidate_` holds subsequent business candidates without assigning sequences.
Existing association continuations, publication, local output and replay continue;
control and recovery commands remain admissible.

When those Tx activities are quiescent, a one-shot `DrainBoundary` notice captures
an exclusive published frontier. Socket completion alone does not prove peer
progress. Rx waits for both directions to recover, issues a new local probe and
requires its exact response to acknowledge at least that frontier. Only then
Session emits `Drained`; business readiness stays paused. Later administrative
traffic may advance publication beyond the captured frontier and does not invent
new business work. A restored logical receive gap blocks the proof even when no
payload nodes are retained.

`drained_()` is a current protocol condition. New recovery invalidates it and
requires fresh explicit drain proof. The configurable `drainTimeout` (default
30 seconds) bounds waiting on association, replay, local I/O or peer response;
timeout closes without changing epoch or sequence progress. `SessionTimer::Drain`
has its own absolute Rx deadline. A reset still needs explicit counterparty
agreement and owner preparation; the current drain operation does not change an
epoch, emit reset Logon or authorize discarding history. The following explicit
preparation procedure consumes a current drain proof.

## Epoch-safe Tx callbacks

The managed Tx APIs require an explicit captured epoch for acknowledgements,
abort and replay. Core `Tx` uses `acknowledge_(epoch, next)`,
`abortTx_(epoch, seq)` and `replay_(epoch, begin, end)`. `LinkTx` additionally
checks connection for `acknowledge_(connection, epoch, next)` and
`replay_(connection, epoch, begin, end)`. Body-free archive notifications carry
their operation's epoch; no application payload-release completion exists.
The generic `SeqTx` component remains independent of protocol epochs.

Core `Tx::reset_(oldEpoch, nextEpoch)` is a quiescent owner primitive, requiring
business readiness off, publication stopped, no pending association/replay,
and an empty, acknowledged operational queue. It starts the new sequence space
at one and reports optional `resetTx(oldContext, nextEpoch, previousProgress, requestID)`
with the old exclusive progress snapshot (requestID zero for synchronous reset).
Epochs must advance and never wrap.
Connection identity and continuation ticket counters persist, so old callbacks
cannot target new data merely because its sequence was reused.

This primitive does not negotiate a wire reset or prove transport quiescence;
its caller must establish protocol agreement and complete I/O preparation first.
The session coordinator uses the quiescent operational checks below.
Receive `SeqRx::reset_` requires
held processing with neither retained input nor a logical receive gap.

## Operational output retirement

All ordinary published operational nodes retire automatically. An agreed idle
epoch transition checks that the operational queue is empty after priority
catch-up, outstanding lookup and output drain. No retained-history eligibility
scan or application release acknowledgement remains. Reset completion stays
posted so its request ID is established before callbacks. Application records
and long-term store retirement remain application policy.

## Agreed live and reconnect reset

After counterparty agreement and ingress/I/O preparation, call owner-local
`Rx::prepareReset_(connection, oldEpoch, nextEpoch, initiator, now, path)` on
both peers. `path` is `ResetPath::Live` by default or `ResetPath::Reconnect`.
A current `Drained` proof, no retained receive work and an increasing epoch are
required. For reconnect, the agreed initiator must match the physical link role;
a live reset can reverse those roles. Agreement is application policy and is
never inferred from receiving an unsolicited 141=Y.

Preparation holds business admission, finishes earlier commands and local output,
then asks Tx to confirm that operational catch-up and owner work are quiescent.
Application history does not block preparation or require a release handshake.
Tx commits the new epoch and posts a request-keyed `ResetReady` notice to Rx; Rx resets its held
queue and commits the same epoch. Optional `epochTx(oldContext, nextEpoch,
previousProgress, requestID)` and `epochRx(connection, oldEpoch, nextEpoch,
previousProgress)` report the old owner snapshots. Applications can wait for
both owner notifications and persist combined state before activation; no store
or mandatory persistence barrier is imposed.

For live reset, call `Rx::startReset_(connection, nextEpoch, now)` after both
peers are prepared. The agreed driver sends Logon 141=Y at sequence one, and the
receiver replies likewise. `Resetting` returns `RxResult::Blocked` before stamping
the incoming frame's epoch: the adapter retains that frame and retries it on
`RetryInput`. Do not stamp it from the sender's context. Existing admission is
retained for live reset; reconnect performs normal inbound admission again.

For reconnect reset, call `Rx::disconnectReset_(connection, nextEpoch)` after
preparation. It closes without sending the reset Logon on the old connection.
The next `open_` consumes that authorization once; the new physical connection
starts with 141=Y and sequence one. Ordinary reconnect without this procedure
continues the existing epoch and cursors. All managed modes restore ordinary readiness after actual directional recovery. Mode 4.4A
still validates 789 but does not replay an obsolete pre-reset range.

A preparation timeout/close cancels an uncommitted request without advancing
epoch. If Tx has committed before the close reaches Rx, its keyed completion
still advances the closed Rx owner; no reset Logon is emitted. Reopen is refused
while that local resolution is pending. A committed epoch is never rolled back;
peer uncertainty following a partial wire handshake needs application policy.
Stale old-epoch frames, completions and closes cannot mutate the new session.
Fresh-session reset uses explicit authorization below. Concrete live/reconnect
barriers run in the reset integration fixtures, including preparation/start
close, timeout and refused-Logon failures followed by continuing-epoch recovery.
Native short-write/EAGAIN behavior is verified separately by TransportTest.

## Deferred replay completion

An asynchronous replay result is consumed for that one current attempt on Tx.
Held/closing results retire their matching obligation and drop content; stale
tickets cannot retire newer lookups. After refusal or resume, request the app
again. No deferred replay bridge node, payload cache or cached approval survives
an attempt. Wire backing is retained only for an already admitted physical output.

## Approaching the sequence limit

`TxLimits::controlHeadroom` reserves the final sequence numbers for controls
(default 1024). Once the next new sequence enters that range, core Tx calls
optional `limitTx(nextContext, maximum, controlHeadroom)`; `LinkTx` exposes
it as `sequenceLimitTx` on the application implementation. The context carries
current epoch/connection and the next sequence, rather than a published-message
identity. The notification runs on Tx after association commits, before subsequent
owner work. Post application policy work from it; do not reenter the sequence
owner. No timer, calendar, Logout or reset policy is selected by this callback.

The notification fires once per epoch, including when restored progress is
already within the reserve at attachment. Pending/refused loads do not advance
it. Replays and ordinary reconnect do not repeat it. A new agreed epoch rearms
it. Business sends are refused within the reserve; controls remain admissible
through the configured maximum. Configuring zero headroom reports exhaustion
without reserving controls. Every sequence calculation remains bounded and never
wraps, including a maximum of UINT64_MAX minus one.


## Fresh-session reset on connect

Before the first open, an application with explicit peer agreement can call
`Rx::resetOnOpen_(epoch)` to request 141=Y/34=1 for that first connection.
The owner must be fresh, with received/processed cursors both one and no retained
input. A captured epoch mismatch, raw mode, invalid config or any previous open
refuses the operation. Before opening, `resetOnOpen_(epoch, false)` cancels that
choice. Restoring advanced receive progress while authorization is armed is
refused. This operation changes neither epoch nor progress: the application has
already selected a fresh epoch rather than abandoning old state implicitly.

The opening event carries that authorization to Tx, which independently requires
loaded/sent/acknowledged cursors one, no queued bytes/replay/history and no pending
association/retrieval. An inconsistent Tx owner refuses attachment before
emitting bytes or consuming a connection ID. The endpoint must handle that refusal
as an opening failure. Do not relabel a continuing session as fresh to discard
application history.

Authorization is consumed by one open attempt. Both reset flags and sequence one
are required, normal inbound admission still runs, and business readiness still
requires recovery/synchronization. Ordinary later reconnect continues this epoch
and numbering without a reset flag. A continuing owner's reset uses the drain and
preparation operations above. An unprepared peer reset still reports `NeedReset`;
its arrival cannot authorize clearing progress. Application admission and
outbound Logon customization use the endpoint hooks documented in ENDPOINTS.md;
TLS certificate/peer verification remains the transport's responsibility.


## Application-owned unsequenced queues

`LinkTx` inherits the optional queue boundary `Hooks<Impl>`. Its owner-local
`enqueue_(body)` calls `enqueueTx(body)` without assigning an epoch, connection,
sequence or timestamp to that body. The application bounds and owns its queue;
false refuses admission. Administrative bodies and null handles are refused.
Default hooks refuse enqueue and return no candidate, preserving direct
`candidate_` use when the application already supplies selected work.

Override `dequeueTx()` to transfer one retained next candidate to Tx. The pump
fetches only while business-ready and after older controls. One candidate is
selected per owner task, yielding between candidates. An empty result pauses
fetching. `queued_()` announces externally prepared work or wakes a paused queue;
accepted enqueue calls it automatically. Queued work remains unsequenced while
disconnected, recovering, draining or reset preparation is pending. The engine
retains at most one fetched candidate while association completes; it creates no
parallel application backlog or persistence store.

Override `selectedTx(body, proposedContext, result)` together with the queue
hooks. The body is returned with the final selection result; asynchronous Pending
loads defer this notification until completion. Accepted means association has
committed, before any publication. For refusal/overload/limit/invalid results,
the context is only the attempted association, and committed progress has not
advanced. The application chooses retention, rejection or retry using the returned
body; these results pause further dequeuing until `queued_()`. Retry after relevant
capacity/policy changes, rather than waking continuously on unchanged overload.
Callbacks execute on Tx and should post work to a separate application owner.

A delayed accepted association after disconnect follows normal unload before
selection refusal; the old captured context is retained. Successful committed
messages retain their assigned sequence through the existing send/replay lifecycle. Unselected application
queue entries survive ordinary reconnect and can be retained across agreed reset
by application policy. Their wire context is assigned only after new readiness.
Direct `candidate_` returns Busy while this queue path has announced work or an
outstanding selection; avoid competing sources that bypass application ordering.
Use the bounded `submit(body)` handoff from AppRx or AppTx as documented in
ENDPOINTS.md; direct selection methods remain Tx-only. Typed send/reply prepares
payloads through the catalog and reports conversion/admission failure through
the existing endpoint contract.


## Peer Reject delivery to the association owner

Ordered managed receive processing emits optional `peerRejectRx(PeerReject)`
after the session observer. The fixed record holds one const pooled backing,
parsed Reject fields, its exact wire view, incoming epoch/connection/sequence and
PossDup flag. Parsed text borrows that backing; moving/retaining the record keeps
it valid after the receive node is released. Invalid controls are rejected before
this hook, high Rejects wait for recovery, and low replay duplicates do not notify
again. Raw mode remains under the external manager.

Post this record to the owner that handled outbound association and call
`LinkTx::peerReject_(report)` there. Its optional `rejectedTx(report)` callback
lets the application resolve `{report.context.epoch, report.message.refSeqNo}`
using the mapping established by load. `context.seqNo` is the incoming Reject's
own sequence; `context.connection` is its receive provenance, which can differ
from the original outbound association's connection. A retained older connection
in the same epoch is valid. Future/zero connections, wrong epochs, absent backing
and zero sequence/reference are refused before notification.

The library supplies no business correlation registry or inferred outcome. It
delivers references even when the application has moved original history to long-term storage, and changes
neither acknowledgement nor send progress. Route the post in association order;
do not mutate another owner's state from Rx. The original wire remains available
for application-specific reference extensions beyond the prescribed built-in
Reject fields. Concrete endpoints must wire this post and drain it at teardown.


## State and outbound session observers

Optional `Rx::stateChangeRx(SessionEvent)` receives each Changed event before
the general `eventRx` hook. The fixed snapshot includes instance, change sequence,
state/actions, epoch and connection. Change sequences remain monotone across
reconnect and agreed epoch turnover. Timer-only events, ignored stale callbacks
and raw bypass do not invent state notifications. Post the snapshot to a separate
application owner; do not reenter lifecycle mutation from the observer.

Optional `sessionTx(body, context)` observes administrative frames after transport
admission and publication notices have been queued, before flush exposes bytes.
The immutable retained body and assigned context identify the admitted message.
Refused frame/association attempts, ordinary business output and local completion
do not notify. Replay reports the actual admitted control: app-suppressed content becomes a SequenceReset gap fill; an app-supplied Reject
is observed as that actual transmitted control.
The replay flag distinguishes those frames. The hook may retain the body or post
it to another owner without copying its fields. The published cursor advances
when the enclosing admission returns; use the supplied context in the callback.

Incoming session observation continues through `eventRx` with Observe and its
retained input, and `sessionRx` for ordered controls handled by the queue. These
callbacks and the outbound observer report session activity, not business results.


## Partial-write completion and output failure

Transport admission owns a complete immutable frame until its final local
completion. A partial-write offset or a writability callback must neither allocate
a new sequence nor call `LinkTx::sent_`. Complete that admitted frame exactly
once, with its original captured body/context, when the local write succeeds or
fails. Local completion still supplies no peer acknowledgement or business result.
A transport callback also needs its own one-shot frame identity: overlapping
replay can reuse epoch, connection and sequence, so those fields alone cannot
identify a particular queued frame.

A Tx fault post can now call owner-local
`Rx::outputFailed_(connection, capturedEpoch)`. It closes with Output and cancels
lifecycle deadlines only for the current epoch/connection. An old failure from
before a live reset cannot close the reused physical connection in its new epoch.
The endpoint must terminate all admitted frame completions during teardown;
transport failure does not authorize dropping their original associations.

The fake byte-stream fixtures write actual prefixes into Rx-owned pooled storage
and run the incremental framer. They cover every split of a representative
business frame, zero-progress callbacks, queued controls, complete/delayed local
completion, mid-frame failure, multiple admitted frames and repeated replays of
the same sequence. Unpublished work retains its assignment through reconnect;
explicit application abort leaves a range for retrieval/gap filling. Already
admitted work retains its sequence for recovery even
when queued bytes never reached the peer. No complete-frame atomic TCP-delivery
claim follows from buffering or local completion.


## Transmission gate and separate association owner

The transmission/assignment gate is complete against fake owners and byte-stream
transport. `test/SESSION.md` maps all stage 5 requirements to evidence, including
separate application-owner association/Reject/unload continuations. Pending work
blocks new assignment; old-context acceptance after disconnect unloads before
new-connection work, even if that connection has already opened. Immutable body
handles and fixed context/tickets cross owners; application maps remain on their
own owner. Queued selection results can be posted there in order with peer Rejects.
Current header values are sampled when publishing, after any deferred association.
Concrete TCP/TLS endpoints and scheduler/teardown paths run in the integration
fixtures, including failover with queued work and stale association/completion
callbacks. The Linux transport/lifecycle gate is complete; see ENDPOINTS.md and
STATUS.md for its scope and remaining final memory/platform/delivery checks.

## Protocol diagnostics

Managed and raw endpoints expose owner-local `rxStats_()`/`txStats_()` snapshots
and optional bounded Rx/Tx wire traces. Configure trace quotas through HubLimits;
capture is disabled by default. Records own escaped/redacted text, rotate within
charged limits, and can be retained through observer handles without retaining
links or original input. Authentication/binary values and unknown dictionary
suffixes are hidden. See ENDPOINTS.md for exact counters, redaction policy,
custom-sensitive-field hooks, lifetime and budget scope. Reproducible trace
unit, memory and installed checks are in test/README.md; TCP/TLS counter/trace
coverage is in itest/README.md.
