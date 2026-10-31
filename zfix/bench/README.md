# Zfix measurements

The managed repair changes resend ownership and readiness after the recorded
measurement snapshot. Current-build functional verification is recorded in
[STATUS.md](../STATUS.md); installation has not been repeated for this repair.
All published numbers/manifests are historical; no benchmark rerun, archive
rewrite or new performance claim accompanies this migration. Commands here document
measurement entry points; that migration covered Zfix rebuilds and functional
tests only.

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

Build the configured targets before running measurements:

```sh
make -C zfix/bench -j8
make -C zfix/bench codec-bench
make -C zfix/bench heaptrace
make -C zfix/bench audittrace
```

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
header inspection and reuse that decoded prefix during dispatch. These rows
exclude transport framing and sequencing, just as the other rows do.
The matching `rescan-*` rows decode the header again to isolate the cost of
that repeated work in the same binary.
Count and warmup are optional:

```sh
zfix/bench/dispatchcodec 20000 2000
```

An optional third argument selects one mode, for example `managed-primary` or
`rescan-primary`. Use separate processes for allocation-heavy comparisons so
earlier modes do not change the sanitizer allocator's quarantine state.

Both emit three JSON rows for scalar, eight-party/four-subparty groups with
32 text bytes, and 4096 binary bytes. Each row contains encode/decode latency
quantiles and the empty monotonic-clock-pair distribution. Clock overhead is
reported without subtracting quantiles. Both use the fixed timestamp
`20261028-12:34:56.123456789`, giving equal 170/910/4281-byte scalar/group/binary
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
orders (32 parties/eight subparties); 4096 binary bytes; fragmentation of binary
orders; recovery of one lost first-measured acknowledgement; and scalar overload
at a declared high offered rate. `--profile`, `--pair` and `--transport` select
subsets. Count, warmup, sessions, rate, overload rate, in-flight limit, group and
binary dimensions, seed and history/queue bounds are configurable; see `--help`.

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
unavailable. Quantiles remain separate per session and repeat. Sum-of-session
order rates use client measured phases; wire rates use the complete case wall
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
cat > /tmp/zfix-release.site <<'EOF'
with_quickfix=/tmp/zfix-deps/install
EOF
CONFIG_SITE=/tmp/zfix-release.site ./z.config -L -G -P /opt/z
make -C zfix clean
make -C zfix -j8
```

This selects Clang for the Zfix-only build, reusing compatible dependencies. Repeat through
the wrapper's GCC configuration for that compiler gate. Sanitizer configurations
use their own site file and instrumented SDK prefix, separate from release
measurements; preserve each actual configuration and build log.

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
