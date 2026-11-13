# Zhttp

## Overview

Typed HTTP/1, HTTP/2 and HTTP/3 clients/servers over TCP, TLS and QUIC, with discovery, pooling, streaming and command-line utilities.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): request/response APIs, lifecycle, codecs, compression and native transports.
- [Utilities](./util/EGM.md): workload client, static-file/JSON-echo server and reusable server helper.
- [Benchmark](./bench/EGM.md): loopback waves, latency and heap observations.

## Interface

The module exports libZhttp and public facade/profile/configuration headers. Compile-time profiles select H1TCP, H1TLS, H2TLS and H3QUIC. Utilities and benchmark targets are non-installed. See [the source manifest](./src/Makefile.am).

## Invariants

Rx owns parsers/input and Tx owns builders/producers/output; borrowed callback data must not outlive its backing. Stop drains dependent work before finalization. Logical requests, wire attempts, submission completion and final retirement are distinct. Protocol-specific parsing behavior is documented in [the library contract](./src/EGM.md).

## Side Effects

Backends open sockets, resolve/discover endpoints, load transport credentials, schedule work and retain buffers. Utilities additionally read server roots, write client output/access logs, and can daemonize or emit supervision events. Benchmarks create loopback traffic and temporary certificate fixtures. See the child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C zhttp test`. [The module target](./Makefile.am) runs unit, integration and interoperability suites. Commands were not run for this documentation change.
