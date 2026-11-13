# Zjrpc arithmetic examples

## Overview

Matching JSON-RPC client/server examples over inherited stdio, HTTP or WebSocket transports.

## Interface

- [zjrpcexample.hh](./zjrpcexample.hh), `ExampleCatalog`: `add` and `add_stream` take required signed 64-bit `lhs`/`rhs` and return required `value`. `AddStream` selects SSE response-body policy but completes one arithmetic result.
- [zjrpc.cc](./zjrpc.cc), `Options`: client accepts `--stdio`, `--ws`, `--host` (127.0.0.1), `--port`/`-p` (8080), `--lhs` (20), `--rhs` (22), `--stream`, and help.
- [zjrpcd.cc](./zjrpcd.cc), `Options`: server accepts stdio/WS selection, port and help. HTTP/WS bind loopback; HTTP uses `/rpc`, while WS accepts `/rpc` with offered subprotocol `jrpc`.
- [Makefile.am](./Makefile.am), `noinst_PROGRAMS`: builds non-installed `zjrpc` and `zjrpcd`.

## Invariants

- Both `main` functions reject positional arguments, simultaneous stdio/WS selection and ports outside 1–65535; client additionally rejects an empty host. [zjrpc.cc](./zjrpc.cc), [zjrpcd.cc](./zjrpcd.cc)
- `App::request` completes only when a token exists, returning `lhs + rhs`; handlers contain no explicit signed-overflow validation. [zjrpc.cc](./zjrpc.cc), [zjrpcd.cc](./zjrpcd.cc)
- Client `run` sends one call after readiness and succeeds only when call and stop succeed. After successful server startup, its exit result reflects `server.stop()`; runtime failure callbacks wake its wait without retaining a separate failure result. [zjrpc.cc](./zjrpc.cc), [zjrpcd.cc](./zjrpcd.cc)

## Side Effects

- Program setup creates four isolated scheduler threads and stderr logging. Client `run` opens the selected transport, sends one add call and logs the value; server `run` waits for SIGINT, closure or failure before stop/finalization. [zjrpc.cc](./zjrpc.cc), [zjrpcd.cc](./zjrpcd.cc)
- HTTP is nonsecure H1TCP; WS connects to `ws://host:port/rpc`; stdio uses dedicated named Rx/Tx threads and inherited streams. [zjrpc.cc](./zjrpc.cc), `main`.

## Verification

From a configured repository root, run `make -j8 && make -C zjrpc/test test`. Default build includes these examples; neither programs nor tests were executed for this documentation change.
