# Zi

## Overview

Platform files and processes, scheduled network I/O and buffers, stream framing, shared-memory rings, logging, DNS, and I/O telemetry.

## Subdirectories & Boundaries

- [src](./src/EGM.md): platform and I/O library.
- [example](./example/EGM.md): TCP/UDP workloads and logging demonstration.

## Interface

- ZiMultiplex/ZiConnection and plugin hooks integrate scheduled Rx/Tx; IOBuf and receive/transmit streams handle framing and backing storage. Files, rings, logging, resolver, process and telemetry APIs provide system services. [src/ZiMultiplex.hh:ZiMultiplex] [src/ZiIOBuf.hh:IOBuf] [src/ZiRxStream.hh:RxStream] [src/ZiTxStream.hh:TxStream]

## Invariants

- Rx/Tx work follows owner-thread contracts; buffers retain backing while borrowing allocator/owner lifetimes. Formatting and some callbacks are synchronous. Stop/drain completes before scheduler, connection and callback destinations are destroyed. [src/ZiMultiplex.hh:ZiConnection] [src/ZiIOBuf.hh:IOBuf] [src/ZiTxStream.hh:append] [src/ZiMultiplex.cc:stop__] [src/ZiResolver.cc:final_]

## Side Effects

- Opens sockets/files/shared memory, schedules workers and callbacks, resolves DNS, logs and rotates files, changes process configuration, and registers telemetry watchers. Cleanup drains callbacks and releases owned OS resources according to each API. [src/ZiMultiplex.cc:udp_] [src/ZiFile.cc:open_] [src/ZiRing.cc:CtrlMem] [src/ZiResolver.cc:openChannel_] [src/ZiLog.cc:stop_] [src/ZiDaemon.cc:init]

## Verification

From the configured repository root, run `make -j8 && make -C zi test`. Commands were not executed for this documentation change. [Makefile.am:test]
