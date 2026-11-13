# Zfix load utilities

## Overview

Installed FIX initiator/acceptor load programs and shared deterministic workloads, bounded result/history storage, checkpoint fixtures, and an importable paired-run harness.

## Interface

- [zfix.cc](./zfix.cc), [zfixd.cc](./zfixd.cc), and [load_main.hh](./load_main.hh): initiator/acceptor entry points through ZfixLoad::native.
- [load_run.hh](./load_run.hh), [load_run.cc](./load_run.cc): workload options, Run/Broker state, and JSON readiness/result reporting.
- [load.hh](./load.hh), [load.cc](./load.cc), [load_fix.hh](./load_fix.hh): deterministic order/cancel/replace/report models, validation, exact bounded latency samples, and FIX dictionary/rendering.
- [load_native.hh](./load_native.hh), [load_native.cc](./load_native.cc): native adapters, history association, reconnect/recovery/reset profiles, and checkpoints. [pair-test.py](./pair-test.py) exposes imported harness functions.
- [Makefile.am](./Makefile.am) builds convenience libraries and installed zfix/zfixd.

## Invariants

- Reusable DTO text/group views borrow model storage; serialize before the next mutation. Samples are bounded and sorted after recording stops; cache eviction follows insertion order, with node/backing charges. [load.cc](./load.cc), [load_cache.hh](./load_cache.hh)
- Workload admission obeys phase, inflight, count/duration, rate and retention limits. Response validation covers business identities, economics, execution uniqueness and workflow ordering. Completed calls require release. [load_run.hh](./load_run.hh), [load.cc](./load.cc)
- Borrowed DTOs become immutable owning TxBody before submission. Busy/Held work enters a byte-bounded FIFO; published business history is retained by epoch/sequence before flush. Ordinary reconnect preserves owners; reset/recovery reconstructs only after drain. [load_native.hh](./load_native.hh)
- Lifecycle profiles require one session, no warmup, and orders workflow. Checkpoints are initiator-only, reject reset with state input, and use fixture-local native binary representation. Load validates stored identities/bodies/bounds; owner preparation validates cursors. [load_run.cc](./load_run.cc), [load_native.cc](./load_native.cc)
- Shutdown timeout forces disconnect but still waits for actual stop before releasing owners. Successful workload validation precedes checkpoint save. [load_native.cc](./load_native.cc)

## Side Effects

- Programs open TCP or TLS sockets, start threads, log to stderr, and flush JSON readiness/results to stdout. ZFIX_HEAPSTATS enables framework allocator observations. [load_native.cc](./load_native.cc), [load_run.cc](./load_run.cc)
- Checkpoints use NoFollow file access; output uses mode 0600 and sync. [load_native.cc](./load_native.cc)
- The imported pair harness requires an empty artifact directory, records logs/commands/hashes/case results, can invoke openssl for a temporary certificate, and launches peers and optional fault proxy in separate process sessions. Cleanup sends TERM then KILL after a grace period. [pair-test.py](./pair-test.py)

## Verification

From a configured repository root, run `make -j8 && make -C zfix test`. Unit tests include load-model and run validation; manual load runs create traffic and optional checkpoints. Commands were not run for this documentation change.
