# Ztcp

## Overview

TCP transport and ping/pong examples built on the framework multiplexer. The library provides CRTP client/server links, allocator-backed streams, telemetry, and asynchronous connection teardown.

## Subdirectories & Boundaries

- [Transport library](./src/EGM.md): `Ztcp::Hub`, `Client`, `Server`, and link templates.
- [Examples](./example/EGM.md): `ztcpclient` and `ztcpserver` demonstrate explicit scheduler, allocator, and endpoint setup.

## Interface

The library exports `libZtcp.la` and public headers; the examples are non-installed programs. See the child contracts for the public API and command-line behavior.

## Invariants

`Hub::validate_` requires valid distinct Rx/Tx threads and matching TCP allocators; `Hub::final` requires a stopped engine. Submitted connection operations must finish while their hub and multiplexer remain alive (`CliLink::stopConnect_`). See [Ztcp.hh](./src/Ztcp.hh).

## Side Effects

`CliLink::connect_` resolves and connects endpoints; `Server::listen` opens listeners; link send/receive operations schedule work across application and I/O threads. The examples print events and exchange ping/pong frames. See the child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C ztcp/test test`. Tests were not run for this documentation change. Test-only directories are outside this contract index.
