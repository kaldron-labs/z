# Zm concurrency benchmarks

## Overview

Standalone workloads for synchronization, references/delegates, hashes/caches, heap allocation, rings, and reader/writer locks. Harness assumptions and checks are distinct from library guarantees.

## Interface

- `zmbench` runs fixed semaphore, lock, reference, singleton, and thread-local exercises. `zmfnbench [N]` compares five call forms; `zmcachebench [cacheSize [batchSize [nThreads [nLoops]]]]` exercises cache misses and loads. [zmbench.cc:main] [zmfnbench.cc:main] [zmcachebench.cc:main]
- `zmhashbench`, `zmhashthreadbench`, and `zmlhbench` measure hash workloads; heap programs take `COUNT SIZE NTHR [VERB]`. [zmhashbench.cc:main] [zmhashthreadbench.cc:main] [zmlhbench.cc:main] [zmheapbench.cc:main] [zmheapbench2.cc:main]
- `zmlockbench nthreads [count]` compares locks; `zmlockbench2` accepts thread/record/delay/iteration dimensions. RW programs accept `[ITERATIONS [READERS [WRITERS]]]`. [zmlockbench.cc:main] [zmlockbench2.cc:main] [zmrwbench.cc:main] [zmrwlockbench.cc:main]
- `zmringbench` exposes writer/reader/message/loop counts, size, timing, spin, and cpuset options; template dispatch selects multi-writer/reader modes from counts. The manifest builds all programs as non-installed executables. [zmringbench.cc:Params] [zmringbench.cc:main] [Makefile.am:noinst_PROGRAMS]

## Invariants

- `zmbench` and `zmhashbench` assume CPU count fits their fixed 80-worker arrays. Some singleton counters are unsynchronized; the NoLock comparison also increments shared non-atomic state. These cases are timing workloads, not correctness-safe concurrent-counter demonstrations. [zmbench.cc:main] [zmbench.cc:S::meyers] [zmhashbench.cc:main] [zmlockbench.cc:work]
- Cache workers retain referenced owners through joins; zero threads can reach division by zero. Heap workload input conversion does not comprehensively reject negative values or arithmetic overflow. [zmcachebench.cc:main] [zmheapbench.cc:main] [zmheapbench2.cc:main]
- Threaded hash checks count attempted operations and bounds, not universal insertion/deletion success. Delegate benchmark counters accumulate across all five cases. [zmhashthreadbench.cc:main] [zmfnbench.cc:main]
- Record-lock users register under the table lock before acquiring their record lock, retain a reference through use, and release before decrement/removal. Completion checks no observed record-ID mismatches and an empty table. [zmlockbench2.cc:getlock] [zmlockbench2.cc:unlock] [zmlockbench2.cc:main]
- Ring readers verify stored message addresses before retirement; writers publish before EOF and reader joins. Worker errors often only print/break rather than producing a nonzero program exit. [zmringbench.cc:Msg] [zmringbench.cc:App::reader] [zmringbench.cc:App::run]
- RW checks validate the expected final counter or equality with successful exclusive writes; they do not assert fairness or minimum success. [zmrwbench.cc:main] [zmrwlockbench.cc:main]

## Side Effects

- Allocates framework objects, hash/cache nodes, scratch storage, heaps, and thread arrays; launches and joins ZmThread, scheduler, or pthread workers; prints timing/statistical output. [zmbench.cc:main] [zmcachebench.cc:main] [zmhashthreadbench.cc:main] [zmheapbench2.cc:main] [zmlockbench.cc:main_]
- Cross-thread heap benchmark allocates on one worker and schedules deletion on the next before stopping the scheduler. [zmheapbench2.cc:main]
- Ring workload opens/closes ring storage, attaches/detaches readers, logs, sleeps during retry, and aggregates statistics across loop iterations. [zmringbench.cc:App::run] [zmringbench.cc:App::reader] [zmringbench.cc:App::writer]
- Harness failures can print diagnostics, abort, or exit; some low-level pthread return codes are unchecked. [zmheapbench.cc:S::doit] [zmlockbench.cc:main_] [zmrwbench.cc:usage_]

## Verification

From the configured repository root, run `make -j8 && make -C zm/test test` for the separate unit suite. Benchmarks build through their default target and should be run with explicitly chosen workload dimensions. No benchmark or test was executed for this documentation change.
