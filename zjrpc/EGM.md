# Zjrpc

## Overview

Typed JSON-RPC 2.0 codecs, bounded dispatch/correlation and stdio/HTTP/WebSocket endpoints, with arithmetic examples.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): envelopes, catalogs, completion/pending work and transport adapters.
- [Examples](./example/EGM.md): add/add_stream methods and client/server programs.

## Interface

The library exports libZjrpc.la and twelve headers. Application catalogs supply actual methods; example programs demonstrate one arithmetic call over selected transport. [Library manifest](./src/Makefile.am), [example catalog](./example/zjrpcexample.hh).

## Invariants

Completion is single-claim, pending/inbound maps are owner-serialized, and shutdown drains posted work before release. Borrowed input remains alive through asynchronous output. [ZjrpcCompletion.hh](./src/ZjrpcCompletion.hh), [ZjrpcPending.hh](./src/ZjrpcPending.hh), [ZjrpcDispatch.hh](./src/ZjrpcDispatch.hh).

## Side Effects

Endpoints allocate message/work storage, schedule callbacks and perform transport I/O. Stdio additionally duplicates handles and uses platform interruption facilities; examples open network endpoints or inherited streams and log results. See child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C zjrpc/test test`. Module `make test` also invokes integration tests. Test-only directories are excluded from this index. No builds/tests/examples were run for this documentation change.
