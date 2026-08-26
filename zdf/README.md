# Zdf

`Zdf` is a typed, asynchronous dataframe and time-series library built on
`Zdb`.  It is intended for append-heavy, real-time data: values are compressed
into small blocks, block metadata remains indexed in memory, block payloads are
loaded through the `Zdb` object cache, and historical readers automatically
become live subscribers when they catch up with the writer.

A dataframe is not stored as a row table.  It is a compile-time view over one
persisted series per reflected field.  Applications write objects row by row,
then read individual fields and use the common value offset to align columns.

## Features

- Typed dataframes generated from `ZfStruct` metadata.
- Standalone fixed-point, integer, floating-point, and time series.
- Append-only writers with one active writer per series.
- Asynchronous historical reads, forward and reverse repositioning, and live
  tailing without changing reader objects.
- Offset seeks and lower-bound value searches for monotonically increasing
  series.
- Byte-aligned signed integer compression with run-length encoding, plus
  optional delta and delta-of-delta transforms.
- Chimp compression for IEEE-754 `double` values.
- 4 KiB compressed data blocks containing at most 4096 values.
- A sparse in-memory block index with interpolation search.
- Asynchronous block persistence and on-demand block loading through `Zdb`.
- Cache pinning while blocks are being read or written, with an eviction hook
  that detaches evicted payloads from the in-memory index.
- Logical prefix purging for archival/retention workflows.
- Rolling count, total, mean, population variance, and population standard
  deviation; `StatsTree` adds minimum, maximum, median, rank, and duplicate-
  aware order statistics.

## Public headers

Most applications need:

```c++
#include <zlib/ZdfStore.hh>
```

`ZdfStore.hh` includes the dataframe, series, compression, schema, and basic
type APIs.  Include `ZdfStats.hh` separately for rolling statistics.

The principal types are:

- `Zdf::Store`: owns the five `Zdb` tables and maps logical shards to
  `ZiMultiplex` threads.
- `Zdf::DataFrame<O, TimeIndex>`: typed collection of the `Series` fields in
  reflected object `O`.
- `Zdf::Series<Decoder>` and `Zdf::TimeSeries`: independently openable series.
- `Zdf::Writer<Decoder>`: one append session for one series.
- `Zdf::Reader<Decoder>` and `Reader::Ctrl`: asynchronous read stream and its
  callback control interface.
- `Zdf::Stats` and `Zdf::StatsTree`: rolling statistical accumulators.

## Dataframe schema

Mark every persisted column with the `ZuFieldProp::Series` property in its
`ZfStruct` declaration.  Other reflected fields are ignored by `Zdf`.  A
usable dataframe needs at least one `Series` field.

```c++
struct Tick {
  uint64_t seqNo;
  ZuTime  time;
  double  price;
  int64_t size;
};

ZfStruct(Tick,
  (((seqNo), (Ctor<0>, Series, Index, Delta)),
    (UInt64)),
  (((time),   (Ctor<1>, Series, Index, Delta, NDP<9>)),
    (Time, "2020/01/01")),
  (((price),  (Ctor<2>, Series)),
    (Float)),
  (((size),   (Ctor<3>, Series, Delta)),
    (Int64)));

using TickDF = Zdf::DataFrame<Tick, false>;
```

For a dataframe named `ticks`, these fields are persisted as the independent
series `ticks/seqNo`, `ticks/time`, `ticks/price`, and `ticks/size`.  The
combined dataframe name, slash, and field ID must fit in `Zdf::IDSize_` bytes.
There is no persisted dataframe catalog or runtime row schema: reopening a
dataframe requires the same C++ object definition, field IDs, and codec
properties.

### Field and codec mapping

| Reflected field | Stored/read value | Codec properties |
| --- | --- | --- |
| `Int8`..`Int64`, `UInt8`..`UInt64` | signed 64-bit mantissa, exposed as `ZuFixed` | none, `Delta`, or `Delta2` |
| `Fixed` | declared-NDP mantissa, exposed as `ZuFixed` | none, `Delta`, or `Delta2` |
| `Decimal` | converted to a declared-NDP `ZuFixed` | none, `Delta`, or `Delta2` |
| `Float` | `double` | Chimp; do not add `Delta` or `Delta2` |
| `Time` | nanoseconds relative to the series epoch, exposed as `ZuFixed` | exactly `Delta`; `NDP<9>` is recommended |

`Delta` compresses the first derivative and `Delta2` the second derivative.
Do not specify both.  `Index` documents that a field is intended for ordered
lookup, but ordering is not checked at runtime; `find()` is correct only when
the appended values are monotonically non-decreasing.

Integer codecs are signed internally.  In particular, a `UInt64` series must
not contain values greater than `INT64_MAX`, and `ZuFixedNull` is reserved for
the read sentinel.  Boolean, string, byte-vector, 128-bit integer, `DateTime`,
UDT, and vector fields are not dataframe series types in the current
implementation.

Fixed-point blocks have one NDP.  Starting a new writer with a different NDP
forces a new block.  Dataframe writers normalize `Fixed` and `Decimal` fields
to the field's declared `NDP`, avoiding mixed scales within ordinary writes.

## Storage and caching

Call `Zdf::Store::dbCf()` before `Zdb::init()` so that the following tables are
present in the database configuration:

- `zdf.series_fixed`: fixed/integer/time series identity, first value, epoch,
  and logical head block.
- `zdf.series_float`: floating-point series identity, first value, and logical
  head block.
- `zdf.blk_fixed`: fixed-series block offsets, counts, scales, and last values.
- `zdf.blk_float`: floating-series block offsets, counts, and last values.
- `zdf.blk_data`: compressed 4 KiB payloads.

Series open loads block headers into a sparse skip-list index but does not load
all compressed payloads.  A reader first checks the `Zdb` object cache, falls
through to the backing store on a miss, and pins the payload until it advances
or stops.  Writers pin the active block until its asynchronous insert/update
completes.  Cache sizing, cache mode, replication, and backing-store durability
are consequently `Zdb` concerns; `Zdf` does not add a second cache or a second
durability layer.  An active partial block is saved when it fills or its writer
stops; wait for the stop callback before treating the final partial block as
durable.

`Store::dbCf()` also copies its optional `threads` setting to all five tables.
The block-data table's configured shard/thread mapping becomes the mapping used
by `Store::run()` and `Store::invoke()`.  As required by `Zdb`, shard and thread
counts must be powers of two, the thread count must not exceed the shard count,
and a table supports at most 64 shards.

## Store lifecycle

The `Zdb`, `ZiMultiplex`, and `Zdf::Store` objects must outlive every dataframe,
series, reader, writer, and pending callback.  The essential initialization
order is:

```c++
ZmRef<Zdb> db = new Zdb;
ZuPtr<ZiMultiplex> mx = new ZiMultiplex{mxParams};
ZuPtr<Zdf::Store> store = new Zdf::Store;

mx->start();

ZdbCf dbCf{cf->resolve("zdb")};
Zdf::Store::dbCf(cf, dbCf);       // inject/configure the five Zdf tables
db->init(ZuMv(dbCf), mx, handler, backingStore);
store->init(db);

db->start();                      // wait for the Zdb up callback
store->run(0, [store = store.ptr()] {
  store->open([store](bool ok) {
    if (!ok) {
      // initialization/recovery failed
      return;
    }
    // The store is ready; open dataframes or standalone series here.
  });
});
```

`Store::open()` recovers the next global series ID from the fixed and float
series tables.  Do not open dataframes or series until its callback reports
success.

For shutdown, stop dataframe writers and readers, wait for their stop
callbacks, and release their references before stopping `Zdb`.  Then stop the
database, call `store->final()` to release table references, finalize `Zdb`,
and stop the multiplexer.  `Store::close()` is declared but is not implemented
at present; do not call it.

## Shards and callbacks

Series state is thread-affined.  Methods that inspect or mutate an open series
assert that they are running on its shard.  This includes `count()`,
`blkCount()`, `length()`, `seek()`, `find()`, `write()`, and reader/writer stop
operations.

Use the dispatch helpers on the most specific object available:

```c++
df->run([df] {
  auto rows = df->count();
  // dataframe operations are safe here
});

df->invoke([df] {
  // Runs inline if already on the shard, otherwise invokes on it.
});
```

`Store` has corresponding `run(shard, ...)`, `invoke(shard, ...)`, and
`invoked(shard)` methods.  `DataFrame` and `Series` expose `run(...)`,
`invoke(...)`, and `invoked()` without a shard argument.

Open, write-acquisition, read, stop, and persistence completion callbacks are
asynchronous continuations.  Keep captured objects alive explicitly, avoid
blocking a shard callback, and perform dependent work from the completion
callback rather than assuming it has completed when the initiating method
returns.

## Opening a dataframe

The last `openDF` template argument selects find-only or create-if-missing
behavior:

```c++
// Called after Store::open() succeeded.
store->openDF<Tick, false, true>(
  0, "ticks",
  [](ZmRef<TickDF> df) {
    if (!df) {
      // At least one underlying series could not be opened or created.
      return;
    }
    // Callback runs on dataframe shard 0.
  });
```

- `Create == true` creates missing component series.
- `Create == false` returns a null dataframe if any component series is absent.
- `df->name()` returns the dataframe name and `df->shard()` its logical shard.
- `df->series<Field>()` returns the typed component series.
- `df->count()` returns the first component series' count.

The implementation assumes all component series remain offset-aligned.  It
does not validate equal counts when opening a dataframe.

## Writing rows

Acquire a dataframe writer asynchronously, append objects on the dataframe
shard, and stop it when the batch is complete:

```c++
df->write(
  [df](ZmRef<TickDF::Writer> writer) {
    Tick tick{
      .seqNo = 42,
      .time = Zm::now(),
      .price = 101.25,
      .size = 500
    };

    writer->write(tick);

    // Flush every component's active block and wait for persistence.
    df->stopWriting([] {
      // The dataframe has no active writers here.
    });
  },
  [] {
    // A component writer could not be acquired or a write failed.
  });
```

Only one writer may be active for a series.  A second acquisition calls its
error callback.  `Writer::write()` returns `false` after failure; dataframe
`Writer::write()` marks the whole dataframe writer failed if any component
write returns false.  Destroying a dataframe writer also stops its component
writers, but an explicit `stopWriting(completion)` is preferable whenever
subsequent work depends on persistence completing.

Rows are not transactional across component series.  The fields are appended
sequentially, so a process error or component failure can leave series with
different counts.  Applications that require atomic row commits must layer
their own sequence/commit protocol above `Zdf`.

## Reading and aligning columns

A read callback receives a `Reader::Ctrl` and one field value.  Return `true`
to keep draining synchronously available values.  Returning `false` only
yields from the current drain; it does not stop or destroy the reader, and the
reader requires a later resume, reposition, or stop.  Use `Ctrl::stop()` to
finish, or `Ctrl::pause()` plus a retained `Ctrl::ref()` to resume later.

```c++
using SeqField = ZfField(Tick, seqNo);

df->find<SeqField>(
  ZuFixed{1'000'000, 0},
  [df](Zdf::FieldRdrCtrl<SeqField> &ctrl, ZuFixed seqNo) {
    if (!*seqNo) {                // null: end of current history
      ctrl.stop();
      return false;
    }

    // offset() is one past the value currently being delivered.
    Zdf::Offset row = ctrl.reader.offset() - 1;

    using PriceField = ZfField(Tick, price);
    df->seek<PriceField>(
      row,
      [](Zdf::FieldRdrCtrl<PriceField> &priceCtrl, double price) {
        // price is from the same logical row as seqNo
        priceCtrl.stop();
        return false;
      });

    ctrl.stop();
    return false;
  },
  [] {
    // Block lookup/load or reader processing failed.
  });
```

`seek<Field>(offset, ...)` starts at the requested zero-based value offset.
`find<Field>(value, ...)` performs a lower-bound search and starts at the first
value greater than or equal to the target.  `find()` relies on monotonic input.
For fixed/integer fields its target and callback value are `ZuFixed`; for float
fields they are `double`.

Within a read callback, `Reader::Ctrl` provides:

- `reader`, or `ref()`, for state, offset, and ownership.
- `fn()` and `errorFn()` to replace callbacks.
- `seekFwd(offset)` and `seekRev(offset)` to reposition and continue reading.
- `findFwd(value)` and `findRev(value)` to lower-bound search from the current
  block toward the tail or from the head through the current block.
- `pause()` and `Reader::resume()` for explicit flow control.
- `stop(completion)`, which returns the next offset after the last value
  delivered and completes asynchronously.
- `purge()` to advance logical retention to the reader's block region.

`Zdf::MaxOffset` seeks into the final block and drains to the current tail.  It
is the normal starting point for a tail-only reader.

### End-of-history and live reads

After the last historical value, the reader invokes the callback once with a
null sentinel:

- fixed/integer/time series: null `ZuFixed`;
- floating-point series: `NaN`.

If the callback does not stop or otherwise change reader state, the reader then
becomes live.  Every appended value is delivered through the same callback.
The callback's Boolean return is not used to stop the end-of-history
transition, so call `ctrl.stop()` explicitly when a finite historical read is
required.

```c++
using PriceField = ZfField(Tick, price);

df->seek<PriceField>(
  Zdf::MaxOffset,
  [](Zdf::FieldRdrCtrl<PriceField> &ctrl, double price) {
    if (ZuCmp<double>::null(price)) {
      // Caught up; the reader is about to become live.
      return true;
    }
    consume(price);
    return true;
  });
```

The series retains live readers until they stop or fail.  Keep a reference
from `ctrl.ref()` if another operation must stop or resume an individual
reader.  `df->stopReading()` stops every historical and live reader in every
component series.

## Time series

`Zdf::TimeSeries` stores signed nanoseconds relative to a persisted epoch.
`TimeSeries::nsecs()` converts an absolute `ZuTime` to the stored mantissa and
`TimeSeries::time()` converts it back:

```c++
using TimeField = ZfField(Tick, time);
auto timeSeries = df->series<TimeField>();

int64_t ns = timeSeries->nsecs(when);
df->find<TimeField>(ZuFixed{ns, 9},
  [timeSeries](Zdf::FieldRdrCtrl<TimeField> &ctrl, ZuFixed value) {
    if (*value) {
      ZuTime absolute = timeSeries->time(value.mantissa);
      consume(absolute);
    }
    ctrl.stop();
    return false;
  });
```

The epoch is the `Time` field's reflected default.  A null/zero default is
replaced with `Zdf::DefltEpoch()` (`2020/01/01`).  The stored mantissa is always
nanoseconds; `NDP<9>` makes its `ZuFixed` representation read naturally as
fractional seconds.

`DataFrame<O, true>` is intended to prepend a synthetic `_time` field sampled
from `Zm::now()`.  The current synthetic field lacks the `Delta` property
required by `TimeSeries`, causing a decoder-type mismatch when its writer/read
templates are instantiated.  Until that is corrected, use
`DataFrame<O, false>` with an explicit `ZuTime` field declared as
`Series, Index, Delta, NDP<9>`.

## Standalone series

Applications that do not need reflected dataframes can open codecs directly:

```c++
using RawSeries = Zdf::Series<Zdf::Decoder>;
using DeltaSeries = Zdf::Series<Zdf::DeltaDecoder<>>;
using Delta2Series =
  Zdf::Series<Zdf::DeltaDecoder<Zdf::DeltaDecoder<>>>;
using FloatSeries = Zdf::Series<Zdf::FloatDecoder>;

store->openSeries<Zdf::DeltaDecoder<>, true>(
  shard, "orders/seqNo",
  [](ZmRef<DeltaSeries> series) {
    series->write(
      [](ZmRef<DeltaSeries::Writer> writer) {
        writer->write(100);
        writer->write(101);
        writer->stop([] {
          // Active block has been saved.
        });
      },
      [] {
        // Writer acquisition/write failure.
      },
      0);                         // NDP for all fixed codecs
  });
```

Use `openTimeSeries<Create>(shard, name, epoch, callback)` for an independently
named `TimeSeries`.  The `Create` behavior is the same as for dataframes.

Useful series inspection methods, all shard-affined, are `head()`,
`blkCount()`, `count()`, `lastBlk()`, and `length()`.  `length()` is a mild
overestimate of compressed bytes: it assumes every block except the active
last block occupies the full 4 KiB.

## Retention and purge

`Reader::Ctrl::purge()` advances the series' logical head toward the reader's
current block.  Purging is coarse-grained: the target is rounded down to an
in-memory index-block boundary of 512 compressed blocks, and the active last
block is retained.  The series record is updated with the new head and the
preceding value needed by ordered block search.

This operation makes the prefix inaccessible through the open series index;
it does **not** delete old `zdf.blk_*` or `zdf.blk_data` rows from the backing
database.  Physical archival/deletion and its coordination with replicas are
an application or backing-store maintenance responsibility.

## Rolling statistics

`Stats` supports efficient add/remove windows without retaining samples:

```c++
Zdf::Stats stats;
stats.add(value);
stats.del(expired);

auto n = stats.count();
auto total = stats.total();
auto mean = stats.mean();
auto variance = stats.var();
auto deviation = stats.std();
```

Variance and standard deviation use the population formula.  Empty `Stats`
returns zero for mean, variance, and standard deviation.

`StatsTree` additionally retains values in a duplicate-aware GNU PBDS order-
statistics tree:

```c++
Zdf::StatsTree<> stats;
stats.add(value);
stats.del(expired);

auto lo = stats.minimum();
auto hi = stats.maximum();
auto median = stats.median();
auto p95 = stats.rank(.95);       // floor(.95 * count), 0 <= rank < 1
```

Empty tree extrema/ranks return `NaN`.  For an even count, `median()` returns
the upper middle element rather than the average of the two middle elements.
Use `StatsTree<StatsTreeHeapID<"App.Window">>` to customize the Z heap ID.

## Current constraints

- Dataframe fields are independent series, not an atomic row store.
- A dataframe must contain at least one reflected `Series` field.
- Series are append-only; there is no point update, truncation, or dataframe
  deletion API.
- One writer is allowed per series; readers may be historical or live.
- Empty series have no readable block; begin reads after at least one value has
  been written.
- Value search requires monotonic non-decreasing data and is not validated.
- Unsigned integer values must fit in signed 64 bits; `ZuFixedNull` is
  reserved.
- Purge is logical and coarse-grained, not physical database reclamation.
- Schema/codec changes are not versioned or migrated automatically.
- The synthetic `TimeIndex == true` path currently has a decoder mismatch; use
  an explicit delta-encoded `ZuTime` field.
- `Store::close()` is not implemented; use orderly reader/writer shutdown,
  `Zdb` stop, and `Store::final()`.

## Source map

- `src/Zdf.hh`: reflected field selection, codec mapping, dataframe, and
  dataframe writer.
- `src/ZdfSeries.hh`: block index, series, asynchronous reader/writer state
  machines, live delivery, and purge.
- `src/ZdfCompress.hh`: integer/RLE/delta and floating-point codecs.
- `src/ZdfBlk.hh`: compact in-memory block header and payload access.
- `src/ZdfSchema.hh` and `src/fbs/`: persisted `Zdb` table records.
- `src/ZdfStore.hh` / `src/ZdfStore.cc`: table injection, lifecycle, shard
  dispatch, and series/dataframe open.
- `src/ZdfStats.hh` / `src/ZdfStatsTree.hh`: rolling and order statistics.

## Building and testing

Build the repository before running module tests so all dependent libraries
and generated FlatBuffers headers are current:

```sh
./z.config /opt/z
make -j8
make -C zdf/test test
```

The unit binaries cover codecs, statistics, store lifecycle, standalone
series, dataframe writes, offset/value searches, and the in-memory `Zdb`
backend.

`zdf/itest/zdffptest` exercises a persistent external `Zdb` backend, reopening
data, float search, rolling statistics, and live tailing.  It is skipped unless
both backend variables are set:

```sh
ZDB_MODULE=/path/to/libZdbPQ.so \
ZDB_CONNECT='host=/tmp dbname=test' \
make -C zdf/itest test
```
