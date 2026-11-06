# Zfix unit tests

Current-build repair verification includes all 29 unit programs, 347 scopes and
the schema checks; see [STATUS.md](../STATUS.md) for the current evidence and
verification scope. Earlier acceptance ledgers below describe their recorded
snapshots. Current managed resend always calls the application; the core keeps
only operational unpublished assignments. Archive callbacks carry epoch/ranges,
without bodies or a release handshake. Ordinary Ready needs actual Rx/Tx recovery,
not a startup probe round trip. Explicit liveness/progress/drain probes remain.

With existing dependencies available, build `make -C zfix/src -j8`,
`make -C zfix/test -j8` and `make -C zfix/test test`.

- `ZfixLoadTest`: six isolated workload/model/accounting/sample and cancel/replace
  policy scopes, with compile-time guards against native codec/session includes.
- `ZfixLoadFIXTest`: eight native mapping scopes covering scalar/text,
  nested and heavy groups, execution reports, cancel/replace and absent OrigClOrdID.
- `ZfixLoadRunTest`: fifteen deterministic workload phase, pacing, refusal,
  bounded-cache, CLI, identity, replay and intended-latency scopes.
  These three targets compile their utility implementation sources directly,
  preserving the `src`, `test`, `util` build order. They run from `make test`;
  `asan-load-test` is also included by `asan-test`, and `installed-load-test`
  by `installed-test` (`ZFIX_PREFIX` selects the staged prefix). Foreign frame
  generation/comparison lives in [interop](../interop/README.md), while native
  process suites belong to [itest](../itest/README.md).
- `ZfixDemoTest`: application-policy scopes covering bounded correlation,
  reordered/multiple/duplicate/unknown reports, rejection, cached/restored
  results, decoded PossDup replay without fresh work, epoch/sequence history
  associations with count/byte caps, custom Logon conversion, and the
  `archivePolicy` regression. That regression transfers a record between two
  application stores, reprices it, changes type/identity at the same sequence,
  and then suppresses it. It tests app policy, not the core replay driver. Shared policy
  code lives in `ZfixDemo.hh/.cc` and is shared by the
  [native application-policy regression fixture](../itest/README.md). Additional targets
  are `asan-demo-test` and `installed-demo-test` (`ZFIX_PREFIX` override).
- `ZfixWireTest`: complete-frame admission/publication, zero-copy TCP backing,
  bounded TLS-style record splitting, ordered one-shot completion, replay tokens,
  asynchronous/synchronous failures, close/stale callbacks and administrative
  reserve. Composes the production `LinkTx` and `WireTx` to check publication
  ordering, completion after the publication callback, and resumption of a loaded
  body at the same sequence when output capacity returns. Logon abort checks
  cover failed completion before and after logical close, including exact
  context, Output fault ordering, charge retirement and repeated close.
- `ZfixRxFramerTest`: shared TCP/TLS receive-stream adapter, every byte split,
  single-byte buffers, fragmented/coalesced text frames, exact-buffer detachment,
  retained-frame lifetime, malformed headers/checksums, frame limits, allocation
  refusal, sticky errors and disconnect cleanup. Uses actual `ZiRxStream` queues
  and pooled queue-compatible buffers, without sockets.
- `ZfixDeclTest`: standalone codec header/link boundary, scalar and private-tag
  alternate facet declarations, nested array/group and union declarations.
  A preprocessor guard rejects a session/network include dependency. Links only
  libZfixFmt and the foundation through Zf.
- `ZfixLinkTest`: session library and codec version exports, typed message entry,
  and compilation of session/endpoint/hook boundary headers.

- `ZfixSignTest`: HMAC-SHA256 known-vector verification over the exact
  unsigned FIX bytes, covered-field/signature tampering, unsigned archive
  restoration and supplied-pair rejection, signature suppression without source
  mutation, variable signatures and pooled sink growth, required final-frame
  presence, standalone/raw encoding, managed replay and failed publication,
  asynchronous raw Tx ownership and routing metadata lifetime, and reuse of
  finalized signed bytes during transport cursor advancement. Registered in
  `make test` and `installed-test`.
- `ZfixCodecTest`: constructor/setter/update/default/requiredness paths;
  mutable and primary-key selection; flattened components; high private IDs;
  unchecked numeric/native null results, ignored range metadata, dates, booleans
  and characters; SOH-delimited text pass-through and unselected field skipping; alternate facets and precision overrides;
  custom scalar dispatch, borrowed input lifetime and identified allocation;
  golden body output, exact-size buffers, configured bounds and sticky failure;
  explicit MultipleValue, enum/flags, 128-bit/fixed numeric and owning pointer paths;
  dual-tag Binary String/Bytes, borrowed pointer identity in live/indexed,
  constructor and group paths, exact raw output, empty required pairs,
  schema-aware selector inspection and matching malformed-pair errors.
- `ZfixGroupTest`: nested/zero groups, reused IDs in distinct scopes, inferred
  delimiters, parent/group shared tags, malformed counts/order/duplicates,
  selected/unselected validation, configured bounds.
- `ZfixUnionTest`: explicit root resolution, deferred borrowed lifetimes, owning
  alternatives, selected nested facets, text alternatives, unresolved-save
  failure, custom representations and allocation/placement construction.
- `ZfixSessionTest`: golden bodies/complete frames for all seven session controls,
  mode-required Logon fields, optional-field emission, ordinary field render facets,
  exact opaque/empty TestReqID echo, optional raw Logon 95/96 bytes, and malformed
  control/session-field error context.
- `ZfixSeqTest`: isolated Rx-owner sequencer over ZmPQRx; bounded read-ahead,
  duplicates, monotonic gap advancement, hold/resume, stale posted/timer work,
  drain-required reset and sequence limits. Callback holds and 128 deterministic
  64-message arrival permutations exercise queue progress. It uses fake task and
  timer hooks; lifecycle clock tests are in ZfixLinkStateTest, with complete queue/
  lifecycle composition covered by Managed and native integration fixtures.
- `ZfixSeqTxTest`: independent new-send/replay cursors, capacity and sequence
  bounds, preserved assignments after output failure, callback-stop restart,
  bounded application retrieval/archive work and unavailable-content gap fills,
  overlapping and open-ended ranges, acknowledged progress, stale tasks,
  drain-required reset, and deferred retrieval with one-shot completion. Repeated
  asynchronous unavailable responses must reach a gap fill without cycling.
- `ZfixLinkStateTest`: injected-clock Rx lifecycle, initial identity/admission,
  physical replacement, local Logon boundary ordering, independent version/mode
  combinations, 789 replay ranges, DefaultApplVerID matching, opaque probe matching,
  recovery/readiness without an initial challenge, explicit liveness/drain proofs,
  heartbeat zero, timer cancellation/expiry, logout, reconnect,
  stale callbacks, raw bypass and configuration bounds. Managed composes queue/
  control transitions; native integration fixtures exercise the TCP/TLS boundary.
- `ZfixRxTest`: pooled complete-frame receive coordinator with fake owners;
  asynchronous admission/backpressure, ordered and duplicate business input,
  malformed control/business rejects, unknown fields, early recovery
  controls, gap-fill/reset-mode rules, peer identity failures and timestamp metadata, missing
  sequences, alternate header order, backing/read-ahead
  limits, mode overlays, high Logout, same-epoch reconnect and raw bypass.
  The business decoder receives effective codec limits; rejected admission
  cannot leak held input into a later connection.
- `ZfixSchemaTest.cc`: syntax-only compilation fixtures. `schema-test` first
  compiles a valid schema/catalog, then requires rejection of thirty-eight negative
  cases: duplicate IDs, flattened collisions, absent IDs, invalid field
  metadata, reserved envelope tags, equal-MsgType alternatives and duplicate
  catalog registrations, duplicate group-entry tags, primitive group elements
  missing nested-union selection, Bytes without an encoding and vectors without explicit
  MultipleValue policy, reserved sub-identity and application-version tags. The
  Binary cases also reject incompatible types/properties, equal/zero/reserved
  pair tags, duplicate declarations, conflicting Sig shorthand, multiple
  signatures and collisions involving payload tags. The normal `test` target
  invokes this gate.

The declaration/link consumers verify boundaries; ZfixLinkStateTest separately
exercises Rx lifecycle transitions. The catalog rejects ambiguous primary
MsgTypes; secondary discriminator descriptors are checked at compile time, and
ZfixDispatchTest exercises runtime selection. Concrete managed/raw TCP/TLS
endpoints are covered by the integration fixtures. The body-conversion gate and
current endpoint evidence are recorded in ../STATUS.md.

For Linux installed-header/library verification, stage foundation and transport
libraries before Zfix (Zdbus is required by the Linux Ztls build):

```sh
for mod in zu zm zt ze zf zi zdbus ztcp ztls zfix; do
  make -C "$mod/src" -j8 || exit $?
  make -C "$mod/src" install DESTDIR=/tmp/zfix-stage || exit $?
done
make -C zfix/test installed-test ZFIX_PREFIX=/tmp/zfix-stage/opt/z
```

Adjust the last path for the configured prefix. The installed-test target uses
only installed Z headers and libraries, recompiles all twenty consumers with the
configured toolchain/version flags and runs their TAP assertions. Its installed
artifacts are under the supplied staging directory; it does not install into
`/opt/z`. Stage checks do not constitute a configured clean release build.

## Framing and sanitizer/fuzz checks

`ZfixFrameTest` exercises literal golden envelopes, every byte split, coalesced
frames, ordinary text, corrupt lengths/checksums, envelope ordering, configured
bounds, ordinary session-field metadata, PossDup/OrigSendingTime and failed output.
It sweeps body lengths through 10/100/1000 and eleven capacities for each, plus
one-byte-short failure. Installed verification includes this consumer.

The new binary framing cases use an independently counted 159-byte golden frame
with BodyLength 136 and CheckSum 108. One ordinary object declares 212/213 with
an RTRF payload, 95/96 with embedded FIX messages, and 93/89 with binary signature
bytes. Live and indexed decoding check exact original payload addresses. All
prefix boundaries and a coalesced next frame are checked. Malformed signature
lengths cannot consume outer CheckSum(10) or the next frame. `ZfixRxFramerTest`
feeds this frame through every split and one octet at a time using the real
receive-stream queues; it retains the returned pooled allocation through stream
cleanup and checks the same payload addresses. Managed message-context retention
is covered separately by `ZfixContextTest` and the native integration fixtures.
These additions pass the Header-free binary/signing gate recorded below;
the verification counts above describe the earlier snapshot.

With configured Clang, `make -C zfix/test asan-test` compiles eighteen
conversion/frame/control/sequencing/lifecycle consumers with ASan, then runs with
LSan enabled. It instruments the codec, controls and test templates; dependency
libraries retain their configured instrumentation. The recorded focused runs
used uninstrumented dependencies; a clean configured full-dependency sanitizer
build verifies the broader scope separately.

`make -C zfix/test fuzz-build` builds the optional Clang libFuzzer/ASan target.
Copy the tracked eight-file seed corpus to a scratch directory before running:

```sh
zfix_fuzz_dir=$(mktemp -d /tmp/zfix-frame-fuzz.XXXXXX)
cp zfix/test/fuzz/* "$zfix_fuzz_dir/"
ASAN_OPTIONS=detect_leaks=1 ./libtool exec ./zfix/test/ZfixFrameFuzz \
  -max_total_time=30 -timeout=5 -max_len=4096 -print_final_stats=1 \
  -artifact_prefix="$zfix_fuzz_dir/" "$zfix_fuzz_dir"
```

The experiment bounds frames/fields to 4096 octets, cumulative entries to 32,
depth to 8 and parse records to 64 KiB. It fuzzes raw bodies and envelopes,
including nested groups and text fields, then re-encodes successful
conversions and checks frame/decoder invariants. Input is borrowed for one
iteration; output storage and parsed values are released before returning.
These bounds describe codec work/retention, not ASan allocator quarantine RSS.
Generated corpus units/artifacts belong in the scratch directory. Output
alternates between fixed storage and a preserving growth callback.

Framing coverage includes progress with one-octet appends, a 4 KiB header value,
relocated caller storage, body-boundary truncation before checksum arrival,
ordinary session fields and read-only text, field inspection and
facet-selected timestamp precision. Scalar coverage includes control-byte
rejection and owning/default/partial-failure CString list cases.

For a focused sanitizer rerun after sequencing-only changes, use
`make -C zfix/test asan-test ASAN_TESTS=Seq`. The default runs eighteen consumers;
additional endpoint/application consumers have dedicated sanitizer targets.

`make -C zfix/test asan-test ASAN_TESTS='Seq SeqTx'` instruments both owner
sequencers. The queue components use fake posting/application hooks; Session and
Managed supply the complete-frame lifecycle/transition coverage separately.

`make -C zfix/test asan-test ASAN_TESTS='Frame Session LinkState'` checks the extended
headers, controls and Rx lifecycle with their implementation sources instrumented.
The default ASan target includes the lifecycle consumer. Direct-source sanitizer
and fuzz builds define the library export macros; installed consumers use the
normal installed-library import boundary.

`make -C zfix/test asan-test ASAN_TESTS='Frame Session LinkState Rx'` also instruments
receive coordination and pooled input handling. Rx requires Zi headers/libraries;
the standalone declaration/codec consumers retain their foundation-only boundary.

## Retained Tx output and association

`ZfixTxMsgTest` verifies immutable pooled bodies, opaque application identities,
control facet selection, ordinary text, independent mutable transport storage,
original/current replay timestamps, jumbo growth, exact frame limits, long
BeginString headroom, and unpublished conversion/header failures. Archive-body
reload covers a non-default-constructible type without output getter calls,
required serialized fields, exact numeric spelling and unknown extensions,
borrowed input reuse, ordinary text and length errors, group requiredness, optional
pointer components and built-in administrative classification. Resend eligibility
is an application decision, not a property of `TxBody`.

`ZfixTxTest` supplies fake Tx/application owners around the actual sequence queue
and wire codec. It covers delayed/refused load, association before publication,
stale acceptance with deferred unload, interleaved business/control numbering,
SendingTime sampled after a delayed load, publication retry, partial transport
cursor advancement, business/Reject replay, administrative and unavailable-history
gap fills, archive/reload, deferred history completion and wrong-epoch rejection,
recreated-owner history reconstruction through the application callback,
business pause with control admission, administrative byte reserve and sequence
headroom/exhaustion, backward-clock replay failure/recovery, and independently
varied declared versions/managed modes. It does not replace full Rx/Tx transition
or network tests.

Both consumers are included in source and installed verification. Run focused
instrumented checks with `make -C zfix/test asan-test ASAN_TESTS='TxMsg Tx'`.
`Tx`'s codec, control, session configuration and Tx implementation sources are
instrumented; dependency DSOs retain their configured instrumentation.

## Prior acceptance ledger (historical)

The following recorded checks precede the current no-history/readiness repairs.
Their counts, retention/release descriptions and negative controls describe that
earlier implementation; they are not current repair verification. The current
fixtures and contracts must be checked separately when execution is authorized.

The queue repair gate replaces the earlier highest-unsent cancellation/reverse
unwind fixtures. Explicit abort now leaves assigned sequence numbers unavailable
for reuse, retains earlier history, releases payload charge once and uses ordinary
retrieval/GapFill callbacks. Current coverage includes zero-byte missing assigned
ranges, retained work on reconnect, Logon-first admission/catch-up, and independent
new-send/replay unavailable lookup prefixes. Deferred unload tests apply only to
stale pre-commit acceptance. The queue repair/migration gate is complete; previous
reverse-unwind fixtures are superseded evidence. See ../STATUS.md for current
results and failing old-behavior controls.

`ZmPQueueTest`, `ZmPQRxTest` and `ZmPQTxTest` are the focused foundation set. They
cover bounded shift across/inside node ranges, deletion preserving the logical
assigned tail, zero-length heartbeat gaps with Ignore/Observe/Fill policy, correctly
ordered failed-callback/stop/restart and pending-phase idle notifications for both
starts. Current normal runs pass 72 top-level results; selectively restoring old
shift/count/idle behavior fails the relevant assertions. Source builds are
warning-free, focused sanitizer runs pass with uninstrumented foundation DSOs,
and all three Valgrind runs report zero errors and zero lost bytes. See
`zmpqfix.md` for the recorded configuration and negative-control details.

For Valgrind use the consumer's directory, e.g. from `zfix/test` run
`../../libtool exec valgrind --leak-check=full ./ZfixTxTest`, so libtool resolves
relative dependency metadata.

## Managed owners and publication ordering

`ZfixLinkTxTest` exercises Rx control-body creation, opaque/empty echoes, mode-specific
Logon fields, bounded command admission, delayed control load, publication notices
before transport flush, retained assignments on reconnect, Logon before retained replay after
backpressure, and local completion before close. It explicitly checks that a
refused Logout command cannot produce a successful drained notice.

`ZfixManagedTest` connects actual initiator/acceptor Rx and LinkTx owners with
FIFO fake queues and complete-frame transport. Owner hooks assert their Rx/Tx
execution domain. Both roles exchange Logon, probes/opaque echoes, a typed order
and clean Logout in all three managed modes. Declared versions vary independently
of mode. A deterministic dropped-frame scenario exercises ResendRequest, original
business replay, ordered delivery of retained high messages and readiness after
recovery. Local completions do not change acknowledgement progress.

The paired Logout scenario failed before Rx stopped ignoring acknowledgement
traffic in Logout. It now requires clean completion on both roles, so transport
failure cannot mask a broken handshake. Session/Rx tests additionally reject
pre-publication probe acknowledgement, stale/duplicate publication notices and
stale echoes after owner recreation with unchanged durable identity/epoch.

Run `make -C zfix/test asan-test ASAN_TESTS='LinkState Rx LinkTx Managed'` to instrument
the participating sources and templates. These are unit owner/codec tests, using
no sockets, external peer, persistence backend or wall-clock sleeps. They do not
replace TCP/TLS integration or the required QuickFIX matrix.

## Selective startup state

Startup-state cases concern field presence independently from zero, explicit
range clears, preservation of omitted progress, cursor/range/limit errors,
opaque application-ID/epoch matching, owner-local failure before mutation,
sequential Rx/Tx snapshot assembly and refusal after first activity. The identity
contract excludes wire headers, mode and version from checkpoint matching;
headers on the current connection remain independently validated.

The sequence consumers additionally restore a known Rx range with no retained
payload nodes and request all missing content on resume, then verify ordered
processing and removal of stale request metadata. Tx restores a pending replay
range and verifies replay identity with unchanged new-send progress. The Rx
Observe adapter preserves a held saved subset, advertises the actual first gap
on resume, and suppresses stale/reentrant retries without dummy nodes. Replay
snapshots track accepted callback progress and pending bounded chunks, including
a fully populated sender queue with no local storage gap. Queue repair validation
is complete with current unit/native evidence in ../STATUS.md. Foundation tests
use ordinary zero-length items for logical tails beyond payload nodes; real FIX
messages still consume one sequence.

Use `make -C zfix/test asan-test ASAN_TESTS='Seq SeqTx Restore Managed'` for focused
instrumentation. State's implementation and owner templates are instrumented;
dependency DSOs retain their configured instrumentation. Stage the foundation
header before installed-consumer checks. Run Valgrind after installation finishes,
since libtool installation can temporarily relink source-tree shared libraries.
These tests use application-owned in-memory records. Separate-process checkpoint
restart/reset is exercised by ../interop/lifecycle-test.py; byte-exact body reload
is also covered by TxFrame unit tests. Application durability remains a policy
outside the library.

## Negotiated Tx idle timing

`ZfixLinkTxTest` checks a seven-second negotiated interval, output/replay activity,
early wakeup re-arming without per-frame timer rescheduling, one-shot tickets,
blocked output without duplicate Heartbeats, stale callbacks after reconnect,
zero HeartBtInt and cancellation for an early high-Logout response.
`ZfixLinkStateTest` checks the negotiated interval in fixed Rx-to-Tx events.
`ZfixManagedTest` additionally exchanges periodic Heartbeats between separate
fake owners, verifies they renew receive liveness, suppresses an idle Heartbeat
when business traffic moves its deadline and ignores a late callback after
Logout. These tests exercise codecs and sequence ownership without sockets or a
real scheduler; transport timer cancellation/draining remains an integration gate.

Run `make -C zfix/test asan-test ASAN_TESTS='LinkTx Managed LinkState'` for focused
instrumentation; dependency DSOs retain their configured instrumentation.

## Published probe watermarks

`ZfixLinkStateTest` checks actual publication sequence capture, invalid notices,
exact/duplicate/numeric-looking echoes, unresolved recovery and mode 4.4A 789.
`ZfixRxTest` requires a high TestRequest to wait for gap recovery before echoing
and proves a high matching Heartbeat cannot acknowledge the local probe boundary.
`ZfixTxTest` and `ZfixLinkTxTest` check exclusive progress notifications, duplicate
suppression, publication bounds and stale epoch/connection events. Their optional
callbacks run on the association owner; local completion still does not ack.

`ZfixManagedTest` now observes acknowledged probe boundaries in all managed modes.
An explicit probe acknowledges prior business transmission without manufacturing
a business result. A dropped business message followed by a high TestRequest is
replayed before echo; both directions then resynchronize and acknowledge bounded
progress. These fake owners use complete wire frames, not real sockets or a
persistence backend. Run focused instrumentation with
`make -C zfix/test asan-test ASAN_TESTS='LinkState Rx Tx LinkTx Managed'`.

## Drain frontier tests

`ZfixLinkStateTest` checks ticket validation, a probe below the required frontier,
exact proof, recovery revocation, bounded waiting and raw bypass. `ZfixLinkTxTest`
holds new candidates while an older load is pending, waits for local completion,
keeps controls operational, and checks refused-load completion without a send
task. `ZfixManagedTest` drops an admitted business message, begins drain and
verifies replay before the peer echo establishes the frontier; both peers can
then drain while business candidates remain unsequenced. `ZfixRxTest` restores a
logical gap without payload nodes and verifies it blocks drain until gap fill.

These tests do not execute an epoch reset or a real transport lifecycle. Run
focused instrumentation with
`make -C zfix/test asan-test ASAN_TESTS='LinkState Rx LinkTx Managed'`.

## Epoch turnover guards

`ZfixTxTest` checks a stopped, acknowledged and released epoch transition while
retaining the physical connection, then reuses sequence one. Old archive, ack and
replay callbacks are refused; a stale send task cannot republish either message.
Reset snapshots report old progress. Readiness, unpublished/unacknowledged data,
delayed archive release, pending replay retrieval, stale/reused epoch IDs and
wraparound prevent turnover. A second monotone transition verifies continued
operation. `ZfixLinkTxTest` checks captured-epoch forwarding through its public
coordinator APIs. `ZfixSeqTest` requires held receive processing and refuses to
reset a logical gap even without retained nodes.

These owner primitives do not execute the live/reconnect reset wire handshake.
Focused instrumentation:
`make -C zfix/test asan-test ASAN_TESTS='Seq Tx LinkTx Managed'`.

## Operational tail retirement

`ZfixTxTest` uses a two-item budget with a larger operational tail and verifies
that preflight does not delete, stale tasks cannot skip phases, deletion is
bounded per owner turn, and successful turnover reports unchanged old ack
progress. Business and Reject tails are refused without partial deletion.
Cancellation, retry and detach guard queued tasks; stable request IDs reject
an old cancellation during a later retry in the same epoch/connection. Missing restored history and
a hole in an otherwise administrative tail are refused, while fully acknowledged
released history is allowed. Core retirement still requires external protocol
agreement and I/O preparation; it is not the complete reset wire gate.

Run `make -C zfix/test asan-test ASAN_TESTS='Tx SeqTx LinkTx Managed'` to instrument
the owner/queue paths and dependent coordinator consumers. Foundation/transport
DSOs remain uninstrumented in this focused build.


## Prepared reset paths and close races

`ZfixManagedTest` prepares both owners before wire activation, then exchanges
141=Y/34=1 in all three managed modes for live and reconnect reset. Live tests
exercise both agreed driver roles, an early blocked peer frame, epoch-two snapshot
cursors, stale close rejection and exactly-once business delivery with old/new
contexts. Reconnect uses a new physical connection and repeats admission; an
ordinary reconnect separately preserves epoch/cursors and sends no reset flag.
Close-race tests force timeout before Tx commit and between Tx commit/Rx notice.
They verify uncommitted cancellation and closed-owner epoch consistency after
commit, with no premature reset Logon.

Session/Rx tests cover invalid preparation, raw bypass, missing reset flag,
request/progress validation, blocked unstamped input and stale old input. Ctl
checks delayed application archive release and captured-epoch retry. These use
fake complete-frame transports; real TCP/TLS reset/recovery, partial-wire failure
and teardown are covered by the native integration fixtures.

`ZfixSeqTxTest::deferredReplayBound` retrieves more history than the replay batch,
checks at most one temporary cache node, retries failed publication, and changes
history availability before a repeated request. The fixture failed before the
transient-cache fix; retained original nodes keep their normal lifetime.

Run `make -C zfix/test asan-test ASAN_TESTS='LinkState Rx Tx SeqTx LinkTx Managed'`
for focused instrumentation, with foundation/transport DSOs uninstrumented.


## Sequence headroom notification

`ZfixTxTest::sequenceNotice` checks accepted versus pending/refused association,
first reserve entry, exact captured context, control availability, no duplicate
on replay/reconnect, restored near-limit attachment, new-epoch rearming,
administrative entry, zero headroom and UINT64_MAX-minus-one arithmetic.
`ZfixLinkTxTest::sequenceNotice` verifies the public coordinator callback and uses
the two remaining numbers for Heartbeat and Logout after business refusal.
The core fixture failed before notification implementation. Run focused checks
with `make -C zfix/test asan-test ASAN_TESTS='Tx LinkTx'`.


## Fresh-owner reset authorization

Session tests run first-open authorization in every managed mode and both roles,
including cancellation, stale epoch, inbound admission, missing flag, unsolicited
flag, raw refusal and one-shot consumption. Rx tests refuse advanced restored
progress both before and after arming, then establish/deliver business without
changing epoch. Ctl tests reject independently restored Tx progress or a pending
association before attachment and reject mismatched target epochs. Managed tests
exchange actual 141=Y/34=1 frames in all managed modes, dispatch business once,
then reconnect normally with continuing numbering and no reset flags.

Run `make -C zfix/test asan-test ASAN_TESTS='LinkState Rx LinkTx Managed'`. These remain
fake complete-frame owners; TCP/TLS authorization and teardown integration are
separate acceptance work.


## Application queue hooks

Five Ctl scopes exercise application-bounded enqueue without sequence assignment,
control priority, one candidate per task, exact selection-before-wire ordering,
empty-queue wakeups, pending association acceptance/refusal, duplicate completion,
retry without gaps, explicit overload/admission refusal and no pump spin. Queued
work survives ordinary reconnect; a load accepted after close unloads under its
old context and returns the body for application-selected retry.

The paired-owner live/reconnect reset scopes now enqueue business after both
peers drain. That work neither publishes nor consumes a sequence in epoch one;
it survives preparation/activation and dispatches exactly once in epoch two.
All managed modes and both live driver roles exercise it. Source Link now
includes Zi's header path because the implemented Hooks boundary uses pooled
TxBody handles; standalone codec consumers remain independent.

Run `make -C zfix/test asan-test ASAN_TESTS='LinkTx Managed'` for focused ownership
and continuation instrumentation. Actual endpoint submission dispatch remains a
later transport/typed API gate.


## Peer Reject association routing

Rx checks ordered high Reject recovery, pooled text/wire lifetime after node
release, duplicate suppression, malformed control exclusion and stale input.
Ctl checks the association-owner entry point, original source provenance after
reconnect, wrong epoch/future/zero contexts, missing backing and references absent
from library history. Neither publication nor acknowledgement changes.

Managed tests produce an immediate protocol Reject for an application-invalid
business message after interleaved control traffic, then route the retained record
to the Tx association owner and resolve its independent application ID from the
load mapping. All modes check exact reference/tag/reason/text, retained wire,
unchanged peer acknowledgement and no second notification on replay. They then
perform a live epoch reset, refuse the old captured report, reuse the outbound
reference sequence, and resolve the new Reject to the new application ID.

Run `make -C zfix/test asan-test ASAN_TESTS='Rx LinkTx Managed'`. These are fake-owner
routing checks; the native integration fixtures cover concrete endpoint dispatch
and teardown.


## State and session observers; simultaneous recovery

Rx `stateChanges` checks state-change delivery before general event routing,
monotone snapshots, admission/timer-only suppression, lifecycle failure/close,
stale callbacks, reconnect and raw bypass. Ctl `sessionObservation` checks actual
admission before observation before flush, retained control/context, refused
attempt suppression, exclusion of business/completion, and replay gap-fill type.
Both fixtures fail cleanly without observer wiring. The initial expected-red Rx
fixture accessed its empty observer list; it was corrected before recording the
clean failing-before result in `/tmp/zfix-observe-before2.log`.

Managed `simultaneousRecovery` drops one business message in each direction,
steps until both receive gaps are unresolved and both peers paused, then verifies
ResendRequest/replay and renewed readiness without deadlock or duplicate dispatch.
All three modes resume independent bidirectional business work afterward.

Run `make -C zfix/test asan-test ASAN_TESTS='Rx LinkTx Managed'` for focused ownership
checks. Concrete scheduler timer drain, transports and public endpoint routing
are covered separately by native integration fixtures.

The requirement/hook mapping is in [SESSION.md](SESSION.md); ../STATUS.md records
the current completed lifecycle gate and final acceptance work.


## Consolidated lifecycle and error matrix

Session failureMatrix/timeoutMatrix/logonMatrix/admissionMatrix/faultRouting and
Rx faultMatrix cover managed modes, both roles, continuing progress, independent
declared versions, transport/output failures at five lifecycle phases, admission,
789/application-version guards, early/exact/stale deadlines and framed-input fault
disposition. They assert state, cursor changes or preservation, action/output
selection, callback order and deadline cancellation. The managed-zero-epoch
regression failed before the Session open guard fix; unknown mode rejection was
already present and remains covered. Full mapping and static counter-cap review
are in [SESSION.md](SESSION.md).

Run `make -C zfix/test asan-test ASAN_TESTS='LinkState Rx'` for focused matrix
instrumentation. Concrete TCP/TLS checks pass on Linux; final memory/release and
platform acceptance are recorded separately in ../STATUS.md.


## Partial byte-stream transmission

Managed `partialWrites` holds an admitted business frame and tries every byte
split, including zero and complete length, across all managed modes. A queued
Heartbeat cannot overtake the frame. Rx copies only each written slice into its
own pooled buffer and scans it incrementally. No dispatch or local completion
occurs for an incomplete prefix; sequence, frozen SendingTime, association and
acknowledgement stay correct through zero progress and writability retries.

`partialFailure` covers an incomplete peer frame and a fully delivered frame with
failed deferred local completion. It checks partial ingress abandonment, one
failure completion, old-epoch/continuing sequence reconnect, original replay time,
exactly-once peer dispatch and retention of later committed unpublished work.
The older unload/assignment-reuse expectation is superseded by the queue repair;
current reconnect checks retain the association and assigned number. Explicit
application abort leaves a missing range for retrieval/GapFill. Archive storage
remains application-owned; the queue migration gate is complete with current
unit and native integration evidence in ../STATUS.md.

`queuedPartialWrites` admits three frames before the first completes, then checks
ordered per-frame success or completion of every frame on failure. Reconnect
replays all admitted sequences without unload/renumbering, including queued frames
whose bytes never reached the peer. `replayDuringPartial` queues the same replay
sequence twice and proves an old completion ticket cannot complete the newer
frame with an otherwise equal context. Processed fake work moves its handles out
of the trace so it does not retain transport frames accidentally.

Rx resetPreparation also checks that old-epoch/connection output-failure posts
cannot close new-epoch readiness. Run focused instrumentation with
`make -C zfix/test asan-test ASAN_TESTS='Rx Managed'`. These are byte-stream fake
owners; actual Ztcp/Ztls I/O and teardown callback draining are stage 6 work.


## Separate application association owner

Managed associationOwner uses owner 3 for association decisions and Reject lookup,
with Pending continuations back to Tx. It checks all modes and both physical roles,
interleaved controls, no refused-load gap, current publication-time headers and
immediate peer Reject ordering. queuedAssociationOwner routes the retained final
selection result to that same owner, retaining refused accepted queue work under
application policy and checking selection-before-Reject delivery.

deferredOwnerClose accepts an old load after disconnect, before or after a new
connection opens. New Logon/selection waits for old-context unload on owner 3;
its acknowledgement returns to Tx, the native body is requeued, the old map is
invalidated and later work uses the new connection. Duplicate old load/unload
completions cannot affect new work. Its clock change also verifies fresh headers.
All application map mutations/lookups assert their owner; cross-owner posts carry
retained immutable handles and fixed metadata only. These are fake-owner checks,
not concurrent-scheduler or actual network evidence.

The completed stage 5 mapping is in [SESSION.md](SESSION.md). Run focused
instrumentation with `make -C zfix/test asan-test ASAN_TESTS='Managed'`.

`ZfixSeqTxTest::deferredWake` is a deterministic regression for an output
writability wake while replay history is pending. Consuming the wake before
lookup completion must retain the active resend gap and resume it exactly once
when the lookup completes. `ZfixLinkTxTest::publication` also checks the original
publication timestamp/body/context supplied to application history observers.


`ZfixRawTest` covers external control body encode/restore, supplied headers and
tracking metadata, standalone frames without managed required fields, retained
buffer identity, exact-one-frame/checksum/type validation and frame limits.
`ZfixWireTest::rawMetadata` verifies that only raw admission accepts zero
sequence/epoch metadata and that completion preserves those values. Both are
included in source and installed unit suites. Use `make -C zfix/test asan-test
ASAN_TESTS='Raw Wire TxMsg'` for focused instrumentation.


The attachment-registry unit test and its source/installed/ASan registrations
have been removed. Persistent-link replacement regression coverage is tracked
in `../../zfix_repair2.md`; earlier passing registry tests do not establish
replacement correctness.

`ZfixSeqTest` includes count-limit admission independently of retained bytes,
duplicate handling at capacity, capacity reuse after gap-fill purge, count-zero
in-order progress, and missing-sequence recovery with a full count window.
`ZfixRxTest` checks managed count-limit overload before business dispatch.

`ZfixTraceTest` covers credential/custom-field redaction, built-in Logon 95/96
secrecy with ordinary replay 95/96 remaining visible, malformed/unknown pairs and
scoped group metadata, escaped log-line injection,
invalid envelopes, disabled capture, byte-budget refusal, rotation, truncation
and observer-owned records surviving eviction. `asan-trace-test` instruments the
trace implementation and fixture; `installed-trace-test` compiles against a
staged prefix. Wire/Rx/Tx/Ctl tests also verify protocol/queue counters across
pause, duplicate completion, association refusal, failed output and reconnect.

`ZfixLinkTxTest::queueClose(true)` closes the physical Tx coordinator without a
terminal lifecycle event. It checks pending association refusal/release,
idempotent close, stale-close isolation and later reconnect progress. The normal
event-driven close variant remains covered too.

`ZfixLinkTxTest::controlMemory` checks that the control queue refuses a jumbo body
when only exposed buffer capacity fits, admits exactly one full allocation/node
reservation, and releases/reuses the charge on physical close/reconnect.

`ZfixLinkTxTest` checks directional Logon receive-capacity advertisement and
independent Tx encoding bounds in every managed mode.

Catalog descriptors accept `MT<Object, MsgType, Facet, Selector, Responses>`.
`NoDiscrim` selects a unique MsgType; `Discrim<Tag, Value>` declares
one root-scoped secondary wire key. Same-MsgType entries must share that tag and
use disjoint values. `Responses` is an explicit type list with unique entries;
it supplies no correlation index or response-count policy. `ZfixLinkTest` checks
a request with two differently selected execution reports. Five additional
schema-negative cases reject duplicate discriminator values, selectors on
different tags for the same MsgType, qualified/unqualified overlap, duplicate
response types and an envelope/MsgType secondary tag. Runtime scanning and native typed receive are implemented; typed send and reply are implemented.

`ZfixDispatchTest` exercises primary/secondary catalog dispatch with distinct
objects and facets, unmodeled root selectors, empty values, valid nested groups
with distinct tags, selector-looking text, and invalid inactive scalar values.
It checks selected required/value/group failures, duplicate keys, header ordering,
exact selected-layout budgets, envelope field limits, wire error locations,
application refusal and borrowed/owning copies. The inspector uses no parse slots.
Catalog session-prefix checks cover unknown extensions before required identity
and sequence fields, schema-derived binary boundaries and conservative
unknown-MsgType inspection. Dispatch callbacks receive the entry and one object;
RequestContext exposes individual retained metadata getters, without Header or
Trailer accessors.
Schema cases 20–21 reject immediate and nested group discriminator declarations.
`ZfixFrameTest` covers the required 8/9/35 order and rejects misplaced MsgType.

Run `make -C zfix/test asan-dispatch-test` to instrument the codec sources and
new templates, or `installed-dispatch-test ZFIX_PREFIX=/tmp/zfix-stage/opt/z` to
compile the standalone dispatch consumer against staged headers/libraries. It
links only libZfixFmt and foundation libraries. ZfixDemoTest and the separate
application-policy integration fixture exercise application-owned policy;
see ../itest/README.md.

`ZfixContextTest` checks empty contexts, copied enclosing-message retention,
borrowed values after the original context is cleared, metadata/timestamps and
safe rejection when an application omits a typed receive overload. Run
`asan-context-test` for the instrumented fixture/templates (configured dependency
DSOs are unchanged), or `installed-context-test ZFIX_PREFIX=/tmp/zfix-stage/opt/z`
for a consumer using staged headers/libraries. Native session TCP/TLS fixtures
also retain a context and borrowed ID until both hubs and links are destroyed.
Recovery TCP/TLS fixtures verify typed receive context during PossDup replay,
including recovery before the new physical connection becomes Ready.

`ZfixSelectTest` checks outbound descriptor/selector agreement in four scopes:
known body keys, empty selectors, appended unmodeled keys, exact archive restore,
duplicate/missing/mismatched history keys, raw conversion and final header-key
validation. It also checks selected parse/output bounds, allocation-free selector
inspection with zero parse budget, and RawOutput::fromFrame envelope-policy refusal.
`asan-select-test` instruments the fixture/templates, codec and TxFrame sources;
configured foundation DSOs remain unchanged. `installed-select-test` uses staged
artifacts. `send-schema-test` must fail the catalog-membership assertion when a
native client attempts to send a foreign descriptor. Default `test` includes it.
Native session fixtures send typed messages from both roles and check public
wrong-owner/quota refusal without advancing sequence progress; raw fixtures send
typed server responses under explicit external-manager header/sequence policy.

`ZfixContextTest` now has five scopes. Reply-policy checks cover missing overloads,
unchanged refusal errors, explicit replay acceptance and repeated authorization
of one retained context. Selection checks bind copied contexts to MsgType plus
the chosen discriminator declaration and reject another same-MsgType variant.
`reply-schema-test` compiles a positive fixture and requires two failures:
a response absent from Request::Responses and a declared response outside the
endpoint catalog. Default test includes these with the existing 22 negative cases.

Native managed fixtures check empty/wrong-discriminator/wrong-MsgType contexts
before policy, unchanged application refusal, retry after refusal, and replies
initiated in either role across all three managed modes. Raw fixtures exercise
explicit external-manager response policy and distinct request/response MsgTypes.

`ZfixWorkTest` checks count/byte boundaries, zero limits, overflow, live tuning,
shared-reference retention, deferred Rx retirement and work references captured
in ZmFn lambdas (four scopes). `asan-work-test` instruments the fixture, work
ledger implementation and template ownership code; dependency DSOs are unchanged.
`installed-work-test` compiles against staged headers/libraries.
Native session TCP/TLS fixtures additionally exercise per-link/hub refusal,
exact-fit/tight-byte admission, pending-retirement charging, worker access to
borrowed input, forced close while work is held and reset drain withholding its
proof until work retires. Each scenario runs in all three managed modes. Raw
fixtures move text input through a worker and typed reply under work admission.

`ZfixRestoreTest` verifies direct owner restoration without a session string key:
Rx/Tx cursor and recovery-range validation, sequence limits, epoch/lifecycle
guards, changed wire headers and zero allocation charge after reconstruction.

## Binary migration verification (2026-11-01)

This earlier gate records the initial codec, Trailer removal and timestamp
snapshot. The Header-free binary/signing verification below supersedes its
implementation status; these earlier counts remain historical.

Verified with Ubuntu clang 21.1.8, GNU C++2b,
`-march=x86-64-v2 -O3 -g -DNDEBUG`, using the existing compatible prerequisites.
No prerequisite modules were rebuilt, cleaned or installed. Builds ran with
`make -C zfix/src -j8` before `make -C zfix/test -j8`, followed by
`make -C zfix/test test`: 29 binaries, 359 top-level TAP tests, and the
positive/38-negative schema plus send/reply compilation gates passed.
The `zfix/util`, `zfix/itest`, `zfix/bench` and `zfix/interop` directories also
built with `-j8`. QuickFIX is disabled in this configuration, so the interop
build covered its native consumer.

`make -C zfix/itest test` passed with local network access: six TCP fixtures
(127 top-level tests), six TLS fixtures (132), the demo fixtures and the native
client/server matrix. An initial sandbox run could not start local listeners;
the successful run used system/network access. No sanitizer builds or final
Valgrind gate were run for this incomplete migration.

An exploratory `zfix/bench/nativecodec 10000 1000` run exercised ordinary
170-byte scalar and 910-byte group frames. It ran while other builds were
active and is not a controlled performance comparison; ordinary/binary
comparisons remain an acceptance requirement.


## Header-free binary and signing verification (2026-11-01)

The complete source unit target passes with Ubuntu clang 21.1.8, GNU C++2b,
`-march=x86-64-v2 -O3 -g -DNDEBUG`: 30 binaries and 391 top-level TAP tests,
plus the positive/38-negative schema and typed send/reply compile gates.
Source, utility, example, integration, benchmark and interop default builds
are warning-free. No prerequisite module was built, cleaned or installed.

The current native integration target passes six TCP fixtures (130 tests),
six TLS fixtures (135), the demo (two) and the native client/server matrix
(one TAP test covering 16 process cases). Signed binary records exercise real
fragmentation, partial writes and writability retries. Managed signing covers
all three session modes, administrative Logon authentication, application HMAC
verification and exact borrowed 96/89 addresses after retained-context teardown.

The independent QuickFIX gate passes twelve canonical frames, five workload
scopes, ten dependency scopes and six SDK diagnostic scenarios. Binary XML
String/Bytes, Logon authentication and signature payloads retain their exact
bytes and original native/peer input addresses. The pinned existing SDK and
canonical dictionary were reused.

A staged SDK uses hash-verified existing prerequisites. All 30 installed unit
consumers pass 391 tests and resolve every Z dependency from the stage. The
new regressions cover nullable components, constructor-required fields,
archive parse-budget boundaries and constructor-bound binary borrowing.
See [the benchmark report](../bench/BINARY.md) for measurements and their limits.

The final installed integration gate also passes twelve harness runs and 248
TAP tests, including the 16-case native client/server matrix. All 45 installed
unit/native executables resolve their Z libraries from the stage. The 30 public
Zfix headers match source, and all 257 original prerequisite/runtime artifact
hashes remain unchanged. The final SDK was installed only from `zfix/src`.

The final memory gate runs through `libtool exec valgrind` on 15 core binaries
(162 top-level TAP tests), including codec/group/union, framing, dispatch,
selection, retained contexts, Logon, trace, raw/managed delivery, signing,
restore and TxBody. Valgrind 3.26.0 reports zero memory errors and zero definite,
indirect or possible lost bytes. No sanitizer builds were used.
