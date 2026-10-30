# Zfix implementation plan

> Current connection-replacement authority: [zfix_repair2.md](zfix_repair2.md),
> implementation and authorized current verification complete; evidence is in
> [zfix/REPAIR2_AUDIT.md](zfix/REPAIR2_AUDIT.md). Prior repair authority:
> [zfix_repair.md](zfix_repair.md). Its always-app
> resend, body-free archive notification, readiness and naming decisions supersede
> earlier implementation/checkpoint text below. Earlier passing gates describe
> their captured pre-repair source. The user subsequently authorized current-build
> Zfix rebuild/retest without reconfiguration or dependency rebuilds; current
> passing evidence is recorded in [zfix/STATUS.md](zfix/STATUS.md).

Prepared 2026-10-26 from [zfix.md](zfix.md). This document prescribes the
implementation sequence; `zfix.md` records the design rationale, source
references and stable decisions Q001–Q018. Apply those decisions throughout.
Updated 2026-10-28 to incorporate [zmpqfix.md](zmpqfix.md) and the user's
queue, cancellation and receive-policy corrections. Those corrections supersede
older conflicting wording in `zfix.md` and historical checkpoints in
[zfix/STATUS.md](zfix/STATUS.md). The separate Zpb missing-field correction is
complete and must not be reopened as part of this work.

`zfix/STATUS.md` records prior stage 1–7 gate evidence, subsequent transport
reversions, completed stage 8 and final stage 9 delivery evidence. Historical passes do not prove the queue
repairs below. Revalidate affected gates after implementation; do not restart
unaffected completed work or count a partial root build as a completed gate.

## Execution rules

Complete the queue repair prerequisite below first, then resume the remaining
stages in dependency order. Complete each stage's acceptance gate before treating
it as a dependency for subsequent stages. Maintain a checklist of completed
deliverables and recorded test results in the module documentation.
Resolve ordinary API details from the cited precedents and repository guidelines;
append any genuinely unresolved design question to `zfix.md` without renumbering
Q001–Q018. Do not silently change a resolved decision during implementation.

Read [GUIDELINES.md](GUIDELINES.md) and [TESTING.md](TESTING.md) before editing
code. Use GNU C++2b, `.hh`/`.cc`, Z metadata and containers, CRTP/ADL/SFINAE,
identified heaps, owner-shard callbacks and the prescribed header/indentation
conventions. Use ZuTest/ZuTestUtil for repository-owned TAP tests. Build with
`make -j8` within `zfix`; refresh its `src` before consumers and build `all`
before invoking a directory's `make test` target. Reuse existing dependency
artifacts under the user's scope below.

User scope correction: further builds and tests are confined to `zfix`.
Reuse existing dependency artifacts; do not rebuild or retest other modules.
This overrides the broader build/retest instructions below and in repository
guidelines for this work. Record reused dependency build types with results.
The user's subsequent platform correction restricts this delivery to Linux x64.
ARM and Windows are out of scope and are not open acceptance gates.

The user's directory-role correction is also authoritative: `example` contains
standalone programs demonstrating the API, without regression-test entry points.
Isolated functional tests belong in `test`, native end-to-end suites in `itest`,
and third-party conformance tools/tests in `interop`. `util` contains reusable
workload programs/support and remains independently buildable. The subsequent
layout repair requires affected checks, not a rerun of unchanged measurements;
retain the original measured-source manifest and document its layout mapping.

Use the FIX specifications and `zfix.md` for session behavior and hook
responsibilities, subject to the corrections above. Use ZfJSON, ZfURI,
ZfTOML/ZfTreeLoad, Zfb and Zpb for adapter implementation patterns.
Use **Zfb** for missing-field assignment semantics. Use Zrest for typed API
shape, Ztcp/Ztls for connections, and ZmPQTx/ZmPQRx for sequence queues.
Do not add a parallel metadata engine, mixed-shard locks or runtime schema
customization machinery.

## Mandatory prerequisite: execute the queue repair plan

Read and implement [zmpqfix.md](zmpqfix.md) in its prescribed order before
continuing stage 8. That document specifies the affected queue algorithms,
Zfix adapter refactor, mutation-free restoration checks, owner lifecycle,
regressions and negative controls. This section aligns the delivery plan with
that repair; it does not replace its detailed contracts.

All queue repair items below are implemented and verified. This prerequisite
and stage 8 are complete. Current GCC/Clang Zfix-only optimized verification,
measurement execution, independent data verification and fresh installed
consumers also pass for the recorded snapshot. Current examples, common-Link
runtime and installed checks also pass. The latest compiler chain and fresh
measurements/audit, package publication/integrity and artifact hashes are complete
as recorded in stage 9.
The historical debug and focused GCC checks below
do not replace the current evidence.

- [x] Preserve the demonstrated bounded-`shift(key)` bug fix and the generic
  empty-payload-queue gap-detection bug fix in `ZmPQRx::stalled()`. Existing
  zero-length heartbeat tests pass independently of FIX; old-behavior controls fail.
- [x] Revert the unsupported Tx start failure-flag changes. Original retry
  behavior passes both start overloads with asynchronous stop completion after
  failure bookkeeping and before subsequent start. Premature-start controls fail
  the fixture's ordering assertion.
- [x] Separately repair false idle notifications in both Tx start overloads.
  Their shared compile-time helper keeps callbacks outside the existing lock.
  Repeated-start tests pass; selectively restored false-idle controls fail.
- [x] **Verified:** bind `Zfix::SeqRx` to
  `ZmPQRxGapObserve<>` through `SeqRxQueue_<Queue>`. Preserve real node/heap types
  and constant-one FIX lengths. Keep one restored watermark Key and one
  application-visible/saved request Span; no synthetic payload or marker branch.
- [x] **Verified:** migrate Rx restore,
  hold, reset, advance, resume, snapshots and retries together. Recovery follows
  the first `queue.gap()`. Preserve held subset snapshots, fresh resume requests,
  retry deduplication and reentrant-hook invalidation. The real scheduler timer-owner late
  callback/drain fixture passes for both TCP and TLS, including memory checks.
- [x] Remove shared `extendTail()`, `rxRestore()`, `recoverGap()`, `rxGap()` and
  forced `stalled()`. Generic Ignore/Observe/Fill regressions pass.
- [x] Narrow ordered `txRestore` to `(send, ackd)`, stopped and empty after
  caller drain, with `ackd<=send`, archive restored to ackd and no queue reset or
  callback. Generic restore/rejection tests pass; Zfix passes `(published, ackd)`
  and independently restores its physical loaded boundary.
- [x] Remove shared `txGap()` and its Zfix false-idle workaround; remove shared
  `rewindTail()` rather than recycling assigned numbers.
- [x] **Verified:** track active replay
  interval plus pending bounded chunks in Zfix's operation context. Advance only
  after accepted message/gap callbacks. Preserve partial success, refusal,
  deferred lookup and snapshots without shared private-state inspection.
- [x] **Verified:** remove assignment-
  recycling `cancelTail_()` and automatic reconnect unwind. Application abort
  deletes a payload and releases its charge once, preserving assignment progress.
  Existing retrieval/gap callbacks own missing-content policy; no cancellation
  categories, tombstones, ledger or additional approval callback.
- [x] **Verified:** preserve independent
  loaded/published/acknowledged progress with zero-byte missing assigned ranges.
  Only an agreed reset/epoch transition permits number reuse.
- [x] **Verified extension:** use the
  Zfix-only `SeqTxQueue_` one-Key transient tail bound to limit each missing
  new-send scan to `replayBatch`, leaving physical storage unchanged. Post another
  existing continuation when accepted work remains. Keep separate bounded
  unavailable-prefix Spans for new-send and replay lookup continuations because
  their ranges can be disjoint; these represent lookup progress, never a
  cancellation cause. See `zmpqfix.md` section 4.3.
- [x] **Verified extension:** assign
  reconnect Logon at the unchanged next sequence and admit it before ordinary
  catch-up/replay. Existing connection/original-time metadata prevents duplicate
  Logon output and identifies old non-replayable controls for ordinary GapFill.
  Establish asynchronous running/post state before the priority admission hook;
  refusal invalidates queued work, and a hook's hold/detach must remain
  authoritative rather than being overwritten by another resume. Unload remains
  only for stale acceptance before sequence commit.
- [x] **Verified extension:** accept the peer's
  immediate high-Logon recovery request while older publication waits on history.
  One exclusive Tx-owned actual-admission Key bounds requests and open-ended
  snapshots independently of assignment and contiguous publication. Keep future
  replay in the existing pending Span, submit only its published prefix, and wake
  it after accepted normal message/GapFill progress. Restore pending end at most
  loaded, initialize the high water to sent, and preserve separate acknowledgement.
  No extra flag, cancellation category or generic queue API is introduced. Test
  the real peer/deferred-history ordering and FIX4.4A replay before closing this gate.
- [x] Finish contract/status reconciliation and all required generic/dependent,
  TCP/TLS recovery, timer-owner drain, memory and root dependency checks. Record
  exact current results and limitations. `zi` must remain unchanged.

Repair-checkpoint evidence, collected before the later scope correction: all
three generic queue programs pass 72 TAP results; current
Zfix units pass 27 programs / 280 results plus compile-failure fixtures; native
TCP/TLS integration passes 12 programs / 222 results; utility tests pass three
programs / 17 results and all 18 native process cases. Focused memory checks pass
84 Rx/Tx/owner results, and native ASan/LSan passes 120 results; Valgrind Session
TCP passes 55 results with zero errors/lost blocks. Focused sanitizer dependency
DSOs remain uninstrumented. The full configured Clang debug `make -j8` completed
without warnings. Representative QUIC/HTTP/TCP/TLS checks pass 38 programs / 318
results. GCC 15.2 debug fixture/header checks pass nine programs / 186 results,
linking the current Clang-built dependencies; this is not a full GCC framework or
release pass. All removed-API searches and whitespace checks pass; `zi` is
unchanged. See [zmpqfix.md](zmpqfix.md) and [STATUS.md](zfix/STATUS.md) for exact
commands, negative controls and limitations.

**Repair gate completed 2026-10-28:** no production references to the removed APIs or old restore
signature remain; all required current repair checks pass with recorded evidence; the
shared diff contains only approved bug fixes and narrow Tx restore. Review every
remaining shared-library hunk against `GUIDELINES.md`. Then resume stages 8 and 9
below and revalidate affected earlier stages. Completing this prerequisite does
not complete the overall Zfix goal.

## 1. Establish module boundaries and contracts

Deliver `zfix/src`, `test`, `util`, `example`, `itest`, `interop` and `bench`
with their build registration and role-specific documentation. Add populated
targets in dependency order as later stages supply their contents.

| Library / files | Required responsibility |
| --- | --- |
| `libZfixFmt`: `ZfixFmtLib.hh/.cc` | Codec exports/version, independent of the session library. |
| `ZfixFmt.hh/.cc` | FIX facet, properties, handler dispatch, scalar conversion and scoped tag indexes. |
| `ZfixFrame.hh/.cc` | Standalone header/trailer types, envelope parser and writer. |
| `libZfix`: `ZfixLib.hh/.cc` | Session library exports/version and codec/transport linkage. |
| `ZfixSession.hh/.cc` | Seven built-in session messages using the common codec. |
| `Zfix.hh/.cc`, `ZfixHooks.hh` | Typed catalogs, context, send/reply interfaces and application hooks. |
| `ZfixLinkState.hh/.cc` | Session identity, transitions, recovery, sequencing and timers. |
| `ZfixClient.hh`, `ZfixServer.hh` | Thin CRTP initiator/acceptor endpoints over shared session machinery. |

Keep `libZfixFmt` dependent only on the foundation through `zf`. Put pooled
buffer and Ztcp/Ztls integration in `libZfix`; standalone codec input views
and output sinks must not require a networking hub or session engine.
Use the prefix/namespace `Zfix`, lower-layer namespace `Zfix::Fmt`, and
`Zfix_Fmt(T *)` ADL dispatch. Factor shared non-template work into `.cc` files.

Write the following contracts before implementing the state machine:

- An event/guard/action transition table for each managed mode, including
  sequence effects, timer effects, emitted messages and callbacks.
- A hook table specifying arguments, ownership, lifetime, ordering, refusal,
  failure and asynchronous continuation behavior.
- A limits table with named defaults, hub overrides and enforcement scope
  (message, session or hub). Standalone codec callers use the same defaults
  or supply limits directly.
- A conformance matrix pinning FIX specification editions and mapping each
  session mode to transition tests. Keep application dictionaries explicit.

**Gate:** build/install the library skeleton and compile standalone codec and
session consumers. Compile minimal scalar, alternate-facet, nested-group,
union and typed-catalog declarations. No session dependency may enter the
standalone codec consumer.

## 2. Implement FIX metadata and typed conversion

Declare `ZuStructFacet(FIX)` and `ZuFieldProp::FIX`. Implement `FIX::ID<N>`
for scalar tags and group counts, `FIX::Len<N>` for paired binary lengths,
and field-specific number/time formatting. Reuse generic properties,
property detectors, field accessors and standard filters. Provide compile-time
facet configuration through `Zfix_Config(Facet *)` and custom scalar dispatch
through `Zfix_StringFmt(T *)`.

Implement `Handler<O, Facet>` construction, placement `new_`, `alloc`, `load`,
`update` and static `save<Filter>`, with checked parse/error results. Select
facets at the facade and dispatch UDTs through the established `AsObject`
(default), `AsArray`, `AsUnion` and `AsString` patterns and field type codes.
Use object allocation/identified heaps as existing handlers do; do not invent
an allocator parameter or a parallel runtime schema system.

Implement the following semantics:

1. Flatten object components into the containing tag scope. Require IDs on
   wire-bearing fields, not on flattened component containers. Reject ID
   collisions within an effective scope, including collisions after flattening.
2. Encode array UDTs as a derived count followed by entries. Infer the receive
   delimiter from the first recognized group field and use its recurrence to
   start the next entry. Use facet order on output and ensure
   every entry emits its first field consistently. Do not require `FIX::Delim`.
3. Enforce group count and nesting boundaries. Explicit zero counts clear
   destination vectors; saving an empty vector emits count zero even when
   optional. Keep absence information for requiredness and diagnostics.
4. Resolve unions through explicit application/catalog discrimination and
   bounded retained parse views. Do not guess alternatives by trial decoding.
   Reject unresolved saves unless an explicit handler supplies a valid wire
   representation. Do not introduce a FIX union wrapper.
5. Use ordinary value conversion for `tag=<SOH>`: the value is `""`, not
   absence or a special reset command. For missing fields, supply constructor
   arguments in `Ctor` order and assign defaults to every selected writable
   `Init`, `Load` or `Upd` field, following Zfb. `Upd` selects mutable and
   primary-key fields. Leave unselected/read-only fields untouched. Validate
   required presence separately from assignment.
6. Permit partial destination modification on failure. Report the failure and
   clean up resources; never dispatch failed decoding as a successful message.
   Do not implement rollback, snapshots or transactional object staging.

Build one static `ZmLHash<..., ZmLHashStatic<...>>` from each scoped schema's
integer tag metadata and map received tags to field-indexed positions. Do not
rebuild lookup maps per message or linearly scan field IDs on the hot path.
Use bounded spans and compact offsets, with scratch storage sized from metadata.

Use `ZuBox`, `ZuFixed`, `ZuDecimal`, enum/flags properties and the built-in
`ZuDateTimeScan::FIX` / `ZuDateTimeFmt::FIXDeflt` / `FIX<NDP, Null>` facilities.
Implement FIX booleans, single characters and MultipleValue formatting as
format policies. Do not encode primitive arrays as arbitrary repeated tags.
Reject known malformed values, overflow, trailing garbage and recognized
duplicates in a scope. Skip unknown tags without semantic validation; reject
ordering only when it violates the protocol.

**Gate:** golden-wire and negative fixtures cover constructor/setter/filter
paths, missing defaults on reused objects, explicit empty values, zero/nested
groups, scoped duplicate IDs, union resolution and input lifetime, high private
tag IDs, precision overrides, legal alternate ordering and configured bounds.
Include compile-fail schema tests. No Zfb/Zpb changes are part of this gate.

## 3. Implement framing, binary fields and complete-frame output

Implement one incremental envelope parser returning incomplete, complete with
consumed length, or error. Enforce leading `8`, `9`, `35` and checksum tag `10`
at the BodyLength-derived boundary. Compute BodyLength and checksum over the
specified original bytes; never search binary data for a trailer marker.
Handle fragmented and coalesced messages with bounded arithmetic and retention.

Use the same length-aware field scanner for body, header and discriminator
inspection. Treat a length/data pair as metadata over one byte value; derive
length on output and consume exactly that many bytes on input, including SOH,
NUL and `=`. Keep known pair metadata available even when a field is not loaded.
Do not infer unknown binary-pair semantics or claim to recover boundaries
without sufficient metadata.

Provide separate body and complete-message APIs. Managed sessions own header
values; standalone callers and raw session managers provide typed headers.
The framer always computes BodyLength and CheckSum. Prevent body tags from
overriding envelope fields.

Write directly to destination storage with envelope headroom or measured
sizes. Compare these approaches for digit-width changes and copies before
choosing. In transport integration, hold pooled frame buffers until encoding
and admission succeed, then publish in order; a live auto-flushing Tx stream
must not expose an unfinished frame. Surface sink errors and explicit flush
failures. Do not claim atomic delivery after publication to TCP.

**Gate:** test every byte split of representative frames, concatenated frames,
binary delimiters, corrupt lengths/checksums, truncation, size limits and
output failure before publication. Fuzz framing and schema-aware decoding with
retention/allocation bounds. Round trips alone do not satisfy this gate.

## 4. Implement control messages and the session engine

Declare the built-in Logon, Heartbeat, TestRequest, ResendRequest, Reject,
SequenceReset and Logout with the field sets recorded in `zfix.md`. Keep
TestReqID opaque and echo it exactly. BusinessMessageReject and other business
messages remain application declarations.

Configure declared version independently from `raw`, `4`, `4.4A` and `T1.1`.
Use version only for header population/matching, including FIXT BeginString
and application-version fields where applicable.
Use mode for session behavior, including the alternate synchronization rules.
Do not select business schemas or gate tags from BeginString. In raw mode,
delegate control traffic, sequence values, recovery and admission to the
external manager while retaining conversion/framing and byte transport.

Use the opaque application-configured `SessionConfig.id` as the sole attachment
and checkpoint key, independently of connection lifetime and wire configuration.
Replace the old header-tuple `SessionId` with `HeaderConfig header` containing
version, local/peer CompIDs and optional sub/location IDs; remove `qualifier`.
Keep mode and application version separate. Applications may encode qualifiers
in their ID but the library must not synthesize a tuple or routing rule.
`Checkpoint.id` is an owned `SessionString`; match only ID and epoch, permitting
new connection headers with the same persistent ID while validating every
connection against its current header configuration. Check Sender/Target
orientation and admit the initial Logon through application callbacks. Bind
new connections to the application-selected persistent link and abruptly close
the stale predecessor without a drain barrier. Remove the attachment registry;
queued logical work and sequence assignments survive for the successor. Implement explicit
accepting/logon/established/logout/closing states after transport readiness, independent receive-gap and
outbound-replay state, heartbeat/test timers, timeouts and failure reporting.

Integrate **ZmPQTx and ZmPQRx** from `ZmPQueue.hh` using the repaired contracts
above. Use their queue, gap, archive/retrieval and scheduling facilities rather
than parallel sequence containers. Bind Rx to Observe through the Zfix-only
restored-watermark adapter; Zfix owns its request metadata and retry timer.
Receive recovery always concerns the first `queue.gap()`. FIX over TCP/TLS has
one outstanding receive gap following Logon; repeated or overlapping recovery
traffic does not justify extra private-gap accessors. Tx replay follows the
peer's known resend request and accepted callback progress, independently of
whether local history has any hole.

Bound read-ahead and replay work. Fetch replay content from the app for every attempt with
OrigSendingTime, PossDup, current SendingTime and recomputed framing. Preserve
independent new-send and replay progress. Handle repeated/overlapping resend
requests, stale gap fills and open-ended resend ranges. Existing retrieval/gap
callbacks supply protocol-valid gap fills under application history policy.

Implement the hooks `enqueue`, `dequeue`, `load`, `reload`, `unload`, `sent`,
`ackd`, `reject`, `stateChange`, `reset` and `session`. Provide owner-shard
operations for restoring received/processed and outbound
loaded/sent/acknowledged progress, recovery ranges and pending resend state.
Support selective restoration that distinguishes omitted members from supplied
values, with consistent session/epoch state restored before activity resumes.
Applications own storage, retention, commit policy, cancellation and archive
availability. Missing content follows the same retrieval/gap callback flow
regardless of its cause. Do not add a database dependency, mandatory persistence
barrier or canceled-versus-unavailable classification.

**Gate:** run the transition table against fake clock, transport and application
hooks. Assert state, sequence progress, output, callbacks and timer effects for
each transition and error exit. Vary declared versions independently of mode;
test raw bypass separately. Test retained, archived, retrieved and unavailable
replay data and opaque heartbeat IDs. Do not infer acknowledgement from an
arbitrary numeric-looking peer TestReqID or socket send completion.

## 5. Enforce transmission assignment, recovery and reset ordering

Keep application queue identity separate from wire MsgSeqNum. Follow this send
sequence for managed sessions:

1. Accept or refuse business work under application policy and limits. Queue
   accepted work without reserving a FIX sequence number.
2. On Tx, select work in transmission order, accounting for intervening control
   traffic and session readiness. Perform fallible body conversion before
   committing the wire association where possible.
3. Assign from the shared new-message sequence space and call `load` with the
   specific business-message identity, sequence and session/epoch context.
   A refused load must not advance committed progress or create a wire gap.
4. Complete the application's association before publishing the frame. If its
   mapping belongs to another shard, use a continuation and resume on Tx;
   do not block Tx or mutate the other owner's state directly. Later new
   messages must not overtake an already committed sequence.
5. Build the header from the current Tx-owned session snapshot, complete the
   frame and publish it. Preserve the assigned number through partial writes
   and retransmission. Retain accepted unsent messages or notify application
   removal through the defined hooks. Removing assigned work leaves its sequence
   consumed and a missing range for existing retrieval/gap handling; never reuse
   that number or silently drop its sequence association.
6. Deliver sequence-keyed reject notifications with sufficient reference
   context for the application to reject the corresponding business request.
   Route association and reject notifications consistently to its owner.

Apply gap-fill/reset-specific header rules separately from ordinary new
control-message allocation. Keep recovery/control traffic operational while
regular application transmission is paused. Reconnection does not itself cancel
assigned business work or rewind the assigned boundary. If the application
aborts that work, preserve its sequence range and apply its existing history/
gap-fill policy. Startup must still send Logon first.

On same-ID reconnect without a reset flag, restore the existing session and
complete required resend/recovery in both directions before resuming regular
traffic with the continuing sequence numbers. Do not reset on disconnect or
local calendar boundaries by default.

For the standard reset path, establish a drain boundary, finish admitted
old-epoch traffic and recovery, then disconnect and reconnect with reset.
Also support coordinated reset while connected after draining both directions.
Hold or refuse subsequent submissions during draining; unsequenced queued work
may be retained for the new epoch by application policy. Emptying the local Tx
queue alone does not prove both directions are drained. Guard delayed work
against stale connections/epochs.

Let applications supply schedules, trading calendars and counterparty reset
policy through hooks. Check sequence limits, notify with configurable control
headroom, and stop new application sends before exhaustion. Never silently wrap.
The exchange review in `zfix.md` informs these mechanisms; complete venue
adapters and venue certification are not first-release deliverables.

**Gate:** test interleaved application/control traffic, delayed and refused
loads, immediate sequence-keyed rejects, partial writes, retained pre-reconnect
submissions, recovery in both directions, both drain/reset paths, stale
continuations, sequence reuse across agreed epoch resets and limit exhaustion.
Cover assigned-message removal with unchanged assignment progress, application
retrieval/gap filling, zero-payload missing-range restoration and Logon-first
reconnect. Verify that reset does not silently discard unresolved recovery or
invent business results.

## 6. Integrate client/server transports and lifecycle

Compose Ztcp initiator and acceptor links, then Ztls. Reuse the same codec and
session implementation in both roles. Delegate ports/listeners to Ztcp and
certificates, peer verification and mTLS to Ztls. Expose outbound Logon preparation
and inbound admission/authentication callbacks; permit no FIX-layer auth.

Enforce ownership throughout:

| Owner | State and work |
| --- | --- |
| Rx | Framing/views, all ZmPQRx state, session transitions, receive/liveness timers and typed dispatch. |
| Tx | All ZmPQTx state, sequence assignment, encoding, send/replay/archive operations and send-idle/throttle timers. |
| Application owner | Correlation, business policy and optional storage; communicate through ordered hooks/continuations. |

Use thin `rxInvoke`/`txInvoke` dispatchers and trailing-underscore owner-shard
implementations. Transfer fixed snapshots and retained handles; do not read
another shard's live state or capture large variable data directly. Do not
freeze wire sequence/header values at business submission.

Retain borrowed receive backing once in the enclosing context/token. Standalone
codec callers retain their own input/parse storage. Off-shard submissions must
own their data through Tx consumption. Apply hub limits to application submission
and administrative posts, codec/frame/group bounds, sequence read-ahead, queued
output and replay batches. Retain ordinary transport buffer-pool tuning and
reserve bounded administrative capacity. FIX introduces no special native receive
quota, incomplete-input budget, network posted-buffer credit or receive-limit
statistic. Use the existing `ZiRxStream` buffering/extraction behavior. Deferred
Zfix processing may revisit that stream through its Zfix adapter; it does not
require a new native transport receive-resumption API.

Use ZmEngine lifecycle and owner-shard timers. Stop admission/reconnect, cancel
owned timers and drain their late callbacks on the timer owner. Submitted DNS
resolves and connects finish through their existing normal continuation callbacks
within a reasonable timeframe; applications must drain them during shutdown.
Add no FIX-driven cancellation APIs or native setup deadline cancellation.
Complete the Rx-to-Tx and outstanding-hook drains before releasing owners; a
generation change alone does not prove callback lifetime safety. Keep raw owner
back-pointers where the transport precedent uses them. Test failed startup
cleanup and restart without imposing rollback. Add protocol/queue counters and
bounded opt-in wire traces with authentication redaction. `zi` remains unchanged.

**Gate:** loopback TCP/TLS tests cover multiple sessions/listeners, optional
auth admission/refusal, connection displacement, reconnect/failover, delayed
storage recovery, overload, partial sends and stop with pending work. Restore
sequence state from an application-owned test store across restart. Use the
ports specified by `TESTING.md`; assert no stale callbacks or unintended
within-epoch sequence reuse.

## 7. Deliver the typed application API and examples

**Status: complete for the current Clang-built source and installed examples.**
The full unit/schema gate passes30 programs/309 scopes; current native
integration passes14 programs/225 scopes, including the moved regression
fixtures, and canonical interoperability passes76 cases. The transport-independent
owner is Link, directly composed by native CliLink/SrvLink with shared physical
hooks; Raw retains its independent engine. The final standalone zfix.cc/zfixd.cc
pair demonstrates typed Parties groups, two declared replies, an unsolicited
Tick, application-owned correlation/completion, sequence-keyed rejection,
missing-history policy and optional admission/authentication extension.
Both TCP/TLS pairs pass from the source build and from identical-source builds
against current staged installed artifacts, with eight actor exits0 and eight
empty stderr logs under ASan/LSan. Source/ELF/DSO/compiler provenance is in
`/tmp/zfix-pq-repair/canonical-example-installed/`; source and installed pair
records are in `canonical-example-{pairs3,installed-pairs}/results.json`.
The rich regression fixtures remain in test/itest. Stage9 separately records
completed compiler, measurement and published-package evidence.

Implement compile-time catalogs carrying object type, facet, MsgType and
optional secondary discriminator/response declarations. Reject ambiguous
registrations at compile time. Expose typed send, receive and reply on both
client and server; connection establishment is the distinction between roles.

Carry session/epoch, sequence, replay/duplicate and version context to the
application. Keep local admission, local send completion, session progress and
business completion distinct. Retain explicit borrowed/owning lifetime contracts
following Zrest. Optional call/completion conveniences must be driven by
application hooks, not a library-owned pending-call registry, correlation
scheme, single-response rule or automatic reconnect completion policy.

Provide small examples for request/reply, repeating groups, unsolicited events
and sequence-keyed rejection. Demonstrate application-owned correlation,
multi-response completion, recovery hooks and optional authentication.

**Gate:** test typed dispatch and lifetime/admission errors. Test immediate,
reordered and multiple replies, duplicate/unknown business keys and replay
under the example's explicit policy. No library path may manufacture a new
business request solely because a FIX message is replayed.

## 8. Deliver independent interoperability and load tools

**Status: complete on Linux x64.** The real four-pairing canonical76, fault104,
security40, duplicate8 and checkpoint/restart/reset/recovery24 process cases pass
over TCP/TLS, including actual persisted-business replay. Six initial
synchronization/probe timeout cases pass. Current GCC/Clang instrumented native
and independent-peer checks pass, and the current installed boundary passes
all15 targets (42 TAP programs/524 scopes plus18 nested process and 42 rich
application-policy cases).
Exact
configurations, artifacts and limitations are recorded in `zfix/STATUS.md`.

Build `zfix` and `zfixd` in `util` and QuickFIX/C++ fixture applications
`quickfix` and `quickfixd` in `interop`. Keep utility support independently
usable by examples, integration tests and benchmarks. Pin QuickFIX's version
and dictionaries; it is a fixture dependency only. Share workload definitions,
not wire parsers or session implementations.

Implement NewOrderSingle/ExecutionReport workloads with explicit order and
execution IDs, acknowledgement/fill progression, counts and terminal outcomes.
Provide named optional cancel/replace and group-heavy scenarios. Expose warmup,
count/duration, target rate/saturation, in-flight limit, session count, message
shape and seed. Add readiness synchronization, bounded deadlines, shutdown and
machine-readable result collection.

Run correctness scenarios and benchmarks for every pairing:

| Initiator | Acceptor |
| --- | --- |
| `zfix` | `zfixd` |
| `zfix` | `quickfixd` |
| `quickfix` | `zfixd` |
| `quickfix` | `quickfixd` |

Default all four to declared FIX 4.4, managed mode `4`, **TCP+TLS**, and the
canonical 4.4 dictionary. Supply explicit custom-tag/group fixture dictionaries
where needed. Keep other versions/modes and plain TCP selectable separately;
test raw mode with an external manager, not a canonical managed-session peer.

Test logon/logout, heartbeat/test timeout, reconnect, decimal/time/binary data,
nested/zero groups, gaps/resends, duplicates, gap fills, original timestamps,
reset and restart recovery. Use a deterministic fault fixture for fragmentation
and corruption. Require Zfix receivers to skip unknown tags; record QuickFIX's
own directional expectations without globally disabling validation.

**Gate:** all four pairings pass valid order/execution scenarios; both mixed
roles pass protocol interoperability. Record exact configurations, sanitized
transcripts, versions, dictionaries and negative-test expectations. Run installed
consumer tests. A skipped independent peer is not a passing gate.

## 9. Benchmark, verify Linux x64 and complete delivery

**Status: complete on Linux x64.** Implementation, compiler/runtime/installed
verification, fresh measurements, independent audit and package publication pass.
The current Link source passes unit/schema30 programs/309 scopes, native
integration14 programs/225 scopes and canonical interoperability76 cases
(`link-runtime-summary.json`). The native suite includes18 underlying workload
process cases and 42 rich application-policy cases. Current standalone examples
pass TCP/TLS from both source and installed builds, as recorded in stage 7.
All15 current installed-consumer targets pass42 TAP programs/524 scopes, plus
the nested18 native process and 42 rich policy cases, with clean ASan/LSan
diagnostics (`link-installed-verified-summary.json`). Their staged prefix is
`/tmp/zfix-link-install.yl143h1q/opt/z`; no dependency module was rebuilt.

The latest complete GCC15/Clang21 chain passes warning-free scoped builds,
unit/schema30/309, native14/225 and four-pair smoke8 for each compiler
(`link-final-compiler-summary.json`). The one later benchmark-only change fixes
the shared codec input timestamp: Native's default formatter trims trailing
fractional zeros while QuickFIX uses fixed nine-digit precision. Separate
wall-clock inputs made two retained Native frame lengths shorter by two bytes.
The failed `final-link-clang-opt-measurements` snapshot remains preserved.
`codec-input-fix-summary.json` verifies targeted GCC/Clang benchmark builds and
both engines' three shapes per compiler, with empty stderr, after selecting the
common `20261028-12:34:56.123456789` input. Protocol/library/test sources were
unchanged, and four protocol ELFs/36 loaded DSOs remained unchanged by this fix.

The complete fresh `final-codec-clang-opt-measurements` run passes288 process
cases (144 timing/144 separate audit) and18 codec rows. Its independent strict
audit passes96 coverage groups,576 heap records,672 resource records,288 audit
records,48 correctly rounded budgets, and679 diagnostic logs containing only72
declared SDK certificate diagnostics. All155 source hashes, six executable
hashes and36 loaded-library hashes match the current snapshot. The SHA-pinned
post-Link `publication-index.json` is complete with646 correctness/provenance
artifacts, including the limited compatible format/sequence Valgrind71 and
observer control evidence. Index SHA256:
`7fd5eb00de8edaa0299fe4f2095d501d1e2f6075fdf862b85c997d2965b2ea89`.

The [report](zfix/bench/RESULTS.md) and
[dated archive](zfix/bench/results/2026-10-28.tar.xz) are published; see
[unpacking instructions](zfix/bench/results/README.md). The archive is780980
bytes with6790 regular files and6789 verified checksum entries, no private keys
or ELF binaries, and separate current/original/rejected snapshots. Archive
SHA256: `bb066944cf075af408fed5210c7199182b306e42bf7152adfb23092e9b4a8587`.
The packaged verifier passes the unpacked current288-case/18-row/155-source
strict audit. The reproduced report matches byte-for-byte after normalizing
only its Raw artifacts footer's absolute unpacked path. Proofs are
`archive-verification.json` and `unpacked-final-reproduction.json` under
`/tmp/zfix-pq-repair/`; individual report/data hashes are in archive `SHA256SUMS`.

Archive reproduction exposed a derived-verifier scope error: recursive log
collection counted separate historical/verification logs. The one-line fix
selects current top-level runner/codec logs and the current process tree,
retaining the exact679-log/72-SDK-diagnostic and every other audit invariant.
The first archive, old verifier/verdict and failed unpacked verdict remain
preserved; measured raw data and manifests did not change. The corrected archive
passes the full integrity and reproduction checks above. No further build or
workload is required.

The final module distribution copy also passes (`dist-final-verification.json`):
all152 measured Zfix source files, module documentation and the published archive
match byte-for-byte. No other module was built. All delivery gates are complete.

Earlier compiler and measurement checkpoints below remain evidence for their
recorded snapshots. The existing dependency set is debug-ABI/ASan; optimized Zfix builds
must retain compatible settings and cannot be called an uninstrumented release.

The earlier pre-Link GCC15 optimized/ASan gate completed with the required inline
visibility guard: warning-free final Zfix `all`, unit27/280 plus schema,
native12/222, utility29+18 process cases, lifecycle24, canonical76, focused
independent codec/load/dependency checks and 42 examples pass. Exact configuration
and8-program/36-library/147-source provenance are in
`/tmp/zfix-pq-repair/gcc-opt-final-manifest.json`; `zfix/STATUS.md` records logs
and superseded failures. The clean scoped Clang optimized/ASan build and its
unit27/280 plus schema, utility29+18 cases, native12/222, focused independent
codec/load/dependency checks and canonical76 cases also pass. Its flags retain
the dependency ABI/sanitizer settings without the GCC-specific visibility flag.
See `clang-opt-{build,unit,util,itest,focused,canonical}.log` under the same
artifact root. The earlier installed16 checkpoint passed against its staged Zfix
snapshot and byte-identical reused dependencies (`clang-opt-installed-summary.json`);
the current installed15 boundary is recorded above.
The earlier pre-Link measurement runner exited0 with288 passing cases:144 timing
and144 separate audit runs, plus18 codec rows across two engines, three shapes
and three repeats. Raw evidence is in
`/tmp/zfix-pq-repair/final-clang-opt-measurements/`. Its independent
`process/verification.json` passes all288 cases,18 codec rows and provenance
checks, with zero sanitizer diagnostics. Post-layout unit/policy35 scopes,
installed-load29 scopes, the independent codec check, native18 process cases and
direct-util matrix smoke8 pass separately; they precede the Link refactor.
Successful package publication/integrity and archive/report hashes are recorded
above. Current compiler, example/native/interop/installed and fresh measurement
checks need no unaffected repeat. Preserve the
original pre-Link snapshot, rejected variable-timestamp snapshot, current fresh
snapshot and layout/codec-fix evidence separately; do not combine timing samples.

Benchmark the four pairings with identical workloads and comparable validation,
logging and application-selected archive policies. Record necessary peer
differences. Support separate local processes and configurable remote hosts.
Measure offered/admitted/completed orders, reports, wire throughput, in-flight
work, errors, unfinished orders and timeouts.

Report order-to-ack and order-to-terminal latency p50/p95/p99/p99.9/max using a
client-local monotonic clock. Include intended-submit scheduling delay for
paced runs, CPU/RSS, allocations/copies, retained bytes, code size and codec
latency. Cover scalar, group-heavy, binary, fragmentation, replay and overload
workloads. Disable per-message tracing for timing runs; retain separate
diagnostic runs. Publish reproducible metadata and derive performance budgets
from measurements rather than asserting unmeasured throughput.

Run current GCC and Clang Zfix builds using existing dependencies. Configure
optimized flags through `z.config` where applicable, then clean/build only `zfix`.
Preserve dependency-required ABI and sanitizer settings, including `ZDEBUG` for
the current dependency artifacts; `NDEBUG` does not disable those layout branches.
Record module/dependency compiler flags, instrumentation and any required inline
visibility policy separately. Cross-compiler sanitizer interoperability requires
actual Zfix runtime evidence. Do not claim a whole-framework or uninstrumented
release from this scoped verification. No further top-level rebuild or
dependent-module retest is required, per the user's scope correction above.
Validate Linux x64 only. ARM and Windows are outside the user's scope; no new
cross-platform work is required. Historical toolchain inventories imply no
platform pass and create no pending gate. Run Clang ASan/LSan and Valgrind checks through
`libtool exec` where applicable. Run Valgrind outside the sandbox with full
system/network access. Record tool/environment limitations rather than reporting
blocked checks as passes.

Finish installed headers/libraries, configure/Makefile registration, dependency
documentation and the `test`, `itest`, `interop`, `bench`, `util` and `example`
README files. Document default limits, hooks, ownership, failure behavior,
recovery/reset operation and all test/benchmark entry points.

**Final gate:** the queue repair prerequisite and all prior gates have current
recorded evidence; the independent interop matrix and benchmark report are
reproducible; memory checks pass;
the Linux x64 scope is explicit; standalone conversion and client/server examples
build against installed artifacts. Keep application-owned durability, auth,
business correlation and replay availability outside the library's policy.

## Implementation pressure audit — 2026-10-29

Completed the requested audit against every `GUIDELINES.md` category and flag
across all seven Zfix source roles and the plan-owned Ztcp/Ztls integration
hunks. [The audit report](zfix/AUDIT.md) records findings, repairs, retained
exceptions and verification. Repairs remove redundant frame-body movement,
cache/trace allocations, SDK group-tree copies, transient formatting allocations
and repeated trusted-frame validation; they also correct concrete heaps,
known allocation accounting and demonstrated Rx/Tx cache-line sharing.

Current Clang optimized/ZDEBUG/ASan verification passes the 309-scope unit/schema
gate, 225-scope native integration gate, independent codec/load/dependency checks,
76 canonical interop cases, standalone TCP/TLS examples and 100,000 fuzz runs.
Dependencies were reused; validation stayed within Zfix on Linux x64. Zi is
unchanged. The dated benchmark archive remains historical evidence for its
captured pre-audit implementation; no new performance result is claimed.

## Link/session/connection state audit — 2026-10-29

Completed the member-state follow-up documented in [the state audit](zfix/STATE_AUDIT.md).
Removed duplicate configuration, status/context fields and stored snapshot
values; six physical lifecycle timers now share one earliest-deadline schedule.
Immutable, Rx, Tx and scheduler-shared data are explicitly identified and grouped.
Scheduler nodes and real SDK contention have separate cache-line boundaries;
no new lock or atomic was added. Required protocol frontiers, independent owner
snapshots and asynchronously retiring completions remain distinct.

Compiler layouts show managed Link 4,416→3,456 bytes and Raw 896→704, including
shared-node isolation padding. All eight native compositions and 25 compiled
consumer layouts pass boundary checks. Current Clang optimized/ZDEBUG/ASan
verification passes 30 unit programs/312 scopes plus schema fixtures, 14 native
integration programs/225 scopes, independent codec/load/dependency checks,
76 canonical process cases and standalone TCP/TLS examples. No sanitizer findings
or warnings; all eight dependency DSOs are unchanged. Full evidence is under
`/tmp/zfix-state-audit/`; no new timing result or cross-platform pass is claimed.

## Cache-line composition repair — 2026-10-29

Corrected the previous state audit's excessive per-timer isolation and native
adapter ownership fragmentation. Removed `LinkTimer`; session/gap/idle scheduler
nodes now form one shared group, with deadline/generation/stop metadata on its
Rx or Tx owner. Native posted/work ledgers and the borrowed input pointer reside
inside the selected engine's Rx group; hooks/input mixin bases are stateless.

All eight complete native layouts preserve shared/Rx/Tx boundaries. Managed
TCP/TLS client/server instances each shrink by another 256 bytes; Raw complete
sizes remain unchanged while consolidating native Rx storage. This consolidates
FIX-owned storage; lower transport and application-derived owner groups remain
separate. [The corrected state audit](zfix/STATE_AUDIT.md) records sizes, scope
and repair verification. Earlier verification counts above are historical.

Focused repair verification uses a frozen baseline plus these repaired headers
and test, isolating the ongoing codec API edits: nine unit programs/98 scopes,
ten TCP/TLS integration programs/212 scopes, eight core layouts and 20 compiled
complete-instance layouts pass with no ASan/LSan findings or compiler warnings.
The full workspace gate remains for the main thread after its concurrent codec
refactor completes. Detailed repair evidence: `/tmp/zfix-cache-layout/`.

## Combined current-source verification — 2026-10-29

The full workspace gate following the cache-line composition repair is complete.
Current Clang 21 optimized/ZDEBUG/ASan builds pass without warnings using the
existing dependency binaries. Verification passes 30 unit programs/320 scopes
plus schema fixtures, 14 native integration programs/225 scopes, independent
codec/load/dependency checks, all 76 canonical process cases, standalone TCP/TLS
examples and 100,000 codec fuzz runs. No sanitizer findings or dependency DSO
changes were observed. The corrected state audit's packed scheduler/native Rx
layout is included in this build, closing its separate integration gate.

The performance follow-up in [AUDIT.md](zfix/AUDIT.md) removes wide arithmetic
from narrow integer parsing, a redundant identical-source projection tree,
duplicate archive structural parsing, unchanged recovery work, redundant
reference operations and unused/recomputed workload storage/values. Required
boundary validation, replay/output lifetimes and generic held-send semantics
remain. Exact budget tests follow actual retained schema storage. A proposed
framework sort replacement was reverted after comparison-count evidence showed
a material repeated-value regression. No foundation changes, new shared state
or timing claims were added. Current-source evidence: `/tmp/zfix-path-audit/`;
the published benchmark archive remains unchanged historical evidence.
