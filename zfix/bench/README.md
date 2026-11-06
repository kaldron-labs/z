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

The dispatch simplification has a separate instrumented comparison in
[REPAIR3.md](REPAIR3.md), with raw observations in [results/repair3](results/repair3).
It does not replace the historical process-load measurements below.

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
