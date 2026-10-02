## Summary

The goal is to turn `zhttp/example/zhttp.cc` from a single-request example
client into a bounded multi-request client for one URL. The new CLI controls are
`-n N` for logical request count and `-j M` for maximum concurrent logical
requests. `N` defaults to `1`, `M` defaults to `1`, `-j` is valid only with an
explicit `-n` where `N > 1`, and `M > N` is a usage error. For `N > 1`, response
bodies are written to `<output-base>.<request-id>`, using the existing default
base `index.html` when `-o` is not supplied.

Current state changes to planned state:

| Current | Planned |
| --- | --- |
| One `Options`, one `State`, one parser, one link, one body file. | One aggregate `Run` plus `Req` state per logical request, with per-link or per-stream parser ownership. |
| `main()` performs redirects around one request result. | Each request owns redirect count, current URL, fallback attempts, output path, parser/file state, and completion. |
| H1 opens one connection and exits after one response. | H1 uses up to `M` worker links; each link serially reuses a keep-alive connection for queued request states. |
| H3 opens one QUIC link, one response stream, and disconnects on completion. | H3 opens one reusable QUIC link per origin and multiplexes up to `M` active request streams where stream credit allows. |
| `mxParams()` creates four isolated threads. | Thread topology keeps transport Rx/Tx isolated and adds `M` non-isolated app workers for request scheduling. |
| Failed H3 Alt-Svc upgrade rewrites the output with a second H1 run. | Each request publishes only a terminal body, using staging/truncation so failed fallback attempts do not leave ambiguous output. |

Research notes:

- curl’s man page documents sequential multi-URL transfers by default, `--parallel` for parallelism, connection reuse within one invocation, and the need for distinct output handling for multiple transfers: <https://curl.se/docs/manpage.html>
- everything curl documents HTTP/2 and HTTP/3 multiplexing as concurrent transfers over one connection, unlike earlier HTTP versions: <https://everything.curl.dev/libcurl-http/multiplexing.html>
- everything curl documents bounded parallel transfer behavior and starting new transfers as others complete: <https://everything.curl.dev/cmdline/urls/parallel.html>
- RFC 9114 defines HTTP/3 as HTTP semantics over QUIC with reliable per-stream delivery, QUIC-managed flow control, and HTTP request streams: <https://datatracker.ietf.org/doc/html/rfc9114>
- HTTP/3 explained documents QUIC unidirectional and bidirectional stream concurrency and request/response use of client-initiated bidirectional streams: <https://http3-explained.haxx.se/en/quic/quic-streams> and <https://http3-explained.haxx.se/en/h3/h3-streams>

The proposed implementation is feasible in the current codebase, but H3 stream
ownership and close-delimited H1 response handling are the highest-complexity
areas. The plan below sequences the work in vertical slices so the refactor is
testable before adding full H1/H3 concurrency.

## Architecture Documentation

### New or Changed Components

- `zhttp/example/zhttp.cc`
  - Add `ZtCLI` parsing and simple semantic validation for `-n` and `-j`.
  - Replace `State` with `Run` and `Req` model.
  - Add bounded request scheduler and aggregate completion.
  - Split transport execution into H1 worker pool and H3 stream scheduler.
  - Make response parsing, body output, redirects, Alt-Svc, fallback, timeout,
    and logging operate on a `Req`.
- `zhttp/test/ZhttpMultiRequestTest.cc`
  - New focused app-level test binary for CLI validation, H1 multi-output,
    H1 reuse/concurrency, redirect independence, and failure isolation.
- `zhttp/test/Zhttp3InteropTest.cc`
  - Extend or reuse H3 fixtures for multi-stream client behavior on one QUIC
    connection.
- `zhttp/test/Makefile.am`
  - Add `ZhttpMultiRequestTest` to `noinst_PROGRAMS` and `TESTS`.
- Potential library changes:
  - `zhttp/src/ZhttpH1.hh` only if close-delimited response body handling is
    implemented generically in the parser rather than handled conservatively in
    the example client.
  - No new third-party dependencies.

### Processes and Threads

- `main()` creates one `ZiMultiplex` for the whole run.
- `mxParams(unsigned concurrency, TransportPlan plan)` computes:
  - isolated I/O Rx and Tx threads;
  - isolated protocol Rx/Tx threads for TCP/TLS/QUIC as needed;
  - `M` non-isolated app worker threads named predictably.
- H1 uses up to `M` active `CliLink` worker objects. Each worker owns one
  connection and one active `Req` at a time.
- H3 uses one preferred `QUICClient::Link` per origin. It opens HTTP/3 control,
  encoder, and decoder streams once, then opens request streams as scheduler
  slots and QUIC stream credit permit.

### Interfaces

CLI additions:

```text
  -n N               submit N GET requests, default 1
  -j M               run up to M requests concurrently, default 1;
                     valid only when N > 1; M must be <= N
```

Planned option fields:

```c++
struct Options {
  ZuCSpan ca;
  ZuCSpan output{"index.html"};
  unsigned requests = 1;
  unsigned concurrency = 1;
  ZuCSpan url;
  bool jSet = false;
  bool http3 = false;
  bool verbose = false;
  bool help = false;
};
```

Use `ZtCLI` `UInt32` fields for numeric option parsing. Do not add a custom
decimal parser. The only extra CLI bookkeeping needed is whether `-j` was
explicitly supplied, so the code can enforce that `-j` is only used with
multi-request mode. If presence detection is needed, keep it to a minimal raw
argv check for `-j`/`--jobs`; leave numeric syntax and range parsing to
`ZtCLI`.

### Data Flows

1. Parse and validate CLI.
2. Parse the original URL once.
3. Initialize `Run` with `N` `Req` entries. Request `i` receives stable ID `i`,
   current URL equal to original URL, and output path:
   - `N == 1`: effective output path unchanged;
   - `N > 1`: append `.` and `i` to the effective output base.
4. Start the transport engine and scheduler.
5. The scheduler activates up to `M` requests.
6. Each active request performs redirects and fallback attempts independently.
7. A terminal success or failure closes/stabilizes that request’s body file,
   updates aggregate counts, and releases one concurrency slot.
8. `main()` waits until `complete == N` or fatal setup failure, then exits `0`
   only when `failed == 0`.

### Event-Driven and Timer Processing

- Replace the single `sem.post()` completion with aggregate completion:

```c++
struct Run {
  ZmSemaphore done;
  unsigned scheduled = 0;
  unsigned active = 0;
  unsigned complete = 0;
  unsigned failed = 0;
  bool fatal = false;
};
```

- Existing `ClientTimeout` may remain as the default timeout per logical
  attempt. Timeout marks the current request failed and schedules the next
  queued request.
- H1 workers and H3 streams call a common `finishReq(req, ok)` path after all
  writes and parser finalization complete.

### Network Programming

- H1/TCP and H1/TLS:
  - Use sequential keep-alive reuse per worker.
  - Do not pipeline.
  - Add `Connection` response header parsing and keep-alive eligibility.
  - Reset parser and per-response fields before reusing a link.
  - If the peer closes after a complete response, mark the link closed and let
    the worker open a replacement for remaining queued requests.
- H3/QUIC:
  - Open HTTP/3 connection streams once via `Zhttp::H3::Cxn::openLocal()`.
  - Use one bidirectional request stream per active request.
  - Dispatch inbound stream data by stream ID to its request/parser.
  - Keep the QUIC link open until all active streams complete.
  - Treat stream failure as request-scoped when possible and connection failure
    as affecting all active streams on that connection.

### Data Stores

- No persistent data store is added.
- In-memory state uses Z containers:
  - `ZtArray<Req, ZtArrayHeapID<"Zhttp.Req">>` for request states.
  - H3 stream lookup can start as a `ZmHash` keyed by stream ID if active stream
    count exceeds trivial linear lookup needs; prefer `ZmHash` because stream
    dispatch is on the receive path.
  - H1 workers can be a `ZtArray<ZmRef<Link>>` sized by `M`.
- Output files are opened lazily. For fallback/probe safety, the first version
  should truncate/restart the final path before the winning terminal attempt
  writes; staging plus atomic publish is cleaner but more code.

## Detailed Design and Implementation Plan

### Phase 1: CLI Validation, Output Naming, and Thread Topology

Focus: user-facing contract without changing transport behavior.

- Add `-n` and `-j` fields to `Options` as `ZtCLI` numeric options:

```c++
(((requests),    (CLI::Opt<'n'>, CLI::Long<"requests">, Deflt<1>)),	UInt32),
(((concurrency), (CLI::Opt<'j'>, CLI::Long<"jobs">, Deflt<1>)),		UInt32),
```

- Validation rules:
  - `ZtCLI::load()` failure is a usage error;
  - `requests >= 1`;
  - `concurrency >= 1`;
  - explicit `-j` requires `requests > 1`;
  - `concurrency <= requests`;
  - if `requests == 1`, effective concurrency is `1`.
- Update `usage()` with defaults, validation rules, and output naming.
- Add `outputPath(options.output, requestID, requests)` helper.
- Change `mxParams()` to accept `concurrency` and a transport plan. For the
  first phase it can keep the existing four isolated threads for `N == 1`, but
  for `M > 1` it must add non-isolated app threads rather than making every
  thread isolated.

### Phase 2: Request/Run State Refactor Preserving Single Request Behavior

Focus: split state ownership while preserving current `N == 1` behavior.

- Replace `State` with:

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
  unsigned redirect = 0;
  unsigned status = 0;
  ZiFile bodyFile;
  int64_t contentLength = -1;
  uint64_t bodyBytes = 0;
  unsigned bodyChunks = 0;
  bool bodyFileOpen = false;
  bool chunked = false;
  bool connectionClose = false;
  bool altSvcH3 = false;
  bool redirecting = false;
  bool framingLogged = false;
  bool done = false;
  bool failed = false;
};

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

- Convert `RequestOps`, request builders, `parseAltSvc()`, `ResponseSink`,
  `ResponseParser`, logging helpers, and result extraction to take `Req &`.
- Keep a compatibility path `runOne<Client>(mx, run, req, result)` so all
  current redirect and fallback tests continue before introducing parallelism.
- Add request ID to log lines only when `run.options.requests > 1`:

```text
req=2 status: 200
req=2 body complete: 1234 bytes in 1 chunks
```

- Ensure response body opens `req.output`, not `options.output`.

### Phase 3: Per-Request Redirect and Fallback State

Focus: make multiple logical requests independent while still running serially.

- Move the redirect loop from `main()` into `runReq(run, req)`.
- Preserve request ID, output path, body counters, and failure state across
  redirects while resetting attempt-specific parser/framing fields.
- Redirect response bodies remain suppressed through `req.redirecting`.
- Implement `resetAttempt(req, truncateOutput)`:
  - closes any open body file;
  - clears status/location/Alt-Svc/framing/parser-facing body counters;
  - optionally truncates `req.output` before an H1 fallback retry or H3 winning
    attempt writes.
- Introduce invocation-scoped Alt-Svc/DNS cache by origin only after correctness
  is established. Initial implementation may probe per request; that is less
  efficient but correct.
- Keep automatic retry for mid-body H1 close or QUIC connection failure out of
  scope; mark the affected `Req` failed.

### Phase 4: H1 Worker Pool and Connection Reuse

Focus: `N > 1` over HTTP and HTTPS with bounded concurrency.

- Add H1 worker state:

```c++
struct H1Worker {
  unsigned id = 0;
  ZmRef<Link> link;
  Req *req = nullptr;
  bool connected = false;
  bool reusable = false;
};
```

- Scheduler starts `min(M, N)` workers. Each worker:
  1. takes the next queued request;
  2. connects if it has no reusable link;
  3. sends one request;
  4. parses one response with a link-owned parser bound to the current `Req`;
  5. completes/fails the request;
  6. if reusable, resets parser and takes another queued request;
  7. otherwise disconnects and reconnects only if more work is queued.
- Add `connection` to `ResponseHeaders` and parse it for `close`.
- Reuse eligibility:
  - false on parser error, body write error, timeout, explicit connection close,
    HTTP/1.0 without keep-alive, or close-delimited response not fully drained;
  - true after complete `content-length` or chunked response with no close.
- Close-delimited H1 responses are the main parser implication. Preferred
  implementation is to extend `Zhttp::H1::Parser` with an optional response
  mode that treats EOF as completion when no content-length/chunked framing is
  present. Conservative fallback is to mark such responses non-reusable and
  avoid writing a partial ambiguous body; if body correctness cannot be
  guaranteed, surface this as a blocking implementation issue before coding.
- Add worker/link IDs in verbose connection logs.

### Phase 5: H3 Multi-Stream Scheduler

Focus: `N > 1 --http3 -j M` over one QUIC connection where possible.

- Change `QUICClient` from one `State`/stream ID to one `Run` plus one H3 link.
- `QUICClient::Link::connected()` opens local H3 connection streams once and
  asks the scheduler to open request streams.
- Add active stream table:

```c++
struct H3StreamState {
  Req *req = nullptr;
  H3ResponseParser<Link> parser;
};
```

Key it by stream ID, either in a `ZmHash` or in the `Req` if all stream dispatch
remains on the QUIC Rx thread and active stream count is small. Prefer `ZmHash`
for clear O(1) dispatch.

- For each activated request:
  - open `Zi::StreamType::Duplex`;
  - record stream ID in `req.responseStreamID`;
  - install parser state;
  - send request headers and FIN using `sendH3Request()`.
- `QUICClient::Stream::process()`:
  - for unidirectional streams, keep using `CxnParser`;
  - for request streams, find the matching `Req` by `this->id()` and process
    with that stream’s parser;
  - on complete stream, finish only that `Req` and do not disconnect the link
    while other active streams exist.
- Link mutation and request-stream opening must happen on the link’s owning
  scheduler context. If app workers trigger scheduling, route the actual stream
  open/send through `txInvoke()` or an equivalent link-thread invocation, as
  used in `zhttp/test/Zhttp3InteropTest.cc` for H3 response sending.
- Set QUIC flow limits from concurrency:
  - `maxStreamsBidi` at least `maximum(H3BidiMax, M + control allowance)` or
    a documented value that supports `M` request streams;
  - `maxData` and `maxStreamData` high enough for expected response bodies or
    failure/queue behavior if peer credit is insufficient.

### Phase 6: Aggregate Scheduling, Failure Isolation, and Diagnostics

Focus: final multi-request behavior across transports.

- Add common scheduler functions:

```c++
Req *nextReq(Run &run);
void activateReq(Run &run, Req &req);
void finishReq(Run &run, Req &req, bool ok);
```

- `finishReq()` decrements active, increments complete/failed, starts more
  queued requests if possible, and posts `run.done` only when all `N` are
  complete or a fatal setup error occurs.
- A request-level error must not cancel queued or active unrelated requests.
- A fatal process-level error includes invalid CLI, URL parse failure,
  `ZiMultiplex` startup failure, or inability to initialize the selected
  transport engine.
- Runtime error logs should include:
  - `req=<id>` when `N > 1`;
  - URL;
  - output path;
  - transport;
  - phase: connect, send, parse, body write, redirect, timeout, fallback.
- Ensure final exit code is non-zero if any request failed.

### Phase 7: Tests and Acceptance Hardening

Focus: prove requirements and prevent regressions.

- Add `zhttp/test/ZhttpMultiRequestTest.cc`.
- CLI validation coverage:
  - default `zhttp URL` remains accepted;
  - malformed numeric `-n`/`-j` values fail through `ZtCLI`;
  - `-n 0` and `-j 0` fail semantic validation;
  - `-j 2 URL` fails because default `N` is `1`;
  - `-n 1 -j 1` fails because `-j` is only valid in multi-request mode;
  - `-n 2 -j 3` fails;
  - `-n 3 URL` accepts default output base.
- H1 app tests:
  - `-n 3 -o body URL` creates `body.0`, `body.1`, `body.2`;
  - `-n 3 URL` creates `index.html.0`, `index.html.1`, `index.html.2` in the
    test temp directory;
  - `-n 3 -j 1` against a local keep-alive server observes one connection and
    three requests;
  - `-n 6 -j 2` against a server with delayed responses observes at most two
    active requests and correct output files.
- H3 test:
  - Extend `Zhttp3InteropTest` or add a new fixture that records QUIC connection
    count and request stream IDs, then run `zhttp --http3 -n 4 -j 2 ...` and
    verify multiple terminal bodies complete over one QUIC connection.
- Redirect tests:
  - multiple requests independently follow redirects and write only terminal
    bodies.
- Failure tests:
  - one body write failure or mid-response close yields non-zero final exit and
    does not invalidate unrelated completed outputs.
- Regression tests:
  - single HTTP, HTTPS, direct H3, DNS/Alt-Svc fallback, explicit `-o`, default
    `index.html`, and verbose logging continue to pass.

## Code References to Impacted Code

- `zhttp/example/zhttp.cc:25` - Extend `Options` and CLI struct mapping with
  `-n`/`-j` string fields and validation-derived unsigned values.
- `zhttp/example/zhttp.cc:44` - Update `usage()` with new options, defaults,
  validation, and multi-request output naming.
- `zhttp/example/zhttp.cc:83` - Replace single `State` with `Req` and `Run`.
- `zhttp/example/zhttp.cc:184` - Change `parseAltSvc()` from `State &` to
  `Req &`.
- `zhttp/example/zhttp.cc:270` - Change `RequestOps` and request builders to
  bind to a request state.
- `zhttp/example/zhttp.cc:343` - Change `ResponseSink` to write per-request
  output paths and include request IDs in logs.
- `zhttp/example/zhttp.cc:503` - Change `CliLink` from one parser/state to
  worker/request-bound parsers and, for H3, stream-bound parsers.
- `zhttp/example/zhttp.cc:671` - Replace single H3 response stream dispatch
  with stream-ID lookup.
- `zhttp/example/zhttp.cc:723` - Replace `run<Client>()` single request with
  request attempt and transport scheduler functions.
- `zhttp/example/zhttp.cc:786` - Make `mxParams()` concurrency-aware and add
  non-isolated app worker threads.
- `zhttp/example/zhttp.cc:838` - Move H3 direct, H1 Alt-Svc, and DNS fallback
  into per-request attempt logic.
- `zhttp/example/zhttp.cc:884` - Change `main()` to initialize one `Run`, start
  the aggregate scheduler, wait for aggregate completion, and compute final
  exit code.
- `zhttp/src/ZhttpH1.hh:222` - Potential parser change for close-delimited
  response body handling if conservative client-side non-reuse is insufficient.
- `zhttp/src/ZhttpH1Session.hh:50` - Existing server helper shows parser reset
  after complete messages; mirror this pattern in the H1 client worker.
- `zhttp/src/ZhttpH3Session.hh:18` - Reuse `Zhttp::H3::Cxn` for one set of H3
  connection streams per QUIC link.
- `zquic/src/Zquic.hh:1686` - Use existing `findStream()`/stream callbacks for
  stream-ID dispatch rather than adding a separate transport abstraction.
- `zhttp/test/Makefile.am:20` - Add the new multi-request test binary.
- `zhttp/test/ZhttpAppTest.cc:20` - Reuse script-driven local app testing
  style for client/server integration.
- `zhttp/test/Zhttp3InteropTest.cc:680` - Reuse H3 client/server stream patterns
  and `txInvoke()` ownership style for multi-stream assertions.

## Detailed Test Plan

- Build-level:
  - `make -j` after configure.
  - `make -C zhttp/test ZhttpMultiRequestTest Zhttp3InteropTest ZhttpAppTest`
    if narrowing the build during iteration.
- Unit/integration tests:
  - `./zhttp/test/ZhttpMultiRequestTest`
  - `./zhttp/test/ZhttpAppTest`
  - `./zhttp/test/Zhttp3InteropTest`
  - Existing parser tests: `./zhttp/test/ZhttpParserTest`,
    `./zhttp/test/ZhttpFallbackTest`, `./zhttp/test/ZhttpQPackTest`,
    `./zhttp/test/ZhttpQPackDynamicTest`
- Manual smoke tests:
  - `./zhttp/example/zhttp -n 3 http://127.0.0.1:<port>/file`
  - `./zhttp/example/zhttp -n 6 -j 2 -o /tmp/body http://127.0.0.1:<port>/file`
  - `./zhttp/example/zhttp --http3 -c cert.pem -n 4 -j 2 -o /tmp/h3 https://localhost:<port>/file`
- Diagnostics checks:
  - verbose logs contain request IDs in multi-request mode;
  - connection-level logs include worker/link/transport context;
  - stream-level H3 logs include request ID and stream ID.

## Acceptance Criteria

- `zhttp URL` behaves as before and writes `index.html`.
- `zhttp -o PATH URL` behaves as before and writes `PATH`.
- `zhttp -n 3 URL` writes `index.html.0`, `index.html.1`, and `index.html.2`.
- `zhttp -n 3 -o body URL` writes `body.0`, `body.1`, and `body.2`.
- Invalid `-n`/`-j` combinations fail before network initialization with a
  usage error.
- At most `M` logical requests are in flight.
- H1 with `-n > 1 -j 1` reuses a keep-alive connection when the server permits.
- H1 with `-j M` opens no more than `M` active worker links.
- H3 with `-j M --http3` uses concurrent request streams on one QUIC connection
  when stream credit allows.
- Redirects and fallback preserve request IDs and output paths.
- Failed requests produce a non-zero final exit without cancelling unrelated
  queued or active requests except for fatal setup errors.

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
- No requirement for explicit `-o` in multi-request mode.

## Options and Open Questions

- H3 link ownership:
  - Preferred: all stream open/send/link mutation runs on the QUIC link’s owning
    scheduler context; app workers only enqueue requests.
  - Alternative: guard link mutation with locks. This is less aligned with the
    existing event-driven transport style and risks subtle ordering issues.
  - ANSWER: preferred option
- H3 active stream lookup:
  - Preferred: `ZmHash` keyed by stream ID for receive-path dispatch.
  - Alternative: store stream ID in each `Req` and linearly scan active
    requests. This is simpler but less attractive if `M` grows.
  - ANSWER: preferred option, but isn't this built-in to `Zhttp`/`Zquic` already?
- H1 close-delimited bodies:
  - Preferred: extend the H1 response parser to support EOF-complete body
    framing for responses without content-length/chunked framing.
  - Conservative fallback: mark such connections non-reusable. This is
    insufficient if the body must be written and the parser cannot consume it,
    so parser support may be required for full correctness.
  - ANSWER: `Zhttp` has since been improved to support close-delimited bodies 
- Alt-Svc/DNS discovery cache:
  - Start with per-request probing for correctness.
  - Add origin-scoped in-run caching only after request independence and output
    publication are stable.
  - ANSWER: add origin-scoped caching using a hash table
- Output publication:
  - Truncate final path before the winning terminal attempt is simpler.
  - Staging path plus atomic rename is safer for failed attempts but adds
    cleanup complexity. Use staging if truncation cannot prevent ambiguity in
    H3 fallback paths.
  - ANSWER: Truncate final path before the winning terminal attempt
