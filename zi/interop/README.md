For the `refactor.md` adapter reorganization, retain the current configuration.
Use `ZIFB_BENCH_CURRENT=1` to run identical before/after workloads when that
configuration is debug. This bypasses only the harness startup guard; compiler
flags, workload, and correctness prechecks are unchanged. Compare native ZiFB
before/after results, not canonical-library release performance. The historical
release acceptance and evidence below retain their original scope.

# FlatBuffers interoperability and pooled comparison

## Current repair acceptance

`repair2.md` governs the current Zdb repair. Retain the existing release
configuration and incremental artifacts; historical debug measurements below
are evidence from their recorded sources, not current repair acceptance.
Milestone19 passed the owning codec/I/O tests and canonical Zdb message
interchange. A refreshed `make -C zi/interop -j4` and `make -C zi/interop test`
remain part of final integration, alongside the full enabled suite and memory
gate. The test-only growth observer added in milestone20 is not yet accepted.

Current saga definitions use `ZiFB::save(SagaPayload&, definition)`: generic
scratch construction followed by one retained-payload insertion, preserving
builtin/heap tuning and returning codec errors through failed submission.
The earlier pooled-saga serializer audit below describes an older state.
Transmit streams instead build in their detached growable `ZiIOBuf` and hand
off its owner where the completed payload meets the stream's emitted-buffer
limit. Growth relocation, detach compaction and splitting remain separate costs.


Performance comparisons require matching release builds for both codecs and
canonical reflection. Collect acceptance measurements on an idle machine;
debug and instrumented results are diagnostic only. The benchmark requires
`NDEBUG`.

This explicit target compares `ZiFB::IOBuilder` with the frozen custom
canonical `Zfb::IOBuilder` baseline in `iobaseline.hh`. It requires canonical
FlatBuffers >= 24 and matching `flatc`; ordinary builds do not traverse this
directory. `zifbinterop` verifies codec and schema compatibility in this same
module. Build both targets with `make -C zi/interop -j4`; run the codec checks
with `make -C zi/interop test`.

After owning libraries are built under the retained configuration:

```
make -C zi/interop -j4
zi/interop/zifbiobench 10000 64 7
zi/interop/zifbiobench 100000 64 7 ops
zi/interop/zifbiobench 100000 64 7 wideops
```

Arguments are count, payload size, sample count, and optional `wide`, `ops`,
`wideops`, `vectors`, `write` or `widewrite` mode. Correctness checks run before timing. Compiler barriers prevent repeated
reads from being hoisted and make the complete constructed object observable,
including fields outside the checksum; they emit no CPU instructions. Use `libtool exec`
with GDB and `trace-growth.py` for separate allocator-request and growth-copy measurements on the retained x86-64 build;
never interpret instrumented runs as latency measurements.

The `ops` and `wideops` modes also report `verify_canonical_schema` and
`verify_native_schema`. Each gives both verifiers identical BFBS and payload
bytes, distinguishing metadata-layout cost from verifier implementation cost.
Schema emission/parsing and borrowed-handle setup remain outside timing.
The original `verify_reflected` comparison still uses each codec's own BFBS.
All verification preflights must succeed; two false results are a failure.

[Recorded measurements](codec-history.md) retain their original
commands and filenames. The benchmark formerly lived in `zf/interop` as
`zffbiobench`; the codec and pooled I/O implementation now share the Zi module.
The move changes no benchmark algorithm or baseline behavior. Regeneration
through `./z.config -c -L -d /usr` and the moved target's incremental Clang
debug build completed without compiler diagnostics.

Both borrowed JSON paths explicitly use `ZfFieldFilter::All`: Row and Wide
contain no excluded fields, and canonical adapters expose read-only getters.
Full output still has to equal ordinary object JSON before timing. This also
avoids comparing separate instantiations of the same byte-formatting routine
solely because one side named `All` and the other used the default filter.
The corrected comparison ran seven alternating samples at 64, 1024 and
65536 bytes with canonical reflection compiled under the same debug flags.
Results are retained as `zi/interop/evidence/m2-shared-{ops,wideops}-SIZE.csv`.
All correctness prechecks passed. In these debug diagnostics, narrow load
deficits recur (22–33%) and narrow printing ranges from 2.7% faster to 5.3%
slower. These observations do not establish release performance; forced
inlining introduced in response has been removed. Collect matching release
measurements before drawing performance conclusions or targeting optimization.

## Constructor metadata release rerun — 2026-10-09

The corrected benchmark was subsequently rebuilt incrementally with `-j4` and
rerun in the retained release configuration. Three fresh processes per workload,
each with 11 rounds of one million operations, show narrow-load native medians
of 1.974–2.015 ns versus canonical 2.005–2.042 ns. Wide-load native medians
are 61.605–62.103 ns versus canonical 71.663–72.507 ns (14–15% faster).
The former narrow-load deficit is absent. Both save comparisons also favor
native; all embedded prechecks pass. No separate tests or dependent rebuilds
were run. See [raw results and settings](../../zi/interop/evidence/ctor-metadata-release/README.md).
This supersedes the audit's initial unmeasured status for the benchmark only.

## Constructor metadata audit — 2026-10-09

`Row` and `Wide` now mark their aggregate members with `Ctor<N>` in C++
member order. Previously the native load value-initialized the aggregate and
then assigned its fields; the canonical comparator assigned a default-initialized
aggregate. Static inspection traced the extra narrow-load zero store to this
fixture omission. Existing constructor metadata supports direct construction;
no codec change or constructor/default contract change is needed.

The audit inventoried 68 metadata-bearing files under benchmark, interop,
unit-test and integration-test directories. Corrections cover these fixtures:

| Source | Types |
| --- | --- |
| `zi/interop/zifbiobench.cc` | `Row`, `Wide` |
| `zi/interop/zifbinterop.cc` | `Bitmaps`, `Evolved` |
| `zi/test/ZiFBTest.hh` | `StreamTest::Row` |
| `zi/test/ZiFBTest.hh` | `UnionTest::Message` |
| `zi/test/ZiFBIOTest.cc` | `Address` |
| `zdb_sqlite/itest/zdbsltypes.hh` | `AllTypes` |
| `zdbus/interop/zdbusinteroptest.cc` | `TextBody`, `ErrorBody`, `UIntBody`, `Inner`, `ComplexBody` |
| `zdbus/itest/zdbusclienttest.cc` | `IDReply`, `ErrorBody` |
| `zf/test/ZfCBORTest.cc` | `IntKeyObject`, `MinIntKeyObject`, `LongKeyObject` |

Constructor indices follow member/constructor order, independently of wire IDs
and render order. Defaults, requiredness, keys and mutability are retained.
Read-only accessors, schema/view/save-only fixtures, configuration loads,
transforming setters and intentional `Init` coverage remain unchanged.
In particular, `ZiFBTest::LoadTest`, `ZfSkipTest` and `ZfCSVTest::CSVInit` retain
mixed construction/initialization and load/update coverage. Imported production
metadata and the database order/saga fixtures already specify their constructor
arguments.

This correction received source review only: no build, test or benchmark was
run. Existing measurement archives describe the preceding sources. The earlier
claim that the narrow-load difference was an unavoidable adapter construction
contract is withdrawn; performance of the corrected fixture is unmeasured.

## Allocation contract audit

The migration retains the caller's `ZiIOBuf` allocator and tuning. Source
comparison against the pre-migration implementation confirms:

- `ZiIOBuf.hh` is unchanged: default inline size 980, maximum size 100 MiB,
  64-byte allocation alignment, and the `ZiIOBuf` heap identity.
- `ZiFB::IOBuilder` retains 8-byte default arena alignment, caller headroom,
  downward growth through `ZiIOBuf::realloc`, and transfer of the existing
  owner on detach. Growth adds `max(request, capacity / 2)` and rounds to
  arena alignment, matching canonical `vector_downward::reallocate`.
- Zdb retains its 192-byte row default, 128-byte heartbeat allocation,
  100 MiB limit, and type-specific `ZdbBufSize`/`ZdbBufHeapID` hooks.
- Telemetry retains its existing application request/message/alert, agent,
  hub frame, and inventory allocator declarations and heap identities.
- The audit caught two saga serializers using generic save scratch instead
  of their old pools. They now use `IOBufAlloc<SagaData>` (`Zdb.Saga.Buf`)
  and `SagaImage::Buf` (`Zum.Saga.Image`) with the original size parameters
  and retained-copy behavior. Their release functional checks passed; focused
  PostgreSQL/SQLite saga and ZumTest Valgrind runs were clean. The database
  report retains the first memory-store scheduler-exit leak observation and
  the unchanged clean reruns. A controlled debugger schedule subsequently
  established that global teardown could overtake detached control-thread
  cleanup. The joinable-thread lifetime fix passes focused regression and
  Valgrind checks. The rebuilt memory-store saga now also passes all 79 cases
  under Valgrind with zero errors or lost allocations. Rebuilt PostgreSQL and
  SQLite saga checks also pass all 12 cases each with zero errors or lost
  allocations. Remaining application checks are pending. See
  [plan.md](../../plan.md) for the evidence and its limits.

New schema buffers use `Zdb.Schema`; these replace static generated schema
artifacts and are required to be owned by each table. This source audit alone
does not establish allocation/copy or performance acceptance.

## Remaining performance coverage

The current Row/Wide harness covers pooled serialization, byte/scalar vectors,
strings, typed/borrowed/reflected reads, verification and JSON output. The
telemetry harness adds nested union envelopes and routing copies. These alone
do not discharge the full plan. Reflected writes, inline/table vectors and
nested replication have initial matching release measurements in
[the initial batch](../../zi/interop/evidence/m2-release-initial), with later
vector/growth fixes recorded in
[the focused batches](../../zi/interop/evidence/m2-release-hotpath).
Refresh the complete matrix against the final rebuilt state and resolve the
remaining regressions. Reuse the existing interop fixtures and pooled baseline;
keep schema construction and field association outside timed operations.

### Inline/table vector comparison

`./zi/interop/zifbiobench 100000 1024 11 vectors` uses the existing pooled
fresh/reuse timing driver. The payload length determines the number of inline
points and child tables (one of each per `sizeof(Point)` bytes). Both writers
are independently verified and every field/element is compared before timing.
Canonical inline structs are constructed directly in the downward buffer;
child offsets retain the original `Zfb::Save::vectorIter` `ZmScratch` policy.
Native offsets use `ZtScratch`; both use stack storage with heap fallback.
The mode is built and has release measurements, including the later
[65536-byte vector comparison](../../zi/interop/evidence/m2-release-hotpath/vector-padding-snapshot.txt).
These results do not close the complete performance matrix or the remaining
reflected-store-write and replication acceptance gaps.

### Reflected writer comparison

`write` and `widewrite` select the Row and Wide schema-driven writer kernels,
for example `./zi/interop/zifbiobench 100000 1024 11 widewrite`. Native code
uses the same `Reflect::saveScalar`/`saveOffset` primitives as store tuple
writers; the canonical baseline reads each cached reflection field's vtable
offset and default before calling the pooled builder. Both write the same
native values in declaration order. Schema creation and field association
are outside timing. Both outputs are checked against the opposite reader
before fresh/reuse samples. Native-value conversion and database scheduling
are outside this kernel; this is not an end-to-end Zdb store throughput benchmark.
The initial release measurements are retained as `write-SIZE.csv` and
`widewrite-SIZE.csv` in the initial batch linked above. Refreshed same-process measurements for the restored current state are recorded
in [runtime83](../../zdb/interop/evidence/repair2-20261010/runtime83/README.md).
They exclude postgres conversion and scheduling and do not close the remaining
end-to-end store performance discrepancy.

Zero-length benchmark spans keep one backing element while retaining length
zero, so both codecs serialize present empty vectors. An absent native span
must not be compared against a canonical present-empty vector when measuring
work or encoded size. This applies to byte, inline and table vectors.

### Release allocation trace

`trace-growth.py` now uses the retained release build's allocation-cache entry.
It separates fixed-pool requests from variable-heap requests and includes
`Bench::run` setup/teardown. The original allocation-only traces omitted unavailable growth/copy counters
rather than returning misleading zeroes. Narrow/wide 64-byte and vector 65536-byte
three-iteration traces match canonical/native allocation requests; see
[scope and raw evidence](../../zi/interop/evidence/m2-release-hotpath/allocation-review.txt).
This does not close the full allocation/copy or latency acceptance gates.

The subsequent [growth-copy trace](../../zi/interop/evidence/m2-release-hotpath/copy-review.txt)
measures actual memcpy/memmove call-site sizes and capacity-extension entries.
It fails if the retained x86-64 release compiler changes the expected three
copy sites per growth function. Row/wide 64-byte cases copy zero growth bytes;
65536-byte vectors copy 294916 bytes in reuse mode and 884748 bytes across
three fresh iterations, identically for both codecs. Allocation requests
and nonempty capacity extensions also match. Native empty initial extensions
are reported separately and introduce no extra physical allocation or copy.
Payload/scratch copies and full application allocation parity remain outside
these measurements; debugger timings do not establish performance acceptance.

### Configured fixed pool versus heap fallback

By default the benchmark leaves the `ZiIOBuf` fixed pool unconfigured, so
fresh owners use heap fallback. To compare configured pooling explicitly:

```
ZIFB_BENCH_POOL=4 taskset -c 6 /tmp/zifb-release-bench 1000000 64 11
ZIFB_BENCH_POOL=4 taskset -c 6 /tmp/zifb-release-bench 1000000 64 11 wide
```

The optional setting affects both codecs and no production defaults. Four
blocks cover the preflight, active and next fresh owners. Separate traces
confirm zero empty-pool requests with this setting, compared with every
request using fallback without it. See [raw results and scope](../../zi/interop/evidence/m2-release-hotpath/pool-review.txt).
Configured-pool fresh medians (canonical/native) are 67.454/63.051 ns for
narrow rows and 108.984/109.959 ns for wide rows. Default fallback remains
101.049/105.717 ns and 152.890/165.013 ns respectively. Retain both regimes;
these round-mean medians do not establish message-tail latency acceptance.

### Direct transmit versus generic staging

`./zi/interop/zifbiobench 100000 64 11 tx` compares the same native codec
through direct TxSink buffer construction and through a generic span destination
which inserts the completed scratch output into an otherwise identical TxSink.
Rounds alternate order. Preflight requires identical bytes, both canonical and
native verification, and the expected completed-span byte count. Timings include
fresh pooled-owner allocation, encoding, flush and synchronous sink consumption;
there is no network or scheduler. The timed sink exposes each complete emitted
region without copying it to an observer buffer. CSV reports emitted bytes,
pooled-buffer allocation requests, sends and completed-span insertion bytes.
Backing growth/relocation is accounted separately by ZiFBIOTest; these pool
request counts do not include backing reallocations or arbitrary allocations.
Use small, near-capacity and oversized payloads to distinguish direct handoff,
scratch growth/compaction and splitting. Staged is a mechanism control using the
current native codec, not the matched HEAD baseline. The mode has been compiled and measured in the current release configuration.
The [recorded comparison](../../zdb/interop/evidence/repair2-20261010/tx69/README.md)
covers payload64/928/944/4096 with11 alternating rounds. Direct output below the
aligned emission limit avoids completed-span insertion and improves median round
time about5–6%; oversized fallback adds one pooled-owner request and is slower
than staged output. This is a native mechanism control, not a HEAD comparison.
