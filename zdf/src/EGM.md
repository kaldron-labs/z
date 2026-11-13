# Zdf dataframes and time series

## Overview

Typed, compressed field series backed by Zdb tables, with historical/live readers and incremental statistics.

## Interface

- `Store` configures and opens tables, named series, and typed `DataFrame<O, TimeIndex>` objects. Dataframes select fields carrying the Series property; time indexing adds a synthetic `_time` field. [ZdfStore.hh:Store] [Zdf.hh:Fields_] [Zdf.hh:TimeField_]
- `Series<Decoder>` exposes seek/find readers, writer acquisition, counts, and reader/writer shutdown. Reader `Ctrl` supports seek/find, callback replacement, pause, stop, and purge. `TimeSeries` converts epoch-relative nanoseconds. [ZdfSeries.hh:Series] [ZdfSeries.hh:Reader::Ctrl] [ZdfSeries.hh:TimeSeries]
- `Decoder`, `DeltaDecoder`, `FloatDecoder`, and specialized `Encoder` types implement integer/run, delta, and XOR floating compression. `Blk` wraps packed metadata and cached payload rows. [ZdfCompress.hh:Decoder] [ZdfCompress.hh:DeltaDecoder] [ZdfCompress.hh:FloatDecoder] [ZdfBlk.hh:Blk]
- `Stats` supplies count/total/mean/population variance/deviation; `StatsTree` adds duplicate-aware order, rank, median, and extrema. [ZdfStats.hh:Stats] [ZdfStats.hh:StatsTree]
- Persistence records are `SeriesFixed`, `SeriesFloat`, `BlkFixed`, `BlkFloat`, and `BlkData`, with corresponding derived table types. The manifest exports `libZdf.la` and its public headers. [ZdfSchema.hh:ZdbTableDerive] [Makefile.am:pkginclude_HEADERS]

## Invariants

- Store, series, dataframe, and writer raw parent pointers require enclosing owners to survive outstanding use. Series pins its metadata row; codecs borrow payload storage. `WrRef` is move-only and stops its writer on destruction/replacement. [ZdfStore.hh:Store] [ZdfSeries.hh:Series::Series] [Zdf.hh:WrRef] [ZdfCompress.hh:Decoder] [ZdfBlk.hh:Blk::decoder]
- Shard-sensitive series operations assert invocation on their owner. Store dispatch forwards through Zdb: `run` posts; `invoke` can execute inline on the owning shard. Dispatch itself is not a completion barrier. [ZdfSeries.hh:Series::invoked] [ZdfStore.hh:Series<Decoder>::run] [ZdfStore.hh:Series<Decoder>::invoke]
- Block size/count limits are 4096; packed offsets use 47 bits and NDP uses five. Codec search callbacks must not skip beyond the offered run. [ZdfTypes.hh:BlkSize] [ZdfTypes.hh:MaxBlkCount] [ZdfBlk.hh:Blk::init] [ZdfCompress.hh:Decoder::search]
- Value-based find requires monotonically increasing values. `readVec` requires positive length and sufficient caller storage. Historical callbacks can pause delivery by returning false; live notification ignores that return and uses explicit pause/stop controls. [ZdfSeries.hh:Reader::Ctrl] [ZdfSeries.hh:Series::readVec] [ZdfSeries.hh:Reader::nextValue] [ZdfSeries.hh:Reader::notifyValue]
- Only one writer is admitted per series. `stopWriting` does not invoke its callback when no writer exists. Writer acquisition can call its continuation inline when resuming a partially filled block. [ZdfSeries.hh:Series::write] [ZdfSeries.hh:Series::stopWriting] [ZdfSeries.hh:Series::write_loadedBlk]
- Dataframe names must fit the ID limit, and count requires a nonempty selected field list. Supported decoder flags are plain, Delta, Delta2, and Float; unsupported combinations lack specializations. [ZdfStore.hh:Store::openDF] [Zdf.hh:DataFrame::count] [Zdf.hh:FieldDecoder_]
- Empty statistics return zero mean/variance; rank requires `0 <= n < 1`; median selects the upper middle for even counts. [ZdfStats.hh:Stats::mean] [ZdfStats.hh:Stats::var] [ZdfStats.hh:StatsTree::rank] [ZdfStats.hh:StatsTree::median]

## Side Effects

- Store configuration may replace configured threads and add table configuration. Opening scans series keys, advances the next ID, commits created records, and loads block indexes. [ZdfStore.cc:Store::dbCf] [ZdfStore.cc:Store::open_0] [ZdfStore.hh:Store::openSeries_]
- Encoding mutates payload buffers and metadata; appends notify live readers synchronously. Dataframe writes stop at the first failed field without rolling back earlier fields. [ZdfCompress.hh:Encoder<Decoder>::write] [ZdfSeries.hh:Series::write_notify] [Zdf.hh:DFWriter::write]
- Rollover/stop save block header and payload rows in separate nested callbacks, then unpin payload and notify the writer. These callbacks do not establish a cross-row durable transaction guarantee. [ZdfSeries.hh:Series::saveBlk] [ZdfSeries.hh:Series::savedBlk]
- Reader stop releases pins/registration and schedules completion. Purge advances indexed-start metadata without explicitly deleting stored block rows; eviction clears cached payload references. [ZdfSeries.hh:Reader::stopped] [ZdfSeries.hh:Series::purge] [ZdfSeries.hh:Series::unloadBlkData]
- Statistics mutate accumulators; the duplicate-aware tree allocates and removes nodes. [ZdfStats.hh:Stats::add] [ZdfStats.hh:StatsTree::Alloc] [ZdfStatsTree.hh:stats_tree::erase]

## Verification

From the configured repository root, run `make -j8 && make -C zdf test`. The module target invokes unit and integration tests. Commands were not executed for this documentation change.
