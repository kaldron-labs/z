# SQLite storage benchmark

## Overview

Indexed seeks, payload operations, empty transactions, and database reopen measurements.

## Interface

- Non-installed `zdbslbench` takes no arguments; `make bench` runs it. Reports p50/p99 timings, query plans, reopen latency, and SQLite memory counters. [Makefile.am:bench] [zdbslbench.cc:main] [zdbslbench.cc:seek]

## Invariants

- Seek/payload workloads discard 64 warmups and retain 1024 samples. Statements reset between executions and are finalized after use. [zdbslbench.cc:seek] [zdbslbench.cc:operation]
- `SQLITE_STATIC` payload storage remains alive through statement finalization. The database uses NOMUTEX, WAL, and synchronous=OFF. [zdbslbench.cc:main]

## Side Effects

- Creates and writes a `/tmp/zdbslbench-XXXXXX` database, prints results, and unlinks the database on normal completion. Failure uses `_exit(1)` and bypasses normal cleanup. [zdbslbench.cc:main] [zdbslbench.cc:fail]

## Verification

With SQLite enabled, run `make -j8 && make -C zdb_sqlite/bench bench` from the configured repository root. Payload operations check SQLITE_DONE; seek loops do not explicitly check the terminal step result. [Makefile.am:bench] [zdbslbench.cc:operation] [zdbslbench.cc:seek] The benchmark was not executed.
