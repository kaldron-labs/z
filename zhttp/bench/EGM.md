# Zhttp loopback benchmark

## Overview

Concurrent request-wave benchmark for TCP, TLS, and QUIC HTTP profiles, including latency and framework heap observations.

## Interface

[zhttpbench.cc](./zhttpbench.cc) accepts MASK COUNT JOBS. Supported masks are 1, 2, 4, 8, 6, 12, and 15; count/jobs must be nonzero and count must cover jobs. ZHTTP_BENCH_HEAPS enables per-heap output. [Makefile.am](./Makefile.am) builds the non-installed executable with test fixture support.

## Invariants

Ten warmup waves precede timed requests; responses contain 64 bytes. Masks containing HTTP/3 force that client policy even when other listeners are enabled. A final wave retains server routes for heap measurement before releasing them on the server shard. Exit fails on errors or incorrect timed response bytes. Normal completion explicitly stops/finalizes endpoints; early failure paths return before that explicit teardown. [zhttpbench.cc](./zhttpbench.cc)

## Side Effects

The benchmark starts logging/resolution and four scheduler threads, initializes temporary certificate fixtures even for TCP, and opens loopback listeners near port 38300. It sends request traffic and prints latency percentiles, elapsed time, allocation/heap observations, received bytes, and errors. [zhttpbench.cc](./zhttpbench.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zhttp test`. Intentional benchmark runs additionally require free loopback ports and fixture setup. Commands were not run for this documentation change.
