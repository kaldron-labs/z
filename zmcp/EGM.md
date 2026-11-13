# Zmcp

## Overview

Typed MCP tool schemas and protocol lifecycle over JSON-RPC HTTP, WebSocket and stdio transports, with arithmetic client/server examples.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): tool catalogs/results, discovery/initialization, clients, contexts, sessions and servers.
- [Examples](./example/EGM.md): add/add_stream tools with optional network Bearer authentication.

## Interface

Exports libZmcp.la and four headers. Zmcp.hh declares the version constants and typed protocol surface; application catalogs supply concrete tools. [Build manifest](./src/Makefile.am), [protocol declarations](./src/Zmcp.hh).

## Invariants

Application Context is dispatch-scoped; completion and pending work use owner-shard routing. HTTP and WS teardown drain routes, sessions and posted work before finalization. Tool catalog metadata determines valid response types and schema output. [Zmcp.hh](./src/Zmcp.hh), [ZmcpServer.hh](./src/ZmcpServer.hh).

## Side Effects

Clients negotiate protocol state and perform tool/control requests. Servers manage application contexts and optional legacy sessions/timers, invoke tools and stream replies. Examples open selected transports and log one arithmetic result. See child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C zmcp/test test`. Module `make test` also runs integration tests. Test/interoperability sources are excluded from this index. No builds/tests/examples were run for this documentation change.
