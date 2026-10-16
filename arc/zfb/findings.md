# Zdb operation and field-type audit: HEAD versus worktree

Static source analysis of `HEAD` (`6e9a1252`) and the current worktree, tracing
`zdb/itest/zdbsmoketest.cc` through the memory-store backend to `MemRow`,
extended to updates, deletes, saga submission, execution, compensation and
recovery. The field audit covers every `ZfFieldTC` code exposed by `ZfStruct`,
including every `...Vec` code, distinguishing typed codec support from memory-store
Tuple support. The original audit did not cover the sqlite3 and postgres stores.
The original audit below performed no implementation changes, builds, tests or
benchmarks. Subsequent repair evidence is recorded in section 9 and `plan.md`;
the original findings describe the audited state before those repairs.

**One definite additional allocation/copy was found: bitmap decoding into the
backend Tuple.** The thread route and overall serialization architecture remain
unchanged. Other differences below are distinguished from this confirmed
regression; static analysis does not establish their net timing impact. The
broader audit also finds unsupported memory-store bindings that silently become
void values, and existing costs that become significant for large vectors or
sagas. These are detailed separately below.



## Repair2 completion — audit115

The consolidated repair is complete within the authorized current-build scope.
The final requirement audit covers all5phases,RQ1–RQ4,retained data-store decisions,
copy/ownership/type/alignment/sharding/diagnostic/size-policy contracts,canonical
interop,owning/store/remaining-enabled suites and final memory gates. Read-only
verification pins850current source/build/artifact identities,23retained memory
performance artifacts,9matching clean memory logs and26accepted remaining targets,
including explicit public Binance execution. Only the2status108SQLite identities
changed sinceaudit104; their incremental source/unit/integration/memory refresh
passes. No new production build/test/Valgrind/configuration change for this audit.

RQ4 is accepted as an engineering assessment within measured workload scope,
with ordinary-operation residuals explicitly retained. SQLite small insertp50
remains+1.24%(316ns),p95/p99−0.43/+0.26%; it is not erased or called noise.
CurrentSQLite saga−9.75/−20.82/−6.91% atwidth32,−9.37/−21.02/−5.61% at4096;
PostgreSQL saga−29.91/−30.79/−30.23%. Memory saga also improves with about15–16
fewer tracked requests. Same-iteration original release codec+store insert sums
+0.54/−0.73/+0.14% and all11store-phase sums−0.45/−1.30/−0.03% atwidth32;
sums exclude gaps and do not replace constituent metrics. All original residual,
variability,maintenance and failed-baseline evidence is retained. No requirement
for every quantile to improve or exact causal attribution is invented.

The original corrected canonical baseline is6e9a1252 plus only the scheduler
queueCount ordering guard. Exact control source/DSO verified; unmodified original
failures stay historical. Diagnostic padding/interposition does not replace
release acceptance and is not applied to production. Oversized transmit splitting,
generic retained-payload copies,required owning boundaries and whole-vector updates
retain their documented costs. Logical completion is not durable commit; partial
heap telemetry and sampled workload limits remain explicit.

Evidence and all requirement dispositions: audit115/README.md,verification.json,
current-files.json,memory-logs.json,accepted-command-records.json,input-hashes.json.
This checkpoint supersedes older incomplete/pending status text for its current
source generation. zhttp remains excluded and uncertified by user instruction;
other compiler/flag/platform configurations are outside scope. PostgreSQL fixture
stopped. Work remains uncommitted; no install,clean,reset or deployment.

## SQLite same-binary producer-context control — gap114

Two copied benchmark executables are built with retained release configuration,
linking unchanged canonical/native production libraries. Same binary/implementation
runs gap0 and minimum20microsecond producer interval before timed insert. Full
store workload retained; no production delay/fix. All24latency processes0/clean,
all11ops3000samples/process,4loader smokes/7worker masks/byte sets/hashes verified.

Gap0 insertp50+1.96%(496ns),gap20000+0.53%(135.5ns); paired change-in-differences
−331ns,5of6pairs shrink. Demonstrates sensitivity to producer timing/context,
not exact causal decomposition or proof that the original316ns is noise. No
padded workload replaces release acceptance. Update-one's gap20p50 grows+1.80%,
so all metrics remain reported. No speculative production edit follows.

Original sqlite109 data reanalysis: same-iteration codec+store insert
+0.54/−0.73/+0.14% atwidth32;−0.71/+0.09/−0.10% at4096. Sum ofall11timed
store-phaseops−0.45/−1.30/−0.03% and−4.10/−0.52/−0.45%. Sums exclude gaps and
are not uninterrupted end-to-end wall times. Constituent releaseinsert+1.24%
(316ns)remains authoritative.850current identities unchanged;postgres stopped.
Evidence: gap114/README.md,summary.json,release-grouped.json,input-hashes.json,
exactsource/build/process records. Performance residual disposition and final
requirement audit remain to be completed; no broad completion claim yet.

## SQLite scheduler/completion handoff isolation — handoff113

Six alternating corrected canonical/current external store-only pairs,width32,
1000×3:all12processes0/no ERROR/FATAL,all11ops3000samples/process. Each process
has21028main-thread enqueues,zero failed pushes; all12012runtime write enqueue
sequences match. Every write completion is paired with its actual same-semaphore
main wait. Separate loader smokes verify the shim forwards to the approved
scheduler control/current library. Original/current/control artifacts unchanged.

Instrumented insert p50 dispatch−30ns(−0.98%),post-to-wake−1ns(−0.03%),
commit-to-post+14.5ns,full+104.5ns(+0.41%),with mixed dispatch/wakeup/fullpairs.
Probe overhead changes the observed gap; it neither explains nor clears the
release+1.24%(316ns)insert residual. No extra enqueue/overflow/relay allocation
demonstrated and no speculative scheduler edit. handoff112's earlier diagnostic
generation is preserved separately;113adds atomic owner publication and explicit
semaphore identities. No production build/source/configuration or memory rerun.
Evidence: handoff113/README.md,summary.json,loader-verification.json,
input-hashes.json,raw source/build/command outputs. Full repair remains open.

## SQLite transaction boundary isolation — txn110/txn111

Two external diagnostic controls use the accepted corrected canonical/current
production generations,each6alternating width32store-only pairs,1000×3. All24
processes exit0/no ERROR/FATAL; all11ops3000samples/process; artifact hashes match.
No production source/configuration change or rebuild, no correctness/memory rerun.

Aggregate insert body adds25.25ns; quantile control adds40.5ns at paired p50.
The quantile probe maps2setup+3003four-write cycles exactly to CSV operations,
excluding each round's warmup, and computes outside-transaction duration per
same-operation clocks before quantiling. Instrumented insert fullp50+2.30%;
outside transaction+556ns (+8.42%,all6pairspositive), transaction+155.5ns(+0.79%).
Quantile deltas are not additive. This narrows the investigation to dispatch,
envelope/callback/teardown/wakeup time; it does not isolate a particular cause.

Source comparison retains moved buffer/CommitFn,one store post,borrowed RecordView,
unchanged store dispatcher/ZmBlock/ZmFn/ZmRef/ZmSemaphore and IOBuf pool shape.
No demonstrated extra copy or heap post found there; no speculative edit applied.
Probe overhead includes clocks/classification and workload-sized external mmap
writes; diagnostic timings do not replace sqlite109's release+1.24%(316ns)insert
residual. Full repair remains open. Evidence: txn110/README.md,txn111/README.md,
summary.json,input-hashes.json,raw outputs/source/build and command records.

## Current SQLite corrected-baseline refresh — sqlite109

The production library accepted in status108 has now completed the corrected
canonical comparison at widths 32 and 4096: 24 latency and 8 separate counter
processes, all exit 0 without application errors, all 16 operations retained.
Six alternating pairs/width yield 18000 latency samples/op/version/width; loader
selection, seven worker CPU masks, sample counts and unchanged artifacts verified.
Read-only checks reverify all 850 current file identities and nine accepted memory
logs (including status108's refreshed SQLite gate). No rebuild or suite rerun.

SQLite saga p50/p95/p99 changes −9.75/−20.82/−6.91% at width32 and
−9.37/−21.02/−5.61% at4096, all six pairs improve each quantile. Tracked requests
fall 50.557→43.428 and 50.540→43.228. Unchanged postgres retains corrected101's
−29.91/−30.79/−30.23% saga result. Approved baseline remains canonical 6e9a1252
plus only the scheduler ordering guard; original failures remain separate.

Small SQLite insert retains +1.24% p50 (paired +316 ns; all six pairs +0.67 to
+3.36%), p95/p99 −0.43/+0.26%. Width4096 insert −0.25/+0.05/−0.08%.
Small update-one +0.98/+0.09/+0.48%, mixed p50 pairs. Saga acceptance is fulfilled;
full repair completion remains unclaimed pending sustained ordinary-store residual
disposition. Separate-generation differences do not isolate the status108 change.
Evidence: sqlite109/README.md,summary.json,input-hashes.json,
current-evidence-verification.json,run.py,analyze.py and all raw records. Counter
telemetry is partial/asynchronous; saga completion is logical, not durable commit.
Postgres stopped; current configuration retained; user-excluded zhttp uncertified.

## SQLite success-path error-object repair — status108

Replaced the transaction wrapper's Event return and insert helper's Event flag
with boolean status; construct paramSizeError only on failure. Existing cap checks,
duplicate-row distinction, rollback-before-callback, exceptions and completion
paths remain intact. This removes unnecessary success-path object construction.

Isolated six-pair full-workload A/B (all12processes0,all16ops3000samples/process)
records insert p50/p95/p99−0.50/−0.63/−0.13%,update−1.06/+0.07/−8.18%,saga
−0.26/−2.32/−3.75%. Pairs are mixed; not a full540ns attribution or fresh canonical
acceptance. Applied the narrow source delta after this scoped comparison.
Incremental SQLite source build,unit6checks and integration56checks pass. Refreshed
SQLite Memcheck0errors,zero definite/indirect/possible lost bytes. An initial
memory harness invocation omitted connection arguments; preserved as setup failure,
not counted as acceptance. Only source/module hashes change among850audit identities.
Other eight memory programs and unchanged dependents retain their prior evidence.

Evidence: status108/README.md,verification.json,current-verified-files.json,
accepted-artifact-changes.json,accept-commands.json,final-memory logs,exact source
snapshots and isolated A/B raw results. Current production performance against
the corrected canonical baseline still needs refresh; older measurements refer
to the explicitly retained pre-status module generation. No broad completion claim.

## SQLite planning and statement execution — plan106/step107

Matched source-extracted parameter planning adds about6.5ns:17.52→24.04ns at
width32,17.53→24.11ns at4096. Native required size-policy checks remain in place.
All24component processes0/empty stderr; identical encoded scratch,production and
control artifacts unchanged. Source extraction is labeled,not misrepresented as
a callable original-module planner. No production change/rebuild.

External SQLite-step timing adds one diagnostic shim,with6alternating full store
pairs,width32,1000×3. All12processes0/no ERROR/FATAL,identical statement counts.
INSERT/BEGIN/COMMIT paired mean changes+0.32/−0.10/−0.12%; UPDATE−1.61%.
Instrumented full insert p50 still+1.67%. Timings are diagnostic only,with setup
and multiple write operations grouped by statement class. This narrows the
remaining540ns uninstrumented insert cost toward envelope/wrapper/callback/
dispatch work; it does not establish causality or clear the original+2.13%.
Evidence: plan106 and step107 README.md,summary.json,input-hashes.json,build and
command records/raw outputs. No speculative production edit or completion claim.

## SQLite compiled binding cost — bind105

An isolated fixture calls each existing compiled bindInput<XFieldRefs> by its
exact typed signature/ELF offset and verified module base. Same nine-field row,
widths32/4096,6alternating pairs,width×10000bindings×7rounds,CPU8. All24processes0
with empty stderr; conversion-scratch hashes match across versions. Actual loader
selection and original/current/control artifacts verified. Two fixture generations
are retained after correcting a printf warning; no production rebuild/change.

Canonical/native batch means are145.58/150.97ns at width32 (paired+3.49%,about5ns)
and794.86/786.84ns at width4096 (−0.64%). This is binding only,through the compiled
reference-list variant; insert's inlined XFields variant, planning, callbacks,
SQLite stepping/commit and dispatch are excluded. It does not account for the
roughly540ns full-store insert discrepancy by itself. No speculative source fix
or completion claim. Evidence: bind105/README.md,summary.json,commands.json,
build.json,symbols.json,loader-verification.json,input-hashes.json and raw outputs.

## Current requirement audit and residual analysis — audit104/residual103

Read-only identity/log review reverified850current files,9clean matching memory
logs,26accepted dependent targets,5pinned original sources and23retained memory
benchmark artifacts. No rebuild/test/Valgrind rerun. The corrected long saga gate
is fulfilled; original baseline failures remain historical. SQLite update p99's
boundary explanation is retained without filtering acceptance samples.

Exact paired median-ratio intervals cross parity for small-memory update and
large-postgres write residuals; this is descriptive uncertainty,not equivalence.
Grouped sums of all11timed store-phase operations change+0.04/+0.61/+0.10% for
sqlite3 and+0.29/+0.74/+0.64% for postgres. Individual metrics remain authoritative.
SQLite width32insert p50 is consistently+2.13% (all6pairs+1.73to+2.76%),p95/p99
+1.81/+0.06%. This is the remaining concrete attribution target; saga gains and
aggregate parity do not silently clear it. Completion is not yet claimed.
Evidence: audit104/README.md,verification.json,verified-files.json,memory-logs.json;
residual103/README.md,summary.json,input-hashes.json,analyze.py. Current configuration,
size policy, type coverage and user zhttp exclusion remain unchanged.

## SQLite update-tail mechanism — sqlite102

Four alternating syscall-traced original/current store phases exit0 clean, with
unchanged release artifacts. All12012runtime write transactions/process have
identical page/commit patterns; both versions have exactly15update checkpoints
and15WAL restarts at identical iteration positions. No additional copy,
allocation, store post or database write was demonstrated by source/counter/trace
review. No production edit or rebuild.

Those30positions match the30slowest updates in every one of12corrected101 timing
runs. With3000samples and the retained percentile definition,p99 is the31stlargest
sample: the single slowest ordinary update. The+47.31% paired p99 remains reported;
neighboring unfiltered p99.1/p99.9 change+0.18/+0.13%,maximum+0.31%. This identifies
a percentile boundary sensitive to one isolated delay,not the cause of every
delay. Conditional distributions are diagnostic and do not replace acceptance.
See sqlite102/README.md,summary.json,tail-summary.json,raw traces and original
input hashes. Original full saga gains and other residuals remain separately
recorded. No speculative optimization or blanket per-run tail guarantee follows.

## Corrected canonical long saga comparison — corrected101 completed

The approved guard-only canonical baseline completed the matched full workload:
six alternating pairs/backend,width32,1000×3iterations,18000samples/op/version.
All24latency and8separate counter processes exit0 without ERROR/FATAL; library
selection, seven worker masks, server placement, artifact/config hashes and cleanup
are verified. No production code change or rebuild. Original failures stay separate.

Native saga p50/p95/p99 changes are−9.67/−16.85/−4.44% for sqlite3 and
−29.91/−30.79/−30.23% for postgres. Every saga quantile improves in all six pairs.
Tracked requests fall50.450→43.380 and55.965→47.997 respectively (600samples/version;
partial Z heap telemetry and asynchronous-window limits). The corrected baseline
resolves the long-saga gate. Scope is logical completion, not durable commit.

SQLite ordinary update p99 remains+47.31%,with p50/p95+0.36/+0.99%; per-process
p99 ranges43.04–125.80µs canonical and48.77–127.82µs native. Five of six paired
p99 ratios favor canonical. No cause is established, no blanket tail pass is
claimed, and no speculative codec change follows. Existing large-width and
memory residuals remain recorded separately. The zhttp stop-building scope persists.
Evidence: corrected101/README.md,summary.json,input-hashes.json,analyze.py and raw
CSV/commands/placement records; report100 incorporates all operations. The pending
baseline preference in historical checkpoints is superseded by this approved run.

## Corrected baseline authorized — corrected101

The user approved using the corrected canonical baseline for long saga acceptance:
`6e9a1252` plus only control85's `queueCount` ordering guard. This supersedes the
pending baseline decision in the historical checkpoints below. Canonical
FlatBuffers, existing release configuration and original artifacts are retained;
no coordinator lifetime changes are backported. `ordered` labels this corrected
baseline; `candidate` labels the native ZiFB worktree. Original failed workloads
remain separate evidence. The matched full-workload measurements and exact
commands are recorded in `zdb/interop/evidence/repair2-20261010/corrected101/`.
No production change or rebuild is needed. Results and performance disposition
will be recorded after the batch completes.


## Consolidated performance report and remaining baseline decision — report100

report100/README.md now presents all recorded memory codec/store/saga quantile
changes at both widths, allocation requests and payload sizes, large external
store results in their separate server-uptime scopes, short external saga results,
component measurements and failed-run/provenance limits. No inconvenient operation
or tail is filtered out. Its generator reads14existing JSON evidence files; input
hashes and evidence links verified. No new measured workload, build or production
change. A fresh audit92 identity/log review verifies850current files,9matching
clean memory logs,26accepted remaining-enabled targets and retained memory fixture
identities. Compiler currently reportsClang23.1.1,x86_64Linux; configuredrelease
flags and postgres16.3 unchanged. No application test/Valgrind suite was rerun.

The plan sets no numerical sample minimum. Failed chosen long original-baseline
runs are unusable evidence; passing short runs remain scoped evidence rather than
a guarantee of longer behavior. The canonical comparison target remains6e9a1252.
The previously requested decision about a labeled guard-only canonical long saga
control remains unanswered across module95,packets96,restart99 and this checkpoint.
Its concrete one-line patch and clean functional replays are available incontrol85/86.
Using it for long acceptance changes the pinned comparison and must not be silently
represented as originalHEAD. No further baseline rerun or speculative source edit
is justified by the same failures; no independent demonstrated repair remains.
Tail uncertainty and excludedzhttpcoverage remain accurately reported. Completion
is not claimed; this checkpoint prepares a reviewable result for the outstanding
baseline decision, without changingrepair2.md's requirements or substituting scope.

## Matched server-uptime result — restart99

The owned postgres instance was restarted before each existing original/current
store-only process, retaining settings, build artifacts, workload, schema/defaults,
pools and CPU placement. Six alternating pairs, width4096,1000×3: all12processes0,
noERROR/FATAL,11operations×3000samples each (18000/op/version). Every base-table
post-run record reports3003inserts,6006updates,3003deletes,live0,zero automatic
vacuum/analyze. All7named masks and serverCPU10 verified. AllDBdrop/stop0,final
status3;production/config hashes unchanged. No timed observer/build/source edit.

Median paired p50/p95/p99 changes: select+0.15/+0.42/+0.28%,find+0.81/+0.92/+0.78%,
recover+0.59/+0.84/+0.53%,insert+0.19/+0.37/+3.47%,update+0.11/+0.38/+3.05%,
update-one+0.14/+0.48/+1.51%,delete+0.27/+4.44/+2.44%. Prior shared-uptime
select+3.32/+2.33/+5.81% does not reproduce under this control. Select paired p50
ranges-1.53to+1.68%;p99-21.62to+15.92%,with similar large-read tail swings. This
is evidence of condition sensitivity, not a unique vacuum cause or an individual-
run tail guarantee. Restart also changes caches/background state; base counters
do not cover allTOASTactivity. Do not pool scopes or discard earlier measurements.

The same unmodified artifacts/workload can run near parity with matched server
uptime. No newly demonstrated redundant codec copy/allocation warrants a source
edit. Full performance completion remains unproven, particularly the long original
saga baseline and residual/tail variability. Patched-control preference remains
pending; this run uses unmodified HEAD and does not substitute a saga baseline.
Evidence:restart99/README.md,summary.json,input-hashes.json,analyze.py,
terminal-verification.json,rawCSV and maintenance/placement records. No process or
server remains running. Current correctness/memory artifacts were not rebuilt.

## Live relation-maintenance diagnostic — stats98

Four existing original/current store-only postgres4096workloads,1000×3,two
alternating pairs,fixedplacement,allprocesses0/noerrors and11operations3000samples
each.146externalstatsobservations0/emptystderr;runtimehashes unchanged;DBcleanup/
ownedpostgresstop0. Actualsettings showautovacuumon/default60snaptime. No source/
build/configurationchange. Observers addload;these are not performance acceptance.

FirstHEAD/currentrunsobserve no automaticmaintenance;latercurrent/HEAD eachobserve
onevacuum/analyze. Atfirstobservedmaintenance,currenthad1613deletes,dead23,17pages;
HEAD803deletes,dead30,10pages. Delayedstats/plannerestimateschangefrom-1/0to1/actual
pages. LaterHEADselectroundmedians1093.34/1046.61/1015.58µsfallaftermaintenance;
latercurrent1091.47/1098.88/1092.72µsdonotshowthesamereductiondespitemaintenance.
Thus unequal maintenance histories are demonstrated,but vacuumalone is not an
establishedcause. Freshdatabases do not establishequal planner/relationstate
throughout a workload. summary.json,settings.csv,146rawJSONobservations and
terminal-verification.json retain evidence. No additional copy/allocation shown.

restart99separately testsmatchedserveruptime by restartingtheownedinstancebefore
eachprocess,with unchangedsettings/workload/artifacts and no timedobserver.
Post-runcountersmustbechecked;do notpoolthese timingswithshared-uptimeresults or
claimrestartisolatesvacuumalone. Originalfullsagabaseline/patchedcontrolpreference
remain separate and unresolved. No completionclaim or baseline substitution.

## Process order/round diagnostic — order97

Raw store71/store72/affinity87 postgres4096 measurements were reanalyzed against
recorded alternating process order, without new workloads. Three pairs/order/batch:
HEAD-first/current-first paired median select changes are+1.02/+3.07%(store71),
+5.59/+3.82%(store72),+0.11/+3.83%(affinity87). Dataset scopes remain separate.
Current is slower in all9current-first pairs; order direction/magnitude varies
across batches, so order alone is not an established explanation. Per-process
select round medians sometimes drop60–80µs,other processes staynear1.1ms.

CSV samples are11-operation sequence indices, not equal per-operation indices.
A first parser pass rejected that mismatch; the corrected parser verifies exact
operation offsets and matches iteration(sample//11)/round. Sequential select-minus-
recover median gaps vary substantially(HEAD-12.5to75.2µs,current18.0to79.4µs),but
these distinct queries do not isolate codec CPU. Variation exceeds module95's
~0.50µs writer overhead. No invented materiality/significance target or acceptance
claim. Raw input hashes,per-pair/per-round values and parser:order97/README.md,
summary.json,input-hashes.json,analyze.py. Live relation-stat observation(stats98)
is a separate diagnostic with unchanged settings,not proof of a maintenance cause.

## Select request equivalence checkpoint — packets96

Reanalysis of retained syscall89 traces proves all252runtime select requests are
byte-for-byte identical across the four original/current processes:72bytes,
Bind/Describe/Execute/Flush, one binary8-byte limit(value1), binary results. The
128-byte trace capture includes these entire packets; truncated initial Parse
text is excluded(query text separately checked in server90). Input/packet hashes,
hex, decoded fields and reproducible parser are retained. No workload/build/server
restart or production edit. A first parser pass rejected resumed-call whitespace;
that syntax was corrected while keeping exact length/protocol checks.

Exact source comparison confirms unchanged StoreTbl::select dispatch and
Store::sendPrepared. This workload binds no group/key/vector data: group count0,
only limit1. Parameter row decoding/vector conversion cannot explain this select's
pre-response difference. TableView root construction and scheduler/runtime effects
remain distinct; identical request bytes do not prove identical timing. No new
copy/allocation or warranted source repair found. Performance acceptance remains
open. Evidence: packets96/analyze.py,summary.json,input-hashes.json,
source-comparison.json and README.md; original raw traces unchanged.

## Actual compiled writer checkpoint — module95

The existing original/current libZdbPQ saveTuple<XFields> functions were measured
through their exact function-pointer signatures and recorded local ELF offsets,
using hashed already-loaded modules. No production export/source/configuration
change or rebuild. Only two diagnostic fixtures were compiled with the existing
generated flags; both commands0 without diagnostics. Six alternating pairs/width,
CPU8,7rounds×5000conversions,all24processes0/empty stderr,allvaluesverified outside
batches; loader controls0 confirm each version's own Z DSOs. Production artifact
hashes match before/after, including after loader controls.

Width4096 actual-module fresh HEAD/current11621.29/12124.14ns,reuse11185.10/11686.04ns;
median paired changes+4.41/+4.42%,all6pairs favor canonical. Width32fresh281.37/308.69ns,
reuse254.89/294.73ns,paired+10.28/+15.48%,all6pairs canonical. Large extra cost is
about0.50µs,small27–40ns. Thus separate saveTuple compilation in row91 is insufficient
to explain the prior whole-select20–40µs discrepancy. Allocation/finish are still
fixture-side; the common indirect call is diagnostic, not product code. Do not
subtract these medians from whole-store timings or claim new allocation/copy proof.
No production repair follows from this measurement; rejected vector75 stays reverted.

Evidence: module95/symbols.json,build.json,comparison.json,summary.json,
loader-verification.json,terminal-verification.json and rawCSV. Long saga baselines,
remaining postgres timing residuals and broader acceptance remain unresolved.

## Fixed-placement long-baseline outcome — affinity94

The first original6e9a1252 SQLite full saga workload failed with missing-primary-key
and fatal SagaStep diagnostics despite verified placement of all7named threads.
The verified child was terminated(exit-15); runner exit1, owned postgres stopped,
original/current runtime hashes unchanged. Partial output contains968saga samples,
not the required3000; it is invalid for performance comparison. The batch stopped
before any current/postgres timing process. Fixed placement does not establish a
valid original long baseline. Existing guard-only controls remain diagnostic;
long matched timing against them still awaits the requested baseline preference.

A first setup attempt defaulted to external70 and overwrote earlier setup/command
metadata before launching any workload. Misdirected records are preserved under
affinity94/attempt1. external70 raw failure CSV/stderr/database/config/interruption
and frozen benchmark artifacts remain intact; artifact/condition manifests were
recovered from retained same-generation copies. Original command durations and
host snapshots could not be recovered and are explicitly marked unavailable,
not reconstructed as original observations. See external70/latency-metadata-recovery.json.
The corrected runner fixes output to its own directory and refuses existing
command records before writing or starting postgres.

No production source/configuration changed. Completion remains unproven; this
failure and provenance loss do not supersede successful short/native store results
or satisfy the full saga gate. Evidence: affinity94/terminal-verification.json,
README.md and external70/latency-metadata-recovery.json.

## Current requirement checkpoint — audit92/reads93

A fresh requirement/artifact audit verifies850unique current files, including
all811remaining-dependent entries,26store source/artifact entries and the
current interop reader executable. The old saga executable in fixture62's memory
manifest is explicitly superseded by store71's current86-check/Memcheck result;
the other14memory entries match. All9applicable memory logs show zero errors and
zero definite/indirect/possible lost bytes.26remaining-enabled accepted targets
retain exit0 with matching artifacts. Five exported baseline production sources
still match git6e9a1252;23retained memory-benchmark artifacts/library identities
are verified separately, including saved pre-extension executables/headers.
No correctness/Valgrind suite, production rebuild or source repair was repeated.

The audit found that direct-reader timing predated the final rebuild. Existing
zdbmsginterop--reads was refreshed on current artifacts,1000000reads×7alternating
rounds at32/4096bytes. Both processes0,empty stderr,matching checksum/owner,
source/binary/config/library hashes unchanged and loader control0. Canonical/
native/explicit-owning median batch means:32bytes4.061/3.986/28.831ns;4096bytes
4.073/3.966/64.785ns. Median paired native/canonical changes-1.66/-3.14%.
Borrowed readers have zero tracked requests/materialized payload bytes; owning32
has one tracked request/read, owning4096 uses jumbo allocation excluded from
telemetry. These are scoped batch means, not complete-saga latency acceptance.

Current zdb/interop README acceptance text was corrected to point to this audit
and distinguish completed scoped correctness/memory checks from incomplete
performance and excluded zhttp coverage. Requirement dispositions and remaining
gates are explicit in audit92/README.md; executable identities/logs reproduced
by audit92/verify.py. Reader details in reads93/README.md and summary.json.

Completion is still unproven: original long external saga baselines fail,
large postgres select/other timing residuals remain unresolved, and zhttp is not
certified under the stop-building instruction. The patched-control baseline
preference remains pending; original failures and short comparisons are retained.
No scope/baseline redefinition. All diagnostic processes terminal, postgres stopped.

## Prior diagnostic checkpoint — row91

The complete reflected postgres Order row writer is now measured in isolation,
using the same schema and borrowed PG binary representations as store71/72,
actual Order pooled allocator, schema-derived native alignment and original
canonical IOBuilder. Two final fixture-only compile/link commands0; three
fixture preparation failures retained separately (names/type span/enum definition).
No production edit/rebuild/reconfiguration. Loader controls verify the respective
Z DSOs. All24measured processes0, empty stderr, production hashes unchanged;
full-row values/bitmap/all vector elements verified outside each timed batch.
Six alternating pairs/width,seven rounds,5000conversions/batch,CPU8.

Widths32/4096 output416/32928bytes and capacities528/33296 match both versions.
Median paired batch-mean differences:small fresh+7.25%,reuse+5.73%;large fresh
+4.67%,reuse+4.06%, every pair favors canonical. Large HEAD/current absolute
medians11546.49/12084.99ns fresh and11171.57/11637.57ns reused:about0.47–0.54µs
extra. Small difference15–20ns. This complete writer cost still does not explain
the20–40µs whole-select discrepancy. Separate compilation/inlining caveats apply;
no additional owning copy/allocation was demonstrated and no new source repair
is justified by timing alone. Do not reapply rejected vector75.

Evidence: zdb/interop/evidence/repair2-20261010/row91/README.md,summary.json,
terminal-verification.json,build.json,loader-verification.json. Prior syscall89,
server90,conversion88 and affinity87 remain separately scoped. Original native
store71/72 select discrepancy and long external-saga baseline failure remain
unresolved; patched-control preference is pending. Broad repair2 remains open.
All diagnostic processes terminal; no benchmark/server/build left running.

## Prior diagnostic checkpoint — syscall89/server90

Client syscall tracing (original6e9a1252/current store-only,width4096,two pairs,
20iterations×3rounds) completes4processes0 without errors. All252select requests
receive the entire65932-byte result in one positive socket read; no added current
receive boundary was observed. Completion wakes match the blocked caller's futex
address. Last-read-to-wake medians HEAD/current61/62µs and62/64µs; send-to-first-read
medians1056/1079µs and1032/1078µs. These are instrumented intervals, not native
latency acceptance or isolated codec costs; they narrow the larger discrepancy
toward pre-read work without proving causality. Find includes3untimed checks.

A separate server-duration diagnostic uses existing per-connection logging
options, suppresses parameter text, and leaves global configuration unchanged.
Four originalHEAD/current processes0,100iterations×3rounds,width4096. Exact read
statement text matches.300measured samples/op/process after warm/semantic-check
exclusion. Select Bind medians HEAD/current94/93µs and87/87µs; Execute929/943µs
and949/934µs. Client select medians1106.34/1117.24µs and1115.88/1099.94µs.
Execution/client direction reverses between pairs; no stable cause is established.
Do not subtract medians as client CPU or use these instrumented runs to clear the
unbound/fixed-placement native select discrepancy.

Both diagnostics preserve exact production artifacts, drop owned databases and
stop postgres. No source repair or rebuild. Evidence in syscall89/server90
README.md,summary.json,terminal-verification.json and reproducible analyze.py.
Current conversion88/affinity87 and original store71/72 remain separately scoped.
Long-saga patched-control baseline preference is still pending; original failures
and valid short81/82 results retained. Broad repair2 acceptance remains open.

## Prior diagnostic checkpoint — convert88

A standalone matched postgres Int32Vec result conversion diagnostic calls the
existing saveOffset helper from original6e9a1252/current headers and libraries,
including the actual binary-array length prefixes and endian conversion. Two
fixture-only compile/link commands reused each generated Makefile/configuration;
no production rebuild/edit. Loader controls confirm each version's own Z DSOs.
All24processes pass, all elements/output sizes checked before/after batches,
production runtime hashes unchanged. Six alternating pairs/width,CPU8,
5000conversions/batch,seven rounds,fresh/reused pooled buffers.

Median paired batch-mean differences:32elements fresh-10.08%,reuse-12.53%;
4096elements fresh+4.46%,reuse+3.19%. Every pair agrees in direction. Large-vector
HEAD/current process-median batch means are5920.18/6186.77ns fresh and
5527.99/5708.70ns reused. This is roughly0.2–0.3microseconds per vector; by itself
it does not explain the tens-of-microseconds whole-row select discrepancy.
The separate diagnostic can differ in compiler inlining/register allocation;
it measures one vector root, not full schema/table finalization or store latency.
Do not use it to reinstate rejected vector75 or claim acceptance.

Evidence: zdb/interop/evidence/repair2-20261010/convert88/README.md,summary.json,
terminal-verification.json,build.json and loader-verification.json. Affinity87
select remains+2.29/+1.50/+11.65% p50/p95/p99 under its distinct placement;
unbound store71/72 remains separately recorded. Long-saga baseline preference
is pending. Broad repair2 is open; all these diagnostic processes are terminal.

## Prior diagnostic checkpoint — affinity87

The original-HEAD/current store-only postgres CPU-placement diagnostic is
terminal: six alternating pairs,1000iterations×3rounds,width4096. All12processes
exit0 with empty stderr,11operations/3000samples each; all12 task snapshots are
nonempty with the expected main/rx/tx/zdb/store CPU masks0/2/4/6/8. Owned postgres
was placed on CPU10. Artifacts are unchanged; database cleanup and stop succeeded.

Select remains slower: median paired p50/p95/p99 +2.29/+1.50/+11.65%,18000samples
per operation/version. Find/recover medians are essentially equal; this does not
establish the cause of select's discrepancy. Runtime placement differs from the
unbound store71/72 runs, whose +3.32/+2.33/+5.81% select result remains separate.
Do not pool the conditions or infer affinity-independent causality.

Evidence: zdb/interop/evidence/repair2-20261010/affinity87/README.md,
latency-summary.json and terminal-verification.json (reproducible verify.py).
No production edit/rebuild. Long external-saga baseline preference remains
pending: control85/86 contains only the existing queueCount guard, and is a
patched canonical diagnostic. Original HEAD failures and short81/82 valid
comparisons remain recorded. Broad repair2 acceptance is still open.

## Prior diagnostic checkpoint — control85/86

Ordering84's stricter write-over-write observer did not save a confirmed witness;
HEAD hit a missing-key SagaStep delete, failed leader election and INACTIVE state
before shutdown. The bounded150s debugger timed out124; cleanup and postgres
stop0. Final observer accounting is missing, so no negative ordering conclusion
or valid timing follows. Raw events/logs retained.

A separate canonical scheduler module control adds ONLY the existing current
queueCount guard to pinned HEAD tryPush_. Same compiler/generated Makefile/flags,
original objects/headers/Zu dependency; one source compile and relink0, no
production/configuration/queue-size change. Original baseline artifact hashes
remain identical. LD_DEBUG confirms only the isolated scheduler DSO is loaded.
Two full3000-measured-saga processes/backend (postgres and sqlite3) all exit0,
all16operations/3000samples each and no ERROR/FATAL diagnostics. Guard-only
replays support an ordering explanation, without proving every failure's cause.

These are explicitly PATCHED canonical diagnostics, not unmodified HEAD results
or a matched current/control performance comparison. The one-line patch is
reviewable at control85/scheduler.patch. User preference for long-run timing
against this labeled control has been requested; original HEAD failures remain
recorded. No dependent timing work proceeds on an assumed answer.

Short81/82 valid unmodified-HEAD comparisons, runtime83 writer controls and
revert78 original accepted artifacts remain as recorded. Broad repair2 remains
open, including original long-baseline failures and large postgres select.
Evidence: ordering84/terminal-verification.json; control85/86 README and terminal
manifests. All replays terminal; no new current production edit.

## Runtime83 writer/dependency narrowing

Profile74's archived libpq/libc/loader/libck/hwloc files have identical hashes
between baseline/current; module compiler/optimization/link flags also match.
Current TableView/VectorView source uses trusted borrowed accessors, with ingress
verification separate; no owning decode or additional per-field verification was
found in these accessors. These checks do not prove equality of all runtime work.

Refreshed existing same-process schema-writer controls (write/widewrite,32 and
16384-byte payloads,100000iterations,11alternating rounds,fresh/reuse) all pass,
with exact output bytes/checksums/capacities and unchanged artifacts. Native
median paired batch means favor native in all cases. Mixed/vector32904-byte
output: fresh1248.86→1180.40ns/op(-5.54%),reuse642.33→565.09(-12.80%).
Byte-only16424-byte fresh is near parity(-0.93%); reuse improves11.81%.
These are batch means, not latency quantiles; conversion from postgres, query
execution and scheduler are excluded. They do not explain or clear the repeated
large postgres select regression. No source fix is justified by these controls.

Short81/82 clean full-saga comparisons and separate counter coverage remain as
recorded below; longer baseline failures remain invalid. No production change,
build, new test fixture or queue tuning. All processes terminal, postgres stopped.
Evidence: runtime83/README.md,writer-summary.json,terminal-verification.json;
zi/interop/README.md refreshed to point to these controls. Broad repair2 remains
incomplete, preserving long-baseline/default-queue failures and store-select gaps.

## Short81/82 acceptance checkpoint

The existing full saga workload completes cleanly on both external stores when
run for a shorter process length: count100rounds3width32,303actual/300measured
sagas per process. Six alternating pairs/backend,12processes each, all exit0 and
no ERROR/FATAL diagnostics;1800samples/op/version. Same schemas/defaults/pools,
original scheduler queue131072 and respective complete runtime stacks; no source,
build or configuration change. Postgres saga median paired p50/p95/p99 improves
12.60/12.69/0.90%; sqlite3 improves11.67/14.76/2.37%. All ratios retained.

These timings measure the existing logical saga completion callback, not durable
commit latency. Persistence/shutdown validity is checked separately after timing.
The shorter run does NOT replace saga77/external70's failed3000-measured-saga
baselines. Their backlog/shutdown behavior remains unaccepted and unexplained;
no broad full-external-saga timing claim. Only1800samples/version limits tail
precision. Scheduler controls compare different runtimes and are not subtracted.

Separate four-process counter batches/backend pass,600samples/op/version. Saga
mean tracked allocations fall postgres55.902→48.010 and sqlite3 50.462→43.372.
Telemetry excludes dependency/system and ZmVHeap jumbo allocations; asynchronous
store work can overlap adjacent logical-completion snapshot windows. These are
observed window deltas, not exact isolated per-saga costs. Instrumented timings
are excluded from acceptance. Artifacts reverified; postgres stopped(status3).
Five relevant baseline production files independently match git6e9a1252 exactly.

The vector75 experiment remains reverted and accepted artifact identities match
revert78. Repeated large-row postgres select/mixed-tail performance, long external
baseline validity and user-stopped zhttp scope keep broad repair2 acceptance open.
Evidence: short81/82 README, latency/counts-summary.json and terminal manifests;
short81/pinned-production-source-check.json. No new production change.

## Saga77/overflow80 checkpoint preceding shorter comparison

The narrow vector75 loop experiment remains reverted. Revert78's library,
header and recorded postgres source/binaries match accepted fixture62 and
store72 exactly; earlier correctness/final-memory results apply by identity.

Full postgres saga77 established a baseline validity problem: pinned HEAD
exited0 and emitted all16operations/3000samples, but logged72 ERROR/FATAL
missing-primary-key/deactivation diagnostics AFTER DB::stop_ began. Two current
full runs exited0 with all samples and no such diagnostics. Reject matched
latency acceptance from this batch; the next known-live HEAD process was
terminated(-15), runner1, finally database cleanup/cluster stop0. Artifacts
reverified. This is distinct from external70's full sqlite baseline failure.

Overflow80's corrected debugger observer required a positive pending count at
successful ring publication, rejecting one zero snapshot. It observed an
event-loop wake published with queueCount1 at the same destination thread as a
prior store-write overflow fallback. This proves wake overtaking, NOT store-write
reordering or the cause of missing-key errors. The debugged workload had no
persistence errors and perturbed the failure; all timings are diagnostic only.
Existing current scheduler ordering guard is retained; no scheduler/config edit.

Both failed-witness overflow79 and corrected overflow80 evidence are retained.
All processes are terminal and the owned postgres cluster is stopped. Broad
repair2 acceptance remains open: repeated postgres store-select/mixed-tail
performance, valid matched external-saga baseline, and user-stopped zhttp scope.
Evidence: saga77/terminal-verification.json, overflow80/terminal-verification.json,
revert78/reused-acceptance-artifacts.json. No clean build/install/config change.

## Restored generation preceding saga77 results

Vector75's local-cursor experiment is reverted after cursor76 isolated A/B
reported select+6.10/+3.86/+6.81% median paired p50/p95/p99,5/6 pairs slower.
Four source-first incremental postgres builds passed. The restored library/header
and every postgres source/binary in fixture62's external/final-memory manifests
match exactly; prior correctness and Memcheck acceptance is reused by artifact
identity. Both full store72 runtime manifests also match. No other repair changed.

Saga77 is running the existing FULL postgres workload atwidth32,count1000,
rounds3,six alternating canonical-HEAD/current pairs, with STORE_ONLY explicitly
cleared, fresh database per process and90second timeout. No result yet. This is
separate from the earlier failed full sqlite baseline and store-only71/72 results.
Broad repair2 performance acceptance remains open, especially repeated postgres
select and mixed tails. Evidence: revert78/README.md and
reused-acceptance-artifacts.json; cursor76/allpairs and artifacts retained.

## Revert78 decision and rebuild history

Cursor76 compared vector75's local-cursor change against the pre-change native
postgres module, using the same current executable/runtime. Archived prior
module hash exactly matches store72. All12 processes passed,18000samples/op/
version; paired select p50+6.10%,p95+3.86%,p99+6.81%,only1/6 favors the change.
This isolates the module generation, not canonical HEAD. Reduced assembly loads
did not justify retaining the change. The three-line saveArray experiment has
been reverted; postgres library/consumers are rebuilding incrementally at-j4.
No other repair is reverted. Results/manifests/allpairs: cursor76/README.md.

Vector75 timing/memory evidence applies to the now-rejected generation. Once
restored artifacts are checked, earlier acceptance can be reused only for exact
matching artifacts. Saga77's full postgres comparison is prepared but not run;
no overlap with rebuild/diagnostics. Broad repair2 performance acceptance remains
open, including the earlier native/canonical select discrepancy.

## Rejected vector75 result

The postgres scalar-vector fill loop now uses a local borrowed input cursor and
loop bound; rebuilt assembly eliminates the per-element referenced-state loads.
No payload copy, allocation, validation or format change. Four incremental -j4
build commands, existing unit13/integration22 top-level checks and final postgres
unit/actual-I/O Memcheck passed (zero errors or definite/indirect/possible leaks).

All12 uninstrumented postgres processes passed: six alternating pairs,
count1000rounds3width4096,18000samples/op/version. Select remains slower than
pinned baseline: median paired p50+4.75%,p95+2.04%,p99+3.14%,only1/6 faster
medians. No latency benefit is proven for this repair; it has not yet been
compared directly with the prior native module. Shared memory-store/sqlite3
production libraries and baseline artifacts are unchanged. The remaining813
artifact guard also matches but excludes the postgres module; separate runtime
manifests and new postgres source/fixture records cover this changed generation.
Owned databases dropped and cluster stopped. Broad acceptance remains open.
Evidence: zdb/interop/evidence/repair2-20261010/vector75/README.md and
terminal-verification.json. Profile74 warnings and all individual pairs retained.

## Investigation preceding vector75 results

Profile74 completed four matched postgres client sampling runs. All experiments
warn about an unreliable sampling interval; CPU samples are diagnostic leads.
Generated assembly independently shows native saveArray reloading referenced span
and captured count state per element, whereas the pinned baseline retains its
loop state in registers. The narrow vector75 repair snapshots the borrowed input
span and loop bound locally inside the fill callback. Rebuilt assembly confirms
register state; no payload copy, owning vector, allocation, validation or format
change was introduced.

The postgres library and its unit/benchmark/integration consumers rebuilt
incrementally in the retained release configuration with -j4 and no diagnostics.
Existing postgres unit13 and integration22 top-level checks passed. Six matched
uninstrumented large-row postgres pairs are running; latency effect and refreshed
final memory gate remain pending. The store71/72 artifact generation is now
historical, not certification of this changed postgres library. Memory-store and
sqlite3 production libraries are unchanged. Broad repair2 acceptance remains
unproven; the baseline external-saga failure and user-stopped zhttp remain open.
Evidence: zdb/interop/evidence/repair2-20261010/profile74 and vector75.

## Previous assessment after store72

Store72 completed24same-artifact large-row repeat processes0 and stopped own
postgres; all813current remaining-enabled/owning guard entries and both tree's
measured artifacts reverified. Combined12pairs/backend,width4096: sqlite3
find/recover/select median improves18.03/23.25/12.07%,all12medians favor current.
SQLite update/update-one/delete median near parity,p99+0.46/+0.76/+1.05%, with
original extreme pairs retained. Postgres select remains+3.32/+2.33/+5.81% by
median paired p50/p95/p99; only2/12medians favor current. Other postgres medians
are-0.41% to+0.49%, but several tails still worsen. Do not dismiss select as noise.

Static select trace found borrowed PGresult Value overlays and direct pooled
builder output, no new owning Tuple/staging/post/invoke. Large select bytes32928
and tracked requests5 are unchanged; common saveTuple also serves find/recover.
Next investigation targets the select conversion/builder/growth path against
pinned baseline. No speculative production change. Scope/conditions/allpairs:
store72/README.md,combined-paired.json,terminal-verification.json.

Current owning Saga86checks/final Memcheck pass atstore71; production libraries
remain unchanged since paired59. Earlier memory-saga combined medians improve;
small indexed update+1.16%, external70 baseline full-saga failure and user-stopped
zhttp remain separately reported. Broad repair2 completion remains unproven.

## Previous acceptance checkpoint — store71

All48 direct-store latency processes and16 allocation-counter processes passed
on sqlite3/postgres atwidth32/4096; six alternating latency pairs per workload,
18000samples/operation/version. Source/library hashes reverified and own postgres
stopped. Current shared fixture is optional-backend/store-only; production
libraries remain identical to paired59/67/68/counts64. Refreshed Saga fixture
passed86checks and final Memcheck with zeroerrors/definite/indirect/possible lost
bytes. Other owning/consumer/remaining-enabled acceptance remains scoped to
unchanged source/binary/library manifests; all811remaining-enabled hashes rechecked.

Small external find/recover saves one tracked Z-heap allocation on both backends.
Large-row counts are unchanged; full-row/delete envelopes are8bytes smaller,
key-select byte counts unchanged. SQLite large-row find/recover/select paired
medians improve18.00/23.98/11.37%. Most other store medians differ by0–2%;
postgres large select is+2.04%. Some large-row tails worsen and paired ranges
are broad. Store72 repeats only width4096 with the same artifacts, retaining all
prior/new pairs. Counter-mode timings are not acceptance, and heap telemetry
excludes SQLite/libpq/system and ZmVHeap allocations.

Memory saga current-generation combined medians improve3.95/2.27% atwidth32/4096;
small indexed update remains+1.16%. Direct transmit below the aligned emission
limit avoids completed-span copies; oversized splitting adds an owner request
and retains its measured overhead. Full baseline external sqlite3 saga failed
with missing-primary-key bookkeeping deletes; candidate same full workload
passed. Its cause and external-saga latency remain unestablished. zhttp remains
excluded under user steering; no scope reply yet. Broad completion is unproven.
Evidence:store71/README.md,latency-summary.json,counts-summary.json,
terminal-verification.json; store72/ pending. Earlier pending statements below
are historical checkpoints and do not replace this current assessment.

## Historical acceptance updates — 2026-10-10

Paired68 completed all12 width32 workload processes with unchanged matched
artifacts. Across all12 paired59+paired68 pairs, saga median paired p50/p95/p99
improves3.95/2.97/3.44%; indexed update remains+1.16/-2.88/+0.51% (only4/12
medians favor current). Update-one improves4.08/2.06/2.63%, all12 medians favor
current. Width4096 combined saga improves2.27/2.59/3.01%. The saga median
regression is absent in these current comparisons; tails vary and the small
indexed-update discrepancy persists. No cause is established and broad repair
completion is not claimed. Raw paired59/67/68 CSV evidence was losslessly gzip
archived with verified original/archive SHA256, recovering806869290bytes;
summary values/sample content unchanged. Full details:paired68/README.md,
combined-paired.json and csv-archives.json. External-store/transmit timing and
the user-excluded zhttp scope remain outstanding limits.


The current repair candidate includes key53, prefix57's restoration of valid-prefix
flush behavior, and callback58's raw-pointer missing-row callbacks. Callback58
passed 101 Zdb unit/interop/integration checks and 97 sqlite3/postgres checks.
The unchanged Zi generation passed 156 unit checks and 45 canonical interop
checks at prefix57. Zum's previously failing saga translation unit now compiles;
the dependent rebuild passed. Zum's five unit programs passed 35 checks;
the six remaining consumer commands passed 104 top-level checks.
Fixture62 fixes borrowed CStringVec pointers retained by native fixture rows,
using owning strings and existing AliasFn metadata. Production borrowing remains
unchanged. Its rebuilt fixtures passed the same 198 Zdb/store checks. All nine
final Memcheck programs passed with zero errors and zero definite, indirect or
possible leaked bytes; four unchanged Zi results were reused after hash checks.
Enabled coverage excludes zhttp under user steering; broad performance
acceptance remains open. Paired59
completed current-generation timing against the pinned baseline.

The preceding paired55 comparison, before prefix57/callback58, recorded:
saga p50/p95/p99 improved 1.72/4.33/6.69% at width32 and 1.15/7.31/13.18% at
width4096. Small-row indexed update still regressed 1.35/3.98/3.33% by median
paired ratios. Its message is smaller (516 versus 524 bytes); the cause remains
unproven. These measurements do not certify the subsequently rebuilt generation.
Width changes store vectors and standalone payload saves; live-saga rows and
the step sequence are fixed, so width4096 is not a 4 KiB live-saga workload.

The pinned comparison baseline is `6e9a1252`, not current repository HEAD
`83d96641`. Production scheduler ordering/lifecycle changes differ from that
baseline; the concrete-callable scheduling-control fixture is identical on both
sides. No scheduler change was made in callback58. `zhttp` and root aggregate
builds remain excluded by user steering. Exact evidence and outstanding scope:
`callback58/`, `paired55/`, `paired59/`, `enabled-suite-inventory.json` and
`audit55.md` under `zdb/interop/evidence/repair2-20261010/`.

Paired59 refreshed the current generation: saga pooled p50/p95/p99 changes
were -2.19/-3.57/-5.31% at width32 and -2.65/+0.91/-2.66% at width4096.
Median paired changes were -2.24/-2.97/-1.46% and -2.24/-0.77/+3.25%.
The larger-run p99 splits three pairs each way; broad performance acceptance
remains unproven. Small indexed update median paired p50 is +0.81%, also split
three pairs each way, with improved tails. The initial larger-width pass that
overlapped summary processing is archived separately and excluded. Enabled63
completed 44 incremental build commands without diagnostics. Crypto65 corrected
three existing release-mode QUIC diagnostic expectations; no production change.
Rest66 built the required REST example peers omitted from the initial batch.
All 26 remaining test targets then completed successfully: 173 prove programs,
2,094 top-level prove checks, plus direct TOML, Go, Python and shell checks.
The initially skipped public Binance check passed when explicitly enabled.
All 811 guarded source/artifact hashes and final memory manifests were reverified;
the disposable postgres cluster is stopped. zhttp/root aggregate remain excluded.

Counts64 refreshed matched allocation evidence using the existing benchmark mode:
fixed live saga averages about 121 baseline versus 105–106 current tracked
Z-heap allocations, roughly 15–16 fewer per three-step saga. Store insert drops
three tracked allocations; indexed update drops one; update-one is unchanged.
These counters exclude ZmVHeap jumbo allocations and asynchronous activity can
cross snapshot boundaries. Allocation-instrumented timings are not latency
acceptance. External-store/transmit latency remains unmeasured by paired59.

Paired67 repeated width4096 after all acceptance work: pooled saga
p50/p95/p99 improved 3.19/1.08/4.53%; median paired improved 2.48/2.83/5.49%.
Across both same-artifact width4096 comparisons, all 12 paired saga medians
favor current; combined median paired p50/p95/p99 improves 2.27/2.59/3.01%.
Tails still vary. Host snapshots recorded performance governors throughout,
without identifying the cause of variation. Paired68 is now repeating width32
to assess the remaining small indexed-update median difference using all prior
and new pairs. No production or benchmark-source change or overlapping work.

### Historical acceptance checkpoints

The following checkpoints describe their recorded source/artifact generations.
Their pending and regression statements are superseded only by later explicit
evidence, including the current summary above.

Profile30 uses latency-only sampling after capture29: store row-load CPU
remains lower thanHEAD; row construction samples vary across profiles, while
most excess worker CPU is in syscalls/wakeups. No new copy or added hot post is
established. Paired31 completes24 unprofiled processes in12 alternating pairs,
24000 saga samples/version: HEAD/current p50=19.156/19.968us (+4.24%),
p95=25.458/26.209us (+2.95%), p99=30.167/31.629us (+4.85%). Scheduler controls
are slightly faster currently; isolated saga-save median241→151ns. This
corroborates a remaining complete-saga discrepancy, without proving its cause.
Source-only shutdown-drain changes since capture29 are preserved and unbuilt;
these results describe the capture29 executable/library hashes. No new build,
HTTP/aggregate work or full performance acceptance. Evidence: `profile30/`,
`paired31/` under `zdb/interop/evidence/repair2-20261010/`.

Capture29 removes the five longstanding SagaFn action wrappers: concrete
callables remain in templated SagaInsert/Update/Delete paths. Owning incremental
build passed without compiler diagnostics; all86 saga tests passed. Tracked
allocations average108.7232 before and105.7341 after per three-step saga.
Latency-only saga p50 improves5.55%/10.23% in reversed process orders, but tails
and scheduler controls vary; full saga performance acceptance remains open.
A proven redundant same-shard follower-recovery invoke is also removed (not
reached by this leader benchmark). Existing validation and queued ordering are
unchanged. Evidence: `zdb/interop/evidence/repair2-20261010/capture29/`.

Growth25 retains `Arena::grow_` as a separate `ZuNoInline` slow path, avoiding
repeated allocation/relocation code in small field helpers. Growth sizes, errors,
wire layout and ownership are unchanged. The actual SagaData row diagnostic
shows no excess native backing allocation: small rows grow once in both codecs;
large rows grow once natively versus twice canonically. Isolated codec medians
improve10–13% in most before/after pairs. Current Zi/Zdb owning libraries,
codec fixtures and saga fixture are rebuilt;126 top-level TAP tests passed.
Six matched full before/after/HEAD runs exited0. Saga median improves2.8–3.4%
versusbefore but remains3.8–6.5% aboveHEAD. Scheduler controls also improve,
particularly in one order, so full-saga gains are not wholly attributed to
outlining. Evidence: `growth25/`; performance acceptance remains open.
The I/O growth observer is now compiled and passed; external-store batch20
fixtures, broader dependent builds, full enabled suite and final memory gates
remain pending. HTTP/aggregate builds remain stopped per user steering.

Callback22 subsequently fixes the longstanding redundant `SagaCompleteFn`
wrapper: exact-type completions forward directly; other callables retain their
adapter. Owning build and all86 refreshed saga tests passed. Both refreshed
benchmark fixtures include identical recover/update-one additions; all four
matched runs exited0. Saga allocations average about110 versusHEAD121, but saga
p50 still regresses12.7–14.2%. Evidence: `callback22/`. Performance acceptance
remains open; HTTP/aggregate builds remain stopped per user steering.


Milestone19 passed216 top-level tests:138 owning unit tests,4 canonical
message interop groups,54 sqlite3 integration and20 postgres integration tests.
The shared parameter-policy fixture exercised count/select/find/insert/update
failure callbacks exactly once in both stores. The transmit fixture observed
zero completed-span insertion bytes on direct grown-owner handoff,928 bytes of
compaction within that owner and3968 object bytes through the splitting fallback.
It does not count every growth relocation or establish transmit timing.

Aggregate build20 was stopped at the user’s request (exit143); zhttp and
other dependent builds remain deferred while work focuses on saga performance.
Small typed-table offset scratch now uses exact metadata-sized ZuArray storage;
only zi/src and zdb/src were rebuilt. The existing saga executable passed all84
top-level tests at envelope21; later callback22/growth25 compile the memory,
shared benchmark and I/O observer additions. External-store additions remain pending.

The same-binary envelope21 comparison reduced saga p50 by5.5–5.8% in both
orders, but the refreshed matched HEAD comparison (`paired21/`, width32,
count10000 rounds7) still regresses9.5–11.7%. Individual store-operation medians
improve; scheduler controls do not explain the complete difference. Performance
acceptance remains open. These measurements use the accepted paired19 workload,
not the newer source-only benchmark additions. The full enabled suite and final
memory gates remain outstanding.


Milestones14/15 accepted the corrected scalar/vector column contract in memory,
sqlite3 and postgres. Generic composite UDT columns were an erroneous repair
requirement and have been removed; recursive codec support remains independent.
Milestone15 passed 169 top-level tests, including parameter arithmetic
and genuine existing-table OID mismatch diagnostics. Milestone17 subsequently
passed 171 top-level tests after the saga nothrow error-return repair. This is
partial acceptance; final dependent builds and memory gates remain outstanding.

Three sequential matched HEAD/candidate benchmark pairs are retained under
`zdb/interop/evidence/repair2-20261010/paired15*`. At width32, codec insert
allocations fell from3 to2 and store insert allocations from16 to13; at width4096,
store insert fell from14 to11. Complete-saga observed allocations fell from
about120–122 to113. These counters include asynchronous lifetime effects and
are separate from the synchronous ownership proof. The first pair shows
complete-saga median latency increases of12–15%; two reversed-order repeats show
4–8% increases. Performance acceptance is unresolved. Codec/store improvements
do not establish a complete-saga improvement. Full latency distributions, byte
sizes, conditions and raw samples are retained with each pair. No cold-recovery
or transmit-fallback timing result is claimed by these workloads.

## 1. Route from the smoke test to MemRow

BFBS is the schema, not the serialized row. The actual route is:

```text
Order in ZdbRow
  → nested FlatBuffer payload inside replication message
  → pooled buffer passed to store thread
  → BFBS-guided decoding into owning Tuple values
  → MemRow owning that Tuple
```

### Schema setup, before operations

| Stage | HEAD | Worktree |
| --- | --- | --- |
| Schema source | Generated, embedded BFBS from `.fbs` | BFBS streamed from `ZfStruct` metadata |
| Ownership | Static generated schema bytes | One buffer owned by each Table |
| Backend field binding | `camelSnake` followed by canonical `LookupByKey` | Compiled `ZuMatcher` resolver followed by indexed BFBS access |
| Per-insert schema generation | None | None |

The worktree allocates and emits the BFBS buffer during
[Table construction](zdb/src/Zdb.hh#L945). Backend construction resolves each
native field into the existing `XField` array; it does not rebuild a schema graph
or match field names for every insert. See the
[Schema resolver](zdb/src/ZdbStore.hh#L40) and
[backend field binding](zdb/src/ZdbMemStore.hh#L715).

This introduces startup allocation and streaming work relative to HEAD's static
schema. It follows the prescribed ownership contract and is outside steady-state
insert cost. The generator specializes schema decisions at compile time and
streams binary values/spans into the preallocated table-owned buffer; it does
not invoke a schema compiler or persist a `.bfbs` file.

### Insert execution

1. **Main thread → `zdb` thread.**
   [The smoke test](zdb/itest/zdbsmoketest.cc#L135) calls `orders->run(0, ...)`.
   No separate shard threads are configured, so shard zero uses the `zdb` thread.

2. **Construct the C++ row on that thread.**
   The callback allocates `ZdbRow<Order>`, then placement-constructs `Order`
   directly in its storage. There is no intermediate `Order` copy. The numeric
   arrays allocate their own storage; the first insert also initializes bitmap
   flags. Its explicit bitmap default initialization is application setup, not
   FlatBuffers or BFBS parsing, and is present in both revisions.

3. **Insert and commit run synchronously on the shard.**
   [Table::insert](zdb/src/Zdb.hh#L1210) enters the insert state and invokes the
   supplied callback directly. [Commit](zdb/src/Zdb.hh#L1475) assigns sequence
   state, updates the enabled row cache, and calls `AnyRow::replicate`.

4. **Build the complete serialized message into one pooled buffer.**
   [AnyRow::replicate](zdb/src/Zdb.cc#L1685) builds the nested row, attaches it to
   `Record`, wraps that in the replication message, and prepends the application
   header.

   - HEAD: `Zfb::IOBuilder`, `ZfbStruct::save`, canonical generated builders.
   - Worktree: `ZiFB::IOBuilder`, `ZfFB::saveObject`, native builder.

   Both construct nested data in the same buffer. Neither serializes the row
   into a second buffer and copies it into the envelope. The native path is
   `startNested` → `rowSave` → `endNested` → `saveRecord` → `saveMsg` → `finish`
   → `saveHdr`. HEAD uses `Zfb::Save::nest` and generated `CreateRecord`/
   `CreateMsg` around the same downward-writing arena.

5. **Pass references to replication and persistence.**
   [AnyTable::write](zdb/src/Zdb.cc#L2176) caches the buffer and invokes
   replication. Here, DB and shard share the `zdb` thread, so `db->invoke`
   executes inline. Standalone replication has no next peer and sends nothing.

6. **Mock wrapper → actual store-thread post.**
   [Mock StoreTbl::write](zdb/test/ZdbMockStore.hh#L224) wraps the call in
   delegates. Deferral is disabled, so it immediately calls the memory-store
   implementation. [Memory StoreTbl::write](zdb/src/ZdbMemStore.cc#L125) then
   posts the buffer reference and commit callback to `zdb_mem`.

7. **Decode and allocate MemRow on `zdb_mem`.**
   The store checks idempotence, then
   [loadRow](zdb/src/ZdbMemStore.hh#L774) follows the message and nested-root
   offsets. Since `vn == 0`, it calls `loadTuple` for all nine fields, then
   allocates `MemRow`, moving the Tuple into it.

   There is no intermediate reconstructed `Order`, whole-buffer copy, or payload
   verification on this trusted local path. `loadTuple_` allocates Tuple storage
   once, dispatches on each cached `XField::type`, and placement-constructs each
   selected Value in uninitialized storage.

8. **Index and complete.**
   The backend inserts keys and the UN index, invokes completion, and
   [committed](zdb/src/Zdb.cc#L2216) posts back to the shard for buffer eviction
   and commit notification.

These handoffs are the same in HEAD. The initial shard post, store post and
completion post are distinct from the inline replication invocation in this
specific configuration.

## 2. Confirmed regression: bitmap ownership is copied

The new [loadValue](zdb/src/ZdbMemStore.hh#L461) does:

```cpp
auto value = ZfFB::valueAt<W>(p);
...
new (ptr) T(value);
```

For strings and ordinary vectors, `value` is a borrowed view. For `ZtBitmap`,
[BitmapMap::load](zf/src/ZfFB.hh#L997) returns an owning bitmap:

1. Allocate its word array.
2. Decode the wire words into that array.
3. Copy the named lvalue `value` into the Tuple.
4. Allocate another word array and copy the words.
5. Destroy the temporary and free its array.

`ZtBitmap`'s defaulted copy reaches the
[deep-copying array constructor](zt/src/ZtArray.hh#L389).

HEAD directly placement-constructs from the returned bitmap
(`HEAD:zdb/src/ZdbMemStore.hh`, lines 456–458):

```cpp
new (ptr) ZtBitmap{
  ZfbTransform::Bitmap::load<ZtBitmap>(...)
};
```

**For the first smoke-test insert, this changes one bitmap allocation into two,
plus an additional word-array copy and free.** Empty bitmaps do not incur that
allocation.

The repair should construct directly from the decoder result, or move the
owning result. No new container or specialized decoding algorithm is needed.

## 3. Other performance differences

### Blanket 32-byte builder alignment: conditional regression risk

HEAD's custom builder uses alignment 8. The worktree's database
[Builder alias](zdb/src/Zdb.hh#L135) uses 32, although this `Order` schema needs
only 8.

[Nested construction](zf/src/ZfFB.hh#L909) propagates that outer alignment into
final message alignment. Relative to HEAD, this can add padding, reduce usable
capacity through rounding, and change when growth occurs.

This is a capacity-boundary risk, not evidence that this particular small insert
grows its buffer. Forced alignment must remain supported, but schemas requiring
eight-byte alignment should not automatically pay for thirty-two-byte alignment.

### Scalar defaults differ: payloads need not have equal sizes

HEAD's `zdb/test/fbs/zdbtest.fbs` gives `orderID`, `seqNo` and `side` zero
defaults. The worktree derives defaults from Z metadata and its existing fallback
logic.

Consequently, zero-valued fields previously omitted can now be present. That
changes writes, layout and potentially padding. This follows the resolved default
contract; it should be distinguished from codec inefficiency when comparing
performance.

### Scheduler posting has additional work

The worktree adds a
[`queueCount` check](zm/src/ZmScheduler.cc#L319) before attempting the ring fast
path.

That adds a load and branch to queued work and changes behavior when overflow
work is pending. It is a shared scheduler change, not a `ZfFB` serialization cost.
Static analysis cannot assign it a meaningful latency impact.

### Reflected access is broadly equivalent

HEAD reads BFBS `field.offset()` and accesses the payload vtable. The worktree
reads BFBS `field.id()`, calculates its vtable slot, and accesses the payload.

Both perform binary offset reads and discriminator dispatch. Neither parses field
names or numbers during Tuple decoding. The new scalar accessor also defers
reading the schema default until the payload field is absent; HEAD supplies that
default as an argument to `GetField`.

No substantial new reflection algorithm was found in the per-insert path. BFBS
field binding is cached during backend setup; there is no per-insert `ZuMatcher`
lookup, `camelSnake`, schema-name search, or reconstructed reflection graph.

## 4. Copies and allocations that remain unchanged

| Operation | Assessment |
| --- | --- |
| Strings into wire buffer | One byte copy per string in both |
| Numeric vectors into wire buffer | Direct element writes in both |
| Nested payload into envelope | No payload copy in either |
| Buffer passed across threads | References/handles; no serialized-byte copy |
| Backend Tuple | One owning allocation in both |
| Backend `prices` and `qtys` | One allocation and element decoding per nonempty vector in both |
| Backend short strings | Copied into builtin string storage; smoke-test strings need no separate heap allocation |
| Tuple into `MemRow` | Ownership moved in both |
| Buffer growth | Both preserve front scratch and downward-written tail through `ZiIOBuf::realloc` |
| Pool size and IDs | Order's configured 512-byte size and heap IDs remain preserved |

The native typed table writer also reuses its offset scratch for final field
positions, avoiding canonical per-field arena records for those tables. That is
a source-level improvement, although it does not establish net timing. Small
schemas use builtin offset storage; larger schemas use stack scratch with heap
fallback. Neither builder needs a separate heap allocation for its arena
bookkeeping when the supplied pooled buffer has sufficient capacity.

The mock store has existing delegate allocations even with deferral disabled:
stateful lambdas are converted to `ZmFn` parameters before the immediate-call
decision. Its work wrapper and wrapped commit callback allocate before `MemRow`
construction; its completion wrapper allocates afterward. Scheduler ring posting
itself normally moves captures into the ring without heap allocation; overflow
takes the allocating path. These costs exist in HEAD too.

After `MemRow` construction, `extractKey` copies values into temporary key Tuples,
and `m_indices[i].add(key, ...)` copies each lvalue key again into its index node.
Those redundant allocations are also unchanged, rather than migration
regressions. The smoke-test Order has three keys; its key fields are strings and
scalars, not the numeric vectors or bitmap.

## 5. Update and delete routes

The operation filters are defined in
[ZfFieldFilter](zf/src/ZfStruct.hh#L3585). HEAD has the same membership rules;
the worktree additionally excludes `Skip` fields.

| Operation | C++ fields serialized | Memory-store fields decoded | Stored-row action |
| --- | --- | --- | --- |
| Insert (`vn == 0`) | Save fields | All bound row fields | Move owning Tuple into new stored `MemRow` |
| Update (`vn > 0`) | Every `Mutable` field and primary-key fields | Every `Mutable` field and primary-key fields | Move mutable Values into existing stored `MemRow` |
| Delete (`vn < 0`) | All key fields, including secondary keys | Primary-key fields only | Remove existing stored `MemRow` and its index entries |

The typed entry points use
[rowSaveUpd / rowSaveDel](zdb/src/Zdb.hh#L985); version selection and nested
message construction occur in [AnyRow::replicate](zdb/src/Zdb.cc#L1685).
All three operations retain the same pooled-buffer ownership transfer, backend
thread post, idempotence check before decoding, and completion route as insert.
They do not generate BFBS, parse text, or verify locally generated row payloads
on each operation.

### Update

[Table::update_](zdb/src/Zdb.hh#L1405) retains stale replication-buffer handles
in scratch storage for rollback and uses the cache's update machinery around
the caller's mutation. Retaining those handles does not copy their bytes.
Commit updates sequence/version state and serializes the update filter.

[loadUpdTuple](zdb/src/ZdbMemStore.hh#L554) allocates a full-length Tuple but
constructs owning payload values only for mutable and primary-key fields;
other slots are void. [loadRow](zdb/src/ZdbMemStore.hh#L774) still allocates a
temporary `MemRow` to carry this update. This is present in HEAD too.

[StoreTbl::update](zdb/src/ZdbMemStore.cc#L170) extracts an owning primary-key
Tuple and locates the existing row. It snapshots secondary keys into scratch
containing owning key Tuples, removes/reinserts the UN index node, and
[updTuple](zdb/src/ZdbMemStore.hh#L594) move-assigns mutable Values into the
existing row. It recomputes secondary keys and changes their index entries only
when their values differ. The existing row is not replaced wholesale.

Consequences in both revisions:

- An update is not a dirty-field or vector-element patch. All mutable fields
  are serialized and decoded, even when only one scalar or vector element changed.
  A large mutable vector is copied to the wire and allocated/decoded again in
  the backend on every update.
- The decoded vector storage moves into the existing row; there is no additional
  vector deep copy in `updTuple`. Old destination storage is released.
- An immutable primary key is decoded into owning storage for lookup, then
  discarded with the temporary update row. Mutable primary-key semantics are
  constrained by lookup using the received key; this migration does not add an
  old-key/new-key protocol.
- Secondary-key snapshots and recomputation copy key Values. Vector/string/bitmap
  keys amplify this existing cost; keys containing owning vectors do not become
  cheap merely because the outer scratch uses stack storage.

The confirmed bitmap temporary regression applies to mutable bitmap updates,
and to bitmaps included in the primary key. It occurs before ownership is moved
into the existing row.

### Delete

[Table::del_](zdb/src/Zdb.hh#L1438) retains stale buffer handles for rollback,
then commit emits the delete filter. The backend constructs a temporary,
full-length Tuple/`MemRow`, decoding only the primary key.
[StoreTbl::del](zdb/src/ZdbMemStore.cc#L216) removes the primary and UN entries
and derives secondary keys from the existing stored row to remove those entries.

There is a producer/consumer asymmetry: the serializer writes **all key fields**,
but this backend uses only primary-key fields from the payload. Large
secondary-key vectors can therefore be serialized and transmitted without being
decoded or used by this backend. HEAD has the same filters and algorithm. This
is an existing optimization opportunity, not a demonstrated migration regression;
changing the common delete protocol requires checking every consumer.

Non-key vectors/bitmaps are not decoded for delete. Primary-key bitmaps do incur
the newly identified extra owning copy. Removing the stored row also releases
its existing vector/string/bitmap allocations; that is destruction, not payload
parsing or a new allocation.

### `findUpd`, `findDel`, and cold reads

These entry points add lookup before the routes above. Cache/buffer hits and
backend misses have different costs; they must not be conflated with direct
`update(row, ...)` or `del(row, ...)`.

On a backend lookup, [retrieve](zdb/src/Zdb.hh#L2310) serializes a key and posts
to the store. [StoreTbl::find](zdb/src/ZdbMemStore.cc#L93) decodes an owning key
Tuple and serializes the stored full row into a result buffer. The frontend
constructs the typed C++ row from that buffer. Thus a cold find followed by an
update can decode vectors into the frontend and serialize/decode them again for
the update. HEAD has this same architecture; there is no direct backend
Tuple-to-frontend-object ownership transfer.

## 6. Saga routes, copies, and thread dispatch

Saga operations use the ordinary table codec and backend paths for both their
application rows and bookkeeping tables. Their additional writes and scheduling
are part of the saga protocol, rather than new `ZfFB` overhead.

### Submission and saved application state

[SagaDB::saga](zdb/src/Zdb.hh#L2893) dispatches admission, validates the live
saga state, allocates a `ZdbRow<SagaData>`, saves the application saga definition,
and schedules insertion/commit on the saga table's shard. It subsequently posts
back to the database driver to advance submission/execution. Local `commit()`
acceptance must not be described as waiting synchronously for store completion.

[MSaga::save](zdb/src/ZdbSaga.hh#L694) has this data route in **both** revisions:

```text
application saga Def
  → temporary pooled FlatBuffer
  → memcpy into owning SagaPayload
  → byte copy into SagaData row FlatBuffer
  → backend owning Bytes allocation and copy
```

`SagaPayload` uses builtin storage (default 256 bytes) with heap fallback. The
temporary builder nevertheless owns a separate pooled I/O buffer; builtin payload
storage does not remove that allocation or the `memcpy`. Larger payloads also
need the payload's fallback allocation. This existing staging cost scales with
the entire saved definition, including its vectors. It is separate from the
zero-copy nesting of an already-built row into its replication envelope.

The worktree uses the definition's `BufferAlign` for this temporary builder,
rather than unconditionally imposing the row builder's blanket 32-byte alignment.
The saved definition is opaque `Bytes` to the `SagaData` memory-store binding;
its internal UDTs/vectors are interpreted by the typed saga codec on recovery,
not by `ZdbMem::Value` when storing `SagaData`.

### Forward steps, compensation, and replay

[Saga::insert](zdb/src/Zdb.hh#L2326),
[Saga::update](zdb/src/Zdb.hh#L2394), and
[Saga::del](zdb/src/Zdb.hh#L2501) prepare forward operations and record their
planned sequence state. A fresh forward step calls
[Saga::intent_](zdb/src/ZdbSaga.cc#L25), which allocates and commits a `SagaStep`
row, then runs the target table operation. Each committed bookkeeping row uses
its own normal serialization, store post, owning Tuple and index maintenance.
These allocations/writes exist in HEAD.

Replay uses the recorded UN and existing comparison logic to skip effects already
applied; a skipped effect does not need another target payload build/store write.
Compensation delegates to the corresponding ordinary table operation. Completion
and retirement delete step intents and the `SagaData` row through `findDel` and
commit, with database/shard dispatch as required. There is no single fixed saga
post count: it depends on the step sequence, shard placement, cache hits and
replay/compensation path. The migration has not added another handoff to each
ordinary target operation.

The existing [saga replication fixture](zdb/itest/zdbsagareptest.cc#L78) updates
only `qtys[0]`, but this still serializes every mutable Order field. Consequently
large-vector update costs and the mutable-bitmap regression also apply to saga
steps. Repeated steps multiply these costs, rather than creating a different
vector decoder.

### Catalog and recovery

The catalog context owns an emitted `SagaTypeStep` BFBS buffer. Generation is
setup work, not per-step work. Recovery scans catalog, `SagaData` and `SagaStep`
rows through the existing select/pagination path.

For the memory backend, selection serializes owning Tuple values into result
buffers; the frontend loads typed Tuples and moves them into page rows. See
[sagaScanData](zdb/src/Zdb.hh#L3160),
[sagaLoadData](zdb/src/Zdb.hh#L3191),
[sagaScanSteps](zdb/src/Zdb.hh#L3239), and
[sagaLoadSteps](zdb/src/Zdb.hh#L3271). The opaque saved payload is copied into
these result buffers and into frontend owning byte storage. Moving a builtin
payload can also copy its inline bytes, whereas a heap-backed payload can transfer
ownership. This materialization/pagination architecture is unchanged from HEAD.
Recovery additionally allocates saga objects, queue/hash nodes and step recovery
records; these are protocol state, not a reflection object graph.

[MSaga::load](zdb/src/ZdbSaga.hh#L674) dispatches by the compiled type matcher,
verifies the saved definition, then constructs it with the typed handler and
constructor metadata. HEAD verifies the definition too. This verification checks
the nested application schema: verifying the outer `SagaData` only establishes
that its `Bytes` span is well formed, not that it contains a valid definition.
It must not be removed as redundant outer-buffer verification. Conversely,
locally generated select-result Tuples are not needlessly verified again.

## 7. Complete field-type and vector audit

The inventory is [ZfFieldTC](zf/src/ZfStruct.hh#L158), with native-to-wire
representations in [ZfFB::Value / Rep](zf/src/ZfFB.hh#L1138).
The table distinguishes wire construction from owning memory-store decoding.
Allocation counts describe nonempty values and exclude the common Tuple,
`MemRow`, index and pooled-buffer costs already described.

| ZfStruct field codes | Wire save and typed view | Memory-store Tuple materialization / HEAD comparison |
| --- | --- | --- |
| `CString`, `String` | String bytes copied once into wire storage; view is a borrowed character span | Normalized to owning `String`; one byte copy, builtin short-string storage or heap fallback. Same ownership model in HEAD. A raw C string can require length discovery. |
| `Bytes` | Direct byte-vector copy; borrowed byte span on view access | One owning byte array and copy. Not treated as a vector of scalar `Value`s. Same in HEAD. |
| `Bool` | One-byte wire scalar | Native bool conversion; no payload allocation. |
| `Int8`, `UInt8`, `Int16`, `UInt16`, `Int32`, `UInt32`, `Int64`, `UInt64` | Scalar wire width follows the field code; width/endian conversion as necessary | One scalar load/conversion; no payload allocation, as in HEAD. |
| `Int128`, `UInt128`, `Fixed`, `Decimal`, `Time`, `DateTime` | Inline structured representations; component writes/reads, no text parsing | Native value constructed in place; no owning heap temporary. Missing inline fields use the field default. |
| `Float` | Native `float` uses wire Float32; other supported representations use Float64 | Binding handles wire Double only: Float32 is a coverage gap, detailed below. Double uses a scalar load. |
| `UDT` | ADL mappings or typed table/inline/union representation, including array-valued UDTs | Memory-store union supports Bitmap and IP, not arbitrary typed UDTs. Bitmap has the confirmed additional owning copy; IP is an inline native value. |
| `CStringVec`, `StringVec` | Offset vector plus one string per element; temporary child offsets in scratch; elements viewed as character spans | `StringVec` allocates one outer array and constructs each owning string once. Long elements allocate separately. `CStringVec` is not bound, detailed below. |
| `BytesVec` | Offset vector of wrapper tables, each containing a byte vector; one byte copy per blob | One outer owning array plus one allocation/copy per nonempty blob. No additional owning blob temporary. Same wrapper and ownership architecture in HEAD. |
| `Int8Vec`, `UInt8Vec`, `Int16Vec`, `UInt16Vec`, `Int32Vec`, `UInt32Vec`, `Int64Vec`, `UInt64Vec` | Contiguous scalar vector written directly, with element conversion where needed | One array allocation sized to element count; scalar reads pushed directly into it. No per-element heap allocation or intermediate owning vector. Same in HEAD. `UInt8Vec` remains distinct from `Bytes`. |
| `Int128Vec`, `UInt128Vec`, `FixedVec`, `DecimalVec`, `TimeVec`, `DateTimeVec` | Contiguous inline-element vector; component writes per element | One outer allocation; each native value decoded into it. Component conversion needs no per-element heap allocation. HEAD also converts these mapped values. |
| `FloatVec` | Float32 or Float64 elements selected from native element type | Double-vector binding materializes one owning array. Float32-vector binding is missing. |

There is no separate `BoolVec` or named `UDTVec` code in this inventory. Generic
UDT arrays are handled through the UDT representation, not an omitted `...Vec`
entry in the table.

### Vector loops and ownership

[saveValue](zf/src/ZfFB.hh#L1886) reserves the scalar/inline vector's destination
and writes into it directly. Its `memcpy` specialization applies to compatible
byte elements or scalar spans already represented as `ZuLittleEndian<Scalar>`.
Ordinary native multi-byte integer vectors still take the element loop, even on
a little-endian CPU. HEAD also uses an element-writing path; this is not evidence
of a new conversion or allocation regression. Compiler elimination/vectorization
of those loops cannot be established from source alone.

Offset vectors (`StringVec`, `CStringVec`, `BytesVec`, table-valued UDT arrays)
need child offsets until the final vector is written. The worktree uses
`ZtScratch`; HEAD likewise used scratch for offset collection. Empty offset
vectors avoid this scratch. Large element counts can exceed scratch's stack
threshold and use heap fallback; this is not one allocation per element. Each
string/blob/table child still has its own wire representation. FlatBuffers does
not provide nested vectors directly, so the `BytesVec` wrapper is required for
canonical interoperability.

[VectorValue](zf/src/ZfFB.hh#L1475) is a borrowed binary view. Obtaining or
iterating the view does not allocate an owning vector or parse a schema graph.
[Handler](zf/src/ZfFB.hh#L1732) passes constructor fields directly to the native
constructor and initializes the remaining writable fields afterward. Native
owning containers then materialize their contents; views must not be mistaken
for ownership that can outlive the serialized buffer. Generic object arrays may
use the `NativeArray` construction adapter, which constructs each native element
rather than creating a separate owning reflection tree.

In [loadValue](zdb/src/ZdbMemStore.hh#L443), the vector path constructs `T` with
`value.length()` capacity and pushes view/scalar elements into it. For strings
and blobs, the loop variable is a borrowed span, **not** an owning `String` or
`Bytes`; each destination element performs its one required owning copy.
Mapped numeric/time values are small native values. This does not reproduce the
bitmap bug, where the loop-independent `value` itself is already owning and is
then copied as an lvalue.

Large vectors multiply baseline wire copying, destination allocation, key copying
and saga staging. No new whole-vector intermediate allocation was found in the
supported memory-store `...Vec` loaders. That conclusion does not extend to
arbitrary application ADL transforms: a transform returning an owning temporary
must be reviewed for its own ownership costs.

### Binding gaps: correctness before performance

[xField](zdb/src/ZdbMemStore.hh#L234) returns discriminator zero when it cannot
bind a field. [loadTuple_](zdb/src/ZdbMemStore.hh#L520) then constructs a void
Value. Unsupported types therefore silently lose their value instead of failing
binding; a void mutable field can subsequently reach the `updTuple` assertion.
This is not a harmless omission/default optimization.

1. **`CStringVec`:** typed `ZfFB` maps it to a vector of strings, but memory-store
   binding accepts only `StringVec` for reflection Vector/String. HEAD has the
   same restriction. Unlike scalar `CString`, it is not normalized to the owning
   string representation. This is a pre-existing coverage gap.
2. **Float32 and Float32 vectors:** the memory store handles reflection Double
   and Vector/Double, with no Float or Vector/Float cases. HEAD has that same
   binding limitation. The worktree now derives Float32 from native `float`
   metadata, so migrated declarations previously paired with Double schemas
   could newly encounter it. A regression for a particular table requires
   comparing its old schema and current native declaration; the smoke Order
   trace alone does not establish one. The native typed codec supporting Float32
   does not establish that the memory backend supports it.
3. **Arbitrary UDTs and UDT arrays:** the memory-store Value union is deliberately
   narrower than typed `ZfFB`. Only the recognized Bitmap/IP UDT bindings exist;
   arbitrary table, inline, union or object-vector fields have no general owning
   Tuple representation. This limitation exists in HEAD too. Saved saga
   definitions can contain typed-codec UDTs inside their opaque Bytes payload
   without requiring such a backend binding.

A repair should normalize equivalent representations (such as `CStringVec` to
owning `StringVec`, Float32 to the backend's double storage) or reject unsupported
bindings during table setup. It should not silently add per-row schema searches,
text conversions, redundant validation or a generic reflection object graph.

### Metadata and defaults

`Ctor`/`Init` determine native construction; `Mutable` and keys determine operation
membership. `Skip` fields retain metadata/IDs but neither load nor save values.
Default omission occurs during object serialization, with `Deflt` and the normal
adapter fallback, rather than being reapplied to an already serialized span.
Absent offset fields yield the defined null/empty representation; required-field
failures belong to the existing verification/error contract. These distinctions
can change emitted field presence and size relative to old hand-written schemas,
but do not justify repeated default evaluation or payload verification in the
trusted local Tuple path.

## 8. Conclusion and priorities

1. Repair the owning bitmap temporary copied into the Tuple: a demonstrated
   additional allocation, copy and free for nonempty bitmaps. It affects insert,
   mutable-field update, primary-key delete/lookup, and corresponding saga steps.
2. Resolve memory-store binding gaps or fail setup explicitly. Typed-codec
   coverage is broader than owning Tuple coverage; silent void values obscure
   correctness failures, especially for `CStringVec` and Float32 vectors.
3. Review blanket 32-byte alignment and resulting capacity thresholds while
   retaining required forced-alignment support.
4. Account for changed scalar-default semantics when comparing emitted sizes.
5. Attribute shared scheduler, mock delegates, update key snapshots, whole-vector
   updates, delete secondary-key serialization and saga payload staging separately
   from migration regressions. These costs exist in HEAD.

The ordinary row trace does not reveal new whole-payload copies, extra
store-thread handoffs, per-operation BFBS generation, textual parsing, or
redundant verification. Saga staging and cold lookup/recovery have existing
payload copies; the row-path conclusion must not be generalized to those routes.

All conclusions are from source inspection. No timing or net performance claim
is made, and no repairs were applied as part of this analysis. Source links and
line numbers refer to the worktree at the time of the audit; HEAD locations are
identified explicitly where applicable.

## 9. Repair2 execution evidence — 2026-10-09

The repairs are in progress under the retained Clang release configuration
(`-march=x86-64-v2 -O3 -g -DNDEBUG`), with incremental milestone builds.
The original numbered audit above remains a description of the pre-repair
state; its line links are not a certification of the changed implementation.

Implemented source changes include direct bitmap construction, normalized
CStringVec/Float32 bindings with separate wire widths, last-use index-key
moves, scratch-stream saga save, primary-key-only delete serialization,
transactional row/key rebind and actual pooled-builder alignment. Recursive
memory-store UDT binding is now implemented but not yet accepted across all
operations. Typed recursive UDT support already existed in both HEAD's adapter
and the direct codec; HEAD's memory-store Tuple binding had the same generic
UDT limitation described in section 7.

Projected record verification now relaxes Required presence only for fields
excluded by the operation's root filter. Present fields still undergo normal
verification. Ordinary row verification remains strict. The Zf suite passed
14 binaries / 363 tests, including malformed excluded-field and projected
Required-field cases (`/tmp/z-repair2-zf-suite.log`). Changed Zf/Zi/Zdb libraries
and Zdb tests built in the retained configuration, without compiler diagnostics
after the recorded compile fixes.

The first Zdb saga execution crashed during the existing `findFail` restart
case, before the new recursive fixture. Core analysis located an empty first
key binding: braces selected the initializer-list constructor for the temporary
array of XFields. The capacity constructor is now selected explicitly. This is
a repair implementation defect, not a regression established by the original
audit. Logs: `/tmp/z-repair2-zdb-saga-test.log` and
`/tmp/z-repair2-zdb-saga-core-gdb.log`; the core dump is retained at
`/tmp/z-repair2-zdb-saga.core`. Post-fix execution passed 3 Zdb binaries / 85
top-level tests (`/tmp/z-repair2-zdb-suite.log`). The first recursive fixture
passed 15 assertions covering insert/find and reverse construction. Subsequent
source changes and expanded operation fixtures are pending the second milestone;
these passing results do not certify them.

No new allocation counts, latency distributions, encoded-size comparison,
canonical interchange result, final dependent acceptance or Valgrind gate is
claimed. Remaining operation/default/cache/alignment and acceptance work is
tracked in the repair execution record in `plan.md`.

The second runtime milestone passed ZdbSagaTest's 80 subtests, including 135
assertions in the expanded recursive store fixture
(`/tmp/z-repair2-zdb-saga-test6.log`), and the Zf suite again passed 14 binaries /
363 tests (`/tmp/z-repair2-zf-suite6.log`). ZiFBTest's runtime-alignment cases
passed 16 assertions each for 8, 16 and 32 bytes, covering growth, nested framing
and retained-buffer reuse (`/tmp/z-repair2-zifb-test6.log`); ZiIOBufTest passed
five cases. PostgreSQL and SQLite libraries compiled without diagnostics, but
their runtime acceptance is pending. Later source changes to native object
metadata, IP fixtures, typed projected alignment and framed-buffer relay are
not covered by those results. See the follow-up entry in `plan.md`.

### 2026-10-10 continuation

Batch 9's ownership checks passed 66 assertions, covering bitmap and vector
destination ownership plus workload-sized table-array and absent-node UDT tapes. This establishes
allocation counts for those cases, not store/saga latency acceptance. The Zdb
suite failed one of 86 top-level tests: the recursive fixture's read-only `next`
was omitted by Save membership, causing two missing-tail assertions. Its metadata
is corrected; the next incremental milestone must accept the corrected case.
The allocation helper's default root filter also omitted the read-only chain:
its intended finite-chain allocation case actually tested absence. Batch 10
uses explicit All membership and tape-length checks to close that fixture gap;
nonempty chain allocation coverage remains pending execution.
The shared all-types integration program passed five top-level tests against
each of the memory and SQLite stores. Raw logs are retained in
`zdb/interop/evidence/repair2-20261010/`.

Typed network ingress now verifies the nested row before peer replication-state
advancement and debug printing. Shard recovery consumes that verified record;
this closes the earlier source gap where a rejected payload could still be
written and advance counters. The change awaits executable acceptance. Generic
table decoding now terminates optional null edges and allocates its final owning
tape after a shared counting traversal. Reverse output preserves table absence.
The typed codec's existing restriction on reconstructing owning native pointer
graphs is unchanged; recursive pointer fixtures inspect borrowed wire views.

The top-level dependent refresh is running with the retained configuration and
`-j4`. Matched performance evidence for codec/store/saga, replication integrations
and the final Valgrind gate remain outstanding. Earlier migration acceptance
does not certify this repair batch.

Further batch-10 source review removed local Tuple/row relays during typed
selection and saga recovery-page construction. Memory-store key-only select
now serializes stored components directly. Cached key-component indexes also
correct extraction order when group fields follow members in declaration order;
the existing fixture now covers lookup, count, key selection, rebind and delete
for that layout. Native inline defaults now use the resolved containing-field
default, alongside the mapped-inline default path. All these changes await the
aggregate milestone's compilation and runtime checks.

The user approved establishing matched HEAD store/saga baselines on 2026-10-10.
An isolated snapshot of HEAD `6e9a1252` has been prepared, but no baseline build
or performance measurement has run. Compiler/configuration switches remain out
of scope. Codec records alone are insufficient for the store/saga gates.

### Matched-schema HEAD comparator established — 2026-10-10

The approved store/saga baseline now has 7,000 samples per operation at widths
32 and 4096, with raw evidence in
`zdb/interop/evidence/repair2-20261010/head-results.md`. It retains the current
configuration and Clang 23.1.1, reuses 13 input-matched objects, and normalizes
only isolated canonical schema defaults to native metadata. All C++ library
algorithms remain at HEAD; test instrumentation and missing unused build-input
corrections are recorded in `head-adjustments.patch`. Baseline smoke and sample
runs passed; no candidate regression or final memory acceptance is claimed.
The candidate rebuild is interrupted by an unfinished concurrent ZiTxStream
refactor; those edits were preserved.


Key53 focused milestone: build44934 terminal0, all3 scoped sequential-j4
commands0/no diagnostics. Runner69348 terminal0/all24commands0,171checks
pass; summary60813 terminal0. Six balanced before/current/HEAD orders,180000
samples/version/operation, verified14current source/artifact hashes. Median
paired current/before saga p50/p95/p99-1.286/-3.624/-4.147%; current/HEAD
-2.629/-1.968/-4.032%. Scheduler medians versusbefore within+.16%, no control
subtraction. Allocation means105.375/105.943 are noisy; short builtin strings
still physically copy on move, no heap-elision claim. Retain the focused owning
findDel key transfer; no universal performance acceptance. Evidence key53/README.md,
source.diff,summary.json,paired-ratios.json. Ordinary paired52 predates this edit.
Continue pending scoped store/saga integration, not HTTP/aggregate.


Store54 completed: build74708 terminal0, six sequential scoped-j4 directory
commands0, no compiler diagnostics. Runner31777 terminal0: sqlite units6,
postgres units13, sqlite integration56, postgres integration22 top-level tests
allpass (97total, no whole-program skips). This accepts pending external-store
batch20 native Float32/CStringVec controlled restart-after-update success and
compensation cases, plus strengthened parameter-size callback assertions.
Controlled DB restart is distinct from existing process-crash coverage.
Disposable PostgreSQL16.3/uint/libz used with original private socket55432,
TCP disabled, ~/postgres/bin; stopped cleanly and status rechecked. Linked
client libpq18.6 and sqlite runtime3.53.4 recorded separately. All26source/
artifact hashes verified; conditions/commands/logs retained in store54/.
No production edit/newtests/configuration/HTTP/aggregate work in this milestone.
Full dependent consistency, enabled suite, remaining matched workload acceptance
and final memory gate remain open; goalactive.


Paired55 completed: runner92292 terminal0/all24 workloads0, summary72330
terminal0. Six alternating HEAD/current pairs perwidth32/4096,180000samples
perversion/operation/width. Ordinary wrappers/shared fixture/current release;
all14key53 hashes verified, identical artifacts at both widths. Saga pooled
p50/p95/p99 versusHEAD width32-1.72/-4.33/-6.69%; width4096-1.15/-7.31/
-13.18%; median paired-1.94/-4.78/-5.66% and-2.00/-4.62/-4.96%. Scheduler
controls also improve; no subtraction/causal attribution. Width affects store
vectors/standalone payload save, not live-saga target size. Small-row store.update
still slower: median paired+1.35/+3.98/+3.33%, p50slower6/6; large-vector
update-8.95%median. Record residual instead of blanket performance acceptance.
Evidence paired55/README.md and bothwidth summaries. Next scoped dependent56
build refreshes Zf/Zi/Zdb tests/interop/examples and zum/ztc/zdf consumers; no
zhttp module/root aggregate target or configuration change.


Dependent56 retry71557 terminal2: Zi interop and Zdb test/itest/example
builds pass, Zum stops at missing libZrest.la. Owner runner93142 terminal2:
Zf344top-level tests pass; ZiTxStreamTest loses valid prefix after rejected
printable, then fixture dereferences absent sent[0] and SEGV. GDB reproduces.
Current flush early-returns on m_failed before sending retained prefix; this
contradicts current documented and HEAD behavior. Stale-object explanation was
considered but not established; source contains defect. prefix57 restores
HEAD send-nonempty/clear-owner/return-sticky-failure sequence, no new validation
or throws. Preserved before header/current key53 artifacts; source candidate
not yet accepted. Retry was already terminal before intended interruption; no
process killed. Prepare22 sequential-j4 owning/dependency/consumer directories,
including required Zrest before Zum; no zhttp/rootaggregate/config switch.


Prefix57 external runner completed all eight commands with expected exits
(PG stopped status3, all others0). sqlite3 unit6/integration56 and postgres
unit13/integration22:97top-level checks pass. All26recorded source/artifact
hashes rechecked unchanged before callback58; disposable PG confirmed stopped.
Dependent build15060 subsequently exposes ten Zum template errors: removing
SagaFn erasure leaves Table::findUpd missing-row callback argument as ZmRef,
which cannot deduce generic auto* action parameters. callback58 passes row.ptr()
at both findUpd/findDel missing-row sites, preserving raw-row callback contract
without restoring heap-backed erasure. Successful ownership and all failure
semantics unchanged. Source candidate pending batch rebuild/acceptance; original
build still live, no overlapping retry. Evidence callback58/README.md.


Scheduler comparison clarification: the pinned matched baseline is6e9a1252,
while current repository HEAD is83d96641. Production scheduler does differ
from the pinned baseline: overflow queueCount ordering guard and coordinator
thread retain/join lifecycle changes. None was edited in callback58. The shared
templated schedHop fixture remains identical on both sides; control results
measure each complete runtime stack. Do not claim identical production scheduler
or attribute observed changes solely to ZiFB. Exact diff/hashes recorded in
paired59/scheduler-baseline.diff and scheduler-sources.json.


Callback58 owning acceptance: runner7665 terminal0, threecommands0;
Zdb units91, messageinterop4 and integration6 top-level checks pass after
corresponding owning rebuilds. Source/artifact hashes retained. Incremental
build44634 remains live through sqlite/store/Zum/Ztc/Zdf; no compiler diagnostics
so far. Broader consumer, final memory and refreshed performance pending.
No production scheduler source changed; pinnedbaseline distinction recorded
separately in paired59. Goalactive.


Callback58 external runner93210 terminal0, alleightcommands expected exits:
sqlite unit6/integration56, postgres unit13/integration22:97checks pass.
All26source/artifact hashes verified unchanged; disposablepostgres stopped
(status3). Combined current Zdb/store acceptance198top-level checks. Zi201
checks remain prefix57 evidence with unchanged Zi source/artifacts; no blanket
broader-consumer or final-memory acceptance. Build44634 remainslive in Zum,
first11directories0, no compiler diagnostics so far. Next: poll same build;
after14successfulcommands run callback58/accept.py (consumer suites and final
memory), then paired59/both.py and perwidth summaries with no build overlap.


Callback58 Zum saga translation unit compiled successfully; object and Zdb
header hashes in callback58/zum-saga-object.json. Zumsrc linked and build
advanced to Zumtest, resolving the ten prior generic-pointer deduction errors.
This is compile evidence, not runtime acceptance. Build44634 stilllive; no
compiler diagnostics recorded. Paired55 allsix width32 shapes confirm update
524bytes HEAD versus516native with same fieldmask475, so a larger native message
does not explain the indexed-update residual. Full enabled-scope inventory now
explicit in enabled-suite-inventory.json/audit55.md; lower/network/Gtk/Zi-itest
checks are still outstanding beyond current scoped consumer batch. No extra
source/configuration change or unlaunched gate claimed as passing.


Callback58 Zum unit runner50831 terminal0: fiveprograms/35top-level checks
pass, including the existing Zum HTTP-dependent fixture without building the
zhttp module. Zumtest build completion was confirmed before this run; no
compilation occurred in the test command. Source/executable/library hashes in
zum-unit-artifacts.json. Remaining consumer runner now omits Zumtest and final
accept.py requires its recordedexit0. Build44634 remainslive at Zumrestart
integration compilation; no compiler diagnostics. No memory/perf gate launched.

Callback58 build44634 observed terminal2 after draining Ztc compiler jobs:
13commands, first12exit0 (including Zum1322.46s), Ztc failed because libZws.la
is missing. Consumer61 prerequisite inventory finds only that external missing
library; missing libZdf.la is produced by its planned owning src stage. No
source edit/configuration change. Retry will build Zws library then Ztc/Zdf;
accepted-build.json is generated only after actual first12+three successful
commands, preserving original failed build.json and source hash verification.
Final acceptance/memory/performance guards consume that successful15command
manifest; they do not erase or waive the prior build failure.

Consumer61 build59371 terminal0, allthreecommands0: Zws library13.83s,
Ztc532.29s, Zdf complete. No compiler diagnostics. accepted-build.json contains
15actualsuccessful commands (callback58 first12+consumer61 three), preserving
the earlier failed attempt separately. Current production header hash unchanged.
Readonly make-q freshness queries for zu/zm/zt/ze/zf/zi/zv/zdb source targets all
return0; owner-freshness.json records commands/results. Consumer prefix12982
is live through Zum integration after Zuminterop0; no final memory or refreshed
performance run yet. Remaining full enabled scope stays explicitly incomplete.

Fixture62 retry48022 terminal0, threefixture directories0, no compiler
diagnostics. Only RepairRow and AllTypes CStringVec native element ownership
and standard AliasFn accessors changed; production sources/libraries untouched.
Current Zdb/store acceptance and final memory rerun started after allthreebuilds
passed. The prior120invalidreads are retained in callback58; no suppression or
new Row buffer-reference storage added. Four Zi Memcheck passes are reused only
with unchanged binary/library hashes; the failed old saga result is not reused.
Consumer61 build and callback58 sixconsumer suites remain separately accepted
for unchanged production source. Current-generation performance not yet run.

## Direct transmit measurement after paired68

Existing already-built zifbiobench tx mode completed four processes0,11
alternating rounds each. Source/executable/library hashes unchanged; no rebuild
or configuration change. Payload64/928 emits104/968bytes: direct/staged
completed-span insertion0/104 and0/968, pool requests1/1, paired median round
time-5.92/-4.90%. Payload944/4096 emits984/4136bytes and takes the existing
splitting fallback: full completed-span insertion on both paths, one extra
direct pooled-owner request (3/2 or6/5), paired timing+62.81/+15.60%.
Counts are pool requests, not backing reallocations; in-owner growth relocation
and detach compaction remain separate. This is a current-native mechanism
control, not canonical HEAD/network/external-store/saga latency evidence.
See tx69/README.md,commands.json,summary.json,artifacts-verified.json.

Static indexed-update reinspection: loadRow borrows the RecordView then builds
its required owning mutable Tuple once; updTuple moves mutable Values. Before
mutation, independent old secondary-key snapshots are retained; changed-key
insertion moves the new Tuple. Pinned baseline uses the same snapshot/delete/
reinsert algorithm. No new extra copy/post is established by the small median
residual. No speculative code edit or numerical acceptance threshold introduced.

## External70 failure and store71 isolation

External70 incremental builds4commands0, current saga86checks and refreshed
Saga Memcheck0 (zeroerrors and definite/indirect/possible lostbytes). Four small
HEAD/current sqlite3/postgres smoke processes0; dynamic-loader records verify
intended tree libraries/modules. Full matched baseline sqlite3 workload then
reported primary-key-missing SagaData/SagaStep deletes and became INACTIVE.
Specific failed child609450 terminated withSIGTERM; runner1458 exit1, own
postgres stopped0. Database/partialCSV/log/interruption record retained.
Candidate same1000x3 width32 complete external workload exited0. This establishes
an observed baseline-only failure in these runs, not its cause or general
correctness. No latency acceptance from the failed batch.

Store71 adds a shared ZDB_REPAIR_STORE_ONLY selection to stop after existing
direct store operations, isolating their performance from baseline saga failure.
No production/scheduler/library algorithm edit. Default full workload unchanged.
Both fixtures rebuilding incrementally under same configuration,-j4. Baseline
full external-saga latency remains unestablished; the failure is not hidden by
the store-only comparison. Prior memory-saga and functional external-store
acceptance keep their recorded scope. zhttp remains stopped; scope question
pending. Broad completion still unproven.

## Grow73 postgres select diagnostic

Both existing release fixtures under libtool exec gdb completed0 with zero
observer errors. Four width4096 cycles/version including warmup; all create/
drop/stop commands0 and artifact hashes unchanged. Each select grows twice,
528→16912→33296, carries0front and32+16420tail bytes. Eight observed growths
and65808tail bytes per version, identical. No extra native growth or relocation
volume for this exercised row. Native saveArray and canonical pvectorIter both
write borrowed postgres elements directly into uninitialized output. No owning
array relay found. Debugger timings do not establish latency/CPU equality.

The repeated postgres select discrepancy remains+3.32% median; growth is now
ruled out as an extra-cost mechanism in this scope. Next investigation targets
conversion/finalization CPU versus backend/scheduler waits. No speculative
production change, no new validation/exception/configuration/HTTP build.
Evidence:grow73/README.md,head/candidate observations.json,installed.json,
commands.json,artifacts-verified.json. Goal remains active and incomplete.
