# Zi networking and logging examples

## Overview

Standalone TCP/UDP client/server workloads and a logging demonstration. These examples exercise multiplexer, buffer, framing, and completion APIs.

## Interface

- `zimxclient IP PORT` and `zimxserver IP PORT` exercise fixed HTTP/1.0-style traffic with connection, retry, buffer, and epoll controls; client adds concurrency and server adds accept-queue configuration. [zimxclient.cc:usage] [zimxserver.cc:usage]
- UDP programs accept endpoint, message-count, buffer, multicast, and debug controls. Client defaults to an ephemeral local endpoint and destination 127.0.0.1:27413; server defaults to that bind address and one completed send. [zimxudpclient.cc:main] [zimxudpserver.cc:main]
- `zilogtest` selects file logging by default, system logging with `-s`, or CSV with `-c`. All five programs and their shared helper headers are non-installed. [zilogtest.cc:main] [Makefile.am:noinst_PROGRAMS] [Makefile.am:noinst_HEADERS]

## Invariants

- TCP/UDP `-t` modifies a local scheduler-parameter object that is not passed to the multiplexer. TCP `-d` thresholds are stored but unused by the inspected send/receive paths. These flags do not establish the advertised behavior. [zimxclient.cc:main] [zimxserver.cc:main] [zimxudpclient.cc:main] [zimxudpserver.cc:main]
- TCP client concurrency must not exceed target connection count for its unsigned remaining-count calculation. Receive framing caps input at 100 MiB; header scanning retains overlap across arrivals. [zimxclient.cc:Mx::disconnected] [zimxclient.cc:Connection::frame] [httpheader.hh:httpHeaderEnd]
- Connection output borrows its connection and multiplexer-owned allocators, transferring buffer references to ZiTx. Owners must survive completion. Server TCP byte counts are initialized before potentially synchronous output. [zimxclient.cc:Connection::Output] [zimxserver.cc:Connection::sendHeader] [zimxudpclient.cc:Connection::Output]
- UDP endpoint parsing accepts HOST:PORT, :PORT, and bracketed IPv6 forms, but not a bare port. Client replies are checked for length rather than payload equality. Neither UDP example implements a receive timeout. [zimxudpclient.cc:parseEndpoint] [zimxudpserver.cc:parseEndpoint] [zimxudpclient.cc:Connection::recvComplete]
- UDP server copies received datagrams to transmit storage, replenishes receive backing, and preserves sender/TOS unless an explicit destination overrides the peer. Main paths wait for completion, stop the multiplexer, then stop logging. [zimxudpserver.cc:Connection::recvComplete] [zimxudpserver.cc:main]

## Side Effects

- TCP client sends a fixed GET; server returns generated HTTP 200 content and closes. These programs open network endpoints, schedule retries, print timing/byte counters, and install signal handlers. [zimxclient.cc:Connection::connected] [zimxserver.cc:Connection::sendHeader] [zimxclient.cc:Mx::failed] [zimxserver.cc:main]
- UDP programs bind/send datagrams, optionally configure multicast, print payloads/hexdumps, start logging/worker threads, and install SIGINT handling. [zimxudpclient.cc:Mx::udp] [zimxudpserver.cc:Mx::udp] [zimxudpclient.cc:Connection::recvComplete] [zimxudpserver.cc:main]
- Logging example emits a backtrace and multiple severity/platform-error records through file/CSV/system sinks; its Fatal record is not an explicit exit operation. [zilogtest.cc:main]

## Verification

From the configured repository root, run `make -j8 && make -C zi/test test` for the separate unit suite. The examples build through the default target and perform network or logging effects when run. They were not executed for this documentation change.
