# zquic repair validation status

Status (2026-11-06): execution of `zquic_repair.md` is complete. All requested
subagents finished; source repairs, audit, native Clang debug validation and QIR
execution are recorded here. Core QIR is green under the user's policy that
incomplete third-party suites are green. Supported loss/long-RTT cases pass.
Supplemental rebinding retains confirmed baseline failures; this is not a claim
that all audit findings or all interoperability cases are repaired.

## Final outcomes

| Phase / check | Outcome |
| --- | --- |
| Phase 1: recovery boundary, ACK eligibility/order, RTT threshold | Integrated; permanent regressions pass. Independent pre-repair reproductions fail all three intended defect assertions with setup controls passing. |
| Phase 2: local source audit and requested subagent repairs | Completed; polling/signaling, named heaps, UDP completion and bounded continuation repairs integrated. Remaining policy/lifetime findings are explicitly unrepaired in zquic_repair_audit_inventory.md. |
| Phase 3: selected-module native Clang debug build | Successful, no compiler warnings/errors; shared GNU Make jobserver with eight slots. FIX excluded as instructed. |
| QUIC / HTTP unit / HTTP integration / WebSocket unit | PASS: 21 binaries/186 groups; 26/262; 8/91; 5/15 respectively. |
| HTTP interop | PASS on final rerun: 5 binaries/30 groups, script checks, H2 shell matrix. Earlier intermittent H2 conformance failure reproduces on baseline and remains an accepted exception, not a repaired defect. |
| Native runtime stress | All 40 complete RuntimeTest runs pass after the qlog lifecycle repair; retained TAP plans independently checked. |
| Fresh debug-image HTTP/3 and hq-interop smokes | Both two-file comparisons and graceful server exit code 0 pass. |
| QIR core, four peers, both directions | 22 actual passes; 2 msquic HTTP/3 unsupported cells accepted green by explicit user instruction; no failures. |
| QIR transferloss / longrtt, four peers, both directions | All 16 cells PASS. Sender qlogs record 126/194/94/100 packet-loss events against ngtcp2/quiche/quic-go/msquic respectively. |
| QIR rebind-port / rebind-addr, four peers, both directions | 6 PASS, 10 baseline failures: all 8 server-direction cells expose whole-file send truncation; 2 quiche client-direction cells time out on both baseline and repaired builds. No unexplained new failure. |

Final tested identity: branch `zquic_repair`, HEAD
`c63e7462c243e3f27f6f51d6b4ac33561917a92d` plus `source-r4.patch`, SHA-256
`338c82394e06f1335469c2e929502684ff60123e9e649017173f6e07095fbc0a`.
The captured patch covers all modified C++ files and the module README. It still
matches the tested source. The zquic module delta is committed as `91faeaac`;
the remaining endpoint changes, imported guidance and reports are included in
the follow-up commit containing this update. The identity above records the
pre-commit validation snapshot; no further C++ edits were made.

Configuration: native x86-64 Clang 21.1.8, gnu++2b, `-march=x86-64-v2 -g
-DZDEBUG -DZquic_DEBUG`, prefix `/usr/local`; no release, sanitizer, GCC or cross
validation is claimed. All Z rebuilds and image-stage relinks shared the parent
eight-slot jobserver. The final endpoint and libZquic ELF debug sections and
resolver lifecycle imports were checked in `image-elf-r4/verification.json`.

Final image: `zhttp-qir:repair-r4-clang-debug`,
`sha256:e64a25177205c24ab6e9e8ac9b8a4b7b1d1cebffcf2216863d79d382f24b37c3`.
Pinned runner: `19016767e70789d70416442a1118ad82b5b5fc2e`.
Image IDs for the endpoint, peers and baseline are in
`qir-image-manifest-r4.json`. The baseline Zquic sources remain byte-identical
to `bc710942eefda5cc5572db282c84927abc460976`; its endpoint main receives the
same resolver lifecycle prerequisite so that protocol comparisons are usable.

Canonical retained artifacts:
`/home/ubuntu/zquic-repair-artifacts-20261106/`. The old
`/tmp/zquic-repair-phase3-20261106` path is a compatibility symlink. Relevant
results: `build-r3.log`, `build-r4.log`, `quic-test.log`, `http-test.log`,
`http-itest-test.log`, `ws-test.log`, `interop-test.log`,
`baseline-regressions.log`, `runtime-fixed-summary.json`, `image-smoke-r4.log`,
`qir-repair-r4-core-r2/`, `qir-repair-r4-rebind/`,
`qir-baseline-rebind-quiche/`, and `qir-repair-r4-recovery/`.
Each final QIR directory retains stdout/stderr, per-case outcomes, report,
endpoint logs and available packet/qlog traces. Initial failed/interrupted
runs remain separately identified; they are not mixed into the final results.
The runner registration is restored; no run-owned containers or networks remain.

## Remaining findings and limits

- **Whole-file send truncation, unrepaired baseline defect:** a 10 MiB HQ
  response exceeds finite stream admission; client/server send helpers ignore
  the failed transmit-stream status before FIN. Direct baseline and repaired
  controls both return success with only 6,039,616 bytes. Baseline QIR truncation
  can differ by one buffer. See `hq-large-send-reproduction/results.json` and
  the source audit for a suggested bounded-streaming/error-propagation repair.
- **Quiche client rebinding timeout, unrepaired baseline interop debt:** both
  rebinding cases exceed 60 seconds on the comparable baseline and repaired
  images. Underlying migration cause remains unresolved; logs are indexed in
  `rebinding-baseline-comparison.json`.
- **H2 WINDOW_UPDATE/GOAWAY, accepted baseline exception:** detailed evidence,
  completion-order hypothesis and suggested repair are in `zquic_defect.md`.
  A later pass without an H2 source change confirms intermittency, not repair.
- **Policy/lifetime debt:** anonymous namespaces, named enums and exposed data
  layout remain where listed in the audit inventory; the owner-retaining
  `notifyStreamTxClosed_` callback remains an unproven lifetime concern.
  `EndpointTxQueueLimit` remains diagnostic-only; no admission redesign was made.
- **Accepted unchanged:** external-process polling, six narrow runtime harness
  polls, `allLinks_0` enumeration and cross-thread telemetry snapshots. No
  synchronization or enumeration redesign was applied to these accepted cases.
- **Coverage/tooling:** `zquic/itest` is absent at this base; existing network
  coverage is in RuntimeTest. The owned-stream API test was deleted as directed.
  Public Binance interop is an opt-in skip. The runner has no falling-RTT or
  dedicated reordering scenario; handshakeloss/multiconnect and ecn endpoint
  cases are unsupported, so deterministic phase 3 coverage remains material.
  The aggregate-only QIR report helper remains unchanged; retained core/rebind
  reports were corrected against raw per-case tables and their originals kept.

The sections below retain transfer and chronological execution evidence.
Statements such as “running”, “unstarted”, or “not yet validated” there describe
those earlier checkpoints; the final outcomes above govern current status.

## Checkout and transfer

- Repository: `/home/ubuntu/src/z.zquic`, independent local clone of
  `/home/ubuntu/src/z` (not a linked Git worktree).
- Branch: `zquic_repair`.
- Parent/base: `bc710942eefda5cc5572db282c84927abc460976`.
- Original repair checkout HEAD: `96afee36256c480e1285d598792750ee4eaae082`.
- Transfer commit: the commit containing this report; identify with
  `git log -1 --format=fuller` and `git show --stat HEAD`.
- Transferred the local tracked delta under `zquic` relative to original HEAD,
  plus the untracked `zquic/test/ZquicRttLossTest.cc`. Did not copy the entire
  newer module or unrelated I/O changes elsewhere in the source repository.
- Copied all five `zquic_*.md` documents byte-for-byte. Their original revision,
  line numbers, temporary paths, and prerequisite observations describe the
  original checkout. This report governs the transfer and next steps.
- The local Stream friend additions and owned-stream RuntimeTest additions
  predate the recovery work but were included as requested local zquic changes.
  Do not misattribute them to the recovery agents.

Applied the local patch with Git three-way application. Only RuntimeTest
conflicted: retained its locally added ownership test and calls, kept the older
base's surrounding code, and omitted the newer HEAD's contextual closeClient
helper (not part of the local delta). Verified identical ordered added/removed
lines in all ten tracked files between original and destination local diffs.
Verified new RTT test and all five copied Markdown files byte-for-byte.
`git diff --cached --check` passed before commit. These are source transfer
checks, not C++ validation. Original checkout was not modified by this transfer.

## Phase 1 — implemented source, execution deferred

Two feature subagents implemented disjoint sections concurrently; coordinator
integrated and reviewed them:

- Recovery boundary stores Tx event-processing time, compares the triggering
  packet's send time inclusively, and migrates loss and ECN callers.
- ACK classification captures inFlight before removal and separates ordinary
  byte retirement from eligible growth. PMTUD remains retirement-only.
- Bounded ACK work retires immediately, stages growth until ECN/loss completes,
  and invalidates credits using a separate congestion epoch. Reset generation,
  discarded-space guards, and persistent-congestion invalidation are retained.
- RTT threshold uses max(latest, smoothed), 9/8 multiplier, precision and floor,
  retaining initial fallback.
- Permanent recovery/congestion/API regressions and new registered RTT test
  cover epochs, equality, mixed ACKs, late/duplicate ACKs, PMTUD, same-ACK
  loss/ECN, independent ECN response, continuations, reset/discard, shared
  spaces, exact 99.84375 ms falling RTT and deadline edges.

See `zquic_repair_feature_a.md` and `zquic_repair_status.md` for invariants and
source coverage. No configuration, compilation, compiler checks, tests, or
images were run during phases 1–2 or transfer.

## Phase 2 — audit performed; not a clean pass

Original source audit covered all present C++ directories and included an
independent recovery review. Applied small layout fixes (Diag, LinkDiag,
OpenSSLCipherCtx, PktTxUpdate), removed an unused polling helper, and repaired
regression setup for initialized diagnostics, explicit ECN activation, and timer
teardown. See `zquic_repair_audit_inventory.md` for evidence/dispositions.

The current harness repair replaces 31 of 37 RuntimeTest polling call sites
with completion signaling or direct checks after synchronous engine completion.
Six narrow harness polls remain accepted; external-process polling is explicitly
accepted unchanged. Existing C++ policy findings remain recorded. Existing I/O API mismatch
observations refer to the original newer checkout, not this older base.
Re-audit affected declarations and callers after this transfer before asserting
phase 2 readiness.

Current local compatibility recheck: the user directed deletion of the mistaken
RxRange-dependent owned-stream test and its four calls; they are removed.
The associated missing macro and pendingMigDrop uses disappeared with it.
No current zquic C++ references to RxRange, ZiMxBackend, TxDataDone,
storageMemory_, or releaseView_ remain. The older local endpoint and buffer
APIs do not contain the newer checkout's reported mismatches. This is source
inspection, not a compiler result or a general compatibility certification.

Imported AGENTS.md and all eleven docs/*.md from local main at
96afee36256c480e1285d598792750ee4eaae082, byte-for-byte; no unrelated code
migration was imported. Source audit repairs cover example concrete heaps,
endpoint layout/template-independent declarations, duplicate ECN accounting,
explicit test captures, a redundant payload cast, unused Stream friendships,
and the crypto anonymous namespace. A subsequent source-only harness repair
adds durable fixture completion signaling and removes the unused shared polling
helper/import. The UDP completion repair removes the normal-traffic Tx-drain
link fan-out and obsolete drain hooks; productive full packetization batches
resume with a bounded same-link continuation, with regression sources added
for FIFO/error drain and full/partial/blocked batches. The endpoint queue
threshold remains diagnostic-only, as before. Remaining concerns include
owner-retaining production callbacks and broader layout/language policy debt,
missing itest coverage, and unexecuted validation. The test heap audit adds named
concrete heaps to eight allocated link types and routes two raw FlowStream
allocations through their existing named full-node allocator; framework-created
streams already use named outer node heaps. The allLinks_0 enumeration finding
is accepted as-is by explicit user instruction, separately from ordinary traffic fan-out; the remaining narrow
harness/external polling are also accepted dispositions. Cross-thread telemetry
reads are accepted snapshots and are exempt from this concern. See the current
audit inventory for evidence and limits.

## Phase 3 — resumed, suites and baseline in progress

All three repair subagents completed before configuration. Artifacts are under
`/tmp/zquic-repair-phase3-20261106` (source.patch, configure.log, clean.log,
build.log, build-scoped.log, build-scoped-r2.log and jobs.mk). Source identity is HEAD c63e7462 plus the recorded local
patch and imported guidance files. Clang 21.1.8 targets x86_64-pc-linux-gnu.

Configuration completed successfully using:

```sh
PKG_CONFIG_PATH=/tmp/uring-zpicotls-direct-clang-install/lib/pkgconfig:/tmp/uring-pg-private/lib/pkgconfig ./z.config -c -L -d -Q /usr/local
make -j8 -f /tmp/zquic-repair-phase3-20261106/jobs.mk clean
make -j8 -f /tmp/zquic-repair-phase3-20261106/jobs.mk build
```

The first QUIC compile reported twelve unused-expression warnings from bare
strings passed to ZuCheckRT in SockTest and RttLossTest. These are corrected
to log_ failure actions and will be rebuilt before tests; the initial log is
retained and is not labeled warning-free.

The clean completed. The initial broad build was interrupted after the user
explicitly excluded FIX. The replacement build uses the actual transport
modules/dependencies only: zu, zm, zt, ze, zf, zi, zdbus, ztcp, ztls, zquic,
zhttp and zws. The initial scoping attempt reached automake's nested root all-recursive without
the module override, so it was interrupted too. The corrected parent invokes
root all-recursive directly; its output is build-scoped-r2.log. FIX is excluded by
the replacement or baseline build. jobs.mk invokes recursive $(MAKE) without
a -j override, sharing the parent's eight-slot jobserver. The top-level-only
SUBDIRS override is prevented from overriding module SUBDIRS via MAKEOVERRIDES=. Actual CXXFLAGS are `-march=x86-64-v2 -g -DZDEBUG -DZquic_DEBUG`;
Z_CXXFLAGS selects gnu++2b. No sanitizer/cross flags are present; prefix is
/usr/local. Available dependencies include zpicotls core/OpenSSL/fusion 1.7,
OpenSSL 3.5.5, ck 0.7.2, hwloc 2.13.0, c-ares 1.34.6, SQLite 3.46.1, GTK 3.24.52,
and the existing PostgreSQL 18.6 private development prefix. No dependencies
were rebuilt or installed for this configuration.


The scoped build completed with exit 0. Its only warnings were the twelve
runtime-check diagnostic strings corrected above; no compile errors occurred.
Default-target rebuilds for the affected test directories and complete QUIC,
HTTP unit/integration/interop-local and WebSocket unit/default interop suites
are now running, alongside an isolated baseline build, through the same parent
jobserver. Per-suite build/test logs and validation-jobs.log are in the artifact
directory. The baseline is an independent archive of bc710942 with the identical
RTT regression and old-API recovery/ACK semantic reproductions added; its build
excludes FIX and other unrelated modules. First-run results: HTTP unit 26 binaries/262 groups PASS; HTTP integration
8 binaries/91 groups PASS; WebSocket unit 5 binaries/15 groups PASS. The default
public Binance fixture skipped because its opt-in was unset. Interop-local five
C++ binaries/30 groups PASS, plus QIR script checks; the H2 shell matrix collided
with the concurrently running HTTP integration matrix port, and h2spec also
reported one WINDOW_UPDATE/GOAWAY failure (145/146 passed). It is being rerun
without matrix concurrency, with baseline comparison for the conformance case.

Initial QUIC run: 21 binaries/183 groups, failures in API continuation diagnostics
and missing nested runtime-scope registration in SockTest/RttLossTest. Registered
the nested helpers via ZuTestCall and refreshed the API fixture's intermediate
controller snapshot; production telemetry behavior is unchanged. The remaining
QUIC binaries, including RuntimeTest, StreamTest and CongestionTest, passed on
the initial run. A full QUIC rerun and HTTP interop rerun are now running with
an isolated baseline build under one -k -j8 parent; corrected sources have not
yet been declared passing. Original logs are retained with -r1 suffixes.

Second-run results: corrected API/Sock/RTT checks pass, but RuntimeTest hit
an intermittent SIGSEGV. Ten GDB runs passed; native repetition reproduced it
on run 20, and a further run with process-local core dumps enabled captured
an 80 MB core in /var/lib/apport/coredump. runtime-core-bt.log and
runtime-core-state.log preserve the evidence: final trace close→stopIdle let
the writer unmap the qlog ring while the producer was still in push2 bookkeeping
(configured=0, Engine Stopped, trace already closed). This is a production
logger lifecycle race, not a demonstrated fixture semaphore UAF. The source fix
holds the existing lifecycle lock through close-record admission only; packet
logging stays unchanged. The restart/drain regression covers sibling close,
final close→idle stop and reinitialization. The shared-jobserver rebuild
completed successfully with no warnings/errors (build-r3.log). The rebuilt full
QUIC suite passes: 21 binaries/186 groups, including the extended logger
restart/drain checks. HTTP unit 26 binaries/262 groups and WebSocket unit
5 binaries/15 groups also pass on this snapshot. All forty native RuntimeTest repetitions pass after the logger repair
(runtime-fixed-1.tap through runtime-fixed-40.tap). Rebuilt HTTP integration
passes 8 binaries/91 groups. Serial interop again passes all five C++ binaries/30
groups and script checks, with only the accepted H2 case failing (145/146);
latest residue is logs/zhttp-h2-interop.QJDqVj99 in the artifact directory.
The default Binance interop remains an explicit opt-in skip.

The baseline built without warnings/errors. Its isolated recovery/ACK semantic
reproductions and byte-identical RTT regression fail their intended assertions,
with setup controls passing (baseline-regressions.log). Baseline HTTP interop
also reproduces exactly the repaired branch's single h2spec failure:
WINDOW_UPDATE overflow should send FLOW_CONTROL_ERROR GOAWAY, but closes the
connection; 145/146 cases pass. Serial repaired interop clears all other H2
shell matrix cases. The user accepted this exact unchanged baseline exception
for the QUIC repair. Detailed evidence, source diagnosis, and a suggested
separate H2 repair are recorded in zquic_defect.md; it remains unrepaired.
QIR is not running yet.
A dedicated runner was cloned at 19016767e70789d70416442a1118ad82b5b5fc2e;
Docker 29.1.3 overlayfs and TShark 4.6.4 are available. The runner dependencies
are isolated in qir-venv. Core QIR is now running; see the phase 4 record below.

Phase 3 is complete with the explicit H2 baseline exception and absent
zquic/itest coverage limit. Compiler/configuration, warning-free build, suite
results, forty native runtime repetitions, and baseline regression evidence
have been established as recorded here. Phase 4 is now building a fresh local
debug image through the same eight-slot parent jobserver; no QIR outcome is
claimed yet. No generated Makefiles or build artifacts were copied from the
original checkout.

Tested C++ delta: source-r3.patch SHA-256
705ed924effc7cf455c1fe82cec54ca58d9af84aac3919be7cdaccb18b7955f0,
relative to c63e7462c243e3f27f6f51d6b4ac33561917a92d. Documentation updates
do not change that tested source snapshot.

Resumed execution steps:

1. Read repository guidance and `zquic_repair.md`, then review this report and
   the transferred commit. Recheck dependencies and API compatibility against
   this exact base; resolve necessary prerequisites through phase 2 audit.
   Do not import the entire unrelated I/O migration just to clear failures.
2. Record revision/local patch identity, compiler version, dependencies and
   configuration. Use native x86-64 Clang debug only, with `/usr/local` prefix.
   Inspect `z.config` at this base before choosing required optional switches.
3. Configure via `./z.config -c -L -d -Q /usr/local` with necessary environment
   settings. Verify actual flags/debug defines and absence of sanitizer/cross
   flags. Top-level clean on configuration change, then `make -j8`; require
   warning-free compilation. Retain commands and logs.
4. Build every dependent test directory's default target before `make test`.
   Run complete zquic/test and zquic/itest where present, affected zhttp/zws
   suites, and zhttp/interop endpoint local tests. Recheck directory existence
   at this older base; missing suites are a coverage gap to resolve explicitly.
5. Inspect exact recovery/ACK/RTT assertions and continuation results. If a
   coherent pre-fix snapshot is available, demonstrate regression failures in
   a separate Clang debug build; never revert active fixes or claim blocked
   baseline runs passed.
6. Route necessary source repairs back through phase 2 audit, then rebuild and
   rerun affected checks. Update this report with actual outcomes and remaining
   blockers. Do not label isolated-header checks as runtime-suite coverage.

Representative commands are in the plan. Phase 4 uses a fresh image from the
same audited Clang debug build and runs the documented QIR matrix only after
phase 3 passes. No release/sanitizer/GCC/cross validation matrix is authorized
by the existing plan.


## Phase 4 — fresh Clang debug image and QIR

The default image target initially failed staging because its install list
omits zdbus, which local TLS requires. The retry supplied the existing
ZHTTP_QIR_INSTALL_DIRS override with zdbus/src added. All Z compilation/relinking
used the same eight-slot parent jobserver, existing debug flags and /usr/local
prefix; FIX remained excluded. Libtool emits expected install/relink notices;
these are distinct from compiler warnings. No source was changed for packaging.

The default Debian stable image built, but its launch check failed:
libzpicotls-core.so.1 requires GLIBC_2.42, unavailable in that runtime.
Retained qir-image-build*.log and qir-image-help.log prove both prerequisites.
A local artifact Dockerfile (image-context/Dockerfile) copied the identical
freshly staged /usr/local tree from that image into Ubuntu 26.04 (glibc 2.43),
installed the documented network tools, and ran ldconfig. No reconfiguration or
release build occurred. qir-image-ubuntu-build.log records packaging.

Fresh tested image: zhttp-qir:repair-clang-debug,
sha256:189d30a5f90a3db7a18ea339f4bb9d06423037e2a1a3b5b0eed7764805846b51.
Image labels carry HEAD, source-r3.patch hash and clang-debug designation.
qir-image-manifest.json records the exact endpoint, four peer and simulator IDs.
The HTTP/3 and transfer/hq-interop two-file container smokes pass both byte
comparisons and graceful server exit code 0. image-smoke.log and image-smoke/
retain the commands, certificates, files and endpoint logs; owned containers
and smoke network were removed.

Core handshake, transfer and http3 are running in both directions against
ngtcp2, quiche, quic-go and msquic in qir-repair-core/, with wrapper output in
qir-repair-core.log. Do not infer pass from process start or wrapper status;
per-case results will be inspected. Rebinding and applicable recovery scenarios
remain pending. Actual runner mappings permit transferloss (2% each direction,
burst 3, 15 ms one-way delay, 10 Mbps, queue 25) through endpoint transfer,
and longrtt (750 ms one-way delay, 10 Mbps, queue 25) through handshake.
Handshakeloss maps to unsupported multiconnect; the ECN case presents ecn,
which this endpoint does not support. No unsupported scenario is counted pass.


### Initial QIR diagnosis and endpoint prerequisite repair

The first image's server direction passes initial peer cases, but every tested
client direction aborts after successful transfer with the resolver singleton
still running (ZiResolver.cc:260 destructor assertion, exit 139). Existing
numeric-IP smokes did not start that resolver. An independent pre-repair
hostname smoke reproduces both HQ/H3 client exit failures while all six file
comparisons and server stops pass (baseline-hostname-smoke.log).

The endpoint process main now stops/finalizes ZiResolver after run(role), using
its existing main-thread lifecycle API. HQ/H3 multi-file smokes use localhost
and assert clean client exit; numeric-IP single-file coverage remains. This is
a necessary endpoint validation prerequisite, not a recovery or resolver-library
rewrite. The source audit is in zquic_repair_audit_inventory.md.

The selected-module top-level rebuild (build-r4.log) is warning-free. All five
interop C++ binaries/30 groups, script checks and the H2 shell matrix pass after
this repair (interop-r4.log; interop-test.log for that run). H2 passes this time
without an H2 source change, demonstrating that the previously accepted baseline
failure is intermittent, not repaired; zquic_defect.md records that observation.
The prior full QUIC/HTTP/WS results cover unchanged libraries. C++ source-r4.patch
SHA-256 is 338c82394e06f1335469c2e929502684ff60123e9e649017173f6e07095fbc0a.

A fresh R4 endpoint image is building under a new tag to avoid changing the
initial matrix's image mid-run. The independent baseline image also receives
only the identical endpoint-main resolver lifecycle prerequisite (plus its
existing test-only reproductions), with baseline Z libraries unchanged. This
makes subsequent rebinding comparisons possible without the unrelated process
exit assertion masking their protocol outcomes. Both image builds share the
same eight-slot jobserver; the baseline uses the same Ubuntu 26.04 runtime.


R4 image rebuilt and passed both two-file container smokes with clean server
exit (image-smoke-r4.log). Exact image is
sha256:e64a25177205c24ab6e9e8ac9b8a4b7b1d1cebffcf2216863d79d382f24b37c3,
tag zhttp-qir:repair-r4-clang-debug. New core output is qir-repair-r4-core/;
initial qir-repair-core/ is retained separately and failed for resolver teardown
(11 pass, 11 fail, 2 unsupported). No results from those images are mixed.

Baseline endpoint with the identical lifecycle prerequisite now passes all nine
hostname/numeric-IP smoke groups (baseline-hostname-smoke-fixed.log). All 34
baseline zquic/src files match bc710942 byte-for-byte, verified in
baseline-qir-source-check.json. Comparable baseline image is
sha256:debac6341639fdbd8c7ff7aa291f8457158510011fae48ac4ed58f35700e43f2.

The exact msquic image's /run_endpoint.sh (retained as msquic-run_endpoint.sh)
excludes http3 from its TESTCASE allowlist and exits 127 for that case. Thus its
two HTTP/3 core cells are unsupported peer coverage, not passing cells or a
zquic repair failure. The pinned runner discards detailed logs for unsupported
cases; its per-direction tables retain that classification.


### QIR storage interruption and rerun

The R4 matrix passed all six ngtcp2 core cells, then hit an explicit disk-quota
error copying a server qlog. The wrapper was still live; it was deliberately
terminated via its verified process group, and its owned server/client/sim
containers and labeled networks were removed. Registration restoration was
verified. qir-repair-r4-core/ retains the interruption record and partial output;
its incomplete cells are infrastructure failures, not protocol failures.

All artifacts were moved to /home/ubuntu/zquic-repair-artifacts-20261106 on the
persistent filesystem; /tmp/zquic-repair-phase3-20261106 is now a symlink to it,
preserving earlier recorded paths. TMPDIR requests the persistent root, but inspection subsequently confirmed that
the pinned runner explicitly creates ephemeral directories under /tmp. Retained
logs/results use the persistent root, and moving the build archive freed /tmp
capacity; the completed core rerun had no quota errors. The unchanged R4 image is rerunning the
full core matrix in qir-repair-r4-core-r2/. This storage-only change does not
alter the source, configuration, image or deterministic validation results.


### Core QIR result and accepted peer coverage

The complete R4 core rerun finishes with 22 actual passes, two unsupported
msquic HTTP/3 cells, and no failures (qir-repair-r4-core-r2/). All supported core
cases pass with zquic as client and server. The user explicitly accepts any
incomplete third-party suite as green in this work; the two msquic cells are
therefore accepted green peer limitations and do not block this repair.

The aggregate-only qir-report generator labels every cell from a direction's
exit status, so exit 0 also labels unsupported cells pass. Its generated report
was corrected from the raw per-case tables; report-aggregate-original.md retains
the original, and case-outcomes.json/md record exact classifications. This can
explain an earlier all-green report, but earlier retained results/image IDs were
not found and that historical attribution is unproven. No reporting-script
source change or third-party suite repair is included in this zquic work.
Rebinding is running separately in qir-repair-r4-rebind/; recovery scenarios and
any required baseline failure comparisons remain pending.


### Rebinding diagnosis: baseline whole-file send truncation

The R4 server fails the ngtcp2 rebinding file-integrity checks: the client gets
6,039,616 bytes of a 10,485,760-byte file despite both endpoint processes exiting
0. A direct local HQ reproduction without rebinding produces the exact same
truncation on both the pre-repair baseline and repaired endpoint; see
hq-large-send-reproduction/results.json and the two server/client logs. The
first baseline invocation failed to load libraries after artifact relocation;
results-first.json retains that infrastructure attempt. The valid baseline
reproduction uses explicit baseline library paths through its libtool wrapper.
The original /tmp artifact alias was recreated and verified.

Source diagnosis: SrvLink::send_ and CliLink::send_ write an entire borrowed
payload through a finite stream queue (default 4096 buffers), ignore the
transmit stream's failed status, and then request FIN. The endpoint's HQ server
submits the complete file at once. Core transfer files are 2/3/5 MiB each and
pass; rebinding uses one 10 MiB file and exposes this pre-existing truncation.
It is not evidence of a recovery or UDP-completion regression. No source repair
for that separate admission/error-propagation defect has been made; baseline
QIR comparison is still required for the complete rebinding matrix.


The completed R4 rebinding matrix has 6 passes and 10 failures across 16 cells.
Zquic as client passes both cases against ngtcp2, quic-go and msquic; only
quiche fails that direction. All eight zquic-server cells fail with the same
6,039,616-byte truncation already reproduced on the independent baseline.
qir-repair-r4-rebind/case-outcomes.json/md and the corrected report.md record
per-case outcomes; report-aggregate-original.md retains the generated report.
Earlier README rebinding descriptions are not assumed current. The remaining
quiche client failures are now being compared through the same pinned runner,
peer image and impairment settings against the baseline debug endpoint in
qir-baseline-rebind-quiche/. No QIR source changes follow from these findings.


The quiche baseline QIR comparison completes with the same four failed cells:
both server responses truncate, and both zquic-client rebinding cases exceed
60 seconds. Repaired and baseline timeout logs are indexed in
rebinding-baseline-comparison.json. Baseline server truncation can differ by
one 1472-byte buffer with timing/window state; the local no-rebinding control
reproduced identical byte counts, and both QIR images exhibit the same finite
queue/ignored-failure defect. There is no unexplained new rebinding failure.
The quiche migration timeout mechanism remains unresolved baseline interop debt;
it is not reclassified as an unsupported third-party case.

Recovery QIR is now running in qir-repair-r4-recovery/ with transferloss and
longrtt against all four peers in both directions. Exact scenarios are:
transferloss: drop-rate, 15 ms delay, 10 Mbps, queue 25, loss rate 2% each
way and burst 3 each way, 2 MiB file; longrtt: simple-p2p, 750 ms delay,
10 Mbps, queue 25, handshake with 1 KiB file. This runner has no time-varying
falling-RTT case or dedicated reordering case. Its handshakeloss endpoint name
multiconnect and its ECN endpoint name ecn are unsupported by this endpoint;
those mechanisms retain their deterministic phase 3 coverage where present.
No unsupported case is presented as an executed recovery pass.


## Execution closeout

All 16 supported recovery QIR cells pass on the final R4 image. The complete
qir-repair-r4-recovery/report.md and case-outcomes.json/md are retained.
loss-qlog-evidence.json confirms that zquic as sender recorded actual packet
losses against all four peers, rather than relying only on scenario labels.
The final completion audit verifies source/image identity, unchanged QUIC
sources since the full suites and forty native runs, compiler diagnostics,
complete peer/direction/case coverage, baseline reproductions, exact guidance
import, restored registration, and cleanup. Its machine-readable record is
completion-audit.json in the canonical artifact directory.

All requested subagents are completed. No build/test/image work remains running.
The plan's required execution is complete with the accepted and unrepaired
findings in the final summary; neither an all-clean audit nor a repair of every
baseline interop defect is claimed. Commit state is recorded in the final
identity section above.
