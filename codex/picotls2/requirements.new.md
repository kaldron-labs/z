## Summary

Extend `Ztls` so TLS handshakes can survive picotls `PTLS_ERROR_ASYNC_OPERATION` by integrating `ZiEventLoop`, while replacing the positional `init()` APIs with parameter objects. Async support is required for server certificate signing and for client mTLS signing.

The implementation must preserve the existing single-threaded picotls model: all `ptls_t`, `ptls_context_t`, and `ptls_handshake_properties_t` access remains on the configured TLS scheduler thread. `ZiEventLoop` is used only to wait for external async job completion, then schedule handshake continuation back onto the TLS thread.

Key codebase facts:
- `Ztls::Link::handshake_()` currently contains a disabled FIXME for `PTLS_ERROR_ASYNC_OPERATION` and otherwise treats it as a fatal handshake error.
- `CliLink::connected_()` also calls `ptls_handshake()` through `handshake__()` for the initial ClientHello path, so async handling must be centralized rather than added only to network-input handling.
- `Engine::init`, `Client::init`, and `Server::init` are positional APIs today; in-repo callers exist in `ztls`, `zws`, `zrest`, `zcmd`, and `zhttp`.
- `ZiEventLoop` is explicitly intended for external handle/socket interop, including "openssl async signing completion (libcrypto / Ztls)", and already provides `init`, `start`, `stop`, `addSocket`, `delSocket`, `addHandle`, and `delHandle`.
- Picotls exposes async completion through `ptls_get_async_job(tls)`. A job may expose a readable fd through `job->get_fd()` or a completion callback through `job->set_completion_callback()`. This requirement supports fd/handle-backed jobs only.
- The OpenSSL backend facade currently hides `ptls_openssl_sign_certificate_t::async`; exposing that flag is required before `PTLS_ERROR_ASYNC_OPERATION` can be produced deliberately by OpenSSL signing.

## Resolved Product Decisions

- Remove the old positional `init()` overloads immediately. Do not keep inline compatibility adapters.
- Put shared optional trust and identity fields in `EngineParams`: `caPath`, `certPath`, and `keyPath`.
- Put `asyncThread` in `EngineParams`, not `ServerParams`, because client mTLS async signing is in scope.
- Add an engine error callback configured by `errorFn(...)`. It defaults to logging the moved `ZeException` with `ZiLogEvent(ZuMv(e))` and is used for async/event-loop failure paths.
- `asyncThread`, when configured, must resolve to a valid scheduler id that is distinct from the TLS thread. Callers must provision it as a dedicated isolated scheduler thread, normally through `ZiMxParams().scheduler(...thread(id, [](auto &t) { t.isolated(1); })...)`.
- Callback-only picotls async jobs are not required in the first implementation. If `get_fd` is unavailable, fail the handshake with an explicit diagnostic instead of installing `set_completion_callback`.
- Remove `cacheMax` from the new public server params unless implementation research identifies a real picotls ticket-cache size control. Preserve ticket lifetime behavior through `cacheTimeout`.

## Product Requirements

### Parameter Object API

Replace positional `Ztls` initialization with fluent parameter objects matching the style of `ZiMxParams`.

Add:
- `struct EngineParams` in namespace `Ztls`.
- `struct ClientParams : public EngineParams`.
- `struct ServerParams : public EngineParams`.
- A required constructor shape for common inputs: `ZiMultiplex *mx`, `ZuCSpan thread`, and `ZuSpan<ZuCSpan> alpn`.
- Fluent setters returning rvalue references with the local convention, for example `EngineParams &&caPath(ZuCSpan) { ...; return ZuMv(*this); }`.

Modify:
- `Engine::init` takes one `EngineParams` parameter.
- `Client::init` takes one `ClientParams` parameter.
- `Server::init` takes one `ServerParams` parameter.
- In-repo call sites migrate to expressions such as `init(Ztls::ClientParams(mx, thread, alpn).caPath(caPath))`.

The new params objects must preserve current synchronous init behavior: spans and paths supplied by the caller are copied or consumed before `init()` returns, so callers are not required to keep temporary arrays or config strings alive beyond initialization.

### Parameter Fields and Validation

`EngineParams` carries common required and optional configuration:
- `mx`: required `ZiMultiplex *`.
- `thread`: required TLS scheduler thread name/id.
- `alpn`: required span of ALPN protocol names, which may be empty.
- `caPath`: optional CA path/file; empty preserves existing platform-default CA loading.
- `certPath`: optional certificate path, shared by client mTLS and server identity.
- `keyPath`: optional private-key path, shared by client mTLS and server identity.
- `asyncThread`: optional async event-loop scheduler thread name/id.
- `errorFn`: optional async error callback; default logs with `ZiLogEvent(ZuMv(e))`.

`ClientParams` derives from `EngineParams` and currently adds no required fields. Client mTLS is configured by inherited `certPath` and `keyPath`; both must be present to enable client certificate signing.

`ServerParams` derives from `EngineParams` and adds:
- `mTLS`: optional boolean for client certificate authentication, default `false`.
- `cacheTimeout`: optional ticket lifetime in seconds; default remains the current effective value of 86400 seconds.

Validation requirements:
- Reject a null `mx`.
- Resolve `thread` through `ZiMultiplex::sid()` and keep the current invalid-thread diagnostic style.
- Reject initialization if `mx->running()` is false.
- If `asyncThread` is set, resolve it through `ZiMultiplex::sid()`, reject invalid ids with an "invalid async thread ID" diagnostic, and reject equality with the TLS thread.
- If scheduler metadata makes isolation visible, reject a non-isolated async thread. Otherwise document and test the required scheduler configuration with an isolated, dedicated thread.
- For client mTLS, reject only one of `certPath` or `keyPath` being set.
- For servers, `certPath` and `keyPath` remain required and `Server::init(ServerParams)` must reject missing or invalid values before listening starts.
- Missing or invalid required values must return `false` and emit existing-style `Ztls` diagnostics.

### Engine Init Threading Semantics

Preserve the current guarantee that picotls context setup runs on the configured TLS scheduler thread.

The public `Engine::init(EngineParams)` must:
- Store and validate the params needed before scheduler invocation.
- Resolve and store the TLS scheduler id.
- Resolve and store the async scheduler id when configured.
- Initialize random/backend state.
- Populate common `ptls_context_t` fields on the TLS thread.
- Initialize and copy ALPN data on the TLS thread.
- Make `run`, `invoke`, and `invoked` remain tied to `EngineParams::thread`.

Client/server-specific context setup must also run on the TLS thread. Any helper that accepts a configuration lambda must stay internal; the public API remains the single params object.

### Async Event Loop Ownership and Lifecycle

Add async event-loop ownership at the common engine level, or in an equivalent shared helper reachable by both `Client` and `Server`. A server-only `m_eventLoop` is no longer sufficient because client mTLS async signing is required.

Add:
- `ZiEventLoop m_eventLoop`.
- State indicating whether the event loop has been initialized and started.
- The resolved async scheduler id.
- A helper to query whether async handling is configured for the engine.

Lifecycle requirements:
- If `asyncThread` is absent, leave the event loop uninitialized and preserve today's synchronous behavior.
- If `asyncThread` is set, initialize `m_eventLoop` with the same `ZiMultiplex` and the resolved async scheduler id.
- Start `m_eventLoop` during init, using a synchronous wait/blocking wrapper if needed so `init()` can return `false` on start failure.
- Route `ZiEventLoop` failure callbacks through `EngineParams::errorFn`.
- Stop `m_eventLoop` during `final()` if it was started, before common TLS/backend cleanup.
- Call `m_eventLoop.final()` after it has been stopped.
- Do not leave sockets/handles registered across `final()`, disconnect, or TLS reset.

The lifecycle should follow the shape used by `ZdbPQ::Store`: initialize on the owning scheduler, start with a completion callback, register readiness callbacks only after start succeeds, stop before final cleanup, and route failures through the configured failure callback.

### Async Signing Enablement

Enable picotls/OpenSSL async signing only when the engine has an async event loop configured.

Backend requirements:
- Extend the `Ztls::Backend::SignCert` facade so callers can enable or disable `ptls_openssl_sign_certificate_t::async`.
- Accept either an async flag at `sign_cert_new(...)` construction time or an explicit setter such as `sign_cert_async(SignCert *, bool)`.
- Keep synchronous signing as the default.

Client behavior:
- If `ClientParams` includes both `certPath` and `keyPath`, configure client mTLS as today.
- If `asyncThread` is also set, configure that client mTLS signer for async operation.
- If `asyncThread` is not set, keep client mTLS signing synchronous.

Server behavior:
- Server certificate/key loading remains required.
- If `asyncThread` is set, configure the server `sign_certificate` callback for async-capable operation.
- If `asyncThread` is not set, keep server signing synchronous so existing deployments do not unexpectedly produce `PTLS_ERROR_ASYNC_OPERATION`.

If a backend cannot support async signing when `asyncThread` was explicitly configured, `init()` must fail with a clear backend capability diagnostic.

### Handshake Result Handling

Centralize handling of `ptls_handshake()` results so all call sites behave consistently:
- Network-input handshakes in `Link::handshake_()`.
- Initial client handshakes in `CliLink::connected_()`.
- Async resume handshakes after a job becomes ready.

For each `ptls_handshake()` result:
- `0`: complete the handshake and call `handshook()` exactly once.
- `PTLS_ERROR_IN_PROGRESS`: wait for more network input.
- `PTLS_ERROR_ASYNC_OPERATION`: register and wait for the async job.
- Peer close-notify alert: follow existing close-notify disconnect behavior.
- Any other error: log/route the existing diagnostic and disconnect without close notify where appropriate.

This handler must preserve current transmit behavior: any bytes generated into the handshake `ptls_buffer_t` must be flushed through `txBuf` and `flushTxBuf_` before returning.

### Handling `PTLS_ERROR_ASYNC_OPERATION`

On `PTLS_ERROR_ASYNC_OPERATION`:
- Retrieve the current async job with `ptls_get_async_job(tls())`.
- If async handling is not configured, emit a clear diagnostic naming `PTLS_ERROR_ASYNC_OPERATION` and `asyncThread`, then disconnect.
- If no job is returned, emit a clear diagnostic and disconnect.
- If the job does not expose `get_fd`, emit a clear diagnostic that callback-only async jobs are unsupported and disconnect.
- Call `job->get_fd(job)` and reject invalid/null fds.
- Register the fd with `ZiEventLoop` using the platform-appropriate handle path. Prefer `addHandle` for generic file descriptors/handles; use `addSocket` only if the backend explicitly returns a socket.
- Register readiness as read readiness; the write/send callback may be a no-op.
- Return without treating the handshake as failed.

On readiness:
- Deregister the fd/handle from `m_eventLoop` before resuming.
- Schedule handshake continuation onto the TLS thread with `app()->run()` or `app()->invoke()`.
- Do not call picotls directly from the `ZiEventLoop` thread unless the implementation has explicitly proven it is already on the TLS thread, which normal configuration forbids.
- Resume by calling `ptls_handshake()` again with no new network input.
- If resume produces another `PTLS_ERROR_ASYNC_OPERATION`, register the next job.
- If resume returns `PTLS_ERROR_IN_PROGRESS`, wait for network input.
- If resume succeeds, complete the handshake once.
- Otherwise use existing alert/error/disconnect handling.

### Async Job Ownership and Cleanup

Prevent dangling event-loop registrations and stale completions across disconnects, reconnects, and TLS reset.

Add per-link async state:
- Current async job pointer for identity only.
- Registered fd/handle value.
- Registration kind (`none`, `handle`, or `socket`) if both are supported.
- A TLS/connection generation counter incremented on `reset_tls_()`.
- A flag indicating whether an async resume is already scheduled.

Add a cleanup helper that:
- Deregisters the pending fd/handle if still registered.
- Clears the pending job pointer and registration state.
- Is safe to call repeatedly.

Call cleanup from:
- Successful handshake completion.
- `disconnect_()`.
- `disconnected_()`.
- `reset_tls_()` before freeing or replacing `ptls_t`.
- Engine `final()` for any still-owned links if such ownership exists.

Async callbacks must hold a safe reference to the link/connection and verify that the active connection pointer, TLS pointer or generation, and pending fd still match before scheduling or executing a resume. A completion from an old connection must not resume a reset or replaced `ptls_t`.

### Client and Server Async Scope

Async support applies to:
- Server certificate signing.
- Client mTLS certificate signing.

Async support does not otherwise change client behavior:
- Clients without `certPath`/`keyPath` remain synchronous even if `asyncThread` is configured.
- Clients with mTLS but without `asyncThread` remain synchronous.
- If any client or server receives `PTLS_ERROR_ASYNC_OPERATION` without an initialized event loop, it fails with a diagnostic instead of silently hanging or treating the state as in-progress.

### Ticket Lifetime and Cache Options

Preserve useful existing ticket behavior while removing obsolete API shape.

Requirements:
- Remove `cacheMax` from the new `ServerParams` public API unless a real picotls ticket-cache maximum can be implemented.
- Keep `cacheTimeout` as the server ticket lifetime option.
- Default `cacheTimeout < 0` to the current 86400-second lifetime.
- Continue configuring `ctx->encrypt_ticket` with the existing backend ticket key.
- Document in migration notes that the old `cacheMax` positional argument was unused in the picotls implementation and is intentionally not represented in the new params API.

### Downstream Caller Migration

Update all in-repo users to the new init API so the tree compiles without positional overloads.

Direct Ztls callers:
- `ztls/example/ZtlsClient.cc`
- `ztls/example/ZtlsServer.cc`
- `ztls/test/ZtlsBufHookTest.cc`
- `zhttp/test/zhttpclient.cc`

Wrapper/config-driven callers:
- `zws/src/Zws.hh`
- `zrest/src/Zrest.hh`
- `zcmd/src/ZtelClient.hh`

Any wrapper that reads config must keep existing user-facing config names unless a new config key is needed. Existing `thread` and `caPath` keys must map into params objects. New async support should use a clear `asyncThread` key where config wrappers expose it.

### Diagnostics and Error Routing

Make async and params failures visible and actionable.

Preserve or add diagnostics for:
- Null multiplexer.
- Invalid TLS thread id.
- Multiplexer not running.
- Invalid async thread id.
- Async thread equal to TLS thread.
- Async thread not configured as an isolated dedicated thread when detectable.
- Missing server certificate/key.
- Client mTLS configured with only certificate or only key.
- `PTLS_ERROR_ASYNC_OPERATION` with no configured event loop.
- `ptls_get_async_job()` returning null.
- Async job with no `get_fd`.
- Async job returning an invalid fd.
- `ZiEventLoop::start()` failure during init.
- `ZiEventLoop::addHandle()` or `addSocket()` failure at runtime.
- Runtime event-loop failure.
- Stale async completion ignored due to connection or TLS generation mismatch, at debug/trace level if such logging exists.

Routing requirements:
- Init validation failures may keep existing `ZiLOG(Error, "Ztls", ...)` style and return `false`.
- Event-loop start/add/runtime failures and async TLS runtime exceptions must route through `EngineParams::errorFn`, whose default logs via `ZiLogEvent`.
- Cascade the new `errorFn` path to other asynchronous failures in `Ztls` where the failure is not a direct synchronous caller error.

### Tests and Verification

Add focused coverage for API migration, unchanged synchronous behavior, and deterministic async resume behavior.

Required coverage:
- Compile all migrated examples/tests without positional `init()` overloads.
- Existing synchronous client/server handshakes still work without `asyncThread`.
- `ClientParams(...).certPath(...).keyPath(...)` still enables client mTLS.
- `ServerParams(...).certPath(...).keyPath(...)` still enables a normal server.
- `EngineParams::asyncThread(...)` validates a separate thread and starts/stops the event loop.
- Async thread equal to TLS thread is rejected.
- Missing or invalid async thread is rejected.
- Server async signing path resumes a handshake after an fd-backed fake async job signals readiness.
- Client mTLS async signing path resumes a handshake after an fd-backed fake async job signals readiness.
- Callback-only fake jobs fail with the expected unsupported diagnostic.
- Disconnect while async is pending unregisters the fd/handle and does not resume freed or reset TLS state.
- Reconnect/reset while async is pending ignores stale completions by generation check.
- `errorFn(...)` can capture async/event-loop failures without relying solely on global log capture.
- `cacheTimeout` still controls picotls ticket lifetime; removed `cacheMax` behavior is documented by migration/API tests rather than silently retained.

Suggested verification commands:
- Rebuild affected modules and examples with `make -j`.
- Run `ztls/test/ZtlsBufHookTest`.
- Run any new async-specific `ztls/test` binary directly from the build tree.
- Run affected wrapper tests or compile targets for `zws`, `zrest`, `zcmd`, and `zhttp` where available.

## Non-Requirements

- Do not implement callback-only picotls async jobs in this iteration.
- Do not add broader client async behavior beyond client mTLS signing.
- Do not change user-facing wrapper config names except to add an optional `asyncThread` where async signing is exposed.
- Do not perform unrelated refactors of `Ztls`, `ZiEventLoop`, or backend crypto code.
