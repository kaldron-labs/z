## Summary
The goal is to improve the `zhttp` command-line client so a single invocation can submit multiple GET requests to the same URL, optionally run those requests concurrently, and preserve efficient transport behavior. The new user-facing controls are `-n N` for the number of logical requests and `-j M` for the request concurrency/worker count.

The current `zhttp` example client is single-request oriented: `Options` has one output path, `State` tracks one response, each `run<Client>()` creates one `CliLink`, response completion posts one semaphore, H1/H3 completion tears down or finalizes the connection, and `mxParams()` creates four isolated scheduler threads. The product requirements below therefore require a shift from "one client state, one link, one body file" to "one run state, N request states, a bounded set of links/streams, and aggregate completion". Comparable clients such as curl reuse connections within one invocation, require distinct output destinations for multiple transfers, and exploit multiplexed streams for parallel HTTP/2/HTTP/3 transfers. HTTP/3 over QUIC also natively provides stream multiplexing and flow control on one connection.

Research sources used:
- curl man page: connection reuse and multiple-output behavior, https://curl.se/docs/manpage.html
- libcurl/everything curl: connection pool/reuse and parallel/multiplexed transfer behavior, https://everything.curl.dev/transfers/conn/reuse.html and https://everything.curl.dev/http/versions/http2.html
- HTTP/3 RFC 9114: QUIC connection, ALPN, Alt-Svc, stream multiplexing, and fallback guidance, https://datatracker.ietf.org/doc/html/rfc9114
- HTTP/3 explained: QUIC stream concurrency and per-stream delivery, https://http3-explained.haxx.se/en/quic/quic-streams

## Product Requirements

### Multi-request CLI
- Add `-n N` to `zhttp`, with a default of `1`. `N` is the number of logical requests submitted by this invocation, numbered from `0` through `N - 1`.
- Add `-j M` to `zhttp`, with a default of `1`. `M` is the maximum number of concurrently active request workers/in-flight logical requests.
- `N` and `M` must be positive decimal integers and must reject zero, overflow, non-numeric text, and negative values with a usage error.
- `-j` requires `-n` with `N > 1`. Supplying `-j` while `N == 1`, or supplying `-j` without an explicit multi-request `-n`, is a usage error.
- `M` must not exceed `N`; either reject `M > N` as a usage error or clamp it to `N`. The preferred requirement is to reject it, because it catches command-line mistakes and avoids creating idle links/threads.
- The help text must document `-n`, `-j`, their defaults, and their validation rules.
- This requirement connects to the output, scheduler, and link-pool requirements because `N` determines result cardinality while `M` determines concurrency and runtime resource sizing.

### Output path semantics
- Preserve the current single-request behavior for `N == 1`: `-o, --output PATH` writes the response body to `PATH`, and the existing default output behavior remains unchanged unless the implementation chooses to make omitted output explicit for validation.
- For `N > 1`, the user must explicitly provide `-o, --output PATH`. The existing default output path must not silently satisfy this rule; `zhttp` must distinguish "user supplied output" from "default output".
- For request `i`, the final response body must be written to `PATH.i`, where `i` is the logical request number in `[0, N)`.
- Request numbering must be stable from scheduling through redirects, protocol fallback, logging, completion, and file naming. Retries, redirects, Alt-Svc probes, and H3 fallback attempts for request `i` must continue to target `PATH.i`.
- Redirect response bodies must not be written. Only the terminal non-redirect response for the logical request may write to the output path.
- Failed attempts must not leave a successful request body ambiguous. If a fallback/probe attempt can partially write before failing, it must either write to a staging path and atomically publish on success, or truncate/restart `PATH.i` before the winning attempt writes.
- Body writes for different request IDs must be independent; concurrent requests must not share a `ZiFile` or mutable body counters.
- This requirement connects to per-request state and fallback handling because the current `State::bodyFile`, `bodyBytes`, `bodyChunks`, and `bodyFileOpen` are single-response fields.

### Request state model
- Replace the single response state model with an aggregate run state plus one request state per logical request.
- Each request state must track at least: request ID, current URL, current redirect count, protocol selected for the current attempt, response status, location, Alt-Svc endpoint, stream ID if H3, output path, output file, body counters, completion flag, failure flag, and timeout/deadline state.
- Aggregate run state must track: `Options`, original URL, total request count, requested concurrency, number scheduled, number active, number complete, number failed, and a completion primitive that can release `main()` when all logical requests have finished or a fatal setup error occurs.
- `RequestOps`, request builders, response parsers, and response sinks must operate on the request state associated with the current stream/link, not on a global `client.state`.
- Logs emitted from response parsing, connection handling, fallback, and redirects must include the request ID when `N > 1`, so verbose output remains useful under concurrency.
- This requirement connects to link reuse and H3 stream dispatch because parsers must route each incoming response to the correct request state.

### Request scheduling and completion
- For `N > 1`, `zhttp` must submit exactly `N` logical requests unless a fatal setup error prevents the run from starting.
- At most `M` logical requests may be in flight at one time. A logical request is in flight from the moment its transport attempt begins until its terminal success/failure, including redirects and fallback attempts.
- The scheduler must start additional queued requests as active requests complete, preserving the stable request IDs and output paths.
- The process exit code must be `0` only if all `N` logical requests complete successfully. Any failed logical request must make the final exit code non-zero.
- A failure in one logical request must not cancel unrelated queued or in-flight requests unless the failure is a fatal process-level error such as invalid CLI, scheduler startup failure, or inability to initialize the required transport engine.
- Timeout behavior must be defined per logical request attempt. The existing `ClientTimeout` can remain the default, but one timed-out request must be marked failed and must release its worker slot.
- This requirement connects to output integrity, link lifecycle, and tests because the program now finishes on aggregate completion rather than one semaphore post.

### H1/TCP and H1/TLS connection reuse
- For HTTP/1.1 over TCP and TLS, `zhttp` must attempt to reuse established H1 connections within the same invocation for multiple logical requests to the same origin.
- H1 reuse should be sequential per connection: send a new request on an idle reusable connection only after the prior response on that connection has completed. H1 pipelining is not required.
- `-j M` should create up to `M` active H1 `CliLink` instances for the run. Each link represents a worker connection that processes a sequence of request states from the aggregate queue.
- If the peer closes an H1 connection after a response, the associated worker may open a replacement connection for remaining queued requests. If the close occurs mid-response, the current request fails unless a retry policy is explicitly added later.
- H1 keep-alive eligibility must respect parser framing: content-length, chunked termination, connection close, and response parser errors must not cause a body from one request to bleed into the next response.
- The client should not open more H1 connections than necessary. For `N > 1, M == 1`, the expected behavior is one connection reused serially when the server permits it.
- This requirement connects to the link-pool and parser requirements because the current implementation disconnects/finalizes after one response.

### H3/QUIC concurrent streams
- For HTTP/3, `zhttp` must leverage QUIC streams for concurrent requests when `N > 1`, rather than treating every logical request as a fully separate one-request QUIC connection.
- The H3 connection must open the required control, encoder, and decoder streams once per QUIC connection, then open one client-initiated bidirectional request stream per active logical request.
- Each H3 request stream must have its own response parser and request state. Incoming stream data must dispatch by stream ID to the matching request state.
- The number of active request streams must be bounded by `M`, by locally configured stream limits, and by peer-advertised stream credit. If stream credit is exhausted, the request should remain queued until credit becomes available or the connection fails.
- H3 must keep the QUIC connection open until all active request streams have completed or failed. Completing one response must not disconnect the whole QUIC link while other streams are active.
- If a stream fails independently, mark only that logical request failed when possible. If the QUIC connection fails, mark all active streams on that connection failed and continue queued requests only if the policy opens a replacement connection.
- This requirement connects to the output, scheduler, and open-question sections because the goal also says `-j` increases the number of `CliLink`s; the preferred behavior is to treat `M` as H3 stream concurrency while still sizing app worker capacity.

### `CliLink` pool and worker model
- `-j M` must increase the available client worker/link capacity from one to `M`.
- For H1 transports, this maps directly to `M` active `CliLink`s, each with at most one active request at a time.
- For H3, the product behavior should prefer multiplexed streams on a QUIC link. If the implementation still instantiates multiple link slots for architectural consistency, it must avoid unnecessary QUIC handshakes when one link has stream capacity; additional QUIC links should be used only for connection failure recovery, stream-credit exhaustion, or an explicitly chosen connection-pool policy.
- Link objects must be associated with their current request state or stream state without using global singletons. A link that completes a request and remains reusable must return to the worker pool and pick the next queued request.
- Link finalization must occur after all request states owned by that link are complete and all pending writes have been flushed/closed.
- This requirement connects to scheduler sizing because `M` link workers must have non-isolated scheduler capacity for application callbacks without stealing dedicated I/O/protocol threads.

### Scheduler and thread topology
- `-j M` must increase the `ZiMultiplex`/`ZmScheduler` thread pool to provide `M` non-isolated app workload threads in addition to dedicated isolated transport threads.
- I/O Rx and I/O Tx threads must remain isolated/dedicated.
- Ztls Rx/Tx, Zquic Rx/Tx, and Ztcp Rx/Tx work must remain on isolated/dedicated protocol threads, not on the app worker threads. If a run can initialize both TLS and QUIC engines concurrently, their dedicated Rx/Tx thread IDs must not collide unless the implementation proves they are never active at the same time.
- Non-isolated app threads should be named and sized predictably, for example `app1..appM`, while isolated transport threads should keep clear names such as `ioRx`, `ioTx`, `tlsRx`, `tlsTx`, `quicRx`, `quicTx`, or a documented shared `netRx`/`netTx` pair.
- The default `N == 1, M == 1` topology must remain lightweight and should not regress the existing single-request behavior.
- Scheduler creation must fail cleanly with a logged error if the computed thread layout is invalid.
- This requirement connects to link-pool behavior because worker/link concurrency must not run request application work on dedicated Rx/Tx paths.

### Redirects and protocol fallback under multiple requests
- Each logical request must follow redirects independently up to `MaxRedirects`, preserving its request ID and output path.
- A redirect for one request must not mutate the URL or protocol choice of another request.
- Existing protocol policy should remain: `http:` uses H1/TCP; `https:` with `--http3` uses direct H3/QUIC; otherwise DNS-advertised H3 is tried first, then H1/TLS with Alt-Svc probing/fallback.
- Alt-Svc and DNS H3 discovery results may be cached within the invocation for the same origin to avoid repeated probing, but the cache must not break correctness when requests receive different redirects or Alt-Svc values.
- If the first H1 response is used only to discover Alt-Svc and an H3 retry is attempted, output publication must ensure that the final `PATH.i` reflects the winning terminal response.
- This requirement connects to output semantics and H3 connection reuse because fallback/probe behavior can otherwise duplicate network work and corrupt output files.

### Resource limits and backpressure
- `N` may be large; the implementation must avoid per-request heap churn in hot paths and should use existing Z containers (`ZtArray`, `ZtLocalArray`, `ZmRef`, `ZmHash` if lookup by stream ID is required) rather than STL containers.
- The request queue must not allocate or open all output files at startup. Open each response body file lazily when the first non-redirect body bytes arrive, as the current single-request client does.
- H3 stream maps and H1 link queues must be bounded by `N` and `M`; there must be no fixed small hard-coded request array that fails for normal `N` values.
- Flow-control limits such as `H3DataMax`, `H3StreamDataMax`, and `H3BidiMax` must be revisited for `M > 1`; defaults should support at least `M` concurrent H3 request streams or fail/queue clearly when stream credit is insufficient.
- This requirement connects to scheduler and H3 behavior because app concurrency must not translate into unbounded connections, streams, files, or buffers.

### Observability and diagnostics
- Existing `-v, --verbose` behavior must remain, with additional request IDs and worker/link IDs for multi-request runs.
- Logs should distinguish connection-level events from request-level events. For example, a QUIC connection log should not appear to belong to a single request when multiple streams are active.
- On usage errors, `zhttp` must print concise validation failures before usage text when feasible.
- On runtime failures, `zhttp` should log enough context to identify request ID, URL, output path, transport, and whether the failure happened during connect, request send, response parse, body write, redirect parsing, timeout, or protocol fallback.
- This requirement connects to tests and operations because parallel output otherwise interleaves and hides the failing request.

### Test and acceptance coverage
- Add CLI validation coverage for default `-n/-j`, invalid integers, `-j` without multi-request `-n`, `M > N`, and `N > 1` without explicit `-o`.
- Add an H1 local-server test where `-n 3 -o body URL` produces `body.0`, `body.1`, and `body.2` with the expected content.
- Add an H1 reuse test for `-n > 1 -j 1` that verifies the server observes a reused connection when keep-alive is available.
- Add an H1 concurrency test for `-n > 1 -j M` that verifies no more than `M` requests are active concurrently and all outputs are correct.
- Add an H3 test for `-n > 1 -j M --http3` that verifies multiple response streams complete on the same QUIC connection when stream credit allows.
- Add redirect coverage where multiple requests independently follow redirects and write only terminal bodies.
- Add failure coverage for one failed body write or parse failure among multiple requests, verifying non-zero exit while other request outputs remain valid.
- Existing single-request tests must continue to pass, including HTTP, HTTPS, and direct H3 app tests.

## Options and Open Questions
The main ambiguity is how literally to interpret "`-j` increases ... the number of `CliLink`s" for HTTP/3. The browser/curl-like behavior requested by the goal points toward one QUIC connection with concurrent request streams; a literal `M` QUIC links would work but would miss the main benefit of H3 multiplexing. Preferred requirement: `M` defines worker capacity, H1 maps that to `M` connection links, and H3 maps it primarily to `M` concurrent request streams on reusable QUIC connections.

The second ambiguity is thread dedication across fallback paths. If TLS and QUIC clients can be alive at the same time during DNS/Alt-Svc fallback, dedicated Ztls and Zquic Rx/Tx threads should be separate. If the implementation guarantees these engines are initialized and finalized sequentially, a shared isolated protocol Rx/Tx pair may be sufficient.

The existing `-o` default is `index.html`, but `N > 1` requires explicit `-o`. Implementation must either teach CLI parsing to track option presence or change the internal default model while preserving single-request user behavior.

Retries for mid-response H1 connection close, QUIC connection failure, or body-write failure are not required by the goal. The pragmatic first version should mark the affected request failed, keep unrelated requests running, and leave automatic retry policy as a future option.

No new HTTP methods, multiple distinct URLs, request bodies, H1 pipelining, response aggregation, progress meters, HTTP/2, cookie/cache semantics, persistent Alt-Svc cache, or cross-process connection reuse are required for this goal.
