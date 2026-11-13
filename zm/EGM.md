# Zm

## Overview

Runtime foundation for allocation, ownership, containers, synchronization, threads, scheduling, diagnostics and telemetry.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): installed Zm/Ztc APIs and platform implementations.
- [Benchmarks](./bench/EGM.md): concurrency, allocation, containers and callable workloads.

## Interface

The library supplies configurable heaps and containers, intrusive references, locks, rings, thread/scheduler lifecycles, global/TLS instances and runtime telemetry. [src/Makefile.am:libZm_la_SOURCES] [src/ZmScheduler.hh:ZmScheduler] [src/ZtcThread.hh:ThreadMgr]

## Invariants

Container synchronization and ownership depend on selected policies, often defaulting to no locking. Retain backing resources through borrowed views and asynchronous callbacks; stop/drain engines and workers before destruction. Heap deallocation must match allocation size/policy. See the [library contract](./src/EGM.md). [src/ZmNodeFn.hh:ZmNodeFn_] [src/ZmEngine.hh:ZmEngine] [src/ZmVHeap.hh:free]

## Side Effects

Operations allocate/free memory, construct/destroy retained values, block/wake/create threads, invoke application callbacks, install process handlers and emit diagnostics/telemetry. Global and TLS facilities register cleanup and invoke finalizers. [src/ZmHeap.cc:ZmHeapCache] [src/ZmThread.cc:run_] [src/ZmTrap.cc:trap] [src/ZmGlobal.cc:ZmGlobal_atexit]

## Verification

From a configured repository root, run `make -j8 && make -C zm test`. Tests and benchmarks were not executed for this documentation change. [Makefile.am:test]
