# Zmcp typed tool protocol

## Overview

Typed MCP tool catalogs, schema/result generation and client/server lifecycle over the repository JSON-RPC transports. Source declares ModernVersion 2026-07-28 and LegacyVersion 2025-11-25, with modern discovery and legacy initialization paths.

## Interface

- [Zmcp.hh](./Zmcp.hh): Request/Response metadata, tool annotations, Contract validation, JSON Schema generation, typed arguments/replies, Context, Completion and Peer. Tool results contain structured status/data, empty content and isError; modern results add resultType and server metadata.
- [ZmcpLib.cc](./ZmcpLib.cc), serverMethod: server/discover, initialize, notifications/initialized, ping, tools/list, tools/call, notifications/cancelled, notifications/progress, notifications/message and logging/setLevel. Client notification routing recognizes progress/message.
- [ZmcpClient.hh](./ZmcpClient.hh): common ClientPeer negotiation/catalog cache, typed calls/batches/control operations and HTTPClient, IOClient and WSClient adapters. HTTP ClientConfig defaults endpoint to /mcp and enables legacy sessions.
- [ZmcpServer.hh](./ZmcpServer.hh): shared Server, context/session/route ownership, HTTP fixed/SSE responses, IOServer, HTTPServer and WSServer. ServerConfig defaults /mcp, legacy sessions enabled, zero fixed lifetime and absent-Origin rejection.
- [Makefile.am](./Makefile.am): exports libZmcp.la and four headers, compiling ZmcpLib.cc and linking JSON-RPC, WS, HTTP, QUIC, TLS/TCP and foundation libraries.

## Invariants

- Contract requires unique valid tool IDs, structured request objects, nonempty response lists with unique statuses 100–599, structured or void bodies and Fixed/SSE response policy. JSON Schema derives from active JSON metadata; reflected object schemas reject additional properties. [Zmcp.hh](./Zmcp.hh)
- Context pointers are borrowed for one handler dispatch. Completion progress requires a live token and progress ID; logging requires a live token and enabled threshold. Off-owner actions retain token/copied strings and use bounded posted admission. [Zmcp.hh](./Zmcp.hh)
- Peer rejects null request IDs and closed dispatch; legacy tools require Ready after initialization notification. Discovery selects modern mode. Typed tool calls validate/narrow arguments and declared responses; unknown tools produce status 404. ClientPeer falls back from unsupported discovery to legacy initialization and retains JSON backing for cached catalogs. [Zmcp.hh](./Zmcp.hh), [ZmcpClient.hh](./ZmcpClient.hh)
- Client mutations run on owner shards. HTTP pending registration precedes send; pre-ready and legacy requests serialize through a bounded queue. Legacy session IDs update queued requests before submission. HTTP terminate latches further admission; finalization requires stopped lifecycle and drained work. [ZmcpClient.hh](./ZmcpClient.hh)
- HTTP routes/session lifecycle is Rx-owned and response/SSE work Tx-owned; WS RPC state is Tx-owned. HTTP Origin acceptance follows configuration/application callback. Legacy session count and route admission are bounded; supplied session IDs must resolve when legacy sessions are enabled. [ZmcpServer.hh](./ZmcpServer.hh)
- Session-aware detached streams may preserve work with stable inbound-call storage; sessionless/closing-session work is cancelled. Stop waits for dispatcher, sessions, routes, SSE disposal and transport contexts; WS stop waits for transport plus per-link drains and shard barriers. [ZmcpServer.hh](./ZmcpServer.hh)

## Side Effects

- Schema/list/result helpers serialize caller output; argument/reply decoding can allocate typed objects. Parameter-header rendering uses temporary scratch-backed views and base64 wrapping where needed. Peer dispatch changes negotiation/logging state and invokes application tools. [Zmcp.hh](./Zmcp.hh), [ZmcpClient.hh](./ZmcpClient.hh)
- HTTP clients submit discovery/initialization/tool/control requests, parse JSON/SSE, retain catalog input and capture legacy sessions. Expired legacy sessions can reinitialize on 404/410. Cancellation removes queued calls or emits legacy cancellation before transport cancellation; termination can send DELETE. [ZmcpClient.hh](./ZmcpClient.hh)
- HTTP servers allocate random session IDs, open application transport/session/stream contexts and optional lifetime timers, replace attached session streams, and schedule response production. DELETE sends acceptance without waiting for session teardown; absent/unknown IDs are also accepted. Origin rejection sends empty 403. [ZmcpServer.hh](./ZmcpServer.hh)
- Context owners close application references; callback exceptions are contained in teardown paths. SSE disposal and session/route cancellation run in bounded batches. Stdio/WS wrappers start underlying transports, notify applications, and drain peer/call state before release. [ZmcpServer.hh](./ZmcpServer.hh), [ZmcpClient.hh](./ZmcpClient.hh)

## Verification

From a configured repository root, run `make -j8 && make -C zmcp/test test`. Module `make test` also runs integration tests. No builds, tests or transports were executed for this documentation change.
