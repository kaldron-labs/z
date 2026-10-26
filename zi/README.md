# Zi: I/O, buffers and process services

Zi integrates files, sockets, DNS, event loops, shared-memory rings and logging
with Z's scheduler and allocation model. Include individual headers such as
`<zlib/ZiMultiplex.hh>` and link `libZi` with its dependencies. Application TCP,
TLS, HTTP and WebSocket protocols live in their own modules; serialization
lives in [Zf](../zf/README.md), [Zfb](../zfb/README.md) and [Zpb](../zpb/README.md).

## Files and filesystem utilities

`ZiFile` owns a file handle and supplies open/read/write, mapping and filesystem
operations. Check `Zi::OK` and inspect `file.error()` on failure:

```cpp
#include <zlib/ZiFile.hh>

bool writeFile(const Zi::Path &path, ZuCSpan bytes) {
  ZiFile file;
  if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) return false;
  return file.write(bytes.data(), bytes.length()) == Zi::OK;
}
```

Choose open flags deliberately: the example's `GC` mode creates/truncates the
output. `ZiFileTxStream` adapts a file to Z stream output; check `flush()` for
write failures. `ZiDir`, `ZiGlob` and `ZiStat` cover directory traversal,
patterns and metadata. `ZiCSV` adds file-based CSV reading/writing over reflected
records; in-memory CSV formatting is in Zf.

## Socket multiplexing and event loops

`ZiMultiplex` combines scheduler destinations with Linux epoll or Windows IOCP.
Configure `ZiMxParams`, including its nested scheduler and `rxThread`/`txThread`,
before `start()`. Connections expose receive/transmit callbacks and lifecycle
notifications; use the higher-level [Ztcp guide](../ztcp/README.md) for a complete
client/server application rather than reimplementing link management.

Keep receive state on Rx and transmit state on Tx. Public `rxRun`/`rxInvoke`
and `txRun`/`txInvoke` hand work to the owner; internal underscore variants
assume the caller is already there. Stop ingress and drain outstanding work
before destroying owners or stopping their scheduler.

`ZiEventLoop` is a separate adapter for external handles/sockets, such as libpq
connections. It uses epoll on Linux and a Windows wait-based backend, rather
than the socket-only IOCP path. Follow its asynchronous start/stop result
callbacks. [ZiEventLoopTest](test/ZiEventLoopTest.cc) and
[ZiMxLoopTest](test/ZiMxLoopTest.cc) exercise the two interfaces.

## Buffers and streams

`ZiIOBuf` provides pooled, reference-counted I/O storage. Configure the pool for
your message sizes and keep buffers alive through asynchronous output.
`ZiRx`/`ZiTx` and `ZiRxStream`/`ZiTxStream` manage receive/transmit flow; stream
parsers must handle fragmented input, while writers should flush at deliberate
message boundaries. A span into a buffer is only a view, not an ownership token.

Use `ZiIOContext` for low-level operation context and callback integration.
Transport-specific buffer allocators in Ztcp/Ztls reuse this infrastructure.
Avoid copying whole payloads into queued lambdas; pass buffer references and
small fixed metadata instead.

## Addresses and asynchronous DNS

`ZiIP` represents socket addresses and supports textual conversion.
`ZiResolver` integrates c-ares with the event loop and scheduler. Its parameters
select address families, DNS servers, timeout and retry policy; preserve query
and callback lifetimes through completion/cancellation. See
[ZiResolverTest](test/ZiResolverTest.cc) for isolated resolver setup and callbacks,
and [ZiIPTest](test/ZiIPTest.cc) for address handling. A particular higher-level
protocol may expose a narrower address/discovery profile than the resolver.

## Resolver lifecycle

`ZiResolver` uses `ZmEngine` to control the complete DNS runtime. Configure it
with `init(params, failFn)` while stopped. Asynchronous `start(fn)` and `stop(fn)`
complete with a boolean; startup diagnostics and runtime failures go to the
configured `ZiEvent::FailFn`. Query results keep their existing typed errors.
Repeated operations and pending opposite operations follow the engine contract:
a start requested during shutdown runs after that stop completes.

Startup completes after the scheduler and event loop are ready. Stop rejects
new query admission, cancels timers and outstanding c-ares queries, drains the
resolver shard and event loop, and releases its scheduler before completing.
Configuration survives stop; another start creates a fresh channel and scheduler
without another init. `initialized()` reports retained configuration, whereas
`state()` reports engine state. `running()` has the canonical engine meaning,
including Starting; wait for start completion before depending on readiness.

Call `final()` only after stop completion, or directly after init without start
or a failed start whose rollback completed. It releases quiescent resources and
never initiates shutdown or waits for a control thread. These preconditions are
always-fatal assertions. A stop completion callback may call final. The main
thread can use `startSync()` and `stopSync()` for boolean, blocking completion;
never use them from resolver, I/O, or worker callbacks.

```cpp
ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
bool started = ZiResolver::startSync();
if (started) {
  // Submit asynchronous queries, or use ZiIP's main-thread blocking adapters.
  ZiResolver::stopSync();
}
ZiResolver::final();
```

Queries retain lazy startup when stopped. Queries submitted during an engine
stop transition are rejected; explicit start requests use the pending-operation
mechanism. Numeric address resolution can complete synchronously without
starting the runtime. Applications using lazy startup must still complete stop
before finalization or program teardown.

`test/ZiResolverTest` covers lifecycle idempotence and reentrancy, restart with
retained DNS-server settings, pending startup during a blocked shutdown, query
cancellation, and callback-driven finalization. Build and run it through the
module's existing `make test` target after dependent libraries are refreshed.

## Shared-memory rings

`ZiRing` layers named, inter-process storage on `ZmRing`. It supports one or
multiple writers and broadcast readers, with fixed- or variable-size messages.
Linux uses shared-memory files and Windows uses named mappings. Processes must
agree on name, layout, size and record protocol; application pointers are not
portable shared-memory payloads.

Attach/create with the parameters in [ZiRing.hh](src/ZiRing.hh), publish complete
records, and retire reads according to the underlying ring protocol. Decide
which process owns unlink/removal and coordinate shutdown. Ztc is an application
of this facility for telemetry publication.

## Logging

`ZiLog` is the asynchronous logging runtime for Ze events. Initialize it before
producers, register sinks, start, and stop it after producers have drained:

```cpp
#include <zlib/ZiLog.hh>

void logExample() {
  ZiLog::init("example");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZiLOG(Info, "example", "started");
  ZiLog::stop();
}
```

`&2` selects stderr. Other sinks include files, system logging, CSV and lambda
callbacks; multiple sinks can receive the same event. Use `ZiLog::level()` to
select filtering and `ZiLogEvent()` to forward a `ZeException`. Formatters that
run later must retain owned captures, including a snapshot of the OS error code.

## Resource tuning

`ZiHeapTune` loads/saves heap configuration and collects measurements;
`ZiHashTune` handles hash configuration. Give components stable IDs, collect a
representative workload, then apply reserves/bucket settings at startup. Heap
sizes are block counts per partition/size class, not byte limits; see the
[Zm guide](../zm/README.md#heap-tuning). [Heap-tuning integration tests](itest/README.md)
describe the command-line workflow and coverage.

## Process and module utilities

`ZiProgram` exposes program/process information, `ZiDaemon` supports daemon
setup, `ZiPIDFile` handles PID files, and `ZiModule` loads shared libraries.
These have platform-specific behavior; consult their headers before choosing
ownership and cleanup policy. `ZtcMx`, `ZtcLink`, `ZtcPool` and `ZtcHub` expose
I/O telemetry vocabulary consumed by the telemetry layer.

## Examples and tests

[example/](example/README.md) contains complete logging/network programs.
Run `make -C zi test` from a configured build tree; [test/README.md](test/README.md)
and [itest/README.md](itest/README.md) describe focused fixtures and residue.
See [TESTING.md](../TESTING.md) for environment, logs and ports.
