# Zfix measurements

The current codec supports ordinary SOH-delimited fields and declared dual-tag
Binary String/Bytes fields. `nativecodec` keeps the ordinary complete-frame
workload for regression comparison; `binarycodec` measures body serialization and
borrowed live/indexed decoding over configurable payload sizes. `txbodycodec`
measures the actual retained-body metadata footprint and pooled allocation
thresholds for dense and sparse schemas. Their source and command descriptions
appear below. The final clang release measurements, source audit and raw archive
are in [BINARY.md](BINARY.md), including the measured allocation/growth-copy cost
at dense and sparse pool boundaries.

The historical [one-pass receive comparison](results/ONEPASS.md) records its
compile-time tag dispatch and streaming decoder, release validation, codec
timings and all four process pairings. That snapshot used SOH-delimited text and
omitted Length/Data workloads. Its measurements and the other archives below
describe their recorded implementations, not the new Binary/API snapshot.

The [release acceptor investigation](results/ACCEPTOR.md) compares all four
Zfix/QuickFIX client/server pairings over TCP and TLS, with 10K orders per
session, one and four sessions and three repetitions. It includes the original
release baseline, controlled changes and the final comparison, CPU samples
and a QuickFIX decoder experiment. The original native worker assignment is
retained after measuring the peer-dependent cost of sharing I/O owners.
Ordinary execution reports omit OrigClOrdID and declare their scalar fields
in tag order.
The [earlier ASan comparison](results/README.md#10k-orders-at-one-and-four-sessions)
and the original measurement snapshot below remain historical. Current-build
functional verification is recorded in [STATUS.md](../STATUS.md). Historical
installation and timing results do not establish the new Binary/API gate.

The dispatch inspection source is [inspect.cc](results/repair3/inspect.cc).
Generated comparison reports are local run output and are not distributed.

The [recorded baseline](RESULTS.md) covers all four pairings over TCP/TLS,
six profiles and three repetitions, with separate allocation/copy diagnostics.
Its [raw evidence](results/README.md) includes manifests, individual results,
stderr and codec provenance. The build is optimized and ASan-instrumented with
reused debug/ASan dependencies; it is not an ordinary release baseline.
The published baseline tables use the post-Link run with identical fixed codec
timestamps. The archive keeps the original and rejected pre-fix snapshots
separate, alongside current verification evidence; see [results/README.md](results/README.md).

After the source and dependent-consumer build gates, build configured benchmark
targets and collect ordinary uninstrumented timing first:

```sh
make -C zfix/bench -j8
make -C zfix/bench codec-bench
```

Keep timing runs separate from builds and diagnostics. After timing, the existing
Linux observers can be built inside this module with `make -C zfix/bench heaptrace`
and, when needed, `make -C zfix/bench audittrace`. The Binary acceptance run uses
heaptrace separately from uninstrumented timings. These targets do not rebuild
prerequisite modules or the independent QuickFIX SDK.

`nativecodec` and `quickfixcodec` accept optional count and warmup arguments:

```sh
zfix/bench/nativecodec 100000 10000
zfix/bench/quickfixcodec 100000 10000
```

`dispatchcodec` times dispatch against a preframed execution report, excluding
encoding and framing. It compares primary lookup, secondary lookup with one
shared object schema, and secondary lookup with different candidate schemas,
using zero or eight parties.
The `managed-primary` and `managed-secondary` rows additionally include catalog
session-field observation into the retained Rx metadata before dispatch. These rows
exclude transport framing and sequencing, just as the other rows do.
Dispatch then loads the one ordinary object schema; there is no separate header
representation or prefix-reuse parameter.
Count and warmup are optional:

```sh
zfix/bench/dispatchcodec 20000 2000
```

An optional third argument selects one mode, for example `managed-primary` or
`same-schema`. Use separate processes for allocation-heavy comparisons so earlier
modes do not change allocator state.

`nativecodec` and `quickfixcodec` emit JSON rows for scalar and
eight-party/four-subparty groups with 32 text bytes. Each row contains encode/decode latency
quantiles and the empty monotonic-clock-pair distribution. Clock overhead is
reported without subtracting quantiles. Both use the fixed timestamp
`20261028-12:34:56.123456789`, giving equal 170/910-byte scalar/group
frames despite their selected timestamp precision rules. Both use the same FIX 4.4 shape; native
typed validation and QuickFIX strict canonical dictionary validation remain
distinct. QuickFIX decode includes constructing its input `std::string` and
copying selected fields into the fixture arena; native typed spans borrow the
input. These measurements include each foreign validation/ownership cost and
do not compare raw parsers alone. Preserve exact commands, tool hashes and
compiler/SDK metadata with the process manifest from the same build.
Debug checks establish functionality,
without constituting final performance evidence.

## Four-pairing process loads

For the one/four-session scalar comparison, use a separate artifact directory
per session count. `--count` and `--warmup` apply to each session, so four
sessions measure 40,000 orders per case and allow 256 orders in flight overall:

```sh
for sessions in 1 4; do
  python3 zfix/bench/load.py \
    --dictionary /tmp/zfix-qf-release/quickfix-2ce8a60667d95a55cdc57a210f165e19cb757126/spec/FIX44.xml \
    --artifacts /tmp/zfix-10k-s$sessions \
    --count 10000 --warmup 1000 --sessions "$sessions" --inflight 64 \
    --repeat 3 --profile scalar --build-label clang21-O3-release
done
python3 zfix/bench/results.py /tmp/zfix-10k-s1 /tmp/zfix-10k-s4 \
  --output /tmp/zfix-10k-comparison.md
```

The reducer accepts one or more artifact roots from the same build and workload,
keeps session counts separate, and rejects different executable/library hashes
or workload settings. It adds client/server comparison and process-resource
tables, retains individual session quantiles, and writes a machine-readable
comparison beside the Markdown report. Shared-window throughput uses all
measured completions from the first session's measured start to the last
session's measured finish in the same client process. The sum of independent
session rates is retained alongside it. Latency quantiles remain separate.

For the full six-profile matrix:

```sh
python3 zfix/bench/load.py \
  --dictionary /tmp/zfix-deps/install/share/quickfix/FIX44.xml \
  --artifacts /tmp/zfix-load --count 10000 --warmup 1000 \
  --rate 0 --inflight 64 --repeat 3 --framework-heaps \
  --build-label configured-release
```

The default matrix includes all four initiator/acceptor pairings over TCP and
verified TLS. Separate local peer processes use their ordinary libtool wrappers.
Each pairing receives the same profile parameters: scalar orders; group-heavy
orders (32 parties/eight subparties); 4096 text bytes; fragmentation of text
orders; recovery of one lost first-measured acknowledgement; and scalar overload
at a declared high offered rate. `--profile`, `--pair` and `--transport` select
subsets. Count, warmup, sessions, rate, overload rate, in-flight limit, group and
text dimensions, seed and history/queue bounds are configurable; see `--help`.

Readiness comes from actual JSON events. Process deadlines diagnose failures;
the fixture does not sleep to establish ordering. Raw outputs and failures are
always retained, with exact argv, exit status, unfinished work and timeouts.
Fragmentation/replay use the verified Python relay in aggregate-summary mode,
without per-message timing traces. Aggregate faults/chunks prove the requested
fault occurred; detailed FIX replay/original-time correctness belongs to the
independent interoperability gate. Relay profiles include Python processing and
TLS termination/re-encryption, so compare them with the same relay profile.

`report.md` includes offered/admitted/completed work, reports, in-flight work,
errors, measured order rates, wire counts/rates, per-session acknowledgement and
terminal latency quantiles, intended-submit scheduling delay, local CPU/RSS and
history-cache retained-byte peaks. Intended-submit-to-acknowledgement/terminal
columns appear when supplied by the helper results; missing metrics are marked
unavailable. Quantiles remain separate per session and repeat. Shared-window
and sum-of-session order rates use client measured phases; wire rates use the complete case wall
interval, including setup, warmup and teardown.

The harness uses `wait4` to measure each local child, including the libtool
wrapper's startup, warmup and shutdown. CPU/RSS therefore cover a broader interval
than the client's measured latency phase. The fork/exec RSS high-water mark can
include an inherited pre-exec memory baseline, which can dominate small runs;
it does not isolate workload resident memory. Local resources are reported for the
relay separately in raw results. Remote server resources are unmeasured.
`manifest.json` records CPU/affinity, compiler flags/version, actual ELF and
linked-DSO hashes/section sizes, dictionary and SDK pins, and validation/logging/
archive/counter differences. Its `sourceFiles` map hashes substantive Zfix
sources in each role directory plus `ZmPQueue.hh`, `Ztcp.hh` and `Ztls.hh`,
including dirty/untracked sources; generated/build directories are excluded.
The Git HEAD alone does not identify that workspace. Native history retains
replayable bodies while QuickFIX retains serialized frames; matching byte/count limits do not make
those stored representations identical. QuickFIX wire counters count SDK wire
attempts; native counters count socket completion. Byte counters describe
serialized FIX frames, excluding TLS record and TCP/IP overhead.

`--framework-heaps` requests one cold `heap` JSON observation per peer through
`ZFIX_HEAPSTATS=1`. The logical-drain sample sums current `Ztc::HeapMgr` arenas:
framework allocation calls and live bytes, excluding SDK/STL/libc allocations.
It measures neither peak RSS nor total process heap usage. A requested missing
or malformed observation fails the run; otherwise the event is optional.

## Separate allocation/copy diagnostics

Add `--heaptrace /absolute/path/to/heaptrace.so` to repeat each case in a
separate diagnostic process. The harness uses `libtool --mode=execute env` to
set the observer environment only after resolving the real peer executable.
Python, libtool and the relay are not preloaded. Diagnostic rows are excluded
from timing results and must not be combined with ASan/Valgrind instrumentation.

The Linux/glibc observer counts successful dynamic allocation calls, non-null
free calls and memcpy/memmove calls, requested allocation bytes and copied bytes
after initialization through its exit hook. A realloc adds its complete requested
size; requested bytes are cumulative rather than net live bytes. The observer
excludes inlined copies and custom allocator internals. Counts
include startup/warmup/shutdown and are observations of libc calls, rather than
a count of every framework allocation or every message-path copy.

For instrumented peers, use the mutually exclusive
`--audittrace /absolute/path/to/audittrace.so` instead. The Linux x64/glibc audit
observer uses `LD_AUDIT` without replacing the allocator, and ASan/LSan remain
enabled. The harness installs `LD_AUDIT` and an absolute `ZFIX_AUDITTRACE_FILE`
only for each real peer through `libtool --mode=execute env`; Python, libtool
and the relay remain unobserved. It runs separate diagnostic copies of the same
workload and excludes them from timing tables and the report reducer.

The audit scope is main-namespace PLT entries from `la_preinit` through its exit
hook. Selected entries are malloc/calloc/realloc/aligned_alloc/posix_memalign,
free, memcpy/memmove and __asan_memcpy/__asan_memmove. Allocation counters count
attempts, not successful allocations or live bytes; requested bytes include the
full realloc request. Overflowing calloc products are omitted from requested
bytes and counted in `requestOverflows`. Free counts include null pointers.
Copies count entry lengths and can include nested calls; `asanCopyCalls` and
`asanCopyBytes` are subsets of the copy totals, rather than additional copies.
Other APIs and direct/local/inlined/non-PLT/custom allocator internal calls are
unobserved. These semantics differ from the preload observer and are reported
in a separate table. The manifest pins the actual selected observer shared
library and its source alongside the peer artifacts.

## Client-only remote service

```sh
python3 zfix/bench/load.py \
  --dictionary /tmp/zfix-deps/install/share/quickfix/FIX44.xml \
  --remote-host peer.example --remote-ports 4430 --remote-client native \
  --remote-server-label independent-peer --ca /path/to/trusted-ca.pem \
  --transport tls --profile scalar --repeat 1 --sender BENCH-C --target PEER-S \
  --artifacts /tmp/zfix-remote
```

The harness launches only the selected client. The operator configures the
remote listener's workload/identity/session state and lifecycle independently;
the harness does not stop or reconfigure it. Remote TLS requires an explicit CA
and verifies the host name. With a relay profile, the client verifies the local
relay while the relay verifies the configured remote CA and reference identity.
Record remote binary/build/resource evidence separately; the local manifest
does not imply those properties are known.

Further builds and tests are confined to Zfix by the user's scope correction.
Reuse existing ABI-compatible dependency artifacts and record their actual
compiler flags and instrumentation. An instrumented dependency set does not
provide an uninstrumented whole-framework release baseline. `--build-label`
labels a run without proving its build configuration.
See [status](../STATUS.md) and [the plan](../../zfix_plan.md) for configuration,
instrumentation scope and delivery evidence. Verification is scoped to Linux
x64; ARM and Windows are outside this task.

`z.config` does not forward `--with-quickfix`; supply the isolated SDK selection
through an explicit configure site file:

The release example requires matching uninstrumented dependencies already
available. With existing ASan artifacts, preserve sanitizer compile/link flags
and label measurements accordingly instead of applying this example unchanged.

```sh
./z.config -L -G -P -k /tmp/zfix-qf-release/install /opt/z
make -C zfix clean
make -C zfix -j8
```

This selects Clang for the Zfix-only build, reusing compatible dependencies. Repeat through
the wrapper's GCC configuration for that compiler gate. Sanitizer configurations
use their own instrumented SDK prefix and compiler/link flags, separate from
release measurements; preserve each actual configuration and build log.

After collecting release process and codec output from the same configured
build, generate the combined report:

```sh
python3 zfix/bench/results.py /tmp/zfix-load \
  --codec /tmp/nativecodec.jsonl --codec /tmp/quickfixcodec.jsonl \
  --output /tmp/zfix-load/release-report.md
```

The reducer reports median individual run rates/p50 values and worst individual
p99 values without merging distributions. It writes `budgets.json` alongside
the artifacts: a host-specific regression envelope of 1.25 times the largest
observed intended-submit-to-terminal p99, rounded up. This measured comparison
envelope is not a portable threshold or production SLA. Preserve raw reports,
metadata and run-to-run distributions with it.

## Binary codec isolation

`binarycodec` measures body serialization and borrowed decoding separately from
framing, checksums, signing and transport. It runs an ordinary String control,
Binary String and Binary Bytes through live and indexed paths. The ordinary
control payload contains letters without SOH; Binary payloads contain SOH, NUL,
`=`, high octets and field/checksum-looking text. These distributions are
intentionally different, so use the ordinary full-frame `nativecodec` rows to
track text-only regressions rather than interpreting a binary/text ratio as an
identical-input comparison.

The arguments are measured count, warmup count, payload octets, optional mode,
and optional phase (`both`, `encode` or `decode`). Modes are `text-live`,
`text-indexed`, `string-live`, `string-indexed`, `bytes-live`, and `bytes-indexed`.
An omitted mode emits all six. Each JSON row records payload/wire extents,
latency quantiles, clock overhead and a consumed-result accumulator. Buffers and
samples are sized from the workload before measurement; borrowed payload
addresses and full byte equality are checked before the timing loop, and each
measured decode checks the direct input address and extent again.

```sh
make -C zfix/bench -j8
zfix/bench/nativecodec 100000 10000
for size in 0 32 4096 65536; do
  zfix/bench/binarycodec 100000 10000 "$size"
done
zfix/bench/binarycodec 100000 10000 4096 bytes-live decode
```

Keep timing runs isolated from builds and instrumentation and repeat them on a
fixed CPU where possible. Save the exact commands, compiler/flags and executable
hashes with each result. For allocation/copy diagnostics, run a single mode and
phase with `heaptrace.so`/`audittrace.so` in separate processes and compare counts
at multiple iteration counts and payload sizes. The observers include setup,
sample storage and quantile/output work and cannot see inlined copies or custom
allocator internals. Consequently, use count/size scaling plus pointer-identity
checks and source inspection to assess borrowed paths; observer totals alone
cannot prove zero payload allocation or copy. No payload temporary is needed for
serialization into the preallocated sink.

## Retained-body allocation thresholds

`txbodycodec COUNT WARMUP BYTES [dense|sparse]` separately measures
`TxBody::encode` and its actual retained pool footprint. Dense has three logical
root leaves. Sparse has the same emitted wire fields plus twelve omitted
optional leaves. Identical body bytes therefore expose metadata/capture overhead
without conflating it with payload copies or extra wire fields. JSON records
logical leaves, encoded body extent, retained `IOBuf::length`, allocated capacity
and `TxBody::memory()` charge; the recorded extents describe the actual build
rather than assuming a particular internal range layout. Buffer destruction is
outside the encode timing interval and before the next allocation, so the pool
can reuse it.

Scan payload sizes around the current pool transition, then time both sides of
each observed threshold. For example:

```sh
for size in 800 832 864 896 928 960 992 1024; do
  zfix/bench/txbodycodec 10000 1000 "$size"
done
```

Run dense/sparse allocation diagnostics in separate processes as well. Omitted
fields must not add another allocation per retained body; crossing the built-in
pool threshold can legitimately change capacity/charge and must be reported.
The full `nativecodec` timing remains necessary to detect ordinary message
regressions outside this isolated retained-body factory.

## Managed plain FIX over TCP

The [2026-11-15 native TCP milestone](../../zutcp/bench/MILESTONE.md) records
the matched kernel/native results and their measurement conditions.

`zfixbench` drives managed `Zfix::Client` and `Zfix::Server` sessions through
`Zfix::TCP`, `Ztcp`, `Zi`, and either kernel TCP or `Zutcp`/`Zxdp`. It uses the
same role/backend positional CLI as `zutcpbench`:

```sh
zfix/bench/zfixbench server|client kernel|native INTERFACE ADDRESS MESSAGES \
  WARMUP PIPELINE CONNECTIONS RESERVE RX_CPU TX_CPU SPIN [SAMPLES.csv]
zfix/bench/zfixbench --selftest
```

`RESERVE` sets the native ingress, TCP packet, and application Tx pool counts
per endpoint. Size it above both the total pipeline window and the aggregate
TCP ingress retention limit (256 frames per connection by default), with
headroom for control and in-flight packets.

`BENCH_WORKLOAD=order|marketdata|updates` selects typed FIX message pairs.
`BENCH_BURST_SIZE` and `BENCH_BURST_US` pace application offers in bursts;
zero burst size sends as pipeline credit permits.

`BENCH_CONGESTION_CONTROL=0|1` selects the native stack's congestion policy
(default `1`). It is read once before startup and reported as
`congestion_control` in the configuration line. `0` disables native congestion
control and pacing while retaining receiver flow control and retransmission.
Kernel endpoints retain their normal congestion control and report `1`.

Each endpoint configures exactly three scheduler slots: sid 1 owns Rx, sid 2
owns Tx, and isolated sid 3 services XDP transmission. Managed application
callbacks use the existing Rx/Tx slots. Both kernel and native runs configure
the same three slots; kernel runs leave sid 3 idle. `BENCH_APP_COLOCATE` defaults
to `1` and accepts only `1`; separate `BENCH_APP_RX_CPU`/`BENCH_APP_TX_CPU`
settings do not change the application placement. The `config` and `result`
lines report `app_colocate=1`. These slot counts exclude the scheduler's
control/timer thread.

`BENCH_LL=1` selects continuous scheduler-ring spinning; the default `0` spins
for the configured count before waiting. It is read once at startup, applies
equally to kernel and native endpoints, and is reported as `ll` in the config
line. Continuous mode also spins the kernel endpoint's idle sid 3; assign all
six endpoint slots explicit CPUs when comparing this mode.

`BENCH_QUEUE_SIZE` selects 65536 bytes (the benchmark default) or 131072 bytes
(the scheduler default) for each slot. Traffic and calibration read it once at
startup; apply the same setting to both endpoints and every compared backend.
`queue_size` reports the actual capacity. After workers stop, `scheduler_queue`
reports each ring's existing counters and capacity, plus `overflow_in`,
`overflow_out`, and `overflow_count` from its fallback queue. Ring input and
`full` counters can lose concurrent updates; they are diagnostic observations,
not exact delivery counts. Fallback counters are protected and final occupancy
is read after stop. Counters cover the whole process lifecycle, have finite
width, and do not measure peak backlog or isolate the timed interval.

`BENCH_ALLOC_TUNE=1` enables a bounded cold metadata-cache and hash-sizing
policy for traffic mode; unset or `0` leaves allocator/hash defaults unchanged.
Other values fail argument validation. It is read once, before logging, State,
workers, transport stacks and hubs are constructed. Kernel and native endpoints
receive the same policy; selftest and generator calibration do not apply it.

The policy configures partition 0, size-class shift 0 caches with these slot
capacities: `ZiIOBuf` 256; `Zutcp.Pkt.OutstandingRange` 512;
`Zutcp.Pkt.TxSentNode` 2048; `Zutcp.Connection.TxRanges` 1024;
`Zutcp.Connection.RxQueue` 64; `Zfix.Rx` and `Zfix.WireQueue` 128 each;
`Zfix.Tx` 4096; and `Zfix.TxBody` and `Zfix.TCP.Rx` 8192 each. Counts round up
observed object peaks from the 128-connection order workload. They apply to
each instantiated size class of the named heap, with normal backing allocation
still available when a cache is empty. They do not resize UMEM, packet pools,
producer admission limits or scheduler rings. `ZmScheduler.Fn` is not tuned.

`Zutcp.Endpoints`, `Zutcp.Endpoint.TupleRouter`, `Zutcp.ServerLinks` and
the multiplexer request hash (configured through `ZiMxParams::requestHash()`,
default key `ZiMultiplex.RequestHash`) round connections up to a power of two,
with a minimum of 32 buckets. Existing load factors and lock-sharding defaults
are retained. Configuration must precede construction to affect these tables.
Its telemetry ID remains `ZiMultiplex.Connect`; the telemetry ID is not its
configuration key. The `config` line reports the actual `request_hash` key,
`allocation_tuning` and `hash_bits` (zero when disabled);
enabled runs also print each `heap_plan` and an `allocation_tuning` summary.
Its `observed_bytes_estimate` uses previously observed size classes, not an
absolute memory limit or actual allocation. Sum the actual heap CSV rows'
`size * cacheSize` for instantiated cache storage, and retain `heapAllocs`,
queue `full` and fallback counters when evaluating the policy.

Set `BENCH_TELEMETRY_DIR` to a separate fresh existing directory for each endpoint
and `BENCH_TELEMETRY_PEER_DIR` to its peer's directory. Both settings are read once
at initialization. After all measured replies finish, the endpoints rendezvous
through `ready`/`dumped` files before starting logout. This retains live connection
queues for `queue-live.csv`, `hash-live.csv`, and `heap-live.csv` while keeping
file I/O outside the measured exchange interval. After worker shutdown,
`queue-drained.csv` and `heap-drained.csv` capture the remaining registered owners.
The queue CSV enumerates `ZiMultiplex::allQueues`, including plugin queues.
Dump failures fail the run while preserving normal teardown.
Check hash `resized` values above zero and compare cache-served allocations with
backing-heap allocations. Global heap totals repeat across arena rows and must
not be summed repeatedly. Report each scheduler queue's telemetry `full` count
alongside its fallback-entry counters; later posts can join an existing fallback
backlog without another ring-full event.

The `traffic_usage` line reports process-wide `getrusage` deltas: `user_us`,
`system_us`, `voluntary`, and `involuntary`. Two main-thread snapshots bracket
listen/connect through measured completion, before the first telemetry barrier
write. This covers connection setup, warmup and measured traffic, excluding
CSV output, barrier waits, logout and final drain. It is not an active-only
measurement or a syscall count. Keep whole-job `/usr/bin/time -v` results
separate: they include reporting and teardown, whose work differs by backend.

`BENCH_CLIENT_XDP_TX_CPU` and `BENCH_SERVER_XDP_TX_CPU` select the CPU for each
endpoint's sid 3. Each value is read once at startup and must name an allowed
CPU. Without an override, sid 3 is explicitly assigned the corresponding Rx
CPU. The config line reports `scheduler_threads=3`, `xdp_tx_sid=3`,
`native_xdp_sender_threads`, and `xdp_tx_cpu`. Verify actual affinity separately
when profiling. XDP transmission uses scheduler `run()` on sid 3; there is no
additional sender pthread or sender semaphore. A pending-descriptor counter
coalesces zero-to-one notifications; sid 3 calls nonblocking `sendto()` and uses
`BENCH_SPIN` for idle probing before returning to the scheduler. There is no Tx
wait timeout or Tx epoll/eventfd. Native Rx retains its epoll/eventfd mechanism.
Record separate sender CPU assignments in comparisons.

The managed corpus uses the same representative order/execution, market-data
request/snapshot, and update/feedback business fields as the fixed direct Zi
corpus in `zutcp/bench`. The FIX group counts describe one typed entry each;
both update directions use the same `X` message schema and body. Its serial
business IDs retain the direct corpus's 10-character formatting. Managed
SendingTime, sequence numbers, Logon, and Logout remain session controlled, so
full frames and process costs differ from the direct byte-stream benchmark.
`check-wire.py` validates captured FIX framing, checksums, session controls,
exact typed business fields and ordering, frame sizes, and TCP byte identity.
Use a bridge capture for native/native evidence. Small functional smoke runs:

```sh
sudo unshare -n -- bash zutcp/bench/private-veth.sh \
  "$PWD/zfix/bench/zfixbench" kernel 4 /tmp/zfix-kernel-smoke \
  0 1 2 3 10 2 1 1 1024 0 check 0
sudo unshare -n -- bash zutcp/bench/private-veth.sh \
  "$PWD/zfix/bench/zfixbench" native 4 /tmp/zfix-native-smoke \
  0 1 2 3 10 2 1 1 1024 0 check 0
```

These commands check basic function only. Managed application callbacks share
the multiplexer Rx/Tx slots. Any `BENCH_CLIENT_APP_RX_CPU`,
`BENCH_CLIENT_APP_TX_CPU`, `BENCH_SERVER_APP_RX_CPU`, or
`BENCH_SERVER_APP_TX_CPU` override must match its role's Rx/Tx CPU.
Assign the third slot through `BENCH_CLIENT_XDP_TX_CPU` and
`BENCH_SERVER_XDP_TX_CPU`. The fixture does not
invoke the managed wire checker automatically. Run `check-wire.py` on captured
directories before treating them as wire evidence.

### Fixed scheduled offers

Set `BENCH_OFFER_RATE` to a positive integer requests/second **per connection**,
and `BENCH_PENDING_CAP` to the maximum due but not admitted requests per connection.
Both are read once at initialization. Rate mode is incompatible with legacy
`BENCH_BURST_SIZE`; an unset rate leaves the existing feedback/burst timing intact.
A cap without a rate is rejected. Rate is at most one billion; cap is at most two
million. The final scheduled offer must be less than 110 seconds after the origin,
within the existing 120-second application and 150-second fixture deadlines.
The existing two-million aggregate sample and bounded pipeline limits still apply.

All client connections finish warmup before a common measured origin is chosen.
With zero warmup they join this barrier when their FIX sessions become ready.
Connections start aligned: measured request index `j` is due at
`origin + floor(j * 1,000,000,000 / rate)` nanoseconds. Deadlines are immutable.
There is no pending packet allocation: sequence counters encode unadmitted work.
The application admits the oldest due request into its bounded pipeline immediately
before formatting it. A full pipeline waits for a response; the existing per-link
timer services the next offer or the first deadline that would exceed the pending
cap. Late service does not reset the offer clock. Exceeding the cap or failing to
complete is a failed row, never a lower offered-rate success.

Scheduled sampling uses `CLOCK_MONOTONIC`. The existing scheduler uses realtime,
so each timer arm converts the remaining monotonic interval to an absolute
scheduler deadline and the callback rechecks its due time. `offer_clock` records
the common monotonic origin, the observation time, and initial/final realtime
clock offsets. No new worker or scheduler implementation is introduced.

In rate mode, the client CSV has these columns:

```
connection,sequence,scheduled_ns,admitted_ns,completed_ns,status
```

All times are offsets from the reported common origin. Every planned request is
written, including failed runs; missing admission/completion values are blank.
Status is `unadmitted`, `admitted`, or `completed`. A zero scheduled offset is valid.
`offer_progress` reports planned/due/admitted/completed counts, missing counts,
`pending` and `pending_peak`. The latter is the maximum observed in application
service, not an exact failed-run queue high-water mark: another connection may
stop before servicing its timer. `pending` is max(due minus admitted, zero).
On failure, due uses the observation cutoff while admitted/completed totals
include work collected through drain; these are not one atomic snapshot. Raw
admission/completion timestamps permit separate cutoff counts. Due counts use the first failure
or main-thread stop observation, before teardown; callbacks which finish while
draining remain visible in the raw timestamps. If phase start was never reached,
origin and due are zero and the planned offsets have no realized absolute epoch.

Exact runs report loaded latency (`completed - scheduled`), admission delay
(`admitted - scheduled`) and service latency (`completed - admitted`) separately.
Each includes p50, p99, p99.9, maximum and counts above 1, 10 and 100 milliseconds.
The usual `measurement` quantiles are loaded latency in this mode. Request rate
uses common origin through last completed response, including backlog/drain;
`offered_requests_per_second` separately reports rate times connections. Failure
rows retain partial records but do not emit a successful latency measurement.
Analysis must inspect process exit status and exact delivery, not just percentiles.

Scheduled storage is two `uint64_t` timestamps per planned request (at most 32 MB),
allocated before work. One at-most-16 MB scratch array is reused for distributions
after workers stop. Legacy CSV remains `connection,sequence,rtt_ns` and legacy
RTT remains pre-format offer through validated response.

Every mode now reports `measured_rx_bytes`, the exact validated business FIX frame
bytes received after warmup. Server totals are request bytes; client totals are
response bytes. Administrative traffic and retransmitted TCP bytes do not count.
For exact runs, sum both totals and divide by the client measured envelope for
useful FIX frame bytes/second; retain each direction separately. This includes FIX
framing and excludes TCP/IP/Ethernet overhead. For failed runs retain the two
partial totals separately; their sum is not completed round-trip bytes. Endpoint
connection IDs do not necessarily correspond across independently accepted links.
The new shared receive counter changes measured application work, so legacy
comparisons also require a fresh baseline with this same executable.

`--selftest` includes rational schedule boundaries (including non-divisor rate
three), delayed-service timestamp decomposition, finite terminal count, exact
pending-cap expiry and the changed overflow deadline after a response frees a
slot. Live validation must additionally cover zero/nonzero warmup, all backends
and families, deliberate overload with complete partial CSV output, teardown and
independent capture agreement for request/response byte totals. The veth fixture
collects both endpoint statuses on a failed case and never writes its `complete`
marker when either fails.

On failure as well as normal completion, main keeps the multiplexer running until
all initiated client links or actually accepted server links have reported their
terminal FIX callback. Client admission and timers are revoked on Rx before
waiting. Server hub stop supplies the listener/transport barrier before the
accepted-link count is read. The `drain` line reports expected and observed
terminal counts; hub finalization follows this completion handshake.

Before accepting a scheduled campaign, independently calibrate generator/timer
headroom at its declared rate/connection grid. An unloaded calibration does not
prove headroom under actual Rx work. Predeclare any generator/environment
invalidation rule for whole comparison groups; transport backpressure, pending
cap overflow and unfinished offers remain failed workload observations. This
mode itself does not certify calibration or performance parity.

### Offer generator calibration

`--calibrate` measures the unloaded scheduled-offer generator on a `ZiMultiplex`
with no connections, listeners or plugin. It does not construct a FIX session:

```sh
zfix/bench/zfixbench --calibrate MESSAGES CONNECTIONS RATE CAP TURN \
  RX_CPU TX_CPU SENDER_CPU SPIN [SAMPLES.csv]
```

`RATE` is offers/second per connection and `CAP` bounds due but unobserved offers
per connection. `TURN` bounds offers serviced by one callback; it is distinct
from the scheduler's 1,000 ns quantum. Use the comparison's pipeline value for
`TURN`. The same message/connection/sample/rate/cap bounds as scheduled workload
mode apply. `BENCH_LL` and `BENCH_QUEUE_SIZE` are read once at initialization;
other benchmark environment settings do not select a transport or alter this
diagnostic's explicit arguments.
All three requested CPUs must be in the process's allowed affinity set.

The diagnostic configures exactly three isolated scheduler slots, each with the
same selected queue size (64 KiB by default), spin setting and scheduler quantum
as the benchmark. Rx is
slot 1; the configured Tx and sender slots 2 and 3 are idle during measurement.
A startup callback on each slot records its actual CPU before timing begins.
All connection streams share one aligned monotonic origin. They reuse the exact
`OfferSchedule` arithmetic and monotonic-to-realtime timer conversion used by
the scheduled workload. One future timer or one immediate same-owner continuation
continues each stream. A callback emits at most `TURN` due offers, preserving their
original deadlines; it does not wait for a response or format a FIX message.
The fixed timer array uses the benchmark's existing 128-connection limit.

Storage uses the same bounded, preallocated pair of uint64 timestamps per sample.
The first timestamp records service of a due offer. The second follows its first
store. Their difference includes due-count/cap checks, bookkeeping and the second
clock read; it is not an isolated memory-store measurement or network completion.
No per-offer allocation is added. Raw CSV columns are:

```
connection,sequence,scheduled_ns,observed_ns,recorded_ns,status
```

Times are offsets from the shared `calibration_clock origin_ns`. All planned
rows are emitted after the scheduler stops, including failures. Unobserved rows
have blank observed/recorded fields and status `pending`; observed rows have
status `observed`. `calibration_connection` reports counts, the observed pending
peak and valid service calls by initial/timer/continuation source. On timeout,
due counts use the main-thread cutoff while observations can continue until the
Rx cancellation callback; these are not one atomic snapshot. The raw timestamps
retain that distinction. A cap violation or deadline failure returns nonzero,
reports `exact=0`, and emits no successful latency/rate summary.

On complete runs, `latency metric=generator` measures observed minus scheduled
and `metric=recording` measures recorded minus observed. `calibration_rate`
reports the final recorded timestamp's envelope and the aggregate nominal rate.
Actual CPUs and the three-slot configuration accompany the result. Teardown
revokes work and cancels every timer on Rx, posts a same-owner drain continuation,
and stops the scheduler before destroying timer/sample owners. `calibration_drain`
records the stream count and scheduler stop result.

The unloaded multiplexer exercises the common kernel I/O owner's event-loop
wake path with Rx quantum 8. It omits native plugin/AF_XDP Rx service, FIX
formatting/validation, transport work, response callbacks and their contention.
A successful high-rate calibration establishes only unloaded generator/timestamp/
timer headroom. It cannot justify excluding transport backpressure, pending-cap
overflow or missing responses from matched-load results, and it does not prove
headroom under the real workload. Freeze the finite calibration grid and its
interpretation before collection. The shared schedule selftest includes rounded
rate-three boundaries, delayed service and the two-million-sample arithmetic
boundary at 240,000 offers/second. Live checks should additionally cover a single
zero-offset offer, future timers, bounded catch-up, cap failure, retained partial
records and timer-owner teardown.
