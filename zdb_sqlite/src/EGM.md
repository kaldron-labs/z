# Zdb SQLite storage

## Overview

SQLite backing-store plugin for Zdb, canonical ordered scalar codecs, and an optional SQLite inspection extension.

## Interface

- Exported C factory `ZdbStore` constructs `ZdbSL::Store`; `StoreTbl` supplies asynchronous count/select/find/recover/write/close. Operations dispatch to the configured store worker. [ZdbSL.cc:ZdbStore] [ZdbSL.hh:Store] [ZdbSL.hh:StoreTbl]
- Configuration requires `thread` and `connect`; synchronous mode accepts NORMAL, FULL, or OFF and defaults to NORMAL. [ZdbSL.hh:StoreCf] [ZdbSL.hh:Synchronous]
- Internal codecs encode ordered integers, floating values, Time, DateTime, Decimal, and Fixed. Checked loaders validate scalar widths; pointer loaders expose unchecked primitives. [ZdbSLCodec.hh:saveUnsigned] [ZdbSLCodec.cc:loadFloat] [ZdbSLCodec.cc:loadTime]
- `sqlite3_zdbslinspect_init` registers `zdbsl_datetime`, `zdbsl_time`, `zdbsl_decimal`, and `zdbsl_float` as deterministic, innocuous UTF-8 SQL functions. [ZdbSLInspect.cc:sqlite3_zdbslinspect_init]
- Build targets are `libZdbSL.la` and `libZdbSLInspect.la`; only ZdbSLLib.hh and ZdbSL.hh are installed public headers. [Makefile.am:lib_LTLIBRARIES] [Makefile.am:pkginclude_HEADERS]

## Invariants

- The worker SID must resolve outside the multiplexer Rx/Tx threads. SQLite uses READWRITE|CREATE|NOMUTEX; the table registry is unlocked and accessed through store dispatch. [ZdbSL.cc:Store::init] [ZdbSL.cc:Store::start_] [ZdbSL.hh:StoreTbls]
- Startup requires actual WAL mode and compatible persisted format=2 and shard count. Internal tables must match exact ordered schema and STRICT requirements. [ZdbSL.cc:Store::startup_] [ZdbSL.cc:validateCoreTable] [ZdbSL.cc:validateCoreStrict]
- Logical schema, field encoding/storage/properties, and key masks must match exactly. Physical tables require expected ordered NOT NULL columns and STRICT mode; index validation rejects missing, extra, partial, duplicate, or incorrectly ordered/directed indexes. [ZdbSL.cc:StoreTbl::schema_]
- Values fit SQLite and Zdb buffer limits. SQLITE_STATIC bindings retain backing until statement reset; every parameter is rebound. Integer encodings are big-endian with signed sign-bit transformation; floating encodings canonicalize NaNs and both zero signs. [ZdbSL.cc:planInput] [ZdbSL.cc:bindInput] [ZdbSL.cc:Reset] [ZdbSLCodec.hh:saveSigned] [ZdbSLCodec.cc:saveFloat]
- Writes reject invalid shards and acknowledge already-applied UN values without mutation. VN zero inserts, positive VN updates with insertion fallback, and negative VN deletes exactly one row. [ZdbSL.cc:StoreTbl::write_]
- Selection respects key grouping and mixed sort directions. Reconstruction checks storage types, codec widths, vector bounds, and trailing data. Inspection functions accept only correctly encoded BLOB values. [ZdbSL.cc:selectSQL] [ZdbSL.cc:saveVarVec] [ZdbSL.cc:StoreTbl::row_] [ZdbSLInspect.cc:inspect]
- `final` releases configuration/table state but does not close SQLite; stop must handle connection shutdown first. Closed table objects can outlive schema owners, so reopening refreshes reflected field views. [ZdbSL.cc:Store::final] [ZdbSL.cc:Store::stop_1] [ZdbSL.hh:StoreTbl::schema]

## Side Effects

- Opens/creates the configured database, sets WAL/synchronous pragmas, and creates STRICT tables `zdbsl_meta`, `zdbsl_schema`, and `zdbsl_mrd`. Logical tables map to `i_<id>` or `a_<id>`, with quoted identifiers and generated key/high-water indexes. [ZdbSL.cc:Store::startup_] [ZdbSL.cc:StoreTbl::StoreTbl] [ZdbSL.cc:StoreTbl::schema_]
- Each accepted write transaction covers row mutation and required high-water metadata. In-memory counts/sequence state update after commit; active transaction destruction rolls back. Durability follows the configured synchronous mode. [ZdbSL.cc:Txn] [ZdbSL.cc:StoreTbl::write_] [ZdbSL.cc:Store::startup_]
- Queries allocate result buffers and emit callbacks; select emits a terminal empty result after successful exhaustion. Writes return the original buffer with their result. Shutdown rejects new SQL work through operation-specific callbacks. [ZdbSL.cc:StoreTbl::select_] [ZdbSL.cc:StoreTbl::tuple_] [ZdbSL.cc:StoreTbl::write_] [ZdbSL.cc:shutdownError]
- Table close retains prepared state for reopening; finalization releases statements. Store stop finalizes transaction statements and closes SQLite; a close failure retains the connection for reporting/retry. [ZdbSL.cc:StoreTbl::close_] [ZdbSL.cc:StoreTbl::finalize_] [ZdbSL.cc:Store::stop_1]
- Loading the inspection extension registers functions on the supplied SQLite connection; evaluation allocates formatted scratch text and returns SQLite errors for invalid input. [ZdbSLInspect.cc:sqlite3_zdbslinspect_init] [ZdbSLInspect.cc:inspect]

## Verification

With SQLite enabled, run `make -j8 && make -C zdb_sqlite test` from the configured repository root. No builds, database operations, or tests were executed for this documentation change.
