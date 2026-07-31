# Zhttp application consolidation follow-up plan

## Problem statement

The first `zhttp.md` plan successfully moved native TCP, TLS, and QUIC
integration behind public `Zhttp` types.  Its initial follow-up audit found a
2,212-line `zhttp/test/zhttp.cc` containing a second client framework above
the normalized transports:

- approximately 375 lines of HTTPS/SVCB DNS parsing and H3 discovery;
- approximately 460 lines of URL, origin, Alt-Svc, redirect, request, and
  response-attempt state;
- approximately 345 lines of H1/H3-specific message building and parsing;
- approximately 400 lines of connection workers, reuse, retry, scheduling,
  engine lifecycle, and completion;
- approximately 200 lines of fallback and discovery-cache orchestration.

The same initial audit found a 707-line `zhttp/test/zhttpd.cc` that selected H1/H3 request
parsers and response builders, instantiates protocol-specific server/link
adapters, coordinates engine lifecycle, installs process/signal machinery, and
implements diagnostic waiting.  Its static-file routing, authorization, range,
MIME, redirect, and body-production behavior is legitimate workload-specific
request/response handling; the generic server/session machinery is not.

The executable boundary is exhaustive.  Both programs may retain only:

- CLI parsing;
- protocol-specific configuration;
- workload selection;
- workload-specific request/response handling;
- output-file handling;
- reporting.

Everything else must be supplied by `libZhttp` or an existing appropriate Z
Framework facility.  Reusable URL, discovery, message-selection,
request-attempt, pool, retry, redirect, fallback, server admission, session,
engine lifecycle, shutdown, and diagnostic scheduling mechanisms do not belong
in either program.

“Retained by an executable” means present anywhere in its program-only source
closure, not merely textually present in its `.cc` file.  The closure includes
private headers, inline/template helpers, generated fragments, and test-only
support compiled into that executable.  Moving a mechanism from `zhttp.cc` or
`zhttpd.cc` to `Zhttpd.hh` does not satisfy this plan.

This is a corrective follow-up to `zhttp.md`, not an H2 implementation plan.
Complete it before the H2 phases in `h2.md` depend on client pooling, fallback,
or the meaning of multiplexing.  Backward compatibility with the current
program-local machinery is not required; delete it as library facilities
replace it.

Apply root `AGENTS.md` and `GUIDELINES.md` throughout.  Preserve static
polymorphism on message data paths, owner-thread state, pooled buffers,
deterministic cleanup, bounded storage, exactly-once completion, and
allocation/copy discipline.

## Immediate defects to eliminate

The refactor is not complete merely when the file is shorter.  It must correct
these existing defects:

1. `PoolLink::H3` is derived from `Base::Multiplexed`.  This conflates a
   transport/session property with an HTTP version.  H2 is multiplexed but
   must never select H3 builders, parsers, close behavior, or diagnostics.
2. Default `--http3=prefer` processes requests serially and constructs,
   starts, stops, and finalizes fresh engines for individual attempts.
   `--jobs` therefore has different semantics among force, prefer, and disable
   modes.
3. HTTPS/SVCB discovery reduces resolved endpoints to a Boolean and log
   message.  The discovered target, port, address hints, and resolution result
   are not passed to the QUIC connection attempt.
4. An Alt-Svc response can cause the already completed request to be issued
   again over H3.  Alt-Svc learned from one response is routing information for
   subsequent eligible attempts; it must not silently duplicate a completed
   request.
5. URL parsing treats only `/` as the authority terminator, and relative
   redirect resolution and Alt-Svc parsing implement incomplete ad hoc
   grammars.
6. Discovery uses fixed arrays and silent truncation for endpoints, aliases,
   and records.  Bounds must be explicit configuration/policy and use
   appropriate Z containers.
7. Request processing calls an internal trailing-underscore disconnect API and
   contains separate H1/H3 send, parser-selection, EOF, reuse, and disconnect
   branches.
8. `processResponse()` contains redundant/unreachable completion logic.

Add a regression test for each defect before or with its repair.  Do not
preserve faulty behavior merely because the current example depends on it.

## Implementation gap-analysis checkpoint

The implementation must be audited against both the former application-local
code and the native `ztcp`, `ztls`, and `zquic` lifecycle implementations.
The comparison is behavioral, not textual: retain proven ordering and
diagnostic coverage, but do not retain duplicate application mechanisms.

As of the final GCC 16 release audit:

- `zhttp.cc` is 797 lines and `zhttpd.cc` is 424 lines; the executable-boundary
  manifest covers `zhttp.cc`, `zhttpd.cc`, and the 1,064-line static-workload
  helper `Zhttpd.hh`;
- the former client explicitly stopped admission, disconnected each H1 worker,
  waited for link completion, stopped engines, and then finalized; the former
  server stopped listeners, stopped engines, finalized them, and only then
  stopped the multiplex and logging facilities;
- `Zhttp::Agent`, `Service`, and `Engines` retain that required ordering:
  prevent admission, drain/cancel logical work, stop engines in reverse
  initialization order, finalize in reverse order, then release runtime,
  multiplex, and logging facilities;
- native TCP, TLS, and QUIC all implement the required Rx-to-Tx asynchronous
  engine drain, but the first H3 wrapper made `stop(done)` call blocking
  `stop()`.  H3 client session abort/drain and server logical disconnect must
  remain callback-driven before delegating to the native QUIC drain;
- H3 stores logical-link references in dynamically growing arrays.  GCC 16
  exposed that `ZtArray` reallocation destructively moved `ZmRef` elements and
  then `free_()` destroyed the old elements a second time.  Reallocation paths
  that subsequently call `free_()` use `moveElems<false>`, leaving every old
  element alive for exactly one destruction by `free_()`.  The
  `moveElems<false>` forward loops advance the source independently of the
  conditional destruction;
- `ZmObject::debug()` is a useful reference-count diagnostic, but its debug
  layout/timing can mask this relocation defect.  Acceptance therefore
  requires an optimized release regression that checks each retained
  object's reference count before and after array growth and cleanup;
- the direct transport lifecycle test remains the behavioral baseline, while
  normalized H1/H3 fixtures must prove the same callback counts and ordering.
  H3 additionally requires active-link synchronous and callback-form shutdown
  coverage;
- the boundary scan currently passes all six checks and proves that reusable
  URL, discovery, pool, parser/builder selection, lifecycle, and protocol
  machinery has not migrated back into either executable closure.

The subsequent lifecycle and policy audit added the following evidence and
repairs:

- URL parsing now validates reg-names, percent escapes, and bracketed IPv6
  literals; HTTPS/SVCB parsing accepts only matching answer-section owners,
  rejects malformed AliasMode parameters, and orders service records by
  priority and wire order;
- discovery resolver operations are injectable through a non-virtual
  context/function-pointer seam while production continues to use
  `ZiResolver`.  Deterministic tests traverse AliasMode across two DNS
  responses, resolve the selected ServiceMode target through ordered IPv4 and
  IPv6 callbacks, preserve hints and endpoint identity, reject alias loops,
  and suppress late cancellation;
- the Agent dispatches all request, discovery, cancellation, and stop state
  to the configured Rx owner shard.  Variable-sized discovery results cross
  shards in one heap object, and discovery requests, discovery posts, and
  normalized client/server links opt into `ZmObject::debug()` reference
  tracing in debug builds;
- queued and active cancellation is a public Agent operation.  A deterministic
  stalled-loopback regression proves cancellation while a native TCP connect
  is pending, exactly-once completion, and `Cancelled` classification;
- generic TCP/TLS server idle-timer teardown now disables and deletes the
  timer, drains a continuation on the timer's Rx owner shard, and only then
  releases admission and application ownership.  Service shutdown drains
  engines first and posts a final Rx barrier before its completion callback;
- `ServiceConfig::idleTimeout` is applied to HTTP/3 through QUIC's native
  `max_idle_timeout` transport parameter, converting seconds to milliseconds;
  an explicit QUIC value remains authoritative;
- the release application matrix passes with ten requests and concurrency
  five in HTTP, HTTPS, H3 force, and H3 prefer modes over IPv4, plus HTTP,
  HTTPS, and H3 over IPv6.  Direct lifecycle tests retain exact callback
  counts and disconnect-before-stop-before-final ordering for TCP, TLS, and
  QUIC;
- the final serial GCC 16 release suite passes all 23 programs and 188 tests,
  plus all six executable-boundary checks;
- an isolated GCC 16 `-DZDEBUG` build using `ZmObject::debug()` passes the
  cancellation/timeout test and the complete TCP/TLS/QUIC lifecycle matrix.
  H3 links and timer/discovery owners use `ZmObject::debug()`; normalized
  TCP/TLS links derive from `ZmPolymorph` and therefore use
  `ZmPolymorph::debug()` instead.
- the Agent policy fixtures now cover typed selection, attempt failure,
  retry, redirect, unsafe replay refusal, retry limits, cancellation,
  timeout, H3-to-TLS fallback, stable request identity, and distinct attempt
  identity;
- service-level HTTP/3 idle expiry now establishes a real logical H3 link,
  observes the native QUIC idle disconnect, verifies service admission
  release, and tears down in the required order;
- the H3 idle and Agent fallback cases are separate test programs.  Combining
  their large template closures under `-DZDEBUG` instantiated at least 1,024
  distinct `ZmSpecific` allocators before `main`; `pthread_key_create()`
  returned `EAGAIN`, leaving OpenSSL unable to allocate its thread-local
  provider/RNG state.  The resulting `RAND_bytes()` abort was neither a QUIC
  failure nor a GCC/Clang difference.  Splitting the unrelated fixtures keeps
  `ZmObject::debug()` enabled on logical H3 links, preserves `ZmSpecific`, and
  reduces compiler peak memory;
- the split fixtures guard partial startup and always unwind initialized
  layers in order: stop the Agent/client, stop Service engines/listeners,
  finalize initialized owners, stop the multiplexer, and reap the exact child
  server PID.  No failure path continues into connection work after a failed
  `init` or `start`;
- Clang 22 `-O0 -g -DZDEBUG` passes the Agent policy, service H3 idle, and
  H3-to-TLS fallback fixtures under `prove`; focused Valgrind memcheck runs
  for idle and fallback report zero memory errors and zero definitely lost
  bytes.  Focused Clang ASAN/LSAN builds of the two real H3 fixtures also pass
  with leak detection and fail-fast enabled;
- the complete isolated Clang 22 `-O0 -g -DZDEBUG` Zhttp build succeeds
  serially under a 6 GiB address-space ceiling.  Its complete 23-program,
  187-test manifest passes, followed by all six executable-boundary checks.
  The run includes the real H3 idle and H3-to-TLS fallback fixtures with
  `ZmObject::debug()` enabled on logical H3 links;
- five consecutive Clang debug runs of the nine teardown-heavy lifecycle,
  transport, H1/H3 engine, cancellation, runtime, H3 idle, and fallback
  programs pass all 215 tests.  Each run starts and drains fresh native
  engines and reaps its exact fallback-server child;
- a complete isolated Clang 22 `-O1 -g -DZDEBUG` ASAN/LSAN build succeeds
  serially.  With leak detection and fail-fast enabled, plus 3 GiB soft and
  4 GiB hard RSS limits, all 23 programs and 187 tests pass, followed by all
  six executable-boundary checks.  Only the repository's existing picotls
  certificate-store suppressions are used;
- the concluding lifecycle audit split executable initialization from startup
  and made both programs explicitly call idempotent `stop()` before `final()`
  after a failed `start()`.  The affected debug and ASAN/LSAN application,
  server, matrix, compatibility, and fallback subset passes all six programs
  and 81 tests; the boundary audit remains six of six;
- the first complete Valgrind pass exposed two application-test teardown/data
  defects rather than a framework-global shutdown defect.  Parser and QPACK
  negative cases lazily start `ZiLog`; both tests now stop the logger
  explicitly before process exit.  QPACK stress construction now converts its
  formatted arrays through NUL-terminated pointers, so spans exclude
  uninitialized bytes after the terminator.  Both focused tests pass strict
  memcheck with definite-leak and uninitialized-read failures enabled;
- the same Valgrind pass found that `ZiDir::read()` assigned the complete
  fixed-width `dirent::d_name` array, including uninitialized bytes after its
  NUL.  It now constructs the name through `const char *`; focused
  `ZhttpdTest` memcheck is clean.  A strict unsuppressed 19-program manifest
  passes 166 tests.  The service-idle fixture now allows the same bounded
  ten-second teardown window as its connection waits and explicitly stops and
  finalizes the lazily started global `ZiResolver` after client/service drain;
  GDB confirmed that the resolver scheduler and event-loop threads were the
  residual owners.  The fixture passes all 16 lifecycle checks and strict
  memcheck, bringing the unsuppressed total to 20 of 23 programs;
- the same resolver ownership is library-owned for the higher-level client:
  `Agent::final()` drains engines, then stops/finalizes the default resolver
  before restoring process runtime state, but preserves a resolver that was
  initialized by the embedding process.  A seven-assertion regression covers
  both ownership directions.  Cancellation, real H3-to-TLS fallback, and
  service-idle fixtures pass under both strict Valgrind and ASAN/LSAN after
  this change; applications need no resolver teardown branch;
- the apparent external TLS/H3 retention was a real `zpicotls` KEM leak:
  `evp_kem_exchange()` replaced an `EVP_PKEY_CTX` without freeing the old
  context.  Sibling-repository commit `0b175e6` frees it before replacement;
  installed package `zpicotls 1.6-2` contains the repair.  The suppression
  file is now empty of suppressions, and the final full ASAN/LSAN manifest
  plus focused strict Valgrind runs pass unsuppressed;
- post-consolidation GCC 16 release throughput spot checks submit 10,000
  requests at concurrency three through `zhttp` and `zhttpd`: TCP/H1
  completes in 462 ms, TLS/H1 in 606 ms, and QUIC/H3 in 649 ms.  These runs
  precede the final resolver-ownership repair, which changes shutdown rather
  than the request data path.  The broader 12-row release/debug evidence
  remains in `release_bench.md` and `debug_bench.md`;
- the installed MinGW GCC 16.1 compiler passes canonical compiler and Libtool
  cross probes.  Repository `z.config -M` is not a Linux cross-build driver:
  it sets the build triplet instead of the host triplet and retains native
  compiler names, causing Libtool to require unavailable `cygpath`.
  Canonical `--host=x86_64-w64-mingw32` configuration proceeds correctly and
  then stops at the first absent target dependency, `libpcre`.  MinGW
  compilation therefore requires either an MSYS2 build or a populated cross
  sysroot, plus correction of the wrapper before Linux-hosted acceptance;
- the earlier GCC failures were caused by an artificial 6 GiB
  per-process address-space limit imposed during diagnosis, not by a runtime
  leak or exhaustion of this 38 GiB host.  Divide-and-conquer compilation
  showed additive GCC template-state retention across the normalized
  TCP/TLS/QUIC pool initializers rather than one defective protocol or
  expression.  With the artificial limit removed, ordinary GCC 16 release
  compilation completed serially: `zhttp.cc` peaked at approximately
  8.8 GiB RSS and `zhttpd.cc` at approximately 9 GiB RSS.  A temporary
  16 GiB swapfile was enabled as a safety reserve but remained unused.
  Validation must remain serial through these measured high-RSS translation
  units unless a builder has independently measured capacity for more
  parallelism;
- `Zhttp::Service` now automatically emits a preformatted
  `Alt-Svc: h3=":<port>"` response header on TLS when both TLS and QUIC are
  enabled. `ServiceConfig::altSvcMaxAge()` controls its lifetime and zero
  disables it. The application supplies no QUIC-specific response logic;
- prefer-mode TLS reuse now re-enters routing when the origin has a live H3
  Alt-Svc entry. The cache predicate compacts expired entries in place and
  does not allocate or copy the cached alternative array on the request path;
- `ZhttpAgentFallbackTest` independently proves both policies: an untrusted H3
  attempt falls back to TLS/H1 with linked attempt identity, and a trusted
  two-request Agent goes from discovery-selected TLS/H1 to cached
  Alt-Svc-selected QUIC/H3. Both Agents are explicitly stopped and finalized
  before the multiplex and child service. The 28-check fixture passes native
  debug, strict Valgrind memcheck, and ASAN/LSAN;
- `zhttpmatrix` now accepts the reusable explicit row
  `zhttp-zhttpd/h3-prefer/j1nN`. It starts both service transports, uses the
  normal client prefer policy, and rejects the row unless verbose reporting
  observes both TLS and QUIC connections. A final-source Clang debug
  `j1n10000` run passed in 6,597 ms; the complete fallback/cache fixture had a
  268 ms median wall time across five runs. `release_bench.md` records the
  exact commands and interpretation.

The final teardown gap analysis found and repaired one transport race that was
not visible in the earlier release-only checks.  TCP/TLS admission can create
an accepted link before the queued `connected_1()` continuation installs its
native connection pointer.  A server stop that enumerated and closed that link
immediately observed no connection, then waited forever for a disconnect that
could not occur.  TCP and TLS server stop now:

1. stop listener/timer ingress;
2. post an Rx continuation so pending connection installation drains;
3. enumerate and close every live link while counting actual traversal;
4. post a second Rx continuation so queued link-down work begins; and
5. after every logical disconnect callback, enter the native Rx-to-Tx engine
   drain and complete the retained public `stop(done)` callback.

The normalized link disconnect path likewise performs Rx cleanup, posts a Tx
drain while retaining the connection/link, then returns to Rx for the
application disconnect/release callback.  H3 performs its logical-session
disconnect continuation before native QUIC stop.  `ClientPool::stop_()` is
therefore only the TCP/TLS `ZmEngine` hook; the multiplexed QUIC specialization
uses `H3_::ClientEngine::stop(done)` and never enters that hook.  No caller
infers completion from return of a no-argument internal `stop_`.

`ZhttpServiceIdleTest` now starts a deliberately incomplete active request and
stops the service over TCP, TLS, and QUIC.  For every transport it proves an
actual normalized `connected` event, active admission, asynchronous and
exactly-once stop completion, admission release before completion, and
subsequent client disconnect.  Debug reference tracing uses
`ZmPolymorph::debug()` for TCP/TLS normalized links and `ZmObject::debug()` for
QUIC links, matching their actual bases.

Current source counts are 797 lines for `zhttp.cc`, 424 for `zhttpd.cc`, and
1,064 for the static-workload-only `Zhttpd.hh`.  Final-source validation is:

- serial GCC 16 release: 23 programs and 194 tests, then all six boundary
  checks;
- serial GCC 16 `-g -DZDEBUG`: 23 programs and 194 tests, then all six
  boundary checks;
- serial Clang 22 `-O1 -g -DZDEBUG` with ASAN/LSAN, leak detection, and
  fail-fast enabled: 23 programs and 194 tests, then all six boundary checks,
  with no suppression;
- strict Valgrind memcheck of service active-stop, normalized H1, normalized
  H3, and H3 interop: zero errors and zero definitely, indirectly, or
  possibly lost bytes, with zero suppression;
- five fresh runs of the nine teardown-heavy programs: 250 tests total, all
  passing;
- `git diff --check`, including every new Zhttp source/test file: clean.

The concluding `GUIDELINES.md` alignment audit re-read the repository teardown,
sharding, timer, lifetime, container, and control-flow rules plus the
supplementary QUIC guidance.  The operational stop paths are continuation
based and do not block an I/O shard: they prevent ingress, drain Rx, drain Tx,
and only then release ownership and invoke the retained completion.  Synchronous
`stop()`/`wait()` wrappers remain only for main-thread application control; all
library composition uses `stop(done)`.  The complete executable closure still
maps to the six allowed application categories, and the audit found no
remaining repair after the accepted-before-connected race was fixed.

GCC's largest release units still require approximately 9 GiB RSS, while the
38 GiB host retained ample available memory and the temporary 16 GiB swap
reserve remained unused.  The only remaining environment-owned hand-off gap
is MinGW compilation in an environment with the target `libpcre` and remaining
cross sysroot dependencies.  It remains an explicit environment gate rather
than being relabelled as a source success.

The concluding release audit also corrected adjacent QUIC test/build
integration exposed by the canonical GCC configuration. GCC's
`-Wclass-memaccess` is disabled in the GCC warning policy so intentional
byte-wise clearing retains the natural typed pointer rather than adding casts
solely to suppress a diagnostic. Qlog-only test utilities now compile only
under `Zquic_DEBUG`; the one close/drain helper needed to prove compiled-out
logger lifecycle remains available in release. `ZquicRuntimeTest` no longer
uses release-erased `DiagCounter` values as synchronization conditions: public
ready/connected callbacks and received-stream state drive release assertions,
while detailed counters remain debug assertions. The serial GCC 16 release
QUIC suite consequently builds without warnings and passes all 20 programs
and 143 tests.

This checkpoint is not a waiver for remaining phase gates.  Any later change
to admission, pooling, H3 session ownership, engine callbacks, or executable
closure must rerun the corresponding comparison and update the evidence.

## Scope

### In scope

- HTTP/HTTPS URL parsing, origin identity, target splitting, and redirect
  reference resolution needed by a client;
- structured Alt-Svc parsing and bounded origin-scoped caching;
- HTTPS/SVCB discovery for secure H3 endpoint selection;
- asynchronous resolution and connection-attempt hand-off;
- one persistent engine lifecycle per client run;
- typed TCP/H1, TLS/H1, and QUIC/H3 attempt pools under one request
  coordinator;
- request concurrency, connection reuse, logical-stream admission, retry,
  redirect, fallback, cancellation, timeout, and shutdown mechanisms;
- protocol-specific configuration supplied by the application;
- a protocol-neutral request/response application callback contract;
- migration of `zhttp.cc` and `zhttpd.cc` to the new library facilities;
- tests, documentation, build/install manifests, diagnostics, and benchmarks.

### Out of scope

- HTTP/2 framing, HPACK, ALPN `h2`, or H2 stream multiplexing;
- a general browser URL implementation supporting arbitrary schemes;
- cookies, authentication databases, HSTS, cache storage, proxying, PAC, or
  content caching;
- Happy Eyeballs redesign below the resolver/transport boundary;
- DoH, DoT, ECH, QUIC v2 discovery, WebTransport, or DATAGRAM;
- automatic replay of non-idempotent or non-replayable request bodies;
- moving CLI formatting, file-output policy, workload generation, or
  human-readable reporting into `libZhttp`.

The library must expose policy controls without embedding the example
program's choices.  Redirect limits, protocol preference, concurrency,
timeouts, retry counts, discovery limits, and replay permission are policy.
Parsing, state machines, scheduling, cache ownership, and exact-once
completion are reusable mechanism.

## Executable boundary contract

The following six categories are the complete allow-list for retained
functionality in both `zhttp.cc` and `zhttpd.cc`.

The boundary is an ownership rule, not a line-count target.  A retained
function is conforming only when its complete responsibility fits one category
below.  A function that combines an allowed application decision with a
prohibited reusable mechanism must be split; the mechanism moves to installed
library code.  “Wiring” is limited to constructing typed configuration and
workload objects, registering reporting/output callbacks, invoking a public
library lifecycle, and mapping its typed result to process exit status.

### CLI parsing

Allowed:

- option declarations, usage text, parsing, validation, and conversion of
  option values into typed configuration or workload policy;
- choosing the process exit status from typed library/application results.

Not allowed:

- protocol negotiation, endpoint discovery, retry/fallback state, connection
  admission, engine coordination, signal-driven shutdown state machines, or
  polling/monitor loops hidden in option helpers.

### Protocol-specific configuration

Allowed:

- constructing public `Zhttp::TCPConfig`, `TLSConfig`, and `QUICConfig`
  values;
- selecting enabled protocols and supplying certificates, keys, CA paths,
  migration, qlog, diagnostic filters, and other documented configuration.

Not allowed:

- defining protocol-specific links, sessions, request parsers, response
  builders, callbacks, lifecycle branches, or connect/listen logic;
- inspecting `TLS`, `Multiplexed`, H1/H3, native stream, or native transport
  traits to choose application behavior.

### Workload selection

Allowed:

- choosing which client requests to submit;
- choosing which server workload features are enabled, such as static-file
  serving, redirects, authorization, directory listing, or single-file mode;
- setting concurrency, request count, limits, and workload-specific policy.

Not allowed:

- implementing the scheduler, connection pool, logical-stream admission,
  server admission, origin cache, redirect/retry/fallback engine, or shutdown
  coordinator that executes those choices.

### Workload-specific request/response handling

Allowed:

- client request method/target/header/body intent;
- client response status/header/body callbacks and workload decisions;
- server static-file lookup, authorization, range/conditional handling,
  redirect planning, MIME selection, and response body production;
- application-specific access logging events emitted from those callbacks.

Not allowed:

- HTTP-version-specific parsing/building, framing, EOF rules, connection reuse,
  keep-alive mechanics, QPACK, generic content-length/chunking state,
  transport disconnect/reconnect, or exactly-once completion machinery.

### Output-file handling

Allowed:

- opening, truncating, writing, and closing the client-selected response file;
- selecting server files/directories as workload data;
- workload-specific error reporting for those file operations.

Not allowed:

- using files as hidden transport/discovery/cache/lifecycle state or
  duplicating generic buffered-I/O machinery already provided by Z libraries.

### Reporting

Allowed:

- human-readable status, access, verbose discovery, memory, hash, heap, QUIC,
  and qlog reporting;
- formatting final counters and exit summaries;
- registering application observers provided by the library.

Not allowed:

- owning diagnostic timers, polling loops, protocol counters, qlog lifecycle,
  or mutable transport/request state solely to produce reports.

A thin `main` may initialize framework logging/process facilities, construct
configuration and workload objects, call the public `init`/`start`/`run`/
`stop`/`final` lifecycle, and return a result.  This wiring does not authorize
program-local lifecycle state machines.  Generic daemon/signal/wait behavior
must use an existing Z facility or a reusable facility outside the programs.

Do not satisfy this contract by moving prohibited code from a `.cc` file into
`Zhttpd.hh`, another program-local header, or a test helper.  Source-boundary
audits cover the complete program-only dependency closure.  Independently
reusable HTTP mechanisms must reside in installed `libZhttp` sources.

### Required boundary evidence

Maintain a reviewed source-boundary manifest during the migration.  It must:

- enumerate every top-level function, class, struct, alias with behavior, and
  non-trivial global in `zhttp.cc`, `zhttpd.cc`, and their program-only source
  closure;
- assign each entry to exactly one allowed category and give a one-line
  application responsibility;
- name the public `Zhttp` or existing framework facility that owns each
  removed mechanism;
- contain no `mixed`, `utility`, `framework`, `core`, or `miscellaneous`
  category;
- be checked by a source-boundary test that rejects known prohibited symbols,
  private/trailing-underscore link calls, native protocol types, and
  program-local files newly added to evade the closure.

The manifest may live in a test fixture or review document while work is in
progress.  At final hand-off, retain the executable allow-list and automated
negative scans in the repository; do not retain a stale line-by-line inventory
that future edits cannot reliably maintain.

For `zhttp.cc`, the final positive inventory is limited to:

- option/usage parsing and conversion to `AgentConfig` plus typed transport
  configurations;
- creation or enumeration of the requested workload;
- request-intent and response-event callbacks;
- response output-file ownership and file-error policy;
- presentation of progress, diagnostics, counters, results, and exit status.

For `zhttpd.cc` and `Zhttpd.hh`, the final positive inventory is limited to:

- option/usage parsing and conversion to `ServiceConfig` plus typed transport
  configurations;
- selection and implementation of the static-file workload;
- request authorization/routing/range/conditional policy and response
  content production;
- workload file/directory access and file-error policy;
- access, diagnostic, startup/failure, and final reporting.

In particular, a protocol name may affect configuration and reporting, but
must not select an application parser, builder, callback flow, worker,
connection/session type, admission path, timer, or lifecycle branch.

## Target application shape

The final client should have one application object and one request/response
sink.  Protocol choice must not change its message-processing code:

```c++
struct App : public Zhttp::Agent<App, RequestHeaders, ResponseHeaders> {
  using Base = Zhttp::Agent<App, RequestHeaders, ResponseHeaders>;

  void response(Request &req, const Zhttp::ResponseInfo &info);
  void body(Request &req, ZuBSpan data);
  void complete(Request &req, const Zhttp::Result &result);
};

Zhttp::AgentConfig config;
config.protocol(...).concurrency(...).redirects(...);
config.tcp(...).tls(...).quic(...);

App app;
app.init(engineConfig, config);
app.start(...);
app.request(request);
app.stop(...);
app.final();
```

Names are provisional, but the ownership and layering are mandatory:

- the application owns request intent and response-side output policy;
- the agent owns routing and request lifecycle;
- typed protocol clients own physical connections or multiplexed sessions;
- typed logical links own one active attempt;
- Rx parsers and response callbacks execute on the Rx owner;
- Tx builders and request-body production execute on the Tx owner;
- runtime protocol selection occurs only between attempts, never per frame,
  field, or body chunk.

The final server should expose one workload planner/sink to a library-owned
multi-protocol server:

```c++
struct Workload {
  Zhttp::ResponsePlan request(const Zhttp::RequestInfo &request);
  void body(Zhttp::ResponseBody &body, const Zhttp::ResponsePlan &plan);
  void complete(const Zhttp::RequestInfo &, const Zhttp::Result &);
};

Zhttp::ServerConfig config;
config.tcp(...).tls(...).quic(...);

Workload workload;
Zhttp::Service<Workload, ReqHeaders, RespHeaders> service;
service.init(engineConfig, config, &workload);
service.start(...);
service.stop(...);
service.final();
```

Names are provisional.  The required boundary is that `zhttpd.cc` supplies
configuration and static-file workload behavior, while the library supplies
protocol parser/builder selection, server/link/session composition, admission,
idle handling, engine coordination, and exact-once shutdown.

Retain the lower-level `Zhttp::Client`/`ClientLink` and
`Zhttp::Server`/`ServerLink` APIs only if they remain useful library
primitives.  The two programs must use higher-level reusable facilities so
they cannot reimplement pools, fallback, server sessions, or lifecycle.
Do not add compatibility forwarders for removed program-local types.

## Proposed library organization

Add or expand:

- `ZhttpURL.hh` / `ZhttpURL.cc`
  - owning `URL`, `Origin`, and authority/target types;
  - HTTP/HTTPS parsing, canonical origin comparison, default-port handling;
  - request-target splitting and redirect-reference resolution.
- `ZhttpDiscovery.hh` / `ZhttpDiscovery.cc`
  - structured Alt-Svc values and cache metadata;
  - HTTPS/SVCB record parsing and endpoint construction;
  - asynchronous DNS discovery and explicit result/failure types.
- `ZhttpClient.hh`
  - retain the typed low-level protocol client;
  - remove protocol identity inferred from `Multiplexed`;
  - expose complete public connect/disconnect/logical-link lifecycle.
- `ZhttpMessage.hh`
  - protocol traits selecting request builder, response parser, EOF semantics,
    and logical completion by HTTP version;
  - shared request-send and response-receive adapters.
- `ZhttpClientPool.hh`
  - typed H1 connection workers and H3 logical-stream admission;
  - one scheduler contract independent of the pool's physical topology.
- `ZhttpAgent.hh`
  - multi-protocol request coordinator;
  - discovery, redirect, retry, fallback, origin cache, attempt selection,
    global concurrency, cancellation, stop, and finalization.
- `ZhttpService.hh`
  - multi-protocol server coordinator over the existing typed servers;
  - common request dispatch, response-send adapter, admission, idle policy,
    engine lifecycle, stop, and finalization;
  - application hooks limited to workload-specific request/response handling.
- `ZhttpConfig.hh`
  - client-agent policy, limits, timeouts, and TCP/TLS/QUIC configuration.

If implementation discovery shows that fewer files produce a clearer
dependency graph, combine closely related types.  Do not create a monolithic
header or move non-template parsing into templates.  Installed header and
`Makefile.am` changes are part of the same phase as each new public facility.

## Data and ownership model

### URL and origin

`URL` owns all text that must survive the input span.  It contains scheme,
host, resolver hostname, explicit/effective port, path/query target, and the
minimum authority metadata needed to reconstruct requests.  `Origin` contains
canonical scheme, host, and effective port and supplies equality/hash
operations.

The supported grammar is deliberately HTTP-specific but must correctly:

- terminate authority on `/`, `?`, or `#`;
- parse bracketed IPv6 and optional ports;
- reject malformed ports, empty hosts, userinfo if unsupported, control
  characters, and invalid authority forms;
- normalize absent path to `/`;
- separate fragments from the request target;
- resolve absolute, scheme-relative, absolute-path, relative-path, query-only,
  and fragment-only redirect references;
- remove dot segments and preserve query semantics;
- distinguish an explicitly supplied port from the default effective port.

### Discovery and routing

An `Endpoint` contains the logical origin, TLS server name, DNS target, remote
address when resolved, port, ALPN/protocol eligibility, source
(HTTPS/SVCB/Alt-Svc/origin), and expiry/policy metadata.

HTTPS/SVCB handling must:

- follow bounded alias chains with loop detection;
- validate ordered SvcParams and mandatory keys;
- recognize ALPN, no-default-ALPN, port, IPv4 hints, and IPv6 hints;
- retain service priority and deterministic endpoint order;
- use address hints as candidates without treating them as authoritative DNS
  replacement;
- return the selected endpoint to the connection attempt rather than merely
  reporting H3 availability;
- distinguish no record, malformed record, unsupported mandatory key,
  transient resolution failure, and usable endpoint results.

Alt-Svc handling must:

- parse comma-separated alternatives, quoted authority, parameters, `clear`,
  `ma`, and `persist` needed by the client policy;
- recognize eligible H3 ALPN tokens without substring matching;
- scope entries to an origin and expire/remove them deterministically when
  accessed or replaced; do not add a background cache scan;
- never replay the response that supplied the entry;
- validate cross-host alternatives according to configured authority/TLS
  policy.

### Requests, attempts, and results

A request is stable application intent.  An attempt is mutable library-owned
state for one protocol endpoint.  Never reset a request object in place to
represent a new attempt.

The request contains:

- method, URL, compile-time/runtime headers, and request-body producer;
- replay classification and application ID/context;
- redirect/retry policy snapshot;
- response callbacks or CRTP hooks.

The attempt contains:

- selected protocol and endpoint;
- attempt number and redirect generation;
- parser/builder state, response framing, and completion state;
- physical/logical link reference;
- bytes sent/received needed to decide replay safety;
- exactly one terminal result.

The terminal result distinguishes success, HTTP completion, redirect refusal,
DNS/discovery failure, connection failure, protocol failure, timeout,
cancellation, shutdown, and indeterminate non-replayable outcome.  Local
logging is not the result contract.

### Pools and scheduler

The agent initializes each enabled typed client once, registers it with one
`Zhttp::Engines`, starts all enabled engines once, and stops/finalizes them
once.

Use one global admission scheduler:

- `concurrency` bounds active logical requests in every mode;
- H1 uses reusable physical connections subject to connection framing and
  origin;
- H3 uses logical streams admitted through reusable QUIC sessions;
- pending requests are bounded and use a named heap;
- control/lifecycle work is never blocked behind request-body work;
- capacity completion immediately admits the next eligible request without a
  polling timer;
- stopping prevents new admission, cancels pending discovery/attempts, drains
  or cancels active work by policy, and completes every request once.

Do not use `Multiplexed` as a protocol discriminator.  Add explicit
HTTP-version/message traits and explicit topology traits where topology
actually matters.  This is required before H2 adds a multiplexed protocol over
TLS.

## Routing, redirect, retry, and fallback policy

For each request generation:

1. derive the canonical origin and consult valid discovery/cache state;
2. select an eligible endpoint according to configured protocol preference;
3. submit one typed attempt without blocking the caller;
4. on failure, classify replay safety before considering another endpoint;
5. on a redirect, resolve the new URL, apply redirect policy, create a new
   generation, and rerun routing;
6. on final response or terminal failure, complete exactly once.

For `prefer H3`, use:

1. a valid cached Alt-Svc H3 endpoint, if any;
2. a usable HTTPS/SVCB H3 endpoint;
3. the origin's TLS/H1 endpoint.

An H3 attempt failure may fall back to TLS/H1 only if no final response has
been delivered and the request is replayable.  `force H3` never falls back.
`disable H3` performs no H3 discovery or attempt.

Alt-Svc learned from response N affects request N+1 and later eligible
requests.  It never triggers a duplicate request N.  Redirects are distinct:
they create a new request generation only when policy permits.

Retry policy must consider method, whether any request body bytes were
committed, response progress, peer GOAWAY/close semantics, and application
replay declaration.  Never infer safety only from the absence of an HTTP
status.  Report indeterminate outcomes rather than silently retrying.

## Diagnostic contract

Keep diagnostics useful after orchestration moves into the library:

- every request, redirect generation, and attempt has stable scalar IDs;
- logs identify origin, selected endpoint source, protocol, attempt, and
  terminal reason;
- verbose discovery logging is a policy-controlled observer, not parsing logic;
- memory diagnostics remain application-controlled;
- QUIC diagnostics and qlog stay behind their existing debug/runtime gates;
- no `ZiLOG` lambda captures mutable request/attempt objects or pointers;
- diagnostic work does not change scheduling or lifetime.

The available investigation tools include `gdb` with `ptrace_scope=0`, ASAN,
Valgrind, qlog, `--debug`, `--quic-diag`, `--mem-diag`, and `-v`.

## Delivery phases

Each phase must leave the default build usable, add deterministic tests for its
new behavior, meet its hand-off criteria, and conclude with a
`GUIDELINES.md` alignment audit and repair.  A later phase may not weaken an
earlier gate.

### Phase 0: characterize behavior and lock down regressions

**Goal:** establish evidence for the defects and preserve legitimate CLI
behavior before moving ownership.

**Work:**

1. Record line/responsibility boundaries and current force/prefer/disable
   execution paths.
2. Add deterministic tests demonstrating:
   - `--jobs` admission in all three modes;
   - one engine start/stop lifecycle per run;
   - SVCB target/port/address propagation;
   - no duplicate request after learning Alt-Svc;
   - URL query-only authority termination;
   - redirect reference classes;
   - exact-once completion across retry, timeout, and stop;
   - one shared zhttpd request/response path for TCP, TLS, and QUIC;
   - zhttpd admission, idle timeout, active-request stop, and reverse engine
     shutdown without program-local protocol branches.
3. Add counters/fixtures at public library boundaries rather than inspecting
   private native transports.
4. Mark failing tests as the Phase 0 repair backlog; do not encode known-bad
   behavior as the expected result.

**Acceptance criteria and hand-off to Phase 1:**

- every immediate defect has a deterministic regression test;
- the existing H1/H3 engine, lifecycle, fallback, multi-request, server,
  static-workload, and application tests have a recorded clean baseline;
- tests distinguish engine lifetime, physical connection, logical link,
  request, and attempt counts;
- no production behavior has been changed merely to make characterization
  easier.
- a baseline boundary manifest accounts for the complete program-only closure
  of both executables and marks every prohibited entry with its destination
  phase and intended library owner;
- baseline line counts and prohibited-symbol scan results are recorded only as
  migration evidence, not used as substitutes for the responsibility audit;
- the Phase 0 hand-off names every still-failing regression and every still
  prohibited executable responsibility; nothing is silently deferred.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `AGENTS.md`, root `GUIDELINES.md`, and
   `zquic/GUIDELINES.md` for H3 diagnostics.
2. Audit new fixtures for time-based completion, private API reach-through,
   fixed capacities, shared mutable counters, and nondeterminism.
3. Repair every finding and rerun the full baseline plus new regression suite.
4. Run `git diff --check`; Phase 1 starts only with a clean audit.

### Phase 1: move URL, origin, redirect, and Alt-Svc primitives

**Goal:** replace ad hoc program parsers with independently reusable,
fully-tested `libZhttp` value types.

**Work:**

1. Implement owning `URL`, `Origin`, and `AltSvc` types in the proposed source
   organization.
2. Implement the HTTP-specific URL and redirect grammar described above.
3. Implement structured Alt-Svc parsing, origin scoping, expiry metadata, and
   deterministic replacement/removal.
4. Add exhaustive table-driven tests for valid and malformed authorities,
   IPv4/IPv6, ports, path/query/fragment boundaries, every redirect reference
   class, dot segments, Alt-Svc alternatives/parameters, `clear`, and bounds.
5. Migrate `zhttp.cc` to these values and delete its local URL, origin,
   redirect, and Alt-Svc parsers.

**Acceptance criteria and hand-off to Phase 2:**

- `zhttp.cc` defines no URL, origin, redirect, or Alt-Svc parser;
- `https://host?x` produces host `host` and target `/?x`;
- redirect resolution passes the selected RFC 3986 reference vectors;
- Alt-Svc never uses substring matching and never causes immediate replay;
- all retained strings own the storage required by their lifetime;
- malformed input fails with a typed result and no partial cache mutation;
- H1/H3 application and fallback regressions pass.
- URL, origin, redirect, and Alt-Svc entries are removed from the executable
  boundary manifest rather than relabelled as workload helpers;
- the program-only closure has no alternate parser, inline grammar, or cache
  mutation helper for those facilities;
- every remaining client function still maps to one allowed category or to a
  later phase with an explicit removal owner.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read `GUIDELINES.md` sections on strings/spans, copies, containers,
   layout, framework fit, and API friction.
2. Audit value ownership, normalization copies, fixed capacities, parsing
   control flow, hash identity, expiry cleanup, and error representation.
3. Repair every finding and rerun URL/Alt-Svc plus application tests.
4. Run `git diff --check`; Phase 2 starts only with a clean audit.

### Phase 2: move HTTPS/SVCB discovery and endpoint selection

**Goal:** provide asynchronous discovery that returns usable endpoints rather
than a Boolean availability hint.

**Work:**

1. Implement HTTPS/SVCB record and SvcParam parsing in `ZhttpDiscovery`.
2. Replace fixed arrays with bounded Z containers whose limits are named
   policy/configuration values.
3. Implement alias-loop detection, priority ordering, address-hint candidates,
   target resolution, and typed outcomes.
4. Make discovery asynchronous end-to-end; remove `ZmBlock` wrappers from the
   discovery path.
5. Pass the selected target, TLS name, address, and port to the typed QUIC
   attempt.
6. Add raw DNS vector tests, malformed/truncated cases, unknown mandatory
   keys, aliases/loops, multiple priorities, IPv4/IPv6 hints, cancellation,
   and endpoint propagation tests.
7. Migrate `zhttp.cc` and delete its `HTTPS`, `H3Endpoint`, DNS wire-parser,
   and discovery-cache implementations.

**Acceptance criteria and hand-off to Phase 3:**

- the example contains no DNS wire parsing or discovery cache;
- every usable discovery result retains and uses its target, TLS name,
  address, port, source, and priority;
- alias depth, endpoint count, and record storage are explicit configurable
  bounds with deterministic overflow behavior;
- cancellation/stop cannot invoke a stale callback or leak a resolver result;
- no discovery path blocks an I/O owner or caller thread;
- discovery unit and end-to-end endpoint-selection tests pass.
- no executable or program-only helper owns resolver requests, alias
  traversal, endpoint ordering, address-hint expansion, or discovery
  cancellation state;
- executable discovery code is limited to configuration and reporting
  observers; endpoint selection and hand-off are library-owned;
- the boundary manifest and negative scans prove that discovery responsibility
  has left the executable closure before Phase 3 begins.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read `GUIDELINES.md` sections on async continuations, sharding,
   cross-thread capture, containers, bounds, and cleanup.
2. Audit resolver ownership, callback captures, alias/endpoint storage,
   cancellation, error propagation, and cache garbage collection.
3. Repair every finding and rerun discovery, cancellation, fallback, ASAN,
   and Valgrind tests.
4. Run `git diff --check`; Phase 3 starts only with a clean audit.

### Phase 3: normalize message traits and public link lifecycle

**Goal:** remove all client and server application decisions based on H1
versus H3 or `Multiplexed`.

**Work:**

1. Add explicit HTTP message traits for builder, parser, EOF, logical
   completion, and topology where required.
2. Replace `sendH1Request`/`sendH3Request` and response-parser specializations
   with one `ZhttpMessage` adapter instantiated by protocol traits.
3. Replace zhttpd's H1/H3 request-parser, response-builder, and `Message`
   specializations with the corresponding library adapter and one workload
   callback contract.
4. Replace H3 logging branches with protocol/version metadata supplied by the
   library diagnostic observer.
5. Expose one public disconnect/cancel operation with consistent callback
   semantics; remove application use of `disconnect_()`.
6. Remove dead completion logic and make parser result mapping explicit.
7. Extend compile-time contract tests with a synthetic multiplexed non-H3
   protocol so no code can infer H3 from multiplexing.

**Acceptance criteria and hand-off to Phase 4:**

- neither application contains a symbol or branch selecting H1/H3 builders or
  parsers;
- scans for `Multiplexed.*H3`, `H3.*Multiplexed`, `disconnect_`,
  `H1RespBuilder`, `H3RespBuilder`, and `ReqParser_<` in `zhttp.cc` and
  `zhttpd.cc` return no matches;
- a synthetic multiplexed non-H3 contract selects its own message traits;
- TCP/H1, TLS/H1, and QUIC/H3 use one client flow and one server workload
  flow;
- parser errors, EOF, success, cancellation, and disconnect each complete once;
- existing message, transport-contract, and engine tests pass.
- neither program-only closure directly names native H1/H3 parser/builder
  types, native transport streams, or internal link lifecycle operations;
- protocol-specific configuration may select a transport, but workload
  callbacks have identical signatures and ordering for TCP/H1, TLS/H1, and
  QUIC/H3;
- all removed message/lifecycle entries are deleted from the boundary manifest
  and the automated negative scan fails when a representative prohibited
  symbol is injected into its fixture.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read `GUIDELINES.md` sections on CRTP/templates, detector traits,
   repeated blocks, casts, API boundaries, and hot-path dispatch.
2. Audit for runtime type erasure, virtual dispatch, compatibility aliases,
   protocol booleans, duplicated adapters, and internal API exposure.
3. Repair every finding and rerun compile-time contracts and all H1/H3 message
   tests.
4. Run `git diff --check`; Phase 4 starts only with a clean audit.

### Phase 4: implement persistent typed clients, servers, and coordinators

**Goal:** move connection workers, logical-stream admission, reuse, request
queues, server admission/session composition, and engine lifetime into
`libZhttp`.

**Work:**

1. Implement typed H1 and H3 pools behind one scheduler/admission contract.
2. Initialize all enabled engines once and retain them for the complete agent
   lifetime.
3. Implement bounded pending queues, global concurrency, capacity return,
   H1 origin reuse, H1 close-delimited EOF, H3 stream admission, cancellation,
   stop, and finalization.
4. Replace status-based reconnect heuristics with typed attempt failure and
   replay classification.
5. Implement the reusable multi-protocol service coordinator: typed server
   composition, shared workload dispatch, admission, idle handling, signal/
   stop hand-off, reverse engine shutdown, and finalization.
6. Add deterministic tests for concurrency in every mode, physical connection
   counts, H1 reuse, H3 session reuse, queue bounds, cancellation at every
   state, stop with active work, and partial engine failure.
7. Add server tests for all protocol combinations, admission limits, idle
   timeout, request errors, active drain, forced stop, partial start, and
   exactly-once link/request completion.
8. Migrate `PoolLink`, `PoolClient`, `runPool`, `runH1Single`, and
   `runH3Single` out of `zhttp.cc`; delete them rather than wrapping them.
9. Migrate `StaticServer`, `AppServer`, `AppServerLink`, interval-monitor, and
   engine-control mechanisms out of `zhttpd.cc`; preserve only its workload
   callbacks and configuration.

**Acceptance criteria and hand-off to Phase 5:**

- force, prefer, and disable use the same scheduler and honor the same
  `concurrency` bound;
- a client run starts/stops each enabled engine at most once;
- H1 physical connections and H3 sessions are reused according to protocol
  semantics without application branching;
- queues, pools, links, timers, and attempts are bounded and empty after final;
- cancellation and shutdown complete queued and active requests exactly once;
- zhttpd supplies no protocol server/link/session types and performs no engine,
  admission, idle-timer, or shutdown coordination;
- all TCP/TLS/QUIC server combinations invoke the same workload callbacks;
- repeated lifecycle stress passes ASAN/LSAN and Valgrind without growth.
- `zhttp.cc` owns no worker, connection/session pool, pending queue, admission
  counter, capacity callback, reuse registry, or engine start/stop branch;
- `zhttpd.cc`, `Zhttpd.hh`, and all program-only helpers own no server/session/
  link composition, active-connection registry, idle timer, signal handler,
  wait primitive, diagnostic timer, or reverse-shutdown sequence;
- executable lifecycle code is a straight public-library sequence with no
  protocol-conditioned control flow: configure, `init`, `start`, submit or
  wait, `stop`, `final`;
- server workload code can be compiled against a fake protocol-neutral service
  harness without including native `ztcp`, `ztls`, or `zquic` link/session
  headers;
- the updated manifest classifies all retained server code within the six
  categories and assigns every remaining client routing item to Phase 5.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read `GUIDELINES.md` sections on owner threads, intrusive ownership,
   deterministic GC, scheduling, buffers, heaps, and shutdown.
2. Audit every client pool, server coordinator, and scheduler member by owner;
   audit queue-node heap identity, registry removal, admission, cross-shard
   captures, fairness, timer cancellation, and completion ownership.
3. Repair every finding and rerun pool, lifecycle, failure-injection, ASAN,
   and Valgrind tests.
4. Run `git diff --check`; Phase 5 starts only with a clean audit.

### Phase 5: implement the multi-protocol agent and policy state machine

**Goal:** centralize discovery, endpoint routing, redirect, retry, Alt-Svc,
fallback, and terminal results while leaving choices configurable.

**Work:**

1. Implement the request-generation and attempt state model described above.
2. Integrate URL/origin, discovery, Alt-Svc cache, typed pools, and
   `Zhttp::Engines` into one agent.
3. Implement force/prefer/disable routing and replay-safe H3-to-H1 fallback.
4. Implement redirects as new generations with configured limits and method/
   body replay policy.
5. Ensure Alt-Svc learned from a response affects only later requests.
6. Add typed result/observer callbacks for selection, attempt failure,
   redirect, retry, fallback, completion, cancellation, and shutdown.
7. Add matrix tests across protocol policy, discovery outcomes, Alt-Svc,
   redirects, replayable/non-replayable bodies, connection failures before/
   after send, response progress, timeout, and stop.

**Acceptance criteria and hand-off to Phase 6:**

- the routing order exactly matches the configured policy;
- discovery endpoints are used, not merely logged;
- a completed request is never duplicated because it advertised Alt-Svc;
- fallback/retry never replays an unsafe request and reports indeterminate
  outcomes explicitly;
- redirects, retries, and fallbacks retain stable request identity while each
  attempt/generation has a distinct ID;
- all terminal paths produce one result and release attempt/link ownership;
- the complete deterministic policy matrix passes.
- `zhttp.cc` owns no attempt state, redirect generation, replay classifier,
  origin/Alt-Svc cache, endpoint ranking, retry budget, fallback transition, or
  exactly-once completion guard;
- the only client callbacks retained are workload request intent,
  response/body/result handling, output-file policy, and reporting observers;
- force/prefer/disable differ only in agent configuration and reported
  decisions, not in application control flow;
- the boundary manifest contains no client item awaiting a later mechanism
  migration; Phase 6 is exclusively final deletion, classification, docs, and
  enforcement.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read `GUIDELINES.md` sections on state, control flow, callbacks,
   cross-thread ownership, logging captures, and cleanup.
2. Audit the complete state machine for duplicate flags, nested branches,
   stale callbacks, replay safety, cache lifetime, exactly-once completion,
   and diagnostic overhead.
3. Repair every finding and rerun the full routing/fallback matrix plus
   lifecycle stress.
4. Run `git diff --check`; Phase 6 starts only with a clean audit.

### Phase 6: enforce the executable boundary in both programs

**Goal:** migrate both programs completely and limit their retained
functionality to the six allow-listed categories.

**Work:**

1. In `zhttp.cc`, retain only:
   - CLI parsing;
   - TCP/TLS/QUIC configuration;
   - request workload selection;
   - workload-specific request construction and response callbacks;
   - response output-file handling;
   - reporting.
2. In `zhttpd.cc`, retain only:
   - CLI parsing;
   - TCP/TLS/QUIC configuration;
   - static-server workload selection;
   - workload-specific request planning and response production;
   - static/output file handling;
   - reporting.
3. Use one client response sink/request submission flow and one server
   workload flow for every protocol mode.
4. Remove client-local URL/discovery/cache/attempt/pool/retry/redirect/fallback
   mechanisms and all protocol-specific builders/parsers.
5. Remove server-local protocol adapters, parser/builder selection, admission,
   idle/lifecycle coordinators, interval monitors, signal-state machinery, and
   engine branches.  Use public service/framework facilities.
6. Classify every remaining top-level type and function in each program under
   exactly one of the six allowed categories.  Delete, move to installed
   library code, or replace anything that cannot be classified.
7. Perform the same classification over `Zhttpd.hh` and every other
   program-local dependency so prohibited code is not merely relocated.
8. Update README examples to show the real agent/service APIs and the exact
   boundary between application policy and library mechanism.
9. Add source-boundary scans and application tests so reusable machinery
   cannot drift back into either program.

**Acceptance criteria and hand-off to Phase 7:**

- every remaining top-level declaration in `zhttp.cc`, `zhttpd.cc`, and their
  program-only helpers maps to exactly one of:
  CLI parsing, protocol-specific configuration, workload selection,
  workload-specific request/response handling, output-file handling, or
  reporting;
- `zhttp.cc` contains no DNS packet parser, URL/Alt-Svc parser, discovery
  cache, origin hash, connection pool, scheduler, retry/fallback state machine,
  lifecycle coordinator, protocol message selector, or internal link call;
- `zhttpd.cc` contains no protocol-specific request parser/response builder,
  server/link/session adapter, admission controller, idle timer, interval
  monitor, signal/shutdown state machine, engine coordinator, or internal link
  call;
- neither program-local dependency contains any prohibited mechanism moved
  from the `.cc` files;
- one request/response callback flow serves TCP/H1, TLS/H1, and QUIC/H3;
- one workload request/response flow serves the server over TCP/H1, TLS/H1,
  and QUIC/H3;
- force/prefer/disable all support `-n` and `-j` consistently;
- client output, server static-file behavior, diagnostics, exit status, and
  policy options retain documented behavior except for explicitly repaired
  defects;
- code review can account for every retained line through the six-category
  allow-list without a miscellaneous/core/framework category;
- line counts for both programs and their program-only helpers are reported,
  but conformance to the allow-list—not a numeric line target—is the gate;
- README, installed headers, and both actual programs agree.
- each manifest entry identifies concrete source lines and passes independent
  review as application policy; declarations with two categories are split
  before hand-off;
- direct includes from `ztcp`, `ztls`, and `zquic` are used only to construct
  documented protocol configuration types; no native engine, client, server,
  link, session, stream, parser, or builder type is referenced;
- executable conditionals on protocol identity occur only while constructing
  protocol configuration, selecting enabled protocols, or formatting reports;
  source-boundary tests reject protocol-conditioned request/response,
  scheduling, admission, connection, and lifecycle branches;
- `main` in each executable contains no loop, timer, semaphore, signal handler,
  retry label, reconnect path, or per-protocol lifecycle branch;
- application tests exercise the same callback implementations over TCP/H1,
  TLS/H1, and QUIC/H3 and compare callback ordering and terminal-result counts;
- installed public headers, not test-local wrappers, are sufficient to build
  the retained workload and thin executable wiring;
- the source-boundary test enumerates the program-only closure explicitly and
  fails if an unclassified helper file or top-level declaration is added.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `AGENTS.md`, root `GUIDELINES.md`, and applicable zquic
   guidance.
2. Audit both complete programs and all program-only helpers, not only changed
   hunks, against the six-category allow-list and for repeated protocol blocks,
   fixed arrays, blocking, broad captures, internal APIs, dead flags, and
   avoidable copies.
3. Repair every finding and rerun client/server application, policy,
   lifecycle, workload, and source-boundary tests.
4. Run `git diff --check`; Phase 7 starts only with a clean audit.

### Phase 7: hardening, performance, toolchains, and final hand-off

**Goal:** prove the consolidated client and server are production-ready and a
valid base for H2.

**Work:**

1. Run deterministic client stress with concurrent origins, H1 reuse, H3
   streams, discovery cancellation, redirects, fallback, timeouts, and
   stop/start cycles.
2. Run deterministic server stress across TCP/TLS/QUIC with concurrent
   requests, admission pressure, slow/static bodies, client resets, idle
   expiry, graceful drain, forced stop, and repeated lifecycle cycles.
3. Use ASAN/LSAN and Valgrind to verify request, attempt, endpoint, cache,
   connection, stream, queue, timer, and buffer cleanup.
4. Use `gdb`, verbose diagnostics, qlog, and QUIC diagnostics to investigate
   any owner-thread, migration, or fallback failures.
5. Benchmark:
   - warm H1 reuse;
   - concurrent H3 requests on one session;
   - prefer-mode routing with cached discovery;
   - failed H3 followed by eligible H1 fallback;
   - concurrent server requests over H1 and H3;
   - old versus consolidated scheduling overhead where reproducible.
6. Verify current GCC and Clang debug builds.  Configure release through
   `z.config`, run the clean build serially (`make -j1`) through the large
   Zhttp/QUIC translation units, and run the full test suite.  Record peak
   compiler RSS before increasing parallelism; never use an unmeasured
   top-level parallel build as an acceptance step.  Perform MinGW compilation
   when supported by the environment.
7. Remove temporary instrumentation, obsolete test helpers, dead build entries,
   and compatibility residue.

**Acceptance criteria and final hand-off:**

- all Phase 0 defects have passing regression tests;
- URL, redirect, Alt-Svc, HTTPS/SVCB discovery, pools, routing, retry, and
  fallback are library-owned and independently tested;
- both programs and their program-only helpers contain only the six allowed
  categories of functionality;
- no mode serializes requests or restarts engines contrary to configuration;
- no completed or unsafe request is silently replayed;
- discovery-selected endpoint data reaches the actual attempt;
- repeated client/server stress shows no leaks, monotonically growing
  cache/queue/registry, stranded timer, stale callback, or duplicate
  completion;
- latency/allocation results show no new per-frame or per-body-chunk
  allocation/copy in the normalized message path;
- GCC and Clang debug builds/tests pass without warnings;
- clean top-level release build and tests pass without warnings;
- the client architecture can add H2 as another typed pool/message trait
  without treating multiplexing as H3 or changing application callbacks;
- the server architecture can add H2 without adding an H2 parser, builder,
  link, session, admission, or lifecycle branch to `zhttpd.cc`.
- a final reviewer can map every executable and program-only-helper
  declaration to one of the six allowed categories using the retained
  boundary evidence, with zero exceptions or deferred items;
- automated negative scans and positive multi-protocol callback tests run in
  the normal `zhttp/test` suite and fail on representative boundary
  violations;
- final line counts are reported for review context, while acceptance is based
  on ownership, closure classification, and behavior tests.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `AGENTS.md`, root `GUIDELINES.md`,
   `zquic/GUIDELINES.md`, and guidance for every other touched module.
2. Audit the complete final diff for layering, the six-category executable
   boundary, static polymorphism, message hot paths, ownership/sharding,
   containers/heaps, copies/allocations, endpoint and cache bounds, replay
   safety, exactly-once lifecycle, logging, diagnostics, tests, and
   build/install integration.
3. Repair every finding and rerun the directly affected suites after each
   repair group.
4. Repeat `git diff --check`, source-boundary scans, full GCC/Clang debug
   tests, ASAN/Valgrind, benchmarks, and the clean release rebuild/test.  The
   follow-up is complete only when the repaired tree still satisfies every
   phase gate and final acceptance criterion.
