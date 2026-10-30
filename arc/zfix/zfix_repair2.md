# Zfix persistent-link and connection-replacement repair plan

Status: implementation and all authorized current verification are complete.
[zfix/REPAIR2_AUDIT.md](zfix/REPAIR2_AUDIT.md) is the current
requirement-by-requirement table; earlier evidence below retains its snapshots.
The user-authorized TCP/TLS/ZiTx header repairs, physical handoff, target binding,
early FIX association, reset outcomes and terminal readiness gate are implemented.
QUIC audit found no equivalent defect and made no change.

Current verified checkpoint under the unchanged Linux x64 Clang/ASan build:

- Library/unit builds and schema gates pass; 29 TAP programs/347 scopes pass.
  Logs: `/tmp/zfix-repair2-final-{src-build,unit-build,unit-test}.log`.
- Managed native session TCP58/TLS64 pass. Three A→B→C/SIDE cases per transport
  preserve A Work/context and pending load through both binds, observe actual
  partial A framing, allow SIDE traffic during both holds, and balance exact
  completion/physical/logical counts with A cleanup delayed.
- Six TLS socket-only B failure/C cases and three terminal-stop cases per
  transport pass within those session totals. The terminal gate's original TCP
  three-case failure is retained; Server readiness now also checks hub running.
  Evidence: `/tmp/zfix-association-chain-final-summary.json`.
- Native stale-error candidates pass ten scopes per transport, with targeted
  fault-removal controls; focused reset-close/outcome checks pass 116 scopes.
- Live replay/Logout/archive TCP18/TLS18 passes app-selected repricing,
  suppression, original assignments, exact archive progress and a fresh probe
  after A cleanup. `/tmp/zfix-replay-replacement-summary.json` explicitly records
  the pre-readiness-gate build; the final complete integration rerun now passes
  TCP19/TLS18 recovery. The added real-EAGAIN managed partial-output TCP target
  now passes recovery19 with 17 strict assertions and no diagnostics; this
  TCP-only proof is `/tmp/zfix-managed-partial-verification.json`.
- The unnecessary cold-preparation WorkBudget/ColdReturn credit was removed
  after actual owner-FIFO proof. Utility build and four cold reset/recovery
  lifecycle cases pass, all 20 child exits zero and sanitizer clean:
  `/tmp/zfix-cold-fifo-audit/{PROOF.md,verification.json}`.
- Current utility/example/interop builds are warning-free, native workload18
  passes, and both standalone example pairs pass with four successful actor
  exits. Logs use `/tmp/zfix-repair2-final-` and
  `/tmp/zfix-repair2-acceptance-`. Final integration default-all passes
  warning-free, then 13 programs/261 scopes (TCP127/TLS132/demo2) and 42 demo
  scenarios pass. Its 211 source/build/config hashes match before/final in
  `/tmp/zfix-repair2-final-itest-verification.json`. Canonical76, lifecycle24,
  probes6 and SDK policy4 pass with retained artifacts in the acceptance path
  files. ASan/LSan leak checks remain enabled; no diagnostics were found.

A final test-only style follow-up names the partial-output workload size with
one shift-based enum constant. The affected TCP recovery binary was rebuilt
and its 19 scopes pass; the other 210 source/configuration pins are unchanged.
The full-suite snapshot is preserved separately from the current pin map in
`/tmp/zfix-repair2-partial-size-verification.json`.

Zfix registry removal, persistent application routing, queued logical-work
preservation, lazy deferred-admission obligations and Wire retirement are
implemented. Physical closes and final logical stops are counted separately.
No replay store, replacement registry, unnecessary cold credit or predecessor
activation barrier is added. Independent source/evidence review against
sections 1–8 finds no unresolved explicit requirement. Historical verification
below remains identified by snapshot. No installation, benchmark, dependency
rebuild or new platform matrix was required.

## Historical verification snapshots

Verification of the preceding native repair snapshot (before early association):

- Unit/schema: 29 programs / 328 scopes pass.
- Native integration: six TCP programs / 105 scopes and six TLS programs / 106
  scopes pass, including native displacement (TCP eight, TLS nine).
- Each closed-starter/TLS-binding fault-removal control fails exactly its new
  regression scope. No dependency library was rebuilt or configuration changed.

Earlier broader Zfix snapshot verification (not rerun for these headers):

- All Zfix build directories compile under the unchanged configuration.
- Full unit/schema target: 29 programs / 328 scopes pass.
- Wire passes 16 scopes, including actual-Link admission, refusal, setup
  retirement and timer/reset ordering; State/Rx/LinkTx/Managed checks also pass.
- Affected managed integration: five TCP and five TLS programs, 194 scopes pass.
- Canonical process matrix: 76; lifecycle: 24; SDK-only rejection: four;
  probe cases: six all pass.
- Native workload wrapper: 18 cases pass; demo driver: two scopes pass.
- Standalone TCP and TLS example pairs pass with empty sanitizer stderr.

Integration exposed and verified repairs for stop completion when Tx never
opened, and ordinary reconnect clearing an aborted reset handshake while
preserving an unresolved logical reset commitment. All six TCP and six TLS
programs passed 204 scopes before adding the native displacement reproducer.
Failed Rx preparation now returns before Tx mutation. Pure timer-only events
stay on Rx; the latest full unit/schema run passes with that optimization.

The original native stale-close regressions now pass after TCP/TLS header
repairs. Pending-close authority is revoked on successor association; queued
cleanup is guarded on its owner, and every physical close settles accounting.
QUIC review found no equivalent defect and made no change. Current-at-entry
and deferred-stage checks pass, including successor refusal before handshake.
Physical queue retirement and bound TLS output also pass their focused native
regressions. That native repair snapshot preceded early FIX association and
does not prove the entire goal. The association split is now implemented;
its current focused verification and remaining consumer checks are recorded in
`zfix/REPAIR2_AUDIT.md`.

Evidence logs use `/tmp/zfix-repair2-` prefixes. Important records include
`unit-all.log`, `wire-test.log`, `linktx-test.log`, `reset-build.log`,
`session-tcp-retest.log`, `session-tls-retest.log`, `recovery-tcp-retest.log`,
`reset-tcp-retest.log`, and `native-pairs.log`; process artifacts are in
`/tmp/zfix-repair2-native-pairs`. The first TCP suite and reset retry preserve
failed pre-correction runtime evidence. The earlier scoped build used `unit-current.log`,
`itest-current-build.log`, `util-current-build.log`, and
`example-current-build.log` with the same prefix. Configuration checksum checks
against the prior baseline passed. Current native logs use the
`/tmp/zfix-transport-final-` prefix. The necessary transport fixes modify
Ztcp/Ztls headers and the ZiTx starter/bound-target entry point; no dependency
library rebuild or external SDK modification was performed.

`GUIDELINES.md` is authoritative. This plan supersedes attachment-registry and
concurrent-connection rejection requirements in earlier Zfix documents. It
preserves the other decisions in `zfix_repair.md`. The user correction is
authoritative: displacement abruptly closes the stale connection without
draining it; queued logical work belongs to the successor. Orderly terminal
owner shutdown is a separate operation.

## 1. Requirement and evidence

A logical link has at most one live FIX session and one current connection.
A newly associated connection displaces its predecessor, including when that
predecessor is live or draining. The application owns logical-link selection;
Zfix must not maintain a second index that rejects another owner of the same ID.

Pre-repair, `ZfixAttach.hh`/`.cc` implemented an Rx-owned hash of application IDs to
link pointers. `EndpointHub` inherits `AttachHub`; `LinkHooks_` forwards claims
and releases; managed `Link` claims on initiator opening or acceptor admission
and releases after shutdown drains. This tracks duplicate link objects, not
which connection belongs to a persistent link.

Pre-repair, `Link::transportUp_()` also disconnected when `m_live || m_closeRef`.
Deleting only the hash therefore cannot implement displacement. Acceptor
consumers that construct a new link for every accepted connection must change
as well.

## 2. Invariants to preserve

- Application ID is the durable, opaque session identity. Header fields,
  transport mode, version, and application version do not compose another
  occupancy key. Preserve the existing enum types and ID/epoch checkpoint
  matching. This repair neither introduces live configuration mutation nor
  prevents a restored session from using a different configured FIX version.
- Connection replacement does not reset sequence progress, reuse assigned
  sequence numbers, increment the persistent session epoch, or manufacture a
  new logical session. Explicit protocol/application reset remains separate.
- Newest association wins immediately. Abruptly terminate and close the old
  connection without flushing it, completing its queued work on it, or waiting
  for its callbacks before activating the successor. Preserve queued logical
  work for the successor, subject to its normal transport/FIX readiness.
- Distinguish connection-specific callbacks from logical-link work. Stale
  connection callbacks may settle their own resources/accounting but cannot
  close, reset, publish on, or notify readiness for the successor. Queued
  logical work resolves the current connection when executed; an old captured
  connection identity must not cause that work to be discarded.
- Operational queues and in-flight work retain only what their existing
  contracts require. Replay/resend content always comes from the application,
  even when original content is operationally present. Preserve archive
  notifications and application control over replacement content or gap-fill.
- Terminal owner shutdown drains connection, scheduler, application, and Tx obligations before
  owner destruction. No blocking waits, extra DNS/connect cancellation, or
  new receive-limit/resumption mechanism is part of this repair.

## 3. Transport prerequisite: prove safe replacement before enabling it

Audit `Ztcp::Link::connected_1()` and `disconnected_0()`/`disconnected_()`/
`disconnected_1()`, and the corresponding Ztls paths, with their dependent
connection, stream, queue, TLS, and hub-accounting code.

Pre-repair TCP issue: `disconnected_(cxn)` only conditionally clears `m_cxn`, but
unconditionally changes disconnect state. Its subsequent continuations clean
`Tx::txQueue`, invoke link disconnect hooks, and clean the Rx stream even if
`cxn` was displaced. A pointer comparison at the first step alone cannot
protect later stages if a successor arrives between those stages.

The initial TLS audit had to examine later Tx cleanup, socket queue cleanup
and final Rx TLS reset, despite its existing first-step connection check.
Subsequent regressions demonstrated stale deferred cleanup and accounting
defects; the repaired stages now pass their targeted negative/positive controls.
This was established independently rather than assumed identical to TCP.

Execution audit additionally found that `ZiTx<Impl>::txQueue` is link-owned,
whereas each `ZiConnection` owns its I/O context. `ZiTx::send` appends to an
existing queue and initializes a socket context only for an empty queue. An
overlap can therefore append successor output behind stale partial output;
stale context completion can consume the wrong queue head. First-step stale
close guards alone cannot establish safe handoff. Prove connection isolation or
an owner-shard handoff covering queue contents and active I/O contexts before
enabling overlap. The queued-output TCP regression now proves and verifies the minimum
I/O-Tx retirement handoff; the TLS protection-target race has a separate passing
regression and fault-removal control. Combined managed FIX association now passes the focused current session
checks above; the native-only results alone did not establish the whole plan.

Before coding, trace every stage's owner and ordering, and record:

| Resource or action | Required replacement behavior |
| --- | --- |
| Current connection pointer and disconnect status | Only the current connection or its valid transition may change them. |
| Rx stream and TLS state | Reset or replace stale connection state on its owner shard without a predecessor drain; obsolete cleanup cannot touch successor state. |
| Logical-link Tx queues | Preserve queued work and dispatch it through the successor when ready; never clean it merely because the predecessor closed. |
| Socket sends and send completions | Abrupt close abandons stale physical output without flushing; submitted attempts remain attributable to the old socket, and each local completion settles once. |
| Connection references and hub counts | Release each acquired obligation once, including obsolete connections. |
| Link callbacks and reconnect scheduling | Obsolete physical close must not report or restart the successor as disconnected. |

Install the successor as current and abruptly close its predecessor using the
`ZiMultiplex::close` path. `disconnect` is graceful disconnect with drain and
must not be used for displacement. Check transport wrappers and their call
chains so that an apparent close does not delegate to graceful disconnect.
Use the existing abrupt-close capability; do not introduce a new close mode.

Do not introduce a pending-candidate queue, a predecessor-drain barrier, or
serialized activation waiting for the stale connection. Order the necessary Rx/Tx owner
mutations through existing shard dispatch; this is an ownership handoff, not a
request to drain or flush old work. Normal successor transport authentication
and FIX establishment still apply.

Make stale physical close paths release only their own resources and accounting.
They must not run link-wide disconnect/stop cleanup against the successor or
clear persistent queues. If connection-specific TLS or I/O resources remain
referenced by outstanding callbacks, retain those resources for their existing
completion lifetime without delaying successor operation. Do not free memory
still referenced by I/O; asynchronous resource reclamation is not a displacement
drain barrier.

Prefer existing connection references, shard ordering, and generation checks.
Invalidate stale connection authority at association, using transport identity
before FIX establishment where appropriate. Do not access the Rx current
pointer from Tx to add a convenient guard. Do not add a second transport
registry, broad locking, or a collection of retired connections in Zfix.

Do not implement a stale-event early return without proving reference and
hub-accounting balance. Conversely, accounting completion does not authorize
cleanup of current-link state. Check failures before socket readiness, TLS
handshake failure, simultaneous close, and application stop as well as normal
establishment.

These are generic transport correctness prerequisites, not FIX policy.
The user subsequently authorized the TCP stale-disconnect repair and checks
for equivalent TLS/QUIC defects. The minimal TCP/TLS header fixes and their
Zfix-hosted regressions are within that authorization. Further dependency
changes must be justified separately by a concrete invariant and evidence.
Do not disguise it as a FIX workaround. Until the prerequisite is satisfied,
replacement cannot be declared complete. The current restriction against
rebuilding beyond Zfix remains; any expanded dependency implementation or
validation scope must be resolved separately before undertaking it.

## 4. Replace per-connection link construction with application routing

Where routing is known at accept, obtain the application's existing logical
link and construct only a new connection bound to it. Where routing is resolved
later, bind at that existing routing boundary instead; do not create a competing
managed link merely to represent an unassociated socket. A single configured link
needs only its existing owner/reference; multiple configured links use the
application's existing lookup. Do not replace `Attachments` with an equivalent
framework-owned session registry under another name.

Application routing and admission remain distinct: routing selects the logical
link; authentication and Logon validation decide whether the selected new
connection may proceed. Do not reinstate the old connection if its successor
fails admission. Unknown routes and invalid Logons may still be rejected.
Where selection depends on incoming identity, document the existing routing
boundary and ensure displacement happens when association is made, without
inventing a header-derived persistent identity. Acceptance of an unassociated
socket alone does not displace an arbitrary link. Once bound, the successor is
current even while transport/FIX establishment is incomplete; being current
does not authorize business publication before normal readiness.

Migrate `example/zfixd.cc`, `util/load_native.hh`, and
`itest/session_io_test.cc`. Inspect every other `Cxn` factory throughout Zfix,
including tools, interop, and tests. `itest/recovery_io_test.cc` already binds
connections to a retained session link; preserve its distinction between
ordinary reconnect and explicit cold reconstruction from checkpoint.

Keep example programs standalone and digestible. Replacement race tests belong
in `test` or `itest`, not in examples. Utility connection-count and completion
semantics must continue to count their documented events; persistent-link
ownership must not silently change those metrics.

## 5. Refactor managed-link transitions and completion authority

Implement against the transport ordering established in section 3. Review
`connectAllowed_()`, `transportUp_()`, `transportDown_()`, `admit()`,
`stopped_()`, their callers, and `fixStopRx_`/`fixStoppedRx_` consumers together.
Do not simply remove the busy guard and allow two generations to share mutable
FIX state.

Use the following transition contract:

| Situation | Required action |
| --- | --- |
| No predecessor activity | Open the selected connection through ordinary FIX establishment. |
| A live; B associated | Make B current, abruptly close A, and carry queued logical work forward to B without waiting for A. |
| C associated before A/B callbacks complete | Make C current and abruptly close B; A/B connection-specific callbacks have no authority over C. |
| Stale connection-specific callback arrives | Release its own resources/accounting; do not mutate successor session, timers, framing, output, or lifecycle state. |
| Logical-work continuation queued before replacement arrives | Apply it to its still-valid logical operation on the owner shard and leave publication queued for the current connection; displacement alone does not invalidate it. |
| Terminal application stop | Prevent further association/activation, close connections, drain owner obligations, then notify final stop and release ownership. |

Connection replacement is not terminal destruction of the persistent link.
Define the meanings of stop callbacks explicitly and update all consumers:
predecessor completion must not cause `retired()` or equivalent to destroy the
link while a successor exists. Preserve any required per-connection completion
notification, carrying existing connection identity where necessary. Do not
emit a whole-link final-stop event for each displaced socket.

On Rx, invalidate predecessor authority and stop its connection-specific timers.
Discard stale partial framing and connection-specific receive state. Do not
route displacement through whole-link application stop. Old admission replies
and posted timer callbacks must recognize their obsolete connection and settle
only their own obligations; they cannot open or disconnect the successor.
Pending logical-link work remains eligible for the successor. Preserve complete
logical receive work already accepted into session processing; discard only
stale connection input that has not crossed that boundary. Connection identity
checks must not erase already accepted logical progress. Include typed reply
and request-context guards in this audit: old connection metadata alone must
not reject still-valid logical work for the same application ID and epoch.

On Tx, preserve the persistent sequencer and queued application work. Rebind
publication to the current connection through the existing owner-shard dispatch
and readiness gates. Work awaiting application preparation/loading must remain
queued for the successor when it is logical-session work; do not reject its
result merely because the old connection disappeared. Retain operation-level
checks for a genuine application abort, explicit reset, or superseding result;
forwarding across displacement does not override those independent decisions.
Connection-specific control output, partial wire writes, and encrypted records
are not transferable
logical work: discard stale transport representations, retaining the underlying
eligible logical operation in its existing queue. Rebuild connection-dependent
output when the successor is ready. Never continue an old socket offset or TLS
record on the new connection.

Already submitted wire attempts can have uncertain delivery. Preserve their
sequence assignments and existing completion/token accounting; use ordinary FIX
recovery and application-provided resend content where recovery is required.
Do not silently treat uncertain delivery as a new unassigned send, retain a
replay cache, or manufacture an application abort. Actual application-aborted
assignments use gap-fill, never tail rewind or sequence reuse.

Preserve normal Logon, recovery, and readiness ordering without a predecessor
completion gate or synthetic TestRequest barrier. A stale completion must not
mark a successor attempt complete, retire the persistent link, or clean its
queues. Check for currently queued work capturing stale connection handles and
change logical-work dispatch to use the current destination on its owner shard.
Keep genuinely connection-specific completions bound to their original attempt.

Audit `m_live`, `m_closeRef`, `m_txStopped`, `m_admissionTicket`, and existing
connection identities before choosing the final representation. Separate
terminal owner teardown from displacement; do not reuse terminal stop flags as
a replacement barrier. Remove state made redundant by the transport's current
connection and phase; add no mirror pointer, boolean, counter, generation, or
pending-candidate state. Any other new state requires a demonstrated missing
invariant. Use existing I/O completion ownership for obsolete resource
reclamation where valid. If its lifetime model is insufficient, record the concrete transport
prerequisite under section 3; do not assume safe reclamation or introduce a FIX
retirement container to conceal that defect.

All Rx and Tx state remains on its owning shard. Cross-shard continuations
carry existing handles and small identity/ticket snapshots, not copied config,
message bodies, or captures reading foreign-shard members. Keep immutable,
scheduler-shared, Rx, and Tx groups identified and cache-line separated per
`GUIDELINES.md`; preserve data alignment and function/definition spacing.

## 6. Delete the attachment mechanism and its direct dependents

Once replacement transitions are implemented, remove:

- `ZfixAttach.hh`, `ZfixAttach.cc`, `AttachIndex`, `Attachments`, and `AttachHub`.
- `EndpointHub`'s attachment base/include and `LinkHooks_` claim/release methods.
- Client/server hook exports and managed-link default attachment hooks.
- Initiator and admission claim branches, release in `stopped_()`, and managed
  `Link::m_attached`.
- Installed header/source build entries, `ZfixAttachTest`, and its ordinary,
  installed-header, ASan, and cleanup target entries.
- Shared attachment-domain fixtures and duplicate-owner refusal expectations.

Do not remove Tx's separate attachment/activation state merely because it uses
similar terminology. Do not remove `SessionError::Duplicate`, FIX duplicate-tag
checks, sequence-duplicate checks, or other unrelated duplicate validation.
Inspect symbol uses rather than applying a global textual deletion.

Update `zfix.md`, `zfix_plan.md`, `zfix_repair.md`, and applicable module contracts,
endpoint documentation, test instructions, and status records. Describe
persistent link ownership, selection, replacement, and lifecycle callbacks.
Retain historical verification as history, not evidence for this new behavior.

## 7. Verification plan

First add focused regression cases that exercise the violated invariants.
Use controlled continuations rather than sleeps to force callback ordering.
Exercise real TCP and TLS replacement in integration tests as well as unit
coverage of managed-link state. Required cases:

| Case | Assertions |
| --- | --- |
| Established A replaced by B | Same logical link and epoch, preserved sequence frontiers without reset/reuse (normal Logon and queued work may advance them); A uses the abrupt `ZiMultiplex::close` path, never graceful `disconnect`; B completes normal Logon/recovery. |
| A in Logon, deferred admission, Logout, or recovery | B displaces A in each phase; old acceptance/refusal/timer/recovery callbacks cannot affect B. |
| A has queued application work, partial Rx input, or partial output | Queued logical work proceeds on B; stale framing/partial wire output is discarded; sequence assignments and exact-once local completion accounting survive. |
| Old connection callbacks deliberately held pending | B establishes and processes ready logical work without waiting for A; releasing A callbacks cannot reset B or discard its work. |
| Logical preparation/load completion delayed across replacement | Its queued operation survives and completes for the successor when ready; no duplicate publication or spurious connection-stale rejection. |
| Fully accepted logical receive work queued across replacement | Accepted session work and progress survive; stale unaccepted socket input and partial framing do not. |
| Rapid A to B to C with old callbacks pending | Each new association immediately displaces the current connection; C remains current; no resurrection, candidate queue, or leaked accounting. |
| B closes or fails authentication with A callbacks pending | No fallback to A; normal failure notifications remain attributable and logical queued work remains subject to the existing reconnect/application policy. |
| Late A close/send completion after B is ready | B remains usable; old completion resolves exactly once; current queue, TLS, and timers survive. |
| Application stop during replacement | No stale callback or association reopens the link while stopped; owner obligations drain and final stop occurs once. An explicit later application restart remains separate. |
| Independent logical IDs and routing | Other links progress independently; application routing selects the intended persistent link. |
| Reconnect versus cold restore | Reconnect preserves progress; ID/epoch restore behavior and configured version-change cases remain correct. |
| Resend and archive during replacement | App remains sole resend source; archive callbacks retain their contract; assigned numbers are not reused. |

Check resource/reference counts and lifecycle notification order, not only
successful final Logon. Explicitly cover stale failures as well as successes.
Avoid tests that merely mirror private flags.

For implementation verification, reuse the current Linux x64 build and its
configuration. Build/test only Zfix within the authorized scope; do not
reconfigure, install, rebuild external SDKs/dependencies, run benchmarks, or add
ARM/Windows work. Run focused regressions first, then relevant existing Zfix
unit/integration and canonical example checks once changes settle. Transport
prerequisite validation uses the separately authorized native-header scope;
passing only Zfix unit tests cannot establish generic transport correctness.

Keep the previously recorded external QuickFIX ASan failure separate. Do not
modify the SDK, suppress the failure, or present a retry as a repair.

## 8. Completion criteria

- No Zfix attachment registry, hooks, claims, or duplicate-owner rejection
  policy remains in current Zfix source/build/API documentation. Independent
  external SDK policy is outside this repair.
- Every relevant Zfix-managed acceptor binds connections to application-owned persistent
  links; overlap displaces the predecessor rather than creating a competing
  managed owner or refusing solely because it is live/draining.
- Displacement has no predecessor-drain barrier; queued logical work survives
  for the successor, and stale transport representations cannot be reused.
- All replacement transitions and stale callbacks satisfy sections 2–5,
  including transport cleanup and accounting prerequisites.
- State, containers, allocations, copies, and cross-shard captures receive a
  changed-line review against all applicable `GUIDELINES.md` flags. Every
  retained/new field has an explicit owner and non-duplicative purpose.
- Required regressions pass under the unchanged authorized build, with commands,
  results, and any unresolved prerequisite recorded accurately in module status.
  No implementation completion claim is made while a prerequisite remains open.
