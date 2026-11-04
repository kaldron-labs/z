# Zfix implementation evidence

> Current authority is [zfix_repair2.md](../zfix_repair2.md), with prior API and
> application-owned resend decisions in [zfix_repair.md](../zfix_repair.md).
> Repair 2 implementation and all authorized current verification are complete
> below: unit/schema, native integration, consumers, examples and process gates.
> Earlier records are historical evidence for their captured
> sources, including the published benchmark archive; they are not current
> performance, installed-consumer or final-repair verification.

## Current Repair 2 checkpoint

Final incremental verification replaces three equal test workload literals
with `PayloadBytes = 200<<10`; only that fixture header changed. Its TCP
recovery target rebuilt warning-free and passes all 19 scopes with leak
checking. The other 210 pins are unchanged and the preceding full-suite
snapshot remains preserved. Current evidence:
`/tmp/zfix-repair2-partial-size-verification.json`.

The existing Linux x64 Clang/ASan configuration and dependency artifacts are
retained. Only Zfix targets are built/tested; the separately authorized native
TCP/TLS/ZiTx header repairs are exercised by Zfix fixtures without rebuilding
dependency libraries. No reconfiguration, SDK edit, installation or benchmarks
are part of this verification.

| Gate | Latest evidence |
| --- | --- |
| Library/unit/schema | Warning-free library and unit builds; schemas and 29 TAP programs/347 scopes pass. `/tmp/zfix-repair2-final-{src-build,unit-build,unit-test}.log`. |
| Managed native session | TCP58/TLS64 pass, sanitizer clean. A→B→C retains original Work/context/load while an independent SIDE session exchanges at both binds; actual partial A FIX input and held A cleanup are covered. `/tmp/zfix-association-chain-final-summary.json`. |
| Authentication/terminal overlap | Six TLS failed-socket-only B→C cases pass. Three managed stop-before-late-bind cases pass per transport after Server::connectReady_ chains native readiness and hub running state. These are included in session58/64. |
| Native stale-error policy | Restored TCP10/TLS10 pass; each fault-removal control fails its targeted new scope. `/tmp/zfix-tx-error-audit/`. |
| Logical reset outcomes | 116 focused owner scopes pass all six reset-close/outcome schedules without extra reset state. Included in the current 29/347 unit result. |
| Replay/Logout/archive | TCP18/TLS18 focused snapshot passes application-only repricing/suppression, exact archive/probe and drain checks. `/tmp/zfix-replay-replacement-summary.json` explicitly predates the final Server readiness gate; final current recovery TCP19/TLS18 passes within the integration result below. |
| Cold restore FIFO | Unnecessary WorkBudget/ColdReturn credit removed after actual owner-FIFO proof. Utility build and four reset/recovery lifecycle cases pass, 20 child exits zero, sanitizer clean. `/tmp/zfix-cold-fifo-audit/{PROOF.md,verification.json}`. |
| Managed partial output | Actual managed TCP EAGAIN displacement passes recovery19 with 17 strict partial-output assertions, no build/memory diagnostics. `/tmp/zfix-managed-partial-verification.json`; this fixture is TCP-only. |
| Current consumers/examples | Utility/example/interop builds pass warning-free: `/tmp/zfix-repair2-final-{util,example,interop}-build.log`. Native workload wrapper18 passes (`/tmp/zfix-repair2-acceptance-native-io.log`); standalone TCP/TLS example pairs pass, four actor exits zero (`/tmp/zfix-repair2-acceptance-examples.path`). |
| Final current integration | Warning-free default-all build; TCP6/127, TLS6/132 and demo2 give 13 programs/261 scopes, with 42 demo scenario results passing. `/tmp/zfix-repair2-final-itest-verification.json` records zero failures/diagnostics and 211 unchanged source/build/config hashes. |
| Final current processes | Canonical76, lifecycle24, probes6 and SDK acceptor-policy4 pass, retained under `/tmp/zfix-repair2-acceptance-{canonical,lifecycle,probe,sdk-policy}.path`. Native18 and standalone example2 are above. |
| Completion audit | All explicit repair2 sections 1–8 requirements are verified; no unresolved required gate remains. No installation, benchmark, dependency rebuild or extra platform work was added. |

The expanded session run first exposed an invalid reloadBudget fixture oracle:
only client Tx was held, while a shared close count included the independently
closing server. The test now samples the client's own Rx stop report together
with its charged admin record, retaining exact reservation and final two-peer
close assertions. Original failure: `/tmp/zfix-association-chain-tcp.log`.
The final warning-free build and single current reruns pass TCP58/TLS64 with
ASan leak detection and LSAN exit99 unchanged; configuration hashes match.

The independent requirement-by-requirement completion audit is authoritative in
[REPAIR2_AUDIT.md](REPAIR2_AUDIT.md). The independent QuickFIX shutdown race
remains separate, without SDK repair, suppression or a retry counted as a fix.

## Historical delivery and incremental repair records

The sections below preserve their original snapshots and intermediate failures.
Current status is the checkpoint above; apparent present-tense statements in
older entries do not supersede it.

Pre-repair delivery status: implementation, queue repairs, then-current GCC/Clang,
unit/native/interoperability, installed examples/consumers, memory evidence and
fresh measurement verification pass on Linux x64. The report and dated evidence
archive are published in [bench/RESULTS.md](bench/RESULTS.md) and
[bench/results/2026-10-28.tar.xz](bench/results/2026-10-28.tar.xz). Earlier dated
sections retain intermediate and superseded findings. The repair source-review
record at the end supersedes these earlier delivery gates. ARM and Windows are
out of scope; dependencies were
reused and Zi is unchanged.


Updated 2026-10-28. Stages 1–7 are complete against the revised plan on the
configured Linux toolchain. Stages 8–9 remain open; portability/release evidence
remains a Stage 9 gate. Earlier entries retain their
historical checkpoint findings; the final checklist records current gate status.

## Stage 1 — boundaries and contracts

- [x] Seven role directories, README files and configure/Makefile registration.
- [x] Independent libZfixFmt/libZfix exports, version and header boundaries.
- [x] Scalar, alternate-facet, nested-group, union and typed-catalog declarations.
- [x] Transition tables, mode overlays, errors, hooks, restoration and limits
  documented in CONTRACTS.md before state-machine implementation.
- [x] FIX Session Layer June 2020 and TagValue v1.0 June 2020 downloaded and
  inspected; SHA-256 and conformance/test references recorded in CONTRACTS.md.
- [x] GUIDELINES.md and the now-available TESTING.md read. The revised plan
  removes legacy-source dependencies; the previous missing-source blocker is
  superseded.
- [x] Rebuilt and installed skeleton libraries; standalone and session consumers
  compile against source-tree and installed artifacts.
- [x] Codec links only the foundation through Zf; its consumer has no network
  include paths and checks that session/network headers have not been included.

### Gate evidence

Clang debug, GNU C++2b, Linux x64, prefix /opt/z. Commands run after the revised
plan was read:

- `make -C zfix -j8`: passes.
- `make -C zfix/test test`: 2 TAP programs, 8 assertions pass.
- `make -C zfix/src install DESTDIR=/tmp/zfix-stage`: passes with previously
  staged foundation/transport dependencies (including Linux Zdbus).
- `make -C zfix/test installed-test ZFIX_PREFIX=/tmp/zfix-stage/opt/z`:
  2 separately compiled TAP consumers, 8 assertions pass.
- `readelf -d zfix/src/.libs/libZfixFmt.so`: foundation/system dependencies;
  no Zi, Ztcp, Ztls or libZfix. The consumer also compiles without their paths.
- `git diff --check`: passes.

Reproducible commands are in test/README.md. Session logs:
`/tmp/zfix-resume-stage1.log`; earlier dependency build/install logs remain in
`/tmp/zfix-deps-build.log` and `/tmp/zfix-install.log`. The initial missing UFix
and TESTING.md impediments were recorded during previous work; they no longer
apply to the updated requirements and current files.

The skeleton consumers verify declarations/linkage only. Conversion, framing,
endpoint operations, independent interoperability, benchmarks, release builds
and memory/platform acceptance are separate later gates.

## Stage 2 — metadata/conversion

- [x] Static schema-scoped ZmLHashStatic indexes; compact field-indexed positions;
  no per-message maps or per-tag allocations for scalar/component parsing.
- [x] Flattened components share the enclosing position table, with compile-time
  ID/collision/reserved-tag and length/data metadata checks.
- [x] Checked integer bounds through 128 bits; ordinary empty-string conversion;
  booleans/characters, decimal/fixed/float and built-in FIX timestamp conversion.
- [x] Constructor order, setter paths, selected default assignment, required
  presence, primary-key/mutable update and partial-failure reporting.
- [x] Identified alloc/placement construction without object staging/copies;
  custom scalar ADL, borrowed spans and field/facet time-format overrides.
- [x] Direct-to-buffer body output and checked sticky sink failure/flush.
- [x] Shared length-aware scanner, paired binary output and recognized-pair
  scanning even when fields are not loaded.
- [x] Initial golden/negative/bounds/lifetime and seven compile-fail fixtures.
- [x] Counted/nested/zero repeating groups with configured cumulative/depth and
  parse-record bounds; consistent output delimiters, including optional components.
- [x] Deferred root union views, explicit resolution and resolved output; flattened
  union alternatives selected through compile-time facet configuration.
- [x] Explicit MultipleValue UDT and vector-code policies; owning object pointers
  and CString destination ownership, lifetime and clearing.
- [x] Scalar/null/default/enum/flags/range/filter and malformed-unselected-value
  coverage; golden-wire, negative, bounds, lifetime and schema gate.

### Gate evidence

Clang debug, GNU C++2b, Linux x64; foundation libraries rebuilt before consumers.

- `make -C zu/test -j8` followed by
  `prove ./zu/test/ZuBoxTest ./zu/test/ZuTimeTest`: 197 TAP results pass.
- `make -C zfix/src -j8`, `make -C zfix/test -j8`, then
  `make -C zfix/test test`: five consumers, 35 TAP results pass, plus ten expected
  schema compilation failures and a successful scalar/group/catalog control.
  Schema diagnostics must contain a static-assertion failure; compiler crashes
  and unrelated build errors do not count as schema rejection.
- Reinstalled into `/tmp/zfix-stage`; `make -C zfix/test installed-test
  ZFIX_PREFIX=/tmp/zfix-stage/opt/z`: five separately recompiled consumers using
  installed headers/libraries, 35 TAP results pass.
- `libtool exec valgrind --leak-check=full` with definite/indirect leaks treated
  as errors: codec, group and union consumers report zero errors, zero definite,
  indirect or possible lost bytes. Framework caches remain reachable.
- Source/installed builds are warning-free. `git diff --check` passes.

The time-format override fixture reproduced an existing foundation defect:
`ZuFmt::Frac<9, 3, '0'>` ignored the source width and printed the low three
nanosecond digits. The fixed-width fractional printer now scales before printing;
ZuTimeTest covers FIX output at 0/3/6/9 digits, zero padding and signed/unsigned
fraction values. This is a required foundation correction for Q007, not a second
FIX time formatter. Zfb/Zpb were not modified.

Session logs: `/tmp/zfix-foundation-build.log`,
`/tmp/zfix-foundation-test.log`, `/tmp/zfix-audit-build.log`,
`/tmp/zfix-audit-tests-build.log`, `/tmp/zfix-audit-final-test.log`,
`/tmp/zfix-audit-installed-test.log`, `/tmp/zfix-audit-valgrind-codec.log`,
`/tmp/zfix-audit-valgrind-group.log` and `/tmp/zfix-conv-valgrind-union.log`.

The README specifies borrowed/deferred and raw/owning pointer contracts, CString
C ABI allocation, null formatting and explicit MultipleValue restrictions.
These are standalone body checks; framing/session/transport/independent-peer
acceptance remains separate. ASan/LSan, clean release and portability gates are
still stage 9 deliverables.

## Stage 3 — framing/binary/output

- [x] One envelope validation parser with incomplete/complete/error results,
  leading 8/9/35, bounded BodyLength arithmetic, exact CheckSum boundary,
  preceding SOH and modulo-256 validation over original bytes.
- [x] Retained offset-only progress for append-only/relocated input; known total
  length exposed before retention; no input allocation or delimiter search in data.
- [x] One schema-derived header/body/trailer index and scanner, including nested
  groups, unselected binary fields, SecureData and Signature pairs.
- [x] Typed complete output, header facet precision and explicit raw writer;
  exact-capacity headroom, digit transitions, sticky failures, no successful
  frame view before finalization or after failure. No network publication here.
- [x] Metadata-backed scalar inspection without destination string copies;
  known text control-byte validation, required pair/vector-policy schema checks.
- [x] Literal golden/negative, every-split, coalescing, binary, configured-bound,
  partial-construction and output-failure fixtures; twelve compile-fail cases.

### Gate evidence

Clang 21.1.8 debug, GNU C++2b, Linux x64:

- Source suite: six consumers, 46 TAP results pass and twelve expected schema
  static-assertion failures; scalar/group/vector/catalog positive control passes.
- Installed suite: all six consumers recompiled against staged headers/libraries,
  46 TAP results pass.
- `make -C zfix/test asan-test`: codec, group, union and frame consumers pass
  with codec sources/templates instrumented and leak detection enabled.
  Foundation libraries remain the configured, uninstrumented build.
- libFuzzer/ASan: 2,344,010 cases in 31 seconds, seed 982030348's saved failure
  included as a regression seed in the subsequent successful run. Limits are
  4096-byte input/output, 32 cumulative entries, depth 8, 64 KiB parse records.
  Successful run seed and exact statistics are in the log; peak ASan-process RSS
  was 470 MiB, including sanitizer quarantine/coverage state, not codec retention.
- Headroom/measurement conversion-and-copy comparison recorded in CONTRACTS.md;
  capacity sweep covers 1011 body sizes and eleven capacities per size, plus
  one-byte-short failures and checksum digit padding. Performance budgets remain
  measurement work in stage 9.
- Warning-free source and installed builds; `git diff --check` passes.

Fuzzing found a progress-cache defect: an unterminated MsgType at the declared
body boundary waited for checksum bytes. The cache now re-enters the validation
parser at that boundary, with both a TAP regression and tracked TruncatedType
seed. ASan also identified temporary inline-string backing in test fixtures;
retained inputs now use caller-owned/named or static storage.

Logs: `/tmp/zfix-final-codec-test.log`,
`/tmp/zfix-message-final-installed-test.log`,
`/tmp/zfix-message-final-asan-test.log`, `/tmp/zfix-message-fuzz-fixed.log`.
The earlier smaller fuzz run passed 2,514,688 cases; the complete-message run
above supersedes it. Full clean sanitizer/release/platform checks and transport
pool admission/publication remain later gates. Valgrind follow-up logs use
`/tmp/zfix-final-valgrind-*.log` and are recorded only after process completion.

## Stage 4 — controls and sequencing

- [x] Seven built-in control DTOs with ordinary Z metadata, identified heaps,
  required fields and optional emission facets; no business-message built-ins.
- [x] Control decoding selects exactly one MsgType. Failed body conversion leaves
  the control unresolved and preserves independently decoded header sequence
  context for the session's error disposition.
- [x] Complete control-frame writer, including 789/1137 mode requirements,
  optional Reject fields and exact opaque/empty TestReqID echo.
- [x] Header formatting can use a separate facet from the chosen control body.
- [x] Rx-owned `SeqRx` wraps ZmPQRx and its queue/gap/scheduling facilities;
  byte-bounded read-ahead, duplicate suppression, monotonic gap advancement,
  drain-required reset, hold/resume and independently guarded drain/retry callbacks.
- [x] Tx-owned SeqTx over ZmPQTx, independent publication/replay cursors,
  bounded replay/archive work, admission limits, explicit acknowledgement and
  application-owned retention/retrieval, including deferred completions.
- [x] Rx lifecycle identity, connection/Logon/admission, mode overlays,
  recovery/readiness probes, absolute deadlines, logout and stale-token guards.
- [x] Managed pooled receive coordination, ordered control/error processing,
  timestamp/identity checks, early recovery traffic and same-epoch read-ahead.
- [x] Tx frame/control coordination, actual replay framing, named application
  hook roles and reset/restoration integration in fake owners.
- [x] Complete fake-clock transition/error/recovery engine gate; consolidated
  evidence and later-stage boundary are in test/SESSION.md.

The generic receive sequencer consumes already classified one-sequence items;
protocol-specific low-sequence/PossDup validation, control processing, identity,
connection/epoch admission and negotiated reset remain the session engine's
responsibility. Holding preserves accepted input and progress. Endpoints must
cancel timers and drain posted owner work before destroying the engine.

### Control/queue baseline evidence

Clang 21.1.8 debug, GNU C++2b, Linux x64:

- Source suite: eight programs, 60 TAP results, twelve expected schema failures.
- Installed suite: all eight consumers recompiled against staged headers and
  libraries, 60 TAP results pass.
- ASan/LSan: all six conversion/frame/control/sequencing consumers pass with
  codec/control sources and sequencer templates instrumented. A focused Seq
  rerun covers the drain-required reset correction. Foundation DSOs remain
  uninstrumented.
- Receive-sequencer fixtures cover out-of-order/duplicate input, overlapping
  gaps, exact byte bounds, sequence exhaustion, stale drain/retry callbacks,
  empty-prefix gap fills, callback-triggered holds, reset and 128 deterministic
  permutations of 64-message arrivals with intermittent holds and duplicates.
- Expanded sequencer Valgrind run reports zero errors and no definite, indirect
  or possible lost bytes. Framework caches remain reachable.

Control emission exposed the documented zero-field ZfStructRender case failing
when its variadic field list was empty. The macro now uses `__VA_OPT__` around
both field maps. ZfStructTest covers the empty facet and explicit JSON AsObject
output; 231 TAP results pass. This foundation correction enables Heartbeat and
Logout to omit absent optional fields without a separate codec.

The cancelled-dequeue regression holds a queued receiver, cancels its posted
work, advances beyond all retained frames, then resumes and receives another
frame. Tokened owner tasks and an on-owner empty-queue kick during resume prevent
ZmPQRx's Dequeuing flag from stranding later ingress. Earlier tickets are inert;
the empty kick also permits a zero read-ahead limit for ordinary in-order input.

Tx archival preparation reproduced a foundation defect in bounded
`ZmPQueue::shift(key)`: it removed the first retained node even if a preceding
gap or a partially acknowledged node placed that node beyond the archive
boundary. The bounded path now checks the actual node end before unlinking;
the unbounded shift uses compile-time dispatch without the bound comparison.
ZmPQueueTest's new regression fails on the old code and passes after correction.
ZmPQueueTest, ZmPQRxTest and ZmPQTxTest pass 60 TAP results; the rebuilt Zfix suite
passes its 60 results and schema gate. Installed consumers also pass 60 results
after refreshing staged Zm/Zfix headers and libraries. The queue regression
passes Valgrind with zero errors and zero definite, indirect or possible leaks.
No independent session gate is implied.
Logs: `/tmp/zfix-pq-shift-before.log`, `/tmp/zfix-pq-shift-after.log`,
`/tmp/zfix-pq-dependent-test.log`, `/tmp/zfix-pq-shift-valgrind.log`,
`/tmp/zfix-pq-dependent-installed-test.log`.

Logs: `/tmp/zfix-control-final-test.log`,
`/tmp/zfix-controls-installed-test.log`, `/tmp/zfix-controls-asan-test.log`,
`/tmp/zfix-empty-facet-test.log`, `/tmp/zfix-seq-drain-test.log`,
`/tmp/zfix-seq-drain-installed-test.log`, `/tmp/zfix-seq-asan-test.log`,
`/tmp/zfix-seq-drain-asan-test.log`, `/tmp/zfix-seq-drain-valgrind.log`.

### Tx sequencing progress

`SeqTx` commits an already converted/admitted/associated candidate, not an
application submission. Its fake owner verifies shared control/data sequence
progress, refused admission without cursor advance, preserved publication keys,
independent replay/new publication, exact byte bounds and sequence exhaustion.
Loaded/published/acknowledged restoration keeps the three cursors distinct;
missing restored unsent content stops new publication without inventing a gap.
Application retention is explicit through archive hooks and prefix release.

Replay and archive requests are split into configured windows before calling
ZmPQTx, bounding even all-unavailable million-sequence ranges per owner turn.
Deferred lookups distinguish pending from unavailable; tickets reject duplicate
completion. The unavailable prefix is retained as a scalar range until a gap
fill publishes. Tests cover repeated asynchronous unavailable responses,
completion while stopped, callback-triggered stops and control publication while
replay waits. A blocked new-content lookup does not disable independent replay.

Foundation changes required by this driver:

- `ZmPQTx::txRestore` restores ordered send/ack/archive cursors and pending resend
  while stopped, without clearing retained queue contents. Invalid cursor order,
  overflow and restoration during activity are rejected. The application owner
  drains old scheduled work before restoration.
- Restart now schedules failed send/replay work even if a failing callback
  stopped the sender, and reinstates the corresponding active flag. A direct
  ZmPQTx regression and the Zfix replay-failure fixture cover this case.

Current source evidence: nine Zfix programs, 73 TAP results and twelve schema
rejections; ZmPQueueTest/ZmPQRxTest/ZmPQTxTest pass 62 TAP results. Logs:
`/tmp/zfix-tx-verified-test.log`, `/tmp/zfix-tx-verified-foundation-test.log`.
Tx component and foundation Tx Valgrind runs report zero errors and zero definite,
indirect or possible leaks (`/tmp/zfix-tx-valgrind.log`,
`/tmp/zfix-tx-foundation-valgrind.log`). Installed verification recompiles all
nine consumers against staged Zm/Zfix headers/libraries and passes 73 results
(`/tmp/zfix-tx-installed-test.log`). Focused ASan/LSan runs pass both Rx and Tx
sequencers with their templates and codec/control sources instrumented
(`/tmp/zfix-tx-asan-test.log`); foundation DSOs remain uninstrumented. Builds are
warning-free and `git diff --check` passes.

This does not complete stage 4. Session identity/admission, fake-clock transitions,
mode overlays, actual replay framing, and complete application-hook integration
remain required, followed by stages 5–9.

### Rx lifecycle and header progress — 2026-10-27

Session is now a concrete Rx-owned lifecycle, with an owning identity/configuration
and injected monotonic clocks. Fixed SessionEvents drive future owner adapters;
no live Tx state is read and no borrowed payload is captured by an event. Managed
open/connected/admission/local-Logon-boundary/peer-Logon sequencing prevents
premature readiness. One-shot admission and connection tokens guard delayed work.

The fake-clock fixtures cover initiator/acceptor, duplicate attachment, refused
admission, exact version/identity/sub-identity/location matching, missing or invalid
mode-specific fields, high/low Logon, 789 reconciliation, both-direction recovery,
opaque probes, unrelated numeric-looking Heartbeats, periodic/explicit timeouts,
heartbeat zero, logout/close, reconnect without epoch reset and raw bypass.
Application transmission becomes ready only after both directions recover and an
exact sync probe response arrives. A later gap pauses readiness and invalidates
the older sync probe. Header/body version fields never select a business schema.

Individual deadline cancellation uses ZuTime::null(); braced assignment selected
its integer assignment overload and produced a valid zero timestamp. The clock
fixtures caught this before acceptance. Final close also clears all logical
deadlines; actual scheduler cancellation/drain remains an endpoint responsibility.

The standalone header now includes Sender/TargetSubID, Sender/TargetLocationID
and ApplVerID, backed by the common metadata/scanner/writer. A literal 91-byte
BodyLength/checksum fixture covers all five fields, and two new compile-fail cases
prevent body override of the new reserved header tags. Existing Header aggregate
consumers use named initializers after this intentional API change.

Latest evidence, Clang 21.1.8 debug, GNU C++2b, Linux x64:

- Source suite: ten programs, 85 TAP results, fourteen expected schema failures.
- Installed suite: all ten consumers recompiled against staged headers/libraries,
  85 TAP results pass.
- Focused ASan/LSan: frame, controls and lifecycle consumers pass with their
  sources/templates instrumented; foundation DSOs remain uninstrumented.
- Lifecycle Valgrind: zero errors and zero definite, indirect or possible leaks;
  framework caches remain reachable.
- Header/framing libFuzzer/ASan follow-up: 2,534,153 runs in 31 seconds, seed
  1170640057, 2,151 new units. Peak process RSS 481 MiB includes sanitizer
  quarantine/coverage state. The bounded corpus now has eight tracked seeds,
  including the new sub-identity/application-version frame.
- Warning-free builds and `git diff --check` pass.

Logs: `/tmp/zfix-session-final-test.log`, `/tmp/zfix-session-installed-test.log`,
`/tmp/zfix-session-asan-test.log`, `/tmp/zfix-session-valgrind.log`,
`/tmp/zfix-session-frame-fuzz.log`; corpus path in
`/tmp/zfix-session-fuzz-corpus-path`.

Stage 4 remains incomplete. The lifecycle currently requests drain-coordinated
reset handling through NeedReset rather than mutating epoch/sequence progress.
Complete ingress/control/error coordination with SeqRx/SeqTx, SendingTime checks,
actual replay framing, reset negotiation and full application hooks remain
required before its complete transition gate, followed by stages 5–9.

### Managed receive coordination — 2026-10-27

`Rx<Impl>` now joins Session, ZmPQRx and the common codec using pooled complete
frames. Headers/control values borrow one enclosing buffer reference. Read-ahead
charges backing allocation plus node overhead, not just a small view into a large
coalesced buffer. This conservative per-frame charge needs no ownership hash or
parallel sequence container. Frame/backing/read-ahead bounds apply before retention
or early-control side effects.

Admission backpressure returns a non-consuming Blocked result. The connection
holds that frame and pauses ingress until admission completes; no pre-admission
traffic enters continuing sequence state. The first Logon is separately retained
under its frame/backing cap. Rejected/stale admissions cannot later deliver that
connection's unadmitted data. Previously accepted same-epoch read-ahead survives
reconnect and is processed in order; old Logout traffic cannot close its successor.

Typed and generic header-prefix APIs share the metadata scanner and scoped hash
indexes. The typed path recognizes body/trailer scope and skips legal unknown
header extensions without accepting a known body field before required header
values. The unsupported-type path stops at the first non-header field and never
interprets fake header tags inside unknown binary content. Failed required-field
loads now recover each independently valid header field for diagnostics; duplicate
MsgSeqNum remains ambiguous and is not exposed as a usable sequence.

Receive tests use actual FrameWriter/writeHeader output, the common framer and
control codec. They cover ordered business delivery, body/control errors consumed
once, sequence-keyed Reject context, unknown MsgType, low PossDup suppression,
forward gap fills, reset-mode SequenceReset without epoch change, clock/identity
faults and effective body-codec overrides. Missing/ambiguous MsgSeqNum and unsafe
length/data pairs never advance input. High Heartbeat/ResendRequest
traffic can act early once (TestRequest now waits for ordering; see the
published-probe section below); requests to replay peer history precede requests for
our own gap. High Logout is acknowledged without abandoning recovery and has one
ordered close disposition. Raw input bypasses managed parsing/admission/sequencing.

The callback bridge uses distinct external scheduling names (`rxPost_`, `gapArm_`,
`gapCancel_`) so a missing mandatory hook fails lookup rather than recursively
finding the internal bridge. Empty-queue resume clears stale base dequeue state
on the owner, preserving progress with readAheadBytes zero.

Latest source evidence: eleven programs, 99 TAP results, fourteen expected schema
failures. Installed verification recompiles all eleven consumers and passes 99
results. Focused Frame/Msgs/Session/Rx ASan/LSan checks pass; a subsequent Rx-only
run verifies the finalized scheduling hooks. Instrumented sources/templates use
uninstrumented configured foundation DSOs. Rx Valgrind reports zero errors and
zero definite, indirect or possible leaks; framework caches remain reachable.

The shared-header follow-up fuzz run passes 2,416,090 cases in 31 seconds, seed
1889518555, 2,069 new units and 471 MiB peak ASan-process RSS (including quarantine
and coverage state). Builds are warning-free; `git diff --check` passes.

Logs: `/tmp/zfix-rx-hooks-test.log`, `/tmp/zfix-rx-hooks-installed-test.log`,
`/tmp/zfix-rx-asan-test.log`, `/tmp/zfix-rx-hooks-asan-test.log`,
`/tmp/zfix-rx-valgrind.log`, `/tmp/zfix-rx-frame-fuzz.log`; corpus path in
`/tmp/zfix-rx-fuzz-corpus-path`.

At this checkpoint stage 4 still lacked Tx framing/replay and application
association; the follow-up below records those components. The complete transition
gate, endpoints, independent interop, benchmarks and portability remain pending.

## Tx output and association follow-up (2026-10-27)

Added optional preserving growth to the standalone Sink/FrameWriter. Failure is
sticky; declined growth yields no publishable frame. Growing output reserves the
maximum envelope prefix and returns a subspan without a body compaction. Tests
force relocation on every extension and cover binary bytes, declined growth,
exact/short capacities and a version longer than the pooled builtin buffer.
Control-body facet selection now lives in saveControl, shared by complete-frame
and retained-body output.

ZfixTxFrame supplies immutable pooled MsgType/body storage with opaque application
identity and separate pooled transport frames. Metadata conversion runs once;
output/replay explicitly copies those original bytes once into mutable transport
storage. The frame builder accepts current Tx header state and validates before
publication. Transport mutation and partial-send cursor changes cannot modify
history. Jumbo growth and bounded failures are covered. This is not a measured
copy/latency budget or complete transport integration.

ZfixTx supplies the owner over SeqTx/ZmPQTx. Business and controls share assignment;
load acceptance precedes publication and refused loads do not consume committed
sequence numbers. Pending candidates block overtaking. Stale accepted loads
unwind through an explicit application hook and cannot release their sequence for
reuse until a deferred unload completes. No mutable queue node crosses to the
application owner. Publication samples current UTC, preserves successful original
SendingTime, and retains sequence identity through publication retry and partial
writes. Business/Reject replay uses the original body; other administrative or
unavailable history is gap-filled without advancing the new-send cursor. Explicit
history/archive continuations preserve application storage/retention policy.
Readiness, administrative reserve and control sequence headroom are tested.

Latest source and installed evidence: thirteen programs, 113 TAP results, fourteen
expected schema failures. Final Frame/Msgs/TxFrame/Tx ASan/LSan checks pass. Instrumented sources/templates use the configured,
uninstrumented foundation/transport DSOs. Tx and TxFrame Valgrind report zero errors and
zero definite, indirect or possible leaks, each with 31,904 bytes/61 blocks of reachable
framework caches. Builds are warning-free and git diff --check passes.

Replay refuses an OrigSendingTime later than the current SendingTime, preventing
invalid output after a backward UTC step. A failed attempt retains history and
resumes after the clock recovers. Tx tests vary declared versions independently
of all managed modes and check optional sub/location/application-version headers.
Reject receive metadata now enforces the same reference-sequence, reference-tag
and reason ranges as output. Four negative cases failed before the correction;
omitted optional defaults remain unchanged.

The follow-up fuzzer alternates fixed and growing FrameWriter storage. It passes
2,167,458 cases in 31 seconds, seed 1430921557, with 2,115 new units and 473 MiB peak
ASan-process RSS (including coverage/quarantine). The tracked eight seeds are
unchanged; generated units remain outside the repository.

Logs: `/tmp/zfix-tx-guards-src.log`, `/tmp/zfix-tx-guards-build.log`,
`/tmp/zfix-tx-guards-test.log`, `/tmp/zfix-tx-guards-asan.log`,
`/tmp/zfix-tx-guards-installed.log`, `/tmp/zfix-tx-guards-valgrind.log`,
`/tmp/zfix-tx-body-valgrind.log`, `/tmp/zfix-reject-before.log`,
`/tmp/zfix-tx-growth-fuzz.log`; corpus path in
`/tmp/zfix-growth-fuzz-corpus-path`.

Stage 4 remains incomplete: the Rx/Tx coordinator must finish reconnect/control
publication ordering, completion/hook delivery, selective restoration and the
complete fake-clock transition gate. Coordinated drain/reset, transport queues,
endpoint teardown, typed facade, interop and benchmarks remain later work.

## Reconnect unwind follow-up (2026-10-27)

Added explicit Tx unwind for application-accepted assignments that never reached
transport admission. While stopped, one owner task obtains the highest unsent
item and notifies its original association owner. Deferred confirmations retain
the queue item and prevent selection/resume; only a matching one-shot completion
removes it and releases its assignment/byte charge. This permits reconnect Logon
at the continuing published boundary without silently losing business context.
Application retain/resubmit policy remains outside Zfix.

The common ZmPQueue now offers guarded logical-tail rewind after explicit
removal. It refuses movement through retained data or before the head. SeqTx
cancels only its highest unpublished key, preserving published history,
acknowledgement, archive and replay state. Restored unsent work uses ordinary
history retrieval and completion; unavailable content remains unresolved and
cannot become a gap fill. The Tx owner reports it once and waits for an explicit
retry rather than automatically looping retrieval. An optional fixed-context
unwound notification resumes enclosing coordinator work after all mappings clear.

Source and installed verification pass thirteen programs / 118 TAP results and
fourteen expected schema failures. The three focused foundation queue consumers
pass 63 TAP results. SeqTx/Tx ASan/LSan pass with the configured uninstrumented
foundation/transport DSOs. Tx and foundation PQueue Valgrind checks have zero
errors and zero definite, indirect or possible leaks; each retains the known
31,904-byte/61-block framework caches. Warning checks and git diff --check pass.

Logs: `/tmp/zfix-unwind-final-src.log`, `/tmp/zfix-unwind-final-build.log`,
`/tmp/zfix-unwind-final-test.log`, `/tmp/zfix-unwind-zm-test.log`,
`/tmp/zfix-unwind-asan.log`, `/tmp/zfix-unwind-installed.log`,
`/tmp/zfix-unwind-valgrind.log`, `/tmp/zfix-unwind-zm-valgrind.log`.
Valgrind runs from each consumer directory under libtool to resolve dependency
metadata. Staged installation includes the new foundation header before the
installed Zfix consumers compile.

The full stage 4/5 gates remain open. The follow-up below records the bounded
control path and publication notices. Complete transition/restoration, drain/reset
and endpoint gates remain required; later deliverables are unchanged.

## Managed control/publication follow-up (2026-10-27)

Added ZfixCtl control-body creation and the Tx-owned SessionTx coordinator.
Rx immediately copies variable control fields into immutable pooled bodies;
commands cross owners as retained handles plus fixed lifecycle metadata. The
unsequenced control queue has a separate named byte limit, including selected
commands awaiting load. Controls use the common association and sequence space;
business candidates wait behind queued controls. Stale connection/epoch/change
commands cannot alter a replacement connection.

The transport reserves complete output without exposing bytes, then flushes only
after the Rx publication notice is queued. New fixed notice types describe Logon,
probe, recovery, unwind and drained progress. Rx consumes these on its owner.
Replacement Logon waits for unpublished-association unwind; queued replay waits
for the replacement's first new control admission, including backpressure retry.
Local completion invokes its own hook and cannot acknowledge session progress.
Close-after-output waits for commands, continuations, recovery and admitted
in-flight output; failed command admission cannot falsely signal successful drain.

Probe response matching now requires publication of the current marker. Matching
pre-publication Heartbeats, wrong epoch/serial and duplicate notices cannot make
the session ready or extend the publication deadline. A drained notice asks Rx to
close transport; actual closure still determines Disconnected.

New Ctl tests cover mode-specific Logon/opaque controls, delayed load, reserve
bounds, notice-before-flush ordering, reconnect unwind, Logon before retained
replay and local completion before close. New Managed tests connect actual Rx and
SessionTx actors in both roles with FIFO complete-frame fake transport. They pass
Logon, probes, typed delivery and clean Logout in all managed modes with declared
versions varied independently. A dropped-frame scenario drives actual ResendRequest,
original replay, ordered retained-high delivery and renewed readiness. Hooks assert
Rx/Tx execution domain, and local completions leave acknowledgement unchanged.

The paired Logout tests failed in every managed mode before the correction: Rx
held sequencing and ignored the peer acknowledgement in Logout. Rx now continues
input and controls until Closing, allowing the handshake to complete rather than
masking failure with a transport disconnect. This is fake transport, not TCP/TLS
or independent peer evidence.

Latest configured Clang/debug Linux x64 evidence: fifteen source and installed
programs / 129 TAP results; fourteen expected schema failures. Focused
Session/Rx/Ctl/Managed ASan/LSan checks pass with configured uninstrumented
foundation/transport DSOs. Ctl and Managed Valgrind checks report zero errors and
zero definite, indirect or possible leaks, with the known reachable framework
caches (31,904 bytes / 61 blocks). Builds are warning-free; git diff --check and
trailing-whitespace checks for new files pass.

Logs: `/tmp/zfix-managed-final-src.log`, `/tmp/zfix-managed-final-build.log`,
`/tmp/zfix-managed-final-test.log`, `/tmp/zfix-managed-before.log`,
`/tmp/zfix-managed-asan.log`, `/tmp/zfix-managed-installed.log`,
`/tmp/zfix-managed-valgrind.log`, `/tmp/zfix-ctl-valgrind.log`.

Stage 4/5 remains incomplete: selective restoration, complete hook delivery,
negotiated Tx idle timing and the full transition/reset/drain gate still require
implementation/verification. Real byte transport/endpoint ownership, typed facade,
application tools, external interop, benchmarks and portability remain later gates.

## Selective restoration follow-up (2026-10-27)

Added application-owned SessionState with full SessionId/epoch and independent
presence flags for Rx received/processed/requested-gap and Tx loaded/sent/ackd,
unpublished bytes and pending replay. Snapshots assemble on each owner without
cross-owner live reads. Omitted values remain unchanged; explicitly selected zero
ranges clear staged request/replay metadata. Invalid masks, identity/qualifier,
epoch, cursors, ranges and byte combinations fail before owner state changes.
Restoration is confined to a fresh owner before its first open/attachment, so a
stale startup record cannot roll an existing epoch back after traffic.

Rx uses the underlying queue's logical tail as the received watermark and head
as processed progress. Restoration creates no placeholder payload nodes. It
preserves previous request metadata while stopped, then requests actual missing
content on resume, including previously buffered content absent after restart.
Queue/PQRx gained guarded restore and logical-tail operations; empty payload
queues with known missing ranges now participate in gap recovery. Snapshot gap
metadata excludes stale requests already satisfied by processing/skip progress.

Tx stages a restored replay range through existing bounded ZmPQTx replay and
history lookup, preserving independent new-send cursors. Sparse amendments also
preserve nonzero unpublished byte accounting. Snapshot TxSent denotes the
ZmPQTx transport-admitted send boundary; local completion remains separate. The
record contains metadata, and the application owns original body/timestamp
storage and durability consistency with accepted associations.

A deferred-history integration case reproduced a coordinator bug: a replay
restored before attachment could remain pending through Logon, then finish
without a recovery-complete notice. SessionTx now retains that pending state at
establishment and notifies Rx when it completes, enabling synchronization to
resume. The pre-fix assertion is saved in the logs below.

Latest focused evidence: sixteen source and installed consumers / 136 TAP
results, fourteen expected schema failures, and 64 focused foundation queue TAP
results. Seq/SeqTx/State/Managed ASan/LSan pass; final Ctl/State/Managed checks
include staged unsent accounting and deferred recovery. Configured foundation/
transport DSOs remain uninstrumented. State, Managed and final Ctl Valgrind checks
have zero errors and zero definite, indirect or possible leaks, with the known
31,904-byte / 61-block reachable framework caches. Builds and whitespace checks
are clean. The top-level incremental dependency rebuild completed successfully
through zdf/itest; session 76563 returned exit status zero.

Logs: `/tmp/zfix-state-recovery-src.log`, `/tmp/zfix-state-recovery-build.log`,
`/tmp/zfix-state-recovery-test.log`, `/tmp/zfix-state-final-zm-test.log`,
`/tmp/zfix-state-asan.log`, `/tmp/zfix-state-staged-asan.log`,
`/tmp/zfix-state-recovery-asan.log`, `/tmp/zfix-state-recovery-installed.log`,
`/tmp/zfix-state-valgrind.log`, `/tmp/zfix-state-managed-valgrind.log`,
`/tmp/zfix-state-recovery-valgrind.log`, `/tmp/zfix-state-recovery-before.log`,
`/tmp/zfix-state-top-build.log`.

The complete stage 4/5 gates remain open: full hook delivery, drain/reset and full restart integration are pending;
negotiated Tx idle policy now has owner-test evidence below. Persistent recovery
still requires a file/store restart fixture; byte-exact original-body reload and
stale probe isolation across owner recreation now have unit evidence below. Endpoint restore barriers, real byte transport,
examples/tools, independent interop, benchmarks and portability remain later work.

## Archive-body reload and recreated probes

`TxBody::restore<Entry>` validates an explicitly selected concrete schema without
constructing the native message or invoking output getters, then copies original
bytes into pooled storage. `ObjectView`/`Handler::check` include serialized-field
requiredness recursively through groups and present optional pointer components.
Known scalar values, binary lengths, reserved tags and configured bounds remain
checked. Unknown extensions and original numeric spellings are preserved. Runtime
built-in control selection and explicit control facets retain administrative
replay classification. No new sequence or timestamp is assigned during reload.

A Tx/application-owner test destroys the original owner and all payload references,
restores exclusive progress in the same epoch, reconstructs the archived body
inside the application retrieval hook and replays with original identity/sequence/
OrigSendingTime plus current SendingTime. No new association or new-send progress
is manufactured. This uses in-memory application records, not durable file I/O.

A failed-before regression confirmed that recreating an owner with unchanged
identity and epoch reused its first TestReqID and accepted the old echo. Each
owner now generates a random 128-bit instance nonce using the framework RNG and
includes it in fixed probe event metadata and opaque wire text. Recreated-owner
old echoes cannot satisfy the new probe in the regression. This nonce is a
liveness identifier, not authentication, and does not change durable epochs.

Latest source and installed checks: sixteen consumers / 141 TAP results and
fourteen expected schema failures. Focused TxFrame/Session and final Tx ASan/LSan
checks pass; foundation/transport DSOs remain uninstrumented. TxFrame, Tx and
Session Valgrind checks have zero errors and zero definite, indirect or possible
leaks; the existing framework caches retain 31,904 bytes in 61 reachable blocks.
The focused builds and whitespace checks are clean. Full transport/store restart,
release/platform checks and the independent interop matrix remain open.

Logs: `/tmp/zfix-body-probe-src.log`, `/tmp/zfix-body-probe-build2.log`,
`/tmp/zfix-reconstructed-build3.log`, `/tmp/zfix-body-probe-final-test.log`,
`/tmp/zfix-body-probe-installed.log`, `/tmp/zfix-body-probe-asan.log`,
`/tmp/zfix-reconstructed-asan2.log`, `/tmp/zfix-body-restore-valgrind.log`,
`/tmp/zfix-reconstructed-valgrind.log`, `/tmp/zfix-probe-recreated-valgrind.log`,
`/tmp/zfix-probe-recreated-before.log`, `/tmp/zfix-body-probe-top-build.log`.
The final top-level `make -j8` completed with exit status zero and no warnings
or errors; exec session 28607 is closed.

## Negotiated Tx idle policy

Rx now snapshots negotiated HeartBtInt in fixed `SessionEvent` metadata. Tx owns
the send-idle deadline and dedicated timer hooks, using a monotonic clock and
absolute deadlines. Successful output admission, including replay and gap fills,
moves the logical deadline; the earlier physical callback re-arms there without
per-message scheduler churn. Due callbacks queue an empty Heartbeat through the
shared sequence/association path. Pending commands, association or unpublished
output suppress duplicate periodic work. Zero HeartBtInt disables periodic Tx
Heartbeats. Logout/close/coordinator failure cancels the timer; one-shot tickets
and connection checks suppress duplicate and stale callbacks. The obsolete
Rx-owned `sendIdle_` entry point is removed.

Three Tx coordinator scopes cover negotiated interval, ordinary/replay output,
early callback re-arming, duplicates, output backpressure, reconnect, zero interval
and high-Logout early acknowledgement. A paired-owner scope exchanges periodic
Heartbeats using real codecs/queues, verifies renewed receive liveness and
business-output suppression, then delivers a late callback after Logout. Concrete
ZmScheduler/transport callback cancellation and draining remain stage 6 work.

The full source suite passes sixteen consumers / 145 TAP results plus fourteen
expected schema failures. Focused Ctl/Managed/Session ASan/LSan checks pass, with
configured foundation/transport DSOs uninstrumented. Source and installed checks both pass sixteen consumers / 145 TAP results.
The top-level incremental `make -j8` passes without warnings/errors. Ctl and
Managed Valgrind checks pass with zero errors and zero definite, indirect or
possible leaks; the existing reachable framework caches retain 31,904 bytes in
61 blocks. Whitespace checks are clean.

Logs: `/tmp/zfix-idle-src2.log`, `/tmp/zfix-idle-build3.log`,
`/tmp/zfix-idle-focused2.log`, `/tmp/zfix-idle-test.log`, `/tmp/zfix-idle-asan.log`,
`/tmp/zfix-idle-installed.log`, `/tmp/zfix-idle-top-build.log`,
`/tmp/zfix-idle-ctl-valgrind.log`, `/tmp/zfix-idle-managed-valgrind.log`.

## Published-probe acknowledgement and hook delivery

A failed-before test confirmed that a high TestRequest was echoed before earlier
missing messages recovered. It now waits for ordered disposition, following the
Session Layer June 2020 section 4.8.8 all-other-messages rule. The exact echo still
borrows the enclosing retained input once. Early Heartbeat liveness and
ResendRequest deadlock avoidance remain; a high Heartbeat cannot acknowledge a
probe while recovery is unresolved.

Probe publication notices now supply the actual assigned sequence. Session retains
that one-shot exclusive boundary and emits `Ackd` only for the exact matching
published response with both directions recovered. Mode 4.4A validated Logon 789
also reports explicit peer progress bounded by the local Logon snapshot. Tx
validates against its published cursor and emits optional `ackdTx_` (core) /
`acknowledgedTx_` (coordinator) notifications on the association owner with epoch,
connection and previous/new exclusive cursors. Duplicate/regressive reports do
not repeat notifications; stale owners and impossible progress are refused.
Neither socket completion nor arbitrary numeric-looking TestReqIDs acknowledge
anything. This is session progress, not a business result. Archive availability
and release remain application policy. An owner-local Rx probe entry point allows
application-selected checkpoints without imposing a library retention schedule.

Five new scopes check invalid/duplicate publication, exact/numeric/stale echoes,
unresolved recovery, high-response suppression, 789, progress notification bounds
and stale epochs/connections. The paired-owner test verifies a business send then
probe, and a dropped business message followed by a high TestRequest: replay and
ordered echo precede resynchronization and bounded acknowledgement. The fake owner
fixture now also reports invalid metadata-only commands rather than checking only
commands with body handles.

Full source checks pass sixteen consumers / 150 TAP results and fourteen expected
schema failures. Focused Session/Rx/Tx/Ctl/Managed ASan/LSan pass; configured
foundation/transport DSOs remain uninstrumented. Installed consumers also pass
sixteen programs / 150 TAP results. The incremental top-level `make -j8` passes
without warnings/errors. Managed, Rx and Tx Valgrind checks report zero errors
and zero definite, indirect or possible leaks; existing reachable framework
caches retain 31,904 bytes in 61 blocks. The strengthened metadata-only fake-owner
guard passes its final focused Managed rerun. Whitespace checks are clean. Full hook coverage, coordinated
drain/reset, concrete transport and later-stage deliverables remain incomplete.

Logs: `/tmp/zfix-probe-order-before.log`, `/tmp/zfix-watermark-src3.log`,
`/tmp/zfix-watermark-build3.log`, `/tmp/zfix-watermark-focused3.log`,
`/tmp/zfix-watermark-final-src.log`, `/tmp/zfix-watermark-final-build.log`,
`/tmp/zfix-watermark-final-test.log`, `/tmp/zfix-watermark-asan.log`,
`/tmp/zfix-watermark-installed.log`, `/tmp/zfix-watermark-top-build.log`,
`/tmp/zfix-watermark-managed-build.log`, `/tmp/zfix-watermark-managed-test.log`,
`/tmp/zfix-watermark-managed-valgrind.log`, `/tmp/zfix-watermark-rx-valgrind.log`,
`/tmp/zfix-watermark-tx-valgrind.log`.

## Ticketed drain frontiers

Rx exposes an owner-local drain request that pauses business readiness and posts
a fixed ticket to Tx. Tx holds subsequent business candidates without sequencing
them, resolves earlier association decisions, and waits for committed publication,
local completion, replay/retrieval and unwind. A one-shot `DrainBoundary` notice
captures the exclusive published frontier. Refused asynchronous loads explicitly
check completion because refusal does not create a send task.

Rx requires both recovery directions complete, then a fresh published probe at
or beyond that frontier and its exact response before reporting `Drained`.
Business admission stays held; operational controls continue. Later control
publication can exceed the original frontier, so equality with the moving Tx
cursor is not the drain criterion. New recovery revokes proof and requires fresh
synchronization. Configurable `drainTimeout` uses the contracted 30-second default
and a dedicated absolute Rx deadline. Timeout closes without changing epoch or
sequence state. Reset agreement and execution remain separate and unfinished.

Receive recovery now also compares logical received/processed cursors. A restored
gap with no retained payload nodes cannot silently appear recovered or prove a
drain. Logon admission and directional Tx notices use the same recovery predicate.
Four new scopes cover tickets/frontier validation, old-probe refusal, deadline
and revocation, delayed accepted/refused association, local completion, ongoing
controls and dropped-message replay before drain proof, plus metadata-only
restored receive gaps. Raw bypass and default/override timeout checks are included.

Full source and installed checks pass sixteen programs / 154 TAP results plus
fourteen expected schema failures. Final Session/Rx/Ctl/Managed ASan/LSan passes,
with configured foundation/transport DSOs uninstrumented. Managed/Rx/Ctl Valgrind
checks have zero errors and zero definite, indirect or possible leaks; existing
reachable framework caches retain 31,904 bytes in 61 blocks. The incremental
top-level `make -j8` passes without warnings/errors. Whitespace checks are clean. The initial Rx sanitizer link failed because
the new restore fixture needs State implementation symbols; the direct-source
recipe now includes `ZfixState.cc` and the focused run was restarted.

Logs: `/tmp/zfix-drain-src3.log`, `/tmp/zfix-drain-build3.log`,
`/tmp/zfix-drain-focused2.log`, `/tmp/zfix-drain-final-src.log`,
`/tmp/zfix-drain-final-build.log`, `/tmp/zfix-drain-final-test.log`,
`/tmp/zfix-drain-asan.log`, `/tmp/zfix-drain-asan2.log`,
`/tmp/zfix-drain-installed.log`, `/tmp/zfix-drain-top-build.log`,
`/tmp/zfix-drain-managed-valgrind.log`, `/tmp/zfix-drain-rx-valgrind.log`,
`/tmp/zfix-drain-ctl-valgrind.log`.

## Epoch-safe completion and quiescent Tx turnover

Managed archive completion, acknowledgement and replay entry points now require
the epoch captured with original work; coordinator acknowledgement/replay also
checks connection. This matters for live reset, where the same physical connection
can reuse sequence one. All dependent fixtures use original history/event epochs,
without compatibility overloads. Stale epochs are refused before queue mutation.

Core Tx supplies an explicit old/new epoch turnover primitive, requiring disabled
business readiness, stopped publication and fully acknowledged/released queue
contents with no pending association, replay/retrieval or unwind. It starts new
cursors at one, preserves physical connection and monotone continuation tickets,
and delivers optional old-context/new-epoch/previous-progress notification. Reused,
regressive and wrapped epochs are refused. This core primitive does not execute
a reset wire handshake, retire remaining administrative history or prove physical
I/O quiescence; those coordinator responsibilities remain unfinished.

Receive queue reset now also requires held processing and no logical receive gap,
including a restored gap without retained nodes. The new Tx scope changes epoch,
reuses sequence one and proves old archive/ack/replay callbacks plus a stale send
task cannot affect the new message. It also checks delayed archive release,
unpublished work, pending retrieval and repeated monotone turnover. Coordinator
negative epoch forwarding and receive-reset guards extend existing scopes.

Source and installed checks pass sixteen programs / 155 TAP results plus fourteen
expected schema failures. Focused Seq/Tx/Ctl/Managed ASan/LSan passes; the final
unpublished-item refusal case also passes a focused source/Tx sanitizer rerun.
Configured foundation/transport DSOs remain uninstrumented. Tx/Seq/Ctl Valgrind
checks have zero errors and zero definite, indirect or possible leaks; existing
reachable framework caches retain 31,904 bytes in 61 blocks. The incremental
top-level `make -j8` and whitespace checks pass without warnings/errors.

Logs: `/tmp/zfix-epoch-src.log`, `/tmp/zfix-epoch-build2.log`,
`/tmp/zfix-epoch-focused.log`, `/tmp/zfix-epoch-test.log`,
`/tmp/zfix-epoch-installed.log`, `/tmp/zfix-epoch-asan.log`,
`/tmp/zfix-epoch-final-build.log`, `/tmp/zfix-epoch-final-focused.log`,
`/tmp/zfix-epoch-final-asan.log`, `/tmp/zfix-epoch-top-build.log`,
`/tmp/zfix-epoch-tx-valgrind.log`, `/tmp/zfix-epoch-seq-valgrind.log`,
`/tmp/zfix-epoch-ctl-valgrind.log`.

## Bounded agreed operational-tail retirement

Core Tx now supports asynchronous retirement after explicit reset agreement and
I/O/ingress preparation. It freezes insertion, readiness/resume and new replay/ack
progress, then validates every retained body and contiguous coverage of the
unacknowledged published interval before deletion. Only non-replayable operational
controls qualify; business, Reject or missing unacknowledged history refuses the
operation without partial preflight deletion. Verified acknowledgement can cover
already released history. Existing archive completions can still release nodes.

Scan and retirement phases use the replayBatch budget, yielding through one-shot
owner tasks. Success starts the next epoch at one and reports the true previous
loaded/sent/acknowledged snapshot; no fake acknowledgement/archive callback is
used. Cancellation or detach prevents queued tasks from advancing epoch. A stable
request ID distinguishes cancellation/completion across retries within an equal
connection and epoch; success and failure notifications both carry it. If cancelled
after retirement starts, already retired operational controls may be unavailable
and gap-filled; business data can never be retired by this mechanism.

Two added Tx scopes verify per-turn work bounds, non-mutating preflight, unchanged
old acknowledgement, selection freeze, business/Reject refusal, missing-history
coverage, cancellation/retry IDs, stale tasks, detach and sequence-one reuse.
The Core reset primitive and this retirement mechanism are prerequisites; Rx
preparation, coordinator wiring and live/reconnect reset handshakes remain open.
No stage 4/5 reset gate is claimed by these owner-only tests.

Source and final installed checks pass sixteen programs / 157 TAP results and
fourteen expected schema failures. Initial Tx/SeqTx/Ctl/Managed and final
Tx/Ctl/Managed ASan/LSan pass; configured foundation/transport DSOs remain
uninstrumented. Final Tx and unchanged SeqTx Valgrind checks have zero errors and
zero definite, indirect or possible leaks; existing reachable framework caches
retain 31,904 bytes in 61 blocks. The final incremental top-level `make -j8` and
whitespace checks pass without warnings/errors. The stable-request-ID refinement
is included in the final source/installed/Tx sanitizer and Valgrind evidence.

Logs: `/tmp/zfix-retire-src2.log`, `/tmp/zfix-retire-build3.log`,
`/tmp/zfix-retire-focused2.log`, `/tmp/zfix-retire-test.log`,
`/tmp/zfix-retire-asan.log`, `/tmp/zfix-retire-installed.log`,
`/tmp/zfix-retire-final-src.log`, `/tmp/zfix-retire-final-build.log`,
`/tmp/zfix-retire-final-test.log`, `/tmp/zfix-retire-final-asan.log`,
`/tmp/zfix-retire-final-installed.log`, `/tmp/zfix-retire-final-top-build.log`,
`/tmp/zfix-retire-final-valgrind.log`, `/tmp/zfix-retire-seqtx-valgrind.log`.

## Coordinated reset paths and bounded deferred replay

Agreed reset now connects Rx preparation, Tx operational-tail retirement and
request-keyed new-epoch completion. The application supplies counterparty
agreement, current drain proof and actual ingress/I/O quiescence. Both owner
cursors become one before explicit activation. Optional owner callbacks report
old progress and permit an application-selected snapshot/persistence barrier.
Live activation can use either agreed driver role; reconnect authorization is
consumed by the next physical open and repeats normal admission. All managed
modes exchange 141=Y/34=1, then recover/synchronize before business readiness.
Mode 4.4A validates 789 without replaying a pre-reset range. Ordinary reconnect
continues the same epoch and numbering without 141=Y.

Resetting ingress returns Blocked before stamping an epoch, so a peer reset
frame arriving before local activation survives in the enclosing retained handle.
Preparation waits for existing commands/output and for application release of
acknowledged business nodes. A close before commit cancels preparation; a close
after Tx commit still lets its keyed completion update the closed Rx owner. This
prevents mixed local epochs and emits no premature reset Logon. Reopen is refused
until pending local preparation resolves. No committed epoch rollback is claimed.

Four paired-owner scopes cover all managed modes, both live driver roles,
reconnect reset, ordinary reconnect and close ordering around Tx commit. Session,
Rx and Ctl scopes cover invalid preparation/progress/request IDs, raw bypass,
missing reset flags, early blocked input, stale old work and delayed archive
release. They exchange actual codec frames through fake transport/owner queues.
Concrete TCP/TLS ingress barriers, partial-wire failures, fresh-session reset
admission and complete public hook/API wiring remain open. No stage 4–9 gate is
closed by these tests.

A separate failed-before test exposed deferred replay bodies retained indefinitely
in the sequence queue. Only newly retrieved replay bridge nodes are now removed
after successful publication. Failed publication retains the node for retry;
original resident history retains its application-selected lifetime. Repeated
requests consult history again and can observe newly unavailable data. The new
scope proves at most one transient node beyond resident history, bounded batches,
retry retention/release and unchanged peer acknowledgement.

Full source and installed-library checks pass sixteen consumers / 165 TAP results
and fourteen expected schema failures. The incremental top-level `make -j8`
passes. Final Session/Rx/Tx/SeqTx/Ctl/Managed ASan/LSan checks pass; configured
foundation/transport DSOs remain uninstrumented. Managed/Rx/Ctl/SeqTx Valgrind
checks report zero errors and zero definite, indirect or possible leaks. Existing
reachable framework caches retain 31,904 bytes in 61 blocks. Whitespace checks
pass.

Logs: `/tmp/zfix-reset-race-focused.log`,
`/tmp/zfix-replay-cache-before.log`, `/tmp/zfix-reset-final-src.log`,
`/tmp/zfix-reset-final-build2.log`, `/tmp/zfix-reset-final-test2.log`,
`/tmp/zfix-reset-final-install.log`, `/tmp/zfix-reset-final-installed.log`,
`/tmp/zfix-reset-final-top.log`, `/tmp/zfix-reset-final-asan.log`,
`/tmp/zfix-reset-final-managed-valgrind.log`,
`/tmp/zfix-reset-final-rx-valgrind.log`,
`/tmp/zfix-reset-final-ctl-valgrind.log`,
`/tmp/zfix-reset-final-seqtx-valgrind.log`.

## Approaching-limit callback

Core Tx now calls optional `limitTx_` on first entry into the configured
control-headroom range; SessionTx forwards it as `sequenceLimitTx_`. The captured
context names the epoch/connection and next sequence. This supplies the required
policy notification without introducing a schedule or automatic reset. It occurs
after association commits, or on attachment when restored progress is already
near exhaustion. Pending/refused loads do not notify. Replay and ordinary
reconnect preserve the one-shot epoch guard; successful epoch turnover rearms it.

Business selection stops at the existing reserve boundary while controls can
consume the remaining numbers. Two new scopes check callback context/order,
association refusal/delay, restore, replay/reconnect, epoch rearming,
administrative entry, zero headroom and UINT64_MAX-minus-one arithmetic. The
coordinator test uses the reserved numbers for Heartbeat and Logout. The core
fixture failed before implementation. Its first post-change run exposed a test
setup omission: reattached queues must resume before acknowledgement archive work
can run; the corrected fixture releases old history before requesting reset.

Source and installed checks pass sixteen programs / 167 TAP results plus fourteen
expected source schema failures; both focused Tx/Ctl programs pass 33 TAP results.
Tx/Ctl ASan/LSan passes, with foundation/transport DSOs uninstrumented. Tx/Ctl
Valgrind checks report zero errors and zero definite, indirect or possible leaks;
existing reachable framework caches retain 31,904 bytes in 61 blocks. The
incremental top-level `make -j8` passes. Build diagnostics and whitespace checks
are clean. Stages 4–9 remain open.

Logs: `/tmp/zfix-seqlimit-before.log`, `/tmp/zfix-seqlimit-src.log`,
`/tmp/zfix-seqlimit-build2.log`, `/tmp/zfix-seqlimit-focused2.log`,
`/tmp/zfix-seqlimit-test.log`, `/tmp/zfix-seqlimit-install.log`,
`/tmp/zfix-seqlimit-installed.log`, `/tmp/zfix-seqlimit-asan.log`,
`/tmp/zfix-seqlimit-top.log`, `/tmp/zfix-seqlimit-tx-valgrind.log`,
`/tmp/zfix-seqlimit-ctl-valgrind.log`.

## Explicit fresh-owner reset authorization

Fresh owners can now authorize a reset Logon before their first open using
`Rx::resetOnOpen_(capturedEpoch)`, with a false second argument to cancel before
opening. Rx requires cursors one and no input; raw mode, invalid configuration,
stale epoch and owners with any previous connection refuse it. Armed authorization
also refuses advanced receive restoration. No epoch/cursor is changed or history
retired by authorization. The application chooses and reconciles its fresh epoch.

Tx independently validates an armed opening snapshot before attachment: target
epoch must equal its own, all cursors must be one, and no queued bytes, history,
replay, pending association, unwind or retrieval can exist. Invalid attachment
leaves progress and connection identity unchanged with no publication. This guards
against inconsistent owner restoration and unresolved prior association even when
Rx is fresh. Endpoint handling of that refusal remains transport integration work.

All managed modes and both roles retain normal inbound admission and require
141=Y/34=1. Authorization is consumed by one open attempt; later ordinary
reconnect continues epoch/numbering without reset. Unsolicited reset still reports
NeedReset rather than clearing progress. Existing continuing owners use the drain,
preparation and agreed reset paths, never this fresh-owner operation.

Four added scopes cover cancellation, stale epoch, raw refusal, inbound admission,
missing/unsolicited flags, receive restore ordering, inconsistent Tx progress,
pending association and all-mode wire exchange/business/reconnect. The first build
caught an enum/int initializer-list mismatch in a test; the fixture now uses three
valid independent restored-progress cases. Focused Session/Rx/Ctl/Managed passes
four programs / 62 TAP results. Full source checks pass sixteen programs / 171
TAP results plus fourteen expected schema failures. Installed checks also pass
sixteen programs / 171 TAP results. Final Session/Rx/Ctl/Managed ASan/LSan passes,
with foundation/transport DSOs uninstrumented. The same four Valgrind consumers
report zero errors and zero definite, indirect or possible leaks; existing
reachable framework caches retain 31,904 bytes in 61 blocks. The incremental
top-level `make -j8` passes; build diagnostics and whitespace checks are clean.
No later-stage gate is closed: complete endpoint/auth hooks, physical ingress
barriers, actual TCP/TLS,
typed application API and independent peer/benchmark work remain outstanding.

Logs: `/tmp/zfix-initial-src.log`, `/tmp/zfix-initial-build2.log`,
`/tmp/zfix-initial-focused.log`, `/tmp/zfix-initial-test.log`,
`/tmp/zfix-initial-install.log`, `/tmp/zfix-initial-installed.log`,
`/tmp/zfix-initial-asan.log`, `/tmp/zfix-initial-top.log`,
`/tmp/zfix-initial-session-valgrind.log`, `/tmp/zfix-initial-rx-valgrind.log`,
`/tmp/zfix-initial-ctl-valgrind.log`, `/tmp/zfix-initial-managed-valgrind.log`.

## Application-owned enqueue/dequeue integration

`Hooks<Impl>` now supplies the optional application queue boundary used by
SessionTx. Tx-owned `enqueue_` calls application-bounded enqueue without capturing
wire sequence/header context. Readiness/control ordering gates fetching one
candidate per posted pump turn. Empty queues pause; `queued_` wakes externally
prepared work. The engine supplies no application backlog/store and holds at most
one fetched body across pending association.

Final selection returns the retained body plus captured proposed context and
result to `selectedTx_`. Accepted follows committed association and precedes wire
publication; pending loads wait for final completion. Refused, overloaded,
limited or invalid candidates leave committed progress unchanged, hand policy
back to the application and pause dequeue until an explicit wake. Control work
continues. A late accepted association after close follows existing unload under
its old context before returning the body refused. Direct candidates cannot
bypass an announced application queue.

Five Ctl scopes verify bounded admission, no submission-time sequence assignment,
control priority, one candidate per owner turn, callback-before-wire ordering,
empty wakeups, asynchronous acceptance/refusal/duplicate completion, explicit
retry without gaps, overload/no spin, paused reconnect and late acceptance after
close/unload. Managed live/reconnect reset scopes in all modes now hold an
application-queued business body after drain: epoch-one wire/sequence progress
stays unchanged, and epoch-two readiness dispatches it once under new context.
Both live driver roles are covered.

The first build exposed the implemented pooled-body Hooks header's Zi dependency
in the session Link consumer. Its source include path now includes Zi; the
standalone codec consumer remains independent. Final focused Ctl/Managed passes
two programs / 31 TAP results. Full source checks pass sixteen programs / 176 TAP
results plus fourteen expected schema failures. Installed checks also pass sixteen
programs / 176 TAP results. Ctl/Managed ASan/LSan passes with foundation/transport
DSOs uninstrumented. Ctl/Managed Valgrind checks report zero errors and zero
definite, indirect or possible leaks; existing reachable framework caches retain
31,904 bytes in 61 blocks. Incremental top-level `make -j8` passes. Build diagnostics
and whitespace checks are clean. Complete public hook routing, TCP/TLS endpoints,
typed submission dispatch and later-stage gates remain open.

Logs: `/tmp/zfix-appqueue-src.log`, `/tmp/zfix-appqueue-build5.log`,
`/tmp/zfix-appqueue-focused3.log`, `/tmp/zfix-appqueue-test.log`,
`/tmp/zfix-appqueue-install.log`, `/tmp/zfix-appqueue-installed.log`,
`/tmp/zfix-appqueue-asan.log`, `/tmp/zfix-appqueue-top.log`,
`/tmp/zfix-appqueue-ctl-valgrind.log`, `/tmp/zfix-appqueue-managed-valgrind.log`.

## Peer Reject routing to the association owner

Valid ordered Rx Reject processing now emits optional `peerRejectRx_` with a
fixed parsed `PeerReject` record retaining one const pooled backing and exact
wire view. Text borrows that backing. High input waits for recovery; malformed
controls and low duplicate replay do not notify. Source context names the
incoming Reject, while its reference sequence identifies outbound work within
that epoch. The prescribed built-in field table is unchanged; original wire
remains available for application-specific reference extensions.

The endpoint posts the record in order to `SessionTx::peerReject_`, which validates
captured epoch, source connection, nonzero incoming/reference sequence and backing
before optional `rejectedTx_`. An older physical connection in the same epoch can
have supplied retained input. The application resolves epoch/reference through
its load mapping; no library business registry or resident-history lookup is
required. Unknown/released references still reach application policy. This does
not change sequence/acknowledgement progress or infer a business outcome.

Three added scopes cover ordered high input, retained text/wire lifetime,
malformed/stale input, duplicate suppression, wrong/future/zero contexts,
old-connection provenance and missing library history. The paired-owner test in
all managed modes receives an immediate Reject after interleaved controls and
resolves the independent application ID on the association owner. It replays the
Reject without repeating notification, resets to epoch two, rejects the old
captured report, then reuses the sequence and resolves a new application ID.

Full source checks pass sixteen programs / 179 TAP results plus fourteen expected
schema failures. The final strengthened Rx/Ctl/Managed focused run passes three
programs / 53 TAP results. Installed consumers also pass sixteen programs / 179
TAP results. Rx/Ctl/Managed ASan/LSan passes with foundation/transport DSOs
uninstrumented. The same three Valgrind consumers report zero errors and zero
definite, indirect or possible leaks; existing reachable framework caches retain
31,904 bytes in 61 blocks. Incremental top-level `make -j8` passes. Build diagnostics
and whitespace checks are clean. Concrete endpoint routing/teardown, remaining
hooks, TCP/TLS, typed API and later acceptance gates remain open.

Logs: `/tmp/zfix-peerreject-src.log`, `/tmp/zfix-peerreject-build3.log`,
`/tmp/zfix-peerreject-test.log`, `/tmp/zfix-peerreject-focused3.log`,
`/tmp/zfix-peerreject-install.log`, `/tmp/zfix-peerreject-installed.log`,
`/tmp/zfix-peerreject-asan.log`, `/tmp/zfix-peerreject-top.log`,
`/tmp/zfix-peerreject-rx-valgrind.log`, `/tmp/zfix-peerreject-ctl-valgrind.log`,
`/tmp/zfix-peerreject-managed-valgrind.log`.

## State/session observers and simultaneous recovery coverage

Rx now invokes optional `stateChangeRx_` with the fixed Changed event snapshot
before general event routing. State, actions, instance, epoch, connection and
monotone change sequence can be posted to the application owner without views
or owner reads. Timer refreshes, ignored/stale callbacks and raw bypass do not
invent notifications. Tx invokes optional `sessionTx_` only after administrative
frame admission and publication-notice enqueue, before transport flush. It reports
actual admitted replay gap fills rather than the skipped original controls.
Refused attempts, ordinary business frames and local completion do not notify.

Two added scopes verify callback order, snapshot fields, reconnect/failure/raw
behavior, suppression and retained body/context across replay/backpressure. Both
fail cleanly before observer wiring. The initial expected-red Rx fixture had an
unchecked empty observer list and crashed; the fixture guard was corrected, then
the clean failing-before run was recorded before implementation.

The stage 4 audit also identified that paired-owner recovery only dropped a
message in one direction. A third scope now drops both directions, steps until
both receive gaps are retained and both peers paused, then verifies both replays,
ordered exactly-once dispatch, fresh readiness and later bidirectional traffic
in every managed mode. Source checks pass sixteen programs / 182 TAP results
plus fourteen expected schema failures; focused Rx/Ctl/Managed passes three
programs / 56 TAP results. Installed checks also pass sixteen programs / 182 TAP
results. Rx/Ctl/Managed ASan/LSan passes with foundation/transport DSOs
uninstrumented. The same three Valgrind consumers report zero errors and zero
definite, indirect or possible leaks; existing reachable framework caches retain
31,904 bytes in 61 blocks. The final incremental top-level `make -j8` passes after
regenerating distribution metadata; SESSION.md is present in generated EXTRA_DIST.
Build diagnostics and whitespace checks are clean.

`test/SESSION.md` maps stage 4 mechanisms and all named hook roles to concrete
source/test evidence and records the remaining consolidated transition/error-exit
audit. It is included in test distribution metadata. Stage 4 remains open rather
than inferring its complete gate from aggregate green tests. Stage 5 partial-write
acceptance and all later transport/API/interop/release work also remain open.

Logs: `/tmp/zfix-observe-before2.log`, `/tmp/zfix-observe-src.log`,
`/tmp/zfix-observe-build2.log`, `/tmp/zfix-observe-focused2.log`,
`/tmp/zfix-observe-test.log`, `/tmp/zfix-observe-install.log`,
`/tmp/zfix-observe-installed.log`, `/tmp/zfix-observe-asan.log`,
`/tmp/zfix-observe-final-top.log`, `/tmp/zfix-observe-rx-valgrind.log`,
`/tmp/zfix-observe-ctl-valgrind.log`, `/tmp/zfix-observe-managed-valgrind.log`.

## Consolidated stage 4 transition/error matrix

The lifecycle matrix now exercises transport/output failures in opening,
pending admission, admitted Logon, established and Logout for all managed modes
and both roles. It checks epoch/connection preservation, no fabricated Logout on
transport failure, cancelled timers and stale/duplicate callbacks after reconnect.
The timeout matrix checks early/exact/dead callbacks for connect/Logon, admission,
probe, Logout and drain. The Logon matrix checks low/equal/high continuing
sequences, both roles, all modes and three independent BeginStrings; admission
refusal, unknown reset, 789 and required application version are also covered.
Public fault routing checks both immediate-close and bounded-Logout dispositions
including limit-coded faults. Rx runs eleven actual framed-input fault variants
per mode/role, checking consumption versus unchanged expected sequence, exact
Reject reference/tag/reason, dispatch/echo exclusion, deadlines and bounded
read-ahead. SESSION.md now gives the consolidated per-transition/error mapping.

The matrix corrected two fixture assumptions: synchronization replaces receive
idle with a separate probe deadline, and missing conditional OrigSendingTime is
a required-tag failure (reason 1), while a future original time is reason 10.
Unknown mode rejection was already implemented and is now explicitly covered.
A separate failing-before case exposed managed Session open accepting epoch zero,
inconsistent with Tx/snapshot state. Open now refuses it before connection
mutation, in every mode/role; raw bypass remains unaffected. The production
change is only this nonzero managed-epoch guard.

Full source checks pass sixteen programs / 188 TAP results plus fourteen expected
schema failures. Focused Session/Rx/Managed passes three programs / 56 TAP results.
Installed consumers also pass sixteen programs / 188 TAP results. Final Session/Rx
ASan/LSan passes, with foundation/transport DSOs uninstrumented. The final
incremental top-level `make -j8` passes without build diagnostics. Final Session/Rx
Valgrind checks report zero errors and zero definite, indirect or possible leaks;
existing reachable framework caches retain 31,904 bytes in 61 blocks. Whitespace
checks include the new untracked module files and pass. The consolidated mapping
and final evidence complete the stage 4 fake-owner engine gate. Stage 5 remains
open for its partial-write acceptance requirement; stages 6–9 remain open.

Connection/probe/drain saturation guards were inspected before increment; no
runtime 2^64-iteration claim is made. Configured sequence exhaustion, including
UINT64_MAX-minus-one arithmetic, and limit-coded public fault routing have actual
execution evidence. This gate is the plan's fake-owner engine gate, not concrete
transport, independent interop or full release/platform memory acceptance.

Logs: `/tmp/zfix-matrix-config-before.log`, `/tmp/zfix-matrix-final-src.log`,
`/tmp/zfix-matrix-final-build2.log`, `/tmp/zfix-matrix-final-focused.log`,
`/tmp/zfix-matrix-final-test2.log`, `/tmp/zfix-matrix-final-install.log`,
`/tmp/zfix-matrix-final-installed.log`, `/tmp/zfix-matrix-final-asan.log`,
`/tmp/zfix-matrix-final-top.log`,
`/tmp/zfix-matrix-final-session-valgrind.log`,
`/tmp/zfix-matrix-final-rx-valgrind.log`.

## Partial byte-stream writes and uncertain completion

The fake transport now supports a held complete frame, byte-prefix writes into
Rx-owned pooled storage, incremental envelope scanning, zero-progress callbacks,
transport backpressure and a bounded identified queue of additional admitted
frames. Frame completion is one-shot under a separate monotone transport ticket;
replay can reuse every protocol context field and therefore cannot use only that
context as completion identity. Processed fake work moves retained handles out
of its trace rather than pinning old transport frames. Application archive data
is stored explicitly by the fixture's application owner.

Four new Managed scopes cover every split of a representative frame in all modes,
header/time/association stability, queued controls, exactly-once local completion,
partial failure, full delivery with failed deferred completion, three admitted
frames on success/failure, reconnect replay and duplicate suppression. Queued
admitted frames retain original numbering even when they wrote zero physical
bytes. Later accepted but unpublished work instead unloads and requeues under
application policy before reconnect Logon; the fixture invalidates its old mapping
before reuse. Its previous no-op unload policy did not provide that requested
retention and was corrected when the new case exposed it. Repeated replay with
identical context is checked against stale completion tickets. Peer/session
acknowledgement never follows merely from a partial write or local completion.

Rx exposes `outputFailed_(connection, capturedEpoch)` for an ordered Tx fault post.
It closes current output failure without allowing old-epoch/connection completions
to close new readiness; Rx resetPreparation tests those guards. The fake fault
bridge now drives lifecycle closure rather than only counting an error. The only
production addition in this checkpoint is that owner-local guarded entry point.

Ztcp/ZiTx source inspection confirms their offset/size handling, and identifies
that cleaning later queued buffers after a failed head does not provide individual
sent callbacks for each. A concrete FIX endpoint must drain its own admitted-frame
completion accounting; that integration remains stage 6 work. No production byte
queue or transport stack is added by these fixtures.

Full source and installed checks pass sixteen programs / 192 TAP results plus
fourteen expected source schema failures. Final Rx/Managed ASan/LSan passes, with
foundation/transport DSOs uninstrumented. The final incremental top-level
`make -j8` passes. Rx/Managed Valgrind checks report zero errors and zero definite,
indirect or possible leaks; existing reachable framework caches retain 31,904
bytes in 61 blocks. Build diagnostics and whitespace checks are clean. Stage 5's
partial-write evidence is now present; its final association-ordering audit,
including explicit fake
third-owner continuations, remains open before that gate closes. Stages 6–9 and
the full objective remain open.

Logs: `/tmp/zfix-partial-src.log`, `/tmp/zfix-partial-final-build.log`,
`/tmp/zfix-partial-final-test.log`, `/tmp/zfix-partial-final-install.log`,
`/tmp/zfix-partial-final-installed.log`, `/tmp/zfix-partial-final-asan.log`,
`/tmp/zfix-partial-final-top.log`, `/tmp/zfix-partial-final-rx-valgrind.log`,
`/tmp/zfix-partial-final-managed-valgrind.log`.

## Separate association owner and stage 5 acceptance

The fake engine now has an explicit third owner for association decisions,
mapping, selection disposition, unload and peer Reject correlation. Tx posts
retained immutable bodies/reports with fixed original context/tickets; application
completions return to Tx. Application state access is asserted on its owner.
No locks, off-owner map mutation or Zfix-owned business registry are introduced.
Three added Managed scopes cover all modes, both physical roles for direct load,
queued submissions, delayed/refused decisions, interleaved Heartbeat, no wire gap,
final-selection-before-immediate-Reject and original native ID correlation.
Deferred load acceptance after close is tested with reopening both before and
after completion: Tx waits, old-context unload clears the app map, then body
requeue/new-connection assignment resumes. Duplicate old load/unload completions
are inert. Header snapshots check a changed UTC clock at publication/reconnect.

The consolidated stage 5 mapping in test/SESSION.md now records all assignment,
replay, partial-write, reconnect, Reject-owner, drain/reset, stale-context and limit
requirements. Combined with the earlier byte-stream cases, this completes the
plan's fake-owner transmission gate. It does not establish actual concurrent
scheduler, socket/TLS, transport teardown, typed endpoint API or independent peer
behavior. Those remain stages 6–9. This checkpoint changes test fixtures and
documentation only; installed header identity was checked before recompiling
installed consumers, and no production forwarding/API shim was added.

Full source and installed checks pass sixteen programs / 195 TAP results plus
fourteen expected source schema failures. Final Ctl/Managed focused checks pass
two programs / 42 TAP results. Managed ASan/LSan passes with foundation/transport
DSOs uninstrumented. Managed Valgrind reports zero errors and zero definite,
indirect or possible leaks; existing reachable framework caches retain 31,904
bytes in 61 blocks. The incremental top-level `make -j8` passes without diagnostics.
Whitespace checks include the new untracked module files and pass.

Logs: `/tmp/zfix-owner-final-build.log`, `/tmp/zfix-owner-final-focused.log`,
`/tmp/zfix-owner-final-test.log`, `/tmp/zfix-owner-installed.log`,
`/tmp/zfix-owner-asan.log`, `/tmp/zfix-owner-valgrind.log`, `/tmp/zfix-owner-top.log`.

## Stage 6 progress — shared receive adapter and real transports

`ZfixRxFrame.hh` supplies the Rx-owned `RxFrame` adapter shared by Ztcp and
Ztls streams. It learns the BodyLength boundary using the existing incremental
framer, then detaches one complete pooled frame with `ZiRxStream::extract`.
A complete head buffer transfers without copying; coalesced input copies the
smaller side, and fragmented input gathers directly into its final pooled
buffer. Only incomplete leading envelope fields require temporary pooled
storage. Frame errors are sticky until physical-stream reset. The caller keeps
an extracted frame across admission pauses before requesting another frame.
The adapter adds no transport dependency to libZfixFmt.

`ZfixRxFrameTest` covers every split and byte-at-a-time input, mixed fragmented
and coalesced binary frames, exact-buffer detachment, retained backing lifetime,
allocation refusal, malformed fragmented headers/checksums, configured bounds
and disconnect cleanup. The installed consumer and focused ASan/LSan target
include this new test.

`itest/ZfixTransportTest.cc` is compiled over both Ztcp and Ztls. Each executable
covers one connection, three simultaneous connections, and two connections
carrying binary frames with 32,769-byte payloads across TLS record boundaries.
A completed frame is split across transport writes, with a second frame queued
behind it; exact pooled receive bytes are echoed after a retained-handle Rx-to-Tx
handoff. Both roles assert Rx dispatch, explicitly check stream flush failures,
and drain physical connection closure before finalizing their transport hubs.
The tests use isolated network Rx/Tx and application Rx/Tx shards and ephemeral
IPv4 loopback ports. The TLS runner generates disposable localhost credentials
and enables certificate/hostname verification using its explicit test trust root.

The first expanded TLS run exposed a fixture teardown race: outbound links were
still disconnecting when the client TLS context was finalized. The fixture now
waits for both physical close callbacks before hub stop/finalization. No transport
library change was required. Thirty consecutive runs of each transport after
this correction pass (180 integration scopes), including the larger binary frames.

Validation (Clang debug/GNU C++2b, Linux x64):

- Source unit suite: 17 programs, 202 TAP scopes; 14 expected schema failures.
- Installed unit suite: 17 programs, 202 TAP scopes.
- Real TCP/TLS: two programs, six TAP scopes; repeated 30 times each.
- Focused RxFrame and real TCP/TLS fixture ASan/LSan: pass; dependency DSOs
  are uninstrumented.
- RxFrame and real TLS fixture Valgrind: zero errors and zero
  definite/indirect/possible leaks. The TLS process retains 32,472 bytes in 63
  reachable framework-cache blocks at exit.
- Top-level incremental `make -j8`: passes without warnings/errors. All 18
  installed Zfix headers match their source files.

This is partial stage 6 evidence. The fixture composes framing with the existing
transport links; concrete managed Zfix Client/Server endpoints remain forward
declarations. Managed authentication/admission, reconnect/failover, store-backed
restart, queued-output completion/failure, ingress/output limits, timers and
pending-work teardown still need production composition and real-I/O acceptance
coverage. Stages 6–9 remain open.

## Stage 6 progress — output admission and record completion

`ZfixWire.hh/.cc` supplies `WireTx`, `WireBuf` and the tunable `WireBufAlloc`
transport allocator. Complete frames are retained during admission; only the
flush following the Rx publication notice exposes their bytes. TCP wraps the
pooled frame backing in a queue-compatible node without copying the frame. TLS
copies into record buffers using negotiated headroom/tailroom and the transport
record bound, preserving its existing allocator tuning. All flushed frames can
queue immediately on ZiTx; no per-frame completion round trip delays publication.

Admission accounts for retained frame storage, frame queue nodes, and a
conservative bound on all record allocations. Administrative reserve is separate
from the ordinary output cap. The adapter retains sequence/context, not a second
business correlation or history registry. Unique monotone transport tokens and
ordered record ordinals distinguish repeated replays of an identical FIX
sequence. Completion is posted exactly once per admitted frame. A write failure
or physical close also completes frames whose records were discarded without
individual socket callbacks. Old/duplicate completions cannot complete a later
frame. Finalization follows the physical-I/O drain and then drains the adapter's
posted completion work before releasing owners.

`ZfixWireTest` tests TCP backing identity, TLS-style fragmented records,
publication gating, FIFO completion, replay tokens, asynchronous/synchronous
failure, close, stale callbacks, malformed completion order, admission bounds and
administrative reserve. Its production SessionTx/WireTx composition additionally
checks that admission precedes notice, notice precedes bytes, completion runs
after core publication, failed transport writes do not rewind published
sequences, and released capacity publishes a waiting body under its existing
loaded sequence. Local completion leaves the peer acknowledgement cursor alone.

The real TCP/TLS fixture now uses this adapter and the custom transport allocator
in both roles. It queues two frames, echoes exact binary payloads across owner
posts, and checks four one-shot local frame completions per connection (two in
each direction). Its explicit close barrier also drains the completion posts.
Validation (Clang debug/GNU C++2b, Linux x64):

- Source and installed unit suites: 18 programs/211 TAP scopes each; source
  schema checks also reject all 14 expected failures.
- TCP/TLS fixture: six scopes, with 30 consecutive runs of each transport.
- WireTx/SessionTx unit composition and real transport fixture ASan/LSan: pass;
  configured dependency DSOs remain uninstrumented.
- WireTx unit and real TLS fixture Valgrind: zero errors and zero
  definite/indirect/possible leaks; reachable framework caches remain at exit.
- Top-level incremental `make -j8`: passes without warnings/errors.
- All 19 installed Zfix headers match source.

Concrete managed Client/Server endpoints and their complete stage 6 acceptance
matrix remain open. The next composition must post SessionTx completion and
`writable_` on Tx, resume paused coalesced Rx input after admission, map absolute
owner timers, and wait for deferred association/unload continuations at teardown.

## Stage 6 progress — concrete managed TCP/TLS endpoints

`ZfixClient.hh` and `ZfixServer.hh` now define the CRTP hubs and thin
initiator/acceptor links, replacing their forward declarations. A compile-time
`TCP`/`TLS` binding delegates connections, listeners, certificates, verification
and mTLS to Ztcp/Ztls. Configurable transport allocators retain pool tuning and
require the output node to carry WireBuf completion metadata. `ZfixLink.hh/.cc`
composes the existing Rx, SessionTx, RxFrame and WireTx owners. The shared bridge
forwards native business association/storage hooks, optional Logon preparation
and admission decisions without adding a business registry or authentication
scheme. [ENDPOINTS.md](ENDPOINTS.md) documents the current API and contracts.

The bridge posts fixed lifecycle events and retained control bodies between
owners; Rx publication notices precede frame exposure. It owns all six session
timers plus a gap retry timer on Rx and idle heartbeat timer on Tx. Every timer
uses the scheduler's absolute clock, cancellation and stale callback guards.
Later receive deadlines reuse the earlier physical wakeup. Pending admission
retains one complete frame; its continuation retries input and resumes retained
coalesced transport bytes. Ztcp/Ztls expose a small Rx-only resume entry point
for that purpose. RxFrame can return validated views rebased over extracted
backing without repeating the checksum traversal.

Physical close cancels Rx timers, posts ordered session close to Tx and drains
WireTx after the underlying I/O queue. Tx stop waits for pending load/unload and
history work plus frame completions. Pending admission has a separate Rx drain
guard. Application stop hooks cancel/complete the original one-shot tickets;
owner barriers then deliver fixStoppedRx_. A single teardown reference retains
the link across the phases; buffer/task back-pointers remain raw. Applications
keep hubs and the multiplex alive until managed stop completes. Owner-only
snapshot/restore entry points expose the existing state mechanism for native
application stores.

`ZfixSessionIOTest.cc` now uses only production Zfix hubs/links plus explicit
application policy. Its nine scopes per transport cover:

- Normal Logon/synchronization, native request/reply, sequence mapping and
  graceful Logout in modes 4, 4.4A and T1.1.
- Admission held until a second control frame is actually blocked, then resumed
  from a third application owner, including the coalesced-input wakeup.
- Real periodic idle-heartbeat timers.
- Inbound admission refusal and outbound Logon preparation refusal.
- Forced close during pending admission or association, cancellation using the
  matching ticket, and stopped callbacks only after the continuation drains.
- Owner-local Rx/Tx snapshots during teardown.

Validation (Clang debug/GNU C++2b, Linux x64):

- Source and installed unit consumers: 18 programs/211 TAP scopes each, with
  all 14 source schema-negative cases rejected as expected.
- Source integration: four programs/24 TAP scopes, including 18 managed scopes.
- Installed managed TCP/TLS consumers: two programs/18 scopes, compiled using
  only staged installed headers and libraries.
- Five repeated final runs of each managed transport: 90 scopes pass. An
  earlier eight-case version also passed ten runs per transport.
- RxFrame validated-view path and all transport/managed fixtures pass ASan/LSan;
  dependency DSOs are uninstrumented in these focused builds.
- Managed TLS Valgrind: zero errors and zero definite/indirect/possible leaks;
  32,472 bytes/63 blocks remain reachable in framework caches.
- Ztcp loop/stream regression: five scopes; Ztls buffer/lifecycle regression:
  16 scopes, all passing.
- Final top-level `make -j8`: passes without warnings/errors. All 21 installed
  Zfix headers match source.

Concrete endpoints are implemented, but the complete stage 6 gate is still open.
Remaining coverage/features include raw-mode endpoint composition, managed
multiple sessions/listeners and duplicate attachment, reconnect/failover and
persisted restart recovery, live reset over real I/O, broad overload/posted-ingress
accounting, metrics and bounded redacted tracing. Typed catalog dispatch remains
stage 7 work; the current native business hook performs explicit typed decoding.

## Stage 6 progress — continuing reconnect and file-backed restart

`ZfixRecoveryIOTest.cc` adds production TCP/TLS recovery fixtures in all three
managed modes. One complete transport-admitted business frame is fault-injected
as locally accepted but not processed by the peer. Continuing reconnect keeps
the same session owners and counters. Restart stops the connection/hubs/mux,
saves both application stores through ZiFile, destroys the stores/body handles,
reloads new stores from files and restores new Rx/Tx owners before physical open.
The fixture owns its native file format; Zfix gains no journal/backend or commit
policy. No disk I/O runs on an I/O shard. History retrieval is deliberately
asynchronous through a third application owner.

The application history receives exact body handles, assigned context and
original SendingTime through the new optional `publishedTx_`/`fixPublishedTx_`
observer. Admission and publication notices precede this callback; bytes follow
it. Refused admission does not notify it. The default consumes the moved handle
without an extra reference copy. This exposes metadata needed for unacknowledged
history without parsing transport records or relying on later archive callbacks.

Repeated restart testing exposed an actual replay wakeup bug. ZmPQTx::start can
call idleResend when resend is already active and no fresh task was scheduled.
A local-send completion during a pending history lookup could therefore clear
SeqTx's replay intent. Once a scheduled wake was consumed while still waiting,
lookup completion left a cached record and active gap with no remaining task.
SeqTx's idle callback now preserves a nonempty active gap. `deferredWake` fails
before this fix and passes afterward, exercising that exact scheduling order.

All six recovery scopes per transport assert same epoch/no reset Logon,
continuing assigned sequences, restored cursors, exact original replay body/ID
and timestamp, one application receipt of the previously lost request, and fresh
traffic after recovery. Restart reloads through application history rather than
surviving library queue nodes. The recovery case moves to a fresh listener with
an ephemeral port; general failover policy and duplicate-attachment coverage
remain separate stage 6 work.

Validation: source and installed unit suites each pass 18 programs/212 TAP
scopes, with all 14 expected source schema failures rejected. Six integration
programs/36 scopes and both installed recovery consumers pass; 20 repeated final runs of each recovery transport (240
scopes); focused SeqTx/Ctl and recovery ASan/LSan; and recovery TLS Valgrind with
zero errors or definite/indirect/possible leaks. Dependency DSOs remain
uninstrumented in the focused ASan builds. Top-level `make -j8` passes without
warnings/errors and all 21 installed headers match source. Full stage 6 remains
open for the other endpoint modes/lifecycle/limits/reset/telemetry requirements.

## Stage 6 progress — externally managed raw TCP/TLS

`ZfixRaw.hh/.cc` adds a separate `Raw<Impl>` engine selected by the
RawClient/RawServer and RawCliLink/RawSrvLink aliases. Shared endpoint roles and
transport adapters take the engine at compile time. Raw links instantiate no
managed Session, sequence queue or FIX timer. The external manager supplies all
header, control, admission, sequencing and recovery policy; transport generation
checks and configured pooled input/output bounds still apply.

Raw body encode/restore allows application schemas for built-in control MsgTypes
and explicit administrative classification. RawOutput owns a supplied-header
complete frame or retains validated standalone output. Wire admission accepts
external zero epoch/sequence metadata only through the raw path, preserving the
managed checks. Output admission precedes publication/flush and every admitted
frame receives one posted local completion. One paused RawInput retains backing
across an external resume and wakes coalesced transport input. Physical close,
output completions and asynchronous manager Rx/Tx drains precede the final owner
barriers and stopped callback. Exact factory/callback/immutability contracts are
in ENDPOINTS.md.

Validation (Clang debug/GNU C++2b, Linux x64):

- Source and installed unit suites: 19 programs/217 TAP scopes each; all 14
  expected source schema-negative cases rejected.
- Source integration: eight TCP/TLS programs/44 scopes, including eight new raw
  scopes. External Logon schemas omit managed required fields; supplied version,
  identities, old timestamps and nonmonotonic/repeated sequences pass unchanged.
  Tests also cover retained paused input, a third application owner, zero quota
  without publication/completion, multi-record binary data and async owner stop.
- Installed raw, managed and recovery consumers: six programs/38 scopes, compiled
  against staged installed headers/libraries (recovery retains only its test
  residue helper from the build tree).
- Twenty repeated raw runs per transport: 160 scopes pass.
- Focused Raw/Wire/TxFrame/SeqTx/Ctl and both raw transports pass ASan/LSan.
  Dependency DSOs remain uninstrumented in these focused builds.
- Raw TLS Valgrind: zero errors and zero definite/indirect/possible leaks;
  framework caches retain 32,472 bytes in 63 reachable blocks.

Top-level `make -j8` passes without warnings/errors; all 22 installed
Zfix headers match source. `git diff --check` passes.

Logs: `/tmp/zfix-raw-{unit,io,installed-unit,installed-io,unit-asan,io-asan}.log`,
`/tmp/zfix-raw-repeat.log` and `/tmp/zfix-raw-valgrind.log`.
Stage 6 remains open for managed multiple sessions/listeners, duplicate
attachment, broader failover/overload/posted-ingress cases, live reset and
counters/bounded redacted traces. Typed catalog dispatch and stages 7–9 remain
open; this checkpoint does not close any later gate.

## Stage 6 progress — managed attachment domains and session isolation

`ZfixAttach.hh/.cc` adds an Rx-owned active-identity index to managed hubs.
Identity keys include mode and every configured SessionId field, borrowing
immutable configured bytes until drain. Hash nodes use the identified Zfix.Attach
heap. Initiators claim before emitting Logon; acceptors claim only after a
validated first Logon and successful application admission, including its
asynchronous continuation. Default duplicate refusal closes only the candidate
with SessionError::Duplicate and emits no candidate control frame. Expired
admission continuations do not claim an identity. Raw hubs instantiate no managed
attachment index.

A claim survives physical close, pending admission/association/history work,
output completions and owner barriers; release precedes fixStoppedRx_. Release
checks owner identity, so a stale owner cannot remove a replacement. Several hubs
may explicitly share a domain through fixAttachmentsRx_ on the same Rx owner.
Independent owners do not share mutable tables or add locks. Application policy
still selects configured candidate links through the native accept hook and owns
persistent session lifecycle/dictionary selection and deliberate replacement.

Managed integration tests now cover three concurrent sessions on one client hub
and two listeners sharing a domain. Distinct SubIDs and payload lanes verify
routing isolation. Closing one session leaves the others exchanging traffic with
continuing owner-local counters. Duplicate tests cover initiator refusal before
Logon on one hub, and acceptor refusal across separate client/listener hubs. The
original owner continues traffic after refusal. A held server association
continuation also keeps the original claim after physical close: a second
listener refuses the duplicate until explicit cancellation and owner drain.
All new cases run in modes 4, 4.4A and T1.1 over TCP and TLS.

Validation (Clang debug/GNU C++2b, Linux x64):

- Source and installed unit suites: 20 programs/220 scopes; all 14 source
  schema-negative cases rejected as expected.
- Source TCP/TLS integration: eight programs/68 scopes, including 42 managed
  scopes. Installed managed consumers: two programs/42 scopes using staged
  headers/libraries. Installed raw/recovery consumers also pass.
- Twenty final managed runs per transport: 840 scopes pass, with no TAP failures.
- Attachment implementation/unit fixture and transport/managed fixtures pass
  ASan/LSan. Dependency DSOs remain uninstrumented in these focused runs.
- Managed TLS Valgrind: zero errors and zero definite/indirect/possible leaks;
  framework caches retain 32,472 bytes in 63 reachable blocks.

Top-level `make -j8` passes without warnings/errors; all 23 installed Zfix
headers match source. `git diff --check` passes.

Logs: `/tmp/zfix-attach-final-{unit,io,io-check}.log`,
`/tmp/zfix-attach-installed-unit.log`, `/tmp/zfix-attach-unit-asan.log`,
`/tmp/zfix-attach-repeat.log` and `/tmp/zfix-attach-valgrind.log`.
Stage 6 remains open for broader failover/overload/posted-ingress accounting,
failed-start cleanup/restart, live reset and counters/bounded redacted traces.
Stages 7–9 remain open.

## Stage 6 progress — failed initialization and listener restart

Managed startupCleanup now fails and explicitly finalizes/reinitializes the same
client/server hubs before a real Logon/request/reply/Logout exchange in every
managed mode. The cases reject null/stopped multiplexes, invalid owner IDs and
coincident Rx/Tx owners. TLS additionally fails after partial CA/certificate
allocation using missing trust/key paths, finalizes the partial state, then
succeeds with the disposable test credentials. No rollback policy is introduced.

The listenerCleanup regression reproduced an actual transport defect in both
Ztcp and Ztls: an ephemeral listener was stopped using configured port zero,
leaving its OS-assigned address bound until multiplex destruction. Native server
hubs now retain the actual bound IP/port. A second test reproduces a pending-bind
race: listener success after stop initialization could install a new listener
that stop never removed. Late success now closes the new native listener; late
failure/retry work cannot notify/reopen a stopped hub. An additional I/O
barrier drains late listener cancellation and accepted-connection notifications
before downLinks_ and final stop completion. Both native server
examples now use start/listen/stop/final and explicit failed-init finalization.
All other native Server::listen call sites were checked for the required start.

The real-I/O fixture creates a bind conflict, stops the original hub, restarts
the failed listener on the exact old ephemeral address, restarts the original,
and performs 32 listen/immediate-stop/exact-port-rebind cycles without stopping
the multiplex. Before-fix TCP/TLS runs fail at listenerCleanup; bound-address-only
TCP runs still fail the pending-listen cycles. The complete fix passes.

Validation (Clang debug/GNU C++2b, Linux x64):

- Source units: 20 programs/220 scopes plus 14 expected schema failures.
- Source integration: eight programs/76 scopes. Installed managed consumers:
  two programs/50 scopes using staged headers/libraries.
- Twenty managed runs per transport: 1,000 scopes pass, including 1,280 paired
  pending-listen/rebind cycles.
- Transport/managed ASan/LSan passes with fixture/templates instrumented;
  dependency DSOs remain uninstrumented.
- TLS Valgrind: zero errors and zero definite/indirect/possible leaks;
  framework caches retain 32,472 bytes in 63 reachable blocks.
- Rebuilt native ZtcpLoopTest/ZtlsBufHookTest: two programs/18 scopes pass.
- Dependent HTTP H1 hub/lifecycle regressions: two programs/19 scopes pass.
- Native TCP example ping/pong and bind-failure stop/final paths pass. Native
  TLS example completes a verified Python TLS handshake/request/reply and exits
  successfully through stop/final. Logs: `/tmp/zfix-start-examples.log`.

Final top-level `make -j8` passes without warnings/errors. All 23 installed
Zfix headers and both transport headers match source; `git diff --check` passes.
Build log: `/tmp/zfix-start-final-top.log`.

Logs: `/tmp/zfix-start-before-{tcp,tls}.log`,
`/tmp/zfix-start-pending-detail.log`, `/tmp/zfix-start-unit.log`,
`/tmp/zfix-start-final-{unit,io,check,repeat,valgrind,regression}.log`,
`/tmp/zfix-start-native-test.log` and `/tmp/zfix-start-http-regression.log`. Full stage 6 remains open for broader failover/outgoing setup deadlines,
overload/posted-ingress accounting, live reset and counters/bounded redacted
traces. Stages 7–9 remain open.

## Stage 6 progress — cancellable pending-connect prerequisite

The production bridge starts FIX lifecycle work after TCP establishment/TLS
readiness. Outgoing setup deadlines therefore need transport cancellation before
that point; suppressing a late callback alone cannot retire the pending socket.
ZiMultiplex now accepts an optional caller-retained ZiConnectReq handle and
provides cancelConnect(handle), dispatching its state exclusively to the network
Rx owner. Each submitted handle is one-shot. Pre-cancelled attempts fail, pending
cancellation produces one non-transient failure, completed cancellation is inert,
and rejected reuse cannot cancel/change the original attempt.

Epoll cancellation closes/removes the pending socket and retains its inactive
node until the current ready-event batch has drained, preventing recycling under
an old event pointer. Terminal paths clear callback references and cancellation
bindings. Completion processing marks its request inactive before error handlers
can reenter cancellation. ConnectEx cancellation closes the pending socket but
retains the overlapped request until completion, avoiding a second close of a
reused socket. Its Windows lifetime rule was checked against the linked Microsoft
reference; Windows execution is still an unverified platform gate.

ZiConnectTest registers six loopback scopes on reserved port 20040: cancellation
before submission, pending/repeated cancellation, active/completed handle reuse,
cancellation after completion, callback-driven cancellation of another ready
request, and early address failure. The repeated pending and callback-driven
cases check exactly one final result and network Rx ownership. The fixture and
multiplexer source pass ASan/LSan; dependency DSOs remain uninstrumented. Valgrind
reports zero errors and zero definite/indirect/possible leaks, with 31,904 bytes
in 61 reachable framework-cache blocks. Twenty repeated fixture runs pass.

A native regression also exposed existing ZtcpLoopTest misuse of the explicitly
single-threaded ZuTestMgr: worker callback checks could corrupt TAP IDs while main
was emitting checks. Those checks now collect atomic diagnostic counts and are
reported on main after owner drain. Twenty repeated TCP loop runs pass. Static
ZuTestCall sites in the new cancellation/attachment fixtures also use separate
source lines so their registration order is deterministic.

Validation (Clang debug/GNU C++2b, Linux x64):

- New source and installed cancellation fixture: six scopes each. Twenty
  repeated source runs pass 120 scopes. Multiplexer-source ASan/LSan and
  Valgrind results are recorded above; other dependency DSOs are uninstrumented.
- Zfix source suite: 20 programs/220 scopes and all 14 expected schema failures.
  Real TCP/TLS integration: eight programs/76 scopes.
- Installed managed TCP/TLS consumers: two programs/50 scopes.
- Native connect/multiplexer/TCP/TLS bundle: four programs/31 scopes. The initial
  bundle exposed the TCP test's malformed concurrent TAP; its corrected fixture
  passes separately for 20 runs/40 scopes and in the final bundle.
- Full top-level make -j8 passes, including all callers rebuilt for the changed
  connect entry signature. The final incremental build also passes without
  warnings/errors. All 23 Zfix headers and the ZiMultiplex/Ztcp/Ztls headers
  match the staged install; git diff --check passes.

Logs: `/tmp/zfix-connect-{test,asan,valgrind,repeat,installed}.log`,
`/tmp/zfix-connect-zfix-{unit,io}.log`,
`/tmp/zfix-connect-installed-zfix.log`, `/tmp/zfix-connect-native-final.log`,
`/tmp/zfix-connect-tcp-repeat.log` and `/tmp/zfix-connect-final-top.log`.

This checkpoint supplies the cancellation primitive; endpoint setup-deadline
arming, cancellation/failure observation and retry ownership are still pending.
It does not claim an operational connectTimeout before physical readiness.
The setup contract also records the asynchronous DNS requirement: ZiIP’s string
constructor can block through its resolver wrapper, so native hostname lookup
must use ZiResolver query/continuation cancellation for an owner-thread deadline.
Stages 6–9 remain open.

## Stage 6 progress — outgoing setup ownership and deadlines

Native TCP/TLS initiators now use one shared ZiConnect attempt for asynchronous
DNS, cancellable pending TCP and readiness deadlines. Managed FIX supplies
SessionConfig.connectTimeout before lookup/submission; Raw defaults to no native
interval. TLS readiness includes handshake and Tx installation. Each attempt
retains its owner until cancellation/result/physical-close drain; stale timer and
resolver posts hold only the retired attempt. Busy links reject address/SNI
changes, and managed live/draining state gates native automatic retries.

ZiResolver now offers acknowledged cancellation. It suppresses future query
results, clears callback captures on the resolver owner, and acknowledges after
an executing callback returns (or after resolver stop). An internally pending
c-ares query may remain until its result/timeout without link callbacks. Numeric
resolution may complete inline, so the shared attempt always posts its result.
Reconnect timers retain their owner and use cancellation generations; rearming
clears the old callable rather than retaining a stale generation.

Pre-readiness failure/cancellation starts endpoint owner drain before application
failure observers, including overrides that do not chain native defaults. It
opens no FIX Session, claims no attachment and assigns no wire sequence. Failed
setup preserves restored Rx/Tx cursors and epoch. Repeated stop without a ready
open now resets Tx stop-post bookkeeping; Raw also resets both owner-completion
flags so an earlier completed stop cannot satisfy a new deferred drain.

Managed setupCleanup adds three scopes per transport (28 managed scopes each).
Each covers repeated numeric/hostname cancellation, a silent local DNS deadline,
a real refused TCP endpoint, queued-connect cancellation with a held network
owner, restored cursors 19/27 and epoch 1, overridden cleanup observers and native
automatic retries. The real server restarts on the refused address; later retries
resolve localhost and exchange Logon/business/Logout. TLS additionally tests a
TCP peer that never answers its handshake, with deadline and explicit close.
Raw adds two scopes per transport (six each), repeating pre-ready cancellation
with immediate/deferred manager stop; deferred Tx is held after Rx acknowledgement
and stopped must remain pending. No raw output or managed FIX timer is introduced.

The first TLS setup regression exposed the native TLS close guard retaining its
pre-TCP state: a second lookup cancellation could be ignored. Cancellation now
runs before that guard. The queued-connect fixture also initially targeted a
real FIX listener; its legitimate accepted-side teardown polluted the shared
fixture counters. It now targets the bound/non-listening TCP endpoint. The full
build exposed protected default reconnect settings once the setup gate began
instantiating retry code; client hubs now grant their link access to that default.

Two initial TLS Valgrind runs reported intermittent existing multi-session
assertions (different scopes: held duplicate drain and ordinary shared-hub
traffic), with no memory errors/lost allocations. Failure-site and final counter
diagnostics were added. The diagnostic run and five subsequent consecutive runs
pass all 28 scopes each; their memory checks are clean. The original intermittent
assertion cause is **not established**, and remains a stage-6 follow-up rather
than a claimed fix. Original failing reports remain in
/tmp/zfix-setup-valgrind-tls.log and /tmp/zfix-setup-valgrind-tls-detail.log;
repeats are /tmp/zfix-setup-vg-stress{1..5}{,-memory}.log.

Final validation (Clang debug/GNU C++2b, Linux x64):

- Full top-level make -j8 and final incremental builds pass without compiler
  warnings/errors. The initial protected-default build failure is described
  above. Final build log: /tmp/zfix-setup-last-top.log.
- Zfix unit suite: 20 programs/220 scopes and all 14 expected schema failures.
  Final real TCP/TLS suite: eight programs/86 scopes (28 managed and six raw per
  transport, plus frame/recovery fixtures).
- Installed managed/raw consumers: four programs/68 scopes. Installed native
  pending-connect fixture: six scopes. All 23 Zfix headers plus ZiConnect,
  ZiResolver, ZiMultiplex, Ztcp and Ztls match the staged install (28 total).
- ASan/LSan: frame/managed fixtures four programs/62 scopes; raw fixtures two
  programs/12 scopes; resolver fixture plus resolver source 18 scopes. Other
  dependency DSOs remain uninstrumented.
- Valgrind TCP managed fixture: 28 scopes pass, zero errors and no definite/
  indirect/possible leaks; 32,416 bytes/62 framework-cache blocks reachable.
  Diagnostic TLS run and five repeats pass as described above, zero memory
  errors/lost allocations, 32,984 bytes/64 framework-cache blocks reachable.
  The two earlier assertion failures are not hidden or considered resolved.
- Native Zi/IP/resolver/connect/multiplexer bundle, full Ztcp test directory and
  full Ztls test directory: 18 programs/109 scopes pass. Dependent HTTP H1 hub/
  lifecycle regressions: two programs/29 scopes pass.
- git diff --check passes. No commits; the user's TODO.md edit and updated root
  design/plan files are preserved. Windows execution remains unverified.

Logs: /tmp/zfix-setup-final-{unit,source,asan,resolver,resolver-asan}.log,
/tmp/zfix-setup-raw-{test,tls-test,asan}.log,
/tmp/zfix-setup-{installed,installed-connect,install}.log,
/tmp/zfix-setup-native-{zi,tcp,tls}.log, /tmp/zfix-setup-http.log,
and the Valgrind logs named above. Staging prefix: /tmp/zfix-stage/opt/z;
/opt/z was not modified.

Stage 6 still needs posted-ingress/hub overload accounting, live reset production
dispatch, counters/bounded redacted traces, broader failover, and investigation
of the intermittent TLS Valgrind fixture assertions. Stages 7–9 remain open.

## Stage 6 progress — production reset dispatch and Busy submission policy

The prior intermittent TLS Valgrind assertion was reproduced on repetition 12
with failure-site diagnostics: the assertion was in the fixture's immediate
submission observer, followed by missing second-exchange traffic. The immediate
submit API explicitly permits Busy when control/application selection is pending;
the fixture had discarded such bodies and required Accepted unconditionally.
It now retains Busy bodies on its own Tx-owned unsequenced queue and checks their
eventual selection. The library's immediate-admission semantics are unchanged.
A deterministic case posts a queue marker and submission in one Tx task, ensuring
the marker's pump cannot run until after submission; Busy must occur and traffic
must still complete. Five subsequent TLS Valgrind runs pass 29 scopes each. The
fifth also records Busy in ordinary multi-session traffic (client/lane 1), then
completes that traffic with correct counters. This closes the earlier fixture
follow-up without treating Busy as a transport/session failure or hiding it in
library retries. Logs: /tmp/zfix-setup-vg-stress12.log and
/tmp/zfix-busy-vg{1..5}{,-memory}.log.

Managed endpoints now expose a fixed ResetRequest dispatcher and Rx-owned
reset_(request) admission method. Drain, initial OnOpen agreement, Prepare,
live Start and prepared reconnect Disconnect call the existing controller.
Live commands require captured current connection/epoch; stale commands cannot
operate on a later incarnation. OnOpen requires the never-opened owner and
matching zero connection/epoch. fixResetRx_ observes local command admission;
Drained/ResetReady events and epoch hooks retain completion semantics. The
application supplies both-party agreement and calendars. Raw remains externally
managed. Rx::drainBegin_ now returns whether a drain was admitted while preserving
its existing event application. No new FIX wire policy or parallel state machine
was introduced.

New ZfixResetTcpTest/ZfixResetTlsTest provide 12 scopes each: all three modes,
either live driver, reconnect reset on continuing owner objects, and explicit
initial reset agreement. A separate application owner posts public commands;
the common Rx owner batches both agreed reconnect-disconnect operations before
native close. Tests refuse prepare before drain, invalid paths/epochs and stale
connection/epoch commands; retain unsequenced work through live turnover; verify
the actual old business sequence boundary is peer-acknowledged before turnover;
check both owner epoch notifications; and exchange reset Logons at sequence 1.
Live reset retains connection 1; reconnect advances both connections to 2.
Independent snapshots are taken per epoch rather than merging an old epoch record.
Non-replayable operational control tails may be retired after explicit agreement;
tests do not invent peer acknowledgements for those tails. Missing TLS credentials
produce expected TAP failure scopes and clean shutdown instead of continuing into
an uninitialized hub.

Validation (Clang debug/GNU C++2b, Linux x64):

- Full top-level make -j8 and final incremental build pass without compiler
  warnings/errors: /tmp/zfix-reset-last-top.log.
- Unit suite: 20 programs/220 scopes plus all 14 expected schema failures.
  Real I/O suite: ten programs/112 scopes (29 managed, 12 reset, six raw and
  existing frame/recovery scopes per transport).
- ASan/LSan: frame/managed fixtures four programs/64 scopes and new reset
  fixtures two programs/24 scopes. Other dependency DSOs remain uninstrumented.
- Reset Valgrind fixtures: 12 scopes each, zero memory errors and zero definite/
  indirect/possible leaks. TCP retains 32,416 bytes/62 framework-cache blocks;
  TLS retains 32,472 bytes/63 blocks. Corrected managed TLS fixture passes five
  consecutive runs/145 scopes, zero memory errors/lost allocations; 32,984
  bytes/64 framework-cache blocks remain reachable in the final run.
- Installed reset/managed/raw consumers: six programs/94 scopes pass. All 23
  Zfix headers plus five native setup/transport headers match the stage (28
  total). Staging prefix remains /tmp/zfix-stage/opt/z; /opt/z is untouched.
- git diff --check passes. No commits; user root design/plan/TODO edits remain
  intact. Windows execution is still unverified.

Logs: /tmp/zfix-reset-{unit,final-io,last-asan,installed,install}.log,
/tmp/zfix-reset-final-vg-{tcp,tls}.log,
/tmp/zfix-reset-vg-{tcp,tls}-memory.log, /tmp/zfix-busy-asan.log,
and the repeated Busy fixture logs above. /tmp/zfix-reset-no-cert.log records
the expected nonzero/no-crash missing-credential run.

Stage 6 remains open for hub/posted-ingress overload bounds, protocol/queue
counters, bounded opt-in authentication-redacted traces, and broader failover
coverage. Live/reset production dispatch and the diagnosed Busy fixture issue
are no longer pending. Stages 7–9 remain open.

## Stage 6 progress — native receive-post budgets and credit retirement

Native receive callbacks now reserve network-Rx-owned credits before publishing
pooled buffers to application Rx. Zfix enables the path for both managed/raw
TCP/TLS links; generic native links compile out the optional credit/refund path.
LinkLimits.postedIngressBytes bounds a link and the hub's PostedLimits bounds
its aggregate, both initially four standalone frame budgets. Charges include the
actual pooled object/builtin storage plus separate jumbo capacity. Return jobs
move the existing connection handle and use a raw link back-pointer; no new
per-buffer link reference is introduced. The original data job retains its usual
owner/connection/buffer handles. All counters and mutations stay on network Rx.

PostedIngress supplies byte/count/peak/refusal snapshots and overflow-safe
admission. Live limit changes are network-owner-only and cannot undercut current
reservations; zero limits refuse every post. Shared hub domains require a stable
binding and the same multiplex/network Rx owner. Native refusal closes only the
offending connection and does not publish the rejected buffer. Managed live
sessions emit Overload and an Rx ingress observer; raw managers receive their
fault observer, including pre-readiness connection zero. Existing parser and
sequence behavior is unchanged.

Credits return after application receive dispatch, including stale-connection
discards. Until network Rx processes the return, the reservation remains charged.
An additional app Rx → network Rx → app Rx barrier precedes native Tx drain;
therefore refunds cannot outlive a stopped link or its hub. The generic default
close path retains its prior owner sequence. The new tests explicitly hold
network Rx after physical connection state clears, drain the other lifecycle
phases, and require stopped to remain pending.

An isolated negative control copies the two native headers into a /tmp include
overlay and disables only that credit barrier. Both TCP and TLS controls fail
exactly the physicalDown-and-not-stopped assertion; all production headers remain
unchanged. This demonstrates that the test detects premature retirement rather
than merely checking a quiet final counter. Overlay/build recipe and expected
failure logs are /tmp/zfix-post-no-barrier{,/zlib},
/tmp/zfix-post-no-barrier.mk and /tmp/zfix-post-negative-{build,tcp,tls}.log.

ZfixIngressTest covers full/zero budgets, release/reuse, refused live shrinking,
unchanged limits after refusal, and UINT64_MAX arithmetic. New TCP/TLS ingress
fixtures prove aggregate admission across two links with application Rx held,
per-link refusal, TLS handshake refusal before raw readiness, credit retirement,
and unaffected-link progress. The managed fixture also rejects a live receive
after a successful exchange and checks the explicit SessionError::Overload event.

This checkpoint bounds native receive posts, not all retained input or all
cross-owner work. Active native reads/partial TLS records and bounded coalesced
tail scratch are separate from posted reservations. Fixed descriptor/callback
cost is bounded by the admitted allocation count but is not itself charged as
buffer capacity. Aggregate retained/incomplete/read-ahead data, connection and
control metadata admission, and variable application-to-Tx posted work remain
open alongside the other stage-6 requirements. Existing per-link codec/Rx/Tx/
replay limits still apply.

Validation (Clang debug/GNU C++2b, Linux x64):

- Full top-level make -j8 and final incremental builds pass without compiler
  warnings/errors: /tmp/zfix-post-last-top.log.
- Unit suite: 21 programs/222 scopes plus all 14 expected schema failures.
  Real I/O suite: 12 programs/118 scopes. Twenty repeated ingress fixture runs
  per transport pass another 80 scopes.
- ASan/LSan: new ingress fixtures plus ZfixIngress.cc (four scopes) and existing
  frame/managed/raw/reset fixtures (102 scopes), all pass. Other dependency DSOs
  remain uninstrumented.
- New TCP/TLS ingress Valgrind fixtures: two scopes each, zero errors and no
  definite/indirect/possible leaks; TCP retains 31,904 bytes/61 framework-cache
  blocks and TLS 32,472 bytes/63 blocks.
- Full native Ztcp/Ztls tests: 14 programs/73 scopes pass. Dependent HTTP H1
  hub/lifecycle checks: two programs/29 scopes pass.
- Installed ledger/ingress/managed/raw/reset consumers: nine programs/102 scopes
  pass. All 24 Zfix headers plus five native setup/transport headers match the
  stage (29 total). Logs: /tmp/zfix-post-installed{,-unit}.log.
- git diff --check passes. No commits; root user plan/design/TODO edits remain
  intact. Windows execution remains unverified.

Logs: /tmp/zfix-post-{unit-all,final-io,ledger-asan,asan-all}.log,
/tmp/zfix-post-vg-{tcp,tls}{,-memory}.log,
/tmp/zfix-post-native-{tcp,tls}.log, /tmp/zfix-post-http.log,
/tmp/zfix-post-repeat-{tcp,tls}{1..20}.log and the negative-control logs above.
Staging remains /tmp/zfix-stage/opt/z; /opt/z is untouched.

## Stage 6 progress — bounded application submission handoff

Managed submit(body) and Raw sendRaw(output) now return boolean handoff admission
and retain their const handle arguments. AppRx reserves per-link
LinkLimits.postedSubmitBytes and hub PostedLimits (second hub constructor
argument), both 4 MiB by default, before any Tx job. The ledger is AppRx-owned;
there are no shared counters, locks, variable payload copies or per-message link
references. AppTx calls run selection and its observer inline. Other threads,
null handles and non-live AppRx submissions return false without posting or
calling selection observers. Applications own refusal/retention/retry policy.
Owner-only submission APIs retain their direct selection results.

NetLink's submission ledger conservatively charges each retained handle even
when several jobs share its bytes. Managed charges the pooled body object and
separate jumbo capacity through TxBody::memory(). Raw additionally charges
RawOutput, ZiIOBuf base and exposed complete-frame capacity; custom frame
allocator metadata/extra builtin storage remains outside that exposed charge.
Fixed scheduler descriptor overhead is bounded by admitted handle count. Owner
control/output paths keep their existing independent limits and administrative
reserve. Credits return after Tx selection/observer, including refusal, and the
link's final stopped observer waits for all returns to AppRx before releasing
its sole teardown handle. Existing output/replay bounds apply after selection.

Real TCP/TLS ingress fixtures now have four scopes each: network ingress plus
shared/per-link submission admission. They hold AppTx, admit exactly one retained
output across two links, refuse the second with no publication/observer, inspect
owner counters, reuse returned capacity, and close with admitted work pending.
The managed quota fixture also refuses a handoff before selection and verifies
unchanged loaded/sent/acknowledged progress, retained caller body and successful
later submission. Manager-thread fixture calls now dispatch to AppRx explicitly.
The forced-Busy fixture explicitly posts its Tx submit after the queue marker,
preserving its ordering with the new synchronous AppTx dispatcher behavior.

Validation to date (Clang debug/GNU C++2b, Linux x64):

- Full top-level make -j8 passes without compiler warnings/errors:
  /tmp/zfix-submit-top.log.
- Unit suite: 21 programs/222 scopes and 14 expected schema failures pass:
  /tmp/zfix-submit-unit.log.
- Real I/O: 12 programs/122 scopes pass: /tmp/zfix-submit-io.log.
- ASan/LSan ingress fixture + ledger: eight scopes pass; dependency DSOs remain
  uninstrumented: /tmp/zfix-submit-asan.log.
- Valgrind ingress fixtures: four scopes each pass, zero errors and zero
  definite/indirect/possible loss. Framework caches remain reachable (TCP
  31,904 bytes/61 blocks; TLS 32,472 bytes/63 blocks):
  /tmp/zfix-submit-vg-{tcp,tls}{,-memory}.log.

Final verification: /tmp/zfix-submit-final-top.log passes without compiler
warnings/errors. Twenty additional runs per transport pass 160 ingress/submission
scopes (/tmp/zfix-submit-repeat-{tcp,tls}-{1..20}.log). ASan/LSan frame/managed
consumers pass another 66 scopes, 74 with the ingress suite; dependency DSOs stay
uninstrumented (/tmp/zfix-submit-managed-asan.log). Staged installed consumers
pass four programs/68 scopes (/tmp/zfix-submit-installed.log), and all 24 Zfix
headers match /tmp/zfix-stage/opt/z/include/zlib. The final staging log is
/tmp/zfix-submit-final-install.log; /opt/z remains untouched. git diff --check
passes. All launched jobs reached terminal status; no background work remains.

The TLS Valgrind command was corrected to pass the libtool/Valgrind launcher
through prove --exec; the initial harness invocation failed before the TLS
fixture started. Only the corrected terminal run is counted above.

Stage 6 remains open for aggregate retained/incomplete-input/read-ahead hub
limits, connection/control metadata admission, protocol/queue counters, bounded
opt-in authentication-redacted traces and broader lifecycle/failover coverage.
Stages 7–9 remain open. No commits; user design/plan/TODO edits are intact.

## Stage 6 progress — hub limit inheritance and read-ahead count

Client/Server and their raw aliases now take one HubLimits snapshot. The common
EndpointHub stores immutable LinkLimits defaults exposed by fixLinkLimits();
constructors without explicit limits inherit them, including named initiator
links and non-default epochs. The link defaults carry codec/group/parse bounds,
read-ahead, buffer, output, wire, control and replay policy. HubLimits.posted and
.submit independently configure the aggregate network/AppRx posted-work ledgers.
Explicit LinkLimits remains a complete per-link override; copy fixLinkLimits()
first when changing only selected fields. There are no runtime schema changes,
new allocations, locks or cross-owner reads of mutable protocol state.

SeqLimits/RxLimits now enforce readAheadCount alongside readAheadBytes before
queue insertion. The default 4,096 is an initial small-order workload choice
(roughly 1 KiB pooled backings against the 4 MiB byte default), not a measured
performance budget. Duplicates consume no capacity. Processing and gap-fill
purge release it. Count zero forbids queuing while preserving in-order progress;
a full gap window still admits its missing in-order recovery frame. Accepted
read-ahead continues to survive physical reconnect. These are per-session bounds
configured at the hub, not an aggregate retained-byte budget across all links.

The raw I/O fixture now configures output/queue limits once per hub. Each
TCP/TLS ingress fixture adds three hub-limit scopes exercising receive-frame,
transmit-frame and wire-queue refusals. The managed fixture adds a zero-count
acceptor hub: an ordinary in-order exchange succeeds, then an injected valid
high-sequence frame fails with Overload before business dispatch. Unit tests
cover independent count exhaustion, duplicate input at capacity, reuse after
purge, zero-count progress and missing-sequence recovery at capacity.

Temporary negative controls leave production unchanged. Replacing hub-default
inheritance with LinkLimits{} in copied Client/Server headers makes exactly all
three selected hub-limit scopes fail. Removing only count enforcement from a
copied Seq header makes exactly the new countLimit scope fail (one of ten).
Overlay sources/recipes: /tmp/zfix-{hub,count}-negative*; terminal logs:
/tmp/zfix-hub-negative.log and /tmp/zfix-count-negative.log.

ASan found and verified a fixture-only borrowed-lifetime correction: the injected
header originally borrowed from a temporary config destroyed before encoding.
A named config now outlives frame construction. The first ASan log records this
failure; it is not counted as a pass. Corrected source, ASan and managed Valgrind
runs all pass. txHeader retains its explicit borrowed-header contract.

Validation (Clang debug/GNU C++2b, Linux x64), all jobs terminal:

- Final full make -j8 succeeds with no compiler diagnostics:
  /tmp/zfix-hub-final-top.log.
- Unit suite: 21 programs/223 scopes plus 14 expected schema failures pass:
  /tmp/zfix-hub-count-unit.log. Focused Seq/Rx tests: 32 scopes pass.
- Final real I/O suite: 12 programs/130 scopes pass:
  /tmp/zfix-hub-final-io.log.
- ASan/LSan: ingress 14 and raw 12 scopes pass in
  /tmp/zfix-hub-count-asan.log; corrected frame/managed 68 scopes pass in
  /tmp/zfix-hub-final-asan.log, 94 total. Fixture/templates and the ingress
  ledger are instrumented; dependency DSOs remain uninstrumented.
- Valgrind: ingress 14 scopes and corrected managed 62 scopes pass, zero
  errors and no definite/indirect/possible loss. Logs:
  /tmp/zfix-hub-count-vg-{tcp,tls}.log (ingress and initial managed run),
  /tmp/zfix-hub-final-vg-{tcp,tls}{,-memory}.log (corrected managed rerun).
- Staged installed ingress/raw/managed consumers: six programs/88 scopes pass:
  /tmp/zfix-hub-installed.log. All 24 Zfix installed headers match source;
  staging log /tmp/zfix-hub-install.log. /opt/z remains untouched.
- git diff --check passes. No commits; user design/plan/TODO edits remain intact.

Stage 6 still needs aggregate retained/incomplete-input/read-ahead memory
accounting, connection/control metadata admission, protocol/queue counters,
bounded opt-in authentication-redacted traces and broader failover coverage.
Retention accounting must preserve continuing read-ahead and borrowed enclosing
input lifetimes; socket close alone is not a release point. No new ownership
or aggregate-retention gate is claimed here. Stages 7–9 remain open.

## Stage 6 progress — protocol snapshots and bounded redacted wire traces

New ZfixStats.hh supplies fixed Rx/Tx/wire snapshots. Managed and raw endpoints
expose rxStats_()/txStats_() on their respective application owners. Counters span
physical reconnect and epoch changes; instantaneous queue fields come only from
their owning shard. Rx counts complete frames once across admission pauses,
bytes, framing/decoding errors, generated/received Rejects, gaps/replay requests,
duplicates/ignored input, business-hook errors and faults/overloads. Raw retains
framing/external-manager semantics without managed sequence state. Tx separates
history/control occupancy, pending completions, load/enqueue refusals, encoding
and output-admission errors. Wire snapshots distinguish admitted/published/local
completed/failed frames, bytes, replay/Reject publication and current/peak charge.
Stale/duplicate completions and invalid continuations do not inflate counters.
No socket completion becomes a peer acknowledgement or business outcome.

New ZfixTrace.hh/.cc implements a bounded owner-local trace store configured by
HubLimits.link.traceRx/traceTx. Bytes zero disables parsing, allocation and
observers; the default escaped record excerpt is 4 KiB. Capture rotates oldest
records before new allocation, charging rounded node/buffer pools, builtin and
jumbo capacity. Impossible/oversized allocations are dropped before allocation.
Owner trace stats report records, rotations, drops, truncation and current use.
Rx/Tx stores occupy separate cache lines. There are no mixed-owner counters,
locks or per-record link references.

Trace records own immutable escaped text, fixed context and original wire length;
observers can retain one enclosing TraceRef through eviction and link/hub
teardown. Internal quotas do not cover observer-owned references/output queues,
which the application must bound. Raw Rx invents no epoch/sequence. Capture taps
complete FIX frames before Tx splitting/encryption and after Rx envelope
validation; no TLS handshake state/material is logged. The core codec/session
unknown-tag and replay policies are unchanged.

The renderer reuses codec tag indexes and binary metadata from the endpoint
catalog, built-in controls and header/trailer. Logon values, sensitive tags
89/91/96/553/554/925/1402/1404 and all binary values are redacted. A sensitive
field makes the remaining suffix opaque, preventing malformed credential text
from masquerading as later public fields. Custom authentication fields use
fixTraceSecret_(msgType, tag), which only adds redaction. Unknown fields, orphan
binary data, malformed length/data and conflicting binary metadata across group
scopes stop rendering; remaining bytes are never copied. Control bytes, quotes
and backslashes are hexadecimal escapes. Capture never changes admission,
publication, socket completion or session progress.

Tests include redaction/custom secrets, binary delimiter/injection boundaries,
unknown/malformed input, group metadata conflicts, escaping, disabled/oversized
budgets, rotation/truncation and retained records surviving eviction. Rx/Wire
unit tests exercise paused input counted once, duplicate completions, reconnect
persistence, association refusal and failed/unpublished output. Real raw TCP/TLS
fixtures compare escaped plaintext and rotate traces, retain observer records
through finalization, and reconcile statistics with application observers. Real
managed fixtures add credentials to both Logons and verify redaction and owner
snapshots through close on both transports.

Temporary negative controls change copies under /tmp only: disabling sensitive
redaction fails exactly the redaction scope, while counting every receive retry
fails exactly the new Rx statistics scope. Production remains unchanged. Source
and recipe: /tmp/zfix-diag-negative/ and /tmp/zfix-diag-negative.mk; terminal
expected failures: /tmp/zfix-diag-negative.log.

Validation (Clang debug/GNU C++2b, Linux x64):

- Full top-level make -j8 passes without compiler diagnostics:
  /tmp/zfix-diag-final2-top.log.
- Unit suite: 22 programs/230 scopes plus all 14 expected schema failures pass:
  /tmp/zfix-diag-unit.log. Focused diagnostic/core run: five programs/79 scopes.
- Real I/O: 12 programs/132 scopes pass: /tmp/zfix-diag-io.log.
- ASan/LSan: new trace implementation + fixture five scopes, plus owner/frame/
  managed/raw/ingress fixtures 96 scopes, all 101 pass. The trace formatter and
  ingress ledger are instrumented with the fixtures/templates; other dependency
  DSOs remain uninstrumented. Logs: /tmp/zfix-diag-{trace-asan,io-asan}.log.
- Valgrind: four TCP/TLS ingress/managed programs, 78 scopes pass; zero errors
  and no definite/indirect/possible loss. Framework caches remain reachable:
  /tmp/zfix-diag-vg-{tcp,tls}.log and per-process memory logs.
- Installed diagnostic/ingress/raw/managed consumers: seven programs/95 scopes
  pass. All 26 Zfix staged headers match source. Logs:
  /tmp/zfix-diag-installed-{trace,io}.log; staging log /tmp/zfix-diag-install.log.
  Stage remains /tmp/zfix-stage/opt/z; /opt/z is untouched.
- git diff --check passes. All launched jobs are terminal. No commits; user
  design/plan/TODO edits remain intact. Windows execution remains unverified.

Stage 6 remains open for aggregate retained/incomplete/read-ahead memory
accounting, connection/control metadata admission and broader failover coverage.
Protocol snapshots and bounded redacted trace deliverables now have concrete
unit, real-I/O, memory, installed and negative-control evidence. Stages 7–9
remain open; no later acceptance gate is claimed complete.

## Stage 6 progress — live standby failover with retained work

ZfixRecoveryIOTest now adds warm-store and file-restored standby handoff in all
three managed modes over TCP/TLS. Two OS-assigned listeners coexist before the
primary fails. Acceptor hubs share an AppRx attachment domain. After primary
owner drain, its stopped listener port is reserved by a non-listening socket.
Retrying that endpoint must fail without opening another logical session or
changing received/processed/loaded/published/acknowledged/recovery cursors.
The same client then selects the standby endpoint in epoch one.

The standby restores both application owners before its link is published on
the listener's network-Rx route, avoiding a partially restored attachment window.
The cold variant loads a distinct store and body allocations from fixture files;
original retired owners remain alive independently. Role identity and reset
flags remain unchanged. The recovered message retains its identity, sequence,
PossDup and exact OrigSendingTime; it is received once.

A new business body is queued before the first disconnect and deliberately
held unsequenced by application queue policy. It survives the failed retry and
is released at recovered readiness, rather than reconstructed by replay. Its
new association waits while an actual old load ticket returns refusal; the
current ticket/cursor and unsequenced/selected work must remain intact. After
valid association, a fixture-owned Tx record queue (within WireTx's charged
admission) holds the current wire head. An actual original token's failed
completion must not fail or complete that current frame. Release then completes
one publication, one local completion and one peer receipt for the new request.
Attachment counts prove drain before handoff and final release. Five stopped
notifications comprise two original connection sides, one failed client setup,
and two sides after handoff; the client session owner is reused. There are only
four Logons/readiness observations and no within-epoch sequence
reuse. Pending fixture work has explicit stop cancellation/cleanup.

This adds test/application policy coverage; it introduces no FIX endpoint-list,
reconnect/reset, persistence, correlation or retry policy in libZfix. The native
Ztcp/Ztls connect interface and managed continuation guards are reused.

Validation (Clang debug/GNU C++2b, Linux x64):

- Top-level make -j8 and final affected-module build pass without compiler
  diagnostics: /tmp/zfix-failover-final-top.log and
  /tmp/zfix-failover-retained-build.log.
- Focused recovery/failover: two programs/24 scopes pass:
  /tmp/zfix-failover-retained-{tcp,tls}.log. Each program has six original
  recovery and six warm/cold failover scopes.
- Full real I/O: 12 programs/144 scopes pass: /tmp/zfix-failover-final-io.log.
  No library/unit source changed; the preceding 230 unit scopes remain recorded.
- Final ASan/LSan: two recovery/failover programs/24 scopes pass, instrumenting
  fixture and adapter templates; dependency DSOs remain uninstrumented:
  /tmp/zfix-failover-final-asan.log.
- Final Valgrind: 24 scopes pass with zero errors and zero definite/indirect/
  possible loss: /tmp/zfix-failover-final-vg-{tcp,tls}{,-memory}.log.
- Installed recovery/failover: two programs/24 scopes pass against the existing
  staged libraries/headers: /tmp/zfix-failover-final-installed.log. All 26 Zfix
  headers still match /tmp/zfix-stage/opt/z/include/zlib. /opt/z is untouched.
- Negative control: removing only WireTx's token equality check from a copied
  header fails exactly the selected failover scope against its live held head.
  Production is unchanged. Current retained-work overlay/recipe:
  /tmp/zfix-failover-negative/ and /tmp/zfix-failover-held-negative.mk;
  terminal expected failure: /tmp/zfix-failover-held-negative.log.
- git diff --check passes. All launched jobs reached terminal state. No commits;
  user design/plan/TODO edits remain intact. Windows runtime remains unverified.

Limits audit against Q015 and the plan: hub configuration already supplies
per-message/per-session frame, group, read-ahead, output and replay bounds;
posted-buffer quotas have explicit hub aggregate scope. The remaining receive
limit gap is actual allocated incomplete-input backing (not just logical stream
length), plus pre-post administrative/connection work admission. A new global
sum of every retained read-ahead/application handle is not claimed or inferred
from per-session configuration. Deterministic partial-write/reset-handshake
fault coverage also remains open. Stage 6 is not complete; stages 7–9 stay open.

## Stage 6 progress — allocated incomplete receive backing

`HubLimits::link.rx.incompleteBytes` now reaches both managed/raw engines and
native TCP/TLS delivery. The default is 4 MiB, replacing the previously intended
but unenforced 1 MiB input row: a maximum ordinary frame needs room for pooled
fragment backing and a final gather. Explicit link snapshots still override hub
defaults. The quota is per session; it adds no global application-retention sum.

ZiRxStream maintains separate jumbo backing across moves, extraction, splicing,
consumption and cleanup. The charge includes the variable heap alignment prefix
and cache size class, plus the transport allocator's rounded object/builtin
storage. Header scratch is charged too. Growth reserves new jumbo backing while
the old allocation remains live. Optional extraction admission reserves split/
gather backing before allocating; exact detach needs no additional object.
Native delivery admits buffers before appending even when a complete frame is
paused. Allocation geometry comes from the native pool type, preserving tuning.
System allocator bookkeeping and idle pool caches are outside these charges.

A failed admission closes only the offending link with managed Overload or raw
Size. It does not become a malformed-frame statistic. Native delivery discards
already-queued input after disconnect starts; existing posted credits retire
before stopped. Completed detached frames use existing pending/read-ahead bounds,
and continuing reconnect retains the read-ahead queue. Owner snapshots expose
current `inputBytes` and cumulative reserved/observed `inputPeak`; disconnect
clears occupancy and preserves the peak. Standalone RxFrame callers use `admit_`
before pushing to bound the retention transition; `next_` also validates input
that a caller has already queued.

Coverage added:

- ZiRxStreamTest: jumbo accounting through queue operations and allocation-free
  extraction refusal, with unchanged stream identity/length and zero-copy bypass.
- ZfixRxFrameTest: zero/exact object budgets, sparse fragments, jumbo accounting,
  split/gather refusal, pre-push admission, and scratch growth with builtin then
  already-allocated jumbo backing. Empty input is filtered without charging even
  at a zero budget. Reset preserves the peak.
- Both ingress transports: hub-inherited sparse-fragment refusal, bounded
  successful gather, and continued delivery while a complete frame is paused.
  Owner snapshots synchronize each write without assuming packet boundaries.
  Two native deliveries are queued while application Rx is held: the first
  overload closes; the second must release its credit without another fault.
- Both managed transports: after a normal exchange, inject an unfinished header
  one byte at a time; require one Overload, bounded peak, zero post-close input,
  no framing error and no managed publication/sequence association for fragments.
- A source-overlay negative control removes only TCP pre-append admission. Both
  the sparse-fragment and paused-input peak assertions fail (scopes 9 and 11).
  Production is unchanged. Recipe: /tmp/zfix-input-negative.mk; overlay:
  /tmp/zfix-input-negative/; details: /tmp/zfix-input-negative-detail.log.
  A second overlay removes only TLS record discard during disconnect and fails
  the queued-delivery scope 11: /tmp/zfix-input-close-negative.mk and
  /tmp/zfix-input-close-negative-detail.log.

Validation checkpoint (Clang debug, GNU C++2b, Linux x64):

- Zfix units: 22 programs/232 scopes plus the 14 expected schema failures,
  /tmp/zfix-input-complete-unit.log. ZiRxStreamTest: 20 scopes,
  /tmp/zfix-input-stream-rounded.log.
- Full Zfix I/O: 12 programs/152 scopes, /tmp/zfix-input-complete-io.log.
- Native TCP: 3 programs/6 scopes, /tmp/zfix-input-native-tcp.log. Native TLS:
  11 programs/67 scopes, /tmp/zfix-input-native-tls.log.
- ASan/LSan: stream 20, final framing 9, ingress TCP/TLS 22 and managed/transport
  TCP/TLS 70 scopes; /tmp/zfix-input-asan-stream.log,
  /tmp/zfix-input-complete-asan-frame.log, /tmp/zfix-input-queued-asan.log and
  /tmp/zfix-input-asan-session-final.log. Dependency DSOs remain uninstrumented.
- Valgrind: stream 20, framing 9, ingress TCP/TLS 22 and managed TCP/TLS 64
  scopes. Zero errors and zero definite/indirect/possible lost bytes. Logs:
  /tmp/zfix-input-vg-{stream,tcp,tls,session-tcp,session-tls}-memory.log and
  /tmp/zfix-input-complete-vg-frame-memory.log.
  The strengthened queued-delivery TCP run is recorded separately in
  /tmp/zfix-input-queued-vg-tcp-memory.log; the TLS memory log above was refreshed.
- HTTP: 26 programs/262 scopes, /tmp/zfix-input-final-http.log.
- QUIC: 20 programs/176 scopes, /tmp/zfix-input-quic.log.
- Final `make -j8`: clean, /tmp/zfix-input-complete-top.log. Installation into
  /tmp/zfix-stage completed without changing /opt/z; logs:
  /tmp/zfix-input-install.log and /tmp/zfix-input-final-install.log. All 26 Zfix
  headers plus six native setup/stream/transport headers match source (32 total).
- Installed units: 20 separately compiled consumers/225 scopes,
  /tmp/zfix-input-installed-unit.log. The final zero-budget empty-input case was
  additionally rebuilt against installed headers/libraries: nine framing scopes,
  /tmp/zfix-input-complete-installed-frame.log; recipe:
  /tmp/zfix-input-installed-frame.mk. Installed framing assertions include both
  empty-input admission and zero retained/peak bytes.
- Final installed TCP/TLS: four separately compiled consumers/86 scopes,
  /tmp/zfix-input-complete-installed-build.log. Dynamic dependencies resolve to
  /tmp/zfix-stage/opt/z/lib, including Zfix, ZfixFmt, Zi, Ztcp and Ztls.
- git diff --check and explicit whitespace checks for current untracked Zfix
  files pass. All launched jobs reached terminal state. No commits; user
  design/plan/TODO edits remain intact. Windows runtime remains unverified.

Pre-post administrative/connection work admission and deterministic partial-write/
reset-handshake fault coverage remain open. Stage 6 is not complete; stages 7–9
remain open.

## Stage 6 progress — bounded administrative owner handoffs

Managed Rx-to-Tx administrative posts now reserve independent per-session and
hub aggregate capacity before entering the destination queue. Configure
`HubLimits::admin` and `HubLimits::link.postedAdminBytes`; both default to 4 MiB.
The AppRx-owned `adminPosted_()` and `fixAdminRx_().state_()` snapshots report
occupancy, peak and refusals. Business submission reservations remain separate.

The scope includes lifecycle events without bodies, encoded controls, generated
Rejects, replay requests and peer Reject reports retaining wire backing. Charges
include rounded callable/delegate envelopes in both directions, pending credit
returns, and owned body/receive backing with its known jumbo prefix/cache class.
The transport's declared receive allocator geometry is preserved. Bodies are
encoded into bounded pooled storage on Rx before admission; refusal adds no
retained work to the Tx queue. Ring capacity, idle caches, and application
retention after callbacks are separate from these posted-work charges.

TxBody::memory now includes its rounded pool object and known jumbo heap backing.
The Tx control queue uses that same body charge plus its rounded command node,
including selected association continuations. It no longer counts only exposed
buffer capacity. Both posted work and destination control storage are bounded.

Admission failure latches the administrative failure on Rx, reports live Overload
before physical close, stops further framing/posting for that session, and forces
close without requiring a graceful TLS send. Terminal failure/disconnected
observers already produced by the session are still delivered when their Tx
snapshot is refused. Failure cannot recursively queue another control. The Tx
coordinator has an owner-only idempotent physical close independent of lifecycle
delivery; it handles stale connection closes, clears commands and drains pending
application continuations. Stopped also waits for administrative credit returns.
Raw endpoints have no managed administrative handoffs.

Coverage/evidence (Clang debug, GNU C++2b, Linux x64):

- `make -j8`: clean, /tmp/zfix-admin-complete-build.log. No compiler warnings or
  errors. User design/plan/TODO edits remain intact; no commits.
- Source units: 22 programs/234 scopes plus the 14 expected schema failures,
  /tmp/zfix-admin-complete-unit.log. New control-memory coverage distinguishes
  exposed capacity from actual jumbo allocation, tests exact reservation and
  capacity reuse, and exercises physical close with pending association work.
- Full source I/O: 12 programs/170 scopes, /tmp/zfix-admin-complete-io.log.
  Each managed transport has 41 scopes. Nine administrative cases cover hub
  inheritance for zero per-link/aggregate limits, real jumbo heartbeat echoes
  and peer Reject backing, replay-command refusal after its lifecycle event,
  generated Rejects, shared-hub isolation/capacity reuse, terminal observer
  delivery and application owners coincident with native owners.
- The live cases disable business submission capacity after normal exchange.
  Controls still progress. Held Tx and Rx owners prove credit returns stay
  reserved until Rx consumes them. Refused work assigns no sequence, a second
  session can be refused while the first continues, and coalesced input stops
  framing after overload. The injector retains configuration through borrowed
  header encoding; it does not reserve managed sequences for injected frames.
- ASan/LSan: control unit 25 scopes and transport/session I/O 88 scopes, all pass;
  /tmp/zfix-admin-complete-asan-ctl.log and
  /tmp/zfix-admin-complete-asan-io.log. Dependency DSOs remain uninstrumented.
- Valgrind: control unit 25 plus managed TCP/TLS 41 each (107 scopes). Zero
  errors and zero definite/indirect/possible lost bytes. Runtime logs:
  /tmp/zfix-admin-complete-vg-{ctl,tcp,tls}.log; memory reports use the matching
  -memory.log suffix. hwloc uses its established Valgrind fallback.
- Source-overlay negative controls fail as required: overriding the live hub
  limit, permitting framing after overload, and dropping a refused terminal
  observer. Production is unchanged. Overlays/selected fixtures:
  /tmp/zfix-admin-negative/; recipes: /tmp/zfix-admin-negative.mk and
  /tmp/zfix-admin-terminal-negative.mk. Terminal expected failures:
  /tmp/zfix-admin-negative.log and /tmp/zfix-admin-terminal-negative.log.
- Staged installation: /tmp/zfix-admin-install.log. All 26 Zfix public headers
  plus six native setup/stream/transport headers match source (32 total).
  No /opt/z installation changed.
- Installed units: 20 separately compiled consumers/227 scopes,
  /tmp/zfix-admin-installed-unit.log. Installed managed/ingress TCP/TLS: four
  separately compiled consumers/104 scopes, /tmp/zfix-admin-installed-io.log.
  Consumers use staged headers/libraries, including the new admission boundaries.
- Whitespace checks pass for tracked changes and current untracked Zfix files.
  All launched jobs reached terminal state. Windows runtime remains unverified.

This closes the earlier internal Rx-to-Tx administrative-post admission gap.
Connection/setup admission and the remaining public continuation dispatch
boundaries still need audit against the application-owned factory/history scope.
Q015 asymmetric receive-limit advertisement also needs review: controlBody uses
the supplied Tx codec frame limit for Logon tag 383. Deterministic native partial
writes and reset-handshake fault coverage remain open. Stage 6 remains open; stages 7–9 remain open.


## Stage 6 progress — directional Logon limits and native backpressure

Managed Logon tag 383 now uses the effective local receive frame limit,
independently of the Tx codec limit used to encode control output. The
`controlBody` and `controlCommands` APIs require explicit receive capacity;
all consumers pass it directly. No compatibility forwarding overload remains.
Managed endpoints supply `rx.codec.frameBytes` and retain `tx.codec` for output.
Factory coverage checks different directional limits in every managed mode and
refuses an undersized Tx encoding budget. Real managed TCP/TLS loopbacks use
client Rx/Tx limits 4096/1024 and server Rx/Tx limits 8192/2048; both admission
hooks verify the peer advertises its receive bound.

The native transport fixture adds Linux backpressure and pending-close cases.
It constrains actual native Rx/Tx socket buffers through `ZiMxParams`, waits for
both transport readiness callbacks, holds native Rx while application Rx/Tx
continue, and admits two 200 KiB binary frames. A test-executable ELF `send`
symbol calls `SYS_sendto` with unchanged arguments and records actual positive
short writes and EAGAIN. It never caps a syscall request or fabricates an error;
production networking has no new test hooks. After release, every byte and
ordered one-shot frame completion must match. Closing at the first EAGAIN
fails both pending frames once, dispatches no incomplete frame, and drains wire
nodes/bytes. Both transports must reach real EAGAIN. Positive short writes are
required for TCP and for resumed TLS; first-EAGAIN TLS cancellation can precede
a positive short write, and is asserted as such rather than claimed otherwise.
The two added cases are Linux-only; Windows remains unverified.

Validation:

- Root `make -j8`: /tmp/zfix-direction-build.log, clean build.
- Source units: 22 programs/234 scopes and 14 expected schema failures,
  /tmp/zfix-direction-unit.log.
- Full source I/O: 12 programs/174 scopes, /tmp/zfix-direction-io.log.
  Observed native short/EAGAIN counts were TCP resume 81/76, TCP close 2/1,
  TLS resume 79/116, TLS close 0/26. Counts are observations, not fixed timing
  expectations; tests require the applicable nonzero counters.
- A source overlay substitutes the Tx limit for managed receive advertisement.
  Its selected real TCP loopback fails at the new admission check, as required:
  /tmp/zfix-direction-negative.log and /tmp/zfix-direction-negative.mk.
  Source/library production files remain unchanged by this negative control.
- ASan/LSan: control unit 25 scopes and transport/session I/O 92 scopes (117
  total), /tmp/zfix-direction-asan-ctl.log and /tmp/zfix-direction-asan.log.
  Changed control sources are instrumented in the unit run; configured native
  dependency DSOs remain uninstrumented.

- Staged installation: /tmp/zfix-direction-install.log. Changed public headers
  match source; /opt/z is unchanged. Installed unit consumers: 20 programs/227
  scopes, /tmp/zfix-direction-installed-unit.log. Installed managed TCP/TLS:
  two programs/82 scopes, /tmp/zfix-direction-installed-io.log. Both rebuild
  against the staged public API/libraries, including explicit receive capacity.
- Tracked and changed untracked-file whitespace checks pass. User TODO edits
  remain 0 added/2 removed; design/plan inputs are preserved. No commits.
  All launched jobs reached terminal state.

Q015 directional advertisement and the native partial-send test gap are now
closed. Connection/setup admission and public continuation dispatch remain to
be resolved against the application-owned factory/history scope. In particular,
`reloaded()` posts retained history handles before Tx validates its ticket;
application history retention alone does not establish a bound on that handoff.
Reset-handshake fault coverage remains open. Stage 6 and stages 7–9 remain open.


## Stage 6 progress — bounded deferred-history responses

`Managed::reloaded` now returns explicit local handoff admission and takes a
borrowed `const TxHistory &`. It is `[[nodiscard]]`; refusal preserves the
caller's immutable history. AppTx delivery is direct after the retrieval hook
returns. AppRx delivery charges actual body allocation plus both rounded
scheduler/delegate envelopes through the existing per-link/hub administrative
ledgers before posting. Null responses still charge the envelopes; repeated
handles and stale tickets each charge a full reservation. Tx validates the
history/ticket after delivery; handoff admission does not claim valid history or
successful replay. Other threads and AppRx calls after the stopped barrier are
refused without a library post or implicit session fault.

The application owns its storage-to-application-owner return hop. A refused
response remains available for retry or direct Tx completion/cancellation.
Direct Tx completion remains available while Rx administrative capacity is
exhausted or closing has refused new administrative commands. Valid pending
retrievals must still complete before the existing stopped barrier. Accepted
Rx responses hold credits until the return runs on Rx, and stop waits for them.
No new lock, multi-owner ledger, per-response link reference, compatibility
forwarder, application history index or mandatory storage facility is added.

Two new real TCP/TLS cases cover jumbo response refusal under independent
per-link and aggregate bounds, unavailable-history envelope admission, rejection
from main/native owners, retained caller identity, repeated stale responses,
exact-capacity reuse, held Tx consumption, held Rx credit returns, close with
admitted work, and stale direct Tx completion after stop. Link capacity lies
between serialized jumbo bytes and actual heap backing; a source overlay that
charges only serialized bytes fails the admission and refusal-count assertions.

The recovery fixture still uses a distinct storage owner and real stored
history. Alternating responses fill the live hub budget to force refusal, then
restore its previous limit and complete the original retained response directly
on Tx through an application-owned return post. Other responses use bounded
Rx admission. Restart/replay/failover assertions continue to check original
bytes, sequence, SendingTime and exactly-once delivery. Forced-refusal counters
are checked whenever the recovery scenario retrieves history.

Validation:

- Root `make -j8`: /tmp/zfix-reload-final-build.log, clean without compiler
  warnings/errors. Source units: 22 programs/234 scopes plus 14 expected schema
  failures, /tmp/zfix-reload-unit.log.
- Full current source I/O: 12 programs/178 scopes (43 managed scopes per
  transport), /tmp/zfix-reload-verified-io.log. Deferred store refusal/fallback
  preserves the existing 12 recovery scopes per transport.
- ASan/LSan: transport/session fixtures 96 scopes, /tmp/zfix-reload-asan.log;
  current recovery fixtures 24 scopes, /tmp/zfix-reload-verified-asan-recovery.log
  (120 total). Configured dependency DSOs remain uninstrumented.
- Serialized-size-only negative control fails the new local jumbo admission and
  refusal-count assertions as required: /tmp/zfix-reload-negative.log. Overlay:
  /tmp/zfix-reload-negative/; recipe: /tmp/zfix-reload-negative.mk. Production
  headers remain unchanged by the overlay.
- Header-only staged update: /tmp/zfix-reload-install.log (`install-data`);
  no source library relink/install overlaps a running source consumer. Installed
  managed TCP/TLS consumers are separately compiled and run: 86 scopes,
  /tmp/zfix-reload-installed-io.log. Installed `ZfixLink.hh` matches source and
  runtime Zfix/ZfixFmt/Ztcp/Ztls paths resolve to the staging prefix. No /opt/z
  installation changed; unrelated installed unit suites were not rerun.
- Tracked and current changed untracked-file whitespace checks pass. User TODO
  remains 0 added/2 removed; user design/plan inputs are preserved. No commits.
  All launched jobs reached terminal state. Windows runtime remains unverified.

The variable-body history-response handoff is now bounded. Other public fixed
metadata dispatchers and connection/setup admission still need their remaining
scope/ownership audit; `ZmScheduler` has a heap overflow queue, so its ring size
alone does not establish a hard bound on arbitrary repeated public posts.
Reset-handshake fault coverage remains open. Stage 6 and stages 7–9 remain open.


## Stage 6 progress — reset failure recovery and epoch timer cancellation

The reset transport fixture now has 27 scopes per TCP/TLS binary. Fifteen new
cases cover all three managed modes after epoch preparation: close before Start,
real timeout before Start, close after the first reset Logon is locally written
and held as a complete frame on the paused peer, timeout in that state, and
outbound Logon preparation refusal. They retain one application-owned
unsequenced business entry across failure, verify exact assigned/new cursors
before reconnect, and assert no false Ready, no epoch rollback, cleared pending
input/native wire queues, no surviving deadlines and returned owner credits.
Reconnect must continue epoch 2 on physical connection 2, preserve every prior
assignment, recover the missing range when necessary, release the retained
business entry and reject captured old-connection reset commands. Each owner
advances its epoch once, and failed handshakes do not silently arm reset again.

These tests exposed a lost Logon deadline during live epoch turnover. The old
epoch's earlier scheduled deadline was reused for the new epoch; when it fired,
its correctly captured old epoch caused the callback to return without running
the new deadline. Debugger inspection showed an expired LinkTimer and the later
new-epoch Session deadline, with both owner threads idle and no Timeout event.
`RxSide::epochRx_` now cancels all old Rx/gap timers before observing the new
epoch. The emitted session event then arms deadlines with the new epoch. The
same cancellation helper serves physical teardown. Earlier-deadline reuse within
one epoch is preserved; no extra timer identity fields or allocations are added.

Validation:

- Root `make -j8`: /tmp/zfix-reset-fault-final-build.log, clean without compiler
  warnings/errors. Existing unit suites were not rerun for this owner-timer
  change; the updated template is exercised directly by the transport fixtures.
- Full source I/O: 12 programs/208 scopes, /tmp/zfix-reset-fault-io.log. All 54
  reset scopes pass, including all 30 new TCP/TLS failure cases.
- ASan/LSan reset TCP/TLS: 54 scopes, /tmp/zfix-reset-fault-asan.log. Adapter and
  fixture templates are instrumented; configured dependency DSOs remain so far
  uninstrumented.
- Removing only the new epoch cancellation reproduces the missing deadline:
  the stopped/deadline assertions fail. The negative fixture then force-closes
  and completes cleanup. Terminal expected failure:
  /tmp/zfix-reset-fault-verified-negative.log; recipe:
  /tmp/zfix-reset-fault-negative.mk; overlay: /tmp/zfix-reset-fault-negative/.
  Production headers are unchanged by this overlay.
- Header-only staged update (`install-data`): /tmp/zfix-reset-fault-install.log.
  Separately compiled installed reset TCP/TLS consumers pass all 54 scopes,
  /tmp/zfix-reset-fault-installed.log. No /opt/z installation changed, and no
  source library relink/install overlaps a running source consumer.
- Tracked and changed untracked-file whitespace checks pass. User TODO remains
  0 added/2 removed; design/plan inputs are preserved. No commits. All launched
  jobs reached terminal state, including intentionally terminated initial tests
  after debugger diagnosis of the missing deadline. Windows is unverified.

The reset-handshake failure coverage gap is closed for these committed-epoch
transport paths. Public fixed-metadata dispatch and connection/setup admission
still need the remaining scope/ownership audit. Stage 6 and stages 7–9 remain open.


## Stage 6 acceptance audit

The audit uses the explicit Stage 6 deliverables and gate in `zfix_plan.md`,
the Q015 limits scope in `zfix.md`, and the pre-existing untrusted-admission and
one-shot hook contracts in CONTRACTS.md/ENDPOINTS.md. The earlier open notes
about a separate connection quota and arbitrary public fixed-metadata calls
are resolved by ownership scope: applications create/admit links and issue
controller calls; Ztcp/Ztls own native setup. One-shot completions return fixed
values for bounded outstanding operations. The native scheduler overflow queue
is explicitly not claimed as a hard admission bound. Payload-bearing public
handoffs and protocol-originated administrative work have their measured
per-link/hub reservations. Application-owned queues/history/factories retain
application admission, consistent with the native framework and the plan.
Stage 7's typed operation/server-work admission remains a later deliverable.

| Stage 6 requirement | Current implementation and acceptance evidence |
| --- | --- |
| Shared initiator/acceptor TCP then TLS composition | ZfixClient.hh/ZfixServer.hh and NetLink share Managed/Raw; transport fixture runs both, including binary multi-record payloads. |
| Delegate ports/listeners/certificates/verification/mTLS | Native Ztcp/Ztls bases and Params aliases; no FIX certificate/authentication policy. TLS fixtures verify disposable localhost credentials; FIX admission is application policy. |
| Outbound Logon preparation; inbound accept/refuse/defer | Managed TxSide/app hooks; SessionIO loopback includes preparation refusal, admission refusal, asynchronous admission and pending close. |
| Rx/Tx/application ownership | Managed nested RxSide/TxSide, owner assertions and fixed-context posts; SessionIO uses isolated owners, including coincident native/application owners. Storage recovery uses a third application owner. |
| Borrowed receive lifetime; owning off-owner sends | RxFrame extracts pooled backing once into Msg; submit/reloaded/sendRaw admit retained immutable handles. Ingress/Raw/Session fixtures hold owners, refuse excess work and verify returned credits/stop. |
| Hub defaults and overrides for all listed protocol budgets | HubLimits/LinkLimits feed codec/Rx/Tx/Control/Wire/Trace and posted ledgers; Codec/Group/Frame/Rx/Tx unit bounds plus Ingress hubLimits, incomplete, paused, submitBudget and Session adminBudget/reloadBudget native tests. |
| Bounded replay/drain work, admin reserve and native pool tuning | SeqTx/Tx replayBatch and independent control/output reserves; native allocators are template parameters. Unit queue/replay tests and real retained/retrieved failover work verify progress without premature association. |
| ZmEngine lifecycle, absolute owner timers and late callback guards | Native hubs inherit ZmEngine; Managed stores owner timers and epoch/connection/ticket guards. SessionIO heartbeat/setup cleanup and ResetIO failure matrix exercise expiry, cancellation and turnover. |
| Stop ingress/reconnect/timers and drain physical/owner/hook work | Native setup cancellation plus Managed close reference, Rx/Tx drain and credits; pending admission/load/drain/manager cases and delayed storage responses complete before stopped. Raw back-pointers remain valid through that barrier. |
| Failed startup cleanup and restart | SessionIO listenerCleanup/startupCleanup/setupCleanup; Raw setupCleanup covers pre-readiness cancellation and deferred stop repeatedly. |
| Counters and bounded opt-in redacted traces | RxStats/TxStats/WireStats and owner TraceQueue; unit Trace tests and native Ingress tracing plus installed consumers verify bounds, escaping, body/authentication redaction and retained observer lifetime. |
| Multiple sessions/listeners and duplicate attachment | SessionIO attachMatrix shares hubs/listeners/domain, tests local/remote identity collisions and draining replacement. |
| Reconnect/failover, delayed storage and durable restart | RecoveryIO recovery/failover covers warm/cold standby, failed primary retry, held association/native output, one-shot stale guards and fresh stores read from fixture-owned files. |
| Overload, actual partial sends and pending stop | Ingress/Session limit matrices; TransportTest pauses native Rx, observes real kernel short writes/EAGAIN, resumes exact bytes or cancels pending frames once. |
| Continuing sequences and reset-handshake failures | Association hooks assert monotonic sequences within epochs; RecoveryIO checks original history and replay delivery; 54 ResetIO scopes include 30 real failure/reconnect cases and cancellation of old-epoch deadlines. |
| Test ports and cleanup | TESTING.md permits fixture-local ephemeral ports; native tests bind loopback/port zero and use disposable TLS credentials. Each fixture drains before releasing owners. |

Acceptance evidence remains the terminal commands recorded above: latest full
source I/O has 12 programs/208 scopes, ASan/LSan reset and installed reset each
54 scopes; body/queue/codec units and negative controls retain their recorded
evidence. The current descriptor changes also pass 234 unit scopes plus 19
schema failures and the complete 208-scope I/O suite. Stage 6 is complete for
the configured Linux build. This does not complete typed APIs/examples,
independent interop/tools, portability or release gates in stages 7–9.

## Stage 7 progress — catalog selectors and declared responses

`Message<Object, MsgType, Facet, Selector, Responses>` now carries explicit
secondary selection and reply descriptors. `NoDiscriminator` is the unqualified
MsgType case. `Discriminator<Tag, Value>` identifies one root-scoped wire field;
envelope/MsgType fields cannot be used as secondary keys. Same-MsgType entries
must use the same tag with disjoint values. Different tags cannot prove disjoint
wire selection, and unqualified/qualified overlap is rejected rather than
resolved by registration order. Empty secondary values remain ordinary explicit
wire values. Responses are unique type-list entries, with no imposed business
correlation/cardinality policy. Catalog::Contains exposes descriptor membership.

The link/declaration consumer compiles one request with two execution-report
selectors and explicit replies. Schema cases 15–19 independently reject equal
selector values, same-MsgType selectors on different tags, unqualified overlap,
duplicate replies and reserved secondary tags. The existing fourteen invalid
schema cases still fail, and a positive selected catalog compiles first.

Current root build and source verification are clean:
/tmp/zfix-catalog-final-build.log, /tmp/zfix-catalog-unit.log (234 scopes plus
19 expected schema failures), /tmp/zfix-catalog-io.log (208 scopes). Expected
negative logs identify the intended Catalog/Message/Discriminator assertions.
Runtime schema-aware selection, typed send/receive/reply, context/lifetime and
operation admission, application policy examples, and the full Stage 7 gate
remain open.

Staged installation completed after all source jobs terminated:
/tmp/zfix-catalog-install.log. The installed public descriptor header matches
source. Installed unit consumers: 20 programs/227 scopes,
/tmp/zfix-catalog-installed-unit.log. Installed reset TCP/TLS consumers: 54
scopes, /tmp/zfix-catalog-installed-io.log. These compile separately against the
staged libraries/headers. No memory-tool rerun is claimed for pure compile-time
metadata; the preceding Stage 6 owner-timer sanitizer evidence remains intact.
Tracked and changed untracked-file whitespace checks pass, user TODO remains
0 added/2 removed, design/plan inputs are preserved and no commits were made.
All launched jobs reached terminal state. No /opt/z installation changed.


## Stage 7 progress — schema-aware runtime catalog decoding

Added installed public `ZfixDispatch.hh` and `dispatch<Dictionary>(FrameView,
callback, limits)`. Immutable static hashes select primary MsgType and secondary
wire values. Unqualified entries use the ordinary MessageHandler directly.
Qualified entries project the candidates' existing FIX metadata into one common
scan scope, reusing Schema_, TagIndex, parseScope, parseGroup and the codec's
bounds. Incompatible scalar/group or binary-length meaning for one wire ID is
rejected at instantiation. Different payload types/facets and group layouts are
supported. Common structural grammar/duplicate checks precede selected payload
conversion; invalid inactive scalar values do not trigger trial decoding.

Only root-scoped values can select an entry. Length-aware binary scanning
observes a paired data selector after consuming its exact byte count; length
selectors observe the original count token. Empty values remain explicit keys.
The selected schema receives a bounded projection of position records, not a
payload copy. Group order is checked against that selected layout; ordinary
codec ordering remains unchanged. Parse bytes cover both position arrays at
peak, and frame/field/group/depth bounds remain active. Selector observation is
a compile-time schema policy and compiles out of normal codec scans.

A callback runs once after full selected construction/validation and receives
the descriptor, decoded object, header and trailer. Spans borrow caller-owned
frame bytes; the decoder retains no backing. Application errors return unchanged,
including offsets. Unknown/absent selectors, malformed recognized fields and
required/value errors never reach the callback. Envelope fields are also checked
against the dispatch caller's effective field limits.

Validation:

- Root `make -j8`: /tmp/zfix-dispatch-complete-build.log, clean without compiler
  warnings/errors. Source units: 23 programs/242 scopes and 21 expected schema
  failures, /tmp/zfix-dispatch-complete-unit.log. Schema cases 20–21 fail the
  intended wire-layout compatibility assertion.
- Current native I/O: 12 programs/208 scopes, /tmp/zfix-dispatch-io.log. No
  native typed-dispatch integration is inferred from this regression gate.
- Focused dispatch: eight scopes; ASan/LSan recompiles codec sources and dispatch
  templates, /tmp/zfix-dispatch-complete-asan.log. Configured foundation DSOs
  remain uninstrumented.
- Two source overlays fail the intended assertions: removing peak projection
  accounting admits work over the parse budget; removing paired-data selector
  observation loses the binary selector. Terminal expected failures:
  /tmp/zfix-dispatch-complete-negative.log. Recipe:
  /tmp/zfix-dispatch-negative.mk; overlays: /tmp/zfix-dispatch-negative/.
  Production headers remain unchanged by those controls.
- Staged installation after all source jobs terminated:
  /tmp/zfix-dispatch-install.log. All public Zfix headers match source; no /opt/z
  installation changed. Installed dispatch passes eight scopes,
  /tmp/zfix-dispatch-installed.log; the other 20 installed unit consumers pass
  227 scopes, /tmp/zfix-dispatch-installed-unit.log (235 installed scopes total).
  The new standalone consumer resolves staged libZfixFmt/foundation libraries.
- Valgrind installed dispatch: eight scopes, zero errors and zero
  definite/indirect/possible lost bytes. Runtime TAP:
  /tmp/zfix-dispatch-vg.log; memory report: /tmp/zfix-dispatch-vg-memory.log.
  The prior process handle is gone and both logs have terminal summaries.
- Tracked/changed untracked whitespace checks pass. User TODO remains 0 added/2
  removed; design/plan inputs are preserved. No commits. All launched work is
  terminal. No Windows runtime pass is claimed.

Endpoint context/lifetime integration, typed send/reply, typed operation/server
work admission and application policy examples remain open. Stage 7 and stages
8–9 are incomplete; Stage 6's Linux gate remains complete.

## Stage 7 progress — native typed receive and retained context

Managed endpoint defaults now bind their explicit MessageCatalog to
catalog-aware header-prefix inspection and checked dispatch. Known qualified
MsgTypes use the shared catalog wire dictionary to distinguish unknown header
extensions from body boundaries. Unsupported MsgTypes retain conservative
prefix inspection. The selected `fixReceiveRx_(Entry *, const Object &,
const RequestContext &)` overload runs once on AppRx after full validation;
missing overloads return Unresolved tag 35 through ordinary session rejection.
Custom header/business decoding hooks remain explicit application choices.

Installed `ZfixContext.hh` supplies a copyable RequestContext retaining one
pooled enclosing Rx message, without retaining the link or hub or adding field
reference counts. Its immutable snapshot exposes runtime session-owner identity,
physical connection, epoch, sequence, declared version and wire PossDup/replay
indication, plus header/trailer/frame and receive timestamps. Runtime identity
survives physical opens on that owner and is not a restart persistence key.
Owning decoded containers and the callback object have separate lifetimes;
applications retain context with escaped spans and bound their own retention.
No pending business-call registry or completion policy is introduced.

Both managed TCP/TLS session fixtures now use default typed receive and four
secondary-selected lane entries. An added case retains context plus a borrowed
request ID through finalization of both hubs, multiplex shutdown and link
release, then reparses the original frame. Recovery fixtures use typed receive
for new traffic and stored PossDup replay, checking metadata against the active
Rx session even when replay precedes Ready on a new physical connection.

Validation:

- Root `make -j8`: /tmp/zfix-context-final-clean-build.log, no compiler warnings
  or errors. Source units: 24 programs/246 scopes plus 21 expected schema
  failures, /tmp/zfix-context-final-unit.log. The final initializer-order-only
  fixture cleanup also passes its three scopes,
  /tmp/zfix-context-clean-unit.log.
- Native I/O: 12 programs/210 scopes,
  /tmp/zfix-context-final-io.log. An initial run was stopped after uncovering
  two missing lane entries in the converted multi-session fixture; all four
  lane registrations are present in the passing rerun.
- ASan/LSan: 134 scopes total. Context fixture/templates: three,
  /tmp/zfix-context-clean-asan.log. Dispatch/header-prefix fixture plus codec
  source instrumentation: nine, /tmp/zfix-context-dispatch-asan.log. Native
  TCP/TLS transport/session/recovery fixtures: 122,
  /tmp/zfix-context-native-asan.log. Configured dependency DSOs remain
  uninstrumented. No sanitizer errors or compiler warnings in these final logs.
- Staged installation only after all source jobs terminated:
  /tmp/zfix-context-install.log. All 28 public headers match source;
  /opt/z remains unchanged. Installed context/dispatch: 12 scopes,
  /tmp/zfix-context-installed-unit.log.

- Installed native TCP/TLS session/recovery: 112 scopes,
  /tmp/zfix-context-installed-io.log. With the 12 installed context/dispatch
  scopes, 124 installed scopes pass against staged artifacts.
- Valgrind installed context: three scopes, zero errors and zero
  definite/indirect/possible lost bytes; 31,904 bytes remain reachable.
  TAP: /tmp/zfix-context-vg.log; memory: /tmp/zfix-context-vg-memory.log.
- Tracked/changed untracked whitespace checks pass. User TODO remains 0 added/2
  removed; design/plan inputs are preserved. No commits. All launched jobs are
  terminal. No Windows runtime pass is claimed.

Typed send/reply, operation/server-work admission and application policy examples
remain open. Stages 7–9 are incomplete; Stage 6's Linux gate remains complete.

## Stage 7 progress — typed send and outbound selector agreement

Managed client/server links now expose send<Entry>(object, applicationID, error).
Entry must belong to MessageCatalog. Body conversion is synchronous on AppRx or
AppTx using effective Tx limits; the existing submit path owns serialized bytes
across the handoff. Public wrong-owner and zero post-quota refusals return Output
without invoking selection or advancing sequence progress. Successful handoff,
Tx selection, association, publication, local completion and business completion
remain distinct. No managed header or sequence is frozen at submission.

Raw links expose typed send with explicit external-manager Header, TxContext and
administrative classification, retaining the existing raw queue/refusal hooks.
CliLink/SrvLink hoist the session send overloads over native byte-send names.
The raw engine retains its complete immutable Tx codec limits for conversion.

Qualified TxBody factories compare emitted root selectors with their concrete
descriptor, using the common schema/position scanner rather than decoding other
objects. Unmodelled body keys append directly to the output sink. Known empty,
binary data and paired-length selectors retain their wire meaning. Restore
checks exact stored keys without adding missing keys or constructing business
objects. Ordinary restore validation positions end their lifetime before
selector scratch is created; exact/one-byte-short parse budgets are tested.

A nullable static validation function carries header/trailer-key policy through
immutable body/history storage. Tx checks it against the completed current frame
before transport publication. RawOutput::fromFrame applies that envelope policy
too. Unqualified factories avoid selector parsing, and normal body-qualified
output avoids rescanning its completed frame. Actual pooled object accounting
includes the new retained policy field. No runtime-custom schema registry,
pending business-call table or reconnect completion policy is introduced.

Validation:

- Root make -j8: /tmp/zfix-send-final-complete-build.log, no compiler warnings or
  errors. Final source units: 25 programs/250 scopes, 21 schema-negative cases
  and one foreign-catalog send-negative case, /tmp/zfix-send-final-unit.log.
  The latter fails exactly the ZuTypeIn catalog-membership assertion, with no
  unrelated compiler errors: zfix/test/ZfixSendSchemaTest.log.
- Final native TCP/TLS: 12 programs/210 scopes,
  /tmp/zfix-send-final-io.log. Both managed roles use typed send for ordinary
  traffic; raw server responses use the explicit external-manager API. The
  existing forced-Busy retained-body path remains covered. A capture-only
  cleanup also passes 88 session scopes, /tmp/zfix-send-clean-session.log.
- ASan/LSan: 114 scopes total. Selector fixture plus codec/TxFrame sources: four,
  /tmp/zfix-send-final-select-asan.log. Native transport/session fixtures: 98,
  /tmp/zfix-send-clean-native-asan.log. Raw TCP/TLS: 12, terminal raw segment of
  /tmp/zfix-send-native-asan.log (the earlier session compilation warnings in
  that file were removed and its clean session rerun is recorded separately).
  Configured dependency DSOs remain uninstrumented.
- Staged installation only after all source work terminated:
  /tmp/zfix-send-install.log. All 28 public headers match source; /opt/z unchanged.
  Installed units: 22 programs/234 scopes (select/context plus the existing 20
  consumers), /tmp/zfix-send-installed-unit.log. Installed native session/raw:
  four programs/100 scopes, /tmp/zfix-send-installed-io.log. All 334 installed
  scopes pass against the staged artifacts.
- Valgrind installed selector: four scopes, zero errors and zero
  definite/indirect/possible lost bytes; 31,904 bytes remain reachable.
  TAP: /tmp/zfix-send-vg.log; memory: /tmp/zfix-send-vg-memory.log.
- Tracked/changed untracked whitespace checks pass. User TODO remains 0 added/2
  removed; design/plan inputs are preserved. No commits. All launched jobs are
  terminal. No Windows runtime pass is claimed.

Typed reply, operation/server-work admission, application policy examples and the
full Stage 7 gate remain open. Stages 8–9 are incomplete.

## Stage 7 progress — typed reply and explicit application authorization

Managed client/server reply<Request, Response>(context, object, applicationID,
error) checks both catalog memberships and the request's declared response list.
It runs on AppRx, rejects empty/mismatched context before policy or conversion,
then invokes the application's fixReplyRx_ overload. Missing overloads refuse
locally; application errors return unchanged. Authorization feeds the existing
typed send/admission/association/publication path. No inbound sequence/header is
copied to managed output and no business key or completion is inferred.

Managed RequestContext snapshots its selected MsgType/discriminator declaration
as fixed metadata, without another field/input retain. is<Entry>() checks that
wire selection without reparsing. The tagged constructor requires an already
checked entry; the managed dispatcher supplies it. Same-MsgType variant contexts
cannot be accidentally reused as another request variant. Session/epoch, routing,
replay and logical business-key policy belong to the application reply hook.
Context remains reusable; no one-response claim, registry or reconnect outcome
is imposed. Standalone codec input ownership remains unchanged.

Raw reply uses retained RawInput, the same compile-time response constraints and
an explicit fixReplyRx_ manager policy. The manager validates/discriminates input
and supplies the outgoing header, TxContext and administrative classification.
The raw engine supplies no managed sequencing/recovery behavior.

Native managed requests now declare four qualified execution-response entries.
Both roles send requests/reply over TCP/TLS in all three managed modes. Empty,
wrong-MsgType and wrong-discriminator contexts bypass authorization; an explicit
application refusal preserves its error/offset and leaves the context usable
for the successful retry. Raw replies retain external-manager duplicate/low
sequence policy and now use distinct request/response MsgTypes.

Validation:

- Root make -j8: /tmp/zfix-reply-final-build.log, no compiler warnings/errors.
  Source units: 25 programs/252 scopes, /tmp/zfix-reply-final-unit.log. There
  are 24 expected compile failures: 21 schema cases, foreign send, undeclared
  reply and a declared reply outside the catalog. Reply also has a positive
  compile fixture. Cases fail the intended response/catalog assertions:
  zfix/test/ZfixReplySchemaTest-{1,2}.log (foreign also fails send membership).
- Native TCP/TLS: 12 programs/216 scopes, /tmp/zfix-reply-final-io.log. Three
  added server-initiated/client-reply cases per transport cover every managed
  mode; they do not change physical connection roles.
- ASan/LSan: 121 scopes. Context/policy/selection: five,
  /tmp/zfix-reply-final-context-asan.log. Native transport/session/raw: 116,
  /tmp/zfix-reply-final-native-asan.log. Fixture/templates are instrumented;
  configured dependency DSOs remain uninstrumented.
- A header overlay removing context discriminator matching fails exactly the
  selection scope. Production headers remain unchanged. Recipe:
  /tmp/zfix-reply-negative.mk; overlay: /tmp/zfix-reply-negative/zlib/;
  terminal expected failure: /tmp/zfix-reply-negative-tap.log;
  clean control build/result: /tmp/zfix-reply-negative-clean.log.
- Staged installation after all source/control work terminated:
  /tmp/zfix-reply-install.log. All 28 public headers match source; no /opt/z
  installation changed. Installed context passes five scopes,
  /tmp/zfix-reply-installed-context.log. Installed TCP/TLS session/raw consumers
  pass 106 scopes, /tmp/zfix-reply-installed-io.log (111 installed scopes total).
- Valgrind installed context/policy/selection: five scopes, zero errors and zero
  definite/indirect/possible lost bytes; 31,904 bytes remain reachable.
  TAP: /tmp/zfix-reply-vg.log; memory: /tmp/zfix-reply-vg-memory.log.
- Tracked/changed untracked whitespace checks pass. User TODO remains 0 added/2
  removed; design/plan inputs are preserved. No commits. All launched jobs are
  terminal. No Windows runtime pass is claimed.

Operation/server-work admission and application policy examples remain open,
including the complete multi-response/reordered/key/replay example gate.
Stages 7–9 remain incomplete; Stage 6's Linux gate remains complete.

## Stage 7 progress — bounded deferred application work

Installed ZfixWork.hh/.cc adds Rx-owned WorkBudget count/byte admission and
identified Work<Input, Owner> storage. Managed work<Entry>(context, error, extra)
requires a registered matching descriptor and a live Established, non-draining
session; raw work(input, error, extra) uses the external manager's retained input.
Link and aggregate hub WorkLimits default to 4096 operations and 4 MiB. Live
changes cannot undercut current count or bytes; zero disables admission.

Reservations cover rounded native buffer object/jumbo backing, rounded enclosing
input node, work object, retirement delegate and declared application extras.
Native input caches original allocation geometry, so later routing to a different
pool does not undercount. Native read-ahead and deferred work share that charge; standalone Rx retains its
logical fallback and manually constructed contexts without native accounting
refuse work admission. A flag uses existing receive-node padding rather than
adding a second charge word.
Shared backing is conservatively charged per distinct admission; reference copies
share one work charge. Additional owned payload/callback storage is application
accounting and must end before the last work reference.

Last-reference release posts a fixed work pointer to AppRx. Final deletion and
input release occur there before link/hub credits return. This uses typed
intrusive references and the existing owner scheduler, with no locks or business
registry. Physical stop holds its existing teardown reference until work and
pending retirements drain; application stop hooks cancel/finish local processing.
Plain contexts/correlation can persist separately without manufacturing business
completion. Managed drain refuses new work and withholds its Rx proof until work
retires, then resumes protocol synchronization. Normal work does not mark recovery
incomplete or pause readiness. Reset preparation checks outstanding work too.

Validation:

- Root make -j8: /tmp/zfix-work-ready-build.log, no compiler warnings or
  errors. Source units: 26 programs/256 scopes plus 24 expected compile failures,
  /tmp/zfix-work-ledger-unit.log. WorkTest covers count/byte/zero/overflow/tuning,
  reference copies, worker-side release and ZmFn lambda retention.
- Native TCP/TLS: 12 programs/234 scopes, /tmp/zfix-work-ready-io.log. Nine added
  work cases per transport cover background borrowed-input use, per-link/hub
  count refusal, tight/exact byte admission, overflow/undercut refusal, pending
  retirement charges, forced close while holding work, and drain proof waiting
  for retirement, all in modes 4/4.4A/T1.1. Raw binary replies also use a third
  owner and declared worker/result delegate charges.
  A reverse-direction TLS readiness race is handled by the fixture's explicit
  queue policy: Held and Busy bodies remain unsequenced until selection resumes.
- ASan/LSan: 138 scopes. Work fixture/ledger implementation/template ownership:
  four, /tmp/zfix-work-ledger-asan.log. Native transport/session/raw: 134,
  /tmp/zfix-work-ready-native-asan.log. Configured dependency DSOs remain uninstrumented.
- An overlay releases input in the final deref caller before Rx retirement.
  It fails exactly retirement/callable scopes 3–4, without changing production
  headers. Recipe: /tmp/zfix-work-negative.mk; overlay:
  /tmp/zfix-work-negative/zlib/; result: /tmp/zfix-work-negative-final.log;
  expected-failure TAP: /tmp/zfix-work-negative-tap.log. Both debug/non-debug
  deref branches are modified by the control.
- Clang/Linux layout probe reproduces the prior input shape and asserts equal
  RxInput and queue-node sizes with the added native-allocation flag:
  /tmp/zfix-work-layout.cc; /tmp/zfix-work-layout.log. Native read-ahead and work
  now use the same complete allocation charge.
- Staged install after all source/control work terminated:
  /tmp/zfix-work-ledger-install.log. All 29 public headers match source; /opt/z unchanged.
  Installed work/context: nine scopes, /tmp/zfix-work-ledger-installed-unit.log;
  installed native session/raw: 124 scopes, /tmp/zfix-work-ledger-installed-io.log
  (133 installed scopes total).
- Valgrind installed work: four scopes, zero errors and zero
  definite/indirect/possible lost bytes; 31,904 bytes remain reachable.
  TAP: /tmp/zfix-work-ledger-vg.log; memory: /tmp/zfix-work-ledger-vg-memory.log.
- Tracked/changed untracked whitespace checks pass. User TODO remains 0 added/2
  removed; design/plan inputs are preserved. No commits. All launched jobs are
  terminal. No Windows runtime pass is claimed.

Typed sends/replies retain their existing bounded handoff/Tx queue admission;
application-owned unsequenced queues and business registries retain their own
bounds. Application policy examples and the full multi-response/reordered/key/
replay gate remain open. Stages 7–9 are incomplete.

## Stage 7 gate — native application examples and explicit business policy

Completed on the configured Clang/Linux toolchain. Earlier Stage 7 sections
record the catalog, typed send/receive/reply, retained-context lifetime and
bounded admission gates; this checkpoint supplies the application examples and
multi-response/reordered/key/replay gate.

- `example/zfixdemo` runs native initiator/acceptor links, with a shared custom
  U1/U2/U3 catalog. Scenarios cover immediate acknowledgement/fill replies,
  repeating parties, unsolicited events, sequence-keyed session rejection and
  reordered/deferred reports. This dictionary does not claim canonical venue
  NewOrderSingle/ExecutionReport interoperability.
- Calls/results/history are application-owned, finite count-bounded containers;
  history and unsequenced output also have independent byte caps. The client
  registers business keys explicitly and completes only after distinct execution
  IDs for both acknowledgement and fill, or its associated rejection. The policy
  case completes key 1002 before 1001, counts four duplicates and one unknown key,
  and creates no call for that unknown report.
- A repeated request returns the cached result's original execution IDs.
  Unknown replayed keys refuse new business work; a decoded actual PossDup frame
  exercises this rule, including restored business results. Reply authorization
  explicitly checks current runtime session/epoch. Contexts held for deferred
  replies use Work leases acquired before creating fresh results; job backing
  is released before its lease. No library business registry or automatic
  replay-to-new-request policy was added.
- The sequence rejection example resolves `(epoch, outbound sequence)` through
  a Tx-owned association to its explicit business key. It verifies the referenced
  body is U1 and the rejected field is quantity; the business key differs from
  wire sequence. Local transport completion remains a separate counter.
- Tx hooks associate before publication, retain original timestamp/history,
  archive core ownership and provide retrieval. Unit tests exercise byte/count
  refusal, two epochs sharing one wire sequence, immutable replay timestamps and
  history restoration. Actual network resend/restart/deferred storage coverage
  remains in Stage 6's native recovery tests.
- Optional custom Logon credentials preserve managed fields, use checked typed
  restoration and validate on admission. Wrong credentials refuse before any
  business registration/result. Logon bodies are not retained in the example's
  replay store. TLS verifies a localhost SAN certificate against the supplied CA.
- The example has default module `test`, ASan/LSan and installed-consumer targets,
  documented in `example/README.md`; policies are shared with `ZfixDemoTest`.

Authoritative evidence (2026-10-27):

- Normal root `make -j8`: /tmp/zfix-demo-accepted-build.log, no compiler warnings.
  Full `make -C zfix test`: /tmp/zfix-demo-gate-full.log. All 27 unit programs /
  262 TAP scopes and 12 native integration programs / 234 TAP scopes pass
  (496 TAP scopes total); 24 expected compile failures plus positive fixtures
  pass. The same target also passes all 42 example invocations: five scenarios
  plus auth acceptance/refusal, three modes, TCP/TLS.
- ASan/LSan policy: six scopes, /tmp/zfix-demo-gate-asan-unit.log. ASan/LSan native
  examples: 42 invocations, /tmp/zfix-demo-asan-native.log. Example/policy and
  adapter templates are instrumented; configured dependency DSOs are unchanged.
- Installed policy: six scopes, /tmp/zfix-demo-gate-installed-unit.log. Installed
  examples: 42 invocations, /tmp/zfix-demo-installed-native.log. Both compile
  against /tmp/zfix-stage/opt/z; all 29 public headers match source. No new Zfix
  installation was needed, and /opt/z is unchanged.
- Valgrind installed policy plus all installed native examples: 43 processes,
  six unit scopes plus 42 invocations, zero errors and zero
  definite/indirect/possible lost bytes. Reachable bytes range 31,904–32,472.
  Memory logs: /tmp/zfix-demo-gate-vg/*.log. Outcome logs:
  /tmp/zfix-demo-gate-vg-unit.log, /tmp/zfix-demo-gate-vg-tcp.log,
  /tmp/zfix-demo-gate-vg-tls.log. hwloc disables its CPUID backend under Valgrind;
  no protocol/TLS check was skipped.

No Windows runtime pass or performance result is claimed. Stages 8–9 remain
open; default examples use a custom dictionary and do not replace the independent
canonical order/execution interop matrix.

## Stage 8 progress — pinned independent peer setup

- `interop/quickfix.lock` pins QuickFIX/C++ 1.16.0 at commit
  2ce8a60667d95a55cdc57a210f165e19cb757126, source archive SHA-256
  b912d0d387920049144c58916c3e0ab7ca51290fd1bf96e20ed0b707005a5777,
  and canonical `spec/FIX44.xml` SHA-256
  a82655b54363aa9c6d1b2f21f294f1198c0d7125d7b44c26d93d3179f3358425.
- `interop/build-quickfix.sh` verifies both hashes, builds Release with SSL and
  installs under the caller-selected root. Upstream examples/tests are disabled;
  no QuickFIX parser/session code is linked into Zfix. Dependency prerequisites
  and exact paths are documented in `interop/README.md` and `docs/DEPENDENCIES.md`.
- Actual build: /tmp/zfix-quickfix-build.log; prefix /tmp/zfix-deps/install.
  GNU C++ 15.2.0, upstream C++17 mode, OpenSSL 3.5.5. Upstream OpenSSL deprecated
  API warnings are present; compilation/install succeed. Installed config defines
  HAVE_SSL. A separate GNU C++2b consumer loads the verified canonical dictionary
  and checks D/8 and required ClOrdID/ExecID fields:
  /tmp/zfix-quickfix-probe.cc, /tmp/zfix-quickfix-probe-build.log,
  /tmp/zfix-quickfix-probe.log.

Final registration rebuild: /tmp/zfix-demo-registration-build.log, clean with no
compiler warnings. Tracked and explicit untracked source whitespace checks and
shell syntax checks pass. User TODO remains 0 added/2 removed. Design/plan inputs
are preserved; no commits. All launched builds, tests and memory checks are
terminal.

This is dependency setup, not an interoperability pass. The `zfix`/`zfixd` and
`quickfix`/`quickfixd` programs, canonical workloads, four-pairing correctness and
protocol-negative matrix remain open, as do Stage 9 benchmarks and portability.

## Stage 8 progress — canonical workload models and independent codec checks

2026-10-28. Added private reusable `util/libZfixLoad.la` and native-only
`util/libZfixLoadFIX.la`. Utility support now builds before its consumers.

- Shared logical order/report/cancel/replace objects include explicit numeric
  business/order/execution IDs, quantities, two-place decimal prices, FIX times,
  nested parties and byte-preserving text/data shapes. Shape storage is built
  once and reused; seeded scalar generation is identical across peers. Party
  roles/subtypes use canonical values at larger shapes. The shared header has
  no native FIX codec/session include dependency, enforced by compile guards.
- Application-owned bounded Calls distinguishes offered/admitted/refused work,
  acknowledgement, terminal report arrival and business completion. A fill may
  precede acknowledgement; both distinct execution IDs are required for the
  baseline order's completion. Duplicates/unknowns create no new call, the peer
  OrderID is independent of the business key and remains consistent, and a
  local refusal cannot discard a partially answered call. Completed records
  require explicit release; clearing storage preserves unfinished diagnostics.
- Samples reserve caller-count-bounded storage before timing and report exact
  nearest-rank p50/p95/p99/p99.9/max nanoseconds after sorting outside timing.
  Warmup calls do not contribute samples. Monotonic latency and wall-clock FIX
  timestamps are separate. No measured performance result is claimed yet.
- Native FIX 4.4 D/8/F/G declarations and a typed catalog support canonical
  conversion. Explicit application render facets omit absent optional text/data
  fields; zero group counts remain present values, including nested groups.
  No library-wide empty-field suppression or BeginString schema selection was
  introduced. Live cancel/replace completion workflows remain open.
- Optional configure `--with-quickfix=PREFIX` (or pkg-config form `yes`) checks
  actual SSL headers/configuration and linkage. `QUICKFIX_DICT` defaults to the
  prefix's installed canonical dictionary. Peer flags stay within the fixture.
  Normal configuration omits this dependency. An unconfigured codec test fails
  explicitly rather than claiming a skipped peer pass.
- `interop/ZfixQuickfixCodecTest` uses only QuickFIX's independent parser and
  canonical dictionary. Nine native frames cover scalar, text-only, binary-only,
  nested/zero groups, a 32-party/eight-subparty shape with 4096 binary bytes,
  acknowledgement/fill, cancel and replace. All upstream validation defaults are
  retained. QuickFIX serializes them back; the native consumer verifies all
  supported IDs/scalars, exact decimal/time values, binary/text bytes and each
  group entry after the independent ordering/framing changes.
- Negative directions are explicit: QuickFIX's dictionary rejects unknown tag
  9999 with InvalidTagNumber; the native consumer skips that added tag and
  preserves known values. QuickFIX's checked frame constructor rejects a
  corrupted CheckSum with InvalidMessage. No validation switch was disabled.
  Existing native checksum negatives retain their earlier gate evidence.

Evidence:

- Root build /tmp/zfix-load-accept-build.log; source helpers: two programs, ten
  scopes, /tmp/zfix-load-accept-unit.log. Independent codec: one TAP scope over
  nine bidirectional fixtures plus negatives, /tmp/zfix-load-accept-peer.log.
- ASan/LSan helper implementations and native templates: ten scopes,
  /tmp/zfix-load-asan.log. Instrumented native producer/consumer in the independent
  codec pipeline: /tmp/zfix-load-asan-peer.log. Configured dependency DSOs retain
  their configured instrumentation; the QuickFIX library is Release.
- Installed helper consumers: ten scopes, /tmp/zfix-load-installed.log, using
  /tmp/zfix-stage/opt/z framework headers/libraries and private helper sources.
  All 29 public Zfix headers still match the staged installation; /opt/z unchanged.
- Valgrind installed policy/model and native codec: ten scopes, zero errors and
  zero definite/indirect/possible lost bytes. The independent pipeline also
  passes with its QuickFIX validator under Valgrind. All three checked processes have 31,904
  reachable bytes. Outcomes: /tmp/zfix-load-vg-policy.log,
  /tmp/zfix-load-vg-codec.log, /tmp/zfix-load-vg-peer.log. Memory summaries:
  /tmp/zfix-load-vg-policy-memory.log, /tmp/zfix-load-vg-codec-memory.log,
  /tmp/zfix-load-vg-peer-memory.log.
- Default dependency exclusion verified with an actual --without-quickfix
  configure and root build: /tmp/zfix-load-nopeer-configure.log,
  /tmp/zfix-load-nopeer-build.log. Its explicit codec-test fails as required:
  /tmp/zfix-load-nopeer-negative.log. Restored the pinned enabled configuration,
  root build and focused source/peer tests:
  /tmp/zfix-load-restored-configure.log, /tmp/zfix-load-restored-build.log,
  /tmp/zfix-load-restored-unit.log, /tmp/zfix-load-restored-peer.log.
- Normal native builds are warning-free. ELF inspection confirms the independent
  validator has QuickFIX/foundation dependencies and no libZfix/libZfixFmt;
  production libZfix has no QuickFIX dependency. Whitespace and shell syntax
  checks pass. User TODO remains 0 added/2 removed; no commits. All launched jobs
  are terminal. No new platform pass is claimed.

The named process tools, shared workload controls, live four-pairing order and
protocol matrix, fault scenarios and Stage 9 benchmark/portability evidence are
still incomplete. This is an independent conversion checkpoint, not the Stage 8
session interoperability gate. The full objective remains active.

## Stage 8 progress — native process tools and shared workload driver

2026-10-28. Added installable `util/zfix` and `util/zfixd` commands and private
native endpoint support. The configured toolchain remains Clang GNU C++2b with
`-g -DZDEBUG`; this is correctness evidence, not the prescribed Stage 9 release
build or a performance budget.

- Both processes default to verified TLS, FIX.4.4, managed mode 4 and the canonical
  D/8 workload. Other native modes/declared versions and plain TCP are explicit
  controls. The initiator verifies the server's supplied CA and hostname.
- Shared parser/session-independent `ZfixLoadRun` implements per-session warmup,
  measured count/duration, target rate/saturation, in-flight limit, shape and seed.
  Intended submission times expose pacing lateness. Each client session owns its
  call registry and samples; all sessions finish Logon before work starts.
  Explicit 16-bit session namespaces/48-bit counters make order and execution IDs
  distinct across the run's logical sessions without using wire sequence identity.
- The acceptor uses one listener per logical session, with expected sender IDs
  derived from listener index rather than connection arrival order. Ephemeral
  ports are published in a JSON readiness event; the client accepts an ordered
  port list. Each process publishes per-session JSON results and nonzero failure
  status. Counts distinguish warmup, local handoff, actual FIX frames/bytes and
  business completion. FIX byte counters exclude TLS record overhead.
- Pacing/registry state stays on AppRx; unsequenced output and replay history stay
  on AppTx. Public close dispatches to network Rx. Posted sends, output queues,
  result caches and replay history are bounded by workload/count/byte controls.
  Buffers preserve existing transport pools. No application locks were added to
  permit non-owner access.
- Result caching reuses execution IDs for known keys and refuses new business
  work for unknown replayed requests. Tx admission validates identity/capacity;
  committed unsent ID/sequence/body associations remain in the bounded core queue.
  Application replay retention starts in the publication hook before transport
  flush, recording exact body/original time/epoch/sequence. This avoids evicting a
  preloaded candidate before publication when retention is smaller than the
  in-flight window. Local completion releases core history, independently of peer
  acknowledgement; bounded external retention supplies replay or permits gap fill.
  Administrative traffic does not depend on business history-cache admission.
- Shutdown closes listener admission, crosses a network Rx barrier to count
  created links, and waits for each actual logical stop callback before hub
  finalization. Failure signals no longer impersonate completed stop barriers.
  Client DNS is stopped/finalized explicitly. Pacing timers are cancelled on the
  owner; forced local close still drains the normal framework stop barrier.
  Samples move to main-owned reporting storage and sort after runtime shutdown.
- `ZfixLoadRunTest` checks phase/rate/duration transitions with a deterministic
  clock, handoff refusal, FIFO count/byte limits, port-list parsing, moved sample
  lifetime and session ID namespaces. Existing model/codec scopes remain active:
  three programs, 17 TAP scopes total. The utility default test also runs its
  supervised process matrix.

Native process matrix: 18 cases over TCP/TLS, covering scalar, three sessions,
group/binary shape, two-record retention with four in-flight calls, pacing,
duration, modes 4/44A/T11, and intentional cache refusal. The runner checks ready
ports, exact business/report/sample counts, limits, exits and final results.
Expected workload failure must exit exactly 1; instrumentation exit 99 or a
signal is a failure. Sanitizer targets use ASan/LSan exitcode 99 so negative
workload cases cannot hide memory failures. A control wrapper returning 99 after
otherwise valid failure results is correctly rejected.

Evidence:

- Root normal build: /tmp/zfix-native-final-review-build.log, warning-free.
  Latest utility default test: /tmp/zfix-native-review-tests.log (17 TAP scopes
  and 18 process cases). Earlier final implementation build:
  /tmp/zfix-native-release-check-build.log (filename only; still debug flags).
- ASan/LSan source helper/driver/templates: /tmp/zfix-native-delivered-asan.log,
  17 TAP scopes and 18 process cases. Latest strict exit-code matrix:
  /tmp/zfix-native-strict-asan.log. Configured dependency DSOs retain their
  configured instrumentation. Helper sample moves/actor lifetime are instrumented.
- Installed-framework consumers: /tmp/zfix-native-delivered-installed.log,
  17 TAP scopes and 18 process cases; strict matrix:
  /tmp/zfix-native-strict-installed.log. Both complete tools compile against
  staged framework headers/libraries and private utility sources. All 29 public
  Zfix headers still match the staged install.
- Valgrind source tools: /tmp/zfix-native-delivered-vg.log, 18 process cases /
  36 processes. Driver unit: /tmp/zfix-native-vg-unit.log, seven TAP scopes /
  one process. Repeated strict TCP/TLS refusal cases: /tmp/zfix-native-vg-strict.log,
  four processes. All 41 memory logs under /tmp/zfix-native-vg/*.log have zero
  errors, zero definite/indirect/possible lost bytes and no fatal signals;
  reachable bytes range 31,904–32,984. The original matrix and strict refusal
  reruns both use libtool exec with full system/network access.
- Independent harness control: /tmp/zfix-native-harness-control.py and
  /tmp/zfix-native-harness-control.log: exit 99 is rejected despite complete
  expected-failure JSON results. ASan runtime option support was checked in
  /tmp/zfix-native-asan-runtime-options.log.
- Actual staged bin install (after all source jobs terminated):
  /tmp/zfix-native-review-install.log. Both copied commands under
  /tmp/zfix-stage/opt/z/bin pass all 18 cases:
  /tmp/zfix-native-review-staged.log. DESTDIR produces expected libtool warnings
  that /opt/z libraries are not installed; the staged libraries are selected
  explicitly. No library reinstall was needed and /opt/z is unchanged.
- Native tool ELF dependencies contain no QuickFIX library:
  /tmp/zfix-native-tool-deps.log. Independent canonical codec check remains green:
  /tmp/zfix-native-delivered-peer.log. Whitespace, Python syntax and existing user
  TODO preservation checks pass; no commits. All launched jobs are terminal.

QuickFIX process initiator/acceptor programs, the other three live pairings,
independent protocol fault/recovery/reset/restart cases, named cancel/replace
process workflows and Stage 9 benchmark/portability delivery remain open. The
native pair, metadata declarations and codec peer do not complete Stage 8.

## Remaining stages

- [x] 2: metadata/conversion and golden/negative/compile-fail gate.
- [x] 3: framing/binary/output, every-split/fuzz/failure gate.
- [x] 4: control messages/session/ZmPQueue and fake-owner transition gate.
- [x] 5: transmission association/recovery/drain/reset fake-owner gate.
- [x] 6: TCP/TLS/lifecycle/ownership/store-restart gate (Linux).
- [x] 7: typed application API/examples/policy/lifetime gate (Linux).
- [ ] 8: tools/QuickFIX pin/four-pairing independent interop gate.
- [ ] 9: reproducible benchmarks/portability/memory/install/release gate.

## Environment limitations

The cloud runtime skill's environment_status tool is not exposed; no readiness
claim is inferred. Both required PDF downloads succeeded with network access.
No skipped independent peer or platform check is recorded as a pass. Zfb/Zpb
remain outside the implementation edit scope.

## Zi audit — remove FIX-driven transport infrastructure

This checkpoint supersedes the stage 6 cancellable-connect, acknowledged-DNS
cancellation and generic receive-backing accounting checkpoints above. The user
requires submitted connects/resolves to complete through normal continuations;
applications drain them before releasing owners. All changes under zi/ have
been reverted to HEAD, including ZiMultiplex/ZiResolver APIs, ZiConnect.hh,
ZiRxStream accounting/admission, build inputs, fixtures and documentation.
`git status --porcelain=v1 --untracked-files=all zi` is empty.

TCP/TLS initiators use existing ZiResolver continuations through the terminal
result, including all configured address families, and then existing mux connect.
No query/connect cancellation or protocol-imposed setup timer remains. Setup
holds the owner while pending. Stop prevents subsequent submissions; an already
submitted socket completes normally, and late success closes through the native
connection path. FIX owner drain precedes failure observers and applications wait
for stopped before finalizing hubs/multiplexes. SessionConfig.connectTimeout
remains a standalone session-machine input, not a native DNS/socket/TLS deadline.
The native loopback setup fixtures now check ordinary completion/failure while
stopping, preserve restored state and verify retry after drain.

Receive admission stays in Zfix::RxFrame. Its budget uses existing stream
length/count: readable payload plus rounded pool objects, header scratch and
known temporary split/gather reservations. The known FIX frame boundary lets
its allocator callback reserve storage before existing extract() allocates;
exact detach allocates nothing. It no longer claims to measure arbitrary spare
capacity in queued caller buffers. Known jumbo charges for Zfix-owned retained
buffers live in ZfixIOBuf.hh, not Zi. No extra stream state or queue mutation
accounting is imposed on other protocol implementations.

Remaining Ztls changes are native asynchronous setup/drain bookkeeping, opt-in
posted-ingress credits and their Rx drain barrier, protocol receive admission/
resume, discard after disconnect starts, bound-address/late-listener cleanup,
reconnect timer lifetime/generation handling, and corrected server-example
start/stop/final sequencing. Cryptography and certificate verification policy
are unchanged. TCP has the corresponding transport changes.

Validation on current Clang/Linux:
- Zi: 22 programs / 130 scopes; TCP: 3 / 6; TLS: 11 / 67.
- Zfix unit: 27 programs / 262 scopes plus compile-failure fixtures.
- Zfix native integration: all TCP programs and all TLS programs pass. The first
  TLS run exposed a timestamp-length-dependent ingress assertion. The fixture
  now measures and sends the same immutable RawOutput; both ingress programs
  pass (11 scopes each). Other TCP/TLS programs passed unchanged (106 each).
- Independent native process matrix: all 18 TCP/TLS cases pass.
- ASan/LSan: framing fixture (9 scopes), transport/setup fixtures (4 programs /
  122 scopes). Dependency DSOs are not fully instrumented.
- Valgrind: framing and managed TCP session fixtures, zero errors and zero
  definite/indirect/possible leaks; 31,904 reachable framework-cache bytes each.

Logs are under /tmp/zfix-zi-audit/. The initial build log records the corrected
helper-header layering error; build-final.log is the subsequent root make -j8
run. This checkpoint does not claim a new installed-prefix, release, Windows or
ARM validation. No commit or installation was made. The user's TODO removals
and untracked design/plan documents remain unchanged.

HTTP dependent regression checks also pass: H1/H2 hub and client cancellation/
pooling fixtures, 4 programs / 69 scopes (http-regression.log). Explicit checks
find no Zfix whitespace issues and no residual tracked or untracked zi source
changes. The two framing Valgrind/ASan jobs and all fixture jobs have completed.

## Receive-policy correction — ordinary framework buffering

This checkpoint supersedes the receive-budget and transport-resumption portions
of the preceding audit, including the temporary local retention budget. FIX
needs no special native receive policy. All new receive quota/admission hooks,
network posted-buffer ledgers/credits, credit-return jobs, additional teardown
barriers, incomplete-input/backing budgets and input-budget statistics have been
removed from Zfix, Ztcp and Ztls. Their policy-specific tests and documentation
were removed. Ordinary codec/frame/field/group limits and FIX sequence/read-ahead
bounds remain. Submission, administrative, application-work and Tx/history budgets
are independent and remain unchanged.

The added native resumeRx_ entry points are also removed. A Zfix-only adapter
borrows the existing transport-owned ZiRxStream while the link lives. Deferred
application continuations revisit its existing extraction operations; the
borrowed reference is cleared at disconnect. There is no extra receive queue,
transport resume API or buffer accounting. Existing deferred-admission, coalesced
input, raw pause/resume and shutdown fixtures pass through this path.

Zi remains exactly HEAD, including build inputs, tests and documentation.
Remaining native TCP/TLS changes concern asynchronous DNS through its normal
terminal continuation, one active setup per link, ordinary completion/close drain,
reconnect timer ownership/generations, and listener bound-address/late-bind
cleanup. The server examples use start/stop/final correctly. DNS/connect
cancellation and native setup deadlines remain removed.

Current validation, Clang debug/Linux:
- Explicit dependency-ordered -j8 builds: ztcp/src, ztls/src, zfix/src, zfix/test,
  zfix/itest, zfix/util, ztcp/test and ztls/test; no warnings or errors.
- Zfix unit: 27 programs / 260 scopes, plus expected compile-failure fixtures.
- Zfix integration: 12 programs / 220 scopes (110 each TCP/TLS).
- TCP: 3 programs / 6 scopes; TLS: 11 programs / 67 scopes.
- Separate-process TCP/TLS workload matrix: all 18 cases pass.
- ASan/LSan: framing 7 scopes and transport/session fixtures 4 programs / 118
  scopes. Dependency DSOs remain uninstrumented.
- Valgrind managed TCP session: 54 scopes, zero errors and zero definite,
  indirect or possible lost bytes; normal reachable framework caches remain.
- Zi diff/status empty; tracked diff and all Zfix source whitespace checks pass.

Current logs: /tmp/zfix-zi-audit/rx-removed-*. The earlier full root build was
explicitly stopped when the user directed removal of the receive policy; its
partial run is not a current full-build gate. No installation, commit, new
release/platform or installed-prefix verification is claimed. The user's TODO
removals and design/plan documents remain unchanged.

## ZmPQueue pressure audit — findings, not API removals

Audit scope: every hunk currently changed in zm/src/ZmPQueue.hh, the added queue
regressions, and all production callers in ZfixSeq.hh. No queue implementation or
repository test code was changed during this audit. Experimental fixtures and
selective old-behavior header overlays are in /tmp/zfix-pq-audit/. This is a
review of necessity; existing tests alone do not justify adding shared APIs.

Keep demonstrated corrections:
- Bounded shift: the documented exclusive boundary must protect a node after a
  preceding gap and a node crossing the boundary. The regression passes current
  code and fails with only shift(key)'s bounded dispatch reverted. Computing the
  end before unlinking avoids mutation before validation. The unbounded template
  path has no runtime bound comparison.
- Both Tx start overloads: a send/resend callback which calls stop() and returns
  false leaves failed work marked active. Restart must both schedule it and
  reinstate the active flag. Independent fixtures using the old txReset API test
  no-argument and keyed start; both pass current code and both fail when only
  the start flag changes are reverted.
- Empty Rx queue gap detection: pre-existing add/rotate already accepts a
  zero-length heartbeat, advances tail and retains no node. send(10, 0) from
  head 5 must request [5,10). Current code does; restoring stalled()'s old
  count==0 return suppresses the request/timer. This correction has a use case
  independent of the added restore APIs. Keep it even if force recovery is removed.

Flag for removal/refactor:
- extendTail duplicates the pre-existing zero-length heartbeat operation. A
  side-by-side test gives identical head/tail/gap/count, including repeated lower
  boundaries and later real input. The existing route allocates/releases a
  temporary node, unlike the added scalar setter; restoration is a cold path.
  Zfix's actual Rx item has constant length 1, so removal is NOT a drop-in edit:
  adapt recovery metadata/markers without adding hot-path allocation or branching
  merely to avoid this API. Do not claim a fabricated payload is required by the
  existing generic queue: it already discards zero-length heartbeats.
- rxRestore plus recoverGap/forced stalled: tested empty restoration with and
  without a saved requested range against rxReset(processed), startQueuing(),
  a zero-length heartbeat at received, stopQueuing(processed), and dequeue().
  Both routes produced the same boundaries, no retained nodes, the same missing
  range and one retry timer. Existing reRequest() also provides explicit rearming
  of a cached request. The exact cached previous-request span is a semantic
  difference: the alternative rebuilds recovery state instead of reinstalling
  m_gap. Zfix must own/adjust persisted request metadata if that distinction is
  required. Flag the shared restoration/force additions as a driver-refactor
  candidate; do not blindly revert them while current callers still depend on them.
- txRestore's independent send/ack/archive/gap contract is broader than its sole
  production caller, which requires an empty queue and passes (published, ackd,
  ackd) with no gap. Flag the independent archive/gap options and retained-node
  restoration contract as unsupported generalization for this work. A minimal
  independent send/ack restoration operation is justified: txReset sets all
  cursors together; start(key) sets send AND ack together; ackd cannot lower ack.
  These do not restore published>ackd without side effects or replaying old work.

Retain justified narrow additions:
- rewindTail: reset/head rewind clears retained data; del/abort intentionally
  leaves tail and a gap. No existing O(1) operation truncates only the logical
  suffix while preserving earlier history. The bounds and last-retained-end
  checks enforce precisely that contract; it allocates nothing.
- rxGap/txGap: read-only views of private cached request/replay state. queue.gap()
  is missing input, not the requested range; flags do not expose a replay span.
  Reconstructing these in wrappers would duplicate evolving base state. Revisit
  rxGap if Rx recovery refactoring removes its production need.

Additional pre-existing correctness finding:
Both start() paths conflate 'no new task scheduled' with idle. Calling start()
while send work or a resend range/task is already active invokes idleSend() or
idleResend(). Two independent fixtures fail those notification assertions while
confirming the work remains pending and subsequently sends correctly. The same
else-idle pattern exists in the original code. Zfix currently guards the resend
notification with txGap().length(); correct the generic idle contract rather
than treating that workaround as evidence for more recovery policy in wrappers.
This audit records the bug; it has not changed that behavior.

Evidence: the three normal queue programs pass 64 TAP scopes, and each passes
Valgrind with zero errors and zero definite/indirect/possible lost bytes. Current
positive probes pass tail equivalence (1 scope), both restart overloads (2), and
Rx reconstruction/zero-heartbeat behavior (3). Negative controls for bounded
shift, restart flags and the empty-queue guard fail as expected. The new idle
notification probes fail on current behavior (2 scopes); these are findings,
not a passing gate. Compile logs contain no diagnostics after harness setup.
No installation or full dependency rebuild was performed for this read-only
source audit. All audit processes have terminated; unrelated edits are preserved.

## Tx restart audit correction — asynchronous stop completion

The prior claim that callback-driven stop/failure required the added start
failure-flag branches is withdrawn. Its fixture called the low-level flag-clearing
queue stop inline before returning false. Under the required asynchronous owner
contract, failure bookkeeping completes first; queued stop completion then drains
and clears Running/Sending/Resending, and start is queued after that completion.
The original start logic consequently schedules remaining send/replay work through
its normal inactive-phase checks.

A revised deterministic fixture requests stop in each failing callback and
completes it after the sender returns, before invoking start. Send and resend
pass for both start overloads with the original failure branches and with current
code. Logs: /tmp/zfix-pq-audit/restart-async-old.log and restart-async.log, two
scopes each. The old inline-stop failures are not evidence for a valid production
restart defect. zmpqfix.md now specifies reverting those unsupported changes and
preserving the correctly ordered lifecycle. The independent idle-notification
finding requires separate assessment. No production code changed in this turn.

## Tx cancellation audit correction — preserve assigned sequence numbers

The prior rationale for retaining rewindTail is withdrawn. Queue cancellation
leaves an assigned sequence range missing; it does not make those numbers
available for reuse. Existing queue deletion/abort semantics preserve that range.
The sender uses the normal retrieval/gap callbacks, and the application owns the
choice of retained business work, retrieval or gap fill. The library must not
classify canceled assignments separately from unavailable history or add another
cancellation ledger/decision protocol.

zmpqfix.md now removes rewindTail and assignment recycling, replaces the automatic
reconnect unwind justification, and requires monotonic sequence/cursor tests,
zero-payload missing-range restore tests, and ordinary Logon/recovery ordering.
This is a plan correction; no production implementation changed in this turn.

## Gap getter audit correction — use first-gap/request ownership

Neither rxGap nor txGap is required by FIX. Receive recovery uses the first
queue.gap(); cached notification history is not another recovery authority.
For transmission, the peer's resend request supplies its missing interval; the
sender's local queue may be complete and have no gap. Existing resend/gap
completion callbacks supply progress, so that distinction does not justify a
new shared getter. zmpqfix.md now removes both getters and specifies retaining
bounded active-request context in the Zfix operation for replay snapshots,
including partial success/failure and deferred retrieval. No new gap policy,
cancellation classification or production implementation was added in this turn.


## Queue repairs underway — 2026-10-28

This execution supersedes the three preceding read-only audit corrections.
`zfix_plan.md` is aligned with `zmpqfix.md`; all outstanding stage 8/9 delivery
and independent-peer gates remain open. Delegated edits have disjoint ownership;
source changes stop before dependent builds begin.

The shared queue patch now retains bounded shift and empty-node logical-gap
bug fixes, restores the original start failure branches, separately suppresses
false idle callbacks through one compile-time start helper, removes the shared
Rx/tail/private-gap additions, and narrows stopped-empty Tx restoration to
`txRestore(send, ackd)`. Three generic programs pass 72 TAP scopes under the
configured Clang 21.1.8 debug build without warnings. All three pass Valgrind
with zero errors and zero definite/indirect/possible lost bytes. Separately named
focused ASan/LSan+UBSan executables pass the same scopes; configured dependency
DSOs are uninstrumented. Temporary controls restoring the old bounded shift,
node-count early return, false idle callbacks, or early restart ordering fail at
their intended assertions. The properly ordered asynchronous stop/restart scopes
continue to pass with the original start branches. Evidence is under
`/tmp/zfix-pq-repair`.

Zfix is being migrated to Observe plus a scalar restored watermark and saved
request span; request/callback-owned replay progress replaces private gap reads.
Assignment recycling and automatic reconnect unwind are removed. Explicit abort
uses ordinary deletion and releases retained payload charge without reducing the
assigned boundary. Missing content follows ordinary history/gap callbacks with
no cancellation categories. Startup admits the current Logon before draining
older unpublished assignments; existing connection/timestamp data prevents a
second admission when that contiguous cursor catches up. Stale nonreplayable
controls gap-fill through the existing control path. Uncommitted stale load
acceptance still drains its unload completion.

A demonstrated missing-key scan would otherwise traverse a whole restored
assigned range in one send call. A Zfix-only queue view temporarily caps the
visible tail to the existing replayBatch turn size, using one Key and unchanged
physical node operations; successful remaining progress posts another turn.
Independent new-send/replay unavailable prefixes retain only lookup-continuation
state, never reasons for unavailable content. The repair plan records this
extension and its bounded-work regressions.

Current Zfix verification is in progress. Initial source builds passed; an
initial test build exposed fixture-only assertion-scope/overload errors, now
corrected. Do not count historical Zfix passes as current repair evidence. No
root dependency rebuild, install, commit, release, or platform pass is claimed.
`zi` remains unchanged and unrelated user edits are preserved.


### Queue repair verification checkpoint

The first complete Zfix unit run passed 27 programs / 273 scopes plus all
compile-failure fixtures. A subsequent skeptical review identified a priority
Logon callback reentry defect: publication could resume an owner after its hook
had held/detached it. The priority path now establishes asynchronous queued
running state before the external admission hook and does not resume afterward;
a refused admission invalidates those posts through hold. A new deterministic
Tx regression covers both hold and detach. The six affected programs now pass
108 scopes, including the new regression. The full current suite and native
integration are being rerun; the preceding 273-scope pass is a checkpoint before
this final ordering repair, not a claim of final delivery.

Current Rx/Tx sequence focused ASan/LSan and Valgrind checks pass all 39 scopes,
with zero sanitizer diagnostics, zero Valgrind errors and zero definite/indirect/
possible lost blocks. Clang 21.1.8 fixture builds use separately named outputs;
dependency DSOs are uninstrumented. Each Valgrind process leaves 31,904 bytes in
61 framework cache blocks reachable. Three Rx negative controls fail when the
watermark is ignored, saved request discarded, or a stale ticket accepted.
Three additional Tx controls fail when the send tail cap is disabled, accepted
replay progress is not recorded, or independent send/replay lookup caches are
merged. Their unchanged snapshot controls pass. These expected failures are
recorded as controls, never passing suites.

Measured Rx state grows from 208 to 232 bytes: exactly one 8-byte Key and one
16-byte Span, with no extra padding. The original physical queue remains 120
bytes and payload node 80 bytes; its adapter is 128 bytes. Restoration allocates
zero nodes. Contiguous traffic posts no dequeue tasks; draining 64 retained
contiguous items uses the existing 63 continuation posts and one gap lookup.
Evidence: `/tmp/zfix-pq-repair/rx-controls`, `tx-controls`, and per-program
sanitizer/Valgrind logs. No `zi` edits or assignment-recycling API remain.


### Priority Logon / pending history recovery correction

A later review established a real protocol race omitted by the preceding green
checkpoint: a priority Logon can reach the peer while retrieval of an older
unpublished assignment is pending. Rejecting a resend beginning at the old
contiguous publication cursor loses a legitimate peer request. Ordinary FIX
then leaves Rx recovery waiting for a completion notice; the FIX44A lifecycle
Replay path instead reports Invalid and closes the session.

Zfix now retains the request in its existing pending replay span until normal
publication reaches it. One Tx-owned Key records the exclusive actual-admission
high-water boundary, distinct from assigned and contiguous-publication progress.
Only successful normal publication/gap filling or accepted priority Logon extends
it. Open/bounded replay cannot include later unadmitted assignments; submitted
chunks remain capped at contiguous publication, preserving original timestamps
and ordinary first-publication accounting. Saved pending replay may extend
through loaded progress. This does not advance peer acknowledgement, add a
cancellation category or introduce another shared queue API.

New deterministic sequence, Tx, coordinator and paired managed-owner regressions
cover deferred history, real peer ResendRequest, FIX44A 789 recovery, future
pending snapshots, exact wire bounds and eventual readiness in all managed modes.
The five affected programs pass 97 scopes after this correction. Fresh complete
unit/native/memory checks are underway, so the earlier 274/222/120-scope results
are checkpoints before this final correction. No final root dependency rebuild,
release, installed consumer or independent-peer delivery gate is claimed.


### Current repair checks — final source before dependency rebuild

After the pending-history/high-Logon correction, the complete current unit suite
passes 27 programs / 280 scopes plus compile-failure fixtures. Native TCP/TLS
integration passes 12 programs / 222 scopes. Utility tests pass 3 programs / 17
scopes and all 18 separate-process TCP/TLS cases, including the declared expected
quota failures. The final added assertion that no replay job is posted before
publication passes in the 25-scope SeqTx test; removing the published-prefix cap
fails that assertion. Restoring the old contiguous-publication request bound
fails valid early peer replay acceptance. Earlier bounded-scan, independent-cache,
partial-replay and Rx negative controls remain effective.

Focused current SeqTx/Tx/Managed sanitizer and Valgrind runs pass 67 scopes;
the unchanged Rx sequence checks add 17. Final native ASan/LSan runs pass all
120 scopes across four transport/session fixtures. Valgrind Session TCP passes
55 scopes, including all seven timer drain assertions, with zero errors and zero
definite/indirect/possible lost blocks; 32,416 bytes / 62 cache blocks remain
reachable. Focused sanitizer dependency DSOs remain uninstrumented. All build
logs are warning-free. These memory and native handles have terminated.

A full configured Clang debug dependency `make -j8` is now running, after which
representative QUIC/HTTP/TCP/TLS regressions remain. No build type was changed;
this is not a release verification claim. GCC/Clang configured clean release,
independent QuickFIX process pairings, complete benchmark delivery and fresh
installed consumers remain later plan gates. Current compiler/platform inventory:
Clang 21.1.8 and GCC 15.2.0 on Linux x64; a MinGW cross compiler exists, but no
Windows runtime or complete cross-dependency pass has been established. No ARM
cross compiler/runtime was found. No platform pass is inferred from this list.


## Queue repair gate completed — resume stages 8 and 9

The full Clang debug dependency build `make -j8` finished with exit0 and no
warnings/errors (`/tmp/zfix-pq-repair/root-build.log`). Current representative
dependents pass: QUIC20/176, HTTP4/69, TCP3/6 and TLS11/67, total38 programs/318
results with no skips. Expected TLS-negative diagnostics are fixture evidence,
not failed tests. Focused GCC15.2 debug builds/runs of generic queues and affected
Zfix fixtures pass9/186 warning-free, linking current Clang-built dependencies;
no full-GCC-framework/release pass is claimed. All process handles are terminal.

Together with the 27/280 unit+compilefail, 12/222 native integration, 18 process
cases, sanitizer/Valgrind checks and failing negative controls above, this closes
`zmpqfix.md` and the mandatory prerequisite in `zfix_plan.md`. Removed APIs have
zero production references; `zi` remains unchanged and TODO diff0 additions/2
removals is preserved. No install/commit occurred. Overall goal stays active:
real QuickFIX process tools, four-pairing interoperability, canonical additional
workflows, benchmarks, clean configured release builds and installed consumers
remain open under stages8/9. A reviewed dependency patch exists only in
`/tmp/zfix-qf-patch-review`; the active pinned dependency remains unmodified.

## Stage 8 live independent peer evidence — current checkpoint

The pinned QuickFIX1.16.0 dependency is now built with the reviewed repository
patch (`interop/quickfix.patch`, SHA256
`6b9dfb2479650eafa1709566b8009a673a9f643c34e9a796cb98d82806f1f92b`).
The lock/build script checks the archive, affected original sources, patch and
canonical dictionary. The patch fixes actual port0 listener routing, TLS DNS/IP
verification and dependency memory defects; it does not disable dictionary,
checksum or certificate validation. The SDK's initial build retains upstream
OpenSSL deprecation warnings; owned helper/framework builds are warning-free.

Real `quickfix`/`quickfixd` use the foreign parser/session and share only workload,
bounded business cache and measurement facilities with the native tools. Their
linkage excludes libZfix/libZfixFmt. Full four-pairing canonical coverage passes
76 separate-process cases, including cancel/replace, nested/zero groups and
binary values over TCP/TLS. Fault coverage passes104 cases: fragmentation,
loss/replay, duplicates, unknown tags, quantity, checksum and BodyLength faults.
QuickFIX discards corrupt frames and recovers through resend; Native closes.
The harness records those directional outcomes without changing validation.
Logs: `/tmp/zfix-pq-repair/four-pair-canonical.tap` and
`four-pair-faults2.tap`; the earlier fault-oracle failures are preserved.

The security matrix passes40 cases, covering valid DNS/IP TLS, wrong FIX IDs,
wrong DNS/IP certificate names and untrusted issuers in all four pairings.
Dependency probes pass8 TAP scopes and6 strict JSON profiles, including two
actual distinct ephemeral listeners. Valgrind and a separately instrumented
patched SDK pass the probes and live helper workloads. One early provisional
ordinary SDK TCP SIGSEGV had no core and was not diagnosed; it did not recur in
5 debugger runs,20 SDK-ASan fresh connections or100 ordinary fresh TCP runs.
These checks do not establish a cause for that historical anomaly.

Duplicate-session acceptance passes8 cases. Each pauses the original Ready
initiator with acknowledged SIGSTOP, attempts the same session, verifies refusal
with zero admitted work/connections, and confirms the exact original socket
fd/inode is still ESTABLISHED before resuming. The original then completes4
orders/8 reports with connections1. Artifacts and valid TAP:
`/tmp/zfix-pq-repair/duplicate-matrix/`.

Checkpoint/restart/reset utilities and tests are present. The first lifecycle
matrix passes12/16: ordinary checkpoint restart passes every pairing over both
transports; all reset cases using a QuickFIX acceptor pass. Four Native acceptor
reset cases complete the business workload but report stale epoch snapshot
errors at shutdown. This is an application checkpoint/reset defect under repair,
not a passing reset gate. Evidence: `/tmp/zfix-lifecycle-266riwus/` and
`/tmp/zfix-pq-repair/lifecycle-four-pair-first.tap`. Matching probe timeout and
final lifecycle memory checks remain pending. Stages8/9 are still open.

### Lifecycle gate repaired and rerun

The native reset defect was in the utility's application-owned mirror snapshots:
new epoch2 owners were correctly refusing to overwrite epoch1 records. Explicit
reset now retires old Rx and Tx snapshots on their respective owners and old
Tx history, retaining Broker business state. No transport/framework API changed.
The complete fresh-client lifecycle matrix now passes16/16 over TCP/TLS:
`/tmp/zfix-pq-repair/lifecycle-four-pair-final.tap`, artifacts
`/tmp/zfix-lifecycle-7a_yvdqw/`. Every resume continues the actual last wire
sequence and does not send141Y; every explicit reset sends Logon1/141Y in both
directions. First client exit and the acceptor's documented drain/next-accept
barrier precede second client startup. All children terminate successfully and
the server completes8 distinct business orders across2 connections. Earlier
failing artifacts remain available. Final lifecycle memory checks are pending.

### Probe timeout and measurement tools

Six Native-receiver TCP/TLS cases now pass with detailed TestRequest112 and
matching dropped Heartbeat112, explicit Native `FIX session timeout`, exit1 and
completion before the application deadline. Native initiator cases intentionally
remain before Ready with zero business admission while initial synchronization
waits for its echo; SDK initiator cases become Ready before the Native acceptor
times out. This proves the declared initial synchronization/probe timer behavior,
not a post-Ready Native initiator heartbeat-silence scenario. QuickFIX/QuickFIX is
not counted as Native timeout evidence. The first four failures were overly
strict fixture Ready/admission assumptions; no production change was made to
satisfy that oracle. Artifacts `/tmp/zfix-pq-repair/probe-matrix2/`; TAP6 passes.

The two standalone codec measurement programs compile warning-free and each
completes scalar,8x4 groups and4096-byte binary encode/decode smoke measurements
(100 measured/10 warmup). Valgrind both profiles exits0, no errors/lost blocks.
These debug runs verify the measurement path, not release performance. A separate
Linux/glibc dynamic-call allocation/copy observer verifies exactly4 successful
allocation requests/449 bytes and2 copy calls/145 bytes in a cold controlled
fixture. It excludes inlined copies and custom allocator internals; diagnostic
preload runs are kept separate from timing. Final release measurements remain
open.

### Lifecycle memory checkpoint and benchmark interface smoke

On the reset-repaired helper source, Native/Native TCP+TLS resume/reset passes4
ASan/LSan process cases and4 Valgrind process cases, including all fresh-client
checkpoint and wire assertions. All12 Valgrind children report zero errors and
zero definite/indirect/possible lost blocks. Logs:
`/tmp/zfix-pq-repair/lifecycle-native-asan3.tap`,
`lifecycle-native-valgrind3.tap`, and `lifecycle-vg-{client,server}.*.log`.
Native/common helper sources are instrumented in the focused sanitizer run;
framework dependency DSOs are not. These runs precede the intended-submit and
heap-observation additions; the next configured whole-framework sanitizer build
will verify the final measurement code.

The process benchmark interface passes a12-case debug smoke: all6 profiles,
each as a timing run and a separate glibc diagnostic rerun. Correct business
counts, aggregate-only relay faults/chunks, child CPU/RSS and observer outputs
are checked. Four additional local API cases exercise client-only mode against
owned local external services in both mixed directions over TCP/TLS. This is
remote-interface evidence, not a remote-network performance claim. Artifacts:
`/tmp/zfix-pq-repair/bench-debug-smoke/` and `bench-remote-smoke/`.

Standalone codec, heap observer, process harness and report reducer sources are
present. Manifests pin substantive source files, actual executable/source-loaded
DSOs, compiler/flags, host/affinity, SDK patch and canonical dictionary. Missing
case files or empty codec evidence are rejected rather than producing an empty
baseline. Clean configured sanitizer, GCC/Clang release, final measurements and
fresh installed consumers remain pending Stage9 work.

### Final Stage 8 persisted-replay and measurement interfaces

Final lifecycle source now passes24/24 separate-process cases over TCP/TLS in
all four pairings: ordinary checkpoint resume, explicit reset, and forced recovery
of old business messages from the fresh client's saved checkpoint. The utility's
`--recover-rx 1` server fixture restores stale receive progress after first owner
drain while retaining application Broker history. Transcripts require every old
D sequence with identical body SHA and original timestamp, actual ResendRequest
and administrative gap fills, cached server responses, old-report diagnostics
and exactly8 unique completed business orders. A restarted client's new4 orders
complete independently of its old replies.

The first expanded matrix failed3 Native-server recovery cases because a utility
reply backlog was capped at twice current in-flight orders. Historical cached
replies and new-order replies can overlap; that count is not a valid bound.
Removing the redundant count cap retains the existing exact body+node byte
admission budget, which also finitely bounds node count. No framework/transport
code changed. Failing evidence `/tmp/zfix-lifecycle-h3ljxep7/` is preserved.
Final passing TAP: `/tmp/zfix-pq-repair/lifecycle-four-pair-recovery2.tap`.

Current utility unit tests pass3/29, including anticorrelated scheduling/response
samples: actual ack p99=300ns and scheduling p99=199ns, but intended ack p99=400ns,
not499ns. Intended ack/terminal distributions are sampled per call, including
follow-up workflows/deferred finalization. Heap-enabled mixed TCP/TLS smokes
pass4 cases/eight peers, exactly one scoped framework-heap record per peer and
37 measured completions with valid intended distributions. Framework counters
exclude SDK/STL/libc; separate glibc diagnostic observations exclude inlined
copies and custom allocator internals.

A whole configured Clang ASan build has now started using `z.config -D -G -P`,
a separate CONFIG_SITE selecting the fully patched/instrumented SDK, top-level
`make clean` and `make -j8`. All source/compiler/runtime consumers were drained
before the build-type change. No final sanitizer/release/benchmark/install pass
is claimed until the running build and subsequent checks finish.

### Configured sanitizer checks — gap-query repair

The clean configured whole-framework ASan build completed the Zfix subtree and
continued into unrelated modules. Its first unit run passed26 programs but
failed the sequence fixture's work-budget scope. Scratch instrumentation proved
64 ordered messages caused128 gap queries and the final queued drain caused2:
`SeqRx::process` queried the gap before the generic receiver queried it again.
There was no sanitizer memory diagnostic. A debugger's optimized scalar values
were not used as evidence; explicit counters established the cause.

When the physical queue is empty and expected equals its logical received tail,
`process` now clears the saved request/cancels retry directly. The generic
receiver remains responsible for the single gap query, including restored
trailing gaps. Hold/restore/reset callbacks remain authoritative. The unchanged
17-scope fixture passes with64 ordered queries and one final-drain query. No
shared generic API, field or transport change was added. Cross-module include
search confirmed no Zfix header consumers outside Zfix; scratch compilations
were finished and runtime consumers drained before rebuilding the affected
`src` then full Zfix subtree. The ASan build type was unchanged.

Current rebuilt unit suite passes27/280 plus compile-failure fixtures;
configured generic queues pass3/75. Standalone Native and SDK codec checks each
pass3 shapes at1000 measured/100 warmup with ASan/LSan and no diagnostics.
Current utility checks pass3/29 plus18 native TCP/TLS process cases. Final
post-repair native integration, lifecycle, independent canonical and48-case
measurement-interface sanitizer checks are running. Earlier222/24/48 passing
sanitizer artifacts remain clearly labeled as pre-cleanup checkpoints.

## User scope correction — further validation confined to Zfix

The user explicitly directed that no further rebuild/retest beyond `zfix` is
needed. This overrides the broader repository/plan build instructions for this
work. The unrelated remainder of the top-level ASan build was stopped (owned
process group2837930, exit143), and its broad build runner disabled. This is an
intentional stop, not a passing whole-repository build. The completed final-source
Zfix sanitizer results above remain valid:27/280 units,29 utility scopes+18
process cases,12/222 native integration,24 lifecycle,76 canonical independent
cases,48 measurement-interface cases, focused SDK checks, codecs and examples.

Further builds, tests and installed-consumer work are restricted to Zfix.
Existing dependency artifacts will be reused with their actual build flags and
instrumentation documented; no whole-framework release claim is inferred from
module-only verification. Existing checks outside Zfix are historical evidence,
not new work to repeat. No installation or commit occurred during the stopped
build.

### Dependency reuse audit under the corrected scope

The older uninstrumented `/tmp/zfix-stage/opt/z` cannot safely supply current
Zfix builds. Read-only comparison finds6 changed public headers among234,
including Zi connection signatures and receive-stream layout. Its libZi exports
connect functions with the removed ZiConnectReq argument and lacks the current
signatures. This is an ABI incompatibility, not a missing compatibility shim to
add. Evidence: `/tmp/zfix-pq-repair/staged-dependency-audit.json`.

Current matching source-tree dependencies are already ASan-instrumented. Fresh
Zfix-only staged installation/consumer checks are proceeding using copies of
those exact existing dependency artifacts, without dependency make/install/test
or global `/opt/z` writes. Instrumented measurements cannot be described as an
uninstrumented whole-framework release baseline. The dependency limitation and
actual flags will accompany measurements; the unsafe older staging is not used.

### Scoped installed boundary and optimized-build findings

The Zfix-only staged ASan installation passes all16 existing installed-consumer
targets:40 TAP programs/521 scopes,18 Native TCP/TLS process cases and 42 example
scenarios. Exact stage `/tmp/zfix-final-asan-install.zdja4jqa/opt/z` contains275
matching public headers;9 dependency DSOs were copied byte-identically and only
Zfix DSOs were installed/relinked. Loader audit resolves every Z DSO within the
stage. No dependency module was rebuilt, installed or tested; `/opt/z` is absent
and unchanged. Logs: `/tmp/zfix-pq-repair/final-asan-installed-*.log`.

Current dependency debug ABI requires retaining `ZDEBUG`: it changes object
bases, lock/global fields and I/O parameter layouts. `NDEBUG` does not undo those
branches. A GCC15 optimized Zfix frame consumer successfully runs11 scopes with
ASan/LSan against the existing Clang-ASan libraries, warning-free; complete
flags/loader/hash evidence is `gcc-runtime-probe-result.json`. The scoped GCC
module build uses-O3/-DNDEBUG while retaining-ZDEBUG/-fsanitize=address, and
will be reported as optimized/instrumented, not an ordinary release framework.

That build exposed a real formatter scratch ownership bug: ASan can place small
stack storage outside the thread's native stack range, causing ZmScratch's
address inference to attempt a heap free. Zfix now uses its framework array with
explicit stack/heap ownership, the same named PrintHeap pool/tuning, and RAII.
No dependency code or sanitizer check was changed. The original failing GCC
control test now passes26 scopes. Initial failure is retained in
`gcc-opt-ctor-check.log`; current rebuild logs are `gcc-opt-scratch-*.log`.
Two test helpers also construct sized strings directly instead of resizing a
default string; this removes GCC bounds diagnostics without suppressions.

A separate current-source, uninstrumented Zfix-only scratch build safely reuses
the compatible normal foundation subset (Zu/Zm/Zt/Ze/Zf only). All185 relevant
headers match except inline queue methods; queue data-member declarations match
and required imports exist. No incompatible staged transport or old Zfix library
is used. Strict Valgrind passes Codec18/Frame11/Seq17/SeqTx25, total71 scopes,
zero errors and zero definite/indirect/possible leaks with no suppressions.
Exact commands, source/ELF/dependency hashes and scope:
`/tmp/zfix-pq-repair/vg-current-zfix/summary.json`. This does not claim current
uninstrumented networking/SDK coverage.

The new Linux x64 LD_AUDIT diagnostic observer forwards unchanged symbol targets
and measures selected PLT allocation attempts/copy-call entries. Its controlled
ASan fixture and existing Native codec preserve successful results; an intentional
use-after-free still reports and exits99. It neither replaces allocators nor
disables sanitizer checks. Direct/inlined/custom-internal calls are excluded;
copy entry sums can include nested calls. This diagnostic mode is separate from
timing. Its harness integration awaits scoped optimized module verification.

### Scoped GCC checkpoints and inline-interposition guard

The GCC optimized/instrumented unit suite passes27 programs/280 scopes after
separating same-line TAP call sites in the Zfix Trace/State fixtures. The foreign
load fixture receives the same local correction; no shared test framework was
changed. Earlier functional successes with duplicate/out-of-order TAP IDs remain
failed harness evidence, not passing suites. Current unit log:
`/tmp/zfix-pq-repair/gcc-opt-tap-unit.log`. Focused independent checks pass codec1,
load/checkpoint5 and dependency8 plus six strict JSON profiles with leak detection
enabled: `gcc-opt-interop-tap-build.log` and `gcc-opt-interop-tap-focused.log`.
These checks precede the inline-visibility adjustment below.

The LD_AUDIT harness integration subsequently passes12 cases: all six Native/
Native TCP profiles, each without the observer and as a separate audit diagnostic
rerun,30 measured/3 warmup. Exact business counts,24 framework heap events,
12 audit records and28 child resource records pass schema checks; all children
exit0 with no ASan/LSan diagnostics. Audit rows preserve allocation-attempt,
free-including-null, nested copy-entry and ASan-subset semantics and are excluded
from timing/reducer distributions. Artifacts:
`/tmp/zfix-pq-repair/gcc-optimized-audit-smoke/`. This concurrent-QA smoke validates
the interface; it is not the final isolated measurement run.

An initial GCC integration run failed Session TCP with an ASan array-cookie read
at1/55 scopes (`gcc-opt-itest.log`). The executable exported a weak default
`ZmSchedParams::nThreads(unsigned)` inline implementation; it interposed the
existing Clang-ASan libZi implementation, whose array-cookie access follows its
own instrumentation. A minimal same-source pair reproduces exit99 with the
exported inline and exits0 with `-fvisibility-inlines-hidden`; the latter omits
that executable dynamic symbol. The real separately named SessionHidden consumer
then passes55/55 scopes with leak checks unchanged. No dependency source/artifact,
array-cookie poisoning or sanitizer check was changed. Evidence:
`/tmp/zfix-pq-repair/cookie-audit/` (`base-run.log`, `hidden-run.log`, dynamic-symbol
audits, `session-build.log`, `session-run.log`). This is compiler/instrumentation
interposition evidence, not a Zfix payload-corruption diagnosis or a general
proof of all cross-compiler combinations.

A new clean **Zfix-only** GCC build using that visibility flag is running; it is
not yet a completed build/runtime gate. Existing dependencies remain frozen.
Clang optimized verification and final isolated measurements follow the current
GCC checks. No further top-level/dependency build or retest is authorized.

### Final delivery artifact checkpoint

The earlier16-target installed-consumer pass remains valid for its recorded
source. Current `ZfixFmt.hh` differs because of the explicit PrintBuf ownership
repair, so it does not establish the final installed header/library boundary.
A fresh dependency-only stage is prepared at
`/tmp/zfix-final-opt-install.ire92o50/opt/z`:245 headers and9 DSOs copied
byte-identically, with `comparison-manifest.json` in its parent. Zfix has not yet
been installed there. Final Zfix-only install and affected installed consumers
remain pending; no dependency make/install/test is needed.

The isolated final runner `/tmp/zfix-pq-repair/final-measurements.sh` is prepared
but has not run. It declares2000 measured/200 warmup orders,3 repeats over all
four pairings, TCP/TLS and six profiles, plus separate audit diagnostics; codec
runs use10000 measured/1000 warmup calls,3 repeats. It must wait for compiler and
QA consumers to terminate. Its intended output is a legitimate **instrumented
optimized baseline**, with actual dependency/ABI/compiler flags and host effects
disclosed, not an ordinary uninstrumented release or a production-throughput
claim. Upper-tail support is limited by each run's finite sample count; repeats
do not merge those distributions. Final reports/budgets and compact raw evidence
are still pending. Platform coverage/disclosures and final documentation/data
registration complete Stage9 under the corrected scope.

### Scoped GCC optimized verification completed

The Zfix-only GCC build using
`/tmp/zfix-pq-repair/config-scoped-gcc-opt.site` is complete. Flags include
`-O3 -DNDEBUG -DZDEBUG -fsanitize=address -fno-omit-frame-pointer` and
`-fvisibility-inlines-hidden`; existing Clang-ASan dependencies and patched SDK
remain unchanged. The full scoped build exited0. Two bounds diagnostics in the
independent codec test helper were then removed by direct sized construction,
after its compiler finished; the final whole-Zfix incremental `all` exited0
without warnings (`gcc-opt-inline-final-build.log`). No dependency module was
built, installed or tested.

Current GCC checks pass with leak detection enabled:

| Check | Passing evidence under `/tmp/zfix-pq-repair/` |
| --- | --- |
| Unit27 programs/280 scopes plus schema fixtures | `gcc-opt-inline-unit.log` |
| Native TCP/TLS12 programs/222 scopes | `gcc-opt-inline-itest.log` |
| Utility29 scopes and18 native process cases | `gcc-opt-inline-util.log` |
| Four-pairing TCP/TLS resume/reset/persisted recovery24 cases | `gcc-opt-inline-lifecycle.log` |
| Independent codec1, load/checkpoint5, dependency8 and six JSON profiles | `gcc-opt-inline-focused.log` |
| Four-pairing canonical76 process cases, including eight expected quota refusals | `gcc-opt-inline-canonical.log`, `gcc-opt-inline-canonical/` |
| Application examples42 scenarios | `gcc-opt-inline-examples.log` |

These supersede the earlier provisional GCC TAP/cookie failure checkpoints for
the listed gates. Full exact configurations and hashes are preserved in
`gcc-opt-final-manifest.json`:8 actual programs,36 loaded libraries and147
substantive source-file hashes. GCC's libasan runtime and the existing
Clang-instrumented framework/SDK are identified separately; successful scoped
runtime evidence does not imply a homogeneous or uninstrumented framework build.
All GCC compiler/runtime actors terminated before changing configuration.

A clean **Zfix-only** optimized Clang build has now started against the same
existing dependencies, without the GCC-specific inline-visibility adjustment.
It is active, not a passing gate. Final Clang QA, isolated instrumented
measurements, refreshed Zfix-only installed consumers and published artifact/
platform reconciliation remain pending. Earlier measurement smokes validate
interfaces and diagnostics; they do not replace the final controlled baseline.

### Scoped Clang optimized verification completed

The clean Zfix-only optimized Clang build finished with exit0 and no warnings
(`clang-opt-build.log`). It uses `-O3 -DNDEBUG -DZDEBUG -fsanitize=address` with
frame pointers and the existing Clang-instrumented dependency/SDK artifacts;
the GCC-specific inline-visibility flag is absent. No dependency module was
rebuilt or retested.

Current Clang checks pass with leak detection enabled:

| Check | Passing evidence under `/tmp/zfix-pq-repair/` |
| --- | --- |
| Unit27 programs/280 scopes plus schema fixtures | `clang-opt-unit.log` |
| Utility29 scopes and18 native TCP/TLS process cases | `clang-opt-util.log` |
| Native TCP/TLS12 programs/222 scopes | `clang-opt-itest.log` |
| Independent codec1, load/checkpoint5, dependency8 and six JSON profiles | `clang-opt-focused.log` |
| Four-pairing canonical76 process cases | `clang-opt-canonical.log`, `clang-opt-canonical/` |

The152 canonical peer diagnostic logs contain no sanitizer failures; eight
quota cases are declared successful negative expectations. Loader evidence
`clang-opt-{native,quickfix}-ldd.log` identifies the unchanged instrumented SDK
and framework dependencies. System crypto and other external libraries remain
uninstrumented. These are scoped optimized/instrumented results, not a new
whole-framework or ordinary uninstrumented release claim. Together with the
completed GCC checks, the Stage9 compiler-verification gate is complete.

The final Clang-built LD_AUDIT observer target is warning-free
(`clang-opt-audittrace-build.log`). Its controlled fixture records exactly5
allocation attempts/743 requested bytes,4 copy entries/80 bytes, with2 ASan copy
entries/36 bytes as a subset and no request overflow
(`audittrace/clang-fixture.json`); its error log is empty. The intentional bad
control still reports ASan heap-use-after-free and exits99
(`audittrace/clang-bad.log`). No allocator replacement, unpoisoning or memory
check suppression was introduced.

Final Zfix-only staged installation and the16 existing installed-consumer
targets are now running; they are not yet a completed final boundary gate.
Final isolated measurements and their compact report/raw-artifact bundle also
remain pending. No measurement run is accepted while compiler/QA consumers are
active. The current-source normal-format/sequence Valgrind71 evidence remains
limited to its verified foundation-only closure; network/SDK memory evidence is
the separate instrumented scope above.

## Current platform scope — Linux x64 only

The user explicitly removed ARM and Windows from this delivery's scope. All
current compiler, runtime, memory, installed-consumer and measurement gates are
Linux x64 only. Earlier platform inventories and statements about untested
ARM/Windows configurations are historical facts, not remaining work or acceptance
gaps. No new cross-platform build, runtime or portability work is required.

Final current Zfix installation and all16 installed-consumer targets now pass:
40 TAP programs/521 scopes,18 native TCP/TLS process cases and 42 example
scenarios, with ASan/LSan enabled and no diagnostics. The refreshed stage contains
all275 current public headers and9 byte-identical reused dependency DSOs;
`/opt/z` remains absent. Evidence:
`/tmp/zfix-pq-repair/clang-opt-installed-summary.json` and its adjacent installed
logs. No dependency module was rebuilt, installed or tested.

The controlled baseline run has exited0 with288 passing process cases
(144 timing,144 separate audit diagnostics) and18 codec rows from six invocations.
Independent raw-data verification passes (`final-clang-opt-measurements/process/
verification.json`): all288 cases and18 codec rows,576 framework-heap records,
672 resource records and288 audit records meet their declared assertions.
All measured-source, program and DSO hashes match the measurement snapshot;
679 diagnostic files contain no sanitizer reports. The72 nonempty SDK logs
contain the expected certificate-verification diagnostic.

The repository report/archive publication and affected checks for the subsequent
demo helper filename correction are pending. Stage8 is complete on Linux x64;
Stage9 remains open for that final publication, affected-check completion and
documentation reconciliation. Final source,
build and instrumentation limitations remain as recorded above.

## Post-measurement directory-role correction

The user requires examples to demonstrate the API without serving as regression
tests. The rich policy fixture is now located in `test`; native application and
process fixtures belong in `itest`. Standalone native client/server examples
are being prepared separately. The three isolated workload suites have moved
from `util` into `test`, retaining their29 scopes, ASan and installed-consumer
entry points. They compile their utility sources directly so the clean
`src`, `test`, `util` traversal has no forward dependency. The explicit
`interop/nativecodec` frame fixture shares its frame writer with mapping tests;
unit assertions remain in `test`. Native process-suite migration is being
completed in `itest`; `util` retains tools and reusable supervision functions.

These source-layout changes still await coordinated builds and affected checks.
They do not alter the measured native/QuickFIX peer or codec implementation.
The baseline and its source manifest remain the original measurement snapshot;
record the later file moves/extractions separately. No unchanged benchmark rerun
or dependency-module rebuild is required.

### Directory-role verification checkpoint

The relocated isolated workload and business-policy fixtures build without
warnings and pass four TAP programs /35 scopes with ASan/LSan enabled
(`/tmp/zfix-pq-repair/post-layout-{unit-build,unit-test}.log`). The new explicit
interop/nativecodec fixture builds and passes the independent QuickFIX codec
check (`post-layout-{interop-build,codec-test}.log`). The installed workload
consumer target also passes against the existing staged headers/libraries
(`post-layout-installed-load.log`). These checks cover the relocation and
extraction; they do not yet verify the subsequent common-Link refactor.

Interop now consumes reusable process supervision in util directly. The native
integration suite has a thin ZuTest driver invoking its Python fixture, so
its TAP entry point follows the repository's test convention. Canonical
examples remain build-only programs. Their first compile exposed a nonexistent
exception-header include; this is corrected to ZePlatform.hh, with successful
example builds and runtime verification still outstanding.

Before the common-Link refactor, all six measured program ELF hashes and all
36 linked-library hashes still match the original benchmark manifest. The
updated post-measurement-layout.json records actual moves/extractions and
replaces the superseded filename-only rename account. Original measurement
manifests remain unchanged. Architecture refactoring, affected runtime and
installed-consumer checks, and report/archive publication keep Stage9 open.

The extracted native integration fixture now passes all18 TCP/TLS process
cases, and the interop matrix's direct util supervision passes eight smoke
cases across all four peer pairings. Both processes exited0. Their retained
case records and logs pass count/status/sanitizer checks in
`/tmp/zfix-pq-repair/post-layout-runtime-summary.json`. These runtime checks
used the unchanged measured executables before the Link refactor; the new
ZuTest wrapper and refactored executable consumers still require their own
builds/checks.

### Common Link structure implemented

The transport-independent owner is now Link. CliLink/SrvLink directly inherit
the selected engine, the shared internal LinkHooks_ and stream input adapter;
the former NetLink engine wrapper is removed. Raw retains its independent
framing/output engine. Four explicit using declarations select the native
attachment and trace hooks over engine defaults. No compatibility aliases,
fields, locks or allocations were added. The namespace body in ZfixLink.hh
exactly matches the prior installed header after the owner identifier rename
(`link-refactor-proof.json`).

Current library and unit-binary builds pass without warnings
(`link-src-build.log`, `link-unit-build.log`). Full unit/schema verification
and the current native utility, integration and example builds are underway.
The staged headers still describe the previous snapshot until a new scoped
Zfix installation and affected installed-consumer checks are performed.

### Refactored source runtime gates

Current src/test/util/itest/interop/bench builds pass without warnings. The
complete unit/schema gate passes30 TAP programs/309 scopes; native integration
passes14 TAP programs/225 scopes (222 transport/session scopes plus two rich
policy-driver scopes covering42 process scenarios and one native-runner scope
covering18 cases). The current four-pair canonical matrix passes all76 cases.
ASan/LSan diagnostics are clean. Logs are link-{unit,itest}-test.log,
link-canonical.log and link-runtime-summary.json under /tmp/zfix-pq-repair.

The final canonical example sources now include typed Parties groups in the
order and both reports, a separate unsolicited Tick, sequence-keyed rejection
diagnostics, missing-history policy and optional admission/auth extension.
Their warning-free build and actual TCP/TLS pairs pass; both roles exit0 with
ACK+Fill+Tick and empty stderr (canonical-example-pairs3). Business completion
remains application-owned and depends on both responses. Identical-source
installed examples and the other fresh installed consumers remain underway.

Refactored native peer machine code differs from the original measured image;
its text shrinks6224 bytes. Both codec and independent peer text sections are
unchanged; library text/read-only/relocated-data sections match the previous
staged libraries (link-{program-comparison,library-sections}.json). New final
measurements will capture the current images after compiler/QA consumers are
quiescent. Original measured manifests and data remain unchanged.

### Standalone example gate completed

Identical final example sources compile against the refreshed installed Link
headers/libraries, then both TCP and TLS pairs pass with both actors exiting0.
Together source and installed runs cover eight actor exits and eight empty
stderr files under ASan/LSan. Retained commands/source/ELF/loader hashes are in
canonical-example-installed/provenance.json and commands.json; pair records
are canonical-example-{pairs3,installed-pairs}/results.json. Root rechecked
current source hashes, all actor exits and stderr lengths. No example test
entry point was added; regression assertions remain in test/itest. The required
group/event/correlation/reject/recovery/admission demonstrations are present
in the small shared schema plus standalone client/server.

### Current installed-consumer gate completed

All15 post-Link installed targets pass against the frozen temporary prefix
/tmp/zfix-link-install.yl143h1q/opt/z. Aggregated TAP evidence is42 programs/524
scopes, plus the nested18 native process cases and 42 rich policy scenarios.
All target exit codes are0; compiler warnings and sanitizer diagnostics are
absent. Independently aggregated results and exact commands/log paths are in
link-installed-verified-summary.json. The separate installed canonical examples
also pass TCP/TLS as recorded above. All30 current Zfix public headers match
source; nine reused dependency DSOs match the previous stage byte-for-byte,
and /opt/z remains absent (link-install-verification.json).

Final current-source GCC and Clang gates are now running through z.config with
the existing compatible dependencies. Only Zfix is cleaned/built/tested. The
current unit/native/interop/installed/example source is frozen; fresh current
measurements will begin after this chain and other QA processes are terminal.

### Final compiler and corrected measurement gates

The complete post-Link GCC and Clang optimized/ASan module gates pass, with no
compiler warnings or sanitizer diagnostics: each has30 unit programs/309 scopes
plus schema fixtures,14 native integration programs/225 scopes, and eight
four-pair smoke cases. Nine dependency DSOs remain byte-identical; only Zfix
was cleaned/built/tested (link-final-compiler-summary.json).

The first post-Link measurement completed288 process cases successfully, but
the independent audit rejected codec frame-size equality. The benchmark had
sampled wall time separately in each engine/shape; Native FIX<-9> trims trailing
fractional zeros whereas QuickFIX FIX<9> pads them. Two Native rows consequently
encoded both timestamp fields one byte shorter. This was a benchmark-input
defect, not a receive or session requirement. codec.hh now uses the same fixed
20261028-12:34:56.123456789 input outside the measured loop. Targeted GCC/Clang
benchmark builds and both three-shape codec runs pass with identical frame
lengths170/910/4281 and empty sanitizer stderr. All protocol executables and
linked libraries remained unchanged (codec-input-fix-summary.json).

A wholly fresh current-source measurement snapshot now passes the unchanged
strict independent audit:288 cases (144 timing and144 separate diagnostics),
18 codec rows,576 framework-heap records,672 wait4 resource records,288 audit
records with zero overflow,679 clean stderr records, all48 measured budgets,
and current hashes for155 sources, six executable images and36 loaded libraries.
The final root is /tmp/zfix-pq-repair/final-codec-clang-opt-measurements; its
process/verification.json reports passed. Earlier snapshots and the rejected
codec audit are preserved separately, with no data substituted or merged.

The SHA-pinned publication index now validates642 retained correctness/provenance
artifacts, including scoped Valgrind71 and the earlier controlled observer/UAF
evidence. Publisher preparation passes without writes. Actual dated archive
publication, checksum/distribution checks and final documentation audit remain
open at this checkpoint.

### Final published delivery

The final archive contains6790 regular files and6789 verified checksums, with
no generated private keys or binary build artifacts. SHA256:
`bb066944cf075af408fed5210c7199182b306e42bf7152adfb23092e9b4a8587`.
Size:780980 bytes. Current results, pre-Link evidence, the rejected variable-time
codec dataset and their verdicts are retained in separate namespaces.
The final publication index pins646 correctness/provenance artifacts.

An unpacked-archive check exposed a verifier scope bug: recursive root log
selection included historical and correctness logs as current timing stderr.
Selection now covers the current process tree and top-level codec/load logs;
all679-log/72-declared-SDK-diagnostic checks and all other invariants remain.
The old verifier, old passing verdict and failed unpacked verdict are retained.
The packaged verifier now passes strict current-source verification after
unpacking. The report reproduces exactly after normalizing its single output
location footer; numeric data is unchanged. Archive integrity/reproduction
evidence is archive-verification.json and unpacked-final-reproduction.json.

All58 generated report table/header rows match the repository report. The
module distribution registration check passes; final copied payload hashes
are recorded in dist-final-verification.json. No further build or workload is
required, and no normal uninstrumented networking/SDK or cross-platform pass
is inferred.

## Implementation pressure audit — 2026-10-29

The separate post-delivery audit is complete. [AUDIT.md](docs/AUDIT.md) records the
full guideline coverage, concrete cost removals, justified storage/validation
boundaries and current verification. It covers all 126 C++ files across the
seven source roles and the plan-owned transport integration hunks.

The current Clang optimized/ZDEBUG/ASan build and 30-program/309-scope unit/schema
gate pass. Native integration passes 14 programs/225 scopes; independent codec,
SDK load/dependency checks, all 76 canonical process cases, standalone TCP/TLS
examples and 100,000 codec fuzz runs also pass without sanitizer findings.
The strengthened fixed-writer capacity test passes after its final rebuild.
The trace-rotation fixture now uses actual allocation charges to size its budget,
preserving all previous behavior assertions. No production correction was
needed for that test failure.

Existing dependency binaries were reused, and runtime scope remains Zfix on
Linux x64. The 2026-10-28 benchmark archive and numeric results are unchanged;
they describe their captured pre-audit source, not these repairs. Current
mechanical copy/allocation reductions do not establish a latency improvement.
Detailed inventories, before snapshots and terminal logs reside under
`/tmp/zfix-perf-audit/`.

## Link/session/connection state audit — 2026-10-29

Completed the member-state follow-up documented in [STATE_AUDIT.md](STATE_AUDIT.md).
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

## Historical pre-repair combined audit gate — 2026-10-29

The current workspace now passes the combined member-layout and performance-path
gate. [STATE_AUDIT.md](STATE_AUDIT.md) records the corrected packed scheduler
group and native Rx storage: complete managed TCP/TLS links shrink another
256 bytes from the earlier isolation layout, while Raw sizes remain unchanged.
[AUDIT.md](docs/AUDIT.md) records native-width integer scanning, one-tree identical
source dispatch, one-pass archive restoration, fewer steady-input clock reads
and reference operations, and reused workload values/byte/group storage.

The complete Clang 21 optimized/ZDEBUG/ASan Zfix build is warning-free. Current
unit verification passes 30 programs/320 scopes plus schema fixtures; native
integration passes 14 programs/225 scopes. Independent codec/load/dependency
checks, all 76 canonical process cases, standalone TCP/TLS examples and 100,000
codec fuzz runs pass without sanitizer findings. The obsolete archive-budget
assertion was corrected to the actual ordinary schema size and rebuilt before
the final unit run. Sources and eight reused dependency DSOs are hash-checked.
Detailed terminal evidence is in `/tmp/zfix-path-audit/`.

The historical benchmark archive and results are unchanged. Current source
operation/layout reductions establish no new latency measurement. Validation
remains Zfix-only on Linux x64.


## Repair implementation — source review only

The breaking changes in [zfix_repair.md](../zfix_repair.md) are implemented in
source. No build, compiler, test binary, Python integration fixture, benchmark,
network workload or installation was executed. The cases added/updated below
are source specifications, not passing runtime evidence. Earlier 320/225/76
counts and layout sizes describe historical snapshots.

| Requirement | Current source evidence |
| --- | --- |
| Every resend obtains app-selected content | `SeqTx::replay` directly invokes the application lookup for every eligible key; it never searches the queue or calls generic `Base::resend`. `TxHistory` is used directly for encoding, without replay queue-node allocation. |
| Replacement/suppression and deferred results | Retrieval carries `TxContext`, acknowledged frontier and ticket. App body/type/ID are authoritative; no body gap-fills. No replay result survives an attempt/refusal. Deferred results retire only their matching obligation; held results drop before validation/publication. Ordinary reload results instead become operational nodes retained across output refusal. |
| Reentrant and late callback ordering | Ordinary/replay lookup key/kind/ticket are registered before the hook. Hold/detach cannot permit an intervening attach; a Pending result remains drainable. Completion schedules remaining current work without relying on a second writable notification. |
| Operational retention and abort | Successful contiguous publication deletes its exact node and charge. Wire owns admitted output through completion. Priority Logon retains only its catch-up marker and cannot be aborted after admission. Managed `abortTx_` preserves unpublished assignments. |
| Current memory accounting | `Checkpoint` contains no TxBytes/queued-memory debt. Empty restore charges zero; ordinary replacements install/charge once under current limits, including the Pending association reservation. Live metrics are operationalCount/operationalBytes. |
| Generic archive notification | Generic ordered archive processing remains. `idleArchive` advances its scan before emitting nonempty epoch/[begin,end) notifications; no body or `archived()` release handshake remains. Warm hold/reconnect preserves unfinished chunks; cold restore starts from saved acknowledgement without historical callbacks. Application storage completion is not a core barrier. |
| Epoch preparation | Empty operational state replaces history eligibility/retirement scans. Reset request completion remains posted and prior lookup/output/core-archive obligations still drain. |
| Native connection boundary | FIX opens after TCP/TLS readiness. The synthetic Connecting/action/deadline/configuration are removed. Initiator identity claim precedes Logon; invalid configuration keeps its Config refusal and unopened refusal emits no invalid Tx command. |
| Owner-confirmed Ready | `LinkState::peerLogon_` initializes both recovery confirmations false. Rx confirms after disposition and Tx confirms actual active/pending replay and lookup state. Assigned publication/retrieval/gap-fill can catch up before Ready, while new business admission stays gated. Ordinary Ready requires no extra packet. |
| Liveness/drain proof | Explicit `probe()` and receive-idle challenges remain independent of Ready. Drain invalidates intervening/failed proofs and accepts only a fresh published echo covering the boundary; stale/early echoes and zero-heartbeat behavior retain their guards. |
| Naming and dependencies | Session administrative namespace, LinkState, Checkpoint, RxMsg/RxFramer, TxMsg header, MT/MTList, Net bindings and LinkTx names are propagated. Context depends on RxMsg rather than full Rx. Header registration and local includes match existing source; no compatibility aliases were added. |
| Consumers and storage policy | Native workload/recovery/reset/session examples and fixtures use the new hooks. Fixture checkpoints are v2 without old byte-debt readers. App stores retain chosen records; archive/repricing/type-ID/suppression examples are application policy. |

Source-only regression scopes include `appReplayChoice`, `lookupReattach`,
`reloadReservation`, `managedAbort`, `ownerReadiness`, `freshDrainProbe`, generic
archive-range handling and application `archivePolicy`. Managed handshake source
assertions now require Ready after only the two Logons. None has been executed.

The source inventory covers 126 authored C++ files plus build inputs/scripts,
checks local Zfix include resolution and public header registration, records
obsolete-name searches and 98-column layout, and pins current hashes under
`/tmp/zfix-repair-exec/`. The original sources and rename map, consumer migration
ledger and core-owner source freezes are retained there for review. No dependency
source was changed. The historical benchmark archive remains SHA256
`bb066944cf075af408fed5210c7199182b306e42bf7152adfb23092e9b4a8587`.

Compilation, sanitizer/runtime correctness, installed consumers and interoperable
behavior of this repaired snapshot remain unverified. The repair plan records
future acceptance cases for execution only when the user's prohibition is lifted.

### Final source completion audit

The final review checked sections 1–6 and the naming migration against current
production paths, forwarding hooks, consumer sources, build inputs and contracts.
The evidence in the table above covers the current source deliverables. Runtime
acceptance remains a future gate, expressly excluded from this execution scope.

One concrete C++ declaration error was repaired: `SeqTx::send_` now deduces the
sequence key and byte charge separately, since generic callers can return
different integer types. Heap identifiers in renamed fixtures and the LinkTx
command queue now follow the final component names. These source corrections
have not been compiled.

Additional unexecuted regression assertions cover ordinary installed-node retry
without another lookup, archive hold before a scheduled chunk and stale-ticket
delivery after resume, reentrant archive acknowledgement, zero-heartbeat explicit
probe/drain proof, and a drain boundary received during incomplete Rx recovery.
The source scopes are `ordinaryRetry`, `warmArchive`, `archives`, `zeroHeartbeat`
and `freshDrainProbe`.

Current README/contract text no longer describes separate replay absence caches,
core history-release waits or the removed archive API. It distinguishes new
business admission from assigned recovery, transient replay results from ordinary
operational retries, and warm archive continuation from cold restore. Earlier
runtime evidence remains explicitly historical.

The refreshed source inventory covers 126 C++ files and 150 authored code/build/
script files. It reports no unresolved local Zfix includes, include cycles,
missing public-header registration, obsolete identifiers, overlength C++ lines,
trailing whitespace or filename case collisions. Hashes of 264 dependency
source/build files are unchanged during this audit; the historical archive hash
above is unchanged. `/tmp/zfix-repair-exec/current-sha256.json` pins the final
authored-code snapshot; `tx-completion-audit.sha256` records the final Tx-owner
freeze. These file inspections establish source consistency, not compilation or
runtime correctness. No build, compiler, test or fixture was executed.

## Current-build repair verification — 2026-10-29

The user subsequently authorized rebuilding and retesting Zfix using the current
build without changing configuration. This record supersedes the initial
source-only verification limit. Final build and test gates pass on Linux x64.

The existing Clang 21.1.8 configuration remains GNU C++2b, x86-64-v2,
`-O3 -g -DZDEBUG -DNDEBUG -fsanitize=address -fno-omit-frame-pointer`, with
ASan linking, shared libraries and zstd-compressed debug sections. No configure
or dependency rebuild was run. Zfix Makefiles were regenerated from their updated
`.am` inputs with Automake and existing `config.status` substitutions to register
the renamed sources. `config.status`, `config.h`, configure inputs, the root
Makefile and compiler/linker settings remain hash-identical; eight reused
framework dependency DSOs are also unchanged. `config.log` only gained the
targeted Makefile-regeneration log entries. The configured QuickFIX SDK was reused.

| Gate | Current result | Evidence under `/tmp/zfix-retest.f2aaxrp8/` |
| --- | --- | --- |
| Zfix default build | All module directories build; zero warnings/errors; final current build exits 0 | `build-fixed.log`, `build-final.log`, `build-recovery-final.log`, `build-current.log` |
| Unit/schema | 30 programs / 317 TAP scopes, plus schema, typed-send and typed-reply compilation checks pass | `unit-final.log` |
| Native TCP/TLS integration | 14 TAP programs / 225 scopes pass, including the rich application fixture and 18 native process cases | `integration-complete.log` |
| Codec/load/SDK boundary | Independent codec 1 scope, QuickFIX load 5 scopes, SDK boundary 8 scopes and 6 JSON/diagnostic cases pass | `interop.log` before its process gates |
| Canonical/fault/security/duplicate | 76 / 104 / 40 / 8 process cases pass | `interop.log`; retained matrix summaries referenced there |
| Lifecycle | All 24 resume/reset/recovery process cases pass after oracle correction | `interop-lifecycle-final.log` |
| Liveness | All 6 receive-idle probe timeout cases pass, with ordinary Ready preceding the explicit challenge | `interop-probe.log` |
| Standalone examples | TCP and verified-TLS client/server pairs pass; all four actors exit 0 with empty sanitizer stderr | `example-pairs/summary.json` |
| Recovery ordering regression | Fixed 10 TCP + 10 TLS repetitions pass: 20 runs / 240 scopes | `recovery-repeated/summary.json` |

All runtime checks used `ASAN_OPTIONS=detect_leaks=1:exitcode=99` and
`LSAN_OPTIONS=exitcode=99`. Final runs have no ASan/LSan diagnostics. Initial
failed runs are retained, rather than counted as passing evidence.
An additional scan of 997 retained diagnostic files across ten current-run
artifact roots found no sanitizer reports; see `sanitizer-artifact-scan.json`.

The rebuild/retest found and repaired two production defects: clearing
`TxHistory::originalTime` with `={}` selected the null `tm *` conversion, so the
ordinary reload path now uses `null()`; automatic FIX44A reconciliation included
the newly sent Logon, so it now replays only historical assignments before that
Logon. The exclusive validation/acknowledgement boundary and explicit
ResendRequest admitted bound remain distinct. Clean startup again produces only
the two Logons. Foreign QuickFIX `Message`/`Session` includes, calls and diagnostics
unintentionally affected by the naming migration were restored.

Fixture corrections preserve their original behavioral checks. They explicitly
stimulate pending-admission backpressure/trace rotation through application
probes, order the uncommitted reset deadline before Tx preparation, recognize
balanced recovery Pause/Ready edges after late peer ResendRequests, and wait for
remote replay delivery before releasing the association that enables a global
test Wire hold. The repeated recovery runs verify that causal barrier. The
independent lifecycle oracle now validates legitimate current-connection replay
against its preceding original as well as requiring every persisted assignment;
the original failing trace and focused reproduction remain available.

The 317 unit scopes are accounted for against the old 320: SeqTx's obsolete-
contract suite was rewritten/consolidated from 25 to 15 scopes; Tx gained three
and LinkTx, LinkState, Rx and Demo gained one each. All 30 program registrations
and `prove` entries remain present after the renames.

`summary.json` records the final counts/configuration checks;
`source-sha256.json` pins the 150 authored code/build/script files. Installation,
alternate build configurations, other platforms, fuzz campaigns and benchmarks
were not rerun. The historical benchmark archive and its hash above are unchanged;
these functional results establish no new performance measurement.


## Enum configuration audit — 2026-10-30

Managed mode, BeginString and application version now use `ZtEnum` domains;
version/appVersion storage is unsigned with `N` for absent configuration.
Attachments contain two enum values plus seven borrowed identity strings.
Canonical wire maps replace owned version strings without adding cached copies
or choosing a business schema. Utility CLI mode/version/scenario and probe/demo
scenario lookup also use enums; independent SDK tools retain foundation-only
workload dependencies. Checkpoint fixtures retain canonical text at their file
boundary. See [ENUM_AUDIT.md](ENUM_AUDIT.md) for remaining business-code schema
findings and intentionally raw/open strings.

Reusing the current Linux x64 Clang/ASan build passed 30 unit programs / 317
scopes plus schema checks, 14 native integration programs / 225 scopes, codec,
load and SDK boundary gates, 76 canonical interoperability process cases,
24 lifecycle and six probe cases, and TCP/TLS standalone example pairs.
`/tmp/zfix-enum-audit/` retains logs and hashes; configuration and all eight
dependency DSOs match the preceding baseline. No dependency rebuild or build
configuration change was made. The retained 644 interoperability diagnostic
files contain no sanitizer reports. Business-schema candidates were audited
and documented; their accepted-input contracts were not changed.


## Opaque application session IDs — 2026-10-30

The former attachment tuple and full-header checkpoint binding were too
restrictive. Following `ufix`'s `config.id` lookup, `SessionConfig.id` is now an
opaque application key. `HeaderConfig header` independently contains version,
CompIDs, SubIDs and LocationIDs; mode and application version remain separate
configuration. The qualifier field is removed; applications choose their own ID
qualifiers and routing policy. Attachment uses only ID; checkpoints own one ID
string and match it with epoch. Incoming wire headers retain their current
configuration validation. No fallback key is synthesized from wire fields.

Regressions reject duplicate IDs despite changed headers/mode, allow separate
IDs with identical headers, and restore actual Rx/Tx owner progress under a
changed complete header configuration and mode. TCP and TLS cold restart now
exercise FIX 4.2 to 4.3 with stable IDs/epoch and existing replay/sequence checks.
Native utility `--id` supplies its application key prefix; fixture checkpoint
v3 persists only ID and epoch/progress, without wire headers or mode.

The current Linux x64 Clang/ASan build passed 30 unit programs / 318 scopes
plus schema checks, 14 native integration programs / 225 scopes, independent
codec/load checks, 76 canonical process cases, 24 lifecycle and six probe cases,
and both standalone TCP/TLS example pairs. `/tmp/zfix-session-id/` retains logs,
source hashes and `summary.json`. Configuration and the eight framework
DSOs match the preceding baseline; no dependency rebuild was performed.

The independent SDK dependency gate failed with a confirmed existing QuickFIX
TCP shutdown use-after-free: `stop(true)` calls main-thread SocketServer cleanup
before joining a worker that also closes/deletes that server. The probe source
and SDK library were unchanged by this repair. The retained stack is
`zfix/interop/zfix-qf-dependency.ts_hf9ex/probe.log`; see
[interop/README.md](interop/README.md). No SDK modification, fixture workaround,
sanitizer suppression or passing retry hides this failure. The separate passing
process gates have 644 retained diagnostic files without sanitizer reports.

## Persistent-link replacement repair — initial dependency blocker (historical)

Execution of `../zfix_repair2.md` is incomplete. Zfix registry removal, persistent
routing, managed replacement, logical queue preservation, deferred admission,
wire retirement, stop-generation handling and reset reconciliation are
implemented. Pure timer-only notifications remain Rx-local. A scoped source
review is recorded in [REPAIR2_REVIEW.md](REPAIR2_REVIEW.md).

Final verification under the unchanged Linux x64 Clang/ASan configuration:

- Unit/schema checks: 29 programs / 328 scopes pass.
- Affected managed integration: five TCP and five TLS programs, 194 scopes pass.
- Canonical interoperability: all 76 cases pass.
- Lifecycle: 24; SDK-only duplicate policy: four; probe cases: six all pass.
- Native workload wrapper: 18 cases pass; demo driver: two scopes pass.
- Standalone TCP and TLS example pairs pass with empty sanitizer stderr.

The dedicated native displacement gate fails on both transports. TCP's delayed
old cleanup reports link-down and loses B's partial input. TLS cannot drain its
physical connection accounting. The negative TLS fixture exits nonzero before
destroying a live hub and is not claimed leak-clean. Existing transport scopes
are retained; no failing displacement case is skipped or counted as success.
See [TRANSPORT_REPAIR.md](TRANSPORT_REPAIR.md) for source evidence and the
required handoff/cleanup/accounting work, including the FIX association boundary.

Plan section 3 explicitly excludes dependency edits pending separate scope
resolution. No dependency or SDK code/build was changed by this execution.
The full goal therefore cannot be marked achieved. Requirement-by-requirement
status is in [REPAIR2_AUDIT.md](REPAIR2_AUDIT.md).

Logs/artifacts use `/tmp/zfix-repair2-` prefixes. The final summary is
`/tmp/zfix-repair2-summary.json`; 138 source/build/script hashes are pinned in
`/tmp/zfix-repair2-source-sha256.json` and remain unchanged during verification.
The prior configuration checksum baseline still matches. The first canonical
attempt overlapped DSO relinking and is invalid evidence; its artifacts are
preserved and the stable rerun passed. The earlier independent QuickFIX
shutdown use-after-free remains separate, without SDK changes or suppression.


## Persistent-link replacement repair — authorized transport fixes

The user authorized the TCP stale-close repair and equivalent TLS/QUIC audit.
TCP and TLS now prevent obsolete close continuations from cleaning successor
state and balance every physical connection's pending count. TLS also clears
predecessor close flags at association, including when the successor is refused
before handshake. QUIC review found no equivalent defect; it was not changed.

Under the existing Linux x64 Clang/ASan configuration, Zfix verification passes:
seven native TCP scopes, eight native TLS scopes, 29 unit/schema programs / 328
scopes, and five managed integration programs / 97 scopes per transport.
Evidence and exact logs are in [TRANSPORT_REPAIR.md](TRANSPORT_REPAIR.md).
Only the authorized Ztcp/Ztls headers were changed in this dependency repair;
no dependency library rebuild, SDK edit or configuration change was performed.
The older source hash manifest and broad process results above describe their
original snapshot and are not claimed as reruns of this newer snapshot.

The full plan remains incomplete: shared physical output queue handoff, early
FIX association invalidation, and their combined replacement regressions still
need proof. The old TCP input-loss and TLS missing-accounting failures above
are preserved negative evidence, not current failures.


## Physical output handoff and TLS target binding — verified native phase

The queued-output TCP regression reproduced a distinct stall after real kernel
EAGAIN: B became ready, but appended output behind A's abandoned queue head.
Replacement now posts abrupt close and I/O-context/queue retirement before B is
published. ZiTx rejects late closed-socket setup before queue mutation and
accepts an explicitly bound physical target. TLS pairs its Tx keys with that
target; an already encrypted A record cannot resolve B during finalization.
A captured connection check also prevents stale Tx-key installation from
publishing readiness. No extra queue, registry, generation or payload copy was
introduced; the new TLS target reference retires with its keys on Tx.

The final restored source passes 29 unit/schema programs / 328 scopes and six
native integration programs per transport: TCP 105 scopes; TLS 106 scopes.
The native displacement programs contribute eight TCP and nine TLS scopes.
Each fault-removal control fails exactly its corresponding new scope. Logs use
`/tmp/zfix-transport-final-`; detailed evidence and preserved intermediate
failures are in [TRANSPORT_REPAIR.md](TRANSPORT_REPAIR.md). Configuration hashes
still match the original build; no dependency library rebuild or SDK edit was
performed. The ZiTx fake connection fixture was adjusted for the actual context
identity and was not rebuilt outside Zfix.

The full repair2 goal remains active and incomplete. Early FIX association must
invalidate A before B authenticates, and its combined logical/reset/lifetime
regressions remain required. [ASSOCIATION_REPAIR.md](ASSOCIATION_REPAIR.md)
records that implementation step and the separately identified Raw overlap
contract. Earlier broad process matrices remain evidence for their recorded
source snapshots, not reruns of this native phase.


## Early association implementation — verification in progress

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


## Association checks and remaining races

Current focused results: State/Rx/Raw owner checks pass 67 scopes; LinkTx passes
32; actual-Link Wire passes 19, including reset commit before/during/after early
association and an unauthenticated third successor. Native transport passes
nine scopes on each TCP/TLS target. Combined Raw A-to-B-to-C replacement passes
seven scopes per transport, with delayed original record completion and
independently deferred external stop replies. Native-native cold reset/recovery
passes four TCP/TLS lifecycle cases after pre-association restoration/work-credit
repairs. Logs use `/tmp/zfix-association-`.

The combined managed native fixture is still being corrected and verified:
its first run exposed missing application reply/queue/history policy, repeated
Ready handling, and cleanup aborting its own pending Logout. These fixture
issues are not evidence for new core policy. The original failed artifacts are
preserved; no failed scope is counted as a pass.

Two further source-reachable races require focused validation/repair: stale
native Tx error policy against a successor, and logical reset outcomes queued
across old physical close followed by early association. A returning false
physical-send handler must not disconnect another socket; the I/O layer already
closes its original failed socket. Reset abort/commit reconciliation must use
its logical operation agreement rather than only current physical identity.
No additional transport registry or connection generation is proposed.

## Resumed outcome and association verification

The managed native session suite passes 49 scopes per TCP/TLS target, including
the three new deferred-admission replacement cases. Original callbacks remain
held while B binds and exchanges traffic; fixture application history remains
outside the engine. Evidence:
`/tmp/zfix-association-native-session-policy-summary.json`.

The reset close/outcome regression first failed five of six new schedules.
The repair keeps the existing logical agreement until its validated outcome
acknowledgement, independently of physical close. Source build and focused
Wire/State/Rx/LinkTx programs now pass 116 scopes, including all six new cases:
`/tmp/zfix-reset-outcome-units-final.log`. No new state/container is introduced.
Native stale-error positives pass ten scopes per transport; removing their
repair fails exactly each new targeted scope. Restored candidates rebuild and
pass ten scopes per transport, with exact header restoration and no sanitizer
diagnostics. Evidence is in `/tmp/zfix-tx-error-audit/`.

Factory reinspection also migrated the managed demo acceptor to an
application-owned persistent link; its build and sequential TCP/TLS process
checks pass two TAP scopes across the three supported managed modes. Established
managed replacement with accepted Work/context and deferred Tx loading is being
added. The remaining section 7 matrix and full current-source verification stay
open; see REPAIR2_AUDIT.md. Configuration checksums still match
`/tmp/zfix-session-id/config-before.sha256`; dependencies/configuration were
not rebuilt or changed.

Established replacement now passes all three modes: session TCP/TLS each pass
52 scopes, including retained accepted Work/context and the original pending
Tx association publishing on B. Evidence:
`/tmp/zfix-association-native-work-summary.json`. The subsequent TLS socket-only
failure/C fixture passes six new cases (TLS 58 scopes; TCP retains 52): raw B
close/fatal alert cannot start FIX or revive A, and C resumes the same owner
while A cleanup is held. Logs: `/tmp/zfix-preauth-audit/{tcp,tls}.log`.

The extra cold-preparation work credit was removed following a per-owner FIFO
proof: the supposedly hazardous skipped-task order is unreachable, and retained
owner references plus round/generation checks suffice. Utility rebuild and all
four cold native reset/recovery TCP/TLS cases pass without the extra machinery;
all 20 child exits are zero with no sanitizer findings. Evidence:
`/tmp/zfix-cold-fifo-audit/verification.json` and `PROOF.md`.

The live resend/archive regression is still under verification. Its first
count oracle incorrectly required one publication despite distinct A/B recovery
requests. Application selection, decoded repriced content, stale suppression,
archive progress and owner drain passed; current checks count the actual
requests/loads and wait for publications, then exercise B after all A cleanup.
No production change is justified by that earlier incorrect oracle.
