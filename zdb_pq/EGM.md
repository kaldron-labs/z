# Zdb PostgreSQL backend

## Overview

PostgreSQL backing store for Zdb, native PostgreSQL types, and continuation-query benchmark.

## Subdirectories & Boundaries

- [src](./src/EGM.md): libpq pipeline backing store and binary conversion.
- [ext](./ext/EGM.md): PGXS decimal/time/bitmap extension, built after standard module directories.
- [bench](./bench/EGM.md): continuation predicates and optional query-plan workload.

## Interface

`ZdbStore` exposes the driver factory. The extension registers zdecimal, ztime, and zbitmap SQL type families; these are separate from the client backing-store library. [src/ZdbPQ.cc:ZdbStore] [ext/zdecimal.sql:zdecimal] [ext/ztime.sql:ztime] [ext/zbitmap.sql:zbitmap]

## Invariants

The backend uses a dedicated scheduler thread and consumes borrowed libpq results before they are cleared. Startup requires matching shard metadata and extension OIDs. [src/ZdbPQ.cc:Store::init] [src/ZdbPQ.cc:Store::recv] [src/ZdbPQ.cc:Store::getNShards_rcvd] [src/ZdbPQ.cc:Store::getOIDs_send]

## Side Effects

Connects to PostgreSQL, creates or inspects schema/indexes, executes prepared reads/writes, and dispatches callbacks. Extension installation changes SQL catalogs; benchmark database mode creates temporary workload data. [src/ZdbPQ.cc:StoreTbl::mkTable_send] [src/ZdbPQ.cc:StoreTbl::write_send] [ext/zdecimal.sql:zdecimal] [bench/zdbpqbench.cc:explain]

## Verification

With PostgreSQL enabled, run `make -j8 && make -C zdb_pq test` from the configured repository root. PGXS extension regressions are separately declared in ext/Makefile. No builds, installs, database operations, or tests were executed.
