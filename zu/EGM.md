# Zu

## Overview

Foundation library for low-level types, compile-time reflection, containers, numeric/text codecs, time values and test infrastructure.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): installed C++/C interfaces and compiled support code.
- [Benchmarks](./bench/EGM.md): numeric, formatting, allocation and container workloads.

## Interface

The library exports fixed storage and borrowed views, tuples/unions and reflected fields, ownership helpers, numeric/date values, codecs, formatting, bitmap C interfaces and the TAP framework. [src/Makefile.am:libZu_la_SOURCES] [src/ZuStruct.hh:ZuStruct] [src/ZuTest.hh:ZuTestMgr]

## Invariants

Keep borrowed backing storage alive, supply capacity to unchecked low-level codecs, and preserve manual object/tag lifetime rules. Intrusive references and the TAP manager are not thread-safe. Numeric nulls and parsing behavior depend on the selected type/format. See the [library contract](./src/EGM.md). [src/ZuSpan.hh:ZuSpan] [src/ZuRef.hh:ZuRef] [src/ZuUnion.hh:Union]

## Side Effects

Operations mutate caller storage, invoke user callbacks and construct/destroy owned values. C bitmap operations call allocators; demangler/calendar configuration changes process state; TAP execution writes output and can exit on failure. [src/zu_bitmap.cc:zu_bitmap_resize] [src/ZuDemangle.cc:transformFn] [src/ZuDateTime.cc:reformation] [src/ZuTest.cc:end_]

## Verification

From a configured repository root, run `make -j8 && make -C zu test`. Commands were not run for this documentation change. [Makefile.am:test]
