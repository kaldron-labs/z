# Zws

## Overview

WebSocket framing and HTTP transport adaptation, with command-line client/server programs and a loopback benchmark.

## Subdirectories & Boundaries

- [Library and programs](./src/EGM.md): `Codec`, `Client`, `Server`, protocol/URI/handshake helpers, `zws`, and `zwsd`.
- [Benchmark](./bench/EGM.md): `zwsbench` measures loopback round-trip latency and framework heap/cache deltas.

## Interface

The library supports HTTP/1 upgrade and HTTP/2–3 Extended CONNECT through transport profiles; installed programs select HTTP/1 TCP/TLS. [ZwsHub.hh](./src/ZwsHub.hh), [zws.cc](./src/zws.cc), [zwsd.cc](./src/zwsd.cc).

## Invariants

`Codec` bounds aggregate input/message bytes and enforces role-specific masking, control-frame and continuation rules. Channel teardown retains the link through shard cleanup before release (`LinkState::down_`). [ZwsCodec.hh](./src/ZwsCodec.hh), [ZwsHub.hh](./src/ZwsHub.hh).

## Side Effects

Library adapters schedule transport and timer work, mutate masked payloads and deliver callbacks. Programs open network endpoints and write output/logs; the benchmark additionally uses temporary-environment helpers and records samples. See child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C zws/test test`. Module `make test` also runs interoperability tests. Tests and programs were not run for this documentation change; test-only directories are excluded from the index.
