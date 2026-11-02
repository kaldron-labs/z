# Session engine acceptance audit

This ledger records earlier acceptance evidence. The current repair source has
not been compiled or tested. Its authoritative contracts are in
[`zfix_repair.md`](../../zfix_repair.md): every resend fetches application content;
archive reports body-free epoch/ranges; initial readiness follows actual owner
recovery without a synchronization challenge; managed FIX begins after native
transport readiness. Old Connecting, history-release and cached-resend entries
below are historical, not current requirements or proof of the repairs.

The Binary/API migration removes Header/Trailer message representations. Current
session fields use ordinary message metadata and are observed directly into
retained Rx state. Timestamp presence, age, clock skew and OrigSendingTime order
are metadata, never session admission or rejection gates. Earlier timestamp-based
rejection entries below are superseded. New source/fixtures await the coordinated
clang release acceptance run; historical counts are unchanged.

The tables below retain earlier session/queue acceptance descriptions, with
current fixture filename spellings. They do not override the repaired contracts.
The queue-repair migration in `zmpqfix.md` is complete; older reverse-unwind/
assignment-reuse evidence is superseded. Current generic, unit, native and
independent-peer results are recorded in ../STATUS.md. Historical fixture counts
do not establish final configured memory/release or fresh installed-consumer
gates, which remain Stage 9 work. Source names below refer to this directory.

## Required mechanisms

| Requirement | Implementation | Current evidence |
| --- | --- | --- |
| Seven built-in controls and prescribed fields | ZfixMsgs; shared metadata codec | Msgs golden bodies, optional facets, malformed/missing controls; Frame/TxFrame wire fixtures |
| Mode independent of declared version | SessionConfig, controlBody/saveControl | Session `modes`, `invalidLogon`; Tx `modes`; Managed `handshake` |
| Raw external control/sequence manager | Session bypass, Rx rawRx, explicit TxFrame headers | Session `raw`; Rx `limitsAndRaw`; TxFrame fixtures |
| Link ownership and connection lifetime separation | Embedded owner progress, PeerConfig identity, monotone connection tokens | Session `identities`, `initiator`, `logoutReconnect`; Rx `reconnect`; Managed `continuingReconnect` |
| Asynchronous admission, refusal and connection replacement | Session admission tickets; Rx retained first frame; managed one-shot deferred replies | Session `acceptor`; Rx `admission`, `rejectedAdmission`; Ctl command ordering |
| Lifecycle/probe/idle/deadline state | Session and LinkTx owner clocks | Session `clocks`, `zeroHeartbeat`, `recovery`, `probePublication`, `recreatedProbe`, `probeWatermark`; Ctl idle scopes |
| ZmPQRx read-ahead/gaps and ZmPQTx send/replay | ZfixSeq owner wrappers | Seq scopes including bounded permutations, callback holds and restored gaps; SeqTx bounded replay/history/deferred scopes |
| Original replay content/number/time, current framing | TxHistory/TxBody/txFrame | Tx `replay`, `clockReversal`, `reconstructedHistory`, `recovery`; Frame/TxFrame golden fixtures |
| Independent directional recovery and unavailable history | Session recovery flags, SeqTx replay cursor/gap fill | Managed `simultaneousRecovery`; SeqTx `history`, `deferredMissing`, `replay`; Rx gap-fill scopes |
| Direct owner restore before activity | Link epoch, Rx/Tx restore guards and range validation | Restore `ownerRestore`, `ranges`, `lifecycle`, `wireChange`, `stagedUnsent`; Rx `restoredDrainGap`; Tx restored scopes |
| Application storage/retention policy | Identified pooled bodies and external history hooks | Tx delayed/refused association/archive/history scopes; SeqTx `deferredReplayBound`; no database/store dependency |

## Hook implementations

| Contract role | Owner entry/notification | Focused evidence |
| --- | --- | --- |
| enqueue/dequeue | LinkTx enqueue_/queued_; Hooks enqueueTx/dequeueTx/selectedTx | Ctl applicationQueue/queueAssociation/queueReconnect/queueBounds/queueClose; Managed reset backlogs |
| load | Core loadTx, coordinator associateTx, loaded_ | Tx assignment/staleLoad; Ctl pending and queue association; Managed peerReject mapping before immediate response |
| reload | Core historyTx/reloaded_, coordinator retrieveTx | SeqTx deferred/history/unavailable/cache-bound scopes; Tx reconstructedHistory |
| unload | Core unloadTx/unloaded_, coordinator releaseTx | Tx/Ctl/Managed stale accepted pre-commit load rollback; no committed-assignment unwind |
| sent | LinkTx sent_/completeTx | Ctl completion and drain/close scopes; Managed complete-frame transport |
| ackd | Core ackdTx, coordinator acknowledgedTx | Tx ackNotifications; Session probeWatermark; Ctl acknowledgement; Managed probeAcknowledgement |
| reject | Rx peerRejectRx, coordinator peerReject_/rejectedTx | Rx/Ctl/Managed peerReject including retained backing, replay suppression and sequence reuse in a new epoch |
| stateChange | Rx stateChangeRx before general eventRx | Rx stateChanges: monotone fixed snapshot, ordering, timeout/close, reconnect/raw and duplicate suppression |
| reset | Core resetTx, coordinator epochTx; Rx epochRx | Tx bounded turnover/retirement; Rx/Ctl resetPreparation; Managed live/reconnect reset and close races |
| session | Rx eventRx Observe/input and sessionRx; Tx sessionTx | Ctl sessionObservation, including admission-before-observer-before-flush and actual replay gap-fill type |

Callbacks run on their documented owners. Application-owned association state
receives ordered posts, never direct off-owner mutation. Concrete endpoint
routing/teardown, application admission and outbound Logon customization are
implemented and exercised by native integration fixtures and application
examples. TLS peer verification belongs to Ztls. See ../ENDPOINTS.md for hooks.

## Transition and error gate evidence

This is the consolidated mapping to CONTRACTS. Each row identifies the source
fixtures that assert state/progress/output and callback/timer effects; the
lifecycle-only Session fixtures are complemented by actual codec/queue frames
in Rx, Tx, Ctl and Managed. Mode is varied independently from BeginString.

| Contract transition | State/progress/output assertions | Callback/deadline assertions and fixtures |
| --- | --- | --- |
| Disconnected/connect or accept | role-specific Connecting/Accepting, unchanged epoch, no wire reservation | Session initiator/acceptor/failureMatrix; stateChanges; connect versus Logon deadline |
| Connecting/connected | Logon state, new Logon from shared Tx sequence; stale connection ignored | Session initiator, failureMatrix; Ctl publication; Logon timer replaces connect timer |
| Accepting/initial Logon/admission | ticketed admission, response only after acceptance; refusal never establishes or advances Rx | Session acceptor/admissionMatrix/logonMatrix; Rx admission/rejectedAdmission; pending and refused loads in Ctl |
| Logon/peer Logon | low/equal/high sequence with both roles, all modes and three declared versions; required 789/app-version behavior | Session logonMatrix/admissionMatrix/invalidLogon; Rx modes; observe/stateChange and Logon deadline cancellation |
| Established/business send | unsequenced backlog, controls first, committed association before wire, no gap after refused load | Tx assignment/staleLoad; Ctl applicationQueue/queueAssociation/queueClose; sent and callback-before-publication |
| Established/in-order input | process once; malformed typed data never reaches application | Rx ordered/rejects/faultMatrix; Managed business dispatch; receive activity without synthetic acknowledgement |
| High input | bounded retention and missing-range request, business held | Rx earlyControls/limitsAndRaw; Managed simultaneousRecovery in every mode; paused readiness and operational recovery controls |
| Low input | PossDup suppression; non-PossDup Logout with no regression | Rx ordered/headerFailures/faultMatrix; duplicate peerReject replay; Logout timer and no repeated application hook |
| ResendRequest | overlapping/open-ended ranges, retained/archived/deferred/unavailable history; independent new/replay progress | SeqTx replay/history/deferred scopes; Tx replay/recovery/reconstructedHistory; bounded scheduling and application archive policy |
| SequenceReset gap fill/reset mode | monotone expected cursor, stale/overlapping range handling, no epoch reset | Rx gapFill; Seq gapFill/reset/restoredGap; recovery notification and retained-input disposition |
| Send idle | negotiated interval; ordinary and replay admission postpone deadline; pending output/load suppresses duplicates | Ctl idleTraffic/idleBackpressure/idleZeroAndLogout; Managed heartbeats; absolute Tx timer, stale callback and zero-interval behavior |
| Receive idle/probe | opaque new TestRequest, no peer-progress inference from unrelated traffic | Session clocks/zeroHeartbeat/probePublication/recreatedProbe; Tx publication notice; receive timer replaced by separate probe deadline |
| Matching Heartbeat | exact published ID and recovered directions establish bounded acknowledgement | Session probeWatermark; Rx earlyProbeReply; Managed probeAcknowledgement; probe cancellation and receive-idle renewal |
| TestRequest | exact echo; high request waits for ordered disposition | Msgs control fixtures; Ctl factory; Rx earlyControls/earlyProbeReply; no numeric-ID interpretation |
| Local Logout/peer Logout | request/response once, early high reply followed by ordered close | Session logoutReconnect; Rx highLogout; Ctl closeAfterOutput; Managed handshake; bounded Logout/close deadline |
| Transport/output failure | Closing, no fake Logout/ack/reset, accepted unsent association retained at its assigned sequence | Session failureMatrix in every mode/role at opening/pending/admitted/established/Logout; Tx failedOutput/staleLoad; Ctl queueReconnect and Managed partialFailure/queuedPartialWrites |
| Timeout/closing/drained | exact deadline closes, early callback only rearms, stale/duplicate callbacks inert; epoch preserved through disconnect | Session timeoutMatrix and failureMatrix; Ctl drain/close; Rx stateChanges; all connection timers cancelled |
| Drain and both agreed reset paths | both-direction frontier proof, queue hold, bounded retirement, sequence-one reuse and captured-epoch guards | Session draining/resetPreparation; Tx turnover/retirement; Ctl resetPreparation; Managed liveReset/reconnectReset/resetCloseRace; drain and preparation deadlines |
| Near sequence limit | business stops with configured control reserve, controls continue, no sequence wrap | Tx sequenceLimit/sequenceNotice; Ctl sequenceNotice; limit callback context, restore and epoch rearming |

The added lifecycle matrix covers transport/output failure at opening, pending
admission, admitted Logon, established and Logout in both roles and all managed
modes. Its timeout matrix covers connect, initial/pending/admitted Logon, probe,
Logout and drain, checking early/exact/stale deadlines and cancelled timers.
The Logon matrix varies low/equal/high peer sequence with continuing local/Rx
progress, all modes, both roles and FIX.4.2/FIX.4.4/FIXT.1.1. It also checks both
roles' missing/equal/lower/future 789, refusal, stale admission tickets, optional
acceptor heartbeat negotiation, required application version and unprepared reset.

| Error/guard category | Recorded state, progress and output effects | Evidence |
| --- | --- | --- |
| Invalid config, unsupported mode, zero managed epoch, active duplicate open | refuse before connection mutation; raw remains externally managed | Session configBounds/initiator/raw; zero-epoch case failed before the open guard fix |
| Wrong first type/version/identity, low or invalid initial sequence | close/refuse or bounded diagnostic Logout according to usable identity; no admission/establishment | Session identities/invalidLogon/logonMatrix; Rx admission/headerFailures/unsafeHeaders |
| Heartbeat, 789 or application-version mismatch | diagnostic Logout with correct reference tag, no readiness; future 789 cannot acknowledge | Session invalidLogon/logonMatrix/admissionMatrix; Ctl factory mode checks |
| Admission refusal or stale acceptance | no establishment, bounded Logout, cancelled admission/Logon state | Session admissionMatrix; Rx rejectedAdmission; Ctl pending load handling |
| Malformed integer, duplicate recognized field, unknown message type | consume once, Reject exact sequence/tag/reason, no typed dispatch; session remains operational | Rx faultMatrix across all modes and both roles; Msgs/codec negative fixtures; Managed peerReject |
| Missing, future or relatively ordered timestamps | pass metadata; sequence and PossDup policy remain independent | New timestamp regressions await coordinated acceptance |
| Wrong attached identity | consume expected input, Reject then bounded Logout; clear probe/receive deadlines | Rx identity regressions; state-change notification precedes general routing |
| Missing sequence or low non-PossDup | no cursor advance; bounded Logout without an invented Reject reference | Rx faultMatrix/headerFailures/unsafeHeaders |
| Read-ahead/output bounds | bounded retained bytes; failed candidate/input cannot echo, dispatch or reserve a sequence | Rx faultMatrix/limitsAndRaw/businessLimits; Tx limits; Ctl queueBounds/control bounds |
| Current/future/stale fault routing, including limit-coded failure | correct tag/error/epoch; immediate close or explicitly requested bounded Logout; stale connection inert | Session faultRouting across modes, roles and both dispositions; failureMatrix/timeoutMatrix |

Configured sequence exhaustion, including UINT64_MAX-minus-one arithmetic, is
executed by Tx sequenceNotice. Connection/probe/drain incarnation-counter caps
are source-audited: Session checks UINT64_MAX before increment and refuses/closes
with Limit; no test claims to perform 2^64 runtime operations. Limit-coded public
fault routing and its callback/deadline disposition are exercised separately.

The matrix initially expected a receive deadline while a synchronization probe
was active; the contract instead uses a separate probe deadline until the exact
reply. The historical OrigSendingTime rejection expectations are superseded by
the current timestamp-metadata contract above. Unknown mode rejection already existed; the new managed
zero-epoch check exposed and fixed a genuine inconsistency with snapshot/Tx state.

## Acceptance scope

The source/installed suite, focused matrix results and memory/build evidence are
recorded in STATUS.md. This gate covers the fake-clock/transport/application
session engine, including the named hook roles and direct owner restoration.
Concrete endpoint routing, admission/customized Logon, transport teardown and
independent interoperability have separate native/application/process fixtures.
Those results complement this engine mapping; see ../STATUS.md for scope.

## Stage 5 transmission/association gate

The stage 5 fake-owner transmission gate is complete. The following maps the
plan's assignment sequence and acceptance requirements to current evidence.
Physical TCP/TLS and scheduler/transport teardown are exercised separately in
the native integration suite.

| Requirement | Mechanism and evidence |
| --- | --- |
| Unsequenced acceptance and application queue identity | TxBody contains immutable encoded content and an opaque application ID, no wire header. Ctl applicationQueue/queueBounds and Managed queuedAssociationOwner assert no sequence on enqueue or while association is pending. |
| Tx selection, control ordering, conversion before association | TxBody encode/control errors return before select/load; TxFrame and codec negative fixtures exercise failures. LinkTx prioritizes controls before dequeue. Ctl factory/applicationQueue and Managed associationOwner/queuedAssociationOwner interleave Heartbeat with pending business selection. |
| Exact load identity/context, no refused-load gap | Core select proposes one sequence and commits only after load acceptance. Ctl pending/queueAssociation and Tx assignment/staleLoad cover refusal/delay. Managed associationOwner checks the application map before Tx completion, in all modes and both roles; refused business leaves its sequence available for Heartbeat. |
| Separate association owner and continuation ordering | Fake owner 3 owns mapping, refusal, unload and Reject lookup. Tx posts immutable handles plus captured context/tickets, then waits. associated/unloaded completions return to owner 2. Managed associationOwner and queuedAssociationOwner assert pending/commit/publication and final-selection-before-Reject order. |
| Current headers and complete pooled output before publication | Tx supplies current session fields/time at publication through ordinary metadata; Frame/TxFrame golden fixtures verify framing. Managed associationOwner advances UTC after mapping but before Tx completion and checks current SendingTime; deferredOwnerClose checks new headers after reconnect and requeue. |
| Partial writes, immutable assigned sequence and replay identity | Managed partialWrites scans every byte split; queuedPartialWrites checks multiple admitted frames and all completions on failure. partialFailure covers incomplete peer data and full peer delivery with failed local completion. replayDuringPartial needs a unique transport ticket even for otherwise equal replay context. |
| Retain/abort accepted unpublished work | Committed work retains its assigned sequence on reconnect. Explicit application abort leaves a missing range for ordinary retrieval/GapFill without renumbering. Only stale pre-commit acceptance invokes unload rollback. Logon is assigned at unchanged next and admitted before catch-up; Tx/Ctl/Managed retained-reconnect, priority-hook reentry and deferred-history recovery fixtures pass. |
| Sequence-keyed Reject on association owner | Rx retains exact parsed reference fields and pooled backing, posts through Tx's epoch/connection guard, then the fixture routes to application owner 3. Managed queuedAssociationOwner checks selection and map order before immediate peer rejection; peerReject checks duplicate replay and reference reuse in a new epoch. No business registry is supplied by Zfix. |
| Operational controls, independent recovery and exact replay content | SeqTx overlap/open-ended/deferred/unavailable fixtures; Tx original body/time/reconstructedHistory and gap-fill fixtures; Rx earlyControls/earlyProbeReply; Managed simultaneousRecovery resolves both directional gaps before readiness. |
| Same-ID reconnect continues epoch/sequence | Managed continuingReconnect/partialFailure/queuedPartialWrites and Tx reconstructed-history scopes verify continuing Logon, original sequence/time and replay, no implicit reset, and retained unsequenced work after recovery. |
| Both drain/reset paths, no discarded unresolved state | Session/Rx/Ctl resetPreparation, Tx epochTurnover/retireControls/retireGuards, and Managed liveReset/reconnectReset/resetCloseRace prove both frontiers, blocked business, bounded operational-tail retirement, stale tasks, owner-epoch consistency, agreed 141=Y/34=1 and queued new-epoch dispatch. Unknown/unacknowledged history, business/Reject retention, pending work and logical Rx gaps prevent premature turnover. |
| Stale continuations and epoch reuse | Core tickets survive connection/epoch changes; captured epochs guard archive/ack/replay/output-failure/Reject paths. deferredOwnerClose checks old pre-commit load/unload completions, including reopening before the application responds. Existing epoch/reset/peerReject scopes reuse sequences while refusing old-context work. |
| Application reset policy, sequence headroom and exhaustion | The application invokes drain/prepare/start/disconnect operations and owns schedules, storage and queued-work policy. Tx sequenceLimit/sequenceNotice and Ctl sequenceNotice exercise configured control reserve, one-shot epoch notification, restore/reconnect, exhaustion and UINT64_MAX-minus-one arithmetic. No automatic calendar reset or sequence wrap is introduced. |

The third-owner fixtures enforce owner access with explicit assertions. They use
fake queues rather than actual concurrent scheduler threads. App queue storage is
Tx-local, mapping/disposition is application-local, and only handles plus fixed
metadata cross owners. No shared application-state reads, locks or library
correlation registry are needed. Current source, memory and historical installed
evidence is recorded in ../STATUS.md; fresh installed consumers remain part of
final delivery.

TCP/TLS, scheduler timer drain, endpoint dispatch/admission, custom Logon,
application checkpoint restart and typed API examples have recorded native and
independent-peer coverage. Final configured memory checks, clean release builds,
measurements, fresh installed consumers and platform disclosures remain Stage 9
requirements. Completing this focused engine gate alone does not prove them.
