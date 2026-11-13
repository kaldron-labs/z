# Zdb

## Overview

Sharded database tables and caches, pluggable storage, host replication/recovery, persistent sagas, and database telemetry.

## Subdirectories & Boundaries

- [src](./src/EGM.md): database library, storage contract and in-memory backend.
- [bench](./bench/EGM.md): shared repair benchmark routines.
- [example](./example/EGM.md): in-memory account-transfer saga.

## Interface

- Typed tables expose asynchronous storage-backed queries and shard-local mutations; DB/Host manage lifecycle and replication, Store abstracts persistence, and SagaDB coordinates persistent phases. [src/Zdb.hh:Table_] [src/Zdb.hh:DB] [src/ZdbStore.hh:Store] [src/Zdb.hh:SagaDB]

## Invariants

- Mutation runs on table shards; callbacks may be inline, and borrowed owners must survive queued work. Replication buffers retain their backing. Saga epochs separate retired callbacks from current replay. [src/Zdb.hh:insert_] [src/ZdbMemStore.hh:run] [src/ZdbBuf.hh:RxBuf] [src/ZdbSaga.cc:SagaDriver]

## Side Effects

- Starts storage and peer connections, mutates caches and indexes, persists and replicates records/saga intent, schedules callbacks, and registers telemetry. Shutdown waits for saga work, stops peers/storage and drains shard queues before final release. [src/Zdb.cc:start_] [src/Zdb.cc:write] [src/ZdbSaga.cc:saga_] [src/Zdb.cc:stop_0] [src/Zdb.cc:final]

## Verification

From the configured repository root, run `make -j8 && make -C zdb test`. Commands were not executed for this documentation change. [Makefile.am:test]
