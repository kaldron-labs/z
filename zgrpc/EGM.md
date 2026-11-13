# Zgrpc

## Overview

Typed gRPC over the framework HTTP/protobuf stack, with bounded framing and metadata, invalidatable call handles, and asynchronous client/server teardown.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): method catalogs, protobuf envelopes, metadata, call handles, HTTP adapters, and client/server endpoints.

## Interface

`libZgrpc.la` exports `Method`, `CallToken`, `Client`, `Server`, framing and metadata facilities. Application-defined catalogs and handlers provide actual RPC service methods. See [source contract](./src/EGM.md) and [build manifest](./src/Makefile.am).

## Invariants

`CatalogValid` enforces unique valid method paths. `CallCore` separates Rx/Tx ownership; endpoint storage survives posted work and transport retirement. `EndpointCalls::final` requires successful shutdown and drained work. See [Zgrpc.hh](./src/Zgrpc.hh), [ZgrpcCall.hh](./src/ZgrpcCall.hh), and [ZgrpcEndpoint.cc](./src/ZgrpcEndpoint.cc).

## Side Effects

Client/server work submits HTTP transport operations, schedules deadlines and shard continuations, invokes application observers/handlers, and drains cancellation during stop. See [Client](./src/ZgrpcClient.hh) and [Server](./src/ZgrpcServer.hh).

## Verification

From a configured repository root, run `make -j8 && make -C zgrpc/test test`. Module `make test` also invokes integration tests. Test-only and interoperability directories are excluded from this contract index. No tests were run for this documentation change.
