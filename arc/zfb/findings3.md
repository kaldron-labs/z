# Zdb data-store audit against findings.md



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

## Historical executable and performance evidence — 2026-10-10

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


Callback58 passed all 97 external-store checks: sqlite3 unit6/integration56 and
postgres unit13/integration22. All 26 recorded source/artifact hashes were
rechecked; the disposable postgres cluster is stopped. The same candidate passed
101 Zdb unit/interop/integration checks. Zum's saga translation unit now compiles
after the missing-row callback fix, and the dependent rebuild passed.
Zum's five unit programs passed 35 checks; the six remaining consumer commands
passed 104 top-level checks. Fixture62 corrected ownership of CStringVec elements
retained by native test rows, preserving production borrowing. Its rebuilt
fixtures passed all 198 Zdb/store checks. Nine final Memcheck programs passed
with zero errors and zero definite, indirect or possible leaked bytes; four
unchanged Zi results were reused after hash verification. Enabled coverage
excludes zhttp under user steering; broad performance acceptance remains open.
Paired59 timing completed. The unchanged
Zi generation's 201 checks remain prefix57 evidence.

Paired55 is the preceding ordinary comparison, before prefix57 and
callback58. Saga distributions improve at both measured widths; small-row indexed
update remains 1.35/3.98/3.33% slower by median paired p50/p95/p99. Current rebuilt
generation timing is recorded in paired59 below. Width changes store vectors and standalone saves;
the live-saga target rows and step sequence remain fixed. Do not promote older
unbuilt/live statements below to current status. Evidence: `callback58/`,
`paired55/`, `paired59/` and
`audit55.md` under `zdb/interop/evidence/repair2-20261010/`. The pinned baseline
is `6e9a1252`; current HEAD is `83d96641`. HTTP/root aggregate builds remain
excluded by user steering.

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

### Historical checkpoints through read51

Read51 source batch (unbuilt): pass existing Msg::RecordView/shard/UN from
StoreTbl::write into its sole private loadRow caller; fetchVN once while retaining
same owning Tuple/MemRow/filter/error/callback behavior. Move terminal captured
buf refs into DB::replicate in both AnyTable::write closures; necessary leader
capture copy and allposts remain. Move final-use update/delete temporarykeys
into existing missing-row diagnostic lambdas (coldpath, no warm latencyclaim).
Before accepted key49 sources/artifacts saved and verified; all owning binaries
remain unchanged. No build/test/profiler thisturn; candidate source not accepted
by earlier171 tests orpaired50 timings. Evidence `zdb/interop/evidence/repair2-20261010/read51/source.diff`, `README.md`,
unbuilt.json. Continue static batch review before owning-j4 milestone; no HTTP,
aggregate, alternate configuration, exceptions or validation changes. Goalactive.


Paired50 ordinary-wrapper comparison completes: runner12508 terminal0 all12
workload processes0; summary70048 terminal0. Six alternatingpairs,count10000
rounds3 width32,180000samples/version/operation. HEAD/shared fixture unchanged;
current artifacts exactly match key49/after.json. Saga HEAD/current p50
19.286/19.145us (-.73%),p95 24.636/25.157us (+2.11%),p99 29.695/32.140us
(+8.23%); median paired changes-1.70/+2.50/+2.69%. Scheduler p95 pooled
2/8/32-post-.69/+3.60/+5.64%, no subtraction or causal attribution. Retain
focused single-view key extraction based on key49's controlled before/after
improvements, unchanged ownership/node layout and171 passing checks; full
no-material-regression gate remains open because ordinary saga tails stillworse.
No further production edit/build/configuration change. Evidence `zdb/interop/evidence/repair2-20261010/paired50/README.md`;
full integration/store/final-memory gates stillopen, HTTP/aggregate remainstopped.


Key49 controlled milestone completed: longer runner66841 terminal0 all21
commands0 (3 verified loader smokes+18 workloads), summary18301 terminal0,
180000samples/version/operation. Median paired current/before saga p50/p95/p99
changes-1.294/-3.683/-2.746%; current/HEAD-2.738/-2.027/-5.934%. Scheduler
medians within+.5% versusbefore; generic saga-save unchanged. Compiled
Record.data slot guards Order2→1,SagaData2→1,SagaStep3→1 corroborate one
borrowed view per extraction; not dynamic instruction counts. Retain focused
key-read change after171 passing checks and this controlled comparison; short
trial's tail concern superseded by stronger but still variable measurements.
Ordinary-wrapper matched HEAD/current acceptance now running session12508,
paired50/,6 alternatingpairs count10000 rounds3 width32. No further source
change/build/profiler; exact HEAD artifacts/shared benchmark match prior.
Full repair2 gates remain open; HTTP/aggregate stay stopped.


Key49 candidate removes repeated envelope/nested-root navigation per key field:
localized RepBuf_<T> tuple renderer extracts one borrowed view, shares existing
view-field getters; no node size/ownership/validation/callback changes. Before
sources and artifacts preserved. Initial build82248 failed metadata adapter
layer; attempt1 retained. Corrected build55965 terminal0 all6 commands0 without
diagnostics. Runner32860 terminal0:171 checks pass,3 verified loader smokes,
9 latency runs and2 heap runs0. Separate getters disappear; iterKey sizes
305→506/355→605/366→787 (Order/SagaData/SagaStep), calls5→3, not timing proof.
First3 rotations have mixed medians and p99 worse all3 versusbefore. Candidate
not accepted. Longer six balanced-order triples now running session66841,
count10000 rounds3 width32, explicit matched loaders. No HTTP/aggregate,
configuration change or profiler. Evidence `zdb/interop/evidence/repair2-20261010/key49/README.md`; inspect session
before continuation and decide retain/reject from evidence. Current source and
artifacts contain candidate until that decision; before/ supports exact restore.


Profile48 refreshes clock sampling on accepted fixed release artifacts after
narrow commit packing: four workloads0 (runner77453 terminal0), both orders,
count10000 rounds7 width32. All report commands0 (report81172 terminal0), generated
only after collection. Every header warns997→0 timer interval; preserve reliability
limit, not timing acceptance. DB total HEAD/current1.258/1.229s then1.270/1.313s;
replicate inclusive.170/.155 then.151/.157; commit.082/.103 then.100/.081.
Saga store loadRow inclusive.085/.076 then.082/.068; store syscall.208/.239 then
.206/.265. No consistent extra reader/builder CPU attribution or extra-post claim.
Callback audit retains per-step SagaStepDriver→SagaCompleteFn allocation because
copied erased delegates share consumed mutable state; simply passing copied
concrete drivers changes behavior. Source mechanism atHEAD too. Evidence
`zdb/interop/evidence/repair2-20261010/profile48/README.md` and `callbacks.md`. All11 accepted hashes match; no production
edit/build/configuration change. Paired46 remains ordinary latency evidence;
performance/full repair2 gates remainopen, HTTP/aggregate stillstopped.


Hw47 instruction-sampling diagnostic rejected as unusable. User-only gprofng
counter probe completed, but real candidate --repair-latency10000 7 32 ran82s
without a completed benchmark phase; SIGINT child-2, runner11511 terminal254.
HEAD/later pairs not run. Report99.14% instructions in one libc static routine
under store record<Upd>/builder.string; interrupted experiment not closed.
No cause, corrupt length or saga cost inferred. Read-only gdb attach failed
ptrace permission; preserve reports/logs `zdb/interop/evidence/repair2-20261010/hw47/`. All11 accepted hashes still
match; no source/build/configuration changes. Warm write audit uses trusted
msg_/record_ and direct reads; no repeated verification demonstrated. No removal
of verification justified. Paired46 remains latest ordinary latency evidence;
full repair2 remains active and open.


Paired46 longer fixed-fixture comparison completes: runner71183 terminal0,
all12 alternating workload processes0; summary70678 terminal0. Sixpairs,
count10000 rounds3 width32,180000samples/version/operation, normal libtool
wrappers and no heap snapshots. Both fixture sources and recorded artifacts
exactly match paired36; all11 accepted source/artifact hashes still match.
Saga HEAD/currentp50=19.006/19.206us (+1.05%),p95=23.313/24.356us (+4.47%),
p99=29.295/29.485us (+.65%). Median paired changes+.47/+4.19/+.25%; p95 slower
in5/6 pairs but ranges-8.59% to+17.05%. Scheduler controls slightly faster and
codec saga-save231→151ns; no control subtraction/causal claim. Same fixed
artifacts show smaller median but larger p95 gap thanpaired36. No production
edit/build/configuration change. Evidence `zdb/interop/evidence/repair2-20261010/paired46/README.md`; no-material-
regression and full repair2 gates remain open.


Ring45 scheduler ownership audit completes: both matched whole benchmark
processes exit normally,982 successful enqueues each, identical target counts
(SID1=6,SID3=535,SID4=441), zero failed pushes and zero heap-owned Fn objects at
enqueue. Direct m_ptr OnHeap checks supplement allocation-entry probes that
could miss inlined code; zero observer errors. Captures range8–144 bytes; no
fixed inline-size threshold. Referenced capture objects may independently
allocate, and deferred closures never queued are outside direct proof. Scope is
whole process (11 sagas plus store/controls/setup/teardown), not recovery or
saturation. No extra aggregate posts or overflow demonstrated; no new scheduler
optimization justified. All11 accepted source/artifact hashes still match.
No production edit/build; debugger timings not acceptance. Evidence `zdb/interop/evidence/repair2-20261010/ring45/`;
performance and full repair2 gates remain open.



Wire44 confirms the actual growing SagaData envelope has the same208-byte body
and112-byte nested row in native and HEAD. The existing4-byte application header
requires growth beyond the original208 allocation. With child bytes, presence
and alignment fixed, arithmetic enumeration of120 row field orders and all
Record/root field orders for ten exercised shapes reproduces current sizes and
finds each already minimal. No simple padding reorder is justified for these
warm saga messages; no production edit or build. Both debugger runs exit0 with
zero observer errors; prior43before source/artifact hashes still match. Evidence:
`zdb/interop/evidence/repair2-20261010/wire44/`. Tail acceptance remains open.



Rootcache43 rejected and fully restored. Skipping final Msg cache scratch removed
100 arena growths but shifted them into100 header-prepend relocations. Both
versions move20800 bytes total across101 sagas; a208-byte body plus4-byte header
still needs more than the original208-byte allocation. Wire lengths and replicate
counts unchanged. Corrected candidate171 checks passed, but no copy/allocation
elimination was established. All before source/artifact hashes restored exactly,
restored171 checks pass; diagnostic objects marked stale for next build. Earlier
accepted optimizations remain. Evidence:
`zdb/interop/evidence/repair2-20261010/rootcache43/`.



Layout42 compares pre/post-diagnostic executable generations with identical
explicit library loading, four rotating orders,60000 saga samples/version.
All16 workload processes and4 loader smokes succeed. Pre-diagnostic native/HEAD
p50=18.895/19.326us, p95=25.768/24.556us, p99=29.776/29.766us; paired median
changes -2.47%/+3.98%/-1.98%. Both generations vary in tails/controls; no isolated
layout cause or stable median regression follows. Loading differs from earlier
normal-wrapper acceptance and the pre-diagnostic p95 still worsens. Both shared
benchmark source/executables are now restored byte-identically to paired36;
diagnostic source/binaries preserved only as evidence. Production hashes and
configuration unchanged, current86 saga checks pass; diagnostic objects marked
stale for next incremental build. Acceptance remains open. Evidence:
`zdb/interop/evidence/repair2-20261010/layout42/`.



Paired41 completes eight longer uninstrumented processes using usage39 fixture
binaries with the diagnostic disabled; no rebuild or production library change.
Two alternating pairs per width,140000 saga samples per implementation/width.
Width32 HEAD/current p50=18.946/18.314us (-3.34%),
p95=22.953/23.805us (+3.71%), p99=28.093/27.762us (-1.18%).
Width4096 p50=19.106/18.945us (-0.84%), p95=24.966/23.354us (-6.46%),
p99=30.427/28.182us (-7.38%); scheduler controls also improve there.
Widths select separate codec/store payloads, not larger application LiveSagas.
These binaries differ from paired36 through benchmark-only diagnostic code;
no stable median regression/removal or codec-only cause follows. Width32 p95
remains worse; full performance acceptance stays open. Lookup40 establishes
native cache comparisons use separate borrowed getters where HEAD inlines;
Order/SagaData/SagaStep getters have no calls, allocator or memcpy. No new
owning-row read temporary or production optimization established. Evidence:
`zdb/interop/evidence/repair2-20261010/paired41/` and `lookup40/`.



Usage39 adds opt-in round-boundary thread CPU/context-switch diagnostics to the
matched shared saga benchmark; no production code or library hash changed.
Both retained-release fixture builds passed without warnings and all86 candidate
saga checks passed. Six alternating pairs completed,180000 latency samples per
implementation. HEAD/current p50=19.026/18.685us (-1.79%),
p95=23.444/24.295us (+3.63%), p99=28.974/30.678us (+5.88%). Median paired
changes are -2.55%/+3.78%/+4.33%. Whole-round worker CPU accounting does not show
added current CPU cost: paired median differences DB-0.176us and store-0.200us
per saga. DB voluntary context switches increase0.057 per saga; these counters
are not application post counts and do not establish a scheduler cause.
Diagnostic callbacks occur only at round boundaries, outside operation timers;
CPU counters include warmup/reset callbacks and codec saves. This diagnostic's
median direction differs from uninstrumented paired36 (+4.7%); tails remain
worse and full performance acceptance remains open. Evidence:
`zdb/interop/evidence/repair2-20261010/usage39/`.


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


Milestone17 passed the incremental build and 171 top-level tests: 101 owning unit,
52 sqlite3 integration, and 18 postgres integration tests. Saga serialization
failure now returns its codec error and follows the existing failed-submission
callback without a new exception. The required-field admission fixture passed.
Dependent Zum callers still require the final integration build.

Additional equal-affinity diagnostic pairs (`paired17-affinity/`) retain a
complete-saga median regression of 5–14% across four pairs, despite store insert
improvements. CPU sampling (`profile17/`) is inconclusive about its cause.
Milestone18 passed all177 owning unit and store integration tests. Shared native
Float32/CStringVec saga insert, update, compensation, uncached selection, deletion
and journal cleanup now pass in memory, sqlite3 and postgres. Crash/recovery of
those particular native fields is not newly established by this fixture.

Milestone16 additionally passed the shared supported-column operation fixture in
memory (81-test unit suite) and postgres (18-test integration suite). Both stores
round-trip native float/CStringVec with double/StringVec controls, defaults,
empty/single/multi-element vectors, primary/secondary keys, grouped selection,
updates, recovery, reopen with replaced BFBS storage, and deletion. Two fixture
errors were corrected before acceptance; no production error-policy change was
needed. The later milestone18 adds saga conversion coverage; final gates remain
pending.

Milestone15 passed 100 unit, 52 sqlite3 integration and17 postgres integration
top-level tests under the retained release configuration. The postgres cases
exercise real OID mismatches with full recognized-column counts and shuffled
physical order, plus general fallback diagnostics. Synthetic planner tests cover
parameter-count, borrowed String/Bytes and aggregate bounds without large
allocations. This does not yet prove every size-failure callback path.

Matched memory-store/codec/saga benchmark pairs are in
`zdb/interop/evidence/repair2-20261010/paired15*`. Allocation reductions are
measured, but repeated complete-saga medians are4–15% slower than the matched
HEAD fixture. Performance acceptance remains open; these measurements do not
cover sqlite3/postgres latency, cold recovery or transmit splitting.

## Execution reconciliation — 2026-10-10

The audit below records the 2026-10-09 source state; it is not a description of
the repaired worktree. `repair2.md` governs implementation and acceptance.
Float32/CStringVec conversion repairs passed milestone11 unit checks and sqlite3
integration. The earlier generic nested UDT/array store requirement was incorrect: store
fields map one-to-one to supported scalar/vector columns. Composite UDTs and
arrays of composites are not supported column types. Their absence is not a
store type-coverage defect. Remove the resulting extension and fixtures.

`OIDs::name` covers every concrete postgres Value alternative. The missing-name
path described below is reached by an unresolved zero binding, not by a missing
static mapping for a supported Value. Complete conversions rather than adding
redundant schema rejection or open-error machinery. A real existing-column OID
mismatch remains possible; retain the recognized-column count and record the
failing field identity separately for its diagnostic.

The existing Zdb 100 MiB policy also applies to parameter buffers. The large
Int8Vec example in §5 demonstrates unsigned arithmetic wrap in the old helper;
its payload exceeds that policy and does not establish reachable corruption for
a compliant Zdb message. Check count against `(limit - header) / elementWidth`
before multiplication, then check payload and aggregate scratch budgets before
addition. sqlite3's prior connection/unsigned checks did not enforce the Zdb cap
or size borrowed String/Bytes parameters. Both stores now have parameter-policy
checks; the remaining acceptance fixtures are recorded in `plan.md`.

No new exception is required for parameter sizing. Existing callbacks carry
size-policy failures. These changes and the narrow mismatch diagnostic are the
authorized exceptions to preserving existing validation and error behavior.

Audit date: 2026-10-09. Baseline: `HEAD` = `6e9a1252`; conclusions concern
the current worktree. `zdb_sl` means `zdb_sqlite`, implementing `ZdbSL`.

This audit follows the issues in `findings.md` through PostgreSQL and SQLite
field binding, insert/update/delete, lookup/select/recovery, and shared saga
serialization. Relevant unit-test code, integration fixture definitions,
test targets and benchmark documentation were inspected for coverage. This is a source audit, not an
exhaustive Zdb correctness review. No implementation changes, builds,
tests or benchmarks were performed. Only this report was added.

## 1. Results

| Issue from findings.md | PostgreSQL | SQLite |
| --- | --- | --- |
| Owning bitmap temporary copied into backend Tuple | Not present | Not present |
| `CStringVec` binding absent | Present | Present |
| Float32 scalar/vector binding absent | Present | Present |
| Arbitrary composite UDT / UDT-array columns | Outside the existing store contract | Outside the existing store contract |
| Silent void-value storage as in memory backend | Not established; postgres setup blocks these bindings, but error handling has defects | Not present; constructor explicitly rejects unsupported binding |
| Blanket 32-byte builder alignment | Present in result builders and shared frontend | Present in result builders and shared frontend |
| Changed native scalar defaults | Shared frontend semantics | Shared frontend semantics |
| Scheduler queue-count check | Shared scheduling cost | Shared scheduling cost |
| Whole-mutable-field update | Present | Present |
| Delete serializes secondary keys unused by backend | Present | Present |
| Memory-store owning key-Tuple/index copies | Not applicable | Not applicable |
| Saga definition staging and cold-read materialization | Shared staging; data-store result conversion | Shared staging; data-store result conversion |

The main correctness findings are the common type coverage gaps and two
postgres setup-error defects. An additional large-vector sizing defect was
found in PostgreSQL. The alignment issue remains a conditional capacity and
padding risk, not a demonstrated timing regression.

## 2. Confirmed type coverage gaps in both backends

PostgreSQL [xField](zdb_pq/src/ZdbPQ.cc#L865) and SQLite
[resolveType](zdb_sqlite/src/ZdbSL.cc#L195) have effectively the same supported
field inventory:

| Field codes / representation | Binding |
| --- | --- |
| `CString`, `String` | Both accepted as backend String |
| `Bytes` | Byte-vector storage, distinct from `UInt8Vec` |
| `Bool`, signed/unsigned integers through 64 bits | Supported |
| `Int128`, `UInt128`, `Fixed`, `Decimal`, `Time`, `DateTime` | Supported mapped inline values |
| `Float` represented as Double | Supported |
| `Float` represented as Float32 | Unsupported |
| UDT Bitmap / IP | Supported recognized mappings |
| Other UDTs and object arrays | No general binding |
| `StringVec` | Supported |
| `CStringVec` | Unsupported despite equivalent vector-of-string wire representation |
| `BytesVec` | Supported wrapper-table vector |
| Integer vectors, including 128-bit vectors | Supported |
| `FloatVec` represented as Vector/Double | Supported |
| `FloatVec` represented as Vector/Float | Unsupported |
| `FixedVec`, `DecimalVec`, `TimeVec`, `DateTimeVec` | Supported |

Neither resolver has a BaseType Float case, either scalar or vector-element.
Both accept only `StringVec` in the Vector/String branch. Their UDT branches
recognize Bitmap and IP only.

The native codec chooses Float32 for native `float`, and Double otherwise:
[Rep<Float>](zf/src/ZfFB.hh#L1235). Thus native `float` and arrays of `float`
reach these gaps even though typed serialization supports them. Backend storage
being double-width does not make a Float32 payload readable by the existing
double loader; the binding and both conversion directions need adjustment.

HEAD has the same Double-only and StringVec-only restrictions in both data-store
resolvers. The coverage gaps are pre-existing. Metadata-derived Float32 schemas
can newly expose the Float limitation for migrated declarations previously
paired with Double schemas; a table-specific regression requires its old schema
and current native declaration. Arbitrary UDT support is a capability limitation,
not evidence that every UDT must be representable as columns.

### SQLite: explicit failure, no silent void persistence

The [constructor](zdb_sqlite/src/ZdbSL.cc#L1060) rejects a missing field or
zero type with `unsupported or inconsistent field <id>`. The
[Store::open catch](zdb_sqlite/src/ZdbSL.cc#L1031) passes the exception to the
open callback. Unsupported values cannot enter the normal prepared-statement
path through this binding route. This satisfies the report's alternative of
explicit setup rejection, although it does not supply the missing bindings.

### PostgreSQL: zero binding survives construction

[StoreTbl construction](zdb_pq/src/ZdbPQ.cc#L1050) retains the zero type without
rejecting it. postgres setup subsequently prevents a normal usable table, but the
failure path is materially weaker than SQLite's. It must not be described as
clean rejection in all cases.

## 3. postgres setup-error defects

### P1: unsupported field on a new table leaves the open callback pending

Trigger: open a previously absent table containing a field whose `xField` type
is zero, for example `CStringVec` or Float32.

[mkTable_send](zdb_pq/src/ZdbPQ.cc#L1336) asks `OIDs::name(0)`, receives null,
logs `missing OID name for type=0`, and returns `SendState::Unsent`. It neither
calls `open_failed` nor queues another operation. In
[Store::send](zdb_pq/src/ZdbPQ.cc#L496), Unsent work is not added to the sent
list, so no data-store response will advance this open operation. Fatal is a logging
severity here, not an exception or callback completion.

The table's stored open callback remains pending until some external teardown
or intervention. This is a liveness/error-delivery defect, not silent successful
storage of a void field. The same missing-name return path exists in HEAD.

Repair direction: reject unresolved bindings before starting postgres setup and
deliver a field-specific error through the open callback. Do not add per-row
type checks to compensate for an invalid setup state.

### P1: existing-table mismatch diagnostic can read past m_xFields

[mkTable_rcvd](zdb_pq/src/ZdbPQ.cc#L1428) increments the matched-field count
when a column name is recognized, before validating its OID. On mismatch it
sets failure. At end of results, its
[error construction](zdb_pq/src/ZdbPQ.cc#L1383) uses that count as an index:

```cpp
auto i = m_openState.field();
// lambda capture:
field = m_xFields[i].vfield
```

If all `n` application columns have been encountered and the final application
column mismatches, `i == n`, outside the valid range `[0, n)`. A one-field
existing table whose application field resolves to zero is a minimal source-level
example. The subsequent diagnostic dereferences `field->type`. Even when the
index is in range, a count is not the identity of the mismatching field, so the
message can name the wrong field.

This can turn intended schema rejection into invalid memory access. HEAD has
the same count/index misuse. Retain the actual failing field identity, or emit a
diagnostic that does not index the array from a count.

These two findings refine the earlier chat answer: PostgreSQL does block normal
use of unsupported bindings, but its new-table branch does not complete rejection
through the open callback, and its existing-table error diagnostic is unsafe.

## 4. The memory-store bitmap-copy regression is absent

PostgreSQL's [bitmapData](zdb_pq/src/ZdbPQ.hh#L746) obtains a borrowed wire-word
view. [loadValue<Bitmap>](zdb_pq/src/ZdbPQ.hh#L912) writes the word count and
big-endian words directly into the operation's variable buffer. `Bitmap` itself
contains a byte span, not an owning `ZtBitmap`.

SQLite's [bitmapData](zdb_sqlite/src/ZdbSL.cc#L498) likewise returns a borrowed
word view. [encodeField](zdb_sqlite/src/ZdbSL.cc#L645) writes count and encoded
words directly into the planned scratch region.

The return paths also write directly into the pooled FlatBuffer:
[PostgreSQL saveOffset](zdb_pq/src/ZdbPQ.hh#L1196) and
[SQLite saveOffset](zdb_sqlite/src/ZdbSL.cc#L1753). Neither constructs an owning
bitmap and then copies its named lvalue into a Tuple. This applies to ordinary
inserts, mutable updates, primary-key lookups/deletes, and corresponding saga
operations. Required wire/storage conversion still copies bitmap words.

PostgreSQL's diagnostic Value printer does construct an owning bitmap for
formatting; that is separate from persistence/result conversion and does not
reproduce the additional owning-copy regression.

## 5. Large-vector sizing: additional PostgreSQL defect

### P2: parameter expansion uses unchecked unsigned size arithmetic

[vecSize / vecVarSize](zdb_pq/src/ZdbPQ.hh#L248) compute PostgreSQL array storage
sizes in `unsigned`. [VarAlloc](zdb_pq/src/ZdbPQ.cc#L2276) sums those sizes in
another unsigned accumulator without overflow checks, then allocates that size.
The encoders subsequently iterate the original element counts and write all
elements using [vecAppend](zdb_pq/src/ZdbPQ.hh#L266).

PostgreSQL arrays add a four-byte length word per element. A valid FlatBuffer
byte-element vector can therefore fit within the native wire size limit while
its data-store parameter representation overflows unsigned sizing. For example, on
the target 32-bit-unsigned platforms, an Int8Vec of 858,993,456 elements requires
`20 + n * (4 + 1) = 4,294,967,300` parameter bytes. `vecSize` wraps to 4. The
encoder still writes the original count, exceeding the allocated scratch.
This is conditional on very large payloads; no such allocation or corruption
was executed in this audit. libpq's signed parameter-length boundary also needs
to be enforced before encoding/sending.

The sizing helpers and unsigned accumulation also exist in HEAD. This is a
pre-existing correctness defect rather than a codec migration regression.

SQLite has an explicit contrast: [planInput](zdb_sqlite/src/ZdbSL.cc#L591) uses
64-bit sizes, checks the connection's length limit and unsigned total, and its
update path checks combined key/update scratch size. The analogous unchecked
parameter-expansion path was not found there.

## 6. Copies, allocations and operation paths

### PostgreSQL

[write](zdb_pq/src/ZdbPQ.cc#L2605) posts a buffer reference and callback to the
store, which queues the query. [write_send](zdb_pq/src/ZdbPQ.cc#L2660) builds
parameter Values in scratch; scalars are inline and String/Bytes reference the
message bytes. Bitmap and vectors use an aggregate variable scratch buffer for
PostgreSQL binary encoding. The libpq client/server transfer is additional work;
this audit does not claim the transport copies no bytes.

Array sizing precedes encoding. String/blob vector sizing scans elements, then
encoding scans them again. Each destination is filled directly; there is no
owning array of owning native strings/blobs between the wire view and the data-store
parameter. Scratch can fall back to framework heaps under stack pressure.

[select_rcvd](zdb_pq/src/ZdbPQ.cc#L2368) and
[find_rcvd_](zdb_pq/src/ZdbPQ.cc#L2473) make non-owning Values over PGresult
storage, then serialize into a pooled result buffer before invoking the callback.
Full-row results use startNested/endNested in the same builder, rather than
copying a separately serialized row into an envelope.

### SQLite

[write / write_](zdb_sqlite/src/ZdbSL.cc#L2160) dispatches to the store thread
and performs a transaction. String/Bytes bind borrowed message spans using
SQLITE_STATIC; ordinary integers bind as SQLite integers. Other values are
encoded directly into planned aggregate scratch and bound as blobs. This avoids
an owning per-field Tuple, but does not eliminate SQLite's own persistence work.

[saveTuple](zdb_sqlite/src/ZdbSL.cc#L1940) reads statement columns and streams
the result into the pooled builder. Vector conversion writes destination elements
directly; string/blob vectors use offset scratch for their wire children.
[row_](zdb_sqlite/src/ZdbSL.cc#L1964) nests the row in its envelope in that same
builder. Stored-value encoding checks belong to the data-store input boundary, rather
than being redundant verification of locally generated frontend FlatBuffers.

### Updates and deletes in both

The common producer serializes all Mutable fields plus the primary key for
updates. Both data-store implementations bind all mutable fields, regardless of which
element the application changed:
[PostgreSQL](zdb_pq/src/ZdbPQ.cc#L2674),
[SQLite](zdb_sqlite/src/ZdbSL.cc#L2214). Large mutable vectors therefore incur
full serialization and data-store conversion on every update. Mutable primary keys
still have no separate old-key/new-key protocol.

The common delete producer serializes all keys, while both data-store deletes consume
only key zero:
[PostgreSQL](zdb_pq/src/ZdbPQ.cc#L2689),
[SQLite](zdb_sqlite/src/ZdbSL.cc#L2242). Large secondary keys are unused payload
for these consumers too. Changing the common delete filter requires checking
other consumers. These are existing architecture costs, not newly introduced
owning temporaries.

data-store engines maintain indices. The memory backend's extractKey/index-node
owning-Tuple copies and update key snapshots do not occur in these data-store paths.
Cold find/select/recovery still materialize serialized result buffers followed
by frontend native values; there is no direct stored-value ownership transfer.

## 7. Alignment, defaults, scheduling and sagas

Both result builders default to `ZiFB::IOBuilder<Wire::MaxAlign>`:
[PostgreSQL](zdb_pq/src/ZdbPQ.hh#L1074),
[SQLite](zdb_sqlite/src/ZdbSL.cc#L1662). MaxAlign is 32; IOBuilder's constructor
defaults its runtime alignment to that template value. The
[shared frontend](zdb/src/Zdb.hh#L135) has the same setting. HEAD used
`Zfb::IOBuilder`, whose alignment was eight bytes, for data-store results. Thus eight-byte schemas
can pay extra rounding/padding in both data-store directions. Actual growth or timing
impact requires workload/buffer-boundary measurements. Preserve explicit field
alignment and schema requirements if reducing the default.

Metadata-derived defaults change what the shared frontend emits relative to
old handwritten schemas. Both data-store scalar input paths read reflected defaults
when a field is absent; mapped inline values use the native constant fallback.
This is the resolved default contract, not evidence of a backend text-parsing
regression. Database rows store materialized column values, so wire omission does
not mean the column is omitted from an insert.

The [scheduler queueCount check](zm/src/ZmScheduler.cc#L323) is shared by queued
dispatch. Its source comment explains the ordering requirement: new work must
not overtake callbacks waiting for ring space. The extra load/branch must not
be charged to data-store field decoding or removed without preserving that invariant.

Saga bookkeeping and target operations use these same backend paths. The
temporary definition buffer, copy into SagaPayload, and copy into the SagaData
Bytes field are shared frontend staging. data-store Bytes binding/conversion
then applies; there is no memory-store owning-Bytes allocation in either data-store
backend. Recovery serializes columns back into result buffers, loads native
payload storage, and retains the required nested-definition verification.
Repeated saga updates multiply full-mutable-vector work. No per-step BFBS
generation or extra reflection object graph was found in these data-store paths.

## 8. Coverage limits and priorities

The SQLite AllTypes fixture uses `double` / `ZtArray<double>` and StringVec;
see [zdbsltypes.hh](zdb_sqlite/itest/zdbsltypes.hh#L17). It therefore does not
exercise Float32 or CStringVec binding. PostgreSQL's unit tests cover binary
array/parameter layout and scratch behavior; integration tests cover ordinary
rows, sagas and continuation queries. Inspection found no focused regression
test for either data-store resolver's missing type bindings, the two PostgreSQL open
failure branches above, or overflowing array expansion. Existing fixture breadth
must not be taken as complete ZfFieldTC coverage.

Priorities:

1. Complete supported field conversions. Preserve the valid-column count and
   retain mismatched field identity separately for diagnostics (§5.1.1 of repair2.md).
2. Normalize CStringVec and support Float32 conversion in both directions where
   intended. Preserve the existing supported-column contract and error behavior;
   arbitrary composite UDTs are not supported columns.
3. Bound PostgreSQL parameter expansion before allocating or writing scratch,
   including per-parameter signed lengths and aggregate scratch arithmetic.
4. Select required builder alignment without imposing 32 on every result.
5. Measure whole-vector updates, delete secondary-key payloads and saga staging
   separately from migration regressions.

All positive/negative findings above are source-level conclusions limited to
the traced paths. No latency, allocation-count benchmark, data-store integration result,
or runtime reproduction is claimed.

## Focused saga copy/allocation audit — 2026-10-10

Read submission, MSaga dispatch, forward/reverse operations, journal deletion,
store emission and recovery scans against matched HEAD. Findings below are
source-established costs, not measured attribution of the remaining regression.

| Path | Cost and ownership | HEAD comparison / next action |
| --- | --- | --- |
| `MSaga::run` (both overloads) | Capturing forwarding lambda around an existing `SagaCompleteFn` allocates another `ZmLambda` closure. Each subsequent step wraps the same completion again. Moving the callback does not avoid allocating its wrapper. | Also at HEAD. Fix exact `SagaCompleteFn` inputs by forwarding the delegate itself; retain the existing adapter for other callable types. Fixed in callback22; owning build and all86 saga tests passed. Matched observed saga allocations are about110 versusHEAD121; saga p50 still regresses12.7–14.2%. |
| Step completion / application action | Converting `SagaStepDriver` to `SagaCompleteFn`, and stateful application lambdas to `SagaFn`, creates heap closures. Scheduler `run` lambdas themselves use variable ring storage in the normal path; they must not all be counted as heap closures. | Also at HEAD. Driver capture includes saga ownership, epoch, step, direction and callback. Removing its allocation requires a concrete ownership design; changing callback ordering is not justified. |
| Definition save / SagaData insert | Native definition is serialized into generic backward scratch, then inserted once into retained `SagaPayload`. Row serialization copies that declared Bytes field into its journal buffer. Large definitions can allocate both scratch fallback and payload storage; small ones use stack/builtin storage. | Old HEAD additionally allocated an I/O owner for standalone definition save. The retained Bytes copy is the explicit repair2 contract. No extra owning native definition copy is introduced by `saveValue`: `toWire` returns the unmapped value by reference. |
| Row replication / commit | Nested row, Record and Msg are composed in one pooled I/O owner. Buffer-cache, replication and store retain or move references to that owner; copying a ZmRef does not copy payload bytes. Growth may relocate existing bytes. Ordinary unbounded Zdb IOStorage detach does not take its size-cap compaction branch. | No separate completed-row relay buffer found in the current producer. Small exact-slot offset scratch has already been changed. |
| Memory-store journal write | Every record builds a Value Tuple and MemRow; Bytes are copied into independently owned store data. Index extraction allocates Tuple storage and copies owning key Values. Update snapshots secondary keys; delete creates a transient MemRow and full-cardinality Tuple with non-key fields void. | Same structure at HEAD. Transient delete-row materialization and lookup-only key Tuples are additional optimization candidates; index keys themselves need stable ownership. They are shared store costs, not saga-specific regression evidence. |
| `Saga::findDel` / journal cleanup | Public const-reference key interfaces force a copy into the by-value lookup. Saga forward deletion explicitly constructs `Key{key}`. Owning string/vector key fields can allocate/copy even for a temporary passed by the caller. | Also at HEAD. A by-value/move interface propagated through saga and table calls could remove temporary-key copies while preserving copies of reused lvalues. Do not confuse this with the already repaired findUpd last-use key moves. |
| Recovery scan | Store results materialize owning Tuple fields, then SagaData is constructed in the page array. Inline SagaPayload moves copy bytes physically; array growth can move them again. Large payload moves transfer heap storage. Decoder then constructs the native saga definition from retained page bytes. | Earlier repair removed the local Tuple and temporary SagaData relay. Page growth remains, as at HEAD; using the known query page size when provisioning scan arrays could avoid repeated inline-payload relocation. Cold recovery does not explain warm saga timings. |

Profile21 (same paired19 executable, new exact-slot libraries, count10000,
rounds7,width32) completed with exit0. Stack-identified DB thread8 sampled1.258s,
store thread6 sampled0.581s; replicate0.158s and commitSend0.083s inclusive.
Profile20 before the slot change sampled1.473s/0.752s and0.206s/0.115s respectively.
These are separate instrumented runs, not exact savings or latency acceptance.
The latest matched uninstrumented pair still regresses9.5–11.7%; no causal claim
or performance pass follows from the profile improvement.

## Direct serialized saga reads — read24

Replication-cache RepBuf_ field adapters use borrowed ZiFB::View access, as HEAD's adapters used canonical accessors. Envelope metadata and postgres/sqlite parameter sizing/binding also read serialized fields directly. No owning Record/Msg relay was found. Row construction on cold cache misses, mutable cache recovery, selected owning Tuple results, memory-store Value ownership and recovery saga definition construction remain ownership/mutation boundaries also present at HEAD. Mapped bitmap access can materialize a bitmap, but HEAD's bitmap transform did so too; this is not newly introduced by ZiFB.

Existing interop read controls now compare identical canonical-produced bytes with native borrowed views and an explicitly synthetic owning Record consumer. read24/README.md records conditions and counter limitations. Borrowed readers report zero tracked allocations; owning materialization costs substantially more. Direct-access differences change sign between the original and corrected-observer builds, so they do not establish a stable accessor regression or explain complete-saga latency. Original observer added one allocation per round; corrected noncapturing observer removes it. ZmVHeap jumbo allocations are outside heap telemetry and must not be inferred absent from a zero count.

### Commit envelope padding follow-up (2026-10-10)

Actual capture29 saga observation found native commit bodies8 bytes larger on the exercised default-shard paths. The general typed-field alignment sort was rejected: it enlarged SagaData and doubled backing allocations for a4KiB fresh-row diagnostic. Generic ZiFB emission is restored. The narrower candidate writes Commit fields in canonical order through saveCommit, preserving field IDs/defaults and all validation/error behavior. Over1111 observed commits it removes7256 body bytes (9.56%); row sizes and growth/relocation counts stay unchanged. Existing171 codec/interop/saga checks pass. Complete-saga timing remains unsettled (-0.89% to+5.71% median versus HEAD, with tail regressions); this is not full performance acceptance. Evidence: zdb/interop/evidence/repair2-20261010/layout35/narrow/README.md.

### Trusted row-access experiment (2026-10-10)

The native cache accessor's empty-vector branch was a source difference from HEAD's direct trusted root access. Removing it changed key-getter code generation but made complete-saga median latency5.26% worse versus the preserved version under matched library preloading. Rejected the experiment; source and release artifacts restored byte-for-byte, with171 checks passing again. This branch is not established as the cause of the saga gap. Evidence: zdb/interop/evidence/repair2-20261010/trusted38/README.md and restore/. Normal-load matched comparison still shows about4.7% pooled median overhead (paired36); full performance acceptance remains open.


### Trusted record and reference-transfer milestone (read51 / paired52)


Read51 built milestone and paired52 (2026-10-10): scoped sequential owning-j4
build13556 terminal0, all3commands0/no diagnostics. Runner97641 terminal0,
all24commands0,171 existing codec/interop/saga checks pass. Six balanced
before/current/HEAD orders: median paired saga current/before p50+1.54%,
p95-3.14%,p99-4.21%; store insert/update/delete p50-2.85%/-2.05%/-2.25%.
Mixed saga timings do not prove a full-saga gain. Compiled loadRow shrinks
1351→1193bytes; both replication closures lose the final-use atomic increment,
with3 rather than4static locked instructions; no heap-elision claim.
Ordinary-wrapper paired52 runner41898 terminal0,12commands0,summary60120
terminal0:180000samples/version/operation. Saga HEAD/current pooled
p50 19.266/19.357us (+.47%),p95 25.978/25.638us (-1.31%),p99
31.128/31.208us (+.26%); median paired-.84/-2.31/-6.71%. All14current
source/artifact hashes match read51/after.json after completion. Retain narrow
read51 source repair; current ordinary pooled timings close to HEAD but prior
tail variability and full gate remain open. Evidence read51/README.md and
paired52/README.md. Broader stores/integration/full-suite/final-memory gates
remain incomplete; HTTP/aggregate builds remain stopped. No new configuration,
validation, exceptions, scheduler posts or callback-order change.


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
