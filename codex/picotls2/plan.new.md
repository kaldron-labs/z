## Summary

The goal is to replace positional `Ztls` initialization with owned fluent parameter objects and to add picotls async-handshake support for fd-backed OpenSSL server certificate signing. The revised scope incorporates the answered questions at the end of `plan.md`: client-side async signing is not required in this iteration, Windows async completion is disabled under `_WIN32`, fake/mock signers are acceptable for deterministic tests, and the unused server `cacheMax` argument is removed rather than carried forward.

The implementation keeps the current single-threaded picotls model. All `ptls_t`, `ptls_context_t`, and `ptls_handshake_properties_t` access stays on the configured TLS scheduler thread. `ZiEventLoop` runs on a separate dedicated scheduler thread and only waits for external async completion, then schedules handshake continuation back to the TLS thread.

Key design points:
- Add `Ztls::EngineParams`, `Ztls::ClientParams`, and `Ztls::ServerParams` in `ztls/src/Ztls.hh`; remove old public positional `init()` overloads immediately.
- Keep `asyncThread` in `EngineParams` for a common engine-level event-loop owner, but enable OpenSSL async signing only for server signers in this iteration.
- Centralize all `ptls_handshake()` result handling so network-input handshakes, initial client ClientHello generation, and async resumes share one path.
- Add per-link async state and cleanup, with a special retired-job path because local picotls/OpenSSL documents that `ptls_free()` must not be called until an in-flight async OpenSSL job has completed.
- Update `ZiEventLoop` narrowly so Unix `addSocket()` and `addHandle()` check `epoll_ctl()` failures instead of always returning `true`, and account for the current immediate "prime the pump" callback behavior during `Ztls` async registration.
- Migrate in-repo direct callers and config wrappers to the params API.

Local API research:
- `ztls/src/Ztls.hh:218` has a disabled `PTLS_ERROR_ASYNC_OPERATION` block in `Link::handshake_()`.
- `ztls/src/Ztls.hh:736` starts client ClientHello with `handshake__()` and currently handles only nonzero/non-`PTLS_ERROR_IN_PROGRESS` as fatal.
- `ztls/src/Ztls.hh:835`, `ztls/src/Ztls.hh:1084`, and `ztls/src/Ztls.hh:1192` are the current positional init APIs.
- `ztls/src/Ztls.hh:1265` confirms `cacheMax` is unused today.
- `zi/src/ZiEventLoop.hh:7` explicitly lists OpenSSL async signing completion as an intended interop use.
- `zi/src/ZiEventLoop.cc:240` and `zi/src/ZiEventLoop.cc:337` currently ignore Unix `epoll_ctl(EPOLL_CTL_ADD)` return values.
- `zi/src/ZiEventLoop.cc:271` and `zi/src/ZiEventLoop.cc:361` synchronously call send and recv callbacks during registration before the node is added to the hash table.
- `/home/count0/src/picotls/include/picotls.h:772` documents the async contract: after `PTLS_ERROR_ASYNC_OPERATION`, obtain `ptls_get_async_job()`, wait for `get_fd()` readability or completion callback, then call `ptls_handshake()` again.
- `/home/count0/src/picotls/include/picotls/openssl.h:193` exposes `ptls_openssl_sign_certificate_t::async`.
- `/home/count0/src/picotls/lib/picotls.c:3233` passes an async-job slot only on the server side, and `/home/count0/src/picotls/lib/picotls.c:5536` returns `tls->server.async_job`.
- `/home/count0/src/picotls/lib/openssl.c:1077` warns users not to call `ptls_free()` before an async OpenSSL operation has completed.

## Architecture Documentation

New or changed components:
- `Ztls::EngineParams`, `Ztls::ClientParams`, and `Ztls::ServerParams` in `ztls/src/Ztls.hh`.
- Common `Engine` async state:
  - `ZiEventLoop m_eventLoop`
  - resolved TLS scheduler id and optional async scheduler id
  - event-loop initialized/started flags
  - `Ztls::ErrorFn` callback, defaulting to `ZiLogEvent(ZuMv(e))`
  - helpers such as `asyncConfigured_()`, `startAsyncLoop_()`, `stopAsyncLoop_()`, `asyncAddHandle_()`, `asyncDelHandle_()`, and `error_(ZeException)`.
- Backend facade additions in `ztls/src/ZtlsBackend.hh` and `ztls/src/ZtlsOpenSSL.cc`:
  - `bool sign_cert_async(SignCert *, bool)` to toggle `ptls_openssl_sign_certificate_t::async`.
  - Synchronous signing remains the default.
  - Return `false` when async is requested but unsupported, including `_WIN32`.
- Per-link async state in `Link`:
  - pending `ptls_async_job_t *` identity
  - registered fd/handle value
  - registration kind, initially `none` or `handle`
  - active `ptls_t *` identity
  - TLS generation counter incremented before replacing TLS state
  - async resume scheduled flag
  - handshake-complete flag
  - cleanup/retire helpers.
- Narrow `ZiEventLoop` robustness change:
  - Check `epoll_ctl(EPOLL_CTL_ADD)` in Unix `addSocket()` and `addHandle()` and return `false` on failure after routing a `ZiEventLoop` exception.

New or changed processes or threads:
- The TLS scheduler thread remains the only thread that calls picotls.
- The optional async scheduler thread runs `ZiEventLoop` and observes async job fd readability.
- Event-loop callbacks never call picotls directly. They deregister or schedule deregistration on the event-loop thread, then schedule handshake resume on the TLS thread.

New or changed interfaces:
- `Engine::init(EngineParams)`, `Client::init(ClientParams)`, and `Server::init(ServerParams)` replace the old positional overloads.
- `EngineParams` owns required `mx`, `thread`, and `alpn`, plus optional `caPath`, `certPath`, `keyPath`, `asyncThread`, and `errorFn`.
- `ClientParams` derives from `EngineParams` and adds no fields. Client mTLS remains configured by inherited `certPath` and `keyPath`, but signing stays synchronous even when `asyncThread` is set.
- `ServerParams` derives from `EngineParams` and adds `mTLS` and `cacheTimeout`.
- Config-driven wrappers keep existing `thread` and `caPath` keys and may add optional `asyncThread` where server async signing is exposed. Client wrappers may accept `asyncThread` for API consistency, but must document that it does not enable client async signing in this iteration.

New or changed data flows:
- Parameter constructors copy `thread`, ALPN protocol names, and optional strings into owned storage before scheduler invocation, so temporary caller spans and config strings do not need to survive `init()`.
- Server async signing flow:
  1. Server `ptls_handshake()` returns `PTLS_ERROR_ASYNC_OPERATION` on the TLS thread.
  2. `Link` obtains the current job with `ptls_get_async_job(tls())`.
  3. `Link` obtains a Unix fd through `job->get_fd(job)`.
  4. `Link` stores pending job/fd/TLS/generation state before registering the fd.
  5. Registration runs on the `ZiEventLoop` scheduler thread using `addHandle`.
  6. Read readiness schedules handle deletion after registration has fully returned, then schedules TLS-thread resume.
  7. TLS-thread resume validates link, TLS pointer, generation, job, and fd identity before calling `ptls_handshake()` again with no new input.

New or changed event-driven or timer processing:
- `Engine::init` starts `ZiEventLoop` synchronously when `asyncThread` is configured, so `init()` can return `false` on start failure.
- `Engine::final` stops the event loop before common TLS/backend cleanup, after any retired async jobs have either completed or been reported through the error callback.
- `Link::asyncCleanup_()` removes active resume registrations during handshake completion, disconnect, and reset.
- `Link::asyncRetireTLS_()` handles the OpenSSL constraint that an in-flight async job must complete before `ptls_free()`.

New or changed network programming:
- No application network socket model changes.
- Picotls/OpenSSL async completion is treated as a Unix external handle/fd. Use `addHandle`; do not use `addSocket` unless a backend explicitly documents a socket.
- Under `_WIN32`, reject `asyncThread` or async signer enablement with a clear diagnostic because picotls returns an `int` fd while `Zi::Handle` is pointer-sized `HANDLE`.

New or changed data stores:
- None.

## Detailed Design and Implementation Plan

### Phase 1: Add Parameter Object API

- Add parameter types to `ztls/src/Ztls.hh`, near the existing public `Ztls` aliases:

```c++
namespace Ztls {

using ErrorFn = ZmFn<void(ZeException)>;
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Ztls.Param">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Ztls.ParamStrings">>));

struct EngineParams {
  EngineParams(ZiMultiplex *mx, ZuCSpan thread, ZuSpan<ZuCSpan> alpn);

  EngineParams &&caPath(ZuCSpan);
  EngineParams &&certPath(ZuCSpan);
  EngineParams &&keyPath(ZuCSpan);
  EngineParams &&asyncThread(ZuCSpan);
  EngineParams &&errorFn(ErrorFn);

  ZiMultiplex *mx = nullptr;
  ParamString thread;
  ParamStrings alpn;
  ParamString caPath_;
  ParamString certPath_;
  ParamString keyPath_;
  ParamString asyncThread_;
  ErrorFn errorFn_;
};

struct ClientParams : public EngineParams {
  using EngineParams::EngineParams;
  ClientParams &&caPath(ZuCSpan);
  ClientParams &&certPath(ZuCSpan);
  ClientParams &&keyPath(ZuCSpan);
  ClientParams &&asyncThread(ZuCSpan);
  ClientParams &&errorFn(ErrorFn);
};

struct ServerParams : public EngineParams {
  using EngineParams::EngineParams;
  ServerParams &&caPath(ZuCSpan);
  ServerParams &&certPath(ZuCSpan);
  ServerParams &&keyPath(ZuCSpan);
  ServerParams &&asyncThread(ZuCSpan);
  ServerParams &&errorFn(ErrorFn);
  ServerParams &&mTLS(bool);
  ServerParams &&cacheTimeout(int);

  bool mTLS_ = false;
  int cacheTimeout_ = -1;
};

} // namespace Ztls
```

- Implement derived setters explicitly or through a small private forwarding helper so chains such as `ServerParams(...).certPath(...).keyPath(...).cacheTimeout(...)` preserve the derived return type.
- Default `errorFn_` to a lambda that logs the moved exception with `ZiLogEvent(ZuMv(e))`.
- Store ALPN names as strings, not borrowed `ptls_iovec_t` values. Convert to `ptls_iovec_t` only inside `Engine::init_alpn_()` on the TLS thread.
- Remove the old public positional `Engine::init`, `Client::init`, and `Server::init` overloads. Any lambda-based helper remains private/protected only.
- Preserve current behavior that an empty `caPath` loads the platform defaults.

### Phase 2: Rework Engine Init, Validation, and Event-Loop Lifecycle

- Replace the public templated `Engine::init(ZiMultiplex *, ZuCSpan, L)` with an internal helper such as `init_(EngineParams &&, L &&configure)` that is called by `Engine`, `Client`, and `Server` public init methods.
- Validate before scheduler invocation where possible:
  - `mx` is not null.
  - `thread` resolves through `mx->sid(thread)` and is in `1..mx->params().nThreads()`.
  - `mx->running()` is true.
  - If `asyncThread` is set, reject under `_WIN32` with a diagnostic explaining picotls' `int` fd limitation.
  - If `asyncThread` is set on Unix, resolve it through `mx->sid(asyncThread)`, reject zero or out-of-range ids, reject equality with the TLS thread, reject equality with `mx->rxThread()` or `mx->txThread()`, and reject `!mx->params().thread(asyncSid).isolated()`.
- Keep the existing invalid TLS thread diagnostic style: `"invalid thread ID \"...\""`.
- Use `"invalid async thread ID \"...\""` for async thread resolution failures.
- Store `m_mx`, `m_thread`, `m_asyncThread`, `m_errorFn`, and event-loop flags before entering the TLS scheduler.
- On the TLS scheduler thread:
  - run `Random::init()`
  - clear and populate `ptls_context_t`
  - initialize cipher suites
  - copy ALPN into existing `m_alpnData` and `m_alpn`
  - initialize and start `m_eventLoop` when async is configured
  - run the client/server context configuration lambda.
- Start event loop synchronously:
  - call `m_eventLoop.init(m_mx, m_asyncThread, failFn)`
  - call `m_eventLoop.start(...)`
  - wrap the callback in `ZmBlock<bool>` or equivalent so `init()` can fail deterministically.
- If any later init step fails after event-loop start, stop and final the event loop before returning `false`.
- Implement `Engine::final()` to:
  - stop accepting new async registrations
  - wait for or report retired async TLS jobs
  - stop `m_eventLoop` with a blocking callback when started
  - call `m_eventLoop.final()`
  - clear `m_errorFn`.
- Route event-loop start/runtime failures and runtime async failures through `m_errorFn`; keep direct synchronous validation failures as existing-style `ZiLOG(Error, "Ztls", ...)` plus `false`.

### Phase 3: Server and Client Context Setup

- `Client::init(ClientParams)`:
  - Validate client mTLS all-or-none: reject exactly one of `certPath` or `keyPath`.
  - Call common engine init with params.
  - On the TLS thread, configure `save_ticket`, ALPN, CA verification, and default client context fields exactly as today.
  - If both `certPath` and `keyPath` are set, load client certificates and key and install `ctx->sign_certificate` as today.
  - Do not enable `Backend::sign_cert_async` for client signers in this iteration. This implements the answered scope decision that client-side async signing is not required.
  - If a client nevertheless receives `PTLS_ERROR_ASYNC_OPERATION`, use the common handler. With `asyncThread` absent it fails with a diagnostic; with `asyncThread` present it can wait on an fd-backed job, but Ztls does not deliberately produce one for client mTLS.
- `Server::init(ServerParams)`:
  - Reject missing `certPath` or missing `keyPath` before listening starts, with explicit diagnostics.
  - Load server certificate/key and configure `ctx->sign_certificate`.
  - If `asyncThread` is configured, call `Backend::sign_cert_async(m_sign, true)` and fail init if it returns `false`.
  - Set `ctx->require_client_authentication` from `mTLS_`.
  - Preserve ticket lifetime with `ctx->ticket_lifetime = cacheTimeout_ < 0 ? 86400 : cacheTimeout_`.
  - Preserve `ctx->encrypt_ticket` with the existing backend ticket key.
  - Remove `cacheMax` completely from the public API and migration examples.

### Phase 4: Backend Async Signing Facade

- Add to `ztls/src/ZtlsBackend.hh`:

```c++
bool sign_cert_async(SignCert *, bool);
```

- Implement in `ztls/src/ZtlsOpenSSL.cc`:
  - Return `false` for null signer.
  - Under `_WIN32`, return `false` when enabling async and leave a short comment that picotls exposes an `int` fd while Windows `HANDLE` is pointer-sized.
  - Under non-Windows with `PTLS_OPENSSL_HAVE_ASYNC`, set `sign->impl.async = async ? 1 : 0` and return `true`.
  - If `PTLS_OPENSSL_HAVE_ASYNC` is false and `async` is true, return `false`.
  - If `async` is false, always leave the signer synchronous and return `true`.
- Keep `Backend::sign_cert_new(PKey *)` synchronous by default so existing deployments do not unexpectedly produce `PTLS_ERROR_ASYNC_OPERATION`.

### Phase 5: Centralize Handshake Result Handling

- Keep `handshake__()` as the low-level wrapper around `ptls_handshake()`:
  - allocate the handshake TX buffer
  - call `ptls_handshake(m_tls, &pbuf, input, inlen, &m_props)`
  - flush generated bytes through `flushTxBuf_()`
  - return the picotls result.
- Add a single result handler returning whether the link remains usable:

```c++
bool handleHandshakeResult_(int n) {
  if (!n) return finishHandshake_();
  if (n == PTLS_ERROR_IN_PROGRESS) return true;
  if (n == PTLS_ERROR_ASYNC_OPERATION) return asyncHandshake_();
  if (isCloseNotify_(n)) { disconnect_(true); return false; }
  ZiLOG(Error, "Ztls", ([n](auto &s) {
    s << "ptls_handshake(): " << strerror_(n);
  }));
  disconnect_(false);
  return false;
}
```

- Use the handler from:
  - `Link::handshake_(ZmRef<ZiIOBuf>)`
  - `CliLink::connected_()` after the initial ClientHello `handshake__()`
  - async resume calls.
- Rename or wrap `handshook()` as `finishHandshake_()` and guard it with a per-link `m_handshook` flag so application `connected(...)` is called exactly once.
- Reset `m_handshook` in `reset_tls_()`.
- On successful handshake completion:
  - call `asyncCleanup_()`
  - initialize record metadata exactly as today
  - call `impl()->connected(...)`.
- If network input arrives while an async job is pending, do not call picotls reentrantly. For the current server signing scope no peer handshake input is expected while the server CertificateVerify is pending; nevertheless, either queue the record in a small handshake-pending queue or log/disconnect explicitly rather than silently invoking `ptls_handshake()` against a pending async job.

### Phase 6: Register and Resume FD-Backed Async Jobs

- On `PTLS_ERROR_ASYNC_OPERATION`:
  - If async handling is not configured, route/log an explicit diagnostic naming `PTLS_ERROR_ASYNC_OPERATION` and `asyncThread`, then disconnect.
  - Call `ptls_get_async_job(tls())`; reject null.
  - Reject missing `job->get_fd` with a callback-only unsupported diagnostic.
  - Call `job->get_fd(job)`; reject `Zi::nullHandle(fd)` and prevalidate the fd on Unix, for example with `fcntl(fd, F_GETFD)`, so invalid fds are caught before registration.
  - Call `asyncCleanup_()` for any previous active registration before storing the new one.
  - Store job identity, fd, registration kind, current `ptls_t *`, and generation before calling `addHandle()`.
- Registration must run on the event-loop scheduler thread. Use `m_eventLoop.run(...)` plus `ZmBlock<bool>` or equivalent to report add failure to the TLS thread.
- Account for `ZiEventLoop::addHandle()` current behavior:
  - It invokes the recv callback synchronously before adding the handle node.
  - The recv callback must not call `delHandle()` inline during this priming call because deletion would run before the hash insert.
  - The recv callback should set/swap an `m_asyncResumeScheduled` flag and schedule a follow-up event-loop task that deletes the handle after registration returns, then schedules TLS resume.
- Use a no-op send callback.
- Route add failure through `m_errorFn` and disconnect.
- Readiness callback captures a safe link reference plus generation, TLS pointer, job pointer, and fd.
- Resume on TLS thread:
  - verify the active TLS pointer, generation, job pointer, fd, and pending state still match
  - ignore stale completions with debug/trace logging if available
  - clear active registration state before calling picotls again
  - call `handshake__()` with no new input
  - dispatch through `handleHandshakeResult_()`
  - allow another `PTLS_ERROR_ASYNC_OPERATION` by registering the next job.

### Phase 7: Async Cleanup, Retired Jobs, Reset, and Finalization Safety

- Add cleanup helpers:
  - `asyncCleanup_()` removes an active event-loop registration and clears active job state.
  - `asyncRetireTLS_()` handles a pending async job when the connection is disconnected or TLS is reset before the job is readable.
- Important constraint: `/home/count0/src/picotls/lib/openssl.c:1077` says users must not call `ptls_free()` until the async operation is complete. Therefore, `reset_tls_()` cannot simply deregister the fd and free the old `ptls_t` while an OpenSSL async job is still in flight.
- Safe reset/disconnect behavior:
  - Mark the active async job cancelled for handshake-resume purposes.
  - Transfer the old `ptls_t *`, fd, job pointer, and generation into a retire-only state owned by the link or by an engine retained object.
  - Keep a safe reference alive until the fd becomes readable.
  - When the fd is readable, deregister it, schedule TLS-thread cleanup, and call `ptls_free()` on the retired `ptls_t` without resuming the handshake.
  - Only then erase the retired state.
- `reset_tls_()` should:
  - increment generation before replacing active TLS state
  - retire pending async TLS if needed
  - otherwise free current `m_tls`
  - create the new `ptls_t`
  - reset record metadata, `m_handshook`, handshake props, and RX stream.
- `disconnect_()` and `disconnected_()` should:
  - mark pending async resume as cancelled
  - remove active resume registration or convert it to retire-only completion watching
  - prevent stale completion from calling picotls on the new or freed TLS state.
- Link destructor should defensively clean active registration and free only completed/retired-safe TLS state.
- `Engine::final()` should stop the event loop only after active/retired async registrations are no longer needed, or route a fatal diagnostic through `m_errorFn` if finalization is attempted with unresolvable pending async work.

### Phase 8: Migrate In-Repo Callers

- Direct callers:
  - `ztls/example/ZtlsClient.cc`
  - `ztls/example/ZtlsServer.cc`
  - `ztls/test/ZtlsBufHookTest.cc`
  - `zhttp/test/zhttpclient.cc`
- Wrapper/config-driven callers:
  - `zws/src/Zws.hh`
  - `zrest/src/Zrest.hh`
  - `zcmd/src/ZtelClient.hh`
- Example migrations:

```c++
ZuCSpan alpn[] = { "http/1.1" };
if (!app.init(Ztls::ClientParams(&mx, "3", alpn).caPath(ca))) {
  // existing error path
}
```

```c++
if (!server.init(
    Ztls::ServerParams(&mx, "3", alpn)
      .certPath(cert_path)
      .keyPath(key_path)
      .cacheTimeout(timeout))) {
  // existing error path
}
```

- Config wrappers:
  - `thread` maps to the params constructor.
  - `caPath` maps to `.caPath(...)`.
  - existing client cert/key config, if present, maps to inherited `.certPath(...)` and `.keyPath(...)`.
  - optional `asyncThread` maps to `.asyncThread(...)` where exposed.
  - Preserve wrapper `init` signatures unless local callers already support boolean init failure. If a wrapper keeps `void init`, it should still check the returned bool and use the existing failure/logging convention instead of silently ignoring failed TLS initialization.
- Migration note: the old positional server `cacheMax` argument was unused and is intentionally absent from `ServerParams`.

### Phase 9: Tests and Verification

- Add `ztls/test/ZtlsAsyncTest.cc`.
- Update `ztls/test/Makefile.am`:

```make
noinst_PROGRAMS = ZtlsPKTest ZtlsBufHookTest ZtlsAsyncTest
ZtlsAsyncTest_SOURCES = ZtlsAsyncTest.cc
```

- Reuse the `ZtlsBufHookTest` style for in-process client/server handshakes and log capture.
- Use fake/mock signers rather than patching picotls for tests:
  - fd-backed fake server signer returns `PTLS_ERROR_ASYNC_OPERATION` on first call and installs a `ptls_async_job_t`.
  - after fd readiness, resumed call delegates to a real signer or emits a deterministic valid signature if test scaffolding can do so safely.
  - variants expose no `get_fd`, return an invalid fd, or signal after disconnect/reset.
- Add or update tests for:
  - all migrated examples/tests compile with no positional init overloads.
  - synchronous client/server handshakes still work without `asyncThread`.
  - client mTLS with `certPath` and `keyPath` still works synchronously.
  - client mTLS with `asyncThread` configured remains synchronous and does not request OpenSSL async signing.
  - normal server cert/key configuration still works.
  - valid Unix `asyncThread` starts/stops the event loop.
  - `_WIN32` async configuration is rejected at init with the documented diagnostic.
  - async thread equal to TLS thread is rejected.
  - async thread equal to `rxThread` or `txThread` is rejected.
  - invalid/missing async thread is rejected.
  - non-isolated async thread is rejected.
  - server async signing resumes after fd-backed fake job readiness.
  - callback-only fake jobs fail with the expected unsupported diagnostic.
  - invalid fd fails with the expected diagnostic before registration.
  - `ZiEventLoop::addHandle()` Unix failure is observable after the epoll error-checking fix.
  - disconnect while async is pending does not resume the handshake, does not free `ptls_t` before the job is complete, and eventually releases the retired TLS state.
  - reset/reconnect while async is pending ignores stale completion by generation check.
  - `errorFn(...)` captures event-loop start/add/runtime failures.
  - `cacheTimeout` still controls `ctx->ticket_lifetime`.
  - removed `cacheMax` is covered by API compile tests and migration notes.
- Suggested verification commands:
  - `make -j`
  - `./ztls/test/ZtlsBufHookTest`
  - `./ztls/test/ZtlsAsyncTest`
  - compile affected wrapper targets for `zws`, `zrest`, `zcmd`, and `zhttp`.

## Code References to Impacted Code

- `ztls/src/Ztls.hh:12` - add any needed includes such as `ZiEventLoop.hh`.
- `ztls/src/Ztls.hh:54` - add public params types near existing `Ztls` aliases.
- `ztls/src/Ztls.hh:163` - handshake properties remain TLS-thread-only.
- `ztls/src/Ztls.hh:206` - network-input handshake dispatch must use centralized result handling.
- `ztls/src/Ztls.hh:214` - current `Link::handshake_()` result handling is replaced.
- `ztls/src/Ztls.hh:218` - disabled async block is replaced with real fd-backed handling.
- `ztls/src/Ztls.hh:238` - `handshook()` becomes guarded completion helper.
- `ztls/src/Ztls.hh:304` - `handshake__()` remains the low-level picotls wrapper.
- `ztls/src/Ztls.hh:386` - `disconnected_()` must clean up or retire pending async state before reset.
- `ztls/src/Ztls.hh:544` - `disconnect_()` must cancel active async resume and avoid stale callbacks.
- `ztls/src/Ztls.hh:576` - `reset_tls_()` must increment generation and retire pending async TLS before replacement.
- `ztls/src/Ztls.hh:717` - `CliLink::connected_()` must dispatch initial ClientHello result through the common handler.
- `ztls/src/Ztls.hh:835` - current public templated engine init is replaced by params-based public API plus private helper.
- `ztls/src/Ztls.hh:980` - `init_alpn_()` remains the common ALPN copy path.
- `ztls/src/Ztls.hh:1084` - current client positional init declaration is replaced with `ClientParams`.
- `ztls/src/Ztls.hh:1099` - current client init implementation migrates to params, including client mTLS validation.
- `ztls/src/Ztls.hh:1192` - current server positional init declaration is replaced with `ServerParams`.
- `ztls/src/Ztls.hh:1195` - `cacheMax` is removed.
- `ztls/src/Ztls.hh:1249` - current server init implementation migrates to params.
- `ztls/src/Ztls.hh:1274` - existing ticket lifetime assignment is preserved through `cacheTimeout`.
- `ztls/src/ZtlsBackend.hh:109` - add async signer facade.
- `ztls/src/ZtlsOpenSSL.cc:39` - `SignCert` wraps `ptls_openssl_sign_certificate_t`.
- `ztls/src/ZtlsOpenSSL.cc:750` - signer construction remains synchronous by default.
- `ztls/src/ZtlsOpenSSL.cc:768` - sign callback accessor remains the context hook.
- `zi/src/ZiEventLoop.hh:109` - event loop init/start/stop/add APIs used by `Engine`.
- `zi/src/ZiEventLoop.hh:133` - `addHandle` is the registration API for picotls OpenSSL fds.
- `zi/src/ZiEventLoop.cc:240` - check Unix `addSocket` `epoll_ctl()` failure.
- `zi/src/ZiEventLoop.cc:271` - account for immediate send/recv priming during socket registration.
- `zi/src/ZiEventLoop.cc:337` - check Unix `addHandle` `epoll_ctl()` failure.
- `zi/src/ZiEventLoop.cc:361` - account for immediate send/recv priming during handle registration.
- `zdb_pq/src/ZdbPQ.cc:65` - comparable validation pattern for dedicated event-loop scheduler configuration.
- `zdb_pq/src/ZdbPQ.cc:114` - comparable event-loop init/start pattern.
- `zdb_pq/src/ZdbPQ.cc:548` - comparable stop-on-start-failure cleanup pattern.
- `zws/src/Zws.hh:241` - wrapper client init call migrates to `ClientParams`.
- `zrest/src/Zrest.hh:516` - wrapper client init call migrates to `ClientParams`.
- `zcmd/src/ZtelClient.hh:424` - wrapper client init call migrates to `ClientParams`.
- `zhttp/test/zhttpclient.cc:131` - direct client init call migrates to `ClientParams`.
- `ztls/example/ZtlsClient.cc:221` - example client init call migrates to `ClientParams`.
- `ztls/example/ZtlsServer.cc:161` - example server init call migrates to `ServerParams`.
- `ztls/test/ZtlsBufHookTest.cc:316` - test server init call migrates to `ServerParams`.
- `ztls/test/ZtlsBufHookTest.cc:322` - test client init call migrates to `ClientParams`.
- `/home/count0/src/picotls/include/picotls.h:772` - async job contract.
- `/home/count0/src/picotls/include/picotls/openssl.h:193` - OpenSSL async signer flag.
- `/home/count0/src/picotls/lib/picotls.c:3233` - local picotls server-only async job slot.
- `/home/count0/src/picotls/lib/picotls.c:5536` - local `ptls_get_async_job()` returns server async job.
- `/home/count0/src/picotls/lib/openssl.c:1077` - `ptls_free()` safety constraint while async OpenSSL jobs are pending.

## Detailed Test Plan

- `ZtlsParamsAPITest` or a params section inside `ZtlsAsyncTest`:
  - Construct `ClientParams` and `ServerParams` with temporary ALPN arrays and strings, call init, destroy temporaries, and verify synchronous handshakes still work.
  - Verify null `mx`, invalid TLS `thread`, out-of-range numeric TLS `thread`, stopped multiplexer, invalid `asyncThread`, equal TLS/async threads, async thread equal to rx/tx thread, and non-isolated async thread diagnostics.
  - Verify client cert/key XOR validation.
  - Verify missing server cert/key validation.
  - Verify `_WIN32` async rejection when built on Windows.
- `ZtlsAsyncTest`:
  - Server fd-backed fake async signer: first handshake pauses, event fd is registered, test signals completion, resume succeeds, and `connected()` is called once.
  - Callback-only fake job: returns async job with no `get_fd`; expect explicit unsupported diagnostic and disconnect.
  - Invalid-fd fake job: expect explicit invalid fd diagnostic and disconnect.
  - Add failure path: use an invalid fd or test hook to verify `addHandle` failure is routed through `errorFn`.
  - Disconnect pending: start async, disconnect before readiness, signal fd, verify stale resume does not call picotls and retired TLS is freed only after completion.
  - Reset/reconnect pending: start async, reset TLS, signal old fd, verify generation mismatch ignores stale completion and new TLS state remains valid.
  - Client mTLS with `asyncThread`: verify the event loop can be configured but client signing remains synchronous and no client async job is requested.
- Existing tests/examples:
  - Update `ZtlsBufHookTest` to params API and keep existing buffer hook coverage unchanged.
  - Update examples and `zhttpclient` so compile coverage proves old positional overloads are gone.
  - Update wrapper compile targets for `zws`, `zrest`, and `zcmd`.
- Build verification:
  - `make -j`
  - direct execution of `./ztls/test/ZtlsBufHookTest`
  - direct execution of `./ztls/test/ZtlsAsyncTest`.

## Options and Open Questions

No unresolved product open questions remain from `plan.md`.

Resolved decisions:
- Client-side async signing is out of scope for this iteration. Client mTLS remains supported synchronously through `ClientParams::certPath(...).keyPath(...)`.
- Do not patch or upgrade `/home/count0/src/picotls` for client async support in this work.
- Disable async completion under `_WIN32` with conditional compilation and a clear comment/diagnostic because picotls exposes an `int` fd and Windows `HANDLE` is pointer-sized.
- Use fake/mock async signers for deterministic tests.
- Remove `cacheMax` from the new public API because it is unused in the current picotls-backed server implementation.

Residual implementation risks to manage in the phases above:
- The local OpenSSL async backend requires the user not to call `ptls_free()` before async completion. The implementation must include retired TLS handling; a simple cleanup-before-free approach is unsafe.
- `ZiEventLoop::addHandle()` currently primes callbacks synchronously during registration. The `Ztls` readiness callback must schedule deletion/resume rather than deleting inline during registration.
- Unix `ZiEventLoop::addSocket()` and `addHandle()` currently ignore `epoll_ctl()` add failures. The plan includes a narrow robustness fix so diagnostics and tests can observe registration failures.
