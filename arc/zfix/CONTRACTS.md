# Zfix contracts

These contracts implement the resolved design in `../zfix.md`, superseded by
`../zfix_repair.md` and `../zfix_repair2.md` where stated. Current authorized
Zfix build/runtime verification passes; see [STATUS.md](STATUS.md) for exact
source snapshots and scope. Protocol references are pinned below; the revised
plan uses the FIX specifications and zfix.md as its sources. No legacy source
or legacy callback signature is a dependency.

## State and ownership

The opaque application-configured `SessionConfig.id` is the durable logical
session key. It survives connections and is not a wire field.
`HeaderConfig header` separately holds version, local/peer CompIDs and optional
sub/location IDs; mode and application version remain separate configuration.
Incoming header identifiers have the opposite orientation and must match the
current connection's configuration. Applications choose any qualifiers encoded
in their ID; the library imposes neither a header tuple nor a routing policy.
Epoch distinguishes reuse of a sequence number following an agreed reset.
An application submission ID never reserves a wire sequence number.

Rx owns transitions, ZmPQRx, framing, retained input, receive-idle and timeout
timers. Tx owns ZmPQTx, new-message assignment, replay/archive, frame output and
send-idle timers. The application owns pacing, queues, correlations and optional
storage on its declared owner. Public dispatchers immediately post to the owner;
owner implementations use a trailing underscore. No cross-owner live reads.
Snapshots contain fixed metadata plus retained handles for variable storage.

Session deadlines use absolute times and share the owner's earliest physical
wake; gap retry and Tx idle use their dedicated scheduler timers. Closing
cancels ingress/reconnect/timers, drains Rx then Tx and hook continuations,
and only then releases owners. Epoch/connection tokens invalidate delayed work.
No rollback is promised for failed startup or failed object conversion.

## Managed transition table

The common table applies to `4`, `4.4A`, `T1.1`. Mode overlays below are part of
the contract, not switches on declared version. Rx transitions post Tx work with
a fixed session snapshot. `new` below means Tx selects a new control message from
the same sequence space as business work. Replay and gap-fill are separate.

| State/event | Guard | Next state | Sequence/output | Timers | Ordered hooks/actions |
| --- | --- | --- | --- | --- | --- |
| Disconnected/transport ready | initiator, attached and not stopping | Logon | preserve epoch/progress; prepare and send Logon | Logon deadline | outbound Logon preparation; session; stateChange |
| Disconnected/accept | acceptor | Accepting | no sequence allocation | initial Logon deadline | resolve the application-selected link; validate Logon |
| Accepting/initial Logon | matching version/identities, successful application admission | Logon | admit input; send new Logon response | Logon deadline | inbound admission; session; stateChange |
| Logon/peer Logon | matching identity/version, admission and reset negotiation complete | Established | reconcile both directions; hold regular traffic until recovered | receive-idle/send-idle | session; stateChange; schedule recovery |
| Established/business send | no drain, both directions recovered, limits allow | Established | body conversion; proposed new sequence; load continuation; commit; complete frame; publish | send-idle from output activity | enqueue/dequeue/load before publication; sent after completion |
| Established/in-order input | expected sequence, valid known values | Established | process once; advance Rx progress | receive-idle | typed callback or session; progress notifications |
| Logon or Established/high input | within read-ahead bound | same | ZmPQRx retains bounded input; ResendRequest for missing range | recovery/liveness remain live | directional recovery notification |
| Established/low input | valid replay metadata | same | suppress duplicate application delivery; never regress expected sequence | receive-idle | discard already processed PossDup input; other low sequences initiate Logout except SequenceReset reset mode |
| Established/ResendRequest | valid bounded/open-ended range | same | request app-selected content for every replay attempt; gap-fill unavailable ranges; new-send cursor independent | bounded owner continuations; liveness deadlines continue | session; reload; bounded replay continuation |
| Established/SequenceReset gap fill | advancing range, valid gap-fill header | same | advance only beyond current expected; drain contiguous retained input | recovery effects | session; directional recovery notification |
| Established/send idle on Tx | negotiated nonzero interval; no pending commands, association or unpublished output | same | new empty Heartbeat through shared sequence/association | move logical absolute deadline on transport admission; re-arm early callback; cancel at logout/close | load; sent |
| Established/receive idle | no outstanding probe | same | new TestRequest with opaque token | probe timeout | session |
| Established/matching Heartbeat | exact outstanding published TestReqID; both directions recovered for watermark | same | session acknowledgement through the captured exclusive publication boundary; no business result | cancel probe; renew receive-idle | session; ackd |
| Established/TestRequest | valid message | same | new Heartbeat echoing bytes exactly | normal liveness | session |
| Established/logout request | application requested | Logout | finish admitted work per drain contract; new Logout | Logout deadline | stateChange; session |
| Established/peer Logout | valid input | Closing | reply Logout if needed | bounded close deadline | session; stateChange |
| Logout/peer Logout | current connection | Closing | complete handshake | cancel Logout timer; close deadline | session; stateChange |
| Any live/transport failure or timeout | current token | Closing | preserve accepted unsent associations; no reset | cancel all connection timers | failure; unload where required; stateChange |
| Closing/physical close | current connection | Disconnected | preserve progress/epoch; begin owner/hook drain | cancel connection deadlines | stateChange; final stopped notification follows actual owner/I/O drain |
| Established/drain request | application reset policy | Established, draining | hold/refuse new admissions; finish old epoch and both directional recovery | drain deadline | drain status; no reset merely because Tx queue is empty |
| Drained/reset by reconnect | both directions proven drained | Closing then Logon/Accepting | prepare both owners under explicit agreement; next connection sends reset Logon | close/Logon deadlines | reset with loaded/acknowledged progress; stateChange |
| Drained/coordinated live reset | peer procedure supports and agrees | Logon then Established | reset sequence/epoch under coordinated procedure | reset/Logon deadline | reset; session; stateChange |
| Established/near sequence limit | configurable control headroom reached | same, app paused | stop new business work; control/recovery capacity remains | normal timers | limit notification; no wrapping |

### Mode overlays

| Mode | Initial/reconnect behavior | Additional synchronization | Transition fixtures |
| --- | --- | --- | --- |
| `4` | normal Logon and both-direction recovery; reset only by agreement | ordinary ResendRequest/replay | equal/high/low Logon; bidirectional gaps; reset admission |
| `4.4A` | common table with agreed 789 exchange | Logon snapshot of next expected input; inferred replay before new traffic | 789 equal/lower/higher than committed next output; missing/invalid 789; concurrent recovery |
| `T1.1` | common table and ordinary sequence recovery | require DefaultApplVerID in Logon; declared FIXT header values remain independent configuration | declared BeginString mismatch; missing application version; version/mode cross-product |
| `raw` | no managed state transitions | external manager supplies headers, sequences, admission, control and recovery | test separately with external manager; codec and byte transport retained |

### Error and synchronization decisions

These rules apply to all managed modes unless the mode overlay says otherwise.
They are centralized here for the fake-clock transition fixtures in stage 4.

| Event/guard | Sequence effect | Action and timer effect |
| --- | --- | --- |
| Invalid envelope, checksum or unsafe field boundary | no Rx advancement | report framing error; close transport without attempting unbounded resynchronization; cancel connection timers |
| Missing MsgSeqNum | no Rx advancement | Logout with diagnostic, then bounded close |
| In-order malformed recognized field, required field absent, duplicate or unsupported MsgType | consume the sequence once | Reject with RefSeqNum/tag/reason; continue; never typed-dispatch failed input |
| High sequence | retain within read-ahead bounds; do not process application input yet | request missing range; process protocol recovery traffic needed to avoid a mutual-resend deadlock, with one sequence disposition per frame |
| Low sequence without PossDup | no regression | Logout then bounded close, except valid reset-mode SequenceReset |
| PossDup without OrigSendingTime, or original time later than SendingTime | consume only when at expected sequence | Reject; already processed low duplicates never advance or dispatch |
| SendingTime outside configured UTC tolerance | consume in-order input | Reject then Logout/close; report timestamp error |
| Wrong identity after attachment | consume only in-order input | Reject then Logout/close; never dispatch |
| First message not Logon or unknown identity | no session mutation | refuse establishment and close without consuming a shared session sequence |
| Application refuses Logon after identity resolution | no establishment | send diagnostic Logout where attachment allows ordered output; close; cancel admission/Logon deadline |
| Unknown tag | no additional effect | skip value without semantic validation; framing remains checked |
| Gap fill at expected sequence, NewSeqNo greater than expected | advance expected to NewSeqNo | drain contiguous read-ahead; recompute remaining gap |
| Gap fill below expected with PossDup | never regress expected | discard stale duplicate; request any gap that remains; no application delivery |
| Gap fill with NewSeqNo not advancing its own sequence | consume only an in-order invalid message | Reject; preserve existing recovery requirement |
| SequenceReset in reset mode | disregard its header sequence; never lower expected | greater NewSeqNo advances; equal is a no-op; lower is rejected; this does not establish a new epoch |
| `4.4A` Logon 789 equal to current local Logon sequence or its exclusive boundary | no inferred historical replay | continue normal recovery |
| `4.4A` Logon 789 below current local Logon sequence | preserve new-message cursor | replay [789, local Logon sequence); no redundant gap request from Logon sequence alone |
| `4.4A` Logon 789 above Tx snapshot next, absent or zero | no fabricated progress | Logout/close with synchronization error |
| `T1.1` Logon missing declared application version | no establishment | Reject/Logout and close |

The exclusive Tx snapshot used to validate/acknowledge 789 is taken after the
local Logon assignment, before later new traffic can change the comparison.
Automatic recovery covers only historical assignments before that Logon; the
peer's earlier snapshot cannot establish that this newly generated Logon was
missed. Explicit ResendRequest still uses the admitted bound and can include the
current Logon. Queued new business resumes only
when both directions have confirmed actual recovery. No additional TestRequest
round trip gates ordinary readiness. Administrative replies and
replay remain operational while regular traffic is held. Optional application/liveness probes provide bounded session watermarks; explicit
drain still requires a fresh published probe/echo, never business acknowledgement.
Already-assigned ordinary catch-up is also permitted before Ready; new
unsequenced business remains gated. Pending replay above the contiguous cursor
can therefore become eligible while both owners are still recovering.

A live reset is accepted only after the application's agreed drain procedure,
with no unresolved directional recovery or association continuations. The reset
initiator sends Logon 141=Y at sequence 1; the acceptor replies likewise; the
resulting next values are 2. Refusal closes without silently abandoning old work.
Disconnect/reconnect reset follows the same explicit epoch agreement. Ordinary
SequenceReset gap-fill/recovery never resets epoch.

## Hook contracts

Hook values are borrowed during the call unless a retained handle is explicitly
transferred. Deferred work must retain payload backing. An enclosing receive
context retains input once; fields do not each acquire a reference. Association
and reject callbacks go to the same application owner in posting order. Refusal
and failures are explicit results; asynchronous hooks resume through one-shot
continuations posted back to Tx/Rx, guarded by epoch and connection token.
There is no blocking I/O-thread store operation or mandatory persistence barrier.

| Hook | Arguments/context | Owner and ordering | Refusal/failure and continuation |
| --- | --- | --- | --- |
| enqueue | owned business payload, application queue ID, admission context | application owner, before dequeue; no wire sequence | refuse before admission; accepted ownership persists until disposition |
| dequeue | current session/epoch and readiness | application owner to Tx by retained candidate | empty result pauses; candidate has no reserved wire sequence |
| load | exact candidate or administrative type, proposed sequence, session/epoch | Tx dispatches association to application owner before commit/publication | refusal leaves committed cursor unchanged; async acceptance resumes Tx; later new sends cannot overtake committed association |
| reload | requested sequence, acknowledged state, session/epoch and replay flag | Tx requests application-selected content and required original SendingTime | app may suppress, reprice or replace type/ID; unavailable drives gap-fill; delayed retrieval uses a one-shot ticket |
| unload | candidate, attempted sequence, captured epoch/context | original association owner, stale accepted load before commit | async rollback drains before another candidate; committed assignments remain intact |
| sent | candidate/type, sequence, epoch, local completion | application owner after local send completion | notification only; not peer/business acknowledgement |
| ackd | sequence progress and epoch | application owner after verified session progress | notification only; no numeric TestReqID inference or implied retention policy |
| reject | RefSeqNum, reference tag/type/reason/text and epoch | association owner after prior load association | application chooses business consequence; no library correlation registry |
| stateChange | monotonic change sequence, state, flags, epoch | Rx transition, ordered notification to application owner | notification; stale epoch callbacks cannot affect new state |
| reset | old/new epoch and acknowledged/loaded progress | after both directions drained and reset agreed | application determines queued unsequenced work policy; no invented business outcome |
| session | incoming/outgoing typed control and context | Rx input or Tx output, ordered with corresponding processing | observe borrowed data or retain context; raw manager owns its own control handling |
| admission | first Logon, identity, transport context | Rx to application owner before active attachment | accept/refuse asynchronously under Logon deadline; displacement does not wait for old admission |
| prepareLogon | encoded outgoing Logon body | synchronous Tx replacement before association/publication | preserve managed fields; application supplies optional auth; no mandatory FIX auth |

Selective restoration accepts independently supplied values with an explicit
unset sentinel: received/processed progress on Rx; loaded/sent/acknowledged
progress and pending resend state on Tx. Restore before session activity resumes.
The restoration record carries session identity and epoch plus independently
optional Rx received/processed cursors, gap bounds, Tx loaded/sent/acknowledged
cursors and pending resend bounds. Omission uses a presence mask, since zero
can be meaningful for progress. Apply Rx and Tx portions on their owners while
activity is stopped; reject inconsistent cursor/range combinations explicitly.
Notifications carry the same epoch and retained message identity as load.

## Limits

Bounds are checked at untrusted ingress/admission, before retaining or posting
more work. HubLimits::link supplies defaults to new links; explicit LinkLimits
replaces that snapshot per link. HubLimits::submit configures the
hub aggregate submission budget; ::admin independently bounds managed Rx-to-Tx
administrative handoffs. Standalone callers use
`Fmt::Limits` or override directly. Defaults below are initial workload choices,
not measured performance budgets. Enforcement tests are required before claiming
any bound is implemented.

| Name | Default | Scope/enforcement |
| --- | --- | --- |
| frameBytes | 1 MiB | message; declared BodyLength plus envelope bounded before retention |
| fieldBytes | 256 KiB | message; known scalar/binary value span |
| groupEntries | 65,536 | message; derived/parsed group count and cumulative retained entries |
| groupDepth | 16 | message; nested group recursion |
| parseBytes | 4 MiB | message; root and group field-position records |
| readAheadCount | 4,096 frames | session; queued ZmPQRx entries, initial roughly 1 KiB small-order backing workload; hub `link.rx.readAheadCount` |
| readAheadBytes | 4 × default frameBytes | session; allocated backing plus ZmPQRx node overhead, conservatively charged per retained frame |
| work.count / work.bytes | 4,096 operations / 4 MiB | AppRx-owned per-link `link.work` and aggregate hub `work`; retained native input, work node, retirement envelope and declared application extras; live limits cannot undercut usage |
| postedAdminBytes | 4 × default frameBytes | session and hub; managed Rx-to-Tx lifecycle/control/replay/Reject work plus pending credit returns, reserved before posting; independent of business submissions |
| postedSubmitBytes | 4 × frameBytes | session and hub; AppRx-to-Tx retained submissions, reserved before posting |
| queuedOutputBytes | 16 × frameBytes | session; admitted unsent output |
| adminReserveBytes | frameBytes | session; bounded control traffic independent of business pause |
| controlQueuedBytes | frameBytes | session; allocated unsequenced control bodies plus rounded command nodes, including selected load continuations; `ControlLimits::queuedBytes` |
| replayBatch | 64 messages | scheduling turn; yield to other work at bound |
| traceRx / traceTx | disabled (`bytes=0`); 4 KiB record excerpt | owner/session; rotate pooled node/buffer charges within configured budget; retained observer references are application-owned |
| sequenceLimit | 2,147,483,647 | session; explicit peer/application override, never wrap |
| controlHeadroom | 1,024 sequences | session; notify and stop new application transmission before exhaustion |

Timeout defaults are operational choices in SessionConfig: Logon/admission
10 seconds, Logout acknowledgement 2 seconds, drain 30
seconds, SendingTime tolerance 120 seconds. Send-idle uses negotiated HeartBtInt;
receive-idle starts a probe after 1.2 times that interval, with a probe deadline
1.2 intervals after issue. HeartBtInt=0 disables periodic idle traffic but not
explicit operator probes. Deadlines use an injected clock in unit tests and
absolute owner-shard timers in transport integration.

## Conformance matrix

| Reference/profile | Pinned edition/dictionary | Required evidence |
| --- | --- | --- |
| FIX TagValue | [Version 1.0, June 2020](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_TagValue_Encoding_v1.0_June_2020.pdf); sections 4–6 inspected | framing byte fixtures, length/data pairs, ordering, checksum/BodyLength |
| FIX session | [June 2020](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Session_Layer_June_2020.pdf), sections 4–5 | transitions and error exits with fake clock/transport/hooks |
| FIX session test cases | [Version 1.0, June 2020](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Session_Testcases_June_2020.pdf), retrieved 2026-10-26 | section mapping below; planned coverage, no execution evidence yet |
| `4` | session specification sections 4, 5.2; zfix.md Q001 | common transitions, invalid input and ordinary recovery |
| `4.4A` | session specification section 4.4.1; explicit 789 agreement | equal/lower/higher/missing 789 and overlapping recovery |
| `T1.1` | session specification section 5.3; zfix.md Q001 | independent declared version and application-version population/matching |
| raw | external manager fixture | complete managed bypass, retained codec/framing |
| canonical interoperability | FIX 4.4; QuickFIX release and dictionary hashes pending fixture pin | all four TCP+TLS pairings, both mixed roles, independent parser/session |
| application dictionaries | explicitly chosen by each catalog/fixture | custom tag/group dictionary; no BeginString schema selection |

No venue certification or complete venue adapters are first-release requirements.

Downloaded and inspected the pinned PDFs on 2026-10-26. SHA-256:

- Session: `661c6bd90db9d67a050f0cd58230896d5d22f9448a942a8453d3595d0455f655`
- TagValue: `8d9793e05258c45950a35be458017ed6c02bdf0fb08d348848f335c1a27d2083`

The supplied BeginString is matched verbatim and never selects a business schema.
The revised plan explicitly separates declared version from managed mode; this
configuration rule governs the pairing rather than automatic profile selection.
Protocol fixtures include that deliberate distinction.

### Independent session-test mapping

The retrieved test-case edition supplies these references for future fixtures.
This mapping supplements the implementation plan and is not an executed
conformance claim.

| Sections | Planned gate |
| --- | --- |
| 4.3–4.4 | 4/6: initiator/acceptor admission, duplicate identity, initial message |
| 4.5.1–4.5.2 | 3/4: envelope, identity, sequence and timestamp failures |
| 4.5.3–4.5.7 | 4: heartbeat, probe, reject, replay |
| 4.5.8–4.5.10 | 4/5: synchronization, gap-fill versus reset |
| 4.5.11–4.5.12 | 4/6: Logout and bounded close |
| 4.5.13–4.5.14 | 2/4/8: typed failures, groups, alternate ordering |
| 4.5.15 | 5: disconnected backlog and current SendingTime |
| 4.5.16 | 8: application-selected signature/encryption coverage; TLS stays in Ztls |
| 4.5.17–4.5.18 | 7/8: explicit addressing dictionary and application duplicate policy |
| 4.5.19 | 4/5: simultaneous directional recovery |

Resolved design decisions govern differences from the reference cases:

- Q014 skips unknown tags, unlike 4.5.13(a,c).
- Q005 converts empty values normally, unlike the blanket rejection in 4.5.13(d).
- Q008/Q011 leave BusinessMessageReject/application policy outside built-ins.
- Q018 selects deferred sequence assignment from 4.5.15's alternatives.

Record these differences in the independent peer's directional expectations.
Use the stale-gap-fill rule in the transition table and test overlapping gaps
independently against Q010. Do not treat a session
SequenceReset as permission to discard application results or change epoch.

## Complete-frame output storage choice

The standalone writer currently reserves the largest feasible length-prefix
width for the caller's bounded capacity, encodes the body once, then compacts
only if the actual BodyLength uses fewer digits. Its capacity calculation solves
`body bytes + decimal width <= capacity - fixed envelope bytes`, including the
one-octet gaps at digit transitions. Tests cover exact capacity, one byte short
and eleven capacities per body length through the 10/100/1000 transitions.

| Strategy | Body conversions | Payload copies/moves | Other constraints |
| --- | --- | --- | --- |
| Measure then emit | 2 | 0 | Getters/custom printers run twice; fallible conversion is repeated. |
| Reserved headroom | 1 | 0 or 1 in-place move | Move only when reserved/actual digit widths differ; no extra body buffer. |

Headroom is selected to avoid a second metadata/conversion pass and repeated
application getters. This is an operation/copy comparison, not a measured
throughput claim. Stage 9 must measure codec/frame cost with representative
workloads and capacities. The writer never publishes to a transport; pooled
transport queue admission/publication and flush-failure handling are implemented
in the source. Earlier Stage 6 results do not verify the current repair.


The pooled Tx path uses the same writer with a growth callback. Growth preserves
written bytes; the returned frame may start after unused envelope headroom, so
ZiIOBuf `skip`/`length` describe the published bytes. It does not compact output.
A const pooled `TxBody` retains original MsgType/body and application identity;
separate output storage permits in-place transport encryption and partial writes.
There is one explicit body copy per output attempt, independently of the metadata
conversion count. This choice requires measurement in stage 9.

`Tx<Impl>` implements load continuation tickets and exact candidate context.
A stale accepted load is explicitly unloaded on its original association owner;
a pending unload blocks further selection until its Tx completion. The internal
sequence callback names remain separate from the external scheduling/archive
hooks (`txPost_`, `historyArchive_`), avoiding inherited callback recursion.
Pending-load and transport-completion lifetimes must be drained by the enclosing
endpoint before destroying its owner.


## Reconnect of loaded, unpublished output

Transport admission establishes the published boundary even if local writes are
still partial. Published frames keep their number and original timestamp for
normal reconnect recovery. The loaded boundary also includes assignments accepted
by the application but never admitted by transport. Reconnection preserves those
assignments and their queue nodes; it does not decide business cancellation.

An application can explicitly abort an unpublished queued item while publication
is held. Ordinary queue deletion releases its retained byte charge once and leaves
the assigned range intact. Assigning 101 and 102 leaves next=103 after either or
both nodes are removed. The application supplies missing content or permits the
existing gap callback to encode a GapFill. The library records no cancellation
reason and does not distinguish canceled content from other unavailable history.
No sequence number becomes reusable before an agreed epoch/reset transition.

A pending lookup pauses its output operation until the normal continuation
completes. Ordinary missing-send and direct replay share one outstanding lookup
obligation. Only ordinary send retains its missing range for the generic scan;
replay coalesces immediate unavailable results within its current bounded turn
and asks the app again on a later attempt. Both scans yield in configured
batches. A Zfix-only queue view caps the logical
tail seen by each new-send turn without changing the real queue's storage or tail.
Acceptance of an incomplete turn posts another continuation.

Reconnect assigns its Logon at the continuing next sequence and admits it before
ordinary catch-up/replay. Its retained output's existing connection and original
SendingTime identify that admission when the normal send cursor later reaches it;
it must not publish the same Logon twice. Old operational connection controls
use ordinary gap filling during catch-up. Business retains its original assignment.
A refused Logon exposes no recovery frames before a successful retry.

An admitted high Logon can cause an immediate peer ResendRequest while the
contiguous new-send cursor still waits for older application history. One
Tx-owned exclusive actual-admission high-water Key bounds that request; assigned
but unadmitted work cannot enlarge an open-ended request. The accepted interval
stays in the existing pending replay Span until ordinary publication reaches it.
Only its already published prefix enters bounded replay. Successful normal
message/GapFill progress wakes the remaining prefix. Pending future replay keeps
recovery incomplete; it is neither dropped nor reported complete during lookup.
This high-water value never advances acknowledgement or the contiguous cursor.

`unloadTx_`/`unloaded_` apply to an accepted load that becomes stale before sequence
commit. That rollback preserves the original body and attempted context, and must
drain before another candidate is selected. It does not retract committed work.
There is no reverse-unwind phase or completion notice.

## Control-command and publication barrier

Rx creates owned control bodies from lifecycle snapshots. It copies variable
echo text into pooled storage before returning from the input callback. Tx owns
the bounded unsequenced command list and the load continuation; selected commands
retain their byte charge until that continuation resolves. Closing discards old
unsequenced internal controls but does not silently release an accepted mapping.
Replacement Logon is assigned at the unchanged next sequence and admitted before
normal catch-up. Replay on the new connection waits for successful Logon
transport admission, including retries.

Transport admission reserves complete output without exposing bytes. Tx queues
fixed publication metadata to Rx before flushing transport. FIFO owner posts
ensure a response cannot establish readiness before publication is observed.
Logon notices carry its assigned sequence and exclusive published boundary;
probe notices additionally carry the opaque serial used to construct TestReqID.
A matching Heartbeat observed before probe publication cannot synchronize the
session. Publication starts the response deadline once; stale/duplicate notices
cannot reset it.

Local completion is separate from publication and peer progress. A one-shot
completion notifies the application with the original body/context and updates
the current connection's in-flight count. Old-connection completions cannot alter
new-connection counters. The endpoint must drain old transport operations before
releasing their owner. No sequence or business acknowledgement is inferred.

Close-after-output waits for commands, selection, recovery and in-flight output.
The resulting drained notice authorizes Rx to request transport close; actual
transport closure drives Disconnected. Rx continues ordered input/controls while
waiting in Logout, including peer acknowledgement and preceding in-flight
business messages. Closing holds sequencing. Command admission failure never
masquerades as successful Logout publication/drain.


## Startup restoration record

`Checkpoint.id` owns the application's `SessionString` ID; the record also owns
epoch and `StateField` masks. `stateMatches` and `stampState` accept `ZuCSpan`
IDs. Identity matching uses only the nonempty application ID and epoch, not
wire headers, mode or version; mask, range and sequence checks remain explicit.
A fresh owner may restore
the same ID/epoch with changed header configuration; current connection headers
still undergo normal validation. An empty record is stamped once; a conflicting
ID is rejected even when no progress fields are selected.
Unused record values are ignored. Selected invalid values return failure before
owner cursors change. Restoration is confined to a fresh owner before its first
connection; an old record cannot rewind an active epoch after disconnect.

| Mask | Record field(s) | Meaning |
| --- | --- | --- |
| RxReceived | rx.received | Exclusive known logical queue boundary. |
| RxProcessed | rx.processed | Next expected sequence. |
| RxGap | rx.gapBegin, rx.gapEnd | Previously requested exclusive range; zero pair clears request metadata. |
| TxLoaded | tx.loaded | Next unassigned sequence, including accepted unpublished associations. |
| TxSent | tx.sent | Contiguous new-send cursor; a priority Logon can already be admitted above it. |
| TxAckd | tx.acknowledged | Independently verified exclusive acknowledgement. |
| TxReplay | tx.replayBegin, tx.replayEnd | Accepted pending replay, possibly awaiting normal publication; zero pair clears it. |

Rx enforces `1 <= processed <= received <= sequenceLimit + 1` and valid requested
ranges within those bounds. Tx enforces `1 <= acknowledged <= sent <= loaded <=
sequenceLimit + 1` and replay ranges within loaded progress. Empty restored
queues have zero physical charge. A missing assigned range
may have loaded greater than sent with zero retained payload charge. Cursor zero is
invalid; a zero range pair explicitly clears staged recovery.

Snapshots capture metadata, not payloads or an application-store transaction.
Rx restoration keeps its exclusive received watermark in a Zfix-only queue
adapter without modifying the physical queue tail or allocating payload nodes.
The first `queue.gap()` controls recovery. A saved request is notification metadata:
restoring received=10, processed=5 and requested=[7,9) preserves that held snapshot,
then resume advertises [5,10) with [7,9) as its previous range. Observe policy leaves
retry scheduling on Rx; stale retry/dequeue tickets remain inert until owner drain.
Tx restoration stages resend work for existing bounded replay/retrieval. A saved
range may extend above sent through loaded: it was accepted while an admitted
priority Logon exposed an older gap. Restore retains that request, initializes
the actual-admission high water to sent, and waits for ordinary publication
before submitting its future prefix for replay. It does not infer peer progress
or restore nonexistent payload nodes. A sparse
startup amendment preserves omitted loaded/sent/range values. A running or
retained queue/recovery operation is not silently discarded by restoration.

Transfer record ownership between owners for sequential snapshot/restore work.
Both sections must be restored successfully before dependent activity resumes;
the enclosing endpoint supplies that barrier. Original body/timestamp storage and
consistency with accepted application associations remain application policy.

## Tx idle timer

Rx includes negotiated HeartBtInt in every fixed `SessionEvent`; Tx consumes it
on establishment without reading the Rx owner. `nowTx_` and the dedicated
`idleArmTx_`/`idleCancelTx_` hooks use one monotonic clock and an absolute deadline.
Callbacks carry connection plus one-shot ticket and invoke `LinkTx::idle_` on
Tx. The endpoint must cancel its `ZmScheduler::Timer` and drain late callbacks
on Tx before releasing the owner.

All successfully admitted output, including replay/gap-fill output, moves the
logical deadline. Physical timers retain the earlier wakeup until it fires;
early callbacks re-arm, avoiding scheduler churn per message. Pending commands,
association or unpublished sequence work suppress additional periodic commands.
HeartBtInt zero disables the timer; logout, close and coordinator failure cancel
it. Output completion is separate from this admission boundary and never implies
peer acknowledgement. Fake-clock/owner tests cover these rules; native session
and timer-drain integration fixtures verify concrete socket/scheduler teardown.

## Verified session acknowledgement

A locally generated TestRequest retains its opaque ID and actual publication
sequence only after the matching one-shot Tx notice. When its exact response
arrives with both directional recovery states complete, Session emits `Ackd`
with the exclusive boundary (probe sequence plus one). Incoming arbitrary IDs
are never interpreted as numbers. Early high-sequence responses, unresolved
recovery, pre-publication or stale notices and duplicate responses cannot advance
this boundary. TestRequest itself waits for ordered receive processing before
an echo, consistent with Session Layer 2020 section 4.8.8's all-other-messages
rule. Heartbeat liveness and ResendRequest deadlock avoidance retain their early
paths; neither bypasses the watermark recovery guards.

Mode 4.4A's validated Logon NextExpectedMsgSeqNum supplies explicit peer progress
bounded by the Logon publication snapshot, independently of any remaining replay.
Tx validates all supplied acknowledgement against its published cursor. Its
optional `ackdTx_` callback (or LinkTx's `acknowledgedTx_`) carries owner context
with seqNo zero plus previous/new exclusive cursors, once per monotone change.
The application posts this notification to its association owner in hook order.
Archive availability/release remains application-selected; acknowledgement is
session progress and never an invented business result or socket completion.

## Drain frontier protocol

The Rx drain request supplies a nonzero per-session ticket and pauses business
readiness. Tx holds subsequent candidates, resolves an outstanding load decision,
finishes committed publication and local completion, and waits for active replay,
retrieval. It then posts one fixed `DrainBoundary` snapshot with the
exclusive loaded/published cursor and drain ticket. A refused asynchronous load
must also evaluate completion because it produces no send task.

Rx accepts only the current epoch/connection/ticket boundary. It waits for receive
and replay recovery, then starts a fresh synchronization probe whose publication
boundary is at or beyond that frontier. An exact response establishes session
acknowledgement and emits `Drained`, while keeping business admission held. An
older probe cannot prove a newer frontier. Recovery invalidates the proof and
starts a fresh bounded wait. Both payload count and logical received/processed
cursors participate in receive recovery; a metadata-only restored gap is pending
work. Later operational controls can lie beyond the original frontier.

A dedicated absolute drain deadline uses configurable `drainTimeout`, default
30 seconds. Expiry preserves old-epoch state and closes the connection. This
protocol proves admitted session-frontier processing, not application business
completion or archive durability. Reset agreement, final owner preparation and
new-epoch execution remain separate required operations. Neither the Tx boundary
notice nor its local completion can by itself authorize reset.

## Epoch checks at asynchronous boundaries

Managed acknowledgements, archive completions and replay requests carry the epoch
captured with their original work. LinkTx replay/acknowledgement additionally
checks the physical connection. A matching connection alone is insufficient for
live reset: the new epoch reuses that connection and starts sequence numbers at
one. A stale epoch is refused before queue mutation or progress notification.
The generic sequence queues remain epoch-independent.

The core Tx turnover primitive accepts explicit old/new epochs only with business
readiness disabled, completed core archive work and an empty operational queue.
It does not discard retained business or operational-control nodes, unresolved
retrieval or unsent metadata. The optional reset notification carries old owner
context, new epoch and the old exclusive progress snapshot. Continuation ticket
and physical connection counters remain monotone across the change. Applications
choose archive/store policy; no app storage-completion barrier is imposed.

This is an owner execution primitive, not permission to bypass the negotiated
reset/drain/transport preparation protocol. Coordinator-level live/reconnect reset uses explicit preparation and operational
queue quiescence as described below. Receive queue
reset also requires a hold and equal logical received/processed cursors so a
metadata-only gap cannot be silently discarded.

## Agreed operational-control retirement

Contiguous new-send admission automatically releases its exact operational node.
Priority Logon retains its operational marker only until contiguous catch-up.
Wire owns admitted output until physical completion. None may supply replay.
An agreed idle epoch transition requires an empty operational queue, completed
core archive work, no pending lookup/association and drained output. It does not
scan history or wait for application payload release/storage completion. Preserve
the posted reset-request completion and captured epoch/ticket checks.

## Coordinated reset preparation and activation

An application-authorized reset consumes a current drain proof on each peer.
`prepareReset_` carries captured connection/old epoch, increasing next epoch,
agreed driver and `ResetPath`. It freezes business selection and ingress; the
caller must establish physical ingress quiescence and counterparty agreement.
Local completion, a received reset flag or an empty queue supplies neither.

Tx waits for prior output, commands, pending association/retrieval and core
archive work, then checks its operational queue is empty. Application history
does not need to be released for the core epoch transition. Keyed `ResetReady` completion reports new Tx
cursors at one; Rx validates old connection/epoch, request and drain identity,
resets its held gap-free queue, and commits that same next epoch. Optional
`epochTx_` and `epochRx_` callbacks report actual old owner progress. No new-epoch
wire Logon is published during this preparation phase. Applications can snapshot
both committed owners and choose persistence before activation.

`startReset_` activates prepared live reset; an agreed driver sends 141=Y/34=1,
and its peer requires those values and replies likewise. Resetting ingress returns
Blocked without consuming/stamping the frame; the transport retains/retries it on
RetryInput. Live reset retains admission. `disconnectReset_` instead closes and
arms the next physical open once; reconnect repeats ordinary inbound admission
and uses the physical initiator role. Ordinary reconnect has no reset authority.
Actual Rx and Tx recovery confirmations gate ordinary readiness; no extra probe does.
Mode 4.4A reset validates 789 without requesting an obsolete replay interval.

Close/timeout cancels an uncommitted preparation. If Tx already committed, its
old-context keyed completion must still commit the closed Rx owner, without
publishing a reset Logon. Open is refused until pending local resolution finishes.
Epoch rollback is forbidden. A stale old-epoch close cannot close active new-epoch
work. Application policy must reconcile peer uncertainty after a failed partial
handshake. Unprepared reset flags still require authorization (`NeedReset`); fresh-owner
authorization is an explicit operation described below. Fake complete-
frame owner tests do not establish concrete transport quiescence.

Replay results are never cached or inserted into the operational queue. Every
attempt requests the app and drops its result after that attempt. Deferred results
arriving held/closing retire the matching obligation without publication; retry
requests the app again. Original in-flight output cannot supply resend content.


## Sequence headroom notification

Entering `next > maximum - controlHeadroom` emits a one-shot Tx-owner policy
notification per epoch: core `limitTx_`, coordinator `sequenceLimitTx_`. Arguments
are a captured next-sequence context, configured maximum and reserved count.
Association must have committed before a newly assigned message can trigger it;
refused/pending loads cannot manufacture progress. Restored near-limit progress
notifies on attachment, after derived/application state exists. Reconnect and
replay do not rearm; committed epoch turnover does. Post work to the application
owner in association order; no recursive queue mutation from the callback.

Business admission stops at the same boundary while controls can consume the
remaining configured headroom. Reaching the absolute maximum prevents every new
assignment; replay keeps original numbering. Zero headroom explicitly reserves
nothing. The engine never silently wraps or automatically changes epoch/calendar.


## Initial reset authorization

An application can explicitly authorize a reset Logon before a fresh owner's
first open using `Rx::resetOnOpen_(capturedEpoch)`. It requires empty receive
state at one; armed authorization refuses restoration of advanced receive state.
A false second argument cancels before open. Session identity/configuration and
epoch are validated; raw mode and owners with any prior connection refuse it.
This does not change epoch or retire anything. Applications select the fresh epoch
and resolve old business history externally before constructing such an owner.

The opening snapshot arms Tx only if its independent cursors are all one, there
are no bytes, retained nodes, replay or pending association/history work,
and nextEpoch equals its own epoch. Refusal leaves attachment and sequence state
unchanged, with no wire publication; the enclosing endpoint handles opening
failure. Both directions still require 141=Y/34=1 and normal admission. Open
consumes the authorization once, including subsequent failed Logon. Native
setup failure before transport readiness does not reach LinkState::open_ and leaves
the authorization pending. Later
ordinary reconnect preserves epoch/cursors with no implicit reset flag. This
operation cannot authorize incoming reset on a continuing session.


## Application queue selection hook ordering

`Hooks<Impl>` supplies optional Tx-local enqueue/dequeue/selection defaults.
`LinkTx::enqueue_` hands an immutable unsequenced body to application-bounded
storage; only acceptance sets the queue wake flag. It rejects null/admin bodies.
No submission-time wire context is captured. Separate application owners hand
prepared candidates to Tx through retained handles rather than sharing live state.

The owner pump prioritizes controls, requires business readiness and obtains at
most one `dequeueTx_` body per task. Empty output pauses until `queued_`; readiness
and connection changes retain an announced backlog. Pending load retains that
single candidate and blocks later assignment. After synchronous result or final
asynchronous completion, `selectedTx_` receives the body, captured proposed
context and final result. Only Accepted confirms committed association, before
publication. Non-Accepted results leave numbering unchanged and pause dequeue;
the application must retain/reject/requeue the returned body and explicitly wake
when retry is appropriate. Refusal is not a fabricated business outcome.

A late accepted load after disconnect performs unload with its old context before
returning the candidate refused. Committed work keeps the core Tx lifecycle;
unselected queue entries stay under application policy across reconnect/reset.
Callbacks run on Tx in association order. Post continuations to another owner and
do not recursively mutate sequence state. Default enqueue refuses/dequeue is
empty/selection is no-op; applications enabling dequeue must handle its final
selection callback. No library application queue, hidden durable store or
mandatory retention policy is introduced.


## Ordered sequence-keyed peer Reject notification

After valid ordered Reject processing and the Rx session observer, optional
`peerRejectRx_` receives a retained `PeerReject` record. It includes incoming
context, parsed reference/tag/reason/text, PossDup, original wire view and one
const pooled backing. All borrowed spans remain valid through that backing;
separate text/header copies and per-field retains are unnecessary. Invalid controls
do not reach this hook; high input waits for recovery and low duplicate replay
cannot repeat application notification.

The endpoint posts this record in order to the association owner. On Tx,
`peerReject_` checks captured epoch, nonzero incoming/reference sequences, backing
and source connection not newer than the current one, then calls `rejectedTx_`.
A prior physical connection within this epoch can have supplied retained input.
Its source context does not identify the original outbound connection: the
application resolves reference sequence within the epoch using its load mapping.
Unknown/released references still reach application policy. No library registry,
acknowledgement advance or business-result inference is added. A default no-op
permits applications that deliberately ignore session Rejects; enabled endpoints
must provide ordered association-owner routing and teardown draining.


## State-change and outbound session observation

Rx invokes optional `stateChangeRx_` for Changed events before general event
routing. The immutable fixed event snapshot carries the monotone change sequence,
instance, state/actions, epoch and connection. Timer refreshes, ignored/stale
callbacks and raw bypass cannot create a state-change notification. Observers
post to other owners and must not recursively mutate this lifecycle owner.

After successful administrative frame admission and publication-notice enqueue,
Tx invokes optional `sessionTx_` with the retained immutable body and assigned
context, before transport flush. Failure before admission creates no notification;
completion does not repeat it. App-suppressed replay content reports the actual
SequenceReset gap fill; an app-supplied administrative body reports that actual
transmitted type/content and carries replay context. Business bodies use their
separate association/sent hooks. The publication cursor advances after the frame hook returns; callback
context supplies the admitted sequence without off-owner state reads.


## Partial writes and transport completion identity

The transport retains each complete admitted frame, its body and assigned context
until one local completion. Offset advances and zero-progress/writable callbacks
cannot reassign the sequence or acknowledge peer progress. Each completion needs
a one-shot identity distinct from epoch/connection/sequence: repeated replay can
reuse all three, including the same replay flag. A stale callback must not complete
another queued frame or decrement its in-flight accounting.

An ordered Tx fault snapshot reaches Rx through `outputFailed_(connection, epoch)`.
Current output failure closes and cancels timers; stale epoch/connection failures
are inert. All transport-admitted frames must complete once during failure or
teardown. Every admitted sequence remains published for FIX recovery, including
later queued frames with zero physical bytes written. The application may observe
failed local completion even when the peer received the entire frame. Replay
retains original body, sequence and OrigSendingTime and must not dispatch already
processed input again. Unpublished accepted work retains its assigned number and
application association through reconnect. An explicit application abort removes
only its payload node; missing-range retrieval or gap filling keeps numbering
monotone.

Ztcp uses ZiTx's offset/size loop for physical partial writes. Its failure path
cleans later queued buffers after reporting the failing head, so a concrete FIX
endpoint must account for/drain every admitted FIX frame; it cannot assume one
ZiTx sent callback will arrive for every cleaned buffer. The concrete adapter
drains its admitted-frame records through physical close. The fake stream's
bounded identified frame queue verifies completion/recovery separately from
native short-write/EAGAIN integration, without adding a library byte queue.

### Endpoint publication metadata

After frame admission, publication-notice enqueue and the administrative session
observer, `LinkTx` invokes `publishedTx_(body, context, originalTime)` for
all original, replayed and generated GapFill frames, then flushes transport.
`Link` forwards this to `fixPublishedTx_`. The immutable body handle is moved
to the observer, so the default adds no reference-copy churn. Refused admission
produces no publication callback. `originalTime` is the first SendingTime for
business replay and the current original timestamp for a generated GapFill.
Applications may retain metadata/history or post ordered storage work; this is
not a built-in store or a mandatory commit-before-send boundary. The core's
publication cursor advances after the callback returns. Observers use explicit
context and must not reenter publication inline. Local completion and independent
peer acknowledgement remain separate events.


### Production raw endpoint composition

`Raw<Impl>` is a separate compile-time framing/output engine selected by
RawClient/RawServer and RawCliLink/RawSrvLink. It instantiates no managed LinkState,
sequence queue or FIX timer. External manager hooks own all session policy;
complete frame buffers and native body/tracking handles cross to Tx. Admission
retains the generation and bounded output checks while allowing externally
assigned zero epoch/sequence metadata. Paused Rx retains one enclosing RawInput
until its connection-specific resume. Both external owner drains and all posted
output completions must finish before the final stopped callback releases the
single teardown reference. See [ENDPOINTS.md](ENDPOINTS.md) for exact hooks,
factory validation/immutability requirements and shutdown contract.


### Persistent-link connection ownership

The application routes each associated physical connection to a persistent
logical link. No managed attachment index, claim/release hook, or duplicate-owner
refusal policy is provided. At most one connection is current; a new binding
immediately displaces its predecessor using abrupt `ZiMultiplex::close`, without
waiting for predecessor work to drain. Graceful `disconnect` is separate.

Logical queued work and sequence assignments survive for the successor. Stale
physical callbacks cannot close or clean the successor, and obsolete wire/TLS
representations are discarded. Application resend and archive ownership is
unchanged. Final owner shutdown still drains outstanding obligations before
destruction. Replacement implementation and regression verification remain in
progress; see `../zfix_repair2.md` for open transport prerequisites.


### Outgoing setup boundary

The adapter starts its FIX session after TCP/TLS readiness. DNS and socket
connects complete through their normal continuations; Zfix adds no cancellation
API or native setup deadline. Hostname resolution uses existing ZiResolver
continuations rather than blocking the application Rx owner. All configured
address families drain before socket submission. An explicit TLS remote-IP
continues to bypass lookup while preserving the configured hostname for SNI.

Applications keep links, hubs and the multiplex alive while setup is pending.
Stop prevents subsequent setup work; an already submitted connect still finishes
normally. A late success is closed through ZiConnection and its ordinary close
drain. Native failure invokes connectFailed after connectRetired_ has started
protocol owner drain. The preconstructed owner opens no FIX connection generation,
claims no active attachment and assigns no new sequence during native setup.
Retry/release waits for the stopped observer, including external-manager Rx/Tx
stop acknowledgements. FIX connection-establishment state/deadline and
`SessionConfig::connectTimeout` are removed; native DNS, socket and TLS setup
retain their own lifecycle.

## Deferred processing admission

Typed receive remains synchronous unless the application obtains an explicit
work lease. Managed work admission checks catalog/context selection and requires
a live Established, non-draining session; raw work admission requires a live
external-manager endpoint. Link and hub count/byte reservations precede the
identified work allocation. Native input records the actual originating pooled
allocation, including jumbo prefix and enclosing node; independent admissions
sharing backing are charged conservatively. Additional application storage and
worker/result closures are declared with extraBytes and released before the last
work reference. Application business registries/correlation remain independently
bounded by application policy.

A last work reference may be dropped off shard. It schedules final deletion on
AppRx, where input/work storage is released before credit return. No locks,
per-field backing references, response-count claim or pending-call registry are
introduced. Shutdown retains its existing owner drain until work and retirement
posts are empty; stop hooks must cancel/finish deferred processing. Releasing a
lease does not manufacture business completion. Managed drain refuses new work,
includes outstanding leases in its Rx proof and reevaluates synchronization after
retirement. Normal work does not mark protocol recovery incomplete. Reset prepare
cannot bypass active work.

## Repair contract: always-app history and archive notification

`fixRetrieveTx_(TxContext, acknowledged, ticket)` is the only history source for
every replay attempt, with requested epoch/sequence and replay context. Returned
body/type/application ID may differ; the app supplies required original time.
No body produces gap-fill. Pending uses its one-shot existing continuation.
No replay body enters the core sequence queue or survives refusal as a cache.
Ordinary missing-send reloads are installed/charged under current operational
limits; restored memory debt and TxBytes are removed. Snapshot occupancy belongs
in Tx statistics rather than Checkpoint progress.

An ordinary output refusal retains that installed node and its charge for a
normal retry; it does not require another lookup. This operational lifetime is
separate from always-app replay. Assigned catch-up can proceed before Ready,
while new unsequenced business admission remains gated, so a pending replay
prefix can become eligible without waiting on its own completion.

`fixArchiveTx_(epoch,begin,end)` informs the app after actual generic ordered
archive completion, including ranges whose core nodes have retired. The interval
is half-open; app policy may retain, move or delete its own records. No `archived()` release
completion remains, and persistence is not a mandatory core barrier. Local output
completion neither advances acknowledgement nor synthesizes archive. Restore
does not synthesize storage transitions; crash-recovery consistency is app-owned.

Warm hold/reconnect resumes unfinished archive chunks and does not repeat
completed ranges. Cold restore starts at restored acknowledged progress without
retrospective callbacks; it is distinct from resuming the same live owner.

