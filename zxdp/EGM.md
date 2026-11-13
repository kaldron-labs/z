# Zxdp

## Overview

Linux AF_XDP raw packet service and packet validation utilities. Application protocol adaptation and scheduling remain outside the library; its boundary covers arenas, packet allocation/return, kernel rings, receive/wake, and retirement.

## Subdirectories & Boundaries

- [Packet library](./src/EGM.md): `Zxdp::Instance`, `Params`, and caller-owned `BufPool`.
- [Packet utility and fixture](./util/EGM.md): direct packet exchange plus isolated namespace or physical-interface execution.

## Interface

[Makefile.am](./Makefile.am) traverses library, tests, utilities, and integration tests. The public library boundary is defined by [INTERFACE.md](./INTERFACE.md) and [GUIDELINES.md](./GUIDELINES.md); source contracts describe its concrete API.

## Invariants

`Instance` uses separate single Rx/Tx owners. Caller allocation and kernel submission leases are independent, and backing is recycled only after both end. Quiesce service before close and retain pending retirement objects until completion. See [Zxdp.hh](./src/Zxdp.hh) and [ZxdpRetire.hh](./src/ZxdpRetire.hh).

## Side Effects

`InstanceData::init` provisions mapped backing, sockets, BPF programs/maps, and event descriptors; packet service submits and receives raw frames. Utility fixture mode creates namespace-local links and writes run artifacts. See the child contracts for resource and teardown details.

## Verification

From a configured repository root, run `make -j8 && make -C zxdp/test test`. Privileged integration/physical tests require their own environment. No tests or utilities were run for this documentation change; test-only directories are excluded from the contract index.
