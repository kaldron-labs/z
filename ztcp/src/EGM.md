# Ztcp transport library

## Overview

Header-defined CRTP TCP client/server transport over `ZiMultiplex`, with application Rx/Tx dispatch, allocator-backed streams, telemetry, reconnect/rebind hooks, and ordered connection teardown.

## Interface

- [Ztcp.hh](./Ztcp.hh): `Ztcp::HubParams`, `ClientParams`, and `ServerParams` configure the multiplexer, threads, callbacks, and TCP allocators. `RxBufAlloc` and `TxBufAlloc` provide allocator aliases.
- [Ztcp.hh](./Ztcp.hh): `Link`, `CliLink`, and `SrvLink` expose connection access, sending, transmit streams, telemetry, and disconnect. `Hub`, `Client`, and `Server` supply lifecycle and scheduling; server applications provide `localIP`, `localPort`, `accepted`, and `Link`.
- [ZtcpLib.hh](./ZtcpLib.hh): repeatable `Ztcp::lib_init()`; its implementation is empty. [Makefile.am](./Makefile.am) exports `libZtcp.la` and installs the two public headers.

## Invariants

- `Hub::validate_` requires a multiplexer, valid distinct Rx/Tx thread IDs, and allocators of types `RxTCP` and `TxTCP`; `Hub::init_` requires both allocators to belong to that multiplexer. `Hub::final` requires a stopped engine. [Ztcp.hh](./Ztcp.hh)
- `Link` allocator buffers must derive from their respective queue node types. `txStream_` and `send_` require the application Tx thread; `txStream` must run outside it. `disconnect_` and client `connect_` require the application Rx thread. [Ztcp.hh](./Ztcp.hh)
- Configure `Link::m_txErrorFn` before link use and keep it stable. The hub and multiplexer must remain alive while submitted DNS/socket operations drain through completion (`CliLink::stopConnect_`). [Ztcp.hh](./Ztcp.hh)
- `Link::rcvd_` interprets application `process` results as zero to pause, negative to disconnect, and positive to continue while data remains. [Ztcp.hh](./Ztcp.hh)

## Side Effects

- `Hub::init_`/`final` register/unregister telemetry management; link construction/destruction changes hub membership and state counts. `defaultErrorFn` logs exceptions. [Ztcp.hh](./Ztcp.hh)
- `Link::recvRaw_` allocates receive buffers and posts application Rx work. `send` posts Tx work; `send_` schedules physical output and updates counters. Allocation failure or a full transmit queue can disconnect. `TxStream_` destruction flushes pending output. [Ztcp.hh](./Ztcp.hh)
- `CliLink::connect_` starts asynchronous host resolution and socket setup; `retryConnect_` schedules retries. `Server::listen` submits listener/accept callbacks; `stopListening` cancels rebind timing and stops listening. [Ztcp.hh](./Ztcp.hh)
- `Link::disconnected_0` chains application Rx, application Tx, and I/O Tx cleanup with connection-identity guards against stale continuations. `Server::stop_` drains listener closure and accepted connections before completing stop. [Ztcp.hh](./Ztcp.hh)

## Verification

From a configured repository root, run `make -j8 && make -C ztcp/test test`. The test Makefile runs `ZtcpInitTest`, `ZtcpLoopTest`, and `ZtcpStreamTest` through `prove`. Commands were not run for this documentation change.
