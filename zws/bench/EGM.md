# Zws loopback benchmark

## Overview

Single-process WebSocket round-trip benchmark with warmup, latency samples and process-wide framework heap/cache telemetry.

## Interface

- [zwsbench.cc](./zwsbench.cc), `Options`: `zwsbench` accepts `--count`/`-n` (10000), `--size`/`-s` (32), `--warmup`/`-w` (1000), `--binary`, `--control`, `--tls`, `--tx-owner`, and help.
- `report` prints transport, Tx path, message kind/size, samples, mean/p50/p90/p99/max latency, heap/cache deltas and received bytes. [zwsbench.cc](./zwsbench.cc)
- [Makefile.am](./Makefile.am), `noinst_PROGRAMS`: builds the non-installed `zwsbench`, linked with protocol/framework libraries and `libZiTest` helpers.

## Invariants

- `main` requires nonzero count and control payload within `Zws::MaxControl`. `run` uses loopback and exact target `/bench`; payload repeats lowercase alphabet. [zwsbench.cc](./zwsbench.cc)
- `ClientApp::send_` keeps one round trip outstanding. `--tx-owner` changes ordinary message initiation to the Tx shard; control ping returns before that branch, so the flag does not alter control initiation. [zwsbench.cc](./zwsbench.cc)
- `ClientApp::complete_` samples only after warmup. `heapStats` aggregates all framework heaps, not just the connection. `report` sorts samples and uses integer mean/nearest-rank percentiles; it emits nothing without samples. [zwsbench.cc](./zwsbench.cc)
- `run` bounds listening/completion waits, stops both hubs, releases the client link, and drains a shard 3→4→3 barrier before finalization. Report eligibility depends on failure state; final exit also incorporates stop results. [zwsbench.cc](./zwsbench.cc)

## Side Effects

- `run` starts scheduler threads, opens a loopback listener/client, exchanges warmup/measured traffic, and allocates payload/sample storage. Server validates and echoes payload; client validates response spans or exact pong payload. [zwsbench.cc](./zwsbench.cc)
- `main` initializes `Zhttp::Test::TempDir` and `ZiTestResidue` helpers even for plain TCP; TLS uses helper certificate/key paths. Their filesystem implementation belongs to the imported helpers. `report` writes stdout and logging uses stderr. [zwsbench.cc](./zwsbench.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zws/test test`. The benchmark is built by the default build but was not executed for this documentation change.
