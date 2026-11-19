# Zxdp

## Overview

Linux AF_XDP raw packet service and packet validation utilities. Application protocol adaptation and scheduling remain outside the library; its boundary covers arenas, packet allocation/return, kernel rings, receive/wake, and retirement.

## Subdirectories & Boundaries

- [Packet library](./src/EGM.md): abstract `Zxdp::Instance`, tagged `Params`, and
  `Zi::IOBufPool` references from the selected XDP instance.
- [Packet utility and fixture](./util/EGM.md): direct packet exchange plus isolated namespace or physical-interface execution.

## Interface

[Makefile.am](./Makefile.am) traverses library, tests, utilities, and integration tests. The public library boundary is defined by [INTERFACE.md](./INTERFACE.md) and [GUIDELINES.md](./GUIDELINES.md); source contracts describe its concrete API.

## Invariants

`Instance` uses separate single Rx/Tx owners. Caller allocation and kernel submission leases are independent, and backing is recycled only after both end. Quiesce service before stop, and return dependent backing before finalization. See [Zxdp.hh](./src/Zxdp.hh).

## Side Effects

`Instance::init` opens the link and wake descriptors. `alloc` provisions one shared
registration, and start activates per-run packet sockets and the classifier.
Packet service submits and receives raw frames. Utility fixture mode creates namespace-local links and
writes run artifacts. See the child contracts for resource and teardown details.

## Verification

From a configured repository root, run `make -j8 && make -C zxdp/test test`. Privileged integration/physical tests require their own environment.
