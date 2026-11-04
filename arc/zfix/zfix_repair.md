# Zfix repair plan

Connection ownership and replacement are superseded by
[zfix_repair2.md](zfix_repair2.md), whose implementation and authorized current
verification are complete. Completion evidence below applies to the earlier
repair only.

Status: source implementation and consumer migration are complete. The initial
review was source-only. The user's subsequent instruction authorizes rebuilding
and retesting Zfix with the current build and unchanged configuration; that
runtime verification is complete for that authorized scope. See the current-build
record in [zfix/STATUS.md](zfix/STATUS.md) for results and repairs found by testing.
Current evidence is recorded in [zfix/STATUS.md](zfix/STATUS.md). Source:
[zfix_findings.md](zfix_findings.md), with the subsequent
user corrections incorporated below. `docs/GUIDELINES.md` is authoritative.
Sections 1–6 use final source identifiers. The naming section records the
pre-repair-to-final mapping; its old names identify the original snapshot only.
It does not define an alternative API or authorize keeping both names.

## Scope and decisions

Repair the managed Zfix session and application boundary. Ignore MT registration
differences and generic Mx connectivity. Legacy FIX declarations inform the
comparison; unfinished legacy methods are not behavioral specifications.

The following user decisions supersede earlier design descriptions:

- Zfix must never maintain a message store/cache for replay or resend. Every
  resend attempt requests its content from the application, including when the
  original message remains in an operational queue or an in-flight frame.
- The application may return no message, a replacement payload, a different
  message type or a different application identity. Stale orders and repriced
  quotes are application policy. Assigned wire sequence numbers are not reused.
- Preserve application notification when `ZmPQTx` archives sequence progress.
  The application owns retained messages and decides whether to move them to
  longer-term storage or delete them. Zfix does not impose a durability barrier.
- Runtime ROE/tag customization is not required. Different ROEs use compile-time
  `ZfStruct` facets. Finding 5 is dismissed; do not add a runtime transform
  registry, a general control-rewrite hook or another session manager.
- The original source-only restriction is superseded by the user's instruction
  to rebuild and retest Zfix using the current build. Preserve the configuration
  and reuse dependency artifacts; do not reconfigure, rebuild dependencies,
  install or rerun benchmarks.

| Finding | Disposition |
| --- | --- |
| 1: cache-dependent resend and inaccessible unpublished abort | Always obtain resend content from the app; expose the existing held-only abort operation through the managed owner. |
| 2: retained published history | Delete core history retention; retain body-free generic archive notifications. |
| 3: mandatory readiness challenge | Announce readiness from actual directional recovery; keep liveness and explicit drain/reset proof separate. |
| 4: synthetic connection establishment | Begin managed FIX after transport readiness; remove the unused FIX connection phase/deadline. |
| 5: runtime ROE customization | No repair: explicitly outside requirements. |

Only edit Zfix and its plan/contract documentation. Do not change `ZmPQueue`, Zi,
Ztcp or Ztls to accommodate these repairs. Retain the endorsed generic queue bug
fixes. No compatibility forwarders, dependency rebuilds, network research,
benchmark reruns, global installation, ARM or Windows work is needed.

## 1. Define the application contract

Change the existing retrieval hook rather than adding a second policy callback:

```cpp
TxReload fixRetrieveTx_(
  TxContext context, uint64_t acknowledged, uint64_t ticket);
void fixArchiveTx_(uint64_t epoch, uint64_t begin, uint64_t end);
bool abortTx_(uint64_t epoch, uint64_t seqNo);
```

These are the final signatures. Propagate them through `ZfixLink.hh`,
`ZfixLinkTx.hh`, `ZfixTx.hh`, `ZfixSeq.hh` and every dependent example, utility,
fixture and document; remove the superseded signatures.

Retrieval remains Tx-owned. `context` supplies the requested epoch, current
connection, exact assigned sequence and ordinary-send/replay distinction.
`acknowledged` is the exclusive session-progress frontier, not the sequence
queue's storage head. Keep the existing `TxReload` outcomes:

| Result | Meaning |
| --- | --- |
| Body present, `pending == false` | The app supplies content for this lookup. Ordinary replacement installs an operational node; replay consumes a transient attempt input. |
| No body, `pending == false` | Suppress this assignment; send a protocol gap-fill. |
| `pending == true` | Wait for the existing one-shot `reloaded(ticket, history)` continuation after the hook returns. |

Validate requested epoch/sequence, structural output limits and original-time
requirements. Do not compare the supplied payload, MsgType or application ID with
the originally submitted message. On replay, retain the requested wire sequence
and replay header semantics; the app supplies appropriate original SendingTime.
New ordinary publication samples its SendingTime as today. The app owns any
business association changes caused by returning replacement content.

Keep `fixPublishedTx_` before transport flush, with the actual transmitted body,
sequence, replay flag and original time. Keep `fixCompleteTx_` as local output
completion and `fixAckdTx_` as session progress. None implies business completion.
These observations let the app retain exactly the information it requires for
future retrieval; Zfix does not retain another copy on its behalf.

`abortTx_` is a direct Tx-owner operation. Check epoch, use the existing held-only
abort rules and return refusal for published work or incompatible outstanding
operations. Do not support off-shard access by adding locks. Applications outside
Tx post fixed epoch/sequence arguments through the existing Tx dispatcher.
An out-of-order priority Logon already admitted to Wire is published for this
purpose: refuse its abort even while contiguous publication has not caught up.
Its existing publication metadata supplies that check; add no new marker state.

Removing unpublished content must leave `next` and its assigned sequence intact,
release its existing unsent charge exactly once and create no cancellation map,
classification or tombstone node. Upon resume, the existing missing-new-send
retrieval path asks the app whether to supply replacement content or gap-fill.
Expose this through `LinkTx` and `Link`; an inaccessible lower-layer method
does not satisfy the contract. `fixStopTx_`, after owner hold/detach, is an
appropriate existing point for reconnect preparation and unpublished aborts.

## 2. Remove replay's dependency on resident sequence nodes

Keep `ZmPQTx` for new-send sequencing and ordered acknowledgement/archive work.
Replace only the Zfix replay driver. Generic `ZmPQTx::resend()` searches resident
nodes before retrieval, so it cannot implement the required always-app contract
while operational nodes remain present. Do not modify the generic queue or add
a mode-dependent queue view that conceals its contents.

Use the existing pending request range, active bounded range, replay task ticket
and one outstanding retrieval ticket. Preserve request merging and the admitted
upper bound, including priority Logon admitted ahead of contiguous publication.
Ordinary missing-send lookup and direct replay share that one obligation; neither
issues another retrieval while it is pending. This does not add a blanket pause
on already permitted resident operational control output.
Snapshot open-ended requests at that admitted bound, but activate only keys below
the contiguous published frontier. A request covering an out-of-order admitted
Logon keeps that portion pending until contiguous catch-up makes it eligible.
Pending ranges, including such blocked portions, count as incomplete Tx recovery.
Already-assigned ordinary publication, retrieval and gap-fill must be able to
catch up during established session recovery while Ready is false; otherwise
the pending replay and Ready would wait on one another. Keep admission of new
unassigned business gated.

For each owner turn:

1. Activate at most `replayBatch` keys from the existing pending range. Start at
   the active range's current sequence.
2. Request every key from the application with `context.replay == true`.
   Never look in the operational sequence queue, Wire, completed output,
   publication observer records or a prior lookup result for resend content.
   After each synchronous hook, recheck running state, attachment, epoch and
   current operation before admitting a local gap or returned body: the hook
   can hold/detach the sender. Discard immediate body/no-body results when that
   happens. Register the lookup key/kind/ticket before invoking the hook, using
   existing state, so reattachment/reset/abort cannot slip through during the
   callback. Clear it for an immediate result and keep it for Pending, even when
   the hook held/detached the sender; that continuation must still drain before
   stop or reattachment can complete.
3. Consume a synchronous body as a local result, encode/admit its frame through
   the existing common Tx output path, then drop the local handle. Refactor that
   output helper to accept history/context directly; do not allocate a queue node
   solely to pass an application replay result to it.
4. Coalesce consecutive synchronous no-body results in a local range. Flush the
   gap before publishing a later body, yielding on Pending, or ending the batch.
   Its range is a protocol progress operation, not a retained absence cache.
5. Advance the active cursor only after successful frame/gap admission. On
   refusal, preserve the first unadmitted key, drop any returned body, and use
   the existing failed/writable/resume scheduling. A later attempt asks the app
   again. Do not spin, retain an approved payload for retry, or add another timer.
6. If a gap succeeds but a following body fails, keep the gap's admitted progress
   and retry from the body sequence. If the gap fails, discard the fetched later
   body and retry from the gap's beginning.
7. On Pending, use the existing lookup key/kind/ticket. Before yielding, attempt
   any preceding local gap. The deferred completion consumes its body directly
   on Tx only if the operation is running/current and the active cursor is still
   that key. Otherwise discard it after retiring the ticket. In particular, a
   failed preceding gap must not permit a later completion to skip that gap.
8. A completion received while held or closing retires the obligation without
   publication or cursor advancement. Resume requests the content afresh. Reject
   reattachment while an old retrieval obligation remains outstanding; preserve
   the existing full stop/drain contract.

Only a completion matching the outstanding ticket/kind may retire that lookup.
A wrong, consumed or stale ticket must not clear a newer obligation, including
a newer lookup of the same key. After matching, retire the obligation; discard
held/closing/non-current content without publication. For an otherwise current
attempt, invalid history reports failure without advancing progress; it must not
strand shutdown.
After consuming or discarding a matching completion, schedule remaining active
replay if running and not failed. A writable notification may already have run
while the lookup was pending; do not rely on receiving another one to resume.

The completion handoff may carry its one retained body reference until Tx
consumes that attempt. Once admitted, Wire retains the output until physical
completion. Neither lifetime is a replay store, and neither may supply a later
attempt or request. No replay result is inserted into the sequence queue,
including deferred results and results for already archived sequences.

Remove `m_replayCached`, replay `m_unavailable`, and the old replay insertion/
deletion paths. With this driver, a nonempty active range represents active
replay, including pending retrieval and refused output; a nonempty pending range
represents queued replay. Remove `m_replaying` rather than mirror those facts.
Retain only the missing-new-send range needed by the existing generic send scan;
it must never suppress a later replay application request.

Make generic `idleResend()` side-effect-safe: `ZmPQTx::start()` can call it even
though the FIX replay range remains active. It must no longer clear FIX replay
state. Complete direct replay through its own range advancement and existing
post/ticket scheduling. Preserve bounded turns and ordered continuation handling.
Keep both initial Tx recovery and later coordinator progress checks tied to
`active.length() || pending.length()` and the outstanding lookup. Route direct
task/completion outcomes through those existing coordinator progress checks;
removing generic resend must not lose a recovery-complete notification.

Remove the core `TxBody::replayable()` gate from application-supplied resend
content. Ask the app for administrative sequence positions too; do not infer
resend availability from the old message's classification. Remove the helper
where unused, or replace any independently required first-send control-lifetime
rule with an accurately named local rule. Do not silently apply an old type's
policy to the replacement body returned by the app.

## 3. Retain only operational output state

The new-send queue may retain an accepted unpublished assignment until its first
successful contiguous publication or an explicit held abort. Priority Logon has
the temporary catch-up lifetime described below. After `SeqTx::send_` succeeds:

1. Complete contiguous publication, admission-frontier and unsent-byte accounting.
2. Remove that exact sequence node from the operational queue.
3. Let existing generic-call, Wire and completion references provide their actual
   remaining lifetimes. Do not transfer it to another history list or map.

Do not remove an unpublished node after an output refusal. Do not remove the
priority reconnect Logon merely because it was admitted ahead of the contiguous
cursor: its node currently proves that its first publication already happened.
Retain it only until contiguous catch-up consumes that marker, then remove it
through the same successful new-send path. A resend request may cover that
sequence while its marker exists, but the request waits for contiguous catch-up.
Once eligible, obtain its content from the app, never from the marker.

Preserve assigned, contiguous published, admitted and acknowledged frontiers;
they describe different progress. Never reuse the sequence number of removed
content. Rename/remove public history-occupancy statistics that would incorrectly
describe the remaining queue as a replay store.

### Account ordinary replacement loads as current memory

`m_bytes` must equal the charges of actual operational nodes not yet consumed by
contiguous publication, including a priority Logon marker already admitted out
of order. It is an operational sequencing charge, not a replay-history size or
a count of all in-flight Wire backing. A previous process's memory occupancy is
not a current allocation.
Remove `StateField::TxBytes`, the restored queued-byte argument/debt and
`TxProgress::queuedBytes`; retain live queue occupancy in Tx statistics. Update
state metadata, restore validation, reset checks and every consumer accordingly.
Restore an empty operational queue with zero charged bytes while independently
restoring assigned/published/acknowledged cursors and replay ranges.

Centralize ordinary reload admission/installation into the existing operational
queue, covering synchronous preflight, generic missing-send lookup of subsequent
keys, and deferred ordinary completion. Validate and charge the actual returned
body/node size exactly once before generic send can publish it. A replacement
need not have the original size. Keep business/control admission reserves;
the admission calculation includes the existing Pending association's reserved
charge as well as installed nodes, without adding a second reservation counter.
Factor the size-budget check from allocation of a new sequence so reloading the
last valid assignment is not refused merely because `next` exceeds the limit.

On output refusal, keep that installed ordinary node and charge for retry. On
publication or held abort, remove the node and subtract its charge once. Do not
double-charge the same node through both preflight and completion paths. If a
replacement cannot fit the remaining operational budget, report an explicit
admission/size failure with no cursor advance; do not reinterpret it as an
application no-body decision and gap-fill it. The app still owns that record and
can change its policy or free other held assignments. This admission helper is
never used by direct replay; replay results remain transient attempt inputs.

### Body-free archive notifications

Keep generic ordered archive processing and its bounded archive scheduling.
Deleting core message nodes is independent of archive progress; it must not
advance acknowledgement or generate an early archive event.

Bridge the generic archive completion to the application at the existing
`idleArchive()` boundary. A completed chunk reports the half-open sequence range
`[begin,end)`: begin is included and end is the new exclusive archive frontier.

```cpp
auto begin = m_archiveScan, end = m_archiveTarget;
m_archiveScan = end;
if (begin < end) impl()->archiveTx_(begin, end);
feedArchive();
```

The upper Tx layer supplies the epoch to `fixArchiveTx_`. Update the scan frontier
before calling the app: the callback may hold the sender or acknowledge more
progress. An empty range emits nothing. The existing target/scan distinction
prevents duplicate scheduling; each target remains bounded by `replayBatch` and
actual acknowledged progress. Generic archive can advance through absent queue
nodes; do not create placeholder messages just to obtain its per-node callback.

The application uses the range to move its own messages to long-term storage or
delete them. It may batch that work. A later retrieval can complete asynchronously
from long-term storage. App persistence completion is not a prerequisite for
core node release or another core archive acknowledgement.

Remove the old public `archived()` payload-release handshake and body-bearing
`fixArchiveTx_(TxHistory)` API; migrate consumers rather than retain no-op shims.
Keep `fixAckdTx_` distinct: acknowledgement notification and completion of the
generic archive stage are separate events. A warm hold/reconnect preserves and
resumes unfinished archive chunks without repeating completed ranges. A cold
restore initializes scan/target from the restored acknowledged frontier as
today and emits no retrospective archive events. Restore is not a synthetic
archive.
This is ordered notification during live processing, not an exactly-once storage
guarantee across process failure. An application requiring recovery of its
storage transition persists/reconciles its own archive frontier; do not add a
second core persistence protocol.

### Simplify consequences of removing history

Delete core-history retention/release configuration, callbacks and statistics
that no longer have readers. Application store bounds remain application-owned.
The load utility's history cache remains its own policy and storage, not Zfix's.

At an agreed idle epoch transition, all ordinary published nodes have already
retired and contiguous catch-up has consumed priority Logon. Replace the old
history eligibility scan/retirement loop with the invariant that the operational
queue is empty. Remove its cursor/expected/retiring fields and the control
coordinator's wait-for-application-release state where solely used by that loop.
Retain one existing posted reset completion if needed: `resetBegin_` must return
its request ID before the coordinator receives the corresponding completion.
Do not introduce synchronous reentrancy into that assignment/callback sequence.
Keep epoch agreement, output-idle, pending-lookup and owner-drain prerequisites.

## 4. Begin FIX at transport readiness

Remove the managed-only connection establishment machinery:

- `State::Connecting` and `SessionAction::Connect`.
- `SessionTimer::Connect` and `SessionDeadlines::connect`.
- `SessionConfig::connectTimeout` and its validation/configuration plumbing.
- `LinkState::connected_()` and the redundant Rx wrapper/call sites.

`LinkState::open_()` runs after native TCP readiness/TLS authentication. It opens
the new connection generation, starts the Logon deadline, and enters initiator
`Logon` with `SendLogon`, or acceptor `Accepting`. Change `LinkTx::apply_` to
accept that initial initiator event instead of expecting `Connecting`.

Ordering matters: initiator identity attachment must succeed before `open_()`
can emit/queue Logon. Move that existing claim before the open call. On claim
failure, refuse/disconnect and report the existing diagnostic without emitting
Logon or faulting an unopened LinkState. If open subsequently refuses, release a
successful claim through the existing final owner drain. Acceptor admission and
identity claim ordering remain as implemented.

The claimed identity is solely the opaque application-configured
`SessionConfig.id`. Headers,
mode and version are not attachment or checkpoint keys. `HeaderConfig header`
contains the current wire version/Comp/sub/location values; validate incoming
headers normally against that connection's configuration. `Checkpoint.id` owns
the application key and matches only that ID and epoch. A new connection may
change headers while retaining its ID and sequence epoch; applications choose
any ID qualifiers and routing policy.

Transport connection, DNS, TLS, retry and their deadlines remain in the existing
native layers. Keep connection generations for stale FIX callbacks, transport
up/down integration, admission, attachment and final stopped notification.

## 5. Separate readiness, liveness and explicit drain proof

Remove `m_synced` and `m_probeSync`. Retain the existing readiness flag,
directional recovery flags, opaque probe ID, monotone probe marker, confirmed
publication boundary, incarnation identity and drain metadata. Do not replace
the two removed flags with another synchronization state machine.

### Authoritative initial recovery

`peerLogon_()` validates and establishes the protocol session, emitting the
existing replay/acknowledgement actions. It must not immediately announce Ready
or infer complete owner recovery from the Logon values alone.

Calculate initial Rx-gap and FIX44A Tx-replay decisions as local wire facts,
then initialize `m_rxDone` and `m_txDone` false. Build RequestGap/Replay actions
from those local facts; using the newly false flags would request false/invalid
ranges. Automatic 789 recovery covers historical assignments before the newly
sent Logon, excluding that Logon itself. Keep the exclusive Logon boundary for
789 validation/acknowledgement and the admitted bound for explicit ResendRequest.
Let the existing post-disposition `Rx::first()->recovered()` confirm Rx,
and the first Established Tx Recovery notice confirm actual Tx state.

Only after those confirmations may `recovery_(true,true)` emit Ready, provided
no drain or peer Logout is active. Do not require another wire exchange. Remove
the old probe-only suppression/optimistic-event merging in `Rx::first()`.
In particular, never leave both Pause and an optimistic Ready in an event that
would cause `LinkTx` to reopen sending before its first recovery notice.
Dispose of any retained early peer Logout before declaring recovery complete.

### Liveness and optional application progress probes

Receive timeout and the existing explicit probe operation still issue opaque
TestRequest IDs. An outstanding liveness challenge does not block ordinary
readiness once both owners have recovered. Preserve exact echo matching, confirmed
publication, stale connection/epoch checks, timeout behavior and byte-exact peer
TestReqID echoing. A reply received before confirmed publication is not proof.

Expose the existing probe operation through a thin managed Rx dispatcher and
Rx-owned trailing-underscore entry point. This lets an application request a
session watermark for acknowledgement/archive policy without making it a
readiness prerequisite. Reuse the existing challenge and event machinery; add
no periodic policy, per-message acknowledgement map or new timer. Do not infer
business acknowledgement from this session watermark.

### Explicit drain and reset

Drain still pauses new business, clears any earlier challenge, waits for the
existing Tx drain boundary and completes recovery in both directions. It then
issues a fresh challenge. Only its exact published echo may emit Ackd and Drained
together, and only while recovery is complete, no peer Logout is pending and
the probe boundary covers the drain boundary. Mark drained and clear its deadline
there; `synchronize()` must not announce Drained merely from empty queues.

When accepting the Tx drain boundary, clear any challenge issued while waiting
for that boundary before requesting the fresh proof. While its proof remains
valid, a completed drain must neither issue another drain challenge nor emit
Drained again. A matching echo consumed during incomplete recovery is liveness
only and cannot become proof
when recovery later finishes.

Keep the coordinator's independent acknowledgement-boundary check and reset's
completed-drain prerequisite. False recovery clears Ready and emits Pause.
During drain it also invalidates completed drain/proof and requires fresh proof
after recovery; outside drain it need not discard an ordinary liveness challenge.
Late recovery notices or heartbeats must never reopen sending after peer Logout.
After clearing/cancelling a challenge, restore the Receive deadline only when
the heartbeat interval is nonzero. Preserve the existing explicit-probe timeout
fallback when heartbeat is disabled; do not create periodic receive-idle traffic
for a zero-heartbeat session.

## 6. Migrate consumers and documentation

Perform the work in this dependency order:

1. Apply the naming migration below, including namespace-shadowing aliases and
   all consumers. The old identifiers in its tables locate components in the
   initial snapshot; use final names for semantic repairs.
2. Define retrieval/archive/abort contracts and update the internal forwarding
   signatures; keep all stages on their existing owners.
3. Implement direct app-sourced replay and completion handling. Remove replay
   caches and the generic resend-state dependency.
4. Retire operational nodes at publication, bridge body-free generic archive
   progress, expose held abort, and simplify obsolete history/reset state.
5. Remove synthetic connection establishment with attachment-before-Logon order.
6. Remove mandatory readiness probes using actual Rx/Tx confirmations; retain
   explicit liveness/drain behavior and expose the existing application probe.
7. Finish semantic consumer migration and revise contracts and fixtures.

Update `zfix.md`, `zfix_plan.md`, `CONTRACTS.md`, `ENDPOINTS.md`, module READMEs,
and affected utility/example documentation. Remove claims that Zfix holds replay
history, that app retrieval happens only on cache misses, that the application
must release core history, or that ordinary Ready requires a probe round trip.
Document body-free archive ranges and app-selected post-archive delivery policy.
Record these user corrections as superseding prior plan text. Preserve historical
benchmark archives and clearly label older audit/layout evidence as historical.

Adapt the load utility's application cache to own publication records and its
archive/delete policy directly. Repricing/suppression fixtures keep their
business decisions outside the library. Existing standalone examples remain
small demonstrations; regression cases belong in `test`, networking/storage
scenarios in `itest`, and independent-peer coverage in `interop`.

Keep immutable configuration first, then genuinely shared scheduler data, then
cache-line-separated Rx/Tx groups. Reuse existing tickets, ranges, retained I/O
handles and post barriers. No locks, atomics, cancellation ledger, replay store,
parallel history container or speculative cached validation is introduced.

## Runtime acceptance criteria

These cases specify the required verification. The user has authorized current-
build Zfix compilation and tests, with configuration and dependencies unchanged.
Benchmarks and installation remain outside this execution scope. Do not report
the earlier 320/225/76 results as verification of these repairs.

| Area | Required evidence |
| --- | --- |
| Always-app replay | Every eligible replay key invokes the app despite other operational nodes, in-flight original output or previous successful replay. A request covering priority Logon waits for contiguous catch-up, then fetches from the app. |
| Suppression/repricing | An expired order returns no body and gap-fills; a repriced quote or different type/ID uses the requested sequence and app-supplied replay content. No original-body equality check. |
| Retry and Pending | Refused output, failed prefix gap, held/stale completion, stop and reconnect preserve cursors; every subsequent replay attempt requests the app again; no replay-result insertion/cache remains. Ordinary output retries may reuse their installed operational node until publication or held abort. A hook returning Pending after holding/detaching still creates a drain obligation. Stale completions cannot retire newer same-key tickets; consumed completions cannot lose replay wakeups. |
| Ordinary abort | Held managed abort releases an unpublished body/charge, preserves its assignment, and subsequent app retrieval supplies a replacement or gap-fill. Published abort is refused. |
| Reload accounting | Larger/smaller ordinary replacements are charged on installation; restored empty queues carry no old byte debt; refusal is explicit and never fabricated as unavailable history; the last valid assigned key can reload. |
| Operational lifetime | Nodes retire after successful contiguous admission; refusals retain initial work; priority Logon catch-up does not duplicate its first send; Wire references survive until physical completion without a history queue. |
| Archive | Generic archive reports each completed nonempty epoch/range once during live processing, including absent core nodes; no events from local completion or cold restore; warm hold/reconnect resumes unfinished chunks without repeating completed ranges; reentrant acknowledgement and batching preserve ordering. Process-failure recovery of the app's storage transition remains application-owned. |
| Application storage | Fixture apps demonstrate optional retention on publication, move/delete on archive, and asynchronous retrieval from long-term storage; deleting at archive results in later app-chosen gap-fill. These are app policies, not library storage requirements. |
| Connection phase | No managed Connecting transition/deadline; duplicate initiator claim emits no Logon; native transport readiness precedes FIX Logon timeout. |
| Readiness | No-gap Logon and completed recovery become Ready without another packet; actual Rx/Tx confirmation is required; assigned ordinary catch-up can resolve priority-Logon replay while Ready is false, without admitting new business; false initial Tx recovery and retained early Logout cannot transiently enable new business. |
| Liveness/drain | Ordinary probes do not block Ready; exact published echo establishes watermark; fresh drain proof remains required; stale/early/wrong echoes and renewed recovery cannot complete drain. |
| Epoch/reset | Empty operational state replaces history retirement; reset request assignment precedes its posted completion; all old tickets/output obligations drain before turnover. |
| Ownership/performance | Source review finds no replay-history container or added shared state, no replay-only queue-node allocation, and no alternate callback that bypasses app selection. |
| Naming | Final message, metadata, live-state and checkpoint names are consistent; network/engine aliases cannot shadow the Session or Transport namespaces; no compatibility aliases remain. |

Use the affected Zfix unit, native TCP/TLS integration and independent-peer entry
points with existing compatible dependencies on Linux x64. Rebuild only Zfix as
needed, preserving the current configuration. Broaden checks only for
changed behavior or actual failures; no dependency-module retest or fresh
performance measurement follows merely from this plan.

Completion requires all four actionable findings, the explicit retention/archive
decisions and the naming migration to be implemented, consumers/contracts migrated,
and current verification recorded within the authorized execution scope.
The initial source-only review is recorded separately from current runtime
acceptance. No previous passing gate or historical performance
measurement is evidence that these breaking repairs compile or run correctly.

## Naming conventions

These renames are part of the implementation scope. Use consistent vocabulary:
`Msg` means a message instance, `MT` identifies a message type, `Session` names
the administrative-message family, and `Link` owns protocol operation.

| Pre-repair | Final | Reason |
| --- | --- | --- |
| `ZfixMsgs.hh/.cc` | `ZfixSession.hh/.cc` | Groups administrative messages and their codec. |
| `ZfixSession.hh/.cc`, class `Session` | `ZfixLinkState.hh/.cc`, `LinkState` | Names the live link protocol state machine and frees Session for the message family. |
| Top-level administrative payloads and `Control` | `Zfix::Session::{Logon, Logout, …, Msg}` | Names the session-message family and its Msg representation explicitly. |
| `Heartbeat`, `TestRequest`, `ResendRequest`, `SequenceReset` | `HeartBt`, `TestReq`, `ResendReq`, `SeqReset` | Use concise protocol abbreviations. |
| `ZfixState.hh/.cc`, `SessionState` | `ZfixCheckpoint.hh/.cc`, `Checkpoint` | Distinguishes application-supplied restoration progress from live state. |
| `RxInput` | `RxMsg`, declared in `ZfixRxMsg.hh` | Names the actual received-message representation, backing and receive metadata. |
| `ZfixRxFrame.hh`, `RxFrame` | `ZfixRxFramer.hh`, `RxFramer` | Names a stream message extractor rather than a received-message object. |
| `ZfixTxFrame.hh/.cc` | `ZfixTxMsg.hh/.cc` | Groups outbound message construction; retain the distinction between TxBody and complete wire construction. |
| `Message<O,…>` | `MT<O,…>` | Describes a message type/mapping rather than holding a message. |
| `Catalog<…>` | `MTList<…>` | Names the compile-time collection of message-type declarations. |
| `Mode` | `Transport` | Identifies FIX4, FIX44A, FIXT11 and Raw session behavior. |
| `ZfixTransport.hh` | `ZfixNet.hh` | Names TCP/TLS bindings distinctly from FIX transport/profile selection. |
| `ZfixCtl.hh/.cc`, `SessionTx` | `ZfixLinkTx.hh/.cc`, `LinkTx` | Names the Tx-side link coordinator explicitly. |

Keep `Link`, `CliLink`, `SrvLink`, `Client` and `Server`: they already distinguish
the appropriate ownership and endpoint roles. Do not introduce an Engine wrapper.

Keep `ZfixDispatch.hh`, which parses, constructs and dispatches selected
messages. Use `MTIndex` for the MsgType/secondary-selector lookup itself only if
it is separately factored; do not rename the whole dispatcher to an index or
add another lookup layer to obtain that name.

Keep `ZfixFmt` for conversion and `ZfixFrame` for shared envelope/framing
facilities. Preserve `TxBody` versus complete wire-attempt construction. Moving
`RxMsg` declarations must preserve the standalone codec dependency boundary and
must not make Raw framing depend on managed link lifecycle state.

Resolve the associated names explicitly:

| Pre-repair | Final |
| --- | --- |
| `ControlCatalog` | `Session::MTs`, an alias of the existing compile-time `MTList` representation |
| `controlType(ZuCSpan)` | `Session::type(ZuCSpan)`, retaining its union-index result and zero sentinel |
| `controlMsgType(const Control &)` | `Session::msgType(const Msg &)`, retaining its wire MsgType result |
| `readControl`, `saveControl`, `writeControl` | `Session::read`, `Session::save`, `Session::write` |

Replace the former header-tuple `SessionId` with `HeaderConfig`, removing its
`qualifier` field. `SessionConfig.id` is now an opaque application `SessionString`
key, and `SessionConfig.header` holds the wire configuration. `TxConfig` uses
the same separation; `Checkpoint.id` owns only the application ID.
Keep the existing `SessionConfig`, `SessionString`, `SessionEvent`,
`SessionAction`, `SessionError`, `SessionFault`, `SessionTimer` and
`SessionDeadlines` names in namespace `Zfix`, declared with the live `LinkState`
component. They describe logical FIX-session configuration and runtime state;
do not move them into the administrative-payload namespace. Rename the existing
class `Session` to `LinkState`; the existing restoration record
`SessionState` separately becomes `Checkpoint`. Remove their obsolete members
as specified by the repairs, without introducing duplicate representations.

Rename the client/server internal network-base alias `Transport` to `NetBase`
and engine-base alias `Session` to `EngineBase`. Rename fixture namespace aliases
such as `namespace Transport = Ztcp/Ztls` to `Net`. These local aliases must not
hide the final `Zfix::Transport` or `Zfix::Session` namespaces. Keep the existing
enum wire/configuration values unchanged by this identifier migration.

These are breaking renames: update declarations, definitions, namespaces,
includes, guards, library source/header registration, heap identifiers where
appropriate, consumer type aliases, fixture filenames and documentation. Add no
compatibility aliases, forwarding headers or old/new parallel APIs. Move the
pre-repair Session implementation to LinkState before assigning its old filename
and bare identifier to the administrative message family; do not overwrite either
component during the migration.

The mapping applies to the original snapshot; earlier sections already use the
final names. Preserve their ownership, lifecycle and application-control contracts
through the migration.
Keep enum wire/configuration values independent of identifier renames. Historical
evidence retains the names of its captured source rather than being rewritten.

Include this naming migration in completion criteria: all affected Zfix consumers
and build/documentation references use the final vocabulary, while the codec,
message, live-state and checkpoint distinctions remain explicit. Rebuilds/tests
must use the current configuration and existing dependencies as authorized above.
