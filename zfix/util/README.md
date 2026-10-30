# Zfix workload tools

The current managed replacement repair is built and verified under the existing
configuration: native18, canonical76 and lifecycle24 process cases pass; see
[STATUS.md](../STATUS.md) for exact scope and retained evidence. Measurement
archives describe their earlier implementation snapshot and were not rerun.

`zfix` and `zfixd` are separate native initiator/acceptor processes using the
canonical FIX 4.4 order/execution workload. TLS is the default, with certificate
verification enabled. With existing dependencies available, build `zfix/src`
first and then `make -C zfix/util -j8`.

```sh
# Terminal 1: use the actual ephemeral port from the JSON ready event.
./zfix/util/zfixd --port 0 --cert server.pem --key server.key
# Terminal 2: replace PORT with that port; defaults match the acceptor.
./zfix/util/zfix --host localhost --port PORT --ca ca.pem
```

Use `--tcp` on both processes to select plain TCP. A native `zfixd` has one
listener per logical session. `--sessions N --port 0` opens N ephemeral listeners
and publishes their ordered ports as JSON. Pass that list to the client as
`--sessions N --ports P1,P2,...`. With a nonzero base port, listeners use consecutive
ports. Session sender IDs are `--sender` (default `ZFIX-C`) plus `-1`, `-2`, etc.;
the target ID defaults to `ZFIX-S`. Expected IDs derive from the listener index,
independently of connection arrival order. All client sessions complete Logon
before any workload starts.

`--id ID` sets the native application's logical session-key prefix, default
`ZFIX`. Per-slot keys are `ID-C-N` for clients and `ID-S-N` for servers, with
one-based N. These application keys are independent of sender/target headers,
version and mode. This naming is a utility policy, not a library key format.

Counts, warmup, rate and in-flight limits are **per session**. Defaults are 1000
measured orders, 100 warmup orders, 64 in-flight orders and saturation (`--rate 0`).
`--duration SECONDS` stops measured offering at that duration or `--count`,
whichever comes first, and drains accepted calls. A nonzero rate uses intended
submission times rather than sleeping after each order; lateness is reported in
`scheduleNs`. Warmup completion starts each session's measured window.

`--deadline` bounds startup/warmup/work and the normal close wait; it must exceed
an explicit duration. Error or close timeout forces local retirement and waits
for actual logical stop callbacks before retiring owners. Native DNS is explicitly
stopped/finalized. Pacing timers belong to AppRx and are cancelled during stop.
Latency arrays move to the reporting thread and sort after the runtime stops.

`--scenario groups` selects a 32-party/eight-subparty shape by default. Explicit
`--parties`, `--subs`, `--text` and `--binary` control shape; zero default values
select the group's convenience defaults under that named scenario. The `orders`
scenario permits zero groups/subgroups. `--scenario cancel` keeps the initial
order open after its acknowledgement, then sends an OrderCancelRequest with a
fresh ClOrdID and the original ID; a canceled ExecutionReport completes it.
`--scenario replace` sends a fresh OrderCancelReplaceRequest after acknowledgement,
changes both quantity and price, and requires a replaced report before the final
fill. OrderID stays constant and every report has a distinct ExecID. Original IDs
link follow-up reports to the original order. These are utility application
policies shared with the QuickFIX tools.

`--seed` controls deterministic quantity
and price generation. Order/execution IDs have separate numeric counters with a
16-bit session namespace and 48-bit counter; ID comparison never guesses wire
sequence identity. Each session's IDs remain distinct from every other session.

Managed mode defaults to `--mode 4 --version FIX.4.4`. Other native modes `44A`
and `T11` are selectable independently of the declared version; for ordinary
FIXT use `--mode T11 --version FIXT.1.1`. The business dictionary remains explicitly
FIX 4.4 and FIXT application version is 6. `--heartbeat` controls the Logon interval.
Fresh-process recovery uses `--state-out FILE` on the first client and
`--state-in FILE --key-offset N` on its replacement, where N is the first
process's completed order count. The checkpoint binds that completed prefix,
application session ID, Rx/Tx progress and bounded original Tx bodies/timestamps.
Main reads it before workers start and writes it only after owners stop. It is a
version-3 fixture-local native binary format, not a framework journal. Each
`Checkpoint.id` is persisted as the application key; saved headers, mode,
version and qualifier are absent. Matching requires the ID and epoch, permitting
changed wire configuration on restart while validating current connection
headers normally. Its restored progress carries no historical queue-memory debt;
an empty operational
queue begins with zero charge, and reloaded content is charged when installed.

Run the acceptor with `--connections 2 --count 8 --warmup 0` and each client
with `--count 4 --warmup 0`. After the first client exits, wait for the server's
`reconnect_ready` JSON event before starting the second. Its `owner-drained`
barrier follows old-link Rx/Tx teardown and release of the active-link guard.
The acceptor retains its application results and replay history across links.
Add `--reset-logon` to the acceptor and second client for an explicitly agreed
fresh epoch with sequence 1 and ResetSeqNumFlag=Y; the second client then omits
`--state-in` and still uses `--key-offset 4` for fresh business IDs. Ordinary
recovery keeps the epoch and sequences. These lifecycle profiles require one
session, no warmup and the orders/groups workflow. Historical responses in the
validated completed prefix increment `oldReports` and never complete fresh calls.
For a checkpoint-lag recovery fixture, add `--recover-rx N` to the two-connection
server, separately from reset. After the first complete owner drain it rewinds
only its application receive checkpoint to N, preserving business results and
sender history. The replacement client's high Logon then drives recovery from
its persisted original messages. Replayed known business IDs reuse cached
results; they never create another order. N must precede the old receive cursor.

`--queue-bytes` defaults to 16 MiB per link for unsequenced output/posted submit/
core wire limits. `--retain` defaults to 4096 records and `--cache-bytes` to 64 MiB,
independently bounding each application result cache and Tx replay-history cache.
Application replay retention starts at publication, before transport flush.
The core keeps only operational assignments until contiguous publication or a
held abort, and Wire keeps admitted output through local completion. Local
completion releases no history and advances no peer acknowledgement. Every
resend requests its content from the application cache; an unavailable record
permits protocol gap fill. This utility retains records across archive-range
notifications until its own count/byte eviction policy removes them. No core
replay cache or payload-release handshake exists.
A duplicate known business key reuses execution IDs. Generated request keys
increase on each session's application owner. A first delivery with a key above
the last accepted key is accepted even when FIX marks it as replay; this permits
recovery of a request lost before the application saw it. An unknown older key
is refused, including without the replay flag, so aged-out requests cannot create
new work. The highwater advances only after result-cache admission. It is
application state that must accompany business recovery across process restart.
Administrative traffic does not depend on business
history-cache admission.

Each process emits a `ready` event then one JSON `result` per session and exits
nonzero on failure. Initiator `progress` covers the measured window; acceptor
progress includes warmup. Counts describe whole workflows: follow-up cancel or
replace requests consume no additional order admission or in-flight slot. Cancel
has two reports; replace has three. Acknowledgement and terminal latency start at
the initial order submission. Admission means local handoff, separate from wire
publication, local completion and business completion. `rxBytes`/`txBytes` are FIX
frame bytes, including control traffic; they exclude TLS record overhead.
Quantiles are client-local monotonic nanoseconds.
`ackNs` and `terminalNs` begin at actual submission. `intendedAckNs` and
`intendedTerminalNs` begin at that call's intended paced time, including queue
lateness; their samples retain the same timestamp through cancel/replace.
Saturation uses actual submission as intended time. Each full interval is
sampled directly; quantiles are never added together. `scheduleNs` records only
the corresponding intended-to-actual submission lag.

With `ZFIX_HEAPSTATS=1`, each process emits one separate `heap` JSON event at
logical drain before framework worker arenas stop. `frameworkAllocCalls` sums
Z heap cache and heap allocation calls; `frameworkLiveBytes` sums outstanding
blocks times their arena size. The scope is process framework heaps and excludes
foreign SDK/STL/libc allocation and RSS. No sampling runs on a message path.

`retainedPeak` reports charged
Tx history allocations, including pool rounding and cache nodes. These debug
correctness runs are not a performance budget or benchmark result.

The utility directory contains native workload programs and reusable support.
Isolated workload/model/mapping tests run from [test](../test/README.md);
native end-to-end process suites and installed-tool checks run from
[itest](../itest/README.md). Shared `pair-test.py` supervision supplies the JSON
readiness/result interface, ephemeral listeners and verified localhost
certificates to integration, interoperability and benchmark harnesses. Its
`pair_cases` workload parameters and `run_pair` profile runner are shared by the
native integration fixture and the independent matrix; neither emits TAP or
requires an integration-test program.

## Shared workload support

`load.hh/.cc` supplies shared logical order/execution models, deterministic
message shapes, bounded application-owned call accounting and exact latency
samples. It depends on the foundation through Zf and includes neither FIX parser
nor session engine. The native and independent peers can share these workload
objects while implementing their own wire conversion and session handling.

`Model` constructs group/text/binary storage once per session and mutates only
scalar key, quantity, price and timestamp for the next order. Its seeded
SplitMix64 permutation is identical across peers. The price has two decimal
places; IDs are explicit numeric workload IDs. Generated binary data includes
NUL, SOH and equals bytes. Party roles/subtypes cycle through canonical values
so larger group shapes remain dictionary-valid.

`Calls` is a single application owner's bounded registry. The caller registers
before submission, explicitly reports local refusal and releases completed
records. It accepts a distinct peer OrderID and requires it to stay consistent.
Acknowledgement and fill use distinct nonzero execution IDs; duplicate reports
are counted once, unknown business keys create no call, and quantity/price/
status inconsistencies fail validation. A fill may arrive before acknowledgement;
business completion waits for both. Refusal cannot discard a partially answered
call. Clearing storage preserves the unfinished count for final diagnostics.

`Samples` reserves storage from the caller's measured-count limit before timing,
records nanoseconds without allocation and calculates exact nearest-rank
p50/p95/p99/p99.9/max after sorting outside the measured work. Warmup calls do
not contribute samples. Acknowledgement/fill arrival latency is measured
independently of the order in which reports arrive. `monotonic()` uses the OS
monotonic clock (QPC on Windows); wall-clock FIX timestamps remain separate.

`load_fix.hh/.cc` supplies native FIX 4.4 D/8/F/G mappings and typed catalog
entries. Explicit render facets select absent optional text/data fields for
scalar shapes. Empty repeating groups retain an explicit zero count, as required
by the codec contract. Receive mappings include all supported fields; BeginString
does not choose the workload schema. The live cancel/replace scenarios use these
declarations in both native and independent peers.

`ZfixLoadTest`, `ZfixLoadFIXTest` and `ZfixLoadRunTest` live in
[test](../test/README.md), including their ASan and installed-consumer targets.
They check shared policy/model/sample accounting, native FIX mappings,
deterministic phase/pacing/duration behavior, refusal, cache quotas, CLI parsing,
deferred sample lifetimes and session-ID namespaces. `load_run.hh/.cc` is
reusable by the independent peer and includes no native parser/session headers.

The explicit `interop/nativecodec` fixture writes canonical frames with
`--frames DIR` and validates peer-rebuilt values with `--check-frames DIR`.
Its frame writer is shared with the mapping unit suite through
`zfix_load_frame.hh`; no second conversion implementation is introduced.
See [interop](../interop/README.md) for the bidirectional check and default
QuickFIX negative expectations.

`libZfixLoad.la` and `libZfixLoadFIX.la` are private reusable convenience libraries
for tools, examples, integration tests and benchmarks. They add no pending-call
registry or dependency to `libZfix`. Installed-consumer checks compile these
helper sources against installed framework headers/libraries.

The independent QuickFIX roles and four-pairing canonical matrix, including
cancel/replace, pass over TCP/TLS. Fault, identity/TLS, duplicate-session and
checkpoint/restart/reset process gates have recorded evidence in ../STATUS.md
and reproducible entry points in ../interop/README.md. The complete lifecycle
matrix passes 24 cases: sixteen resume/reset plus eight stale-Rx checkpoint
recovery cases, covering every pairing over TCP/TLS. Recovery verifies persisted
old-order PossDup/body hashes/original timestamps, ordinary gap fills, cached
server results and ignored old client reports while completing exactly eight
distinct business orders. Evidence: `/tmp/zfix-pq-repair/lifecycle-four-pair-recovery2.tap`
and `/tmp/zfix-lifecycle-uq27nrth/`. Current compiler, memory, measurement and installed-consumer evidence is
recorded in [STATUS.md](../STATUS.md); post-measurement test-role changes have
separate affected-check evidence.
