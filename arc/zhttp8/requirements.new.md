## Summary

Improve the `zhttp` command-line client so one invocation can submit multiple
GET requests to the same URL, optionally run those requests concurrently, and
preserve efficient transport behavior. The user-facing controls are:

- `-n N`: submit `N` logical requests, default `1`.
- `-j M`: allow up to `M` concurrent logical requests, default `1`.

Logical requests are numbered from `0` through `N - 1`. For `N > 1`, response
bodies are written to per-request files by appending `.<request-id>` to the
base output path. Because `zhttp` already defaults `-o, --output` to
`index.html` and never writes response bodies to stdout, multi-request mode may
use the default output base. For example, `zhttp -n 3 URL` writes
`index.html.0`, `index.html.1`, and `index.html.2`.

The current `zhttp` example client is single-request oriented:
`zhttp/example/zhttp.cc` has one output path in `Options`, one `State`, one
response parser, one `CliLink`, one response body file, and one semaphore post
for completion. H1/H3 completion currently tears down or finalizes the single
connection, and `mxParams()` creates four isolated scheduler threads. This
work therefore changes the product model from "one client state, one link, one
body file" to "one run state, N request states, bounded active work, and
aggregate completion".

Comparable clients such as curl reuse connections within one invocation,
require distinct output destinations for concurrent/multiple transfers, and
use multiplexed streams for parallel HTTP/2/HTTP/3 transfers. HTTP/3 over QUIC
natively provides stream multiplexing and per-stream flow control on one
connection.

Research and codebase context used:

- `goal.md` and `requirements.feedback.md`.
- Current client implementation in `zhttp/example/zhttp.cc`.
- Related benchmark plan in `http_bench.md`, especially the native
  multi-request mode phase.
- curl man page: connection reuse and multiple-output behavior,
  https://curl.se/docs/manpage.html
- everything curl: connection pool/reuse and parallel/multiplexed transfer
  behavior, https://everything.curl.dev/transfers/conn/reuse.html and
  https://everything.curl.dev/http/versions/http2.html
- HTTP/3 RFC 9114: QUIC connection, ALPN, Alt-Svc, stream multiplexing, and
  fallback guidance, https://datatracker.ietf.org/doc/html/rfc9114
- HTTP/3 explained: QUIC stream concurrency and per-stream delivery,
  https://http3-explained.haxx.se/en/quic/quic-streams

## Product Requirements

### Multi-request CLI

- Add `-n N` to `zhttp`, with a default of `1`. `N` is the number of logical
  requests submitted by this invocation, numbered from `0` through `N - 1`.
- Add `-j M` to `zhttp`, with a default of `1`. `M` is the maximum number of
  concurrently active logical requests.
- `N` and `M` must be positive decimal integers. Reject zero, overflow,
  non-numeric text, empty values, and negative values with a usage error.
- `-j` requires an explicit `-n` with `N > 1`. Supplying `-j` while `N == 1`,
  or supplying `-j` without an explicit multi-request `-n`, is a usage error.
- Reject `M > N` as a usage error. This catches command-line mistakes and
  avoids creating idle links, streams, or app worker capacity.
- The help text must document `-n`, `-j`, their defaults, validation rules, and
  multi-request output file naming.
- `N` determines result cardinality. `M` determines bounded request
  concurrency, H1 connection worker count, H3 request stream concurrency, and
  non-isolated application scheduler capacity.

### Output path semantics

- Preserve current single-request behavior for `N == 1`: `-o, --output PATH`
  writes the response body to `PATH`, and the default output path remains
  `index.html`.
- For `N > 1`, do not require the user to provide `-o`. Use the effective base
  output path, explicit or default, and append `.<request-id>` for each logical
  request.
- For request `i`, the terminal response body must be written to `PATH.i`,
  where `PATH` is the effective output base and `i` is in `[0, N)`.
- Request numbering must be stable from scheduling through redirects, protocol
  fallback, logging, completion, and file naming. Retries, redirects, Alt-Svc
  probes, and H3 fallback attempts for request `i` must continue to target
  `PATH.i`.
- Redirect response bodies must not be written. Only the terminal non-redirect
  response for the logical request may write to the output path.
- Failed attempts must not leave a successful request body ambiguous. If a
  fallback or probe attempt can partially write before failing, it must either
  write to a staging path and atomically publish on success, or truncate/restart
  `PATH.i` before the winning attempt writes.
- Body writes for different request IDs must be independent; concurrent
  requests must not share a `ZiFile`, mutable body counters, parser state, or
  output-open flag.
- Output files should be opened lazily on the first non-redirect body bytes, as
  the current single-request client does.

### Request state model

- Replace the single response state model with one aggregate run state plus one
  request state per logical request.
- Each request state must track at least: request ID, current URL, redirect
  count, selected protocol for the current attempt, response status, redirect
  location, Alt-Svc endpoint, H3 stream ID when applicable, output path, output
  file, body counters, parser/framing state, completion flag, failure flag, and
  timeout/deadline state.
- Aggregate run state must track: `Options`, original URL, total request count,
  requested concurrency, number scheduled, number active, number complete,
  number failed, transport discovery/fallback cache where used, and a
  completion primitive that releases `main()` when all logical requests finish
  or a fatal setup error occurs.
- `RequestOps`, request builders, response parsers, and response sinks must
  operate on the request state associated with the current stream/link. They
  must not read or mutate a global single-response `client.state` except
  through the aggregate/request state model.
- Response parser instances must be per active H1 link/request or per active
  H3 request stream. One parser must not process interleaved responses for
  multiple logical requests.
- Logs emitted from response parsing, connection handling, fallback, redirects,
  output, and completion must include the request ID when `N > 1`.

### Request scheduling and completion

- For `N > 1`, `zhttp` must submit exactly `N` logical requests unless a fatal
  setup error prevents the run from starting.
- At most `M` logical requests may be in flight at one time. A logical request
  is in flight from the moment its first transport attempt begins until its
  terminal success or failure, including redirects and fallback attempts.
- The scheduler must start additional queued requests as active requests
  complete, preserving stable request IDs and output paths.
- The process exit code must be `0` only if all `N` logical requests complete
  successfully. Any failed logical request must make the final exit code
  non-zero.
- A failure in one logical request must not cancel unrelated queued or
  in-flight requests unless the failure is a fatal process-level error such as
  invalid CLI, scheduler startup failure, or inability to initialize the
  required transport engine.
- Timeout behavior is per logical request attempt. The existing
  `ClientTimeout` may remain the default; a timed-out request is marked failed
  and releases its worker slot.
- Aggregate completion must wait for all active writes, parser completions,
  stream closes, and reusable link finalization needed to make output files
  durable and link ownership clear.

### H1/TCP and H1/TLS connection reuse

- For HTTP/1.1 over TCP and TLS, `zhttp` must attempt to reuse established H1
  connections within the same invocation for multiple logical requests to the
  same origin.
- H1 reuse is sequential per connection: send a new request on an idle reusable
  connection only after the prior response on that connection has completed.
  H1 pipelining is not required.
- `-j M` creates up to `M` active H1 worker links. Each link owns one
  connection at a time and processes a sequence of request states from the
  aggregate queue.
- For `N > 1, M == 1`, expected behavior is one H1 connection reused serially
  when the server permits keep-alive.
- If the peer closes an H1 connection after a response, the associated worker
  may open a replacement connection for remaining queued requests. If the close
  occurs mid-response, the current request fails unless a retry policy is added
  later.
- H1 keep-alive eligibility must respect parser framing: content-length,
  chunked termination, connection close, and parser errors must not cause a
  body or response state from one request to bleed into the next response.
- The client should not open more H1 connections than needed to satisfy `M` and
  current queue pressure.

### H3/QUIC concurrent streams

- For HTTP/3, `zhttp` must prefer QUIC stream multiplexing over opening one
  QUIC connection per logical request.
- `-j M` specifies H3 request concurrency. It does not require `M` `CliLink`
  instances for H3.
- The preferred H3 model is one reusable QUIC `CliLink` per origin with up to
  `M` concurrent client-initiated bidirectional request streams, subject to
  local and peer stream limits.
- If the implementation uses one `CliLink` with multiple request streams, it
  must make link access from multiple worker callbacks safe. Acceptable designs
  include serializing all link mutation on the link's scheduler context,
  routing stream work through the owning link thread, or otherwise documenting
  and enforcing the ownership rule used by existing `Zquic` APIs.
- The H3 connection must open the required control, encoder, and decoder
  streams once per QUIC connection, then open one client-initiated bidirectional
  request stream per active logical request.
- Each H3 request stream must have its own request state and response parser.
  Incoming stream data must dispatch by stream ID to the matching request
  state.
- The number of active request streams must be bounded by `M`, by locally
  configured stream limits, and by peer-advertised stream credit. If stream
  credit is exhausted, the request remains queued until credit becomes
  available or the connection fails.
- H3 must keep the QUIC connection open until all active request streams have
  completed or failed. Completing one response must not disconnect the whole
  QUIC link while other streams are active.
- If a stream fails independently, mark only that logical request failed when
  possible. If the QUIC connection fails, mark all active streams on that
  connection failed and continue queued requests only if the policy opens a
  replacement connection.
- Additional QUIC links should be used only for connection failure recovery,
  persistent stream-credit exhaustion where another connection is a deliberate
  policy choice, or a later explicitly configured connection-pool mode.

### `CliLink` pool and worker model

- `-j M` increases available request concurrency to `M`; it does not mandate an
  identical link-count mapping for every transport.
- For H1 transports, request concurrency maps directly to up to `M` active
  `CliLink`s, each with at most one active request at a time.
- For H3, request concurrency maps primarily to concurrent streams on reusable
  QUIC links. If multiple H3 link slots are instantiated for architectural
  consistency, they must not cause unnecessary extra QUIC handshakes when one
  link has stream capacity.
- Link objects must be associated with their current request state or stream
  state without using global singletons. A reusable H1 link that completes a
  request must return to the worker pool and pick the next queued request.
- Link finalization must occur after all request states owned by that link are
  complete and all pending writes have been flushed/closed.
- The design must distinguish connection-level failures from request-level
  failures so unrelated active or queued work can continue where the transport
  allows it.

### Scheduler and thread topology

- `-j M` must increase the `ZiMultiplex`/`ZmScheduler` topology to provide `M`
  non-isolated application workload threads in addition to dedicated isolated
  transport threads.
- I/O Rx and I/O Tx threads must remain isolated/dedicated.
- Ztls Rx/Tx, Zquic Rx/Tx, and Ztcp Rx/Tx work must remain on
  isolated/dedicated protocol threads, not on app worker threads.
- If a run can initialize TLS and QUIC engines concurrently, their dedicated
  Rx/Tx thread IDs must not collide. If the implementation guarantees those
  engines are initialized and finalized sequentially for a given run, a shared
  isolated protocol Rx/Tx pair is acceptable only if documented in the code.
- Non-isolated app threads should be named and sized predictably, for example
  `app1..appM`. Isolated transport threads should keep clear names such as
  `ioRx`, `ioTx`, `tlsRx`, `tlsTx`, `quicRx`, `quicTx`, or a documented shared
  `netRx`/`netTx` pair.
- The default `N == 1, M == 1` topology must remain lightweight and must not
  regress existing single-request behavior.
- Scheduler creation must fail cleanly with a logged error if the computed
  thread layout is invalid.

### Redirects and protocol fallback under multiple requests

- Each logical request follows redirects independently up to `MaxRedirects`,
  preserving request ID and output path.
- A redirect for one request must not mutate the URL, protocol choice, parser
  state, output state, or redirect count of another request.
- Existing protocol policy should remain:
  - `http:` uses H1/TCP.
  - `https:` with `--http3` uses direct H3/QUIC.
  - `https:` without `--http3` tries DNS-advertised H3 first, then H1/TLS with
    Alt-Svc probing/fallback.
- Alt-Svc and DNS H3 discovery results may be cached within the invocation for
  the same origin to avoid repeated probing, but the cache must not break
  correctness when requests receive different redirects or Alt-Svc values.
- If an H1 response is used only to discover Alt-Svc and an H3 retry is
  attempted, output publication must ensure that final `PATH.i` reflects the
  winning terminal response.
- Automatic retries for mid-response H1 connection close, QUIC connection
  failure, or body-write failure are not required. The first version should
  mark the affected request failed, keep unrelated requests running, and leave
  retry policy as future work.

### Resource limits and backpressure

- `N` may be large; implementation must avoid hot-path heap churn and should
  use existing Z containers (`ZtArray`, `ZtLocalArray`, `ZmRef`, `ZmHash` if
  lookup by stream ID is required) rather than STL containers.
- The request queue must not open all output files at startup.
- H3 stream maps and H1 link queues must be bounded by `N` and `M`; there must
  be no fixed small hard-coded request array that fails for normal `N` values.
- Flow-control limits such as `H3DataMax`, `H3StreamDataMax`, and `H3BidiMax`
  must be revisited for `M > 1`; defaults should support at least `M`
  concurrent H3 request streams or fail/queue clearly when stream credit is
  insufficient.
- App concurrency must not translate into unbounded connections, streams,
  output files, buffers, parser objects, or scheduler work items.

### Observability and diagnostics

- Existing `-v, --verbose` behavior must remain.
- Multi-request logs must include request IDs. Connection-level logs should
  include worker/link IDs, transport, origin, and whether the event applies to
  one request or the connection as a whole.
- Logs should distinguish H3 connection-level events from stream/request-level
  events, because many request streams can share one QUIC link.
- On usage errors, `zhttp` must print concise validation failures before usage
  text when feasible.
- On runtime failures, `zhttp` should log enough context to identify request
  ID, URL, output path, transport, and whether the failure happened during
  connect, request send, response parse, body write, redirect parsing, timeout,
  or protocol fallback.
- Concurrent verbose output may interleave, but each line should contain enough
  identity fields to be useful without relying on adjacent lines.

### Test and acceptance coverage

- Add CLI validation coverage for default `-n/-j`, invalid integers, `-j`
  without explicit multi-request `-n`, `M > N`, and accepted `N > 1` with the
  default output base.
- Add an H1 local-server test where `-n 3 -o body URL` produces `body.0`,
  `body.1`, and `body.2` with expected content.
- Add an H1 local-server test where `-n 3 URL` produces `index.html.0`,
  `index.html.1`, and `index.html.2` in the working directory or configured
  test output directory.
- Add an H1 reuse test for `-n > 1 -j 1` that verifies the server observes a
  reused connection when keep-alive is available.
- Add an H1 concurrency test for `-n > 1 -j M` that verifies no more than `M`
  requests are active concurrently and all outputs are correct.
- Add an H3 test for `-n > 1 -j M --http3` that verifies multiple response
  streams complete on the same QUIC connection when stream credit allows.
- Add redirect coverage where multiple requests independently follow redirects
  and write only terminal bodies.
- Add failure coverage for one failed body write, parse failure, timeout, or
  mid-response close among multiple requests, verifying non-zero exit while
  unrelated request outputs remain valid where expected.
- Existing single-request behavior must continue to pass, including HTTP,
  HTTPS, direct H3, DNS/Alt-Svc fallback, `-o PATH`, default `index.html`, and
  verbose logging.

## Explicit Non-Goals

- Do not add new HTTP methods, request bodies, multiple distinct URL arguments,
  response aggregation, progress meters, HTTP/2, cookie/cache semantics,
  persistent Alt-Svc cache, cross-process connection reuse, or H1 pipelining.
- Do not implement automatic retry policy for mid-response H1 close, QUIC
  connection failure, or body-write failure as part of this goal.
- Do not fake multi-request behavior by spawning one `zhttp` process per
  request.
- Do not require explicit `-o` for `N > 1`; the default `index.html` base is a
  valid multi-request output base.
