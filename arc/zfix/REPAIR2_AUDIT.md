# Repair 2 completion audit — verified

`../zfix_repair2.md` is the governing plan. The current source implements
persistent-link displacement, the authorized native handoff/cleanup repairs,
and early FIX association. The focused native and managed replacement gates
below pass. The final current library/unit/consumer/integration/process checks
also pass under the unchanged authorized configuration. Earlier records below
retain the results of their own snapshots and are not current failures or
final-source evidence.

| Requirement | Verified current evidence |
| --- | --- |
| Delete attachment registry and direct build/API dependents | Source/build/API registrations and registry-specific fixtures removed. Historical records remain identified as history. |
| Application routes to a persistent link | Example, utility, recovery, demo and session acceptors migrated. Actual managed session TCP 58/TLS 64 scopes pass, including two application-routed logical IDs during replacement. Current utility/example/interop builds and native18/example pairs pass; final integration and canonical process checks pass. |
| Abrupt close through ZiMultiplex::close | Native TCP/TLS tests prove stale cleanup isolation, queued physical output retirement and bound TLS output without predecessor flush/drain. Restored stale-error candidates pass ten scopes per transport; fault-removal controls fail their targeted scope. The new managed partial-output TCP recovery target passes 19 scopes with actual EAGAIN. |
| Preserve logical queue, assignment and epoch | Established A→B and A→B→C pass all three managed modes on TCP/TLS. The original pending Tx association publishes its assigned sequence on the successor; no sequence/epoch reset or reuse. |
| Old wire callbacks cannot consume successor attempts | Wire/LinkTx controlled-owner tests, native physical handoff tests, Raw replacement, and combined managed cases pass with held old callbacks and exact completion/accounting checks. |
| Accepted receive work and partial input | Managed A→B→C retains A's accepted Work/RequestContext and returns its exact credits once on C. Actual incomplete A FIX prefix is observed before B binds; subsequent normal Logon/business prove framing replacement. |
| Delayed logical association survives | Managed A→B and A→B→C hold the original server load over native association/authentication, then publish once on the current connection with the original sequence. SIDE traffic progresses while it is held. |
| Reset result survives replacement ordering | Actual-Link Wire exercises all six close/outcome schedules and commit before/during/after early association with a third successor. Focused outcome checks pass 116 scopes; current full unit/schema checks pass 29 programs/347 scopes. Final reset/native suites pass; no extra reset×native-displacement matrix is required. |
| Overlapping deferred admissions and owner lifetime | Actual Link tests pass four completion orderings, duplicate/stale reply handling, final-stop obligations and destruction. Native deferred A admission and TLS failed-B/C cases also pass. |
| Terminal stop drains every obligation once | Actual Link/Raw owner tests and physical native accounting pass. Managed stop-before-late-bind cases pass all three modes over TCP/TLS after the Server readiness gate; no Ready/business or reopened session is allowed. |
| A to B to C with stale callbacks | Managed chain passes three modes on TCP/TLS with A's native close held through C authentication/business. SIDE traffic continues at both binds, and final physical/logical counts are exact. |
| Admission/authentication failure and no fallback | Admission/refusal owner tests and six real TLS socket-only B close/fatal-alert cases pass; C resumes the same owner with A callbacks held. No fallback or pre-authentication FIX readiness occurs. |
| Independent logical IDs | The managed A/SIDE/B/C chain proves SIDE request/reply progress during both replacements while A Work/load remain pending, using separate application-owned state/history. Older routeMatrix checks remain separate ordinary isolation coverage. |
| Reconnect and cold restore | Existing ordinary cases and four cold native-native reset/recovery TCP/TLS cases pass after removing the unnecessary preparation credit. Current full lifecycle24 passes resume/reset/persisted recovery across all four pairings over TCP/TLS; prior lifecycle records remain historical. |
| Application-only resend and archive notification | Focused recovery TCP/TLS each pass 18 scopes, including six live replay/Logout cases: app-selected repricing/suppression, original sequence identity, contiguous archive ranges and fresh probe after all A cleanup. The final current recovery TCP19/TLS18 rerun passes after the readiness gate; TCP includes managed real-EAGAIN partial output. The earlier focused snapshot caveat remains historical. |
| Consumer physical-close accounting | Current native18 workload wrapper passes. Combined session tests require four physical retirements and one final stop per persistent server owner. Current utility/example/interop builds and standalone example pairs pass; final integration 13/261, demo 42 scenario results and canonical76 all pass. |
| Current documentation agrees | Current authority and snapshot limits reconciled here, STATUS.md, ASSOCIATION_REPAIR.md, REPAIR2_REVIEW.md and the governing plan. SDK rejection policy remains separate from native displacement. |
| GUIDELINES changed-line review | Ownership/allocation/copy dispositions are in REPAIR2_REVIEW.md, including early association, Raw deferred-stop credits, cold FIFO proof, terminal readiness and new managed fixture state. Independent sections 1–8 source/evidence audit found no unresolved requirement; final source/config hashes are unchanged across verification. |
| Existing build configuration retained | Existing Linux x64 Clang/ASan configuration and dependency artifacts reused. Authorized TCP/TLS/ZiTx header changes are covered by Zfix-hosted tests; no dependency library rebuild, reconfiguration, installation or benchmark run. |
| Full authorized Zfix verification | Current unit/schema 29/347 and managed session TCP58/TLS64 pass. Current utility/example/interop builds, native18 workload and TCP/TLS example pairs pass. Final integration 13/261, canonical76, lifecycle24, probes6 and SDK policy4 pass. No unresolved required gate remains. |
| External QuickFIX failure preserved accurately | Independent SDK shutdown race remains separately documented; no SDK edit, suppression, or retry is presented as its repair. |

Current evidence:

- Final style follow-up names the partial-output fixture's payload size as
  `PayloadBytes = 200<<10`. Only that test header changed; the other 210 pinned
  entries and production sources are unchanged. Its affected TCP recovery
  binary was rebuilt warning-free and all 19 scopes pass with leak checks.
  Original full-suite snapshots are preserved; the current 211-entry pin map
  and incremental evidence are in
  `/tmp/zfix-repair2-partial-size-verification.json`.
- `/tmp/zfix-repair2-final-itest-verification.json`: warning-free default-all
  integration build; TCP six programs/127 scopes, TLS six/132, demo two scopes,
  13 programs/261 total. All 42 demo scenario results pass. No sanitizer findings;
  211 source/build/config hashes match before/final.
- Current process path files `/tmp/zfix-repair2-acceptance-{canonical,lifecycle,
  probe,sdk-policy}.path` resolve to retained cases/manifests. Their results are
  canonical76, lifecycle24, probes6 and SDK policy4, all passing. The SDK-only
  policy gate supplies no native displacement evidence; that is covered above.
- `/tmp/zfix-repair2-final-{src-build,unit-build,unit-test}.log`: library and
  unit builds, schema gates and 29 TAP programs/347 scopes; sanitizer clean.
- `/tmp/zfix-association-chain-final-summary.json`: warning-free two-target
  build, TCP58/TLS64, three A→B→C/SIDE cases and three terminal-stop cases per
  transport, six TLS pre-authentication failure/C cases, unchanged configuration.
- `/tmp/zfix-replay-replacement-summary.json`: focused TCP18/TLS18 replay,
  Logout, application-history/archive and post-cleanup probe proof. Its explicit
  pre-readiness-gate snapshot is retained. The final integration verification
  now supplies the current recovery TCP19/TLS18 results.
- `/tmp/zfix-managed-partial-verification.json`: managed TCP recovery19 passes,
  including actual EAGAIN/partial output and 17 strict displacement assertions;
  no build or memory diagnostics. The fixture is TCP-only, not a TLS claim.
- `/tmp/zfix-repair2-final-{util,example,interop}-build.log`: current consumer
  builds pass warning-free. `/tmp/zfix-repair2-acceptance-native-io.log` records
  all 18 native workload cases. `/tmp/zfix-repair2-acceptance-examples.path`
  points to both current standalone TCP/TLS pairs, all four actor exits zero.
- `/tmp/zfix-cold-fifo-audit/{PROOF.md,verification.json}`: no external cold
  preparation obligation exists; extra WorkBudget/ColdReturn machinery removed.
  Utility build and four lifecycle cases pass, all 20 child exits zero.
- `/tmp/zfix-tx-error-audit/`: restored native TCP10/TLS10 and targeted negative
  controls. Physical repair details remain in [TRANSPORT_REPAIR.md](TRANSPORT_REPAIR.md).

The first expanded session run passed all new cases but failed an older
reloadBudget assertion that read both peers' aggregate stop count while holding
only client Tx. The corrected test snapshots that client's own Rx stop report
with its still-charged admin reservation; exact bytes/count and both final
closes remain required. The failed log is retained as
`/tmp/zfix-association-chain-tcp.log`; final TCP58/TLS64 both pass.

## Independent requirement-by-requirement audit

Source and retained evidence were inspected; this audit did not run another
build or workload. Raw case-status and final integration metadata checks are
recorded in `/tmp/zfix-repair2-independent-completion-audit.json`. All explicit requirements in plan sections 1–8 are covered:

| Plan section | Source and proof disposition |
| --- | --- |
| 1: persistent-link correction | Example, utility, demo, session and recovery factories select application-owned links. Registry code/build/API and exported library symbols are absent. |
| 2: identity/progress/lifetime invariants | Checkpoint identity is opaque ID/epoch; native and managed replacement preserve sequence frontiers. Work/load and app-controlled replay survive; no canceled-assignment reuse or predecessor activation barrier appears. Existing reset/version-change and terminal-drain cases pass. |
| 3: native prerequisite | Ztcp/Ztls abruptly close the old Cxn and retire its physical queue/context before successor publication; stale cleanup only releases its original references/accounting. Owner-bound TCP target and TLS key target prevent re-resolution onto B. Native negative controls and restored TCP10/TLS10 pass their intended verdicts. QUIC audit found no equivalent defect. |
| 4: application routing | One retained handle per application-selected session; A/SIDE/B/C routing proves independent-ID progress during both switches. Tool quotas remain documented application connection-count limits, not duplicate-owner policy. Standalone examples retain no regression harness. |
| 5: completion authority | Socket bind advances the existing connection; authentication opens FIX/Wire on that generation. Actual-Link admission/timer/failure/reset cases and real managed pre-authentication/C/terminal cases check stale authority and exact resource return. Delayed logical Work/load and replay/archive remain app-owned. |
| 6: registry/dependent removal | No Attach registry symbols or registrations remain; Tx's separate m_attached and unrelated protocol Duplicate checks are retained. Native refusal expectations were removed from the external SDK policy gate. Current API/endpoint/test/status documentation describes persistent replacement and labels old evidence. |
| 7: required regressions | Logon/deferred admission, established Work/load, Logout/recovery, accepted work, actual partial Rx/output, held late callbacks, A→B→C, authentication failure/no fallback, terminal stop, SIDE routing, ordinary/cold restore and app resend/archive have direct fixtures. Final unit347, integration261, native18, example2 and the recorded process gates pass without configuration/dependency changes. |
| 8: completion/review | Ownership/allocation/copy and per-field necessity review is in REPAIR2_REVIEW.md. Final tests retain strict failures, sanitizer settings and exact lifetime/count assertions. Source/config snapshots are unchanged across the final runs; no explicit requirement remains open. |

The managed partial-output EAGAIN fixture is Linux TCP-only. TLS target and
physical-output isolation have their separate actual native regressions; the
managed TLS session/replay suites supply their documented protocol coverage.
No undocumented full Cartesian matrix, platform, install or benchmark gate is
inferred. The external SDK shutdown race remains a disclosed independent
limitation rather than a silently repaired dependency.

## Historical execution records


The following records describe intermediate snapshots; the table above and
`STATUS.md` hold the current result. Failed artifacts remain preserved.

### Integration failure under repair

The first rebuilt TCP session run failed 7/46 scopes and reported leaked owner
allocations. Review identified the replacement `transportDown_` guard dropping
pre-establishment native retirement callbacks, preventing setup-attempt stop
completion. The fix must preserve duplicate-down filtering while allowing these
connection-zero retirement paths. A focused actual-Link regression is being
added; this is not dismissed as a fixture-only failure. The original output is
retained in `/tmp/zfix-repair2-tcp-test.log`.

## Additional verification snapshot

`make -C zfix/test -j2 test` passed all schema gates and 29 programs / 325 scopes
before the new setup-retirement regression was added. The physical-close utility
build passed, followed by all 18 cases in `native_io_test.py` (TCP and TLS,
including expected quota refusal). Artifacts: `/tmp/zfix-repair2-native-pairs`;
logs: `/tmp/zfix-repair2-unit-all.log`, `/tmp/zfix-repair2-util-build.log`, and
`/tmp/zfix-repair2-native-pairs.log`. This is ordinary workload coverage, not
proof of the still-open native displacement handoff.

## Stop generation and reset diagnostics

Corrected session TCP/TLS runs fixed setupCleanup but still failed four scopes
(initial admin refusal and Logon preparation refusal). The Tx stop guard had
compared Rx's stop generation to a coordinator that had never opened. A pending
stop request now carries its actual generation through final completion; a new
actual-Link refusal regression is added but not yet rebuilt.

Rebuilt recovery TCP passes all 12 scopes. Rebuilt reset TCP exposes a distinct
unexpected Output fault on epoch 2 / connection 2 after StartedClose, plus
reset-Logon count mismatches after prepared-close/timeout. Ordinary reconnect
versus live replacement reset metadata is under investigation. No oracle was
relaxed and native reset success is not claimed.

## Current-source rebuild

The current full unit/schema target passes 29 programs / 327 scopes, including
initial-refusal stop completion. The current integration rebuild completed;
six TCP and six TLS programs are running against the corrected stop-generation
and ordinary-reconnect/reset behavior. Current logs are
`/tmp/zfix-repair2-unit-current.log`, `/tmp/zfix-repair2-tcp-current.log`, and
`/tmp/zfix-repair2-tls-current.log`. Utility/example rebuilds remain in the
active build chain. These runtime results are still pending.

The duplicate-connection interop gate now tests only QuickFIX acceptor policy
(four TCP/TLS cases with native or QuickFIX initiators); native duplicate-refusal
expectations are removed. Its Python syntax check passed, but its revised
process gate is not yet run. This change supplies no native displacement proof.

## Current integration result

All six TCP and all six TLS native integration programs pass on the corrected
source: 102 scopes per transport, 204 total. This resolves the prior setup,
initial-refusal drain, ordinary reconnect/reset, and recovery failures. Logs:
`/tmp/zfix-repair2-tcp-current.log` and
`/tmp/zfix-repair2-tls-current.log`. These ordinary integration cases do not
exercise simultaneous native displacement. A dedicated native reproducer is
being added; the generic transport prerequisite remains unimplemented.

A separate source review identified a permitted early-timer snapshot queued
behind Tx reset commit, carrying an obsolete epoch on the same connection.
Wrapper handling and a controlled regression remain under review; the passing
integration results do not discharge that scheduling case.

## Additional completed checks

Current consumer builds completed. Demo process checks pass two scopes; both
standalone example pairs pass with successful exits and empty sanitizer stderr;
all four revised QuickFIX-server rejection-policy cases pass. Logs/artifacts:
`/tmp/zfix-repair2-demo-current.log`, `/tmp/zfix-repair2-examples`, and
`/tmp/zfix-repair2-sdk-duplicates`.

The actual-Link early-timer/reset ordering regression passes. Pure timer-only
updates now remain Rx-local while retaining observer notification; no obsolete
Tx snapshot or admin reservation is created. Focused Wire now passes 16 scopes
(`/tmp/zfix-repair2-timer-build.log`, `/tmp/zfix-repair2-timer-test.log`). Ordinary
consumer results above precede that final timer-post elision. Native displacement
reproduction and its transport prerequisite remain open.

## Native displacement baseline

The dedicated native replacement scope is now built for TCP and TLS in
`transport_test.cc` (existing five scopes retained). It defers A's native close
callback without blocking a worker, establishes B on the same persistent server
link, confirms actual partial input on B, then releases A's cleanup and checks
B processing, abrupt-close syscall behavior, and physical shutdown accounting.
Baseline runtime checks against unchanged dependency headers are running;
logs: `/tmp/zfix-repair2-displacement-tcp-before.log` and
`/tmp/zfix-repair2-displacement-tls-before.log`. A failed terminal-accounting
baseline deliberately exits nonzero rather than destructing a live hub; no
leak-clean claim is made for that negative path.

## Reproduced native prerequisite failures

The new replacement baseline fails on both unchanged native transports. TCP
emits a stale link-down callback after B is established and loses B's partial
input, preventing its reply. TLS reports that physical accounting did not drain.
Logs: `/tmp/zfix-repair2-displacement-tcp-before.log` and
`/tmp/zfix-repair2-displacement-tls-before.log`. Both return nonzero; the negative
TLS path deliberately exits before destroying a live hub. These failures are
not accepted as passing behavior and cannot be covered by managed-owner tests.

All 24 lifecycle interoperability cases passed against the preceding ordinary
consumer snapshot (`/tmp/zfix-repair2-lifecycle.log`, artifacts
`/tmp/zfix-repair2-lifecycle`). No SDK change, workaround, suppression, or
native-displacement claim is implied by those results.

## Remaining scope boundary

The native failures require changes to Zi/Ztcp/Ztls transport headers, not an
application attachment registry or a Zfix-only wrapper. Section 3 of the plan
explicitly places dependency edits outside this execution scope and requires
that scope to be resolved separately. No dependency edit or rebuild has been
performed. Final Zfix-only build/schema/unit checks and the canonical process
matrix are running; the native replacement regressions remain failing gates.
Their failures must not be hidden, skipped, or reclassified as success.

## Canonical runner/build overlap — invalid verification attempt

The canonical runner was mistakenly started alongside the final build. Its
native-native TLS-duration server failed to load the actively relinked Zfix DSO
(`file too short`), while the other pair groups completed. This is a build/run
overlap, not a demonstrated protocol failure; the attempt is not accepted as
verification of the final source. Original artifacts remain at
`/tmp/zfix-repair2-canonical`. Rerun the matrix only after the build is terminal.
The final unit/schema result is 29 programs / 328 scopes passing.

## Initial verdict (historical)

The full goal is not achieved. Native TCP displacement loses successor input;
native TLS displacement fails terminal physical accounting. The safe handoff,
FIX association integration and remaining native regression matrix require the
transport prerequisite repair excluded by plan section 3. That scope must change
before implementation can continue toward completion. Passing scoped checks
cannot waive those requirements. Final evidence is summarized in
`/tmp/zfix-repair2-summary.json`; current source/build/script hashes remain pinned
and configuration checksums match the original build.


## Historical verdict after authorized stale-close repair

The full goal is not achieved. TCP no longer loses successor input to stale
close cleanup, and TLS physical accounting now drains. Eight TCP and nine TLS native scopes
pass, including queued partial output,
closed-socket setup, replacement between cleanup stages, early TLS refusal, and
encrypted A output released after B association. Each targeted fault-removal
control fails its corresponding regression. Unit/schema and affected managed
integration reruns also pass;
see TRANSPORT_REPAIR.md for logs. QUIC review found no equivalent bug and made no
change. The early FIX association boundary and full combined native replacement
matrix remain incomplete requirements. Older failure and scope records above
are historical; they do not override this current verdict.


Current restored-source verification: 29 unit/schema programs / 328 scopes;
six native TCP programs / 105 scopes; six native TLS programs / 106 scopes.
All pass. `/tmp/zfix-transport-final-summary.json` and its source manifest
record this phase. No full goal completion or broad process-matrix rerun is
claimed. The physical queue and TLS target repairs do not waive the early FIX
association requirement or the remaining matrix in ASSOCIATION_REPAIR.md.


## Early association implementation — initial verification snapshot

Managed State/Rx/Tx now separate association from authenticated start. The
existing connection number advances at the native socket hook; no Logon or FIX
deadline starts before authentication. The existing live flag now represents
association, while session state represents authentication/FIX phase. Native
hooks carry the predecessor-presence snapshot so a third pre-authentication
socket preserves an armed reset without a stored phase mirror. TCP's Tx-owned
producer destination is installed after protocol bind and before authenticated
publication, preventing an old producer from selecting B in that interval.

Raw now uses the same split and scopes old output outcomes to their connection.
Deferred external stop hooks use lazy one-shot callbacks and aggregate owner
obligations; they cannot retire an active successor. The direct Raw stop API
and its consumers are migrated. Cold workload-owner restore must also move
before association; that consumer migration is being implemented.

The first focused owner build exposed private-base access in new LinkTx test
assertions (`/tmp/zfix-association-owner-build.log`); production APIs are not
being widened for those assertions. The unaffected State/Rx/Raw owner programs
build and pass 67 scopes (`/tmp/zfix-association-owner-initial-test.log`).
Actual-Link and combined native FIX/Raw regressions, the complete owner build,
consumer rebuild, and remaining process gates are not yet verified. The
previous 142-file native-phase source manifest records its earlier snapshot;
it is not a source-unchanged claim for this newer implementation.

## Resumed verification

The existing build compiled the six new `resetCloseOutcome` schedules in
`ZfixWireTest`. The baseline fails five of those six cases (scopes 18–22 of
25); uncommitted reset with no successor already passes. Build and failure
logs: `/tmp/zfix-reset-close-baseline-build.log` and
`/tmp/zfix-reset-close-baseline.log`. The repair must reconcile the exact
logical reset agreement across physical close; broad stale-notice acceptance
is not justified.

The repair now retains existing reset agreement fields until the matching Rx
outcome acknowledges them; close revokes commit authority without forgetting
the outcome. Rx validates old outcomes by epoch/target/drain, and Tx validates
the action-scoped request ticket before clearing its agreement. No new stored
state or transaction container was added. Source build and four focused
programs pass, 116 scopes including all six new Wire schedules. Logs:
`/tmp/zfix-reset-outcome-src.log`,
`/tmp/zfix-reset-outcome-units-final.log`. Leak checking remained enabled with
exit 99 on sanitizer failure; there were no diagnostics. One old synthetic
Close test now includes the target/drain fields supplied by real Rx events.

Latest focused passing evidence before those six cases was added: Wire 19,
LinkTx 32, State/Rx/Raw 67 scopes; native transport nine per TCP/TLS target;
Raw integration seven per target; cold native-native reset/recovery four
lifecycle cases. Logs use `/tmp/zfix-association-`. The latest managed native
fixture and stale Tx-error policy regression sources are being compiled and
run. These checks do not yet prove the complete section 7 matrix.

The managed native session fixture now passes 49 scopes on each TCP/TLS target
with the corrected application queue/history, one-shot workload start and
graceful Logout cleanup. Its three added cases retain A's deferred admission
and native close while B binds and exchanges traffic. Evidence:
`/tmp/zfix-association-native-session-policy-summary.json` and corresponding
TCP/TLS logs. Native transport Tx-error fault-removal controls fail exactly
their new scope per target. Restored candidates rebuild and pass ten scopes
per target with no sanitizer errors; headers match the frozen candidates byte
for byte. Artifacts are in `/tmp/zfix-tx-error-audit/`, including
`control-exits.log` and the preserved negative/restored logs. The restored
transport binaries use the repaired reset DSO; the session fixture's 49-scope
runs predate that reset repair.

Factory reinspection found another managed single-ID acceptor in
`itest/zfix_demo_io.hh` constructing a new logical owner per accept. It now
routes to one application-owned `Hub::connection`; the driver releases that
handle after the multiplex stops. Its build and sequential demo process gate
pass two TAP scopes covering all three modes and TCP/TLS scenario scripts,
with leak checking enabled and no sanitizer diagnostics. Logs:
`/tmp/zfix-demo-persistent-build.log`, `/tmp/zfix-demo-persistent-test.log`.
Generic transport loopback's independent socket
owners and Raw external-manager fixtures are separate contracts, rather than
managed FIX session routing precedents.

The first demo process invocation was started before its build handle was
confirmed terminal. Its passing output is retained as
`/tmp/zfix-demo-persistent-overlap-invalid.log` and is excluded from evidence.
The build is now confirmed terminal; the sequential rerun uses
`/tmp/zfix-demo-persistent-test.log`.

Remaining combined managed evidence must include the following, rather than
infer it from the Raw or standalone sequencer tests:

- Established A with delayed accepted receive/application work and pending Tx
  load now passes all three managed modes on TCP/TLS (52 session scopes per
  transport). Evidence: `/tmp/zfix-association-native-work-summary.json`.
  A third managed successor and another routed logical ID's traffic during
  displacement remain required.
- Replacement while Logout or application-driven resend/recovery is actually
  pending; stale outcomes must neither close B nor complete B's operation.
- Application-selected resend/suppression and archive callbacks crossing
  replacement, with exact ranges, assignments and callback accounting.
- B refusal/authentication failure with A callbacks held; terminal application
  stop during overlap and a late candidate must not restore or reopen A.

Native TLS socket-only failure/C coverage now passes six added cases (three
modes × raw peer close/fatal TLS alert). B emits no FIX timer/Ready/assignment;
C resumes the same server owner at connection 3/epoch 1 and exchanges before
and after held A cleanup. TLS session total is 58; TCP remains 52 for this
snapshot. Evidence: `/tmp/zfix-preauth-audit/{tcp,tls}.log`. Terminal-stop and
independent-ID overlap additions remain in progress.

Live replay/Logout/archive verification now passes six added cases per
transport (three modes × established recovery/Logout). Each holds the original
application lookup over B association, selects repriced seq2, suppresses stale
seq3 and checks one decoded application disposition. Actual peer recovery
requests can overlap/repeat; completed application lookups and publications
are counted separately rather than imposing an invalid one-replay rule.
Archive callbacks form a contiguous prefix ending at the acknowledged frontier.
After the complete A native cleanup chain, B completes a new probe whose
published sequence pins the required archive boundary, preventing leftover
notification tokens from satisfying the check. Old failover's polling loop
was also replaced with an owner completion signal. Logs and source hashes:
`/tmp/zfix-replay-replacement-summary.json`. These binaries are a focused
snapshot; the final rebuild must include the new server readiness gate.

The late-stop TCP baseline fails only the three newly added managed-mode
scopes; the corresponding TLS 61 scopes pass. A late accepted TCP connection
reaches authenticated FIX start after native server stop processing begins.
`SrvLink::connectReady_()` now chains the existing native hook and server
`running()` state. This adds no state and leaves native socket accounting to
the existing stop barrier. Combined positive validation is in progress;
baseline evidence is in `/tmp/zfix-preauth-audit/stop-both-{tcp,tls}-baseline.log`.

The gate's three terminal-stop cases now pass on both transports, and the
combined chain cases pass too: established A→B→C retains Work/load while SIDE
exchanges during both displacements, with an actual incomplete A FIX prefix
observed before B binds. TLS passes all 64 scopes. TCP's first combined run
failed one older reloadBudget assertion: it incorrectly prohibited either peer
from reporting stop while only the client's Tx shard was held. The fixture now
samples the client's own stop report alongside its retained admin credit;
the final exact two-peer close count remains required. The final warning-free
build and current reruns pass TCP58/TLS64; the original failure is preserved.
Evidence: `/tmp/zfix-association-chain-final-summary.json`.

Current library/unit verification passes source default-all, unit default-all,
all schema gates and 29 TAP programs / 347 scopes, with leak checking enabled
and no sanitizer findings. Logs: `/tmp/zfix-repair2-final-{src-build,unit-build,unit-test}.log`.
Remaining consumer/source additions still need their final verification.

The earlier cold-credit finding was withdrawn after tracing per-owner FIFO:
either the round guard skips stale preparation, or A's preparation and final
return precede B's externally reported terminal completion. Existing retained
owners and stale-publication checks suffice. Extra WorkBudget/ColdReturn
machinery was removed; no regression will simulate an impossible order by
skipping a queued task. Source proof is in REPAIR2_REVIEW.md. The utility build
and four normal cold native-native lifecycle reruns pass after removing that
machinery; all 20 child exits are zero with no sanitizer findings. Evidence:
`/tmp/zfix-cold-fifo-audit/verification.json` and `PROOF.md`.

Once these repairs settle, rebuild the current Zfix consumers and rerun the
authorized unit, integration and canonical process gates without overlapping
DSO relinking with running consumers. Recheck configuration hashes and complete
the source/ownership review before claiming completion.
