# Ztcp ping/pong examples

## Overview

TCP client/server examples using the Ztcp transport library and explicit scheduler and allocator setup.

## Interface

- [ztcpclient.cc](./ztcpclient.cc), `main`: `ztcpclient SERVER PORT [REPEAT]`, with a default repeat count of one.
- [ztcpserver.cc](./ztcpserver.cc), `main`: accepts `SERVER PORT` and an optional third argument. Usage advertises `[REPEAT]`, but the server does not read that argument.
- [Makefile.am](./Makefile.am), `noinst_PROGRAMS`: builds the non-installed `ztcpclient` and `ztcpserver` executables against `libZtcp` and framework dependencies.

## Invariants

- Client `main` rejects zero port or repeat count; server `main` rejects a zero port. Both require two or three positional arguments. [ztcpclient.cc](./ztcpclient.cc), [ztcpserver.cc](./ztcpserver.cc)
- Client `App::Link::process` accepts a six-byte `pong\r\n` frame and disconnects at the configured repeat count. Server `App::Link::process` responds to a six-byte `ping\r\n` frame. [ztcpclient.cc](./ztcpclient.cc), [ztcpserver.cc](./ztcpserver.cc)
- Both programs initialize the application with multiplexer-owned allocators before starting the multiplexer. Client `main` retains its link while awaiting completion. Server `main` waits on `done`; its only explicit post is in `listenFailed`, so accepted-client disconnection does not terminate the server. [ztcpclient.cc](./ztcpclient.cc), [ztcpserver.cc](./ztcpserver.cc)

## Side Effects

- Each `main` starts four isolated scheduler threads, assigns Rx/Tx to threads 1/2, configures stderr logging, and connects or listens on the supplied endpoint. [ztcpclient.cc](./ztcpclient.cc), [ztcpserver.cc](./ztcpserver.cc)
- Client `sendPing` writes and flushes ping frames; server `process` writes and flushes pong frames. Callbacks print connection/listener events; `usage` prints help and exits with status one. [ztcpclient.cc](./ztcpclient.cc), [ztcpserver.cc](./ztcpserver.cc)

## Verification

From a configured repository root, run `make -j8 && make -C ztcp/test test` for the module test suite. The examples are built by the default build; they were not executed for this documentation change.
