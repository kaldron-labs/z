# Zi: I/O

`zi` provides framework I/O facilities through `libZi`. FlatBuffers
serialization lives in [zfb](../zfb/README.md); protobuf serialization lives
in [zpb](../zpb/README.md).


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
