# PostgreSQL native Z types

## Overview

PGXS extension exposing native decimal, time, and bitmap operations through PostgreSQL Datum functions and SQL declarations.

## Interface

- `libz` exports PostgreSQL V1 wrappers for `zu_decimal`, `zu_time`, and `zu_bitmap`, plus module compatibility metadata. [zdecimal.c:zdecimal_in] [ztime.c:ztime_in_fn] [zbitmap.c:zbitmap_in] [magic.c:PG_MODULE_MAGIC]
- `zdecimal` is a 16-byte type with text/binary I/O, numeric casts, arithmetic/comparisons, default btree/hash operator classes, and min/max/sum/avg aggregates. Native `zdecimal_round` has no installation-SQL declaration. [zdecimal.sql:zdecimal] [zdecimal.sql:zdecimal_ops] [zdecimal.sql:avg(zdecimal)] [zdecimal.c:zdecimal_round]
- `ztime` is a 16-byte type with default CSV text I/O, explicit ISO/FIX formats, binary I/O, int8/decimal casts, time arithmetic/comparisons, indexes, and min/max. [ztime.sql:ztime] [ztime.sql:ztime_in_iso] [ztime.sql:ztime_ops]
- `zbitmap` is a variable-length externally stored type with bit/range access, iteration, bitwise operations, comparison, and default btree/hash operator classes. [zbitmap.sql:zbitmap] [zbitmap.sql:zbitmap_set_range] [zbitmap.sql:zbitmap_ops]
- Build manifest creates `libz` and concatenates the three SQL inputs into versioned installation SQL. The control file declares version 0 and relocatable installation. [Makefile:OBJS] [Makefile:extension_version] [libz.control:default_version] [libz.control:relocatable]

## Invariants

- Text input permits trailing whitespace but rejects remaining junk. Decimal recognizes exact `NaN` as its native null sentinel. [zdecimal.c:zdecimal_in] [ztime.c:ztime_in_fn] [zbitmap.c:zbitmap_in]
- Decimal binary transfer uses 16 bytes; time uses 12 bytes and zeroes native padding on receipt. Bitmap input contains a word count and words; counts at or above 8192 are rejected after narrowing to unsigned. [zdecimal.c:zdecimal_recv] [ztime.c:ztime_recv] [zbitmap.c:zbitmap_recv]
- Scalar decimal/time arithmetic reports out-of-range when non-sentinel operands produce a null sentinel. Decimal aggregate arithmetic does not use the same scalar overflow check. [zdecimal.c:zdecimal_add] [ztime.c:ztime_add] [zdecimal.c:zdecimal_sum] [zdecimal.c:zdecimal_acc]
- Average state has exactly two non-null elements in one dimension with the expected native size; zero count returns SQL NULL. [zdecimal.c:zdecimal_acc] [zdecimal.c:zdecimal_avg]
- Bitmap SQL declarations depend on `uint4`, while `libz.control` declares no extension dependency. Provide that type before installation. [zbitmap.sql:zbitmap_length] [libz.control:default_version]

## Side Effects

- Native wrappers allocate PostgreSQL memory and formatted/binary output; aggregate-context sum/average can mutate their input state. [zdecimal.c:zdecimal_sum] [zdecimal.c:zdecimal_acc] [ztime.c:ztime_send]
- Bitmap wrappers detoast input and pass mutable storage to native operations; growing operations use the PostgreSQL allocator. [zbitmap.c:PG_GETARG_ZBITMAP_P] [zbitmap.c:zbitmap_set] [zbitmap.c:zbitmap_alloc]
- Sort-support functions install comparator callbacks. Extension installation creates SQL types, functions, casts, operators, operator classes, and aggregates. [zdecimal.c:zdecimal_sort] [ztime.c:ztime_sort] [zbitmap.c:zbitmap_sort] [zdecimal.sql:zdecimal] [ztime.sql:ztime] [zbitmap.sql:zbitmap]

## Verification

With PostgreSQL enabled, build from the configured repository root using `make -j8`. This directory uses PGXS via `pg_config`, after the standard module directories; the module test command is `make -C zdb_pq test`. PGXS separately declares `REGRESS = init zdecimal ztime zbitmap drop` with `--inputdir=test`. [Makefile:PG_CONFIG] [Makefile:REGRESS] No build, installation, database operation, or regression test was executed.
