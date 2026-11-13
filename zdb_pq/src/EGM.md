# Zdb PostgreSQL storage

## Overview

Zdb backing-store plugin using libpq binary parameters, prepared statements, and pipeline operation on a dedicated store thread.

## Interface

- C factory `ZdbStore` creates `ZdbPQ::Store`; `Store` supplies init/start/stop/final/open and `StoreTbl` supplies count/select/find/recover/write/close. [ZdbPQ.cc:ZdbStore] [ZdbPQ.hh:Store] [ZdbPQ.hh:StoreTbl]
- `StoreCf` requires thread/connect settings and defaults replicated to false. Startup resolves built-in and pguint/libz extension OIDs; it does not install extensions. [ZdbPQ.hh:StoreCf] [ZdbPQ.cc:OIDs::name] [ZdbPQ.cc:Store::getOIDs_send]
- `Value`, `OIDs`, `ParamLayout`, and tuple conversion helpers bridge reflected framework/FlatBuffers values to PostgreSQL binary scalar/vector representations. [ZdbPQ.hh:Value] [ZdbPQ.hh:OIDs] [ZdbPQ.hh:ParamLayout] [ZdbPQ.hh:loadTuple] [ZdbPQ.hh:saveTuple]
- Count returns a scalar; select emits projected buffers and terminal empty completion; find/recover return copied replication/recovery records or an empty result. [ZdbPQ.cc:StoreTbl::count_rcvd] [ZdbPQ.cc:StoreTbl::select_rcvd] [ZdbPQ.cc:StoreTbl::find_rcvd_]
- The manifest builds `libZdbPQ.la`, installs ZdbPQLib.hh/ZdbPQ.hh, and links libpq plus framework dependencies. [Makefile.am:libZdbPQ_la_SOURCES] [Makefile.am:pkginclude_HEADERS] [Makefile.am:libZdbPQ_la_LIBADD]

## Invariants

- Store thread selection excludes multiplexer Rx/Tx threads. Initial `PQconnectdb` is synchronous; nonblocking and pipeline modes are enabled afterward. Socket progress and store operations then run through the configured event-loop/thread machinery. [ZdbPQ.cc:Store::init] [ZdbPQ.cc:Store::start_] [ZdbPQ.cc:Store::start]
- Results are dispatched synchronously and cleared immediately afterward. Borrowed PostgreSQL spans cannot outlive their PGresult; outgoing borrowed strings/bytes and conversion scratch must survive the send call. [ZdbPQ.cc:Store::recv] [ZdbPQ.hh:Value::load] [ZdbPQ.hh:loadTuple] [ZdbPQ.cc:Store::sendPrepared]
- Parameter planning bounds individual strings/bytes and aggregate conversion scratch. Scalar loads enforce widths; vector-header checks accept empty or one-dimensional lower-bound-one arrays but do not establish complete element/buffer validity. [ZdbPQ.hh:planTuple] [ZdbPQ.hh:Value::load] [ZdbPQ.hh:validateVecHdr]
- Startup requires resolved types and matching persisted shard count. Table opening checks recognized field names/types and expected index columns; these checks do not establish exact existing NOT NULL/primary-key layouts. [ZdbPQ.cc:Store::getNShards_rcvd] [ZdbPQ.cc:StoreTbl::mkTable_rcvd] [ZdbPQ.cc:StoreTbl::mkIndices_rcvd]
- Per-shard UN suppresses already-applied writes. VN zero inserts, positive VN updates, and negative VN deletes; update/delete require exactly one affected row. Delete and skipped insert chain a separate MRD write before final callback. [ZdbPQ.cc:StoreTbl::write_send] [ZdbPQ.cc:StoreTbl::write_rcvd]
- Stop fences admission and drains queued/sent work before closing the connection. Closed/reopen table handles are retained and refresh schema views on reopening. [ZdbPQ.cc:Store::stop] [ZdbPQ.cc:Store::stop_1] [ZdbPQ.cc:Store::open] [ZdbPQ.hh:StoreTbl::schema]

## Side Effects

- Connects to PostgreSQL, registers socket callbacks, allocates work/result buffers, submits prepared binary SQL, and invokes completion/failure callbacks. [ZdbPQ.cc:Store::start] [ZdbPQ.cc:Store::send] [ZdbPQ.cc:StoreTbl::select_save] [ZdbPQ.cc:Store::notice]
- Startup creates schema `zdb`, table `zdb.meta` for shard metadata, and `zdb.mrd` for per-table/shard high-water values. Logical internal tables use the zdb schema; application tables use public. [ZdbPQ.cc:Store::mkSchema_send] [ZdbPQ.cc:Store::mkTblMeta_send] [ZdbPQ.cc:Store::mkTblMRD_send] [ZdbPQ.cc:StoreTbl::StoreTbl]
- Opening can create tables and indexes, prepare queries, count rows, and merge live-row and MRD recovery maxima. Query preparation implements grouped, direction-aware continuations. [ZdbPQ.cc:StoreTbl::mkTable_send] [ZdbPQ.cc:StoreTbl::mkIndices_send] [ZdbPQ.cc:StoreTbl::prepSelect_send] [ZdbPQ.cc:StoreTbl::mrd_rcvd]
- Write results update in-memory recovery maxima before terminal null-result completion. Pipeline synchronization/callbacks alone do not establish an explicit transaction coupling row mutation to the separate MRD operation. [ZdbPQ.cc:StoreTbl::write_rcvd] [ZdbPQ.cc:Store::send]
- Stop calls PQfinish and resets retained table state; finalization releases event-loop and registry state. [ZdbPQ.cc:Store::stop_1] [ZdbPQ.cc:Store::final]

## Verification

With PostgreSQL enabled and the required server/extensions available, run `make -j8 && make -C zdb_pq test` from the configured repository root. No builds, database operations, or tests were executed for this documentation change.
