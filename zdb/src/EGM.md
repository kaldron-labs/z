# Zdb source contract

## Overview

Sharded database tables and caches, pluggable storage, host replication/recovery, persistent sagas, and database telemetry.

## Interface

- `Store`/`StoreTbl` expose backend lifecycle, table opening, count/select/find/recover/write callbacks; results distinguish missing rows, errors, and stream completion. Factory symbol is ZdbStore. [ZdbStore.hh:Store] [ZdbStore.hh:StoreTbl]
- Saga definitions describe serialized phases and operations; `SagaDriver` retains the saga and completion state. Ztc database telemetry exposes identity, snapshots, table/host enumeration and manager watches. [ZdbSaga.hh:Saga] [ZdbSaga.hh:SagaDriver] [ZtcDB.hh:DBMgr]
- Typed tables expose count/select/find, insert/update/delete, intended-update-number operations, and eviction. Rows expose typed data, version/sequence/update numbers, shard identity, pinning, and commit/abort. [Zdb.hh:Table_] [Zdb.hh:AnyRow] [Zdb.hh:Row_]
- `DB`, `Host`, and `SagaDB` expose database lifecycle, cluster telemetry/state, table enumeration, and typed saga submission/completion. [Zdb.hh:DB] [Zdb.hh:Host] [Zdb.hh:SagaDB]
- `ZdbMem::Store` is a separately built in-memory driver with keyed tables, schema-aware rows, prefix/continuation selection, find and recovery by shard/update number. [ZdbMemStore.hh:Store_] [ZdbMemStore.cc:select] [ZdbMemStore.cc:recover]
- `RxBuf` retains receive backing while owning metadata; `TxBuf` retains the framed source with per-connection descriptors. Table allocation reserves envelope space. [ZdbBuf.hh:RxBuf] [ZdbBuf.hh:TxBuf] [ZdbBuf.hh:tableBufAllocFn]
- Local `Msg::Root` schemas define Heartbeat, Replication, Recovery and Commit envelopes with a little-endian length header; verified accessors return borrowed payload views, while underscore helpers assume trusted bytes. [ZdbMsg.hh:Root] [ZdbMsg.hh:Hdr] [ZdbMsg.hh:msg]
- Database host/table telemetry provides keyed state and cache counters with synthetic RAG values. [ZtcDBHost.hh:DBHostTelemetry] [ZtcDBTable.hh:DBTableTelemetry]
- The manifest builds `libZdb.la` and separate `libZdbMem.la`, with installed database/store/saga/telemetry headers. [Makefile.am:lib_LTLIBRARIES] [Makefile.am:pkginclude_HEADERS]

## Invariants

- SchemaBuf owns reflection bytes whose borrowed views survive until store close. Shards are 8-bit, update numbers 64-bit, sequence numbers 128-bit, and negative versions denote deletion. [ZdbTypes.hh:SchemaBuf] [ZdbTypes.hh:Shard] [ZdbTypes.hh:VN]
- Store writes use update numbers as idempotency keys; tuple callbacks consume borrowed contents synchronously. Store initialization/finalization are blocking and idempotent. [ZdbStore.hh:Store] [ZdbStore.hh:TupleFn] [ZdbStore.hh:write]
- Saga definitions require phases ending in a nonrepeated terminal phase; operations are Insert/Update/Delete. Repetition counts are immutable saved payload; invalid serialized bytes throw before allocating a saga. [ZdbSaga.hh:SagaDefValid] [ZdbSaga.hh:SagaLayout] [ZdbSaga.hh:load]
- Rows are immovable and retain raw table pointers; tables and query contexts borrow database/store/configuration owners. Mutation wrappers assert shard invocation. Transactions begin with insert/update/delete and end in commit/abort. [Zdb.hh:AnyRow] [Zdb.hh:AnyTable] [Zdb.hh:Table_]
- UN registries retain shared row references and replication buffers own their IOBufs with raw backlinks. Automatic eviction rejects pinned rows; intended-UN insertion assumes consistent caller data for an existing durable key. [Zdb.hh:CacheUN] [Zdb.hh:RepBuf_] [Zdb.hh:FindEvict] [Zdb.hh:insert]
- Shard count is a power of two from 1 to 64; configured shard-thread count is a power of two no larger than shard count. Saga mutation entrypoints require the table shard; forward execution records intent before mutation and compares saved update numbers on replay. [Zdb.hh:DBCf] [Zdb.hh:insert] [Zdb.hh:update] [Zdb.hh:del]
- DB final requires its owning multiplexer stopped first; drainShards alternates shard/DB callbacks before releasing tables. [Zdb.hh:DB] [Zdb.hh:drainShards]
- Memory indexes retain shared rows; key zero enforces primary uniqueness while secondary duplicates are allowed. VN zero inserts, positive updates mutable fields, and negative deletes. Already-open tables and retained shard-count changes are rejected. [ZdbMemStore.hh:Index] [ZdbMemStore.hh:loadRow] [ZdbMemStore.hh:init]
- Saga completions carry epoch, step and direction; stale epochs retire work without executing the old continuation. Activation drains callbacks before rebuilding. Deadlines select replay/rollback during activation rather than running a live timer. [ZdbSaga.cc:SagaDriver::operator ()] [Zdb.cc:sagaActivate] [Zdb.cc:sagaReplay]
- Lifecycle coordination runs on the database owner, table work returns completion through it, and only one aggregate all-table operation may be outstanding. Host Active and application activation are separate states. [Zdb.cc:all] [Zdb.cc:holdElection]
- Incoming records require verified framing, associated peers, known tables, valid shards and row verification. Only insert/update/delete states commit; failed update-number allocation aborts before sequence-number allocation. [Zdb.cc:msgRead2] [Zdb.cc:msgRead3] [Zdb.cc:commit_]

## Side Effects

- Saga construction allocates metadata and initializes step/shard state. Database manager mutation callbacks run under its write lock; enumeration runs under its read lock, while captured snapshot callbacks run after unlocking. Registry pointers borrow database owners. [ZdbSaga.hh:init_] [ZdbSaga.hh:SagaCatalog__] [ZtcDB.cc:ZtcDBMgr_]
- Mutation helpers invoke callbacks inline and abort-clean up on return or exception. Commit updates caches/counts and calls shard write with a replication buffer; recovery marks old buffers stale and updates/removes cached rows and indexes. [Zdb.hh:insert_] [Zdb.hh:update_] [Zdb.hh:del_] [Zdb.hh:commit] [Zdb.hh:rowRecover]
- Storage failures log and schedule database failure on its owner; select errors also report an empty result. DB print mutates election peer/leader fields while formatting. [Zdb.hh:storeFailed] [Zdb.hh:select_] [Zdb.hh:print]
- Memory-store operations schedule closures on the configured multiplex thread, retaining callbacks/buffers but borrowing this; opening completes inline. Writes acknowledge old update numbers, change indexes and watermarks for new operations, and return the original buffer. Finalization clears tables unless preserve is configured. [ZdbMemStore.hh:run] [ZdbMemStore.cc:write] [ZdbMemStore.hh:final]
- Saga submission persists intent before admission success. Forward failures compensate preceding effects; failed compensation abandons bookkeeping and logs without normal completion. Startup reconciles persisted saga definitions, reserves recovery update numbers and deletes orphan intents. [ZdbSaga.cc:saga_] [ZdbSaga.cc:terminal_] [ZdbSaga.cc:sagaCatalog] [ZdbSaga.cc:sagaLoadSteps]
- Initialization loads or accepts a store and configures hosts. Startup opens/recovers tables then elects a leader; failures close tables, stop storage and drain shards. Connections schedule heartbeat/reconnect work and retain self across owner handoffs. Shutdown stops network work, closes tables, stops storage and drains shards before completion; final removes registries and releases state. [Zdb.cc:init] [Zdb.cc:start_] [Zdb.cc:connected] [Zdb.cc:stop_0] [Zdb.cc:final]
- Replication recovery reads serialized/row caches before asynchronous storage and skips missing historical records. Writes cache and forward records before storage completion; successful callbacks return to the shard, evict buffers, and send commit except for recovery. [Zdb.cc:recSend] [Zdb.cc:write]

## Verification

From the configured repository root, run `make -j8 && make -C zdb test`. The module target runs unit and integration suites with ZI_LOGDIR configured. Commands were not executed for this documentation change. [../Makefile.am:test]
