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

`ZiMultiplex` combines scheduler destinations with Linux epoll/io_uring or Windows IOCP.
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

## Common I/O object model

Zi provides the foundational I/O framework. Protocol implementations built on
it are required to adopt the following generic object model consistently,
including its client/server ownership and lifetime relationships. The names
`Hub`, `Link`, `Pool` and `Cxn` describe roles shared across protocols.

The framework supplies the common hub, link and pool interfaces as `Ztc::Hub`,
`Ztc::Link` and `Ztc::Pool`, declared in Zi's `ZtcHub.hh`, `ZtcLink.hh` and
`ZtcPool.hh`. Socket transport is provided by `ZiMultiplex` and `ZiConnection`;
protocol types implement the model on that foundation. The model follows
verbatim.

### Z I/O generic object model

- Client-side:
  - `Hub`:
    - owns `Link`s and `Pool`s
    - explicitly configured
    - can be started/stopped under app control
    - long living, typically the same lifetime as the process
  - `Link`:
    - explicitly instantiated by the app
    - used as an explicitly selected outbound route
    - specifies destination
    - long living, typically the same lifetime as the process
    - encapsulates application message processing
    - sometimes termed "line" or "session" in other frameworks
  - `Pool`:
    - load-balancing group of `Link`s
  - `Cxn`:
    - instantiated by the transport when the app starts the `Link`, causing a `connect`
    - owned by a `Link`
    - transient and can be short-lived
    - typically automatically re-instantiated via reconnection under `Link`/`Hub` control
    - encapsulates transport state
- Server-side:
  - `Hub`:
    - keeps track of `Link`s
    - explicitly configured
    - can be started/stopped under app control
    - long living, typically the same lifetime as the process
  - `Pool`: unused on server-side
  - `Link`:
    - instantiated on-demand when incoming connections are accepted
    - encapsulates application message processing
    - is constructed with origin, and often enriched with identity following auth
    - the same lifetime as the incoming connection
    - sometimes termed "line" or "session" in other frameworks
  - `Cxn`:
    - instantiated by the transport once the app calls `listen` and connections are accepted
    - owns the `Link`
    - transient and can be short-lived
    - encapsulates transport state

### Mapping the model to Zi and protocol types

| Model name | Framework type | Protocol implementation |
| --- | --- | --- |
| `Hub` | [`Ztc::Hub`](src/ZtcHub.hh) supplies common control, link/pool enumeration and telemetry. | A protocol hub implements lifecycle and ownership; for TCP, `Ztcp::Hub<App>` underlies `Ztcp::Client<App>` and `Ztcp::Server<App>`. |
| `Link` | [`Ztc::Link`](src/ZtcLink.hh) supplies common up/down control and telemetry. | A protocol link carries application processing and routing state; TCP uses `Ztcp::CliLink` and `Ztcp::SrvLink`. |
| `Pool` | [`Ztc::Pool`](src/ZtcPool.hh) supplies the common link-pool telemetry and queue enumeration interface. | A client protocol pool implements grouping and load balancing of outbound links. |
| `Cxn` | [`ZiConnection`](src/ZiMultiplex.hh) supplies socket transport state, receive/send operations and connection callbacks. | A protocol connection wrapper binds transport to a link; TCP uses `Ztcp::CliCxn` and `Ztcp::SrvCxn`. |

`ZiMultiplex` supplies the scheduler and socket I/O runtime used by protocol
hubs. A hub uses it to connect or listen; the multiplexer is a separate runtime
object from the model's `Hub`. `ZiCxnInfo` supplies connection metadata,
including local and remote addresses. At the socket API boundary, a
`ZiConnectFn` completion callback returns the concrete `ZiConnection` subclass
for an outgoing connection or an accepted incoming connection. This is where
the protocol constructs the model's transport connection and, on acceptance,
its server link.

The ownership distinction in the model is explicit in
[`Ztcp.hh`](../ztcp/src/Ztcp.hh): a client link holds a `ZmRef<Cxn>` and its
`Ztcp::CliCxn` holds a raw link pointer, allowing the link to survive replacement
connections. A server link holds a raw connection pointer and its
`Ztcp::SrvCxn` holds a `ZmRef<Link>`, tying the link to the accepted connection.
Other protocols must preserve these model relationships in their own types.
See the [Ztcp guide](../ztcp/README.md) for complete client/server examples.

The model's `Pool` groups links for outbound routing. The receive/transmit buffer
pools described below allocate I/O storage and serve a different purpose.
Protocol-specific transport machinery must retain the same distinction between
application links and transient transport connections, even when a logical
connection does not correspond one-to-one with a socket.

## Buffers and streams

`ZiIOBuf` provides pooled, reference-counted I/O storage. Configure the pool for
your message sizes and keep buffers alive through asynchronous output.

Initialize the multiplexer, construct its typed Rx and Tx allocators, register
both with `mx.allocator(a)`, then start the multiplexer and publish listeners or
connections. Direct Zi use initializes the multiplexer in its constructor;
Ztcp/Ztls dependents initialize their hub before preparing its allocator policies.
Registration requires `state() == ZmEngineState::Stopped` and asserts that
precondition. All allocators, including those registered by protocol client/server
initialization, must be registered before `start()`. Binding and registry
insertion complete directly before return; there is no I/O-thread dispatch.
The application must serialize registration against other registrations,
`start()` and teardown. The assertion does not prevent a concurrent start race.
Selecting an allocator for I/O never registers it implicitly.

Each network allocator is permanently dedicated to one multiplexer. Allocators
are app-owned, and the multiplexer retains one intrusive list entry per bound
allocator. There is no unbind, restart or reassignment lifecycle. Multiple
listeners and connections can share an Rx allocator; different listeners can
select different registered Rx allocators. `listen` and `connect` take the Rx
allocator immediately before their options argument. Accepted connections inherit
exactly their listener's selection. UDP setup also requires a registered UDP Rx
allocator. `ZiMxParams` contains no allocator parameters or multiplexer-wide Rx
allocator defaults.

Each submitted buffer independently selects Tx through its original allocator,
which must already be registered with that same multiplexer. The original
allocator pointer is separate from application `owner` metadata and survives
storage growth and retained ranges. Unregistered or wrong-multiplexer sends are
fatal in every build and backend, including ordinary and oversized storage.
Registration does not change compatibility: Rx cannot serve Tx, UDP cannot serve
streams or vice versa, and App allocators cannot serve Mx I/O. Zi distinguishes
direction and stream/datagram transport; Ztls owns TCP/TLS-specific checks.

Payload slot counts belong to allocator constructors, for example
`ZiRxTCPBufAllocator<>{256}` or `ZiTxTCPBufAllocator<>{512}` on Linux. The
independent defaults are 128, overridable through `ZiIOBuf_DefltRxRingSize` and
`ZiIOBuf_DefltTxRingSize`. Counts apply per allocator, not per connection; budget
for every app-owned policy and its slot geometry. The payload arena limit is
`ZiIOBuf_MaxRingSize` (32768). On `_WIN32`, network allocators have zero-argument
constructors and ring-sizing controls are compiled out.

`ZiMxParams::rxRingSize(n)` and `txRingSize(n)` configure the native owner's
kernel SQ/CQ capacities, independently of payload pool counts. They are also
compiled out on Windows. Kernel counts are in
`[ZiMxParams::MinRingSize, ZiMxParams::MaxRingSize]` (2..32768), with defaults of
128; kernel descriptor counts round up while allocator payload counts retain
the requested size. Native operation shares one ordinary Rx kernel ring and one
SQPOLL Tx kernel ring across the registered arenas, with distinct Rx/Tx threads.
Native completion dispatch, SQ publication and kernel entry are handled directly
by `ZiMultiplex` on those shards. Arena, operation and connection queues use
intrinsic membership; there is no separate native event-loop object.
`rxBufSize` and `txBufSize` select socket byte limits. The native-only `spin`
setting defaults to 1000 and controls bounded idle checks before blocking.

When runtime `Zi::uring()` is false, network policies allocate existing builtin
`IOBufAlloc` backing; they do not initialize or map payload rings. Native arena
exhaustion also falls back to existing `IOBufAlloc`, preserving original
provenance and UDP receive metadata capacity. Heap tuning controls descriptors,
builtin storage and jumbo growth. UDP preserves receive progress when mapped
slots are retained and silently drops zero-length datagrams. TCP EOF retains its
normal disconnect semantics.

Arena slot allocation and return use the shared MPMC free list, with free-list
links in unused aligned buffer heads. A depleted arena's return latch coalesces
notifications: an Rx-owner return schedules that owner directly, while an
off-owner return uses the existing scheduler queue and coalesced wake. Arming
rechecks availability to avoid losing a concurrent return. Stop/detach disarms
the latch, and service records remain stable until finalization. There is no
per-arena return eventfd, kernel READ/CQE/rearm stage or separate return poller.

Graceful disconnect drains pending output; abrupt close abandons it. Both finish
their connection operations and release their I/O contexts before closure is
reported. Staged Mx stop concludes I/O while retaining registered allocators and
native resources. Final Mx teardown releases registrations, owner rings and its
allocator references, clears raw back-pointers and permanently retires bindings.
It never makes an allocator reusable by another Mx.

The [workload sizing inventory](test/README.md#network-buffer-pool-sizing-in-workloads)
records payload working-set choices. These API contracts do not establish a
passing functional gate or measured performance acceptance.
`ZiRx`/`ZiTx` and `ZiRxStream`/`ZiTxStream` manage receive/transmit flow; stream
parsers must handle fragmented input, while writers should flush at deliberate
message boundaries. A span into a buffer is only a view, not an ownership token.

Use `ZiRxContext` and `ZiTxContext` for low-level receive and transmit
callbacks, respectively. Both are defined in `ZiIOContext.hh`.
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
thread can use `start()` and `stop()` for boolean, blocking completion;
never use them from resolver, I/O, or worker callbacks.

```cpp
ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
bool started = ZiResolver::start();
if (started) {
  // Submit asynchronous queries, or use ZiIP's main-thread blocking adapters.
  ZiResolver::stop();
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
See [TESTING.md](../docs/TESTING.md) for environment, logs and ports.
