## Summary

The goal is to turn `zhttp/example/zhttp.cc` from a single-request example
client into a bounded multi-request client for one URL. The new CLI controls are
`-n N` for logical request count and `-j M` for maximum concurrent logical
requests. `N` defaults to `1`, `M` defaults to `1`, explicit `-j` is valid only
when `N > 1`, and `M > N` is a usage error. For `N > 1`, response bodies are
written to `<output-base>.<request-id>`, using the existing default base
`index.html` when `-o` is not supplied.

There is no `plan.feedback.md` in this workspace at the time of this revision.
The open questions from `plan.md` are resolved here:

- H3 link ownership: all stream open/send/link mutation runs on the QUIC Tx
  scheduler context; app/request scheduling only enqueues or invokes work.
- H3 request stream dispatch: rely on `Zquic`'s existing stream table and
  `findStream()` plus per-`QUICClient::Stream` parser/request state. Do not add
  a redundant app-level stream hash for normal dispatch.
- H1 close-delimited bodies: `Zhttp::H1::Parser` already supports EOF-framed
  response bodies through `m_eofBody` and `eof()`. The client must use this
  behavior correctly and must not reuse a connection that completed a body only
  by EOF.
- Alt-Svc/DNS discovery cache: add an invocation-scoped origin cache using
  `ZmHash`, after the per-request state boundary is in place and before H3
  concurrency is added.
- Output publication: truncate/open the final output path at the beginning of
  each body-writing terminal attempt. Do not add staging files or atomic rename
  in the first implementation.

Current state changes to planned state:

| Current | Planned |
| --- | --- |
| One `Options`, one `State`, one parser, one link, one body file. | One aggregate `Run` plus `Req` state per logical request; H1 parsers are worker-bound and H3 parsers are stream-bound. |
| `main()` performs redirects around one request result. | Each request owns redirect count, current URL, fallback attempts, output path, parser/file state, and completion. |
| H1 opens one connection and exits after one response. | H1 uses up to `M` worker links; each worker serially reuses one keep-alive connection for queued request states. |
| H3 opens one QUIC link, one response stream, and disconnects on completion. | H3 opens one reusable QUIC link per origin, opens HTTP/3 connection streams once, and multiplexes request streams up to `M` and peer credit. |
| `mxParams()` creates four isolated threads. | Thread topology keeps transport Rx/Tx isolated and adds enough non-isolated scheduler capacity for bounded request scheduling. |
| Failed H3 Alt-Svc upgrade rewrites output with a second H1 run. | Each request writes only the current terminal body attempt, with final-path truncation at explicit attempt boundaries. |

Research notes used to refine the design:

- curl fetches multiple command-line URLs sequentially by default and uses
  `--parallel` for parallel transfers; this supports the plan's explicit
  `-j` bounded concurrency model: <https://curl.se/docs/manpage.html>
- everything curl documents that HTTP/2 and HTTP/3 multiplex concurrent
  transfers over one connection, while HTTP/1.x needs serial reuse or multiple
  connections: <https://everything.curl.dev/libcurl-http/multiplexing.html>
- everything curl documents bounded parallel transfer behavior where new
  transfers start as others complete:
  <https://everything.curl.dev/cmdline/urls/parallel.html>
- RFC 9114 requires HTTP/3 request/response exchanges on QUIC streams and at
  least the HTTP control stream plus QPACK encoder/decoder streams:
  <https://www.rfc-editor.org/info/rfc9114/>
- RFC 9112 defines HTTP/1.1 response body length rules, including
  connection-close-delimited responses when no content length or final chunked
  transfer coding is present: <https://www.rfc-editor.org/info/rfc9112/>

The implementation is feasible in the current codebase. The highest-risk area
is H3 lifecycle coordination because stream open/send and connection mutation
must occur on the QUIC Tx side while response processing occurs from QUIC stream
callbacks. The plan is intentionally vertical: first lock down CLI/output and
single-request behavior, then add request-owned redirect/fallback, then H1
multi-request behavior, then Alt-Svc/DNS caching, and finally H3 multiplexing.

## Architecture Documentation

### New or Changed Components

- `zhttp/example/zhttp.cc`
  - Add `-n`/`--requests` and `-j`/`--jobs`.
  - Replace single `State` with `Run` and `Req`.
  - Add request scheduling helpers shared by H1 and H3.
  - Make response parsing, body output, redirects, Alt-Svc, DNS probing,
    fallback, timeout, logging, and completion operate on a `Req`.
  - Make H1 use worker-owned links/parsers and H3 use stream-owned parsers.
  - Use an invocation-scoped origin discovery cache for DNS HTTPS RR and
    Alt-Svc results once request independence is established.
- `zhttp/test/ZhttpMultiRequestTest.cc`
  - New focused app-level/integration test binary for CLI validation, output
    naming, H1 reuse/concurrency, redirects, fallback publication, and failure
    isolation.
- `zhttp/test/Zhttp3InteropTest.cc`
  - Extend the existing H3 fixture for multi-stream client behavior over one
    QUIC connection, or add a small companion test if reuse would make the file
    too broad.
- `zhttp/test/Makefile.am`
  - Add the new multi-request test binary to the zhttp test build.
- No new third-party dependencies.
- No required library parser change: `zhttp/src/ZhttpH1.hh` already has
  close-delimited response support via `Parser::eof()`.

### Processes and Threads

- `main()` creates one `ZiMultiplex` for the whole run.
- Keep Rx and Tx thread IDs explicit for TCP/TLS/QUIC client params. The current
  example uses `"3"` and `"4"` for client Rx/Tx and `mx.rxThread() == 1`,
  `mx.txThread() == 2`.
- `mxParams(concurrency, plan)` should:
  - preserve existing isolated I/O/protocol threads for `N == 1`;
  - avoid making every request worker an isolated thread;
  - add non-isolated scheduler capacity when `M > 1`, sized conservatively from
    `M` but capped by actual needs if the implementation is callback-driven.
- H1 uses up to `M` active worker links. Each worker owns one connection and at
  most one active request at a time.
- H3 uses one `QUICClient::Link` per origin for the initial implementation.
  Link-local H3 connection streams are opened once with
  `Zhttp::H3::Cxn::openLocal()`. Request streams are opened on the QUIC Tx
  context as concurrency slots and peer stream credit permit.

### Interfaces

CLI additions:

```text
  -n, --requests=N  submit N GET requests, default 1
  -j, --jobs=M      run up to M requests concurrently, default 1;
                    valid only when N > 1; M must be <= N
```

Planned option fields:

```c++
struct Options {
  ZuCSpan ca;
  ZuCSpan output{"index.html"};
  uint32_t requests = 1;
  uint32_t concurrency = 1;
  ZuCSpan url;
  bool http3 = false;
  bool verbose = false;
  bool help = false;
};
```

Use `ZtCLI` `UInt32` fields for numeric option parsing:

```c++
(((requests),    (CLI::Opt<'n'>, CLI::Long<"requests">, Deflt<1>)),	UInt32),
(((concurrency), (CLI::Opt<'j'>, CLI::Long<"jobs">, Deflt<1>)),		UInt32),
```

`ZtCLI::load()` returns `-1` on syntax/parse errors and returns the positional
argument count on success. It does not expose per-option presence, so keep a
small pre-scan of raw `argv` for `-j`, grouped short options containing `j`, and
`--jobs`/`--jobs=...`. Leave all numeric parsing and range conversion to
`ZtCLI`.

Validation rules:

- `ZtCLI::load()` failure is a usage error.
- There must be exactly one URL positional argument.
- `requests >= 1`.
- `concurrency >= 1`.
- explicit `-j` requires `requests > 1`.
- `concurrency <= requests`.
- if `requests == 1`, effective concurrency is `1`.

### Data Flows

1. Pre-scan argv only for explicit jobs-option presence.
2. Parse CLI with `ZtCLI::load()`.
3. Validate CLI semantics before network initialization.
4. Parse the original URL once.
5. Initialize `Run` with `N` `Req` entries. Request `i` receives stable ID `i`,
   current URL equal to original URL, and output path:
   - `N == 1`: effective output path unchanged;
   - `N > 1`: append `.` and `i` to the effective output base.
6. Start `ZiMultiplex` and initialize the selected transport client.
7. Activate up to `M` requests.
8. Each active request independently performs redirects and protocol fallback.
9. Body writes open/truncate `req.output` lazily only for non-redirect terminal
   body attempts. Redirect bodies remain suppressed.
10. A terminal success or failure closes the request body file, updates
    aggregate counts, and releases one concurrency slot.
11. `main()` waits until all `N` requests are complete or fatal setup fails.
12. Final exit code is `0` only when there were no failed requests.

### Event-Driven and Timer Processing

Replace the single `sem.post()` completion with aggregate completion:

```c++
struct Run {
  Options options;
  URL originalURL;
  ZtArray<Req, ZtArrayHeapID<"Zhttp.Req">> reqs;
  ZmSemaphore done;
  unsigned scheduled = 0;
  unsigned active = 0;
  unsigned complete = 0;
  unsigned failed = 0;
  bool fatal = false;
};
```

- Existing `ClientTimeout` can remain the default timeout per logical attempt in
  early phases.
- Timeout marks only the current request failed unless it is a fatal setup
  timeout before a transport can be initialized.
- H1 workers and H3 streams call a common `finishReq(run, req, ok)` path only
  after parser completion and body-file close.
- `finishReq()` starts queued work if slots remain and posts `run.done` when
  `complete == requests` or `fatal`.
- Any shared `Run` counters mutated from multiple scheduler contexts need an
  explicit ownership rule. Preferred first implementation: route counter and
  queue mutation through one app scheduler context. If a lock is needed, keep it
  on the cold scheduling path and do not lock parser/body hot paths.

### Network Programming

H1/TCP and H1/TLS:

- Use sequential keep-alive reuse per worker.
- Do not pipeline.
- Add `Connection` response header parsing.
- Add HTTP version tracking if needed for reuse eligibility.
- Reset parser and per-response fields before reusing a link.
- If the peer closes after a complete response, mark the link closed and open a
  replacement only if more queued requests remain.
- If the parser is in EOF-body mode, `CliLink::disconnected()` must call
  `parser.eof()`. A response completed this way can be successful but the
  connection is not reusable.
- Reuse eligibility is false on parser error, body write error, timeout,
  explicit `Connection: close`, HTTP/1.0 without keep-alive, or any
  close-delimited body.
- Reuse eligibility is true after complete `content-length` or chunked response
  with no explicit close and compatible HTTP version semantics.

H3/QUIC:

- Open HTTP/3 connection streams once with `Zhttp::H3::Cxn::openLocal()`.
- Use one client-initiated bidirectional request stream per active request.
- Do not add a separate stream-ID hash for dispatch. `Zquic::Link` already has
  `findStream(id)`, and callbacks arrive on `QUICClient::Stream`; store the
  active `Req *` and `H3ResponseParser` on the stream object.
- For each request stream:
  - open `Zi::StreamType::Duplex` on the QUIC Tx context;
  - bind `stream->req` and reset/configure its parser;
  - record stream ID in `req.responseStreamID` for diagnostics;
  - send request HEADERS and FIN.
- `QUICClient::Stream::process()`:
  - for unidirectional streams, keep using `Zhttp::H3::CxnParser`;
  - for request streams with `req != nullptr`, process with the stream parser;
  - on stream completion, finish only that request;
  - do not disconnect the link while other active streams or queued requests
    remain.
- Link mutation and request-stream opening must happen on the QUIC Tx scheduler
  context. Use `txInvoke()` when entering from app/request scheduling.
- `Zquic::CliLink::send()` already copies payload and invokes Tx when not on
  the Tx context; request stream open still needs explicit Tx ownership because
  `stream()` mutates link state and asserts Tx-side invariants elsewhere.
- QUIC transport parameters must support concurrency:
  - `maxStreamsBidi >= M`;
  - `maxStreamsUni >= 3` for peer HTTP/3 control, encoder, and decoder streams,
    with existing `H3UniMax` remaining acceptable;
  - `maxData` and `maxStreamData` should remain at least the existing constants
    initially, with tests using smaller fixture values only where intentional.

### Data Stores

- No persistent data store is added.
- In-memory state uses Z containers:
  - `ZtArray<Req, ZtArrayHeapID<"Zhttp.Req">>` for request states.
  - `ZtArray<ZmRef<H1Worker>, ZtArrayHeapID<"Zhttp.H1Worker">>` or equivalent
    for H1 workers sized by `M`.
  - `ZmHash` for the origin-scoped Alt-Svc/DNS cache.
- No app-level H3 stream hash is needed in the primary design because
  `Zquic::Link` already maintains stream lookup and stream callbacks.
- Output files are opened lazily. The implementation truncates the final path at
  the first body write of each terminal attempt. Staging files and atomic rename
  remain a future hardening option, not part of this plan.

## Detailed Design and Implementation Plan

### Phase 1: CLI Contract, Output Naming, and Single-Request No-Op Path

Focus: user-facing contract without changing transport behavior.

- Extend `Options` with `requests` and `concurrency`.
- Add `hasJobsOpt(argc, argv)` for presence only.
- Add `validateOptions(options, argc, jobsSet)` or equivalent.
- Update `usage()` with new options, defaults, validation, and output naming.
- Add:

```c++
ZtString<> outputPath(ZuCSpan base, unsigned reqID, unsigned requests);
```

- Preserve exact current behavior for `requests == 1`:
  - default output remains `index.html`;
  - explicit `-o PATH` writes `PATH`;
  - redirect/fallback behavior remains in `main()` for this phase.
- Add focused tests for CLI validation and output-path helper before changing
  transport state.

Dependency notes:

- This phase depends only on `ZtCLI`, `ZtString`, and existing URL parsing.
- No transport API behavior changes are required.

### Phase 2: Request/Run State Refactor While Preserving One Request

Focus: move state ownership from process-global single request to `Req`, while
still executing one request end-to-end exactly as today.

Replace `State` with:

```c++
struct Req {
  unsigned id = 0;
  URL url;
  Protocol::T protocol = Protocol::H1;
  H3CxnState::T h3State = Zhttp::H3::CxnState::Init;
  ZtString<> output;
  ZtString<> altSvcHost;
  uint16_t altSvcPort = 0;
  ZtString<> location;
  int64_t responseStreamID = -1;
  unsigned redirects = 0;
  unsigned status = 0;
  ZiFile bodyFile;
  int64_t contentLength = -1;
  uint64_t bodyBytes = 0;
  unsigned bodyChunks = 0;
  bool bodyFileOpen = false;
  bool chunked = false;
  bool connectionClose = false;
  bool http10 = false;
  bool altSvcH3 = false;
  bool redirecting = false;
  bool framingLogged = false;
  bool done = false;
  bool failed = false;
};
```

Add the `Run` aggregate shown above.

- Convert `RequestOps`, request builders, `parseAltSvc()`, `ResponseSink`,
  `ResponseParser`, logging helpers, and result extraction from `State &` to
  `Req &`.
- Keep a compatibility execution path:

```c++
template <typename Client>
int runOne(ZiMultiplex &mx, Run &run, Req &req, RequestResult *result = nullptr);
```

- Ensure response body opens `req.output`, not `options.output`.
- Add request ID to log lines only when `run.options.requests > 1`:

```text
req=2 status: 200
req=2 body complete: 1234 bytes in 1 chunks
```

- Add `resetAttempt(req, truncateOutput)`:
  - closes any open body file;
  - clears status/location/Alt-Svc/framing/parser-facing body counters;
  - clears `done` for the next attempt;
  - if `truncateOutput`, opens `req.output` with `ZiFile::Write | ZiFile::GC`
    and immediately closes it, or marks that the next body write must open with
    truncation before appending any bytes.

Dependency notes:

- This phase creates the ownership boundary required by all later phases.
- Keep `Client` structs otherwise close to their current form to reduce risk.

### Phase 3: Per-Request Redirect and Fallback Attempts, Still Serial

Focus: make logical requests independent while still running `N` serially with
effective concurrency `1`.

- Move the redirect loop from `main()` into:

```c++
int runReqSerial(ZiMultiplex &mx, Run &run, Req &req);
```

- Preserve request ID, output path, body counters, and failure state across
  redirects as appropriate, while resetting attempt-specific parser/framing
  fields.
- Redirect response bodies remain suppressed via `req.redirecting`.
- Implement per-request equivalents of:
  - direct H1 for `http://`;
  - direct H3 when `--http3`;
  - DNS HTTPS RR H3 probe;
  - H1 Alt-Svc first;
  - H3 Alt-Svc probe;
  - H1 fallback after failed H3 probe.
- At each terminal body-writing attempt, truncate/restart the final output path
  before writing. This makes the current final-path publication policy explicit
  and removes the old partial-H3-output ambiguity.
- Keep automatic retry for mid-body H1 close or QUIC connection failure out of
  scope. Mark the affected `Req` failed.
- Add serial `-n > 1 -j 1` support over the existing one-shot connection
  behavior as a transitional slice, even before H1 keep-alive worker reuse.

Dependency notes:

- This phase depends on Phase 2's `Req` boundary.
- It deliberately avoids H1 worker reuse and H3 multiplexing so redirect and
  fallback correctness can be tested first.

### Phase 4: H1 Worker Pool and Keep-Alive Reuse

Focus: `N > 1` over HTTP and HTTPS with bounded concurrency and serial reuse per
connection.

Add H1 worker state:

```c++
struct H1Worker {
  unsigned id = 0;
  ZmRef<Link> link;
  Req *req = nullptr;
  bool connected = false;
  bool reusable = false;
  bool closing = false;
};
```

- Scheduler starts `min(M, N)` workers.
- Each worker:
  1. takes the next queued request;
  2. connects if it has no reusable link;
  3. sends one request;
  4. parses one response with a worker/link-owned parser bound to the current
     `Req`;
  5. completes/fails the request;
  6. if reusable, resets parser and takes another queued request;
  7. otherwise disconnects and reconnects only if more work is queued.
- Add `connection` to `ResponseHeaders` and parse `Connection: close` and
  `Connection: keep-alive`.
- Capture response HTTP version from the parser if needed for HTTP/1.0
  keep-alive semantics. If parser callback support is missing for response
  version, add the minimal callback to `Zhttp::H1::Parser` rather than inferring
  from headers alone.
- Use existing `Zhttp::H1::Parser::reset()` after complete non-EOF responses.
- Use existing `Zhttp::H1::Parser::eof()` in `disconnected()` for EOF-framed
  responses.
- Never reuse a connection after EOF-framed completion.
- Add worker/link IDs in verbose connection logs.

Dependency notes:

- This phase depends on Phase 3's per-request redirect/fallback entry point.
- This phase does not require H3 work.

### Phase 5: Invocation-Scoped Alt-Svc/DNS Discovery Cache

Focus: avoid repeated H3 discovery probes while preserving per-request fallback
correctness.

Add an origin key and cache:

```c++
struct Origin {
  ZtString<> scheme;
  ZtString<> host;
  uint16_t port = 0;
};

struct OriginDiscovery {
  bool dnsChecked = false;
  bool dnsH3 = false;
  AltSvcEndpoint dnsEndpoint;
  bool altSvcChecked = false;
  bool altSvcH3 = false;
  AltSvcEndpoint altSvcEndpoint;
};
```

- Store entries in `ZmHash` keyed by scheme/host/port. Use existing Z hash/key
  patterns rather than STL containers.
- Cache only within one invocation.
- Cache positive and negative DNS HTTPS RR H3 discovery results.
- Cache Alt-Svc results learned from H1 terminal responses for that origin.
- Do not persist Alt-Svc and do not share cache across processes.
- If a cached H3 endpoint fails for a request, fall back for that request only;
  do not globally disable the endpoint unless the implementation records a
  clear in-run negative result with a conservative policy.

Dependency notes:

- This phase depends on Phase 3 so cached discovery can be consumed by each
  request attempt without reintroducing process-global request state.
- It should land before H3 multiplexing so the H3 scheduler sees stable origin
  selection behavior.

### Phase 6: H3 Multi-Stream Scheduler

Focus: `N > 1 --http3 -j M` over one QUIC connection where possible.

- Change `QUICClient` from one `State`/stream ID to one `Run` plus one H3 link.
- `QUICClient::Link::connected()` opens local H3 connection streams once and
  asks the scheduler to open request streams.
- Add request state to `QUICClient::Stream`:

```c++
struct Stream : public Zquic::CliStream<Link, Stream>,
                public Zhttp::H3::CxnParser<Stream> {
  Req *req = nullptr;
  H3ResponseParser<Link> parser;
};
```

- Construct/reset each stream parser when binding a request.
- For request activation:
  - invoke onto QUIC Tx context;
  - call `link->stream(Zi::StreamType::Duplex)`;
  - if stream credit is unavailable and `stream()` returns null, leave the
    request queued and retry when `MAX_STREAMS` extends credit or an active
    stream completes;
  - bind the request and send the H3 request.
- `QUICClient::Stream::process()` handles:
  - unidirectional H3 connection streams through `CxnParser`;
  - request streams through `parser.process(*this)` when `req` is non-null.
- On request stream completion, clear `stream->req`, finish that request, and
  keep the QUIC link open while active or queued requests remain.
- On H3 connection-level error, fail all active H3 requests for that link and
  let each request's fallback policy decide whether to run H1.
- On request stream reset/cancel, fail only that request when possible.
- Set QUIC limits from concurrency:
  - `maxStreamsBidi` at least `M`;
  - `maxStreamsUni` at least `H3UniMax` or another documented value >= 3;
  - keep existing `H3DataMax` and `H3StreamDataMax` unless tests prove a need
    for tuning.

Dependency notes:

- This phase depends on Phase 5 for stable origin selection and Phase 3 for
  per-request fallback after H3 failure.
- This phase uses `Zquic`'s built-in stream lookup and object callbacks; no new
  dispatch container is planned.

### Phase 7: Aggregate Scheduling, Failure Isolation, and Diagnostics

Focus: unify final behavior across transports.

Add common scheduler functions:

```c++
Req *nextReq(Run &run);
void activateReq(Run &run, Req &req);
void finishReq(Run &run, Req &req, bool ok);
```

- `finishReq()` decrements active, increments complete/failed, starts more
  queued requests if possible, and posts `run.done` only when all `N` are
  complete or fatal setup error occurs.
- A request-level error must not cancel queued or active unrelated requests.
- Fatal process-level errors include invalid CLI, URL parse failure,
  `ZiMultiplex` startup failure, or inability to initialize the selected
  transport engine.
- Runtime error logs should include:
  - `req=<id>` when `N > 1`;
  - URL;
  - output path;
  - transport;
  - phase: connect, send, parse, body write, redirect, timeout, fallback.
- Ensure final exit code is non-zero if any request failed.
- Confirm single-request logs remain close to current output unless verbose mode
  is enabled.

Dependency notes:

- Earlier phases may have local transitional scheduling. This phase removes
  temporary duplication and centralizes completion semantics.

### Phase 8: Tests and Acceptance Hardening

Focus: prove requirements and prevent regressions.

- Add `zhttp/test/ZhttpMultiRequestTest.cc`.
- Add it to `zhttp/test/Makefile.am`.
- Extend or reuse `zhttp/test/Zhttp3InteropTest.cc` for H3 client multi-stream
  assertions.
- Prefer existing `ZuTest`/`ZuTestUtil`, `TempDir`, Caddy fixture, and local
  client/server patterns from `Zhttp3InteropTest.cc` and `ZhttpAppTest.cc`.
- Keep tests deterministic by using loopback ports, temporary directories, and
  bounded wait helpers.

## Code References to Impacted Code

- `zhttp/example/zhttp.cc:25` - Extend `Options` and CLI mapping with
  `-n`/`-j`.
- `zhttp/example/zhttp.cc:39` - Update `usage()` with new options and
  multi-output rules.
- `zhttp/example/zhttp.cc:65` - Add `connection` to `ResponseHeaders` and add
  response version handling if needed.
- `zhttp/example/zhttp.cc:83` - Replace single `State` with `Req` and `Run`.
- `zhttp/example/zhttp.cc:184` - Change `parseAltSvc()` from `State &` to
  `Req &` and later integrate discovery cache.
- `zhttp/example/zhttp.cc:270` - Change `RequestOps` and request builders to
  bind to `Req`.
- `zhttp/example/zhttp.cc:343` - Change `ResponseSink` to write `req.output`,
  parse connection headers, and include request IDs where applicable.
- `zhttp/example/zhttp.cc:503` - Change `CliLink` from single parser/state to
  worker/request-bound H1 parsers and stream-bound H3 parsers.
- `zhttp/example/zhttp.cc:671` - Replace single H3 response stream ID dispatch
  with per-stream request/parser state.
- `zhttp/example/zhttp.cc:723` - Replace `run<Client>()` with request attempt
  and transport scheduler functions.
- `zhttp/example/zhttp.cc:786` - Make `mxParams()` concurrency-aware.
- `zhttp/example/zhttp.cc:838` - Move H3 direct, H1 Alt-Svc, and DNS fallback
  into per-request attempt logic and later discovery cache.
- `zhttp/example/zhttp.cc:884` - Change `main()` to initialize one `Run`, start
  the aggregate scheduler, wait for aggregate completion, and compute final
  exit code.
- `zhttp/src/ZhttpH1.hh:222` - Existing parser transition into EOF-body mode
  for responses without content length/chunking.
- `zhttp/src/ZhttpH1.hh:346` - Existing `Parser::eof()` completion path for
  close-delimited bodies.
- `zhttp/src/ZhttpH1.hh:355` - Existing `Parser::reset()` for parser reuse.
- `zhttp/src/ZhttpH3Session.hh:27` - Existing `Zhttp::H3::Cxn::openLocal()`
  opens control, encoder, and decoder streams.
- `zhttp/src/ZhttpH3Session.hh:154` - Existing H3 helper uses
  `findStream(stream.id())` when sending FIN.
- `zquic/src/Zquic.hh:780` - Existing `txInvoke()` and Tx context helpers.
- `zquic/src/Zquic.hh:1661` - Existing local stream creation.
- `zquic/src/Zquic.hh:1686` - Existing `findStream()` stream lookup.
- `zquic/src/Zquic.hh:2803` - Existing `CliLink::send()` invokes Tx when
  called off the Tx context.
- `zt/src/ZtCLI.hh:1294` - `ZtCLI::load()` returns positional count or `-1`;
  it does not report option presence.
- `zi/src/ZiFile.hh:36` - `ZiFile::Write` includes create, write-only, and
  truncate.
- `zi/src/ZiFile.hh:163` - `ZiFile::rename()` exists but is not part of the
  first output publication design.
- `zhttp/test/Makefile.am:20` - Add the new multi-request test binary.
- `zhttp/test/ZhttpAppTest.cc:20` - Reuse script-driven local app testing style
  where applicable.
- `zhttp/test/Zhttp3InteropTest.cc:680` - Reuse existing H3 client/server
  stream patterns.
- `zhttp/test/Zhttp3InteropTest.cc:954` - Reuse Caddy H3 fixture and QUIC
  parameter setup style.

## Detailed Test Plan

Build-level:

- `make -j` after configure.
- During iteration, narrow to:
  - `make -C zhttp/test ZhttpMultiRequestTest`
  - `make -C zhttp/test Zhttp3InteropTest`
  - `make -C zhttp/test ZhttpAppTest`

CLI validation tests:

- default `zhttp URL` remains accepted.
- malformed numeric `-n`/`-j` values fail through `ZtCLI`.
- `-n 0` and `-j 0` fail semantic validation.
- `-j 2 URL` fails because default `N` is `1`.
- `-n 1 -j 1 URL` fails because explicit `-j` is only valid in
  multi-request mode.
- `-n 2 -j 3 URL` fails.
- `-n 3 URL` accepts default output base.
- `--jobs=2 -n 3 URL`, `--jobs 2 -n 3 URL`, and grouped short option forms
  involving `j` are handled consistently with the explicit-jobs pre-scan.

Output naming and publication tests:

- `-n 3 -o body URL` creates `body.0`, `body.1`, and `body.2`.
- `-n 3 URL` creates `index.html.0`, `index.html.1`, and `index.html.2` in the
  test temp directory.
- single request still writes exactly `PATH` or `index.html`, without suffix.
- a failed H3 probe followed by H1 fallback leaves only the successful terminal
  body in the final output path.
- redirect responses do not write bodies to the final output path.

H1 tests:

- `-n 3 -j 1` against a local keep-alive server observes one connection and
  three requests when the server permits reuse.
- `-n 6 -j 2` against a delayed local server observes at most two active
  requests and correct output files.
- close-delimited response bodies complete successfully on EOF and are not
  followed by connection reuse.
- `Connection: close` prevents reuse.
- parser error or body write failure fails only the affected request.

H3 tests:

- Extend `Zhttp3InteropTest` or add a fixture that records QUIC connection count
  and request stream IDs.
- Run `zhttp --http3 -n 4 -j 2 ...` and verify multiple terminal bodies complete
  over one QUIC connection where peer credit allows.
- Verify request streams are client-initiated bidirectional streams.
- Verify H3 control, encoder, and decoder streams are opened once per link.
- Verify a request stream failure is request-scoped when possible.
- Verify a connection-level H3 error fails active H3 requests and does not
  corrupt already completed outputs.

Redirect/fallback tests:

- multiple requests independently follow redirects and write only terminal
  bodies.
- redirect count is per request.
- too many redirects fails only the affected request.
- Alt-Svc learned by one request can be reused by later requests through the
  invocation-scoped cache.
- negative DNS HTTPS RR lookup is not repeated for every request after the
  cache is added.

Regression tests:

- single HTTP, HTTPS, direct H3, DNS/Alt-Svc fallback, explicit `-o`, default
  `index.html`, and verbose logging continue to pass.
- Existing parser tests remain green:
  - `./zhttp/test/ZhttpParserTest`
  - `./zhttp/test/ZhttpFallbackTest`
  - `./zhttp/test/ZhttpQPackTest`
  - `./zhttp/test/ZhttpQPackDynamicTest`

Manual smoke tests:

```text
./zhttp/example/zhttp -n 3 http://127.0.0.1:<port>/file
./zhttp/example/zhttp -n 6 -j 2 -o /tmp/body http://127.0.0.1:<port>/file
./zhttp/example/zhttp --http3 -c cert.pem -n 4 -j 2 -o /tmp/h3 https://localhost:<port>/file
```

Diagnostics checks:

- verbose logs contain request IDs in multi-request mode.
- H1 connection logs include worker/link context.
- H3 stream logs include request ID and stream ID.
- final summary or exit behavior makes partial failure visible.

## Acceptance Criteria

- `zhttp URL` behaves as before and writes `index.html`.
- `zhttp -o PATH URL` behaves as before and writes `PATH`.
- `zhttp -n 3 URL` writes `index.html.0`, `index.html.1`, and
  `index.html.2`.
- `zhttp -n 3 -o body URL` writes `body.0`, `body.1`, and `body.2`.
- Invalid `-n`/`-j` combinations fail before network initialization with a
  usage error.
- At most `M` logical requests are in flight.
- H1 with `-n > 1 -j 1` reuses a keep-alive connection when the server permits.
- H1 with `-j M` opens no more than `M` active worker links.
- H1 close-delimited responses complete on EOF and do not reuse the connection.
- H3 with `-j M --http3` uses concurrent request streams on one QUIC connection
  when stream credit allows.
- Redirects and fallback preserve request IDs and output paths.
- Failed requests produce a non-zero final exit without cancelling unrelated
  queued or active requests except for fatal setup errors.
- No staging files are required for the first implementation; final output paths
  are truncated at explicit terminal-attempt boundaries.

## Non-goals

- No HTTP methods beyond GET.
- No request bodies.
- No multiple distinct URL arguments.
- No response aggregation.
- No progress meter.
- No HTTP/2.
- No cookies, persistent cache, persistent Alt-Svc cache, or cross-process
  connection reuse.
- No H1 pipelining.
- No automatic retry policy for mid-response H1 close, QUIC connection failure,
  or body-write failure.
- No process-per-request implementation.
- No staging-file plus atomic-rename output publication in the first
  implementation.
- No new third-party dependency.

## Options and Open Questions

No blocking open questions remain.

Resolved implementation decisions:

- Use final-path truncation for output publication. Revisit staging plus atomic
  rename only if later requirements demand crash-safe publication or preserving
  previous successful outputs across failed program invocations.
- Use `Zquic` stream ownership and per-stream parser/request pointers for H3
  dispatch. Do not add a separate app-level stream hash.
- Use existing `Zhttp::H1::Parser::eof()` for close-delimited bodies.
- Add invocation-scoped origin discovery caching with `ZmHash`.
- Keep request scheduling and aggregate completion ownership explicit. Prefer a
  single app scheduler context for queue/counter mutation; use a cold-path lock
  only if the final implementation cannot cleanly preserve that ownership.
