## Summary
The goal is to extend `Ztls` so server-side TLS handshakes can survive picotls `PTLS_ERROR_ASYNC_OPERATION` by integrating `ZiEventLoop`, while also replacing the increasingly positional `init()` signatures with parameter objects.

Key findings:
- `Ztls::Link::handshake_()` currently treats `PTLS_ERROR_ASYNC_OPERATION` as a commented-out FIXME and otherwise falls through to fatal error handling (`ztls/src/Ztls.hh:212`).
- `Engine::init`, `Client::init`, and `Server::init` are positional APIs today (`ztls/src/Ztls.hh:835`, `ztls/src/Ztls.hh:1084`, `ztls/src/Ztls.hh:1249`), and repo callers in `ztls`, `zws`, `zrest`, `zcmd`, and `zhttp` use those signatures.
- `ZiEventLoop` is explicitly intended for external socket/handle interop, including "openssl async signing completion (libcrypto / Ztls)" (`zi/src/ZiEventLoop.hh:7`), and already has the needed `start`, `stop`, `addSocket`, and `addHandle` APIs.
- The installed picotls headers say async handshake completion is represented by `ptls_get_async_job(tls)`, then either an fd from `job->get_fd()` or a callback from `job->set_completion_callback`, followed by another `ptls_handshake()` call once complete.
- The OpenSSL backend wrapper currently hides `ptls_openssl_sign_certificate_t::async`, so enabling async signing will also require a small backend-facing requirement, not only `Ztls.hh` changes.

High-level requirements:
- Add `EngineParams`, `ClientParams`, and `ServerParams` in namespace `Ztls`.
- Change the public `Engine::init`, `Client::init`, and `Server::init` API to accept the corresponding single params object.
- Add an optional `ServerParams::asyncThread(ZuCSpan)` configuration and a `ZiEventLoop m_eventLoop` member to `Server`.
- When server async support is configured, start and own a `ZiEventLoop` on the configured multiplexer thread and use it to wait for picotls async jobs.
- Resume the TLS handshake on the TLS thread after async completion, without violating the current single-threaded picotls execution model.

## Product Requirements

### Parameter Object API
- Description: Replace positional `Ztls` initialization with named parameter objects that mirror the fluent style of `ZiMxParams`.
- What will be added:
  - `struct EngineParams` in namespace `Ztls`.
  - `struct ClientParams : public EngineParams`.
  - `struct ServerParams : public EngineParams`.
  - Required constructor arguments for the common engine inputs: `ZiMultiplex *mx`, `ZuCSpan thread`, and `ZuSpan<ZuCSpan> alpn`.
  - Fluent setters returning rvalue references, following the `ZiMxParams &&method(...) { ...; return ZuMv(*this); }` convention.
- What will be modified:
  - `Engine::init` will take one `EngineParams` parameter.
  - `Client::init` will take one `ClientParams` parameter.
  - `Server::init` will take one `ServerParams` parameter.
  - Internal repo callers will migrate to expressions such as `init(Ztls::ClientParams(mx, thread, alpn).caPath(caPath))`.
- How it connects:
  - This is the foundation for adding optional `asyncThread` without further widening `Server::init`.
  - The params objects must preserve current synchronous init semantics: all spans and paths passed by the caller are consumed or copied before `init()` returns.

### Parameter Fields and Validation
- Description: Define the required and optional configuration carried by each params object.
- What will be added:
  - `EngineParams` fields/getters for `mx`, `thread`, `alpn`, and common trust configuration such as `caPath`.
  - `ClientParams` fields/getters for client-specific certificate options, currently mTLS `certPath` and `keyPath`.
  - `ServerParams` fields/getters for server certificate/key options, `mTLS`, `cacheMax`, `cacheTimeout`, and optional `asyncThread`.
- What will be modified:
  - `Client::init` will map params onto current behavior: ALPN copy, CA loading, optional client certificate/key loading, ticket saving, and no client async behavior unless explicitly added later.
  - `Server::init` will map params onto current behavior: ALPN copy, CA loading, required server cert/key validation, optional client authentication, ticket lifetime, ticket encryption, and async event-loop setup.
  - Missing or invalid required values must produce existing-style `ZiLOG(Error, "Ztls", ...)` diagnostics and return `false`.
- How it connects:
  - Server cert/key remain required for a usable server even if represented as fluent setters; `Server::init` must reject missing values before listening starts.
  - Thread validation should continue using `ZiMultiplex::sid()`, and async thread validation should use the same naming/numeric rules.

### Engine Init Threading Semantics
- Description: Preserve the current guarantee that picotls context setup runs on the configured TLS scheduler thread.
- What will be modified:
  - The public `Engine::init(EngineParams)` must validate `mx`, resolve `thread`, verify `mx->running()`, initialize random/backend state, and populate the common `ptls_context_t` fields on the TLS thread.
  - Client/server-specific context setup must also occur on the TLS thread. If the final implementation needs a protected helper that accepts a configuration lambda, that helper must remain internal; the public API remains single-parameter.
  - `run`, `invoke`, and `invoked` behavior remains tied to the TLS thread selected by `EngineParams::thread`.
- How it connects:
  - Async completion callbacks from `ZiEventLoop` must not call picotls directly unless they are already on the TLS thread; they should schedule back through `app()->run()` or `app()->invoke()`.

### Server Async Event Loop Lifecycle
- Description: Add optional server ownership of `ZiEventLoop` for external async TLS work.
- What will be added:
  - A `ZiEventLoop m_eventLoop` member to `Ztls::Server`.
  - State indicating whether the event loop has been configured/started and the resolved async scheduler id.
  - `ServerParams::asyncThread(ZuCSpan)` as an optional fluent setter.
- What will be modified:
  - `Server::init(ServerParams)` will initialize and start `m_eventLoop` when `asyncThread` is non-empty.
  - `Server::final()` will stop and finalize `m_eventLoop` if it was started.
  - Destruction/finalization must not leave sockets/handles registered with `ZiEventLoop`.
  - If `asyncThread` is absent, `m_eventLoop` may remain uninitialized and server behavior must remain equivalent to today.
- How it connects:
  - `ZiEventLoop` should follow the lifecycle shape already used by `ZdbPQ::Store`: initialize with `mx` and a scheduler id, start with a completion callback, register fd/handle readiness callbacks only after start succeeds, stop before final cleanup.

### Async Signing Enablement
- Description: Enable picotls/OpenSSL async signing only when the server has an async event loop configured.
- What will be added:
  - Backend support to set or request asynchronous certificate signing for `Backend::SignCert`, because the current facade exposes only `sign_cert_new`, `sign_cert_free`, and `sign_cert_cb`.
  - A clear mapping from `ServerParams::asyncThread` to backend async signing behavior.
- What will be modified:
  - When `asyncThread` is set, `Server::init` will configure the server `sign_certificate` callback for async-capable operation.
  - When `asyncThread` is not set, the server should keep synchronous signing to avoid producing `PTLS_ERROR_ASYNC_OPERATION` in normal operation.
- How it connects:
  - This requirement makes the event loop observable and useful while preserving current behavior for existing deployments.
  - It also provides a clean failure mode if a future backend can return async jobs for reasons other than OpenSSL signing.

### Handling `PTLS_ERROR_ASYNC_OPERATION`
- Description: Convert async handshake results into resumable server-side handshake state instead of fatal errors.
- What will be modified:
  - `Link::handshake_()` will explicitly detect `PTLS_ERROR_ASYNC_OPERATION`.
  - On async operation:
    - Retrieve the current picotls async job with `ptls_get_async_job(tls())`.
    - If no server event loop is configured, log a clear diagnostic and disconnect the connection.
    - If no async job or no supported wait mechanism is available, log a clear diagnostic and disconnect.
    - Register the job's fd/handle with `Server::m_eventLoop` when `get_fd` is available.
    - If only `set_completion_callback` is available, arrange for the completion callback to wake/schedule the TLS thread without calling picotls from the callback thread.
    - Return without treating the handshake as failed.
  - On job readiness/completion:
    - Deregister the fd/handle from `m_eventLoop`.
    - Schedule handshake continuation on the TLS thread.
    - Resume by calling `ptls_handshake()` again with no new network input, flushing any generated handshake data through the existing `txBuf`/`flushTxBuf_` path.
    - If resume returns success, call `handshook()` exactly once.
    - If resume returns `PTLS_ERROR_IN_PROGRESS`, continue waiting for network input.
    - If resume returns another `PTLS_ERROR_ASYNC_OPERATION`, register the next async job.
    - Otherwise use existing alert/error/disconnect handling.
- How it connects:
  - This requirement depends on the server event loop lifecycle and backend async enablement.
  - It must preserve the current invariant that `ptls_t` and `ptls_handshake_properties_t` are touched only on the TLS thread.

### Async Job Ownership and Cleanup
- Description: Prevent dangling event-loop registrations and stale completions across disconnects, reconnects, and TLS reset.
- What will be added:
  - Per-link state for the pending async job registration, such as the job pointer and registered fd/handle.
  - A cleanup helper called from handshake completion, disconnect, and `reset_tls_()`.
  - A generation or connection identity check so a completion from an old connection cannot resume a reset or replaced `ptls_t`.
- What will be modified:
  - `disconnect_()`, `disconnected_()`, and `reset_tls_()` will clear pending async state before freeing or replacing TLS state.
  - Async callbacks will hold a safe reference to the link/connection and verify that the active connection and TLS generation still match before resuming.
- How it connects:
  - Existing `CliLink` can overlap/reconnect and `SrvLink` is connection-owned, so stale callback protection is required even if async is initially server-only.

### Server-Only Scope
- Description: Keep the requested async support scoped to server handshakes.
- What will be added:
  - `asyncThread` exists on `ServerParams`, not `EngineParams` or `ClientParams`.
- What will be modified:
  - Client code may still move to `ClientParams`, but it will not start a `ZiEventLoop`.
  - If a client handshake unexpectedly receives `PTLS_ERROR_ASYNC_OPERATION`, it should fail with a diagnostic unless a future requirement explicitly adds client async handling.
- How it connects:
  - Server async is the stated goal and aligns with the main OpenSSL async signing use case for certificate verification/signing under server load.

### Downstream Caller Migration
- Description: Update in-repo users to the new init API so the tree exercises the requirement consistently.
- What will be modified:
  - `ztls/example/ZtlsClient.cc`, `ztls/example/ZtlsServer.cc`, `ztls/test/ZtlsBufHookTest.cc`, and `zhttp/test/zhttpclient.cc`.
  - Wrapper modules that call `Ztls::Client::init`, including `zws/src/Zws.hh`, `zrest/src/Zrest.hh`, and `zcmd/src/ZtelClient.hh`.
  - Any code that wraps config objects should translate existing config keys into the new params object without changing user-facing configuration names.
- How it connects:
  - This confirms the params design is practical for both direct callers and config-driven wrappers.
  - It also protects against accidental reliance on removed positional overloads.

### Diagnostics and Failure Modes
- Description: Make async and parameter failures visible and actionable.
- What will be added or preserved:
  - Invalid TLS thread: existing "invalid thread ID" style diagnostic.
  - Invalid async thread: analogous "invalid async thread ID" diagnostic.
  - Async returned with no configured event loop: explicit error naming `PTLS_ERROR_ASYNC_OPERATION` and `asyncThread`.
  - Async job has no fd/callback mechanism: explicit backend capability error.
  - Event-loop start/add failure: explicit `ZiEventLoop` error and `Server::init` failure when detected during init.
  - Runtime event-loop failure: logged through `ZiLog` or routed through a defined server failure callback.
- How it connects:
  - These diagnostics are needed because async behavior is otherwise rare and hard to reproduce.

### Tests and Verification
- Description: Add focused coverage for API migration and async resume behavior.
- What will be added or modified:
  - Compile coverage for the new params API by updating all existing Ztls examples/tests.
  - A test that existing synchronous server/client handshakes still work without `asyncThread`.
  - A test that `ServerParams(...).asyncThread("...")` validates and starts the event loop.
  - A deterministic async handshake test. If OpenSSL async signing cannot be reliably triggered in CI, add a test hook or fake `ptls_sign_certificate_t` that returns `PTLS_ERROR_ASYNC_OPERATION`, exposes a pipe/event fd, signals completion, and verifies that `Ztls` resumes the handshake.
  - Cleanup tests that disconnect while an async job is pending and verify no stale callback resumes freed TLS state.
- Suggested verification commands:
  - Rebuild affected test binaries under `ztls/test` and affected examples.
  - Run `ztls/test/ZtlsBufHookTest` after migration.
  - Run any new async-specific Ztls test directly from the build tree.
- How it connects:
  - The async path is stateful and timing-sensitive; deterministic fake-job coverage is more valuable than relying only on an OpenSSL configuration that may not trigger async on every platform.

## Options and Open Questions
- Should the old positional `init()` overloads be removed immediately, or kept temporarily as inline adapters that construct params objects? The goal says the public API will take one params object, but adapters could reduce downstream breakage outside this repository.
  - ANSWER: remove immediately
- Should `caPath`, `certPath`, and `keyPath` live in `EngineParams` as shared optional fields, or should `certPath`/`keyPath` be duplicated in `ClientParams` and `ServerParams` because their validation differs? The requirement allows either as long as getters/setters are succinct and call sites remain clear.
  - ANSWER: shared optional
- Should server async event-loop runtime failures call an optional app callback, or is `ZiLog` sufficient? Existing `ZiEventLoop` can accept a failure callback, while current `Ztls` only documents an optional `exception` callback in comments.
  - ANSWER: add to `EngineParams` an optional exception callback `ZmFn<ZeException> m_errorFn` set by `errorFn(...)`, defaulted to `[](ZeException e) { ZiLogEvent(ZuMv(e)); }`, and call that instead of going direct to `ZiLog`; cascade this change where appropriate to other asynchronous errors that currently log direct to `ZiLog`
- Should `asyncThread` be allowed to equal the TLS thread? It may be simpler to require a distinct scheduler thread, but `ZiEventLoop` itself only needs a valid scheduler id.
  - ANSWER: no, `ZiEventLoop` is woken up by an FD operation and associated system calls, which would increase latency on the TLS thread; `asyncThread`, if used, must be a separate dedicated thread that is exclusively for this purpose (i.e. `isolated(1)`); 
- Should callback-only picotls async jobs be supported in the first implementation, or should first support be fd/handle-based only with a clear diagnostic for callback-only jobs? Picotls documents both, but `ZiEventLoop` maps most directly to fd/handle readiness.
  - ANSWER: callbacks are only used by custom application integrations, no need to support them
- Should client mTLS async signing be added now? The explicit goal only adds `asyncThread` to `ServerParams`; adding client async support would expand scope.
  - ANSWER: support client async support for mtls (this implies moving `asyncThread` into `EngineParams`
- What is the intended role of `cacheMax`? The current server init accepts it but does not use it. The params migration should preserve behavior, but this remains an existing ambiguity.
  - ANSWER: this may be a holdover from `mbedtls`, which was the previous implementation; `cacheMax` and `cacheTimeout` are intended to control ticket caching; update this for `picotls`, removing `cacheMax` if no longer relevant
