# Zdb SQLite backend

## Overview

SQLite implementation of the Zdb storage interface, scalar inspection extension, and storage benchmark.

## Subdirectories & Boundaries

- [src](./src/EGM.md): backing store, codecs, and inspection extension.
- [bench](./bench/EGM.md): standalone SQLite workload measurements.

## Interface

`ZdbStore` constructs the backing-store implementation; `sqlite3_zdbslinspect_init` installs value-inspection SQL functions. [src/ZdbSL.cc:ZdbStore] [src/ZdbSLInspect.cc:sqlite3_zdbslinspect_init]

## Invariants

The configured worker serializes NOMUTEX database access. Startup/schema validation requires compatible metadata, STRICT tables, and exact indexes; shutdown must close SQLite before finalization. [src/ZdbSL.cc:Store::init] [src/ZdbSL.cc:Store::startup_] [src/ZdbSL.cc:StoreTbl::schema_] [src/ZdbSL.cc:Store::final]

## Side Effects

Opens database files, changes pragmas, creates/validates schema, commits row/high-water updates, and invokes operation callbacks. The benchmark writes a temporary database. [src/ZdbSL.cc:Store::startup_] [src/ZdbSL.cc:StoreTbl::write_] [bench/zdbslbench.cc:main]

## Verification

With SQLite enabled, run `make -j8 && make -C zdb_sqlite test` from the configured repository root. Commands were not executed for this documentation change.
