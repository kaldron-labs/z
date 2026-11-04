For the `refactor.md` adapter reorganization, retain the current configuration.
Use `ZFB_BENCH_CURRENT=1` to run identical before/after workloads when that
configuration is debug. This bypasses only the harness startup guard; compiler
flags, workload, and correctness prechecks are unchanged. Compare native Zfb
before/after results, not canonical-library release performance. The historical
release acceptance and evidence below retain their original scope.

# FlatBuffers interoperability and pooled comparison

## Current repair acceptance

`repair2.md` governs the current Zdb repair. Retain the existing release
configuration and incremental artifacts; historical debug measurements below
are evidence from their recorded sources, not current repair acceptance.
Milestone19 passed the owning codec/I/O tests and canonical Zdb message
interchange. A refreshed `make -C zfb/interop -j4` and `make -C zfb/interop test`
remain part of final integration, alongside the full enabled suite and memory
gate. The test-only growth observer added in milestone20 is not yet accepted.

Current saga definitions use `Zfb::save(SagaPayload&, definition)`: generic
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

This explicit target compares `Zfb::IOBuilder` with the frozen custom
canonical `Zfb::IOBuilder` baseline in `iobaseline.hh`. It requires canonical
FlatBuffers >= 24 and matching `flatc`; ordinary builds do not traverse this
directory. `zfbinterop` verifies codec and schema compatibility in this same
module. Build both targets with `make -C zfb/interop -j4`; run the codec checks
with `make -C zfb/interop test`.

After owning libraries are built under the retained configuration:

```
make -C zfb/interop -j4
zfb/interop/zfbiobench 10000 64 7
zfb/interop/zfbiobench 100000 64 7 ops
zfb/interop/zfbiobench 100000 64 7 wideops
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
Results are retained as `zfb/interop/evidence/m2-shared-{ops,wideops}-SIZE.csv`.
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
were run. See [raw results and settings](../../zfb/interop/evidence/ctor-metadata-release/README.md).
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
| `zfb/interop/zfbiobench.cc` | `Row`, `Wide` |
| `zfb/interop/zfbinterop.cc` | `Bitmaps`, `Evolved` |
| `zfb/test/ZfbTest.hh` | `StreamTest::Row` |
| `zfb/test/ZfbTest.hh` | `UnionTest::Message` |
| `zfb/test/ZfbIOTest.cc` | `Address` |
| `zdb_sqlite/itest/zdbsltypes.hh` | `AllTypes` |
| `zdbus/interop/zdbusinteroptest.cc` | `TextBody`, `ErrorBody`, `UIntBody`, `Inner`, `ComplexBody` |
| `zdbus/itest/zdbusclienttest.cc` | `IDReply`, `ErrorBody` |
| `zf/test/ZfCBORTest.cc` | `IntKeyObject`, `MinIntKeyObject`, `LongKeyObject` |

Constructor indices follow member/constructor order, independently of wire IDs
and render order. Defaults, requiredness, keys and mutability are retained.
Read-only accessors, schema/view/save-only fixtures, configuration loads,
transforming setters and intentional `Init` coverage remain unchanged.
In particular, `ZfbTest::LoadTest`, `ZfSkipTest` and `ZfCSVTest::CSVInit` retain
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
- `Zfb::IOBuilder` retains 8-byte default arena alignment, caller headroom,
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
[the initial batch](../../zfb/interop/evidence/m2-release-initial), with later
vector/growth fixes recorded in
[the focused batches](../../zfb/interop/evidence/m2-release-hotpath).
Refresh the complete matrix against the final rebuilt state and resolve the
remaining regressions. Reuse the existing interop fixtures and pooled baseline;
keep schema construction and field association outside timed operations.

### Inline/table vector comparison

`./zfb/interop/zfbiobench 100000 1024 11 vectors` uses the existing pooled
fresh/reuse timing driver. The payload length determines the number of inline
points and child tables (one of each per `sizeof(Point)` bytes). Both writers
are independently verified and every field/element is compared before timing.
Canonical inline structs are constructed directly in the downward buffer;
child offsets retain the original `Zfb::Save::vectorIter` `ZmScratch` policy.
Native offsets use `ZtScratch`; both use stack storage with heap fallback.
The mode is built and has release measurements, including the later
[65536-byte vector comparison](../../zfb/interop/evidence/m2-release-hotpath/vector-padding-snapshot.txt).
These results do not close the complete performance matrix or the remaining
reflected-store-write and replication acceptance gaps.

### Reflected writer comparison

`write` and `widewrite` select the Row and Wide schema-driven writer kernels,
for example `./zfb/interop/zfbiobench 100000 1024 11 widewrite`. Native code
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
[scope and raw evidence](../../zfb/interop/evidence/m2-release-hotpath/allocation-review.txt).
This does not close the full allocation/copy or latency acceptance gates.

The subsequent [growth-copy trace](../../zfb/interop/evidence/m2-release-hotpath/copy-review.txt)
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
ZFB_BENCH_POOL=4 taskset -c 6 /tmp/zfb-release-bench 1000000 64 11
ZFB_BENCH_POOL=4 taskset -c 6 /tmp/zfb-release-bench 1000000 64 11 wide
```

The optional setting affects both codecs and no production defaults. Four
blocks cover the preflight, active and next fresh owners. Separate traces
confirm zero empty-pool requests with this setting, compared with every
request using fallback without it. See [raw results and scope](../../zfb/interop/evidence/m2-release-hotpath/pool-review.txt).
Configured-pool fresh medians (canonical/native) are 67.454/63.051 ns for
narrow rows and 108.984/109.959 ns for wide rows. Default fallback remains
101.049/105.717 ns and 152.890/165.013 ns respectively. Retain both regimes;
these round-mean medians do not establish message-tail latency acceptance.

### Direct transmit versus generic staging

`./zfb/interop/zfbiobench 100000 64 11 tx` compares the same native codec
through direct TxSink buffer construction and through a generic span destination
which inserts the completed scratch output into an otherwise identical TxSink.
Rounds alternate order. Preflight requires identical bytes, both canonical and
native verification, and the expected completed-span byte count. Timings include
fresh pooled-owner allocation, encoding, flush and synchronous sink consumption;
there is no network or scheduler. The timed sink exposes each complete emitted
region without copying it to an observer buffer. CSV reports emitted bytes,
pooled-buffer allocation requests, sends and completed-span insertion bytes.
Backing growth/relocation is accounted separately by ZfbIOTest; these pool
request counts do not include backing reallocations or arbitrary allocations.
Use small, near-capacity and oversized payloads to distinguish direct handoff,
scratch growth/compaction and splitting. Staged is a mechanism control using the
current native codec, not the matched HEAD baseline. The mode has been compiled and measured in the current release configuration.
The [recorded comparison](../../zdb/interop/evidence/repair2-20261010/tx69/README.md)
covers payload64/928/944/4096 with11 alternating rounds. Direct output below the
aligned emission limit avoids completed-span insertion and improves median round
time about5–6%; oversized fallback adds one pooled-owner request and is slower
than staged output. This is a native mechanism control, not a HEAD comparison.

## Associative-container benchmark preparation

`zfbmapbench` prepares native hash/tree map serialization, borrowed keyed
lookup, owning load and update kernels. It builds an equivalent canonical
sorted-vector fixture and checks both directions before measuring. Canonical
timing comparisons are deferred. Run, for example:

```sh
./zfb/interop/zfbmapbench 64 1000
./zfb/interop/zfbmapbench 4096 100
```

CSV reports elapsed time, emitted bytes, retained arena capacity, remaining
arena scratch head, and the native offset-sort scratch size. Use multiple
entry counts to expose arena growth and scratch fallback. The setup and
preflight run outside timed kernels; the owning kernels include their normal
node allocation and release. Allocation attribution needs a profiler run on
the appropriate kernel before drawing performance conclusions. For a first
whole-process memory trace, run `libtool exec valgrind --tool=massif
--massif-out-file=/tmp/zfbmapbench.massif ./zfb/interop/zfbmapbench 4096 1`.


---

## Canonical source contracts before Zfb

Captured from commit `a114cf416ca8b070069b927d92ed5b289d61fefd`. These are independent source expectations
for the migration and interoperability harness, not production codegen inputs
or a historical compatibility requirement. Declaration order determines IDs;
a union inserts its discriminator immediately before its value slot. Canonical
`flatc` 25.12.19 is the installed reference (dependency floor: 24).

Orphaned `zdash/src/fbs` schemas are intentionally excluded.

### `zdb/example/fbs/zdbsaga_account.fbs`

```fbs
namespace zdbsaga.fbs;

table Account {
  id:uint64;
  balance:int64;
}

root_type Account;
```

### `zdb/example/fbs/zdbsaga_intent.fbs`

```fbs
namespace zdbsaga.fbs;

table BalanceTransfer {
  transfer_i_d:uint64;
  from_i_d:uint64;
  to_i_d:uint64;
  amount:int64;
  transfer_shard:uint8;
  from_shard:uint8;
  to_shard:uint8;
}

root_type BalanceTransfer;
```

### `zdb/example/fbs/zdbsaga_transfer.fbs`

```fbs
namespace zdbsaga.fbs;

enum TransferStatus:int8 {
  Pending = 0,
  Complete
}

table Transfer {
  id:uint64;
  from_i_d:uint64;
  to_i_d:uint64;
  amount:int64;
  status:TransferStatus;
}

root_type Transfer;
```

### `zdb/src/fbs/zdb_.fbs`

```fbs
include "zfb_types.fbs";
namespace Zdb_.fbs;
table TableState {
  table:string;
  un:uint64;
  shard:uint8;
}
table DBState {
  sn:Zfb.UInt128;
  table_states:[TableState];
}
table Heartbeat {
  host:string;
  state:uint32;
  n_shards:uint32;
  db_state:DBState;
}
table Record {
  table:string;		// table ID
  un:uint64;		// update number - secondary key
  sn:Zfb.UInt128;	// database sequence number
  vn:int64;		// record version number - -ve if deleted
  shard:uint8;		// shard
  data:[ubyte];		// nested row data - contains keys if row is deleted
}
table Commit {
  table:string;
  un:uint64;
  shard:uint8;
}
union Body {
  Heartbeat:Heartbeat,
  Replication:Record,
  Recovery:Record,
  Commit:Commit
}
table Msg {
  body:Body;
}
root_type Msg;
```

### `zdb/src/fbs/zdb_saga.fbs`

```fbs
include "zfb_types.fbs";

namespace Zdb_.fbs;

table SagaData {
  type:string;
  id:Zfb.UInt128;
  shard:uint8;
  data:[ubyte];
  deadline:Zfb.Time;
}

root_type SagaData;
```

### `zdb/src/fbs/zdb_saga_step.fbs`

```fbs
include "zfb_types.fbs";

namespace Zdb_.fbs;

table SagaStep {
  type:string;
  id:Zfb.UInt128;
  step:uint32;
  shard:uint8;
  // Intended target UN; UINT64_MAX records an already-absent Delete.
  un:uint64;
}

root_type SagaStep;
```

### `zdb/src/fbs/zdb_saga_type.fbs`

```fbs
namespace Zdb_.fbs;

enum SagaOp:int8 {
  Invalid = 0,
  Insert,
  Update,
  Delete
}

table SagaTypeStep {
  type:string;
  step:uint32;
  table:string;
  op:SagaOp;
  repeat:bool;
}

root_type SagaTypeStep;
```

### `zdb/src/fbs/ztc_db.fbs`

```fbs
include "zfb_types.fbs";

namespace Ztc.fbs;

enum DBCacheMode:int8 {
  Normal = 0,
  All
}

enum DBHostState:int8 {
  Instantiated = 0,
  Initialized,
  Electing,
  Active,
  Inactive,
  Stopping
}

table DBTableTelemetry {
  db_i_d:string;
  id:string;
  count:uint64;
  cache_loads:uint64;
  cache_misses:uint64;
  cache_evictions:uint64;
  cache_size:uint32;
  cache_mode:DBCacheMode;
  rag:int8;
}

table DBHostTelemetry {
  ip:Zfb.IP;
  db_i_d:string;
  id:string;
  priority:uint32;
  port:uint16;
  state:DBHostState;
  voted:uint8;
  rag:int8;
}

table DBTelemetry {
  thread:string;
  threads:[string];
  n_shards:uint32;
  self:string;
  leader:string;
  prev:string;
  next:string;
  n_cxns:uint32;
  heartbeat_freq:uint32;
  heartbeat_timeout:uint32;
  reconnect_freq:uint32;
  election_timeout:uint32;
  n_tables:uint16;
  n_hosts:uint8;
  n_peers:uint8;
  state:DBHostState;
  active:uint8;
  recovering:uint8;
  replicating:uint8;
  rag:int8;
}
```

### `zdb/test/fbs/zdbtest.fbs`

```fbs
include "zfb_types.fbs";
namespace zdbtest.fbs;
enum Side:int8 {
  Buy = 0,
  Sell
}
table Order {
  symbol:string;
  order_i_d:uint64;
  link:string;
  cl_ord_i_d:string;
  seq_no:uint64;
  side:Side;
  prices:[int32];
  qtys:[int32];
  flags:Zfb.Bitmap;
}
table SagaA {
  value:uint64;
}
table SagaB {
  value:uint32;
}
table RepeatSaga {
  first:uint64;
  second:uint64;
}
table BatchSaga {
  ids:[uint64];
  fail_at:uint32 = 4294967295;
}
table LiveSaga {
  order_i_d:uint64;
}
table ShortSaga {
  order_i_d:uint64;
}
table ChangedSaga {
  order_i_d:uint64;
}
table PayloadSaga {
  data:[uint8];
}
root_type Order;
```

### `zdb_sqlite/itest/fbs/zdbsltypes.fbs`

```fbs
include "zfb_types.fbs";
namespace zdbsltest.fbs;

table AllTypes {
  id:ulong;
  string_value:string;
  bytes_value:[ubyte];
  bool_value:bool;
  int8_value:byte;
  uint8_value:ubyte;
  int16_value:short;
  uint16_value:ushort;
  int32_value:int;
  uint32_value:uint;
  int64_value:long;
  uint64_value:ulong;
  int128_value:Zfb.Int128;
  uint128_value:Zfb.UInt128;
  float_value:double;
  fixed_value:Zfb.Fixed;
  decimal_value:Zfb.Decimal;
  time_value:Zfb.Time;
  date_time_value:Zfb.DateTime;
  bitmap_value:Zfb.Bitmap;
  ip_value:Zfb.IP;
  string_vec:[string];
  bytes_vec:[Zfb.Bytes];
  int8_vec:[byte];
  uint8_vec:[ubyte];
  int16_vec:[short];
  uint16_vec:[ushort];
  int32_vec:[int];
  uint32_vec:[uint];
  int64_vec:[long];
  uint64_vec:[ulong];
  int128_vec:[Zfb.Int128];
  uint128_vec:[Zfb.UInt128];
  float_vec:[double];
  fixed_vec:[Zfb.Fixed];
  decimal_vec:[Zfb.Decimal];
  time_vec:[Zfb.Time];
  date_time_vec:[Zfb.DateTime];
}

root_type AllTypes;
```

### `zdf/src/fbs/zdf_blk_data.fbs`

```fbs
include "zfb_types.fbs";
namespace Zdf.DB.fbs;
table BlkData {
  blk_offset:uint64;	// block index within series (from 0)
  series_i_d:uint32;	// series ID
  buf:[uint8];		// 4K of compressed data
}
root_type BlkData;
```

### `zdf/src/fbs/zdf_blk_fixed.fbs`

```fbs
include "zfb_types.fbs";
namespace Zdf.DB.fbs;
table BlkFixed {
  blk_offset:uint64;	// block index within series (from 0)
  offset:uint64;	// offset of this block (in values)
  last:int64;		// last value in block
  series_i_d:uint32;	// series ID
  count:uint16;		// count of values in block
  ndp:uint8;		// NDP for this block
}
root_type BlkFixed;
```

### `zdf/src/fbs/zdf_blk_float.fbs`

```fbs
include "zfb_types.fbs";
namespace Zdf.DB.fbs;
table BlkFloat {
  blk_offset:uint64;	// block index within series (from 0)
  offset:uint64;	// offset of this block (in values)
  last:double;		// last value in block
  series_i_d:uint32;	// series ID
  count:uint16;		// count of values in block
}
root_type BlkFloat;
```

### `zdf/src/fbs/zdf_series_fixed.fbs`

```fbs
include "zfb_types.fbs";
namespace Zdf.DB.fbs;
table SeriesFixed {
  id:uint32;		// series ID (1-based, globally unique)
  name:string;		// field name within DF
  first:int64;		// first value in first block
  ndp:uint8;		// NDP of first value
  epoch:Zfb.DateTime;	// epoch (creation time)
  blk_offset:uint64;	// first block (initially 0)
}
root_type SeriesFixed;
```

### `zdf/src/fbs/zdf_series_float.fbs`

```fbs
include "zfb_types.fbs";
namespace Zdf.DB.fbs;
table SeriesFloat {
  id:uint32;		// series ID (1-based, globally unique)
  name:string;		// field name within DF
  first:double;		// first value in first block
  epoch:Zfb.DateTime;	// epoch (creation time)
  blk_offset:uint64;	// first block (initially 0)
}
root_type SeriesFloat;
```

### `zfb/src/fbs/zfb_types.fbs`

```fbs
namespace Zfb;
table Bytes { // ZuBytes within a BytesVec
  data:[ubyte];
}
table Bitmap { // ZuBitmap / ZmBitmap / ZtBitmap
  data:[uint64];
}
struct UInt128 { // uint128_t
  h:uint64;
  l:uint64;
}
struct Int128 { // int128_t
  h:uint64;
  l:uint64;
}
struct Fixed { // ZuFixed
  mantissa:int64;
  ndp:uint8;
}
struct Decimal { // ZuDecimal
  h:uint64;
  l:uint64;
}
struct Time { // ZmTime
  sec:int64;
  nsec:int32;
}
struct DateTime { // ZtDate
  julian:int32;
  sec:int32;
  nsec:int32;
}
struct IPv4 { // ZiIP V4
  addr:[uint8:4];
}
struct IPv6 { // ZiIP V6
  addr:[uint8:16];
}
union IP {
  IPv4,
  IPv6
}
```

### `zfb/test/fbs/zfbtest.fbs`

```fbs
namespace zfbtest.fbs;

table PlainElem {
  bah:int32;
}
table Elem {
  bah:int32;
}
table Test {
  foo:int32;
  bar:string;
  baz:[string];
  elems:[Elem];
}
root_type Test;
```

### `zfb/test/fbs/zfbtest2.fbs`

```fbs
include "zfb_types.fbs";
namespace zfbtest2.fbs;
table Object {
  id:string;
  price:int32;
  flags:Zfb.Bitmap;
}
table Test {
  foo:int32;
  bar:string;
  baz:Object;
}
root_type Test;
```

### `zfb/test/fbs/zfbtest3.fbs`

```fbs
include "zfb_types.fbs";
namespace zfbtest3.fbs;
enum Side:int8 {
  Buy = 0,
  Sell
}
table Order {
  symbol:string;
  order_i_d:uint64;
  link:string;
  cl_ord_i_d:string;
  seq_no:uint64;
  side:Side;
  price:int32;
  quantity:int32;
  bitmap:Zfb.Bitmap;
  id:string;
  ip:Zfb.IP;
}
root_type Order;
```

### `ztc/src/fbs/ztc_msg.fbs`

```fbs
include "ztc_telemetry.fbs";
include "ztc_request.fbs";

namespace Ztc.fbs;

union Body {
  Request,
  Ack,
  Telemetry,
  EOS,
  Error
}

table Msg {
  body:Body;
  sub_id:uint64;
  device_id:string;
  agent_gen:uint64;
}

root_type Msg;
```

### `ztc/src/fbs/ztc_request.fbs`

```fbs
include "ztc_types.fbs";

namespace Ztc.fbs;

table Request {
  seq_no:uint64 = 18446744073709551615;
  group:Group;
  filter:string;
  interval:uint32;
  subscribe:bool;
  alert_date:uint32;
  alert_seq_no:uint64;
  id:string;
}

table Ack {
  id:string;
  seq_no:uint64;
  status:AckStatus;
  interval:uint32;
}

table EOS {
  id:string;
  seq_no:uint64;
}

table Error {
  id:string;
  seq_no:uint64 = 18446744073709551615;
  code:int32;
  message:string;
}
```

### `ztc/src/fbs/ztc_telemetry.fbs`

```fbs
include "zfb_types.fbs";
include "ztc_types.fbs";
include "ztc_db.fbs";

namespace Ztc.fbs;

table AppTelemetry {
  version:string;
  role:string;
  start_time:Zfb.DateTime;
  state:int8;
  degraded:bool;
  rag:RAG;
  ztcver:uint32;
}

table Shutdown {
}

table AlertTelemetry {
  message:string;
  time:Zfb.Time;
  seq_no:uint64;
  tid:uint64;
  date:uint32;
  severity:int8;
}

table HeapTelemetry {
  id:string;
  partition:uint16;
  vshift:uint8;
  size:uint32;
  alignment:uint16;
  sharded:bool;
  cache_size:uint64;
  cpuset:Zfb.Bitmap;
  cache_allocs:uint64;
  cache_frees:uint64;
  cross_frees:uint64;
  heap_allocs:uint64;
  heap_frees:uint64;
  global_heap_allocs:uint64;
  global_heap_frees:uint64;
  global_heap_max:uint64;
  allocated:uint64;
  rag:RAG;
}

table HashTelemetry {
  id:string;
  addr:uint64;
  load_factor:double;
  eff_load_factor:double;
  count:uint64;
  max_count:uint64;
  node_size:uint32;
  resized:uint32;
  bits:uint8;
  c_bits:uint8;
  linear:uint8;
  shadow:uint8;
  rag:RAG;
}

table ThreadTelemetry {
  name:string;
  tid:uint64;
  stack_size:uint64;
  cpuset:Zfb.Bitmap;
  cpu_usage:double;
  alloc_stack:uint64;
  alloc_heap:uint64;
  sys_priority:int32;
  sid:uint16;
  partition:uint16;
  priority:ThreadPriority;
  main:bool;
  detached:bool;
  rag:RAG;
}

table CxnTelemetry {
  mx_i_d:string;
  socket:uint64;
  rx_calls:uint64;
  rx_bytes:uint64;
  tx_calls:uint64;
  tx_bytes:uint64;
  rx_buf_size:uint32;
  rx_buf_len:uint32;
  tx_buf_size:uint32;
  tx_buf_len:uint32;
  mreq_addr:Zfb.IP;
  mreq_if:Zfb.IP;
  mreq_if_index:uint32;
  mif:Zfb.IP;
  mif_index:uint32;
  ttl:uint32;
  local_i_p:Zfb.IP;
  remote_i_p:Zfb.IP;
  local_port:uint16;
  remote_port:uint16;
  flags:uint8;
  type:CxnType;
  rag:RAG;
}

table MxTelemetry {
  id:string;
  stack_size:uint32;
  queue_size:uint32;
  spin:uint32;
  timeout:uint32;
  rx_buf_size:uint32;
  tx_buf_size:uint32;
  rx_thread:uint16;
  tx_thread:uint16;
  partition:uint16;
  state:EngineState;
  ll:uint8;
  priority:uint8;
  n_threads:uint8;
  rag:RAG;
}

table QueueTelemetry {
  owner_i_d:string;
  id:string;
  in_bytes:uint64;
  out_bytes:uint64;
  in_count:uint64;
  out_count:uint64;
  count:uint64;
  size:uint32;
  full:uint32;
  type:QueueType;
  rag:RAG;
}

table HubTelemetry {
  id:string;
  mx_i_d:string;
  down:uint16;
  disabled:uint16;
  transient:uint16;
  up:uint16;
  reconn:uint16;
  failed:uint16;
  n_links:uint16;
  rx_thread:uint8;
  tx_thread:uint8;
  link_type:LinkType;
  state:EngineState;
  rag:RAG;
}

table LinkTelemetry {
  hub_i_d:string;
  id:string;
  rx_calls:uint64;
  tx_calls:uint64;
  rx_bytes:uint64;
  tx_bytes:uint64;
  reconnects:uint32;
  type:LinkType;
  state:LinkState;
  rag:RAG;
}

table PoolTelemetry {
  hub_i_d:string;
  id:string;
  rx_calls:uint64;
  rx_bytes:uint64;
  tx_calls:uint64;
  tx_bytes:uint64;
  idle:uint16;
  busy:uint16;
  down:uint16;
  type:LinkType;
  state:PoolState;
  rag:RAG;
}

union TelemetryBody {
  HeapTelemetry,
  HashTelemetry,
  ThreadTelemetry,
  CxnTelemetry,
  MxTelemetry,
  QueueTelemetry,
  HubTelemetry,
  LinkTelemetry,
  PoolTelemetry,
  DBTelemetry,
  DBHostTelemetry,
  DBTableTelemetry,
  AppTelemetry,
  Shutdown,
  AlertTelemetry
}

table Telemetry {
  id:string;
  seq_no:uint64;
  value:TelemetryBody;
}
```

### `ztc/src/fbs/ztc_types.fbs`

```fbs
namespace Ztc.fbs;

enum RAG:int8 {
  Off = 0,
  Red,
  Amber,
  Green
}

enum Group:uint8 {
  Heap = 0,
  Hash,
  Thread,
  Mx,
  Queue,
  Hub,
  DB,
  App,
  Alert
}

enum AckStatus:int8 {
  OK = 0,
  Invalid,
  Unsupported,
  Failed
}

enum ThreadPriority:int8 {
  Unset = -1,
  RealTime,
  High,
  Normal,
  Low
}

enum EngineState:int8 {
  Stopped = 0,
  Starting,
  Running,
  Stopping,
  StartPending,
  StopPending
}

enum CxnType:int8 {
  TCPIn = 0,
  TCPOut,
  UDP
}

enum QueueType:int8 {
  Thread = 0,
  IPC,
  Rx,
  Tx
}

enum LinkType:int8 {
  TCP = 0,
  TLS,
  QUIC,
  H1,
  H3,
  WS,
  FIX
}

enum LinkState:int8 {
  Down = 0,
  Disabled,
  Deleted,
  Connecting,
  Up,
  ReconnectPending,
  Reconnecting,
  Failed,
  Disconnecting,
  ConnectPending,
  DisconnectPending
}

enum PoolState:int8 {
  Down = 0,
  Up,
  Failed
}
```

### `zum/src/fbs/zum_action.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Action {
  app_i_d:uint64;
  id:uint32;
  name:string;
  label:string;
  state:int8;
  origin:int8;
  catalog_revision:uint64;
  tombstone:bool;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type Action;
```

### `zum/src/fbs/zum_admin_access.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table AdminAccess {
  actor_kind:int8;
  actor_i_d:string;
  app_i_d:uint64;
  operation_i_ds:[uint32];
  role_i_ds:[uint64];
  state:int8;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type AdminAccess;
```

### `zum/src/fbs/zum_app.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table App {
  id:uint64;
  name:string;
  label:string;
  state:int8;
  next_action_i_d:uint32;
  auth_version:uint64 = 1;
  catalog_revision:uint64;
  catalog_digest:[ubyte];
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
  audience:string;
}
root_type App;
```

### `zum/src/fbs/zum_assignment.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Assignment {
  app_i_d:uint64;
  user_i_d:uint64;
  role_i_ds:[uint64];
  state:int8;
  auth_version:uint64 = 1;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type Assignment;
```

### `zum/src/fbs/zum_auth_policy.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table AuthPolicy {
  app_i_d:uint64;
  provider_i_d:uint64;
  local_first:bool = true;
  eligibility_mode:int8;
  eligibility_claim:string;
  eligibility_values:[string];
  assignment_max_age:uint32;
  session_idle:uint32;
  session_absolute:uint32;
  token_lifetime:uint32;
  consent_policy:int8;
  state:int8;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type AuthPolicy;
```

### `zum/src/fbs/zum_client.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Client {
  id:string;
  app_i_d:uint64;
  label:string;
  secret_digest:[ubyte];
  secret_version:uint64 = 1;
  redirects:[string];
  created:int64;
  updated:int64;
  profile:int8;
  grants:uint8;
  refresh_allowed:bool;
  identity_scopes:[string];
  state:int8;
  version:uint64 = 1;
  owner:Zfb.UInt128;
  previous_secret_digest:[ubyte];
  previous_secret_expires:int64;
}
root_type Client;
```

### `zum/src/fbs/zum_client_access.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table ClientAccess {
  client_i_d:string;
  app_i_d:uint64;
  role_i_ds:[uint64];
  state:int8;
  auth_version:uint64 = 1;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type ClientAccess;
```

### `zum/src/fbs/zum_consent.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Consent {
  user_i_d:uint64;
  client_i_d:string;
  app_i_d:uint64;
  role_i_ds:[uint64];
  state:int8;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type Consent;
```

### `zum/src/fbs/zum_cred.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Cred {
  id:[ubyte];
  user_i_d:uint64;
  public_key:[ubyte];
  sign_count:uint32;
  created:int64;
  updated:int64;
  state:int8;
  backup_eligible:bool;
  backed_up:bool;
  label:string;
  owner:Zfb.UInt128;
  user_version:uint64 = 1;
  version:uint64 = 1;
}
root_type Cred;
```

### `zum/src/fbs/zum_evidence.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Evidence {
  app_i_d:uint64;
  user_i_d:uint64;
  provider_i_d:uint64;
  role_values:[string];
  eligible:bool;
  observed:int64;
  deadline:int64;
  source:int8;
  policy_version:uint64;
  protected_refresh_token:[ubyte];
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type Evidence;
```

### `zum/src/fbs/zum_ext_identity.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table ExtIdentity {
  provider_i_d:uint64;
  issuer:string;
  subject:string;
  user_i_d:uint64;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type ExtIdentity;
```

### `zum/src/fbs/zum_grant.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Grant {
  id:[ubyte];
  issuer:string;
  app_i_d:uint64;
  user_i_d:uint64;
  client_i_d:string;
  credential_i_d:[ubyte];
  audience:string;
  redirect_u_r_i:string;
  actions:Zfb.Bitmap;
  digest:[ubyte];
  challenge:[ubyte];
  binding_digest:[ubyte];
  pkce_challenge:[ubyte];
  oauth_state:string;
  auth_version:uint64;
  auth_time:int64;
  created:int64;
  expires:int64;
  kind:int8;
  purpose:int8;
  state:int8;
  owner:Zfb.UInt128;
  user_name:string;
  user_handle:[ubyte];
  requested_role_i_ds:[uint64];
  role_i_ds:[uint64];
  label:string;
  user_version:uint64 = 1;
  client_version:uint64;
  assignment_version:uint64;
  policy_version:uint64;
  evidence_version:uint64;
  authority_source:int8;
  actor:string;
  oauth_state_present:bool;
  scope:string;
  nonce:string;
  authority_provider_i_d:uint64;
  prompt:string;
  max_age:uint64;
  prompt_present:bool;
  max_age_present:bool;
}
root_type Grant;
```

### `zum/src/fbs/zum_issuer.fbs`

```fbs
namespace Zum.fbs;
table Issuer {
  id:string;
  schema_version:uint32;
  core_app_i_d:uint64;
  bootstrap_phase:int8;
  initial_user_i_d:uint64;
  initial_client_i_d:string;
  key_check:[ubyte];
  pending_key_check:[ubyte];
}
root_type Issuer;
```

### `zum/src/fbs/zum_maintenance.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;

table GrantDelete {
  id:[ubyte];
  digest:[ubyte];
  expires:int64;
  state:int8;
  owner:Zfb.UInt128;
}
table SessionDelete {
  digest:[ubyte];
  idle_deadline:int64;
  absolute_deadline:int64;
  version:uint64;
  owner:Zfb.UInt128;
}
table RefreshDelete {
  id:[ubyte];
  expires:int64;
  state:int8;
  version:uint64;
  owner:Zfb.UInt128;
}
table EvidenceDelete {
  app_i_d:uint64;
  user_i_d:uint64;
  provider_i_d:uint64;
  deadline:int64;
  version:uint64;
  updated:int64;
  owner:Zfb.UInt128;
}
table ConsentDelete {
  user_i_d:uint64;
  client_i_d:string;
  app_i_d:uint64;
  state:int8;
  version:uint64;
  updated:int64;
  owner:Zfb.UInt128;
}
```

### `zum/src/fbs/zum_provider.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Provider {
  id:uint64;
  name:string;
  issuer:string;
  client_i_d:string;
  client_secret:[ubyte];
  scopes:[string];
  role_claim:string;
  claim_source:int8;
  state:int8;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type Provider;
```

### `zum/src/fbs/zum_refresh.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Refresh {
  id:[ubyte];
  issuer:string;
  app_i_d:uint64;
  user_i_d:uint64;
  client_i_d:string;
  credential_i_d:[ubyte];
  audience:string;
  actions:Zfb.Bitmap;
  digest:[ubyte];
  spent:[Zfb.Bytes];
  auth_version:uint64;
  auth_time:int64;
  created:int64;
  expires:int64;
  generation:uint32;
  state:int8;
  owner:Zfb.UInt128;
  requested_role_i_ds:[uint64];
  role_i_ds:[uint64];
  user_version:uint64 = 1;
  client_version:uint64;
  assignment_version:uint64;
  policy_version:uint64;
  evidence_version:uint64;
  authority_source:int8;
  scope:string;
  nonce:string;
  authority_provider_i_d:uint64;
  version:uint64 = 1;
  updated:int64;
}
root_type Refresh;
```

### `zum/src/fbs/zum_request.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table IdemRequest {
  actor_kind:int8;
  actor_i_d:string;
  operation:uint32;
  idempotence:string;
  request_digest:[ubyte];
  saga_i_d:Zfb.UInt128;
  status:int8;
  result_i_ds:[string];
  expires:int64;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type IdemRequest;
```

### `zum/src/fbs/zum_role.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Role {
  app_i_d:uint64;
  id:uint64;
  name:string;
  label:string;
  actions:Zfb.Bitmap;
  state:int8;
  origin:int8;
  catalog_revision:uint64;
  tombstone:bool;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type Role;
```

### `zum/src/fbs/zum_role_map.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table RoleMap {
  app_i_d:uint64;
  provider_i_d:uint64;
  value:string;
  role_i_d:uint64;
  state:int8;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type RoleMap;
```

### `zum/src/fbs/zum_saga.fbs`

```fbs
include "zfb_types.fbs";
include "zum_issuer.fbs";
include "zum_app.fbs";
include "zum_role.fbs";
include "zum_action.fbs";
include "zum_user.fbs";
include "zum_cred.fbs";
include "zum_provider.fbs";
include "zum_client.fbs";
include "zum_sign_key.fbs";
include "zum_client_access.fbs";
include "zum_admin_access.fbs";
include "zum_role_map.fbs";
include "zum_auth_policy.fbs";
include "zum_grant.fbs";
include "zum_refresh.fbs";
include "zum_request.fbs";
include "zum_consent.fbs";
include "zum_ssf_delivery.fbs";
include "zum_maintenance.fbs";

namespace Zum.fbs;

table ConsentCode {
  before_grant:Grant;
  after_grant:Grant;
  before_consent:Consent;
  role_i_ds:[uint64];
  now:long;
}

table SessionCleanup {
  sessions:[SessionDelete];
  updated:int64;
}

table RefreshCleanup {
  refreshes:[RefreshDelete];
  updated:int64;
}

table AppCleanup {
  app_i_d:uint64;
  evidence:[EvidenceDelete];
  consents:[ConsentDelete];
  updated:int64;
}

table Enrollment {
  ceremony_i_d:[ubyte];
  user_i_d:uint64;
  name:string;
  handle:[ubyte];
  credential_i_d:[ubyte];
  public_key:[ubyte];
  sign_count:uint32;
  created:int64;
  backup_eligible:bool;
  backed_up:bool;
  label:string;
  precreated:bool;
  before_grant:Grant;
  before_user:User;
}

table CredentialAdd {
  ceremony_i_d:[ubyte];
  issuer:string;
  user_i_d:uint64;
  user_handle:[ubyte];
  credential_i_d:[ubyte];
  public_key:[ubyte];
  sign_count:uint32;
  created:int64;
  backup_eligible:bool;
  backed_up:bool;
  label:string;
  user_version:uint64 = 1;
  before_grant:Grant;
}

table RecoveryStart {
  capability_i_d:[ubyte];
  digest:[ubyte];
  issuer:string;
  user_i_d:uint64;
  user_version:uint64;
  created:int64;
  expires:int64;
  actor:string;
  version:uint64;
  old_state:int8;
  old_updated:int64;
  request:IdemRequest;
}

table RecoveryEnroll {
  ceremony_i_d:[ubyte];
  issuer:string;
  actor:string;
  user_i_d:uint64;
  user_version:uint64;
  old_handle:[ubyte];
  new_handle:[ubyte];
  credential_i_d:[ubyte];
  public_key:[ubyte];
  sign_count:uint32;
  created:int64;
  backup_eligible:bool;
  backed_up:bool;
  label:string;
  before_grant:Grant;
  before_user:User;
}

table CodeFamily {
  code_i_d:[ubyte];
  code_digest:[ubyte];
  family_i_d:[ubyte];
  issuer:string;
  app_i_d:uint64;
  user_i_d:uint64;
  client_i_d:string;
  credential_i_d:[ubyte];
  audience:string;
  requested_role_i_ds:[uint64];
  role_i_ds:[uint64];
  actions:Zfb.Bitmap;
  digest:[ubyte];
  auth_version:uint64;
  user_version:uint64 = 1;
  client_version:uint64;
  assignment_version:uint64;
  auth_time:int64;
  created:int64;
  expires:int64;
  scope:string;
  nonce:string;
  authority_provider_i_d:uint64;
  policy_version:uint64;
  evidence_version:uint64;
  authority_source:int8;
  before_grant:Grant;
}

table ExternalProjection {
  provider_i_d:uint64;
  issuer:string;
  subject:string;
  user_i_d:uint64;
  name:string;
  handle:[ubyte];
  created:int64;
}

table AppEnrollment {
  core_app_i_d:uint64;
  app_i_d:uint64;
  app_name:string;
  app_label:string;
  audience:string;
  sign_key:SignKey;
  client_i_d:string;
  secret_digest:[ubyte];
  redirects:[string];
  client_type:int8;
  catalog_client:bool;
  created:int64;
  catalog_publish_op:uint32;
  operation_query_op:uint32;
  request:IdemRequest;
}

table AppActionAdd {
  app_i_d:uint64;
  action_i_d:uint32;
  name:string;
  label:string;
  created:int64;
  old_app_version:uint64;
  old_auth_version:uint64;
  old_updated:int64;
  request:IdemRequest;
}

table AssignmentChange {
  app_i_d:uint64;
  user_i_d:uint64;
  old_roles:[uint64];
  new_roles:[uint64];
  old_state:int8;
  new_state:int8;
  version:uint64;
  auth_version:uint64;
  old_updated:int64;
  updated:int64;
  app_version:uint64;
  app_auth_version:uint64;
  app_updated:int64;
  request:IdemRequest;
  assign_roles:bool;
  if_match:string;
}

// Immutable before-images, encoded with each record's ZfbStruct schema.
// Each entry is one independently journaled and compensated row effect.
table RoleDelete {
  app:App;
  role:Role;
  members:[Zfb.Bytes];
  clients:[Zfb.Bytes];
  admins:[Zfb.Bytes];
  maps:[Zfb.Bytes];
  updated:int64;
  request:IdemRequest;
}

table CatalogEdit {
  before:[ubyte];
  after:[ubyte];
}

table CatalogRows {
  added:[Zfb.Bytes];
  changed:[Zfb.Bytes];
}

table CatalogPublish {
  before:App;
  after:App;
  actions:CatalogRows;
  roles:CatalogRows;
  clients:CatalogRows;
  client_access:CatalogRows;
  request:IdemRequest;
}

table AssignmentAdd {
  app_i_d:uint64;
  user_i_d:uint64;
  created:int64;
  request:IdemRequest;
}

table RoleEdit {
  app:App;
  before:Role;
  action_i_ds:[uint32];
  if_match:string;
  updated:int64;
  request:IdemRequest;
  kind:int8;
  label:string;
  state:int8;
}

table ActionEdit {
  app:App;
  before:Action;
  if_match:string;
  updated:int64;
  request:IdemRequest;
  state:int8;
}

table AppChange {
  before:App;
  if_match:string;
  label:string;
  updated:int64;
  request:IdemRequest;
  state_only:bool;
  state:int8;
}

table UserEdit {
  before:User;
  if_match:string;
  profile:string;
  email:string;
  fields:uint8;
  updated:int64;
  request:IdemRequest;
  state_only:bool;
  state:int8;
}

table CredEdit {
  before:Cred;
  if_match:string;
  label:string;
  updated:int64;
  request:IdemRequest;
  state_only:bool;
  state:int8;
}


table ProviderEdit {
  before:Provider;
  if_match:string;
  values:Provider;
  updated:int64;
  request:IdemRequest;
  state_only:bool;
  state:int8;
  fields:uint8;
}

table ClientEdit {
  before:Client;
  if_match:string;
  values:Client;
  updated:int64;
  request:IdemRequest;
  state_only:bool;
  state:int8;
  fields:uint8;
  overlap_seconds:uint32;
}

table KeyRetire {
  before:SignKey;
  if_match:string;
  retire_after:int64;
  updated:int64;
  request:IdemRequest;
}

table ClientAccessState {
  before:ClientAccess;
  if_match:string;
  updated:int64;
  request:IdemRequest;
  state:int8;
}

table AdminAccessState {
  before:AdminAccess;
  if_match:string;
  updated:int64;
  request:IdemRequest;
  state:int8;
}

table RoleMapDelete {
  app:App;
  before:RoleMap;
  if_match:string;
  updated:int64;
  request:IdemRequest;
}

table RoleMapPut {
  app:App;
  before:RoleMap;
  role_i_d:uint64;
  if_match:string;
  if_none_match:string;
  updated:int64;
  request:IdemRequest;
}

table PolicyPut {
  app:App;
  before:AuthPolicy;
  values:AuthPolicy;
  if_match:string;
  if_none_match:string;
  updated:int64;
  request:IdemRequest;
}

table ClientAccessPut {
  app:App;
  before:ClientAccess;
  values:ClientAccess;
  if_match:string;
  if_none_match:string;
  updated:int64;
  request:IdemRequest;
}

table AdminAccessPut {
  app:App;
  before:AdminAccess;
  values:AdminAccess;
  if_match:string;
  if_none_match:string;
  updated:int64;
  request:IdemRequest;
}

table ProviderAdd {
  before:Provider;
  values:Provider;
  updated:int64;
  request:IdemRequest;
}


table RoleAdd {
  app:App;
  before:Role;
  values:Role;
  updated:int64;
  request:IdemRequest;
}

table UserInvite {
  before:User;
  values:User;
  updated:int64;
  request:IdemRequest;
  grant:Grant;
  external:User;
}

table ClientAdd {
  app:App;
  before:Client;
  values:Client;
  updated:int64;
  request:IdemRequest;
}

table KeyAdd {
  before:SignKey;
  values:SignKey;
  updated:int64;
  request:IdemRequest;
}

table Revoke {
  sessions:[Zfb.Bytes];
  consents:[Zfb.Bytes];
  grants:[Zfb.Bytes];
  updated:int64;
  request:IdemRequest;
}

table GrantCleanup {
  grants:[GrantDelete];
  updated:int64;
  request:IdemRequest;
}

// Legacy decoders accept interrupted full-image maintenance sagas and fail
// them normally instead of making recovery reject their type.
table GrantCleanupV2 {
  grants:[Zfb.Bytes];
  updated:int64;
  request:IdemRequest;
}
table SessionCleanupV1 {
  sessions:[Zfb.Bytes];
  updated:int64;
}
table RefreshCleanupV1 {
  refreshes:[Zfb.Bytes];
  updated:int64;
}
table AppCleanupV1 {
  app_i_d:uint64;
  evidence:[Zfb.Bytes];
  consents:[Zfb.Bytes];
  updated:int64;
}

table KeyBinding {
  issuer:string;
  before_check:[ubyte];
  after_check:[ubyte];
  before_pending:[ubyte];
  after_pending:[ubyte];
}

table SecretRekey {
  field:uint32;
  provider_i_d:uint64;
  app_i_d:uint64;
  user_i_d:uint64;
  key_i_d:string;
  before:[ubyte];
  after:[ubyte];
}

table SSFDeliveryAdd {
  delivery:SSFDelivery;
}
```

### `zum/src/fbs/zum_session.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table Session {
  digest:[ubyte];
  user_i_d:uint64;
  provider_i_d:uint64;
  issuer:string;
  subject:string;
  auth_time:int64;
  idle_deadline:int64;
  absolute_deadline:int64;
  state:int8;
  auth_version:uint64 = 1;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type Session;
```

### `zum/src/fbs/zum_sign_key.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table SignKey {
  id:string;
  issuer:string;
  algorithm:string;
  provider_ref:string;
  public_jwk:string;
  private_material:[ubyte];
  not_before:int64;
  retire_after:int64;
  state:int8;
  version:uint64 = 1;
  created:int64;
  updated:int64;
  owner:Zfb.UInt128;
}
root_type SignKey;
```

### `zum/src/fbs/zum_ssf.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table SSFRx {
  receiver_i_d:string;
  app_i_d:uint64;
  audience:string;
  delivery_u_r_l:string;
  callback_auth:[ubyte];
  revision:uint64 = 1;
  updated:int64;
  owner:Zfb.UInt128;
  expires:int64;
  errors:uint32;
}
root_type SSFRx;
```

### `zum/src/fbs/zum_ssf_delivery.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table SSFDelivery {
	  event_i_d:string;
	  receiver_i_d:string;
	  family_issuer:string;
	  family_i_d:string;
	  family_expires:int64;
  set:[ubyte];
  next_delivery:int64;
  receiver_revision:uint64;
  owner:Zfb.UInt128;
}
root_type SSFDelivery;
```

### `zum/src/fbs/zum_user.fbs`

```fbs
include "zfb_types.fbs";
namespace Zum.fbs;
table User {
  id:uint64;
  source:int8;
  name:string;
  profile:string;
  email:string;
  handle:[ubyte];
  created:int64;
  updated:int64;
  state:int8;
  owner:Zfb.UInt128;
  auth_version:uint64 = 1;
  version:uint64 = 1;
}
root_type User;
```
