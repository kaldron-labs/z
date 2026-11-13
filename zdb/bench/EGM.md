# Database repair benchmark support

## Overview

Shared serialization, storage, saga, and scheduler benchmark routines.

## Interface

- `RepairBench::run(count, rounds, width, heaps=true)` runs workloads. `ZDB_REPAIR_CONFIG` supplies configuration; `ZDB_MODULE` selects a dynamic store, otherwise the memory store is used. `ZDB_REPAIR_STORE_ONLY` excludes saga/scheduler workloads. [ZdbRepairBench.hh:run] [ZdbRepairBench.hh:config] [ZdbRepairBench.hh:newStore]

## Invariants

- Requires `NDEBUG` and nonzero workload dimensions. [ZdbRepairBench.hh:run]
- Store setup and asynchronous operations complete before measurement or teardown proceeds. Teardown closes the table, stops/finalizes the store, and stops the multiplexer. [ZdbRepairBench.hh:store]
- Heap snapshots are main-thread-only; each round excludes its first complete iteration. Deletion serializes only the primary key. [ZdbRepairBench.hh:heap] [ZdbRepairBench.hh:output] [ZdbRepairBench.hh:store]

## Side Effects

- Prints CSV and serialized shapes, mutates repairbench rows, schedules work, and optionally loads a store module. Failed checks print diagnostics and terminate. [ZdbRepairBench.hh:run] [ZdbRepairBench.hh:store] [ZdbRepairBench.hh:newStore] [ZdbRepairBench.hh:check]

## Verification

The shared header checks serialized masks, reconstructed rows, store outcomes, saga completion, and operation totals. [ZdbRepairBench.hh:shape] [ZdbRepairBench.hh:store] [ZdbRepairBench.hh:sagas] No standalone benchmark target is asserted here; no benchmark was executed.
