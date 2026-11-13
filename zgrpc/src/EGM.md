# Zgrpc typed RPC transport

## Overview

Typed gRPC method catalogs, protobuf framing, metadata handling, bounded call admission, and client/server adapters over the repository HTTP transport. This package supplies transport machinery; application method catalogs and handlers supply services.

## Interface

- [Zgrpc.hh](./Zgrpc.hh): `Method` declares service/name, request/response types, streaming kind and metadata; catalog macros derive exact method-path matching. `Message<T>` retains values and optional borrowed backing; `Status`, `Reply`, `Metadata`, and `Limits` describe results and bounds.
- [ZgrpcClient.hh](./ZgrpcClient.hh), `Client`: lifecycle/pool configuration, typed `open`, and unary `call`. [ZgrpcServer.hh](./ZgrpcServer.hh), `Server`: lifecycle and typed admission; applications provide `request` for unary input or `open` returning a streaming observer. Optional hooks handle headers, signing, sensitivity and lifecycle.
- [ZgrpcCall.hh](./ZgrpcCall.hh), `CallToken`: client send/end, server finish, streaming send or non-streaming complete/reply, and cancellation, constrained by role and method kind. [ZgrpcEndpoint.hh](./ZgrpcEndpoint.hh), `CallOptions`: metadata and nanosecond timeout, with negative duration disabling deadlines.
- [ZgrpcFrame.hh](./ZgrpcFrame.hh): `FrameRx`, `decodeFrame`, and `encodeFrame` handle envelopes and verified protobuf. [ZgrpcMeta.hh](./ZgrpcMeta.hh): `MetaRx` and timeout/text/binary codecs handle protocol and application metadata. [ZgrpcHTTP.hh](./ZgrpcHTTP.hh) and [ZgrpcMulti.hh](./ZgrpcMulti.hh) expose single-method and catalog HTTP builders/parsers.
- [Makefile.am](./Makefile.am): exports `libZgrpc.la`, eleven public headers and `Zgrpc.pc`; depends on Zpb, Zhttp, Zquic, Ztls, Ztcp and lower framework libraries.

## Invariants

- `MethodValid`/`CatalogValid` validate protobuf names/types, supported method kinds, metadata declarations, non-size-prefixed facets, and unique `/service/method` paths. `valid(Limits)` requires nonzero bounds. Public metadata insertion rejects reserved HTTP and `grpc-*` names. [Zgrpc.hh](./Zgrpc.hh)
- `Direction::end` requires exactly one message for successful non-streamed completion. `BodyBuilder` bounds size/count/queued bytes and preserves the first production error. `sendRaw_` trusts protobuf payloads; transferred buffers must be mutable unpublished storage with prefix/head/tail room. [ZgrpcCall.cc](./ZgrpcCall.cc), [ZgrpcHTTP.hh](./ZgrpcHTTP.hh)
- `CallCore` separates Rx/Tx state and immutable route snapshots; owners are dereferenced on their destination shard. Off-shard token success means ingress admission. Endpoint registry and terminal selection are Rx-owned; retained tokens do not retain endpoints. [ZgrpcCall.hh](./ZgrpcCall.hh), [ZgrpcEndpoint.hh](./ZgrpcEndpoint.hh)
- `FrameRx::feed` rejects compression, unknown flags, and oversize payloads before assembly allocation; `end` rejects partial frames. Borrowed payloads require immutable retained backing; exclusive fragmented assembly requires no other reader. [ZgrpcFrame.hh](./ZgrpcFrame.hh), [ZgrpcFrame.cc](./ZgrpcFrame.cc)
- `RxInput` and continuations retain input independently; terminal callback owners must break cycles. `TxBody` emitter/writer belong to Tx. Endpoint shutdown waits for transport stop, reservations, stop posts and registered work to drain; `final` requires successful stop. Client/server work retirement also drains already-posted timer callbacks. [ZgrpcIO.hh](./ZgrpcIO.hh), [ZgrpcEndpoint.cc](./ZgrpcEndpoint.cc), [ZgrpcClient.hh](./ZgrpcClient.hh), [ZgrpcServer.hh](./ZgrpcServer.hh)
- `MetaRx::initial` accepts supported gRPC protobuf content types and requires request TE trailers; response `end` requires status after accepted initial metadata. Status arrival alone does not certify completion. `RxInput::finish_` checks HTTP completion, frame completeness, metadata, then cardinality. [ZgrpcMeta.cc](./ZgrpcMeta.cc), [ZgrpcIO.cc](./ZgrpcIO.cc)

## Side Effects

- `Client::init` and `Server::init` force preferred H2 policy to H2, remove the total body cap, and bound retained bytes by gRPC queue limits; client configuration disables retries and redirects. [ZgrpcClient.hh](./ZgrpcClient.hh), [ZgrpcServer.hh](./ZgrpcServer.hh)
- Client admission posts Rx work, submits HTTP requests, arms deadlines and drains Tx messages. Server admission dispatches application handlers/observers, publishes response routes and arms deadlines; expired/refused calls bypass handlers, and handler exceptions become INTERNAL. [ZgrpcClient.hh](./ZgrpcClient.hh), [ZgrpcServer.hh](./ZgrpcServer.hh)
- `FrameRx::feed` retains contiguous backing or assembles copied fragments. `RxInput::pump_` bounds each turn by bytes and messages. `BodyBuilder::drain_` writes bounded batches and invokes signing/producer hooks. [ZgrpcFrame.cc](./ZgrpcFrame.cc), [ZgrpcIO.cc](./ZgrpcIO.cc), [ZgrpcHTTP.hh](./ZgrpcHTTP.hh)
- `EndpointCalls::stop` closes admission, posts bounded cancellation work and stops transport. `RxInput::fail` disconnects; terminal processing releases chunks/channel before callback. Client completion notifies and releases observers, swallowing terminal observer exceptions. [ZgrpcEndpoint.cc](./ZgrpcEndpoint.cc), [ZgrpcIO.cc](./ZgrpcIO.cc), [ZgrpcClient.hh](./ZgrpcClient.hh)

## Verification

From a configured repository root, run `make -j8 && make -C zgrpc/test test`. Module `make test` additionally includes integration tests. No builds or tests were run for this documentation change.
