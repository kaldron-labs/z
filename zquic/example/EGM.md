# Zquic echo examples

## Overview

Non-installed QUIC client/server programs using explicit packet allocators and isolated I/O and protocol threads.

## Interface

- [zquicclient.cc](./zquicclient.cc): `zquicclient SERVER PORT [CA] [REQUEST]`, default request zquic-example; a numeric-only fourth argument is rejected.
- [zquicserver.cc](./zquicserver.cc): `zquicserver SERVER PORT CERT KEY [COUNT]`; port/count must be nonzero.
- [Makefile.am](./Makefile.am) builds both programs against QUIC, TLS and the framework.

## Invariants

Both programs configure zquic-echo ALPN and four isolated I/O Rx/Tx and QUIC Rx/Tx threads. The client waits for response FIN before success, then waits for disconnect completion before finalization. The server prepends "echo: " to each received frame, preserving its FIN; its count advances when the FIN send is accepted, not when delivery is acknowledged. Missing streams/send failures also advance the count. A single listen failure may leave a server with COUNT greater than one waiting. See [client](./zquicclient.cc) and [server](./zquicserver.cc).

## Side Effects

Programs open UDP endpoints, start workers, read configured TLS credentials/CA, and write diagnostics to stderr. The client writes response payload and a final newline to stdout. The server emits echo traffic until its completion count. See [client](./zquicclient.cc) and [server](./zquicserver.cc).

## Verification

From a configured repository root, run `make -j8 && make -C zquic test`. Manual examples require suitable endpoints and certificates. Commands were not run for this documentation change.
