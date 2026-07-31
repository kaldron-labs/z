## Summary

The goal is to turn `zhttp/example/zhttpserver.cc` from a fixed-body HTTP/1
example into a static file server for the local filesystem. The completed
server should keep the existing `zhttp` transport goal, exercise server-side
HTTP/1 over TCP, HTTP/1 over TLS, and HTTP/3 over QUIC, and cover the practical
static-server behavior exposed by `darkhttpd`.

The required product behavior is:

- static files served from a document root;
- single-file mode;
- directory index files;
- generated directory listings;
- GET and HEAD;
- Range / partial content;
- If-Modified-Since / Last-Modified;
- keep-alive with idle timeout control;
- MIME type resolution and overrides;
- access logging;
- host-based and catch-all 301 forwarding;
- optional HTTP-to-HTTPS forwarding;
- hidden-dotfile policy;
- Basic auth;
- HTTP/1 over TCP;
- HTTP/1 over TLS;
- HTTP/3 over QUIC.

Research findings from comparable static servers:

- `darkhttpd` is the functional compatibility reference. Its documented scope is
  a single-binary static-only server with GET, HEAD, Range, If-Modified-Since,
  keep-alive, IPv6, directory listings, request logging, custom response
  headers, host-based 301 redirects, sendfile on several Unix platforms,
  chroot, privilege dropping, idle connection timeouts, overly-long request
  rejection, and no CGI. Latest public release shown by GitHub is 1.17,
  released July 24, 2025.
- Caddy's `file_server` documents a useful correctness model: build filesystem
  paths from the site root plus the URI path, enforce canonical URI redirects
  for directories without a trailing slash, and prevent path-component traversal.
  It explicitly notes that symlinks inside a root can still point outside it;
  this plan therefore does not claim symlink sandboxing unless a later phase
  adds an explicit `openat()`/`O_NOFOLLOW` policy.
- NGINX's static content documentation uses the same root-plus-URI model, serves
  index files for slash-terminated directory requests, can enable autoindex
  listings, and supports trying file/directory existence before returning 404.
- NGINX's `if_modified_since` behavior highlights an implementation choice:
  compare file mtime exactly or with "before" semantics. This plan chooses the
  practical HTTP behavior used by many static servers: return 304 when the file
  mtime is less than or equal to the parsed `If-Modified-Since` time after
  second-level normalization.

Local API findings that change the implementation shape:

- Percent encoding and decoding have already been factored into
  `zu/src/ZuPercent.hh`. Use `ZuPercent::Codec` for allocation-free span
  calculation, printing/encoding, truncation-safe fixed-buffer encoding, and
  in-place decoding with policy callbacks. `zhttpserver` should use
  `ZuPercent` directly with server-specific policies for request paths,
  queries, redirects, and generated listing hrefs. Do not depend on `ZtURI` in
  the static server; `ZtURI` is for `ZtStruct` structured type conversion
  to/from query strings.
- `Zhttp::H1ReqParser` and `Zhttp::H3ReqParser` only call typed selected-header
  callbacks. The server must declare every request header it needs in a
  `ZhttpHeaders(...)` typelist: `host`, `authorization`, `range`,
  `if-modified-since`, `connection`, `referer`, and `user-agent`.
- `Zhttp::H1::Parser::reset()` supports sequential HTTP/1 messages, so
  keep-alive can reuse a per-link parser after a complete request. It does not
  expose the request HTTP version; if HTTP/1.0 close behavior is required, add a
  parser callback for the version token rather than guessing from headers.
- `Zhttp::H1RespBuilder` and `Zhttp::H3RespBuilder` emit compile-time header
  typelists. Response header names remain application-declared at compile time;
  arbitrary runtime response header names and the darkhttpd-style `--header`
  option are non-goals for this plan.
- `Zhttp::H3::Cxn` owns local H3 control and QPACK streams. `zhttpserver.cc`
  must use it and must not introduce local `QPackEncoderTx` subclasses or
  duplicate peer stream uniqueness checks.
- H3 sends from the local code pattern must respect Tx-thread ownership:
  use `txInvoke()`, `txStream_()`, and `send_()` for stream writes when not
  already on the Tx thread.
- `ZiFile` is unbuffered and direct to the OS. Prefer `ZiMMapFile` for static
  file response bodies so the server can write mapped spans without copying
  whole files into heap buffers. Keep a bounded `ZiFile` read fallback for empty
  files, mapping failures, platform edge cases, and any transport path that
  cannot safely hold the mapping alive until Tx completion.
- `ZiMMapFile::mmap()` rejects `length <= 0`; empty files need an explicit
  zero-length response path. Its Unix implementation currently touches the last
  mapped byte, so the implementation phase must validate read-only serving
  semantics with focused tests and either adjust `ZiMMapFile` or fall back to
  buffered reads if the existing behavior is unsuitable.
- `ZiDir` only returns directory entry names and does not sort or stat entries.
  Directory listing needs an explicit sortable entry container.
- `ZuPercent::Codec` should provide the server's path, query, and listing href
  percent handling. The server must still add its own NUL rejection, `.`/`..`
  normalization, hidden-dotfile policy, slash canonicalization, and query
  split.
- `Ztls::ServerParams` and `Zquic::ServerParams` already support
  `.certPath(...).keyPath(...).alpn(...)`; no new TLS/QUIC parameter API is
  needed for the first server pass.
- `ZmScheduler::Timer` through `ZiMultiplex` provides the right primitive for
  per-link idle timeout scheduling and cancellation.
- `ZiDaemon` is available in the `zi` layer and should be the process helper
  for `--daemon` and `--pidfile`. Call it after CLI/path validation and before
  opening access-log files, starting `ZiMultiplex`, binding listeners, or
  creating protocol state, because daemonization/reinvoke closes the inherited
  runtime state.
- `zhttp/test/ZhttpTestUtil.hh` is test-only infrastructure for temporary
  directories, generated localhost certificates, curl HTTP/3 capability
  detection, curl HTTP/3 retries, loopback port allocation, file diagnostics,
  and polling retries. `zhttpserver` and `zhttpclient` must not include,
  link, or otherwise depend on code from `zhttp/test`; any runtime helper they
  need must live in production source, not in the test tree.

The plan is intentionally vertically sliced. Each phase should wire a visible
sub-feature end to end, with placeholders added only where a later phase needs
stable integration points.

## Architecture Documentation

### New or Changed Components

Add a small static-server support layer near the example:

- `zhttp/example/ZhttpStaticServer.hh`
  - `Options`, `State`, `RequestData`, `ResponsePlan`, `StaticPlanner`;
  - path decoding and filesystem lookup helpers;
  - MIME map and default MIME table;
  - date formatting/parsing helpers for HTTP-date;
  - range parsing;
  - directory listing rendering;
  - access log sink abstraction.
- `zhttp/example/zhttpserver.cc`
  - CLI and process setup;
  - HTTP/TCP, HTTPS/TLS, and H3/QUIC server/link/stream wiring;
  - response emission over H1 and H3 using the protocol-neutral
    `ResponsePlan`.

Production/test boundary:

- `zhttpserver` and `zhttpclient` must not include headers from `zhttp/test`,
  link objects from `zhttp/test`, or copy test-only helper implementations into
  production code.
- Test binaries may use `zhttp/test/ZhttpTestUtil.hh` to spawn the production
  binaries, allocate loopback ports, generate local certificates, and poll for
  readiness.
- If a helper is genuinely needed by both a production binary and a test, move
  or reimplement the minimal runtime piece in `zhttp/src`, `zu/src`, `zi/src`,
  or the owning production module before using it from the binary.

Use compact Z-style names and containers:

```c++
struct Options {
  ZtString<> root;
  ZiIP       addr;
  uint16_t   port = 8080;
  ZtString<> cert;
  ZtString<> key;
  ZtString<> index{"index.html"};
  ZtString<> mimetypes;
  ZtString<> defaultMimetype{"application/octet-stream"};
  ZtArray<Forward> forwards;
  ZtArray<Header> headers;
  ZtString<> forwardAll;
  ZtString<> authUser;
  ZtString<> authPass;
  ZtString<> logPath;
  ZtString<> pidfile;
  ZtString<> uid;
  ZtString<> gid;
  unsigned   maxconn = 0;
  unsigned   timeout = 30;
  bool       ipv6 = false;
  bool       daemon = false;
  bool       syslog = false;
  bool       noListing = false;
  bool       chroot = false;
  bool       noKeepalive = false;
  bool       singleFile = false;
  bool       hideDotfiles = false;
  bool       forwardHttps = false;
  bool       noServerID = false;
  bool       http = true;
  bool       https = false;
  bool       http3 = false;
};
```

```c++
struct State {
  Options              options;
  MimeMap              mime;
  LogSink              log;
  ZmSemaphore          done;
  ZmAtomic<unsigned>   active = 0;
  ZmAtomic<uint64_t>   requests = 0;
  ZmAtomic<uint64_t>   errors = 0;
};
```

### New or Changed Processes or Threads

- Keep the current `ZiMultiplex` scheduler model with isolated Rx and Tx
  threads.
- H1 TCP and H1 TLS receive paths run parser/planner work from their Rx-side
  `process()` callbacks.
- H1 response writes use the existing link `txStream()` path, or `txInvoke()`
  if later measurements show cross-thread write ownership requires explicit
  scheduling.
- H3 response writes must follow the known interop pattern: capture a stream
  ref, call `app()->txInvoke(...)`, build on `ref->txStream_()`, and complete
  with `link->send_(ref, "", true)` when the response is done.
- Idle timeout uses one `ZmScheduler::Timer` per H1 link. Schedule it through
  the owning multiplexer; cancel it on disconnect and update it on request
  progress. H3/QUIC idle behavior should first use QUIC transport parameters
  where available, then add app timers only if needed for parity.

### New or Changed Interfaces

Target CLI:

```text
Usage: zhttpserver /path/to/wwwroot [OPTION]...

Options:
  --port number              listen port, default 8080 or 80 if root
  --addr ip                  listen address, default all interfaces
  --ipv6                     listen on IPv6 address
  --daemon                   detach and run in background
  --pidfile filename         write PID to file
  --maxconn number           maximum concurrent accepted connections
  --log filename             append access log to file, "-" for stdout
  --syslog                   send access log to syslog
  --index filename           directory index file, default index.html
  --no-listing               disable generated directory listings
  --mimetypes filename       extension to MIME map
  --default-mimetype string  default MIME type, application/octet-stream
  --uid uid/uname            drop user privileges after initialization
  --gid gid/gname            drop group privileges after initialization
  --chroot                   chroot to wwwroot after initialization
  --no-keepalive             disable HTTP keep-alive
  --single-file              serve only the specified file
  --hide-dotfiles            reject dotfiles
  --forward host url         301 redirect by Host, repeatable
  --forward-all url          301 redirect all requests
  --forward-https            redirect HTTP requests to HTTPS
  --no-server-id             omit server identity headers/listings
  --timeout secs             idle connection timeout, 30 default, 0 disables
  --auth username:password   Basic authentication
  --http                     enable HTTP/1.1 over TCP, default
  --https                    enable HTTP/1.1 over TLS
  --http3                    enable HTTP/3 over QUIC
  --cert path                TLS certificate for --https/--http3
  --key path                 TLS private key for --https/--http3
  -h, --help                 show help
```

Compatibility and validation rules:

- `root` is required unless `--help` is present.
- Default port is 80 only when running as root on supported Unix platforms;
  otherwise 8080.
- Root must exist. It must be a directory unless `--single-file` is set.
- `--single-file` root must be a readable regular file.
- `--https` and `--http3` require both `--cert` and `--key`.
- `--pidfile` with `--chroot` must be validated before chroot and must resolve
  to a path that remains usable after chroot.
- `--forward-all` and `--forward` may coexist; catch-all redirect wins because
  it matches before host-specific filesystem work.
- Preserve `--port 0` by reporting the chosen port after listen.
- Keep `--body` only in a short migration phase for the existing
  `ZhttpClientServerTest`; remove it when the static-file integration test is
  updated.
- For unsupported platform-specific options on `_WIN32`, fail clearly rather
  than silently ignoring security/process options.

Request model:

```c++
using ReqHeaders = ZhttpHeaders(
  "host",
  "authorization",
  "range",
  "if-modified-since",
  "connection",
  "referer",
  "user-agent");

struct RequestData {
  Zhttp::Method::T method = -1;
  ZtString<>       target;
  ZtString<>       path;
  ZtString<>       query;
  ZtString<>       host;
  ZtString<>       authorization;
  ZtString<>       range;
  ZtString<>       ifModifiedSince;
  ZtString<>       connection;
  ZtString<>       referer;
  ZtString<>       userAgent;
  bool             h3 = false;
  bool             tls = false;
  bool             http10 = false;
};
```

Response model:

```c++
struct ResponsePlan {
  unsigned   status = 500;
  ZtString<> reason;
  ZtString<> contentType;
  ZtString<> location;
  ZtString<> wwwAuthenticate;
  ZtString<> lastModified;
  ZtString<> date;
  ZtString<> etag;
  ZtString<> contentRange;
  uint64_t   contentLength = 0;
  uint64_t   fileOffset = 0;
  uint64_t   fileLength = 0;
  bool       sendBody = false;
  bool       close = false;
  bool       file = false;
  bool       generated = false;
};
```

### New or Changed Data Flows

H1 TCP/TLS:

1. Link receives bytes.
2. `ReqParser` extracts method, target, and selected headers into
   `RequestData`.
3. `StaticPlanner::plan(req, transport)` returns a `ResponsePlan`.
4. H1 response emitter writes status, headers, and either generated body or a
   file body segment backed by `ZiMMapFile` when mapping succeeds.
5. Access log records the completed request.
6. Parser resets or link closes according to keep-alive policy.

H3:

1. `H3ServerLink::connected()` validates ALPN `h3` and calls
   `h3.openLocal(*this)`.
2. Unidirectional streams are parsed with `Zhttp::H3::CxnParser` and delegate
   peer stream state/QPACK callbacks to `link.h3`.
3. Bidirectional streams parse requests with `Zhttp::H3ReqParser`, using
   `link.h3.qpackRxTable` and `link.h3.qpackDecoderWrite`.
4. Planner returns the same `ResponsePlan` used by H1.
5. H3 response emitter runs on the Tx thread, sets builder `qpackTx`,
   `qpackEncoderWrite`, and `streamID`, emits HEADERS and DATA frames, and
   finishes the stream with FIN.

### New or Changed Event-Driven or Timer Processing

- Add per-H1-link idle timers in the keep-alive phase.
- Active connection counting happens in `accepted()`/`connected()` and
  `disconnected()` paths. If `--maxconn` is reached, reject or immediately close
  new links deterministically and log the event.
- Shutdown must flush and close access logs and remove active timers before
  destroying links/server objects.

### New or Changed Network Programming

- Plain HTTP uses `Ztcp::Server<HTTPServer>` and `Ztcp::SrvLink` as the current
  example does.
- HTTPS/H1 uses `Ztls::Server<TLSServer>` and `Ztls::SrvLink`, configured with
  ALPN `http/1.1`.
- HTTP/3 uses `Zquic::Server<H3Server, H3ServerLink>` with ALPN `h3`, configured
  with certificate/key and reasonable stream/data limits:

```c++
ZuCSpan alpn[] = { "h3" };
server.init(
  Zquic::ServerParams(&mx, "3", "4")
    .certPath(options.cert).keyPath(options.key).alpn(alpn)
    .maxData(1<<20).maxStreamData(256<<10)
    .maxStreamsBidi(64).maxStreamsUni(8));
```

Tune these defaults after functional tests; keep them explicit so HTTP/3 server
limits are not accidental.

### New or Changed Data Stores

- `MimeMap`: extension-to-MIME map. Prefer `ZmHash` with case-insensitive
  normalized keys; a small `ZtArray` is acceptable only if the built-in table is
  tiny and measured cold.
- `DirEntries`: sortable directory entries for generated listings. Use
  `ZtArray<DirEntry, ZtArrayHeapID<"ZhttpStatic.Dir">>` and `ZuSort` or the
  local sorting idiom.
- `LogSink`: stdout/file/syslog abstraction. Serialize writes with `ZmLock` or
  route logging to one thread; avoid interleaved lines.

## Detailed Design and Implementation Plan

### Phase 1: CLI, Validation, and Static Planner Skeleton

- Replace no-root CLI semantics with `zhttpserver /path/to/wwwroot [flags]`.
- Keep the current manual parser initially. `ZtCLI` is schema-oriented and not
  obviously a good fit for repeated ordered `--forward` values;
  reconsider only if a local repeated-option pattern exists.
- Add `Options`, `State`, `RequestData`, and `ResponsePlan`.
- Add `ZhttpStaticServer.hh` and wire a minimal `StaticPlanner` that can return
  either the old fixed `--body` response or a placeholder 404 for filesystem
  paths.
- Validate root, transport cert/key combinations, port range, auth format,
  repeated forward syntax, and platform-unsupported options.
- Preserve current `ZhttpClientServerTest` with `--body` during this phase.

Tests:

- `zhttpserver --help` includes all target options.
- Invalid root fails.
- `--single-file` rejects directories and accepts a file.
- Missing cert/key for `--https` or `--http3` fails.
- Bad `--auth` fails.
- `--port 0` starts and prints the selected port.
- Existing fixed-body HTTP/TCP test still passes.

Dependencies on preceding phases: none.

### Phase 2: H1 End-to-End Static File GET

- Implement the first real vertical slice over HTTP/TCP:
  - parse request method, target, Host, Connection;
  - split target into path and query;
  - decode and normalize path;
  - map it to root;
  - stat/open regular file;
  - emit 200 with `Content-Length`, `Content-Type`, `Date`, `Last-Modified`,
    `Accept-Ranges: bytes`, and `Server` unless disabled;
  - stream file body from a `ZiMMapFile` mapping when the file size is nonzero;
  - use an explicit no-body path for zero-length files;
  - keep a bounded `ZiFile` read fallback for mapping failure or platform
    exceptions.
- Add a compact built-in MIME table for common static types:
  `html`, `htm`, `txt`, `css`, `js`, `json`, `png`, `jpg`, `jpeg`, `gif`,
  `svg`, `ico`, `wasm`, `pdf`, `mp3`, `mp4`, `webp`.
- Unknown extensions use `options.defaultMimetype`.
- Use `ZiFile::mtime()` and existing path helpers for metadata. For response
  bodies, prefer `ZiMMapFile::mmap(path, ZiFile::ReadOnly | ZiFile::GC, size,
  false)` and write `ZuCSpan{static_cast<const char *>(file.addr()) + offset,
  length}` slices to the H1 body stream.
- Verify `ZiMMapFile` read-only behavior before relying on it for static files;
  if its current last-byte touch is unsafe with `ReadOnly`, either fix
  `ZiMMapFile` in the implementation or fall back to bounded `ZiFile::pread()`
  while documenting the reason.
- Do not use a stack-heavy fixed buffer and do not copy whole files into heap
  strings or arrays.

Tests:

- `zhttpserver ROOT --http --port PORT` serves an existing file.
- Body hash matches source for small and large files.
- Known MIME type is emitted.
- Unknown MIME falls back to `application/octet-stream`.
- `--default-mimetype text/plain` changes fallback.
- Missing path returns 404.

Dependencies on preceding phases: CLI/root validation and response skeleton.

### Phase 3: Filesystem Safety, Single-File Mode, and Dotfiles

- Harden path resolution:
  - reject NUL after percent decoding;
  - reject malformed percent escapes;
  - reject encoded and literal traversal above root;
  - normalize repeated `/`, `.` and `..`;
  - keep query out of filesystem lookup;
  - reject hidden dotfile path components when `--hide-dotfiles` is set.
- Use a server-local `ZuPercent::Codec` policy for component-level percent
  decoding, including the server's terminator and plus-handling rules. Keep
  server-specific component validation around the decoded output.
- In single-file mode:
  - root is a file;
  - serve only `/`, `/<leafname>`, or the exact requested configured file name
    chosen for compatibility;
  - return 404 for any other path;
  - do not allow directory listing behavior.
- Add explicit behavior for permission errors:
  - 403 when stat/open fails because of access denied;
  - 404 when target does not exist;
  - 500 for unexpected errors.
- Document that symlink escape prevention is a non-goal for the first parity
  pass unless later requirements demand `openat()`/handle-relative traversal.
- Treat the completed `ZuPercent` tests as the generic codec baseline.
  Static-server tests should focus on filesystem policy around decoded
  components, not on retesting every shared percent encode/decode case.

Tests:

- Reject `../`, `%2e%2e`, repeated traversal, and malformed `%` escapes.
- Reject decoded NUL.
- Dotfile hidden and visible modes.
- Single-file serves only intended paths.
- Permission-denied path returns 403 where the platform can create one.

Dependencies on preceding phases: Phase 2 file GET path.

### Phase 4: Directory Canonicalization, Index Files, and Listings

- Add directory handling end to end:
  - if a directory request lacks trailing slash, return 301 to slash path while
    preserving query string;
  - if slash path contains the configured `--index` file, serve it as a normal
    file;
  - if no index and listings enabled, generate an HTML listing;
  - if `--no-listing`, return 403.
- Directory listing requirements:
  - omit `.` and `..`;
  - include parent link only when not at root;
  - omit hidden dotfiles when configured;
  - sort entries by name;
  - mark directories with trailing slash in labels and hrefs;
  - escape HTML text and attributes;
  - percent-encode URL href path components;
  - include size and modification time when available;
  - omit or simplify server footer when `--no-server-id`.
- Keep listing HTML functionally compatible, not byte-for-byte compatible, with
  darkhttpd.
- Use `ZuPercent::Codec` with a server/listing policy for generated hrefs so
  directory listings share the same uppercase `%XX`, truncation-safe, and
  allocation-free behavior as the shared percent codec.

Tests:

- Directory without slash redirects.
- Index file wins over listing.
- Listing includes visible children sorted by name.
- Listing escapes names containing `<`, `&`, quotes, and spaces.
- Listing hrefs work for escaped names.
- `--no-listing` returns 403.
- Dotfiles omitted from listing when hidden.

Dependencies on preceding phases: safe path normalization and file serving.

### Phase 5: Request Policy: Redirects, Basic Auth, Methods, and HEAD

- Add redirect precedence before filesystem lookup:
  - `--forward-all`;
  - first matching `--forward host url`;
  - `--forward-https` for non-TLS HTTP requests;
  - filesystem directory slash redirect from Phase 4.
- Define host matching:
  - strip optional port from Host before comparing;
  - compare case-insensitively;
  - preserve request path/query when constructing redirect target only if that
    is darkhttpd-compatible; otherwise document the exact chosen behavior.
- Add Basic auth:
  - parse `Authorization: Basic ...`;
  - compare against base64 of `username:password`;
  - use constant-time comparison for equal-length byte strings;
  - return 401 with `WWW-Authenticate: Basic realm="zhttpserver"`;
  - document that Basic auth over plain HTTP is insecure.
- Add method handling:
  - GET and HEAD supported;
  - HEAD returns the same headers as GET with no response body;
  - unsupported methods return 405 and `Allow: GET, HEAD`.

Tests:

- GET and HEAD for file and generated listing.
- 301 by host forward.
- 301 catch-all forward.
- 301 HTTP-to-HTTPS forward.
- Auth success, missing auth, wrong scheme, wrong credentials.
- Unsupported method returns 405 with Allow.

Dependencies on preceding phases: request header capture, filesystem response
planning, and directory redirect support.

### Phase 6: Response Header Completeness

- Keep response header names in the server's compile-time `ZhttpHeaders(...)`
  typelists. Do not add a runtime-varying response header-key hook.
- H1 must preserve standards-sensitive header order:
  - status line;
  - `Content-Length` when a body length is known;
  - core server headers;
  - final CRLF.
- H3 must use the existing QPACK path:
  - pseudo headers first;
  - content length and core headers;
- Add `Date`, `Server`, `Last-Modified`, `Accept-Ranges`, `Content-Range`,
  `Location`, and `WWW-Authenticate` to the response plan fields.
- Emit `Server` unless `--no-server-id`.

Tests:

- `--no-server-id` omits Server and listing footer identity.
- H1 and H3 response builder coverage remains compile-time-header based.

Dependencies on preceding phases: response plan fields and H1 static responses.

### Phase 7: Conditional Requests and Range Requests

- Add HTTP-date formatting and parsing:
  - format `Date` and `Last-Modified` with `ZuDateTime::strftime()` using GMT,
    e.g. `%a, %d %b %Y %H:%M:%S GMT`;
  - parsing may use a small server-local parser for IMF-fixdate plus common
    obsolete formats if practical;
  - normalize file mtime to seconds for comparison.
- `If-Modified-Since`:
  - ignore invalid dates and serve normally;
  - return 304 when file mtime <= parsed header time;
  - 304 response includes validator/cache headers but no body.
- Parse single byte ranges:
  - `bytes=start-end`;
  - `bytes=start-`;
  - `bytes=-suffix`.
- Return 206 with `Content-Range` and adjusted `Content-Length`.
- Return 416 for unsatisfiable ranges with `Content-Range: bytes */size`.
- Ignore malformed or unsupported multipart ranges and serve 200 for the first
  pass unless tests or RFC review require 416. Document the chosen behavior.
- For HEAD with Range, emit the same headers as GET range response but no body.

Tests:

- 304 when IMS date is equal/later than mtime.
- 200 when IMS date is older or invalid.
- 206 explicit range.
- 206 open-ended range.
- 206 suffix range.
- 416 unsatisfiable range.
- HEAD range emits no body.
- Range on generated listing is not supported and returns 200 full listing.

Dependencies on preceding phases: file metadata, response headers, and body
streaming.

### Phase 8: H1 Keep-Alive, Idle Timeout, and Maxconn

- Add request lifecycle tracking to H1 TCP and TLS links:
  - increment active on accepted/connected;
  - decrement on disconnect;
  - enforce `--maxconn`;
  - close on parser error or oversized request;
  - reset parser after each complete request when keep-alive is allowed.
- Add HTTP version capture to `Zhttp::H1::Parser` if not already available:
  - request line parser should pass version or a `http10` bool to the
    implementation;
  - use that for HTTP/1.0 close-by-default behavior.
- Keep-alive policy:
  - `--no-keepalive` closes after first response;
  - `Connection: close` closes;
  - HTTP/1.0 closes unless `Connection: keep-alive`;
  - successful HTTP/1.1 keeps alive unless close is required.
- Idle timeout:
  - `--timeout 0` disables app idle timer;
  - otherwise schedule/update one `ZmScheduler::Timer` per link;
  - timeout closes the connection and logs the close reason.

Tests:

- Two GETs on one TCP connection.
- `--no-keepalive` closes after first response.
- `Connection: close` closes.
- HTTP/1.0 default close and optional keep-alive if supported.
- Timeout closes idle connection.
- Maxconn rejects/closes excess connections deterministically.

Dependencies on preceding phases: static response emission and parser reset.

### Phase 9: Access Logging

- Implement common-log-style access lines with:
  - remote address;
  - auth user or `-`;
  - timestamp;
  - request line;
  - status;
  - response bytes;
  - Referer;
  - User-Agent.
- Support stdout default, `--log filename`, and `--syslog`.
- Serialize logging. A `ZmLock` around a `ZiFile` append sink is acceptable
  because logging is outside the hot response path and writes one complete line.
- Escape quotes and control characters in Referer/User-Agent/request target.
- Flush on shutdown.

Tests:

- stdout log line.
- file log append.
- missing Referer/User-Agent become `-`.
- quoted/escaped Referer and User-Agent.
- auth user appears when authenticated.
- syslog path compile coverage where supported.

Dependencies on preceding phases: request/response completion metadata.

### Phase 10: HTTPS / H1 TLS

- Add `TLSServer` and accepted link using the public Ztls/Zhttp server APIs;
  `Zhttp3InteropTest` may be consulted for expected behavior, but no
  `zhttp/test` code should be shared with or copied into the production
  server.
- Configure ALPN:

```c++
ZuCSpan alpn[] = { "http/1.1" };
server.init(
  Ztls::ServerParams(&mx, "3", "4")
    .certPath(options.cert).keyPath(options.key).alpn(alpn));
```

- Reuse the H1 parser, planner, response emitter, keep-alive, timeout, maxconn,
  and logging logic.
- The transport flag `tls=true` should feed `--forward-https` so HTTPS requests
  are not redirected to themselves.

Tests:

- curl HTTPS/H1 with generated local cert.
- `zhttpclient -c CERT https://localhost:PORT/file`.
- HEAD, Range, If-Modified-Since over TLS.
- Keep-alive over TLS.

Dependencies on preceding phases: H1 server feature set.

### Phase 11: HTTP/3 / QUIC Static Responses

- Add `H3Server`, `H3ServerLink`, and `H3ServerStream` using the public
  Zquic/Zhttp H3 APIs; `Zhttp3InteropTest` may be consulted for expected
  behavior, but no `zhttp/test` code should be shared with or copied into the
  production server.
- Configure ALPN `h3`, cert/key, and explicit stream/data limits.
- On connect:
  - validate `Zi::Connected` reports QUIC Version 1 and ALPN `h3`;
  - call `h3.openLocal(*this)`.
- For unidirectional streams:
  - run `Zhttp::H3::CxnParser`;
  - delegate state/QPACK callbacks to `link.h3`;
  - close on duplicate control/encoder/decoder stream errors.
- For bidirectional request streams:
  - parse with `Zhttp::H3ReqParser`;
  - set parser `qpackRx_`, `qpackDecoderWrite_`, and `streamID_`;
  - plan response using the same `StaticPlanner`.
- For responses:
  - use `Zhttp::H3RespBuilder`;
  - set builder `qpackTx_`, `qpackEncoderWrite_`, and `streamID_`;
  - emit generated bodies and mapped file body ranges through H3 DATA frames;
  - keep the `ZiMMapFile` object alive until the Tx-thread lambda has emitted
    the DATA frames and stream FIN;
  - finish with FIN.
- Avoid duplicating QPACK stream writers or peer stream uniqueness logic in the
  example.

Tests:

- curl HTTP/3 static file.
- curl HTTP/3 directory listing.
- curl HTTP/3 Range.
- `zhttpclient --http3-only -c CERT`.
- Duplicate peer control/encoder/decoder stream remains a connection error.

Dependencies on preceding phases: protocol-neutral planner, file streaming, and
H1 static correctness.

### Phase 12: Memory-Mapped File Body Optimization and Sendfile Evaluation

- Treat `ZiMMapFile` as the preferred steady-state file body path, not an
  optional afterthought:
  - map regular non-empty files once per response;
  - write the requested full-file or range span directly from the mapped
    address;
  - close/unmap only after the transport has accepted the body bytes;
  - avoid retaining mappings across requests until correctness and lifetime
    behavior are proven.
- Keep bounded buffered reads as the fallback path:
  - zero-length files;
  - mmap failure;
  - platforms or file types that reject mapping;
  - any case where Tx lifetime cannot be made explicit and safe.
- Add platform `sendfile()` only after memory-mapped correctness is locked:
  - Linux path guarded by `#ifndef _WIN32`;
  - compare against mmap for plain H1 only;
  - verify TLS and H3 remain on mmap/buffered paths because they need encrypted
    or framed output;
  - preserve mmap and buffered fallback everywhere.
- Add measurements or stress tests before changing default buffer sizes.
- Use heap IDs for any new pooled buffers.

Tests:

- Large file body hash matches source.
- Range of large file matches expected slice.
- Mapping failure injection or forced fallback still serves the file.
- Empty file returns correct headers and no mmap attempt.
- Mapped file lifetime is safe for H1 keep-alive and H3 Tx-thread emission.
- Concurrent downloads do not corrupt output.
- Buffered fallback remains covered on all platforms.

Dependencies on preceding phases: complete file serving and range support.

### Phase 13: Daemon, PID File, Chroot, and Privdrop

- Implement `--daemon` and `--pidfile` with `ZiDaemon::init()`:
  - run CLI validation, root validation, and path preflight first;
  - call `ZiDaemon::init()` before opening long-lived files, binding sockets,
    starting `ZiMultiplex`, or creating TLS/QUIC state;
  - pass the PID file path to `ZiDaemon` instead of writing it locally;
  - treat `ZiDaemon::Running` as a clean startup failure with a clear diagnostic;
  - do not hand-roll fork/setsid/reinvoke logic in `zhttpserver.cc`.
- Implement `--chroot`:
  - open log destinations or validate paths before chroot;
  - perform root/path validation before chroot;
  - after chroot, serving root becomes `/` for directory mode or the file leaf
    for single-file mode;
  - fail closed if any step fails.
- Implement `--uid` and `--gid`:
  - resolve numeric and named IDs;
  - call `setgroups`/`initgroups` where available;
  - drop gid before uid;
  - fail closed on any error.
- On Windows, support `--daemon` and `--pidfile` through `ZiDaemon`; reject
  chroot, uid, and gid with clear messages.

Tests:

- PID file written through the `ZiDaemon` path.
- Existing running PID is detected and reported.
- Invalid uid/gid fails.
- Chroot path constraints checked.
- Privdrop/chroot tests gated on root or skipped with TAP diagnostics.

Dependencies on preceding phases: CLI validation, logging, and root semantics.

### Phase 14: Darkhttpd Compatibility Harness, Documentation, and Cleanup

- Update `ZhttpClientServerTest` to serve real static content instead of using
  `--body`.
- Add `ZhttpStaticServerTest` for planner/filesystem/MIME/range/auth/redirect
  unit coverage.
- Add `ZhttpDarkhttpdCompatTest`:
  - run `darkhttpd` and `zhttpserver` side by side against the same temporary
    tree;
  - skip with TAP diagnostics when `darkhttpd` is not installed;
  - compare status, important headers, and body for parity cases.
- Suggested parity cases:
  - file GET;
  - file HEAD;
  - index file;
  - directory listing;
  - no-listing;
  - dotfiles;
  - single-file;
  - Range;
  - If-Modified-Since;
  - forward-all;
  - host forward;
  - auth;
  - MIME override.
- Update `zhttp/README.md` with:
  - static server quick start;
  - H1/H3 examples;
  - cert generation notes for local tests;
  - unsupported platform-specific options;
  - security notes for Basic auth, symlinks, and chroot.
- Remove temporary `--body`.
- Split implementation further only if needed:
  - keep transport wiring in `zhttpserver.cc`;
  - keep static policy, planner, and filesystem utilities in
    `ZhttpStaticServer.hh`.

Tests:

- `make -C zhttp/example zhttpserver`.
- `make -C zhttp/test ZhttpClientServerTest ZhttpStaticServerTest`.
- Optional `ZhttpDarkhttpdCompatTest` when darkhttpd is available.

Dependencies on preceding phases: all user-facing behavior.

## Code References to Impacted Code

- `zhttp/example/zhttpserver.cc:1` - Replace fixed-body HTTP demo with CLI,
  process setup, server startup, and thin H1/H3 transport glue.
- `zhttp/example/ZhttpStaticServer.hh:1` - New static-server planner,
  filesystem, MIME, response, range, date, auth, redirect, listing, memory-map,
  and logging support.
- `zhttp/example/Makefile.am:20` - Add `ZhttpStaticServer.hh` to sources or
  distribution metadata if needed by automake conventions.
- `zhttp/src/Zhttp.hh:41` - Existing `ZhttpHeaders(...)` typelist macro used
  for selected request/response headers.
- `zhttp/src/ZhttpH1.hh:121` - H1 request-line parser currently discards HTTP
  version; add a callback if HTTP/1.0 keep-alive behavior needs exact support.
- `zhttp/src/ZhttpH1.hh:355` - H1 builder emits compile-time headers; preserve
  compile-time response header keys.
- `zhttp/src/ZhttpH3.hh:1415` - H3 builder emits compile-time headers through
  the existing QPACK path; do not expose runtime-varying response header keys.
- `zhttp/src/ZhttpH3Session.hh:17` - Reuse `Zhttp::H3::Cxn` for H3
  control/QPACK stream ownership.
- `zhttp/test/ZhttpTestUtil.hh:1` - Test-only helper facade for temp dirs,
  generated certs, curl HTTP/3 detection, loopback ports, diagnostics, and
  retry loops. It may be used by test binaries only; `zhttpserver` and
  `zhttpclient` must not include or link it.
- `zhttp/test/Zhttp3InteropTest.cc:1056` - Test-only behavioral reference for
  TCP server coverage; do not copy test helper code into production.
- `zhttp/test/Zhttp3InteropTest.cc:1091` - Test-only behavioral reference for
  TLS/H1 server parameters and ALPN coverage; do not copy test helper code into
  production.
- `zhttp/test/Zhttp3InteropTest.cc:1126` - Test-only behavioral reference for
  QUIC/H3 server parameters, ALPN, and stream limit coverage; do not copy test
  helper code into production.
- `zhttp/test/ZhttpClientServerTest.cc:39` - Existing fixed-body integration
  test that must migrate to static file serving.
- `zhttp/test/Makefile.am:23` - Add `ZhttpStaticServerTest` and
  `ZhttpDarkhttpdCompatTest` binaries.
- `zi/src/ZiFile.hh:8` - `ZiFile` is unbuffered; response body streaming must
  prefer `ZiMMapFile` mapped spans, with bounded buffers only as fallback.
- `zi/src/ZiFile.hh:232` - `ZiMMapFile` exposes `mmap()`, `addr()`,
  `mmapLength()`, and `close()` for mapped file response bodies.
- `zi/src/ZiFile.cc:355` - `ZiMMapFile::mmap()` rejects non-positive lengths
  and should be tested before use with read-only static file serving.
- `zi/src/ZiFile.hh:170` - `ZiFile::mtime`, `exists`, `isdir`, and path helpers
  are available for filesystem metadata.
- `zi/src/ZiDir.hh:30` - `ZiDir::read()` returns names only; listing code must
  sort/stat entries itself.
- `zu/src/ZuPercent.hh:1` - Shared header-only percent codec; use it directly
  for static-server href encoding and any server-specific decode policy.
- `zu/test/ZuPercentTest.cc:1` - Generic codec coverage for span calculation,
  encoding, overflow reporting, printing, decoding, malformed escapes, plus
  handling, truncation, and zero-fill behavior.
- `zt/src/ZtURI.hh:1` - `ZtURI` also delegates to `ZuPercent`, but it should
  remain outside `zhttpserver`; it is intended for `ZtStruct` structured type
  conversion to/from query strings.
- `zm/src/ZmScheduler.hh:326` - Scheduler timer API for idle timeout.
- `ztls/src/Ztls.hh:126` - `Ztls::ServerParams` supports cert/key/ALPN.
- `zquic/src/Zquic.hh:1015` - `Zquic::Server` requires cert/key and uses
  configured ALPN and stream limits.
- `zhttp/README.md:1` - Add static server usage and security notes.

## Detailed Test Plan

Use `ZuTestUtil` TAP-style tests for C++ unit/integration coverage, matching
current zhttp tests.

Add `zhttp/test/ZhttpStaticServerTest.cc`:

- CLI validation helpers if CLI parsing is factored into testable functions.
- Path decode/normalize:
  - `/a/b`;
  - repeated slash;
  - `.` components;
  - traversal rejection;
  - encoded traversal rejection;
  - malformed percent rejection;
  - decoded NUL rejection.
  These are static-server policy tests layered over `ZuPercent`; generic
  percent codec behavior is already covered by `ZuPercentTest`.
- MIME map:
  - built-in extension;
  - case-insensitive extension;
  - custom mimetypes file;
  - fallback default.
- Planner:
  - GET/HEAD file;
  - missing file;
  - permission denied if platform permits;
  - directory slash redirect;
  - index file;
  - listing;
  - no-listing;
  - dotfiles;
  - single-file;
  - auth;
  - forwards;
  - conditional requests;
  - ranges.
- Memory-mapped body planning/emission:
  - non-empty file maps and serves from the requested span;
  - zero-length file skips mapping;
  - forced mmap failure uses bounded buffered fallback;
  - range slice points at the correct mapped offset.

Extend `zhttp/test/ZhttpClientServerTest.cc`:

- Start `zhttpserver TEMP/root --http --addr 127.0.0.1 --port PORT`.
- Fetch a real file with `zhttpclient`.
- Verify body and basic headers.
- Remove `--body` once this passes.

Extend or add TLS/H3 integration:

- In test binaries only, use `ZhttpTestUtil.hh` for temp dirs, cert
  generation, curl H3 detection, loopback ports, file diagnostics, and retry
  loops. Do not include it from `zhttpserver` or `zhttpclient`.
- Test HTTPS/H1 with curl and `zhttpclient`.
- Test HTTP/3 with curl where available and `zhttpclient --http3-only`.
- Skip with TAP diagnostics when curl lacks HTTP/3.

Add `zhttp/test/ZhttpDarkhttpdCompatTest.cc`:

- Detect `darkhttpd` in PATH.
- Spawn both servers against the same tree on loopback ports.
- Compare status, selected headers, and body for the parity cases listed in
  Phase 14.
- Skip when missing.

Manual smoke commands after implementation:

```sh
./zhttp/example/zhttpserver /tmp/www --http --port 8080
./zhttp/example/zhttpserver /tmp/www --https --cert cert.pem --key key.pem --port 8443
./zhttp/example/zhttpserver /tmp/www --http3 --cert cert.pem --key key.pem --port 8443
curl -v http://127.0.0.1:8080/
curl -vk --http1.1 https://localhost:8443/
curl -vk --http3-only https://localhost:8443/
```

## Acceptance Criteria

- `make -C zhttp/example zhttpserver` passes.
- `make -C zhttp/test ZhttpClientServerTest ZhttpStaticServerTest` passes.
- Optional `ZhttpDarkhttpdCompatTest` passes or skips clearly when darkhttpd is
  not installed.
- `zhttpserver ROOT --http --port PORT` serves files, indexes, and directory
  listings.
- `zhttpserver ROOT --https --cert CERT --key KEY --port PORT` serves the same
  content over HTTP/1.1 TLS.
- `zhttpserver ROOT --http3 --cert CERT --key KEY --port PORT` serves the same
  content over HTTP/3.
- `zhttpclient` can fetch from all three transports.
- curl can fetch from all three transports where local curl supports HTTP/3.
- GET, HEAD, Range, If-Modified-Since, redirects, Basic auth, MIME overrides,
  hidden dotfiles, single-file mode, and no-listing behavior
  pass tests.
- Path traversal and malformed URL tests pass.
- Large file and range responses do not buffer entire files in memory.
- Non-empty static files are served from `ZiMMapFile` mappings when mapping is
  available, with bounded `ZiFile` reads only as fallback.
- H1 keep-alive supports multiple sequential requests and idle timeout.
- `--maxconn` is enforced deterministically.
- Access logs are complete and not interleaved.
- `--daemon` uses `ZiDaemon`, with PID file creation and running-PID detection
  covered by tests.
- `zhttpserver` and `zhttpclient` do not include headers from `zhttp/test`,
  link objects from `zhttp/test`, or depend on test-only helper code.
- No direct local `QPackEncoderTx` subclasses are introduced in
  `zhttpserver.cc`.
- H3 control/QPACK stream setup remains in `Zhttp::H3::Cxn`.
- Daemon/PID behavior works through `ZiDaemon`; chroot/privdrop options either
  work on supported Unix systems or fail clearly on unsupported platforms.

## Non-goals

- CGI, FastCGI, reverse proxying, upload handling, WebDAV, Lua/plugin hooks,
  compression, TLS certificate automation, HTTP/2, H3 server push,
  WebTransport, and QUIC DATAGRAM.
- Arbitrary runtime response header names and the darkhttpd-style `--header`
  CLI option. Zhttp response header keys remain compile-time fixed by the
  application.
- Perfect byte-for-byte HTML directory listing compatibility with darkhttpd.
  Functional equivalence is sufficient: safe escaped listing, links, sizes,
  and modification times.
- Multipart Range support in the first parity pass.
- Symlink sandboxing inside the document root in the first parity pass. Root
  traversal through request path components must be prevented; symlink escape
  prevention requires a stricter handle-relative filesystem policy and should
  be a separate requirement.
- Legacy compatibility for the temporary `--body` mode after static-file tests
  have been migrated.

## Options and Open Questions

Resolved decisions:

- Use a protocol-neutral `StaticPlanner` and shared `ResponsePlan`; keep
  transport links thin.
- Sequence features vertically: make each static-server behavior work over H1
  first, then reuse the planner for TLS/H1 and H3.
- Use manual CLI parsing for the first implementation because repeated ordered
  `--forward` values are central to this server.
- Use `ZuPercent::Codec` directly for the static server's path, query, and
  listing policies; keep server-local validation around decoded components.
- Implement single-range support only; multipart ranges are a non-goal until a
  concrete compatibility requirement appears.
- Treat symlink escape prevention as a non-goal for the first pass, explicitly
  documented.

Implementation options to revisit during coding:

- Invalid Range behavior: preferred first-pass behavior is to ignore malformed
  ranges and serve 200, but return 416 for syntactically valid unsatisfiable
  ranges. Confirm against darkhttpd during the compatibility harness phase.
- Single-file URL aliases: decide whether `/`, `/<leafname>`, or only `/` maps
  to the configured file after checking darkhttpd behavior locally.

Open questions: none that block planning. The items above are implementation
choices with preferred defaults and can be validated in tests.
