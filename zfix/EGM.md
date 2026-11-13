# Zfix

## Overview

Schema-driven FIX encoding/dispatch and managed or externally managed TCP/TLS transport, with typed examples, load tools and benchmarks.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): formatting, framing, session sequencing, ownership and transport adapters.
- [Utilities](./util/EGM.md): installed load peers, shared workloads, bounded history and checkpoint fixtures.
- [Examples](./example/EGM.md): one-order typed client/server lifecycle.
- [Benchmarks](./bench/EGM.md): codecs, paired native/QuickFIX runs, optional observers and retained-result comparison.

## Interface

[The source manifest](./src/Makefile.am) builds libZfixFmt and libZfix. [The utility manifest](./util/Makefile.am) installs zfix/zfixd; example programs with the same names are non-installed. Applications own business queues, admission policy and durable history.

## Invariants

Borrowed views require retained input backing. Rx and Tx state remain on their respective owners; connection/epoch identity fences asynchronous work. Transport admission, local completion and peer acknowledgement are separate boundaries. Keep hubs alive until output, posted work and deferred hooks drain. See [the library contract](./src/EGM.md).

## Side Effects

The library allocates/retains buffers, formats data, schedules timers/owner work and uses TCP/TLS. Tools additionally create traffic, checkpoints, process groups and benchmark artifacts; the example intentionally has no durable replay store. Saved benchmark result archives and test-only sources are excluded from this index.

## Verification

From a configured repository root, run `make -j8 && make -C zfix test`. [The module target](./Makefile.am) runs unit and integration checks. Commands were not run for this documentation change.
