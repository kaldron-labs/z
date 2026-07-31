# Async Resolver Plan

## Goal

Integrate `c-ares` as the `zi` asynchronous DNS backend, wrapped by
`ZiEventLoop`, with process-wide singleton lifecycle similar to `ZiLog`.
Applications may explicitly initialize, start, stop, and finalize the resolver,
but first use should default-initialize and start it on demand like `ZiLog`.
The singleton must also adopt the carefully-crafted `~ZiLog` cleanup model:
at `atexit`, singleton destruction idempotently stops the resolver, cancels or
drains outstanding work, and joins its owned thread/event-loop path without
requiring explicit application teardown.

The resolver must support runtime enable/disable of IPv6 so deployments can keep
IPv4-only behavior without rebuilds.

`GUIDELINES.md` applies to this work. Keep the resolver performance-oriented,
allocation-conscious, and idiomatic to the Z framework; do not add generic C++
abstractions where a direct c-ares/Z wrapper is clearer and faster.

Verification scope follows the current top-level `Makefile.am`.  Update source
where APIs require it, including Windows-specific branches and work-in-progress
modules outside active top-level `SUBDIRS`, but do not require tests, link
checks, or library validation for those paths.  Examples include `zcmd`, `zrest`,
`zws`, `zproxy`, and similar WIP trees, which are not expected to build unless
they are explicitly re-enabled and declared buildable.

`ip6.md` precedes this plan. Do not start the async resolver replacement until
the required IPv6 slices in `ip6.md` are complete and green. This plan assumes
`ZiIP`, `ZiSockAddr`, current resolver IPv6 behavior, and dependent socket paths
are already family-aware; do not add resolver-local compatibility shims for gaps
that belong in the IPv6 plan.

After c-ares is introduced, the old blocking `ZiResolver.{hh,cc}`
implementation is deleted. The new files may keep the same public header/source
names, but all old namespace-function call sites must be updated to the new
singleton/callback API or moved to the appropriate higher-level module.

## Build Integration

Add `c-ares` as a required `zi` dependency through pkg-config:

- In `configure.ac`, add:
  ```m4
  PKG_CHECK_MODULES([CARES], [libcares], [], [dnl
    AC_MSG_ERROR([libcares required])
  ])
  AC_SUBST(CARES_LIBS)
  AC_SUBST(CARES_CFLAGS)
  CPPFLAGS="$CARES_CFLAGS $CPPFLAGS"
  Z_IO_LIBS="$Z_IO_LIBS $CARES_LIBS"
  ```
- Place this after `Z_IO_LIBS` is initialized for the target OS and before
  `AC_SUBST(Z_IO_LIBS)`.
- Remove `res_nquery`/`res_query` checks in the same slice that deletes the old
  resolver implementation.
- In `zi/src/Makefile.am`, add any new resolver support headers/sources to
  `pkginclude_HEADERS` and `libZi_la_SOURCES`.
- Verify Linux pkg-config names and link flags.  Keep the Windows link path
  source/configuration consistent with Winsock libraries already used by `zi`,
  and add c-ares link flags through `CARES_LIBS`, not hard-coded locally, but
  exclude msys2/mingw link validation from this gate while Windows-only
  libraries are outside the active top-level build.

## Public Shape

Delete the current synchronous `ZiResolver` namespace implementation and replace
it with a process-wide `ZiResolver` singleton. Do not introduce a separate
`ZiCares` or `ZiResolverAsync` API; the asynchronous resolver is the new
resolver API.

Use an API shape close to `ZiLog`:

- `static ZiResolver *instance();`
- `static void init();`
- `static void init(ZiResolverParams);`
- `static void start(ZiEvent::StartFn = {});`
- `static void stop(ZiEvent::StopFn = {});`
- `static void final();`

`init()` with no params applies default parameters. `init(params)` overrides
those defaults before the resolver starts. `start()` calls default `init()` if
the app has not already initialized the singleton. First query follows the
`ZiLog::log__()` pattern: under the resolver lock, initialize defaults if
needed, start if needed, then enqueue the query.

`stop()` must drain/cancel active DNS queries and remove all c-ares sockets from
the event loop before invoking the stop callback. `final()` releases c-ares
state and clears params after the resolver is stopped. Explicit app lifecycle is
preferred for daemons, but not required for simple clients/tests.

Construct the singleton at the same cleanup level as `ZiLog`:
`ZmSingletonCleanup<ZmCleanup::Library>`. Any resolver-owned scheduler/thread,
event loop, timer, c-ares channel, and queued query state must be safe to tear
down from the destructor. The destructor should mirror `~ZiLog`: if still
started, mark EOF/stopping, wake the owned event loop, and join unless
destruction is running on the owned resolver thread. It must be idempotent with
explicit `stop()`/`final()` so an application can configure the resolver at
startup, for example to disable IPv6, while simple programs can call
`resolve()` without any init/start/stop/final ceremony.

## Configuration

Introduce `ZiResolverParams` as a compact value type. Required fields:

- contained `ZmSchedParams m_scheduler`, exposed with `scheduler()` accessors
  like `ZiMxParams`
- `bool ipv6 = true`
- `bool ipv4 = true`
- optional timeout/retry knobs mapped directly to c-ares options
- optional server/search/domain configuration if needed by current deployment

`ZiResolverParams` should contain `ZmSchedParams` the same way `ZiMxParams`
does: a concrete `m_scheduler` member, a `scheduler()` getter returning
`ZmSchedParams &`, and a templated `scheduler(L &&)` mutator that applies a
lambda to the contained scheduler params. This lets applications pass scheduler
tuning to `ZiResolver::init` alongside resolver-specific configuration for
queue sizing, priority, affinity, and related settings.

`ZiResolver` owns and controls the resulting dedicated `ZmScheduler`, paired
with its `ZiEventLoop`. Before constructing/initializing the scheduler,
`ZiResolver` must force the contained params to `id("ZiResolver")` and
`nThreads(1)`, so the resolver contributes exactly one worker thread plus the
scheduler control/timer thread. The `ZiEventLoop` uses that scheduler with
worker `sid` 1. Scheduler selection and `sid` are not public resolver
parameters.

Audit the `sid` boundary before implementation: current `ZmScheduler`
thread-specific `push`/`run`/`invoke` APIs use 1-based worker IDs. The resolver
therefore posts all event-loop and c-ares work to worker `sid` 1. Scheduler
timer APIs reserve `sid == 0` for any worker/control-thread dispatch. Do not
confuse that reserved timer/control `sid` with the resolver event-loop owner:
c-ares socket processing and resolver mutable state stay on worker `sid` 1.

The `ipv6` flag controls runtime AAAA behavior:

- `ipv6 == false`: do not issue AAAA queries and do not emit `ZiIPType::V6`
  results from async resolve APIs.
- `ipv4 == false`: do not issue A queries and do not emit `ZiIPType::V4`.
- reject `ipv4 == false && ipv6 == false` at `init()`.

Keep address ordering explicit. The first implementation should preserve
query-completion order unless callers need policy. Add ordering policy only as
a named config field.

## Event Loop Ownership

`ZiResolver` owns a dedicated `ZmScheduler`, a `ZiEventLoop` member, and an
`ares_channel`.

Lifecycle:

1. `init()`
   - choose default params;
   - default failFn calls `ZiLOG(Fatal, ...`
   - call the same internal initializer as `init(params, failFn)`.
2. `init(params, ZiEvent::FailFn failFn)`
   - store params;
   - initialize the owned `ZmScheduler` from `params.scheduler()`, overriding
     its `id` and `nThreads` as described above;
   - call `ares_library_init`;
   - create/configure `ares_channel`;
   - initialize owned `ZiEventLoop` with the owned scheduler, `sid` 1, and
     `params.failFn`.
3. `start(fn)`
   - call default `init()` if not already initialized;
   - start the owned `ZmScheduler`;
   - start the owned `ZiEventLoop`;
   - do not accept queries until the start callback succeeds.
4. Query activity
   - c-ares socket-state callback adds/removes sockets from `ZiEventLoop`;
   - socket recv/send callbacks call `ares_process_fd`;
   - c-ares timeout processing may be scheduled by the scheduler control/timer
     thread, but the actual c-ares processing must run on the event-loop worker
     context (`sid` 1), not on scheduler `sid` 0.
5. `stop(fn)`
   - mark stopping;
   - cancel outstanding c-ares queries;
   - remove all c-ares sockets from the `ZiEventLoop`;
   - stop the event loop;
   - stop the owned `ZmScheduler`;
   - invoke `fn`.
6. `final()`
   - destroy the channel;
   - call `ares_library_cleanup` when this singleton is the owner of library
     init;
   - call `ZiEventLoop::final()`;
   - finalize/reset the owned scheduler;
   - clear stored params and state.
7. `~ZiResolver()`
   - if the singleton is still started, perform the same idempotent shutdown as
     `stop()`/`final()`;
   - wake and join the owned thread/event-loop path, avoiding self-join during
     process exit;
   - tolerate prior explicit `stop()` and `final()` without double-destroying
     c-ares or event-loop state.

All c-ares callbacks must execute on the event-loop scheduler context (`sid` 1).
Public query methods called from other threads should ensure default init/start
under lock, then post to that context and capture only value params and callback
handles.

## Socket Integration

Use c-ares socket-state callback as the single source of truth:

- on readable/writable interest, call `ZiEventLoop::addSocket`;
- on no interest, call `ZiEventLoop::delSocket`;
- call `ZiEventLoop::unblock` only if c-ares did not already make the socket
  nonblocking for the current platform;
- map send-ready to `ares_process_fd(channel, socket, ARES_SOCKET_BAD)`;
- map recv-ready to `ares_process_fd(channel, ARES_SOCKET_BAD, socket)`;
- if both read and write are ready, process both without allocating wrapper
  work.

Keep the c-ares socket boundary platform-neutral:

- c-ares uses `ares_socket_t`; convert only at the `ZiEventLoop` boundary to
  `Zi::Socket`, which is `int` on Unix and `SOCKET` on Windows;
- do not assume negative file descriptors on Windows; use `Zi::nullSocket()`;
- ensure c-ares library initialization covers any required Winsock startup on
  Windows, or explicitly retain a minimal Windows startup owner if c-ares on the
  supported msys2/mingw build does not do so;
- map c-ares and socket errors to `ZeError`/`ZeException` without mixing Unix
  `errno` and Windows `WSAGetLastError` paths.

Timers:

- use `ares_timeout` to compute the next deadline after every c-ares state
  change and callback;
- do not run c-ares callbacks on the scheduler timer/control thread; scheduler
  timer sid 0 remains reserved for scheduler timer/control dispatch;
- if a resolver timeout mechanism is added, it must wake/process c-ares from
  worker `sid` 1;
- cancel any resolver-owned timeout state during `stop()` and `final()` using
  the normal scheduler teardown pattern.

Do not integrate c-ares sockets directly into `ZiMultiplex`. Resolver sockets
are control-plane work and belong under `ZiEventLoop`.

## Query APIs

Provide async query APIs that return through `ZmFn` callbacks:

- `resolve(host, ResolveFn, FailFn = {})`
  - emits one or more `ZiIP` values, respecting `ipv4`/`ipv6`;
  - use c-ares A/AAAA query APIs directly rather than blocking
    `getaddrinfo()`;
  - build `ZiIP` from native `in_addr`/`in6_addr` without redundant byte swaps.
- `name(ZiIP, NameFn, FailFn = {})`
  - reverse lookup with the active `ZiIP::type()`.
- generic DNS query APIs that expose the full c-ares capability set needed by Z
  applications, not just address and reverse lookup:
  - support raw query-by-type/class so future callers can query records such as
    `TXT`, `MX`, `SRV`, `CAA`, `SVCB`, and `HTTPS` without changing
    `ZiResolver`;
  - provide typed convenience parsers only where they are DNS-generic and belong
    in `zi`;
  - return DNS response data through Z value/buffer types and callbacks without
    hidden heap churn.
- `cancel(handle)` if c-ares query cancellation needs caller-visible control.

c-ares host/query APIs take narrow DNS names. Keep the public Z API consistent
with existing `Zi` string conventions, but normalize at the resolver boundary:
on Windows do not pass `Zi::Hostname` wide strings directly to c-ares; convert
DNS names to a narrow `Zi::Name`/`ZtString` representation suitable for c-ares
and keep that conversion outside hot callback paths.

Move HTTP-specific HTTPS/SVCB and HTTP/3 endpoint policy out of `ZiResolver` and
into `zhttp`. `ZiResolver` should remain a lean interface to c-ares. It may
provide generic DNS response access for `HTTPS` and `SVCB` records, but
interpreting ALPN, aliases, `ipv4hint`/`ipv6hint`, and fallback policy is a
`zhttp` concern.

Replace existing synchronous `ZiResolver::{resolve,name,https,http3}` call sites
with the async callback APIs or with the new `zhttp` HTTPS/HTTP3 resolver helper
as part of this migration. Remove the old direct blocking resolver
implementation; do not preserve generic blocking `ZiResolver` shims. The only
intentional blocking path is the `ZiIP` convenience wrapper described below.

Preserve the current blocking `ZiIP(S &&s)` string constructor and string
assignment behavior. These call `ZiIP::resolve`, which must explicitly block
with `ZmBlock` on async `ZiResolver::resolve`; the resolver thread calls back
with either the resolved `ZiIP` or an error, and the blocked caller resumes with
the same throwing/error behavior as today. Add comments in `ZiIP` warning that
the string constructor, string assignment, and `ZiIP::resolve` block.

## State and Lifetime

Represent each active query as an intrusive object owned by the resolver until
completion or cancellation:

- store callback(s), host/IP, query type, and result accumulation inline;
- do not store raw back-pointers to the singleton; use `ZiResolver::instance()`
  where singleton access is needed;
- do not reference-count the process singleton from query nodes, callbacks,
  handles, or other `ZiResolver`-dependent code;
- remove query nodes deterministically when c-ares completes or cancellation is
  processed.

Do not scan long-lived query containers for cleanup. Completion/cancellation
must delete from owner containers immediately.

## IPv6 Runtime Behavior

When IPv6 is enabled:

- issue AAAA queries alongside A queries for `resolve`;
- return `ZiIP` values tagged `ZiIPType::V6`;
- make `ipv6hint` available to `zhttp` through generic HTTPS/SVCB DNS response
  data.

When IPv6 is disabled:

- suppress AAAA queries;
- leave `ipv6hint` endpoint suppression to `zhttp` policy while keeping generic
  DNS query responses faithful to the wire response;
- keep IPv4 result behavior and ordering as close to the current resolver as
  possible.

This flag is runtime configuration, not a compile-time feature macro.

## Failure Handling

- Convert c-ares status codes to `ZeError`/`ZeException` consistently.
- Per-query failures call the query fail callback if provided.
- Event-loop or c-ares channel failures call the process-level `failFn`.
- During `stop()`, cancellation should not log noisy warnings for expected
  teardown completions.

## Tests

Add focused tests under `zi/test`:

- lifecycle: `init`, `start`, `stop`, `final`;
- lazy lifecycle: first `resolve()` default-initializes and starts the singleton;
- repeated start/stop without leaked sockets/timers;
- IPv4-only mode emits A records and suppresses AAAA;
- IPv6-enabled mode can emit AAAA for a controlled test host or injected c-ares
  response path;
- generic query coverage for at least one non-address record type, e.g. `TXT`;
- cancellation before completion;
- reverse lookup for IPv4 and IPv6 where platform/test environment supports it;
- no callbacks after `stop()` returns.

Avoid public DNS dependence in unit tests where possible. Prefer a local fake
DNS server socket driven through `ZiEventLoop`, or isolate parser tests with
synthetic DNS payloads.

Verification gates:

- `./z.config -c /opt/z` after adding pkg-config dependency;
- `make -C zi/src -j8`;
- `make -C zi/test -j8`;
- targeted resolver/event-loop tests;
- top-level `make -j8` before merging because `Z_IO_LIBS` affects shared
  library linkage.  This covers active `SUBDIRS` only; WIP modules outside that
  set and Windows-only libraries are excluded unless re-enabled and expected to
  build.

## Required Work Slices

These slices are prescriptive and must land in order. Begin only after the
`ip6.md` required slices are complete. Each slice must keep the tree buildable,
avoid compatibility shims, and preserve the singleton lifecycle invariants
already introduced by earlier slices.

1. Build integration
   - Add `PKG_CHECK_MODULES([CARES], [libcares])`.
   - Link `CARES_LIBS` into `Z_IO_LIBS` and add any new resolver support files
     to `zi/src/Makefile.am`.
   - Acceptance:
     - Configure fails clearly when libcares is absent.
     - `libZi` links with c-ares through the normal `Z_IO_LIBS` path.
     - Linux configure/build paths resolve c-ares without local hard-coded
       libraries.  Windows source/configuration keeps Winsock and c-ares linkage
       in the normal variables, but msys2/mingw configure/build validation is
       excluded from this gate.
     - Gate: `./z.config -c /opt/z && make -C zi/src -j8`.
   - Unlocks: resolver source can include and link c-ares without local build
     hacks.

2. Singleton shell and params
   - Delete the old `ZiResolver.{hh,cc}` implementation and replace it with the
     process-wide `ZiResolver` singleton API.
   - Remove `res_nquery`/`res_query` configure checks once the old implementation
     is gone.
   - Add `ZiResolverParams` with the `ZiMxParams`-style contained
     `ZmSchedParams m_scheduler`.
   - Use `ZmSingleton<ZiResolver, ZmSingletonCtor<...,
     ZmSingletonCleanup<ZmCleanup::Library>>>` as in `ZiLog`.
   - Implement lazy default init/start, explicit `init`/`start`/`stop`/`final`,
     and `~ZiResolver` idempotent shutdown.
   - Acceptance:
     - `init()` applies defaults and `init(params)` stores resolver params before
       start.
     - First query default-initializes and starts the singleton under lock.
     - Repeated `start`/`stop`/`final` are idempotent.
     - Destructor cleanup joins the owned scheduler/event-loop path and avoids
       self-join during process exit.
     - Gate: focused lifecycle tests under `zi/test`.
   - Unlocks: c-ares channel and query work can rely on stable process-wide
     lifetime.

3. Owned scheduler and event loop
   - Add the owned one-thread `ZmScheduler`/`ZiEventLoop` pair.
   - Force scheduler params to `id("ZiResolver")` and `nThreads(1)` while
     preserving caller-provided scheduler tuning that does not conflict with
     those invariants.
   - Initialize `ZiEventLoop` with the owned scheduler and `sid` 1.
   - Keep all resolver-owned mutable c-ares/socket/query state on that scheduler
     context; public entry points from other threads post value data to `sid` 1.
   - Acceptance:
     - Starting the resolver creates exactly one scheduler worker thread, plus
       the scheduler control/timer thread.
     - Stop/final drains the event loop before scheduler teardown.
     - Tests use `ZmBlock`/`ZmSemaphore`, not sleeps or polling.
   - Unlocks: c-ares socket and timer callbacks have a single owner thread.

4. c-ares channel, sockets, and timers
   - Wrap `ares_channel` with socket-state callbacks, `ZiEventLoop` socket
     add/remove, and c-ares processing callbacks on worker `sid` 1. Scheduler
     timer/control handling on reserved `sid` 0 must not run c-ares callbacks
     directly.
   - Store active socket/query/timer state in resolver-owned intrusive nodes or
     members with explicit `ZmHeapID`/heap-aware containers where heap allocation
     is required.
   - If resolver-owned timeout state is added, follow the `GUIDELINES.md`
     teardown sequence: cancel/disarm timeout state, set teardown state, post a
     drain continuation on `sid` 1, then complete destruction.
   - Do not scan long-lived containers for cleanup; remove socket/query nodes
     deterministically on c-ares state changes and completions.
   - Acceptance:
     - Socket add/remove is driven only by c-ares socket-state callbacks.
     - `ares_socket_t` to `Zi::Socket` conversion is correct on Unix and remains
       source-correct for Windows.
     - Windows startup/error handling source is updated for the supported
       msys2/mingw c-ares build, but Windows build/test verification is excluded
       from this gate.
     - `stop()` cancels outstanding DNS work, removes all c-ares sockets, cancels
       timers, and invokes the stop callback only after late callbacks are
       drained.
     - No query callback fires after `stop()` returns.
     - Gate: fake/local DNS server or injected callback tests; no public DNS
       dependency.
   - Unlocks: query APIs can be layered over a deterministic event-loop backend.

5. General c-ares query API
   - Implement async A/AAAA `resolve` with runtime `ipv4`/`ipv6` gating.
   - Implement reverse lookup.
   - Implement raw query-by-type/class for the c-ares DNS surface. Use Z enum
     style for common DNS types/classes (`A`, `AAAA`, `PTR`, `TXT`, `MX`, `SRV`,
     `CAA`, `SVCB`, `HTTPS`, `IN`) and still allow numeric type/class values for
     records not yet named.
   - Return raw DNS response data through Z spans/arrays and callbacks. Use
     `ZtArray`/`ZtBuiltin`/`ZtLocalArray` or pooled buffers with heap IDs as
     appropriate; do not return STL strings/vectors or allocate hidden
     temporaries in hot paths.
   - Provide typed convenience parsers only for DNS-generic records that belong
     in `zi`. `TXT` is the first required non-address example.
   - Acceptance:
     - IPv4-only mode issues/emits only A records.
     - IPv6-enabled mode can issue/emits AAAA records.
     - Reverse lookup works for IPv4 and IPv6 where the platform/test fixture
       supports it.
     - Generic `TXT` query test proves the API is not address-only.
     - Cancellation before completion releases query ownership exactly once.
   - Unlocks: dependent modules can migrate off old `ZiResolver` APIs without
     losing DNS capabilities.

6. Blocking `ZiIP` conveniences
   - Preserve `ZiIP(S &&s)`, string assignment, and `ZiIP::resolve` as explicitly
     documented blocking conveniences implemented with `ZmBlock` over async
     `ZiResolver::resolve`.
   - Add comments in `ZiIP` warning callers that these functions block.
   - Do not call this blocking path from the resolver scheduler context; guard
     against self-deadlock with an assertion or documented precondition.
   - Acceptance:
     - String construction resolves numeric IPv4/IPv6 without DNS where possible.
     - Hostname string construction blocks and resumes with either the resolved
       `ZiIP` or the expected error.
     - Tests cover success, error, and cancellation/shutdown interaction.
   - Unlocks: source conveniences remain available while generic resolver users
     move to async APIs.

7. HTTP-specific migration to `zhttp`
   - Move HTTPS/SVCB parsing, HTTP/3 endpoint selection, `ipv4hint`/`ipv6hint`
     endpoint handling, alias following, and fallback policy into `zhttp`.
   - `ZiResolver` may expose raw/generic `HTTPS` and `SVCB` DNS response data, but
     it must not know about ALPN, HTTP/3 fallback, TLS host selection, or endpoint
     policy.
   - Acceptance:
     - No HTTP-specific structs or policies remain in `zi/src/ZiResolver.hh` or
       `zi/src/ZiResolver.cc`.
     - `zhttp` tests cover HTTPS/SVCB parsing, alias depth, hints, duplicate
       suppression, IPv4-only policy, IPv6-enabled policy, and HTTP/3 fallback.
   - Unlocks: the resolver is a lean c-ares interface and HTTP policy is owned by
     the HTTP module.

8. Caller migration and old resolver removal
   - Migrate all old namespace-function call sites to the replacement async
     `ZiResolver` API, the documented blocking `ZiIP` convenience path, or the
     new `zhttp` HTTPS/HTTP3 helper.
   - Delete the old direct blocking resolver implementation and do not preserve
     generic blocking `ZiResolver` shims.
   - Acceptance:
     - `rg "ZiResolver::https|ZiResolver::http3|res_query|res_nquery"` finds no
       live `zi` resolver dependency.
     - `rg "getaddrinfo|getnameinfo"` in generic resolver-dependent paths shows
       no remaining direct blocking resolver calls except platform-specific
       tests or explicitly documented fallback code.
     - Gate: `make -C zi/src -j8`, `make -C zi/test -j8`, targeted resolver and
       event-loop tests, then top-level `make -j8` before merging.  The
       top-level gate covers active `SUBDIRS` only.

## Final Acceptance Criteria

- `ZiResolver` is a process-wide singleton with `ZiLog`-style lazy init/start,
  explicit lifecycle, `ZmCleanup::Library` cleanup, and idempotent destructor
  teardown.
- The old blocking `ZiResolver.{hh,cc}` implementation is gone; all call sites
  are migrated.
- The resolver owns exactly one `ZmScheduler` worker thread, the scheduler
  control/timer thread, one `ZiEventLoop`, one c-ares channel, and
  deterministic timer/socket teardown.
- Generic c-ares query APIs support more than address and reverse lookup, with
  tested `TXT` coverage and raw type/class escape hatch for future records.
- Linux builds validate c-ares linkage, c-ares socket type conversion, and
  resolver scheduler `sid` behavior.  Windows-specific source paths keep
  Winsock startup/error handling and socket conversion correct, but
  msys2/mingw build/test validation is excluded while those libraries are not in
  the active top-level build.
- HTTP-specific HTTPS/SVCB and HTTP/3 policy lives in `zhttp`, not `zi`.
- `ZiIP` string construction/assignment blocking behavior is preserved,
  documented, and implemented with `ZmBlock` over async resolution.
- No query, socket, timer, or callback path reference-counts the singleton; no
  dependent code stores singleton back-pointers.
- Tests avoid public DNS dependencies, use deterministic synchronization, and
  cover lifecycle, cancellation, IPv4-only, IPv6-enabled, generic query,
  blocking convenience, and no-callback-after-stop behavior.
- `./z.config -c /opt/z`, `make -C zi/src -j8`, `make -C zi/test -j8`, targeted
  resolver/event-loop tests, and final top-level `make -j8` all pass with no
  warnings.
