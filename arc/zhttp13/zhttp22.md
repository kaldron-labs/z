# Zhttp Server Response Builder Plan

## Objective

Make every application response emitted by `Zhttp::Server` conform to the same
protocol-neutral `Builder` contract used for outbound client requests. Preserve
the existing ability for one workload to emit different concrete response types
for different endpoints without virtual dispatch or type erasure.

The public contract remains structural: application types do not inherit a
runtime Zhttp base. The `#if 0` declarations in `Zhttp.hh` describe the required
shape, while protocol-specific CRTP adapters retain the concrete application
type.

`Server` already preserves the concrete type through the templated
`respond(..., emit)` path and already consumes most of the response-facing
Builder operations. This change formalizes that behavior, adds the missing
Builder lifecycle, and documents the server-only asynchronous streaming
extension. It does not replace the workload API or introduce a nominal response
hierarchy.

## Non-goals

- Do not add a runtime `Zhttp::Response` class, virtual functions, response
  type erasure, or a variant containing all endpoint response types.
- Do not change `Workload::respond(const RequestMeta &, Request &, Emit &&)` or
  move endpoint dispatch into `Server`.
- Do not redesign the protocol CRTP builders or the H1/H2/H3 framing paths.
- Do not merge asynchronous response production into synchronous
  `Builder::body(emit)` at the cost of another allocation or an escaping body
  stream.
- Do not add compatibility fallbacks which make `reset()` optional. This is a
  deliberate application contract change and repository dependents should be
  migrated.

## Contract Shape

Refactor the outbound documentation in `Zhttp.hh` into three contracts:

```c++
struct Builder {
  using Headers = ZhttpHeaders(...);
  using Trailers = ZhttpHeaders(...); // optional
  using BodyPolicy = Body::None;

  void reset();

  template <typename Key, typename L> void header(L &&l);
  template <typename L> void header(L &&l);

  template <typename Emit> void body(Emit &&emit); // body policies only
  template <typename L> void bodyHdrs(L &&l);      // fixed bodies only
};

struct Request : public ZmObject, public Builder {
  template <typename L> void operation(L &&l);
  template <typename L> void host(L &&l);
  template <typename L> void protocol(L &&l);
  // queue, routing, replay, and client lifecycle operations
};

struct Response : public Builder {
  unsigned status();
  template <typename L> void reason(L &&l); // H1 only
  bool close() const;
  // Async streaming alternative to Builder::body(); present only when used.
  template <typename Done> void next(unsigned max, Done done);
};
```

Move request-only and response-only callbacks out of the shared sketch. This
makes `Builder` the common header/body construction contract and makes the two
role extensions explicit.

Document these invariants:

- `Server` calls `reset()` exactly once for each emitted wire response, before
  `close()`, `status()`, headers, or body construction.
- `reset()` clears transient construction state while preserving the response
  selected and configured by `Workload::respond()`.
- A response is consumed on the Tx shard. `reset()`, `close()`, status, header,
  synchronous body, and `next()` calls originate there.
- Other than `reset()` being first, applications must not depend on callback
  order. In particular, fixed H2/H3 bodies can be produced and patched before
  their encoded header block is built, whereas H1 builds its headers first.
- `Server` may move the concrete response after `emit(response)`; applications
  must not retain references to an emitted response or its transient data.
- `Headers`, optional `Trailers`, and `BodyPolicy` are compile-time properties
  of each concrete response type.
- The concrete response type may vary at every `emit()` call. No common runtime
  response base, variant, or virtual interface is required.
- `respond()` must invoke `emit()` exactly once. Zero or duplicate emission is
  a response build failure.

## Body Production

Preserve both body paths currently implemented by `Server` and document their
relationship to the response contract.

Synchronous fixed and streaming responses use the shared Builder callback:

```c++
template <typename Emit>
void body(Emit &&emit);
```

`body()` synchronously invokes its emitter zero or one times according to
`BodyPolicy::Optional`. The emitted writer is called synchronously with a
concrete protocol body stream and returns `void` or `bool`. Fixed responses use
`bodyHdrs()` after production to patch provisioned fields such as
`Content-Length`.

Asynchronous server streaming remains a response-specific extension:

```c++
template <typename Done>
void next(unsigned max, Done done);
```

It is selected only when `BodyPolicy::Streaming` is true and the response has a
well-formed `next(unsigned, BodyChunkFn)` operation. `Server` retains the
concrete response in `AsyncBody_`, calls `next()` on the Tx shard, and permits
`done(ZmRef<ZiIOBuf>, bool final)` to be invoked from another thread. `done()`
posts the result back to the Tx shard before accessing link-owned state.

An asynchronous response does not also implement `body()`: the optional
`Response::next()` operation is the server-specific, policy-selected replacement
for synchronous Builder body production. Keep this exception explicit in the
`Response : Builder` contract rather than attempting to move an arbitrary
producer out through `body(emit)`. Such a move would either move a response
while one of its member functions is active, require type erasure, or add
another retained allocation to the async path.

Keep the existing SFINAE detector style; do not introduce concepts or
`requires`. Rename `HasAsyncBody` only if a name such as
`HasAsyncResponseBody` materially clarifies that it detects `Response::next()`.

For optional policies, zero calls to the body emitter produce the header-only
form. For non-optional policies, zero calls are a build failure. More than one
call is always a build failure. These emitter-count rules apply to the
synchronous `body()` path; asynchronous `next()` always represents a present,
required streaming body under the current implementation. Do not claim
optional-async semantics unless they are separately implemented and tested.

## Server Refactor

1. At the start of the typed `emit` path in `sendLiveTx_()`, call `reset()` on
   the emitted concrete response exactly once. Do this before inspecting
   `close()`, `status()`, headers, or body state, then forward/move that same
   response into the existing adapter without adding a copy.
2. Keep the generic lambda so each endpoint may emit a distinct response type
   and all dispatch remains compile-time.
3. Continue moving the concrete response into `ResponseTx`; do not introduce a
   heap allocation for synchronous responses.
4. Make `ResponseTx` the sole protocol adapter for response headers and
   framing. Keep status, reason, declared/runtime headers, trailers,
   `Placeholder` provisioning, `bodyHdrs()` patching, and header validation there.
5. Continue using `MessageTx` for synchronous fixed/streaming construction so
   client requests and server responses retain the same emitter-count, writer,
   header-patch, and accounting rules.
6. Keep the asynchronous path specialized around `Response::next()`. Do not
   force it through `MessageTx`: it deliberately spans callbacks and transport
   fences while synchronous Builder lambdas do not escape their calls.
7. Preserve bounded retention and pooled I/O behavior: retain only the owned
   concrete response and handles, request at most the currently available
   retained budget, accept only non-empty buffers no larger than the
   reservation, and release retained accounting at every fence, failure,
   cancellation, and shutdown path.
8. Preserve the existing response state machine:
   `Queued -> Sending -> Committed -> Completing -> Done`, with exactly one
   terminal completion and one transport finish/cancel outcome.
9. Ensure H1 ordering remains per-link FIFO while H2/H3 responses remain
   independently schedulable. The Builder adoption must not move link-owned
   state off its current Rx/Tx owner.
10. Make the internal error response a normal `Response` Builder, including a
    no-op `reset()`.

## Application Migration

Update all repository-owned server applications and fixtures:

- Add `reset()` to every emitted response type. For immutable/fresh response
  values it may be a no-op; stateful fixed builders must reset counters and
  patch state without clearing endpoint configuration.
- Retain distinct response classes such as empty, generated fixed body, static
  file, redirect, and error responses. Each class declares its own `Headers`,
  optional `Trailers`, and `BodyPolicy`.
- Keep asynchronous streaming responses on `Response::next()`. Add `reset()`
  without clearing file offsets, handles, or other configuration needed for
  the selected response; clear only transient per-build state.
- Keep endpoint selection in `Workload::respond()` and emit the selected
  concrete type directly.
- Do not add compatibility shims, forwarding bases, or a common response
  variant. Propagate the required `reset()` operation through repository-owned
  response types.

The known migration points are `Server::ErrorResponse` in `ZhttpServer.hh`, the
response fixtures in `ZhttpServerIdleTest.cc`, and `ResponseBase` in
`zhttpd.cc`. Re-run the repository search during implementation so newly added
server workloads are not missed.

Provide a minimal documented example along these lines:

```c++
template <typename Emit>
void respond(const RequestMeta &meta, Request &request, Emit &&emit) {
  if (meta.path() == "/health") {
    emit(EmptyResponse{204});
    return;
  }
  if (meta.path() == "/record") {
    emit(JSONResponse{request.record});
    return;
  }
  emit(FileResponse{lookup(meta.path())});
}
```

This example must demonstrate that the response types are unrelated concrete
types which independently satisfy `Response : Builder`.

## Validation

Extend the transport contract and server tests to cover:

- `reset()` is called once and precedes every other response callback.
- A configured response survives `reset()` while transient produced-length and
  patch state are cleared.
- One workload emits different no-body, fixed, optional, synchronous stream,
  and asynchronous stream response types based on the request.
- Declared and runtime headers, trailers, H1 reason phrases, `close()`, and
  fixed-body `Placeholder` patching work through the shared contract.
- Optional bodies emit zero or one producer; required bodies emit exactly one;
  duplicate and missing emission fail deterministically on the synchronous
  body path.
- Synchronous writer failure, invalid headers, patch failure, oversized async
  chunks, empty async chunks, transport failure, peer reset, and server shutdown
  each produce the correct terminal outcome without retained-byte leaks.
- Async callbacks arriving after cancellation cannot resume transmission or
  complete the request twice.
- H1, H2, and H3 serialize equivalent status, headers, body, and trailers for
  the same application response.
- HEAD responses and status codes 1xx, 204, and 304 suppress bodies correctly;
  successful CONNECT responses obey their content-length/body restrictions.
- Existing client request Builder behavior and replay/reset ordering remain
  unchanged.

Use `ZuTest`/`ZuTestUtil` TAP tests. Prefer extending
`ZhttpTransportContractTest` for compile-time and cross-protocol contract
coverage and `ZhttpServerIdleTest` for lifecycle, async cancellation, and
shutdown behavior. Add a focused server response test only if those files
would otherwise mix unrelated state machines.

## Build and Acceptance

After implementation:

1. Build the library and focused tests with `make -C zhttp/src -j8` followed by
   `make -C zhttp/test -j8`.
2. Run the focused response/transport/server tests through their source-tree
   wrappers, not binaries under `.libs`.
3. Build `zhttp/util` and `zhttp/itest` to verify application migration.
4. Run `make -C zhttp/test test` and the relevant zhttp integration tests.
5. Run a top-level `make -j8` to catch dependent API breakage.

The change is complete when every server response, including asynchronous file
streaming and internal errors, enters through `Builder::reset()` and the common
header contract; synchronous bodies use `Builder::body(emit)`, asynchronous
streaming uses the documented `Response::next()` extension; different endpoint
response types require no common runtime base; and all supported HTTP versions
pass the same response contract tests.
