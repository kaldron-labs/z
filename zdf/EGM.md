# Zdf

## Overview

Compressed typed dataframes, time series, historical/live readers, and statistics over Zdb storage.

## Subdirectories & Boundaries

- [src](./src/EGM.md): storage integration, codecs, readers/writers, and statistical structures.

## Interface

`Store` opens named `Series` and reflected `DataFrame` instances; `Stats` and `StatsTree` provide incremental aggregation and ordering. [src/ZdfStore.hh:Store] [src/ZdfSeries.hh:Series] [src/Zdf.hh:DataFrame] [src/ZdfStats.hh:Stats]

## Invariants

Keep parent store/series/dataframe owners alive through use, respect shard affinity, and use monotonically increasing values for value-based searches. Storage row callbacks do not imply cross-row durable transactions. [src/ZdfSeries.hh:Series::invoked] [src/ZdfSeries.hh:Reader::Ctrl] [src/ZdfSeries.hh:Series::saveBlk]

## Side Effects

Creates and updates Zdb metadata/payload rows, posts shard work, mutates compressed blocks, and invokes reader/writer callbacks. [src/ZdfStore.hh:Store::openSeries_] [src/ZdfSeries.hh:Series::saveBlk] [src/ZdfSeries.hh:Series::write_notify]

## Verification

From the configured repository root, run `make -j8 && make -C zdf test`. No builds or tests were run for this documentation change.
