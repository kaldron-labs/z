# PostgreSQL continuation benchmark

## Overview

Mixed-direction continuation predicate generation and optional query-plan measurement.

## Interface

- Non-installed `zdbpqbench` prints predicates for 2, 4, and 8 fields. An optional first argument is passed to `PQconnectdb` to run the database workload. [Makefile.am:noinst_PROGRAMS] [zdbpqbench.cc:main]

## Invariants

- Keys alternate ascending and descending fields; continuation values carry explicit `int8` casts. [zdbpqbench.cc:key] [zdbpqbench.cc:predicate]
- Each result is cleared. The connection closes on normal completion or connection failure; SQL failure exits immediately. [zdbpqbench.cc:exec] [zdbpqbench.cc:main]

## Side Effects

- Prints SQL and plans. With a connection, creates a temporary 100000-row table/index, runs ANALYZE and EXPLAIN (ANALYZE, BUFFERS), and reports two continuation positions. [zdbpqbench.cc:explain] [zdbpqbench.cc:main]

## Verification

With PostgreSQL enabled, build using `make -j8` from the configured repository root. `./zdb_pq/bench/zdbpqbench` exercises predicate output without a database connection. Database execution checks CONNECTION_OK and accepted PGresult statuses. [zdbpqbench.cc:main] [zdbpqbench.cc:exec] Not executed.
