## Summary

The goal is to address the findings in `findings.md` for `zhttpserver` and
`zhttpclient` without turning the example programs into a broad transport-layer
rewrite. The revised implementation should preserve current externally visible
example behavior except where the findings identify unsafe or incorrect
behavior.

The two high-severity items are the priority:

- static file serving currently authorizes one pathname and later reopens that
  pathname, leaving a TOCTOU/symlink escape window;
- successful `ZiMMapFile` response serving truncates bodies larger than 4GiB by
  casting `uint64_t fileLength` to `unsigned` for a single `ZuCSpan`.

Local API review found that `ZiFile` currently exposes pathname `open()`,
`pread()`, `size()`, and pathname static helpers, but does not expose
fd-relative opening, no-follow opening, directory handles, `fstat()` metadata,
or mmap-from-existing-handle support. `ZtCLI` supports long options
`--name=value` and `--name`, short options/flags from metadata, positional
arguments, and vector positional arguments, but it does not naturally model the
existing repeated `--forward host url` pair as one repeatable long option
without either a custom field format or a small compatibility scanner.

Web research supports the local direction: Linux `openat()` exists specifically
to avoid races in path prefixes by holding a directory fd, `O_NOFOLLOW` fails
final symlink opens, libuv exposes the same `O_NOFOLLOW` concept with a Windows
support caveat, and nginx's symlink-disabling implementation uses
`openat(O_NOFOLLOW)` on each path component to avoid races.

Research references:

- Linux `open(2)` / `openat(2)`: https://man7.org/linux/man-pages/man2/open.2.html
- Linux `openat2(2)` and `RESOLVE_NO_SYMLINKS`: https://man7.org/linux/man-pages/man2/openat2.2.html
- libuv filesystem flags, including `UV_FS_O_NOFOLLOW`: https://docs.libuv.org/en/v1.x/fs.html
- nginx `disable_symlinks` changeset note: https://freenginx.org/hg/nginx/file/7033faf6dc3c/conf/nginx.conf
- Static Web Server symlink policy: https://static-web-server.net/features/disable-symlinks/

The implementation plan is sequenced as vertical slices:

1. make static file planning and sending handle-backed and no-follow capable;
2. fix body emission for large mapped and read-backed files;
3. add regression tests around the security and large-body behavior;
4. then perform lower-risk example alignment work: constants, CLI, parser
   ownership, logging, stale flush removal, and broader STL reduction.

## Architecture Documentation

### New or changed components

- `zi/src/ZiFile.hh` / `zi/src/ZiFile.cc`
  - Add a small fd-relative/no-follow file-opening surface used by static file
    serving and testable independent of HTTP.
  - Add handle metadata helpers so callers derive size, mtime, and file type
    from the opened handle rather than from a later pathname lookup.
  - Add a way to duplicate/shadow an already opened handle for streaming if
    existing copy semantics are insufficient for safe ownership.

- `zhttp/example/ZhttpStaticServer.hh`
  - Replace serving-time `std::filesystem::symlink_status()` plus later
    pathname reopen with a handle-backed planning result.
  - Keep normalized request paths as the routing identity and separate them
    from OS handles.
  - Keep generated bodies, redirects, auth, ranges, HEAD, directory indexes,
    directory listing, `--single-file`, `--chroot`, and hidden-dotfile behavior.

- `zhttp/example/zhttpserver.cc`
  - Send file response bodies from the authorized handle/path state using
    bounded chunks.
  - Move repeated constants into named example policy constants.
  - Later convert runtime diagnostics to `ZiLOG` and move option loading toward
    `ZtCLI`.

- `zhttp/example/zhttpclient.cc`
  - Move repeated constants into named example policy constants.
  - Remove the extra stale flush after `builder.finish(tx)`.
  - Remove raw parser heap ownership from `CliLink`.
  - Later convert runtime diagnostics to `ZiLOG`.

- `zhttp/test/ZhttpStaticServerTest.cc`
  - Extend static-server coverage for symlink rejection, opened-file metadata,
    range/HEAD/listing regressions, and large-body chunk emission.

- `zt/test/ZtCLITest.cc` or a new zhttp-specific CLI test
  - Add focused coverage if `zhttpserver` option parsing is converted via
    `ZtCLI`.

### New or changed processes or threads

No new threads or process lifecycle changes are required. Existing
`ZiMultiplex` scheduler usage remains unchanged.

### New or changed interfaces

Add the minimum `ZiFile` API needed to avoid path races. Exact names should
match local style during implementation; this is the intended shape:

```c++
class ZiFile {
public:
  enum Flags {
    NoFollow = 0x20000,
    Directory = 0x40000
  };

  struct Stat {
    Offset size = 0;
    ZuTime mtime;
    bool regular = false;
    bool directory = false;
  };

  int openAt(const ZiFile &dir, const Path &name,
    unsigned flags, unsigned mode = 0666, Offset length = 0);
  int fstat(Stat &stat) const;
  int dup(const ZiFile &file, unsigned flags = GC);
};
```

Linux implementation details:

- `Directory` maps to `O_DIRECTORY`.
- `NoFollow` maps to `O_NOFOLLOW`.
- `openAt()` maps to `::openat(dir.handle(), name, openFlags, mode)`.
- `fstat()` maps to `::fstat(handle(), &st)` and fills type, size, and mtime.
- `dup()` maps to `::dup()` so `ResponsePlan` can own a stable handle without
  aliasing lifetime unexpectedly.

Windows implementation details:

- Preserve buildability and current behavior where exact POSIX semantics are
  unavailable.
- Use `CreateFile()` with `FILE_FLAG_OPEN_REPARSE_POINT` for pathname
  no-follow checks.
- Use `NtCreateFile()` with `OBJECT_ATTRIBUTES::RootDirectory` for
  fd-relative `openAt()` semantics instead of reconstructing a string path from
  the directory handle.
- Use `FILE_FLAG_BACKUP_SEMANTICS` for directory opens where needed.
- Treat Windows reparse points as no-follow failures after opening them with
  reparse-aware flags, so callers get consistent "do not follow symlink" API
  semantics.
- Windows cannot be tested in the current Linux workspace. Keep the Windows
  implementation compiled by guarded source, but require a separate MinGW/MSYS2
  verification pass before claiming Windows runtime acceptance.

`ResponsePlan` should hold authorized file state:

```c++
struct ResponsePlan {
  ...
  ZiFile fileHandle;
  ZtString<> filePath;       // retained only for diagnostics / mmap fallback
  uint64_t fileOffset = 0;
  uint64_t fileLength = 0;
  bool file = false;
};
```

If `ZiMMapFile` cannot map from an existing file handle without unsafe path
reopen, either add `ZiMMapFile::mmap(ZiFile &&file, ...)` or skip mmap for
handle-backed static responses initially. Correctness is more important than
preserving mmap use in this example.

### New or changed data flows

Current unsafe flow:

1. request path is normalized into a `std::string`;
2. `std::filesystem::symlink_status(path)` authorizes it;
3. later `file_size()`, `last_write_time()`, `mmap()`, or `open()` reuses the
   pathname.

Target safe flow:

1. request path is percent-decoded and normalized;
2. the root directory is opened once as a directory handle;
3. each normalized component is opened relative to the previous directory;
4. final file opens use no-follow semantics and `fstat()` confirms regular
   file type;
5. `ResponsePlan` carries the already authorized handle and metadata;
6. `sendBody()` streams exactly that handle.

Generated responses, redirects, auth failures, 304, 405, 416, and directory
listings continue to use in-memory `ZtString<>` bodies.

### New or changed event-driven or timer processing

No event-loop, scheduler, idle-timer, or QUIC stream state changes are required.
The body sending helper still writes synchronously to the existing response body
stream used by H1 and H3 builders.

### New or changed network programming

No transport protocol changes are planned. HTTP/1, TLS, and QUIC/H3
construction remains unchanged aside from named constants for buffer sizes and
flow-control limits.

### New or changed data stores

No persistent data store is added. MIME lookup may later move from
`std::vector<std::pair<std::string, std::string>>` to a Z container such as
`ZmHashKV<ZtString<>, ZtString<>, ...>` or `ZmLHashKV` with normalized extension
keys. This is an in-memory example cache only.

## Detailed Design and Implementation Plan

### Phase 1: Add safe `ZiFile` primitives and tests

Add the lower-layer primitives needed before rewriting the static server. This
phase is intentionally independent of HTTP so API behavior is clear.

Modify:

- `zi/src/ZiFile.hh`
- `zi/src/ZiFile.cc`
- a suitable `zi/test/*` test binary if one already exists for file behavior,
  otherwise add `zi/test/ZiFileTest.cc` and update `zi/test/Makefile.am`.

Implementation details:

- Add `ZiFile::NoFollow` and `ZiFile::Directory` flags.
- Update `open_()` flag translation for Linux and Windows.
- Add `openAt()` on POSIX with relative names only. Reject absolute paths and
  names containing `/`; path walking belongs above this helper.
- Add handle-backed `fstat()` returning type, size, and mtime.
- Add a handle duplication helper if `ResponsePlan` needs independent
  ownership. Avoid shadow copies that skip `close()` unless ownership is
  explicit.
- Keep C++ style aligned with `ZiFile` and do not introduce STL types.

API scrutiny:

- POSIX `O_NOFOLLOW` only protects the final path component for a single
  `open()` call. Therefore static serving must open every path component
  fd-relative, not just call `openat(root, "a/b/c", O_NOFOLLOW)`.
- `openat()` protects against parent directory replacement only if the parent
  directory fd was itself obtained safely and retained.
- Windows cannot exactly mirror POSIX `openat()`; implementation and tests must
  make the portability boundary explicit.

Tests:

- Open a regular file relative to an opened directory.
- Reject a final symlink with `NoFollow` on POSIX.
- On Windows, add equivalent reparse-point coverage when running in a Windows
  environment that permits symlink creation; this environment cannot execute
  that test.
- Reject `openAt()` names containing `/` or absolute paths.
- Confirm `fstat()` reports regular file, directory, size, and a non-null mtime.
- Confirm duplicated handles can be read after the original wrapper is closed.

### Phase 2: Make static file planning handle-backed end to end

Replace the security-sensitive file-planning path with fd-relative traversal and
stable file handles. This resolves the high-severity TOCTOU/symlink escape for
regular-file serving.

Modify:

- `zhttp/example/ZhttpStaticServer.hh`
- `zhttp/test/ZhttpStaticServerTest.cc`

Implementation details:

- Extend `State` with a root directory handle for directory mode if practical,
  initialized after `validate()`/before serving. For tests, make initialization
  explicit in `initState()`.
- For `--single-file`, open the configured file once with no-follow semantics
  and plan from the handle, or open its parent directory and final leaf
  fd-relative. Preserve the accepted URLs `/` and `/<leaf>`.
- Replace `fileOrDir()` regular-file authorization with:
  - normalized component iteration;
  - fd-relative directory open for intermediate components;
  - no-follow final file open;
  - handle-backed `fstat()` for type, size, and mtime.
- For directory index lookup, open the directory handle first, then open the
  configured index name relative to that directory with no-follow semantics.
- Directory listing may initially remain pathname-based only if regular-file
  serving is already safe. Listing entries must still be generated from
  normalized leaf names and hidden-dotfile filtering must remain.
- Store the opened file in `ResponsePlan::fileHandle`; keep `filePath` only if
  needed for MIME lookup or diagnostics.
- MIME lookup should use the normalized request path or final leaf, not a
  re-resolved OS pathname.

Tests:

- POSIX-only: final symlink inside root pointing outside root returns 403 or 404
  and never produces a file response.
- Normal file GET and HEAD still return 200, correct `contentLength`,
  `contentType`, and `sendBody`.
- Directory slash redirect still preserves query string.
- Directory index still wins over listing.
- Directory listing still sorts entries and applies `--hide-dotfiles`.
- Range and unsatisfiable range behavior still matches existing tests.
- `If-Modified-Since` still returns 304 when appropriate.

### Phase 3: Fix body emission and large-file truncation

Make response body sending bounded and handle-backed. This phase resolves the
second high-severity finding and integrates with Phase 2.

Modify:

- `zhttp/example/zhttpserver.cc`
- `zhttp/test/ZhttpStaticServerTest.cc`

Implementation details:

- Add a named file-copy chunk policy constant, for example:

```c++
namespace {
  constexpr unsigned ZhttpFileChunk = 16<<10;
}
```

- Add a helper used by both mmap and pread paths:

```c++
template <typename Body>
void sendSpanChunks(Body &body, const char *p, uint64_t len) {
  while (len) {
    unsigned n = len > ZhttpFileChunk ? ZhttpFileChunk : unsigned(len);
    body << ZuCSpan{p, n};
    p += n;
    len -= n;
  }
}
```

- If mmap remains path-backed, do not use it for handle-backed static responses
  until `ZiMMapFile` can map the authorized handle. A path-backed mmap would
  reintroduce the TOCTOU issue fixed in Phase 2.
- If mmap-from-handle is added, map only the required range or a safe window and
  always emit with `sendSpanChunks()`. Never cast full `fileLength` to
  `unsigned`.
- Keep the `pread()` fallback, but read from `resp.fileHandle` and replace the
  raw `char buf[16384]` literal with the named constant.
- Check `pread()` return values carefully: stop on `Zi::EndOfFile`/error and do
  not underflow `left` if a short read occurs.

Tests:

- Add a fake body sink test for `sendSpanChunks()` that records chunk sizes for
  a length greater than `UINT_MAX` without allocating a huge buffer.
- Add a sparse-file or direct helper test for a >4GiB planned `fileLength` if
  the platform and filesystem make that cheap; otherwise keep the unit test at
  helper level and document why no runtime fixture is used.
- Verify normal small-file body sending still writes exact bytes.
- Verify ranged body sending starts at `fileOffset` and sends `fileLength`
  bytes.

### Phase 4: Centralize example policy constants

Replace repeated hard-coded capacities with named constants after the security
and body-sending code has settled enough that constants will not churn.

Modify:

- `zhttp/example/zhttpserver.cc`
- `zhttp/example/zhttpclient.cc`
- optionally a small shared example header if both programs naturally share
  constants and the include does not add coupling.

Constants to name:

- request parser body cap: `1<<20`
- response parser body cap: `100<<20`
- socket buffer builtin/max sizes: `8<<10`, `100<<20`
- QUIC connection and stream windows: `100<<20`, `16<<20`
- max bidirectional/unidirectional streams: `64`, `16`
- file copy chunk size: `16<<10`
- DNS scratch IP count: `8`
- client timeout: `10`
- max redirects: `8`
- mimetype file cap currently `16<<20`

Implementation details:

- Put constants near the example/application boundary, not in low-level
  transport headers.
- Add short comments only where the value is policy rather than protocol.
- Keep names succinct: examples include `ReqBodyMax`, `RespBodyMax`,
  `BufBuiltin`, `BufMax`, `H3DataMax`, `H3StreamDataMax`, `H3BidiMax`,
  `H3UniMax`, `FileChunk`, `DNSMaxIPs`.

Verification:

- Build `zhttp/example/zhttpserver` and `zhttp/example/zhttpclient`.
- Run `zhttp/test/ZhttpStaticServerTest`.

### Phase 5: Remove stale client flush FIXME

Resolve the low-risk correctness cleanup while the client request path is still
otherwise unchanged.

Modify:

- `zhttp/example/zhttpclient.cc`

Implementation details:

- Remove `tx << Zi::flush()` after `builder.finish(tx)` in `sendH1Request()`.
- Remove the FIXME comment.
- The finding already confirmed `Zhttp::H1::Builder::finish()` flushes in
  `zhttp/src/ZhttpH1.hh`; keep behavior centralized there.

Verification:

- Run the available HTTP client/server integration test if present.
- At minimum build `zhttp/example/zhttpclient` and run an HTTP smoke test
  against `zhttpserver`.

### Phase 6: Remove raw parser heap ownership in `zhttpclient`

Replace `CliLink::Parser *parser` with direct or explicitly owned storage.

Modify:

- `zhttp/example/zhttpclient.cc`

Implementation details:

- Preferred approach: embed `Parser parser{this, &this->app()->state}` if
  construction timing permits and `this->app()` is usable in the constructor.
- If direct construction is awkward because CRTP base state is not ready, use a
  `ZuUnion<void, Parser>` or local placement-storage pattern and construct on
  first response:

```c++
ZuUnion<void, Parser> parser;

Parser &parser_() {
  if (parser.template is<void>())
    new (parser.template new_<Parser, true>()) Parser{this, &this->app()->state};
  return parser.template p<Parser>();
}
```

- Do not keep raw `new`/`delete`.
- Confirm both H1 and H3 parser aliases still bind the correct `CliLink` type
  and stream ID callback.

Verification:

- Build `zhttp/example/zhttpclient`.
- Run `zhttp/test/Zhttp3InteropTest` if configured dependencies permit.
- Smoke HTTP and HTTPS where local cert setup permits.

### Phase 7: Convert `zhttpserver` option parsing to `ZtCLI`

Move the server parser toward the structured pattern already used by
`zhttpclient`, while preserving current command spellings and semantics.

Modify:

- `zhttp/example/ZhttpStaticServer.hh`
- `zhttp/example/zhttpserver.cc`
- add tests in `zhttp/test/ZhttpStaticServerTest.cc` or a new focused CLI test.

Implementation details:

- Add `ZtStruct((Options, CLI), ...)` metadata for scalar options and flags.
- Preserve root as `CLI::Arg<1>`.
- Preserve long option spellings exactly: `--no-listing`, `--default-mimetype`,
  `--single-file`, `--hide-dotfiles`, `--forward-all`, `--forward-https`,
  `--no-server-id`, `--http3`, and so on.
- `ZtCLI::Parser::scanArg()` accepts `--name=value` and `--name`, and
  `ZtCLI::load()` returns the count of positional arguments. Account for this
  return value when validating the single root arg.
- Preserve the existing `--forward host url` repeatable pair. Because current
  `ZtCLI` long-option loading does not group two following argv values into one
  repeated object naturally, use one of these approaches:
  - implement a small custom `ZtCLI_Fmt(Forward *)`/array loader if it can read
    paired values cleanly; or
  - keep a narrow pre-scan for `--forward` pairs, remove them from argv, and let
    `ZtCLI` load the rest.
- Keep `parseAuth()` as post-parse validation unless a custom CLI string format
  is cleaner.
- Replace `parseUInt()` for `port`, `timeout`, and `maxconn` with typed fields,
  then keep explicit validation for `port <= 65535`.
- Preserve non-Windows root-user default port change from `8080` to `80`.
- Preserve default transport selection: if HTTPS or H3 is requested and
  `--http` was not explicitly provided, disable HTTP.

Tests:

- root positional required and duplicate root rejected;
- `--port 8081` and `--port=8081`;
- invalid port and overflow rejected;
- repeated `--forward host url`;
- auth with and without colon;
- HTTPS/H3 require cert and key;
- default HTTP disabled when only HTTPS/H3 is requested;
- explicit `--http --https` keeps HTTP enabled;
- Windows-only unsupported flags remain guarded if tests can compile that path.

### Phase 8: Convert runtime diagnostics to `ZiLog`

Make logging consistent with repository guidance and daemon/syslog behavior.

Modify:

- `zhttp/example/zhttpserver.cc`
- `zhttp/example/zhttpclient.cc`

Implementation details:

- Leave `usage()` output on `std::cerr`.
- Convert runtime diagnostics after `ZiLog::start()` to `ZiLOG` or
  `ZiLogEvent`.
- For diagnostics before `ZiLog::start()`, either initialize logging earlier or
  keep a minimal stderr failure path if logging cannot be initialized yet.
- Align HTTP listening with HTTPS/H3 listening by using `ZiLOG`.
- Preserve status/header/framing/connect/disconnect information content in
  `zhttpclient`, but route through component names such as `zhttpclient` and
  `zhttpclient.response`.
- Confirm daemon/syslog mode no longer emits normal runtime diagnostics to
  stderr.

Verification:

- Foreground smoke with stderr sink.
- Syslog/daemon smoke where supported.
- Existing access log output remains under `zhttpserver.access`.

### Phase 9: Reduce STL and filesystem churn in static server support

After the security-sensitive file-serving path is correct and tested, replace
remaining request-path STL use with Z Framework facilities.

Modify:

- `zhttp/example/ZhttpStaticServer.hh`
- `zhttp/test/ZhttpStaticServerTest.cc`

Implementation details:

- Replace `str()` conversion hotspots with `ZuCSpan`/`ZtString<>` flows.
- Convert path normalization to decode into `ZtString<>` or `ZtLocalArray`
  scratch and track components as spans or offsets.
- Replace `std::vector<std::string>` component storage with `ZtArray` or a
  scratch component table.
- Replace MIME map with a Z hash:

```c++
using MimeHash = ZmHashKV<ZtString<>, ZtString<>,
  ZmHashCmp<ZuICmp, ZmHashHeapID<"ZhttpStatic.Mime">>>;
```

  If `ZmHashKV` case-insensitive comparison is awkward for temporary spans,
  normalize extensions to lowercase once and use the default comparator.
- Preserve override semantics: later mimetype entries supersede earlier
  builtins. If `ZmHash::add()` does not replace existing values, delete/update
  explicitly or use a container operation that does.
- Convert directory listing accumulation to `ZtArray<Entry>` and sort with
  `ZuSort` rather than `std::sort`.
- Replace `std::to_string` response construction with streaming into
  `ZtString<>` using `ZuBox`.
- Remove unused `<algorithm>`, `<filesystem>`, `<string>`, and `<vector>`
  includes only after their last use is gone.

Verification:

- Build with the default compiler.
- Build with clang configuration if available (`./z.config -L ...`) and not
  disruptive to the current workspace.
- Run `zhttp/test/ZhttpStaticServerTest`.
- Manual smoke for directory listing, MIME override, range, redirect, auth, and
  hidden-dotfile behavior.

## Code References to Impacted Code

- `zi/src/ZiFile.hh:35` - add `NoFollow`/`Directory` flags and handle metadata
  declarations.
- `zi/src/ZiFile.hh:104` - add `openAt()`, `fstat()`, and handle duplication
  declarations near existing `open()`/`size()` APIs.
- `zi/src/ZiFile.cc:214` - extend POSIX/Windows flag translation in
  `ZiFile::open_()`.
- `zi/src/ZiFile.cc:525` - use existing `pread()` behavior for handle-backed
  streaming; no semantic change expected.
- `zhttp/example/ZhttpStaticServer.hh:105` - extend `ResponsePlan` with stable
  authorized file state.
- `zhttp/example/ZhttpStaticServer.hh:257` - replace `decodeNormalizePath()`
  STL component storage in the later cleanup phase.
- `zhttp/example/ZhttpStaticServer.hh:405` - keep high-level planning flow but
  route static-file cases through safe handle-backed helpers.
- `zhttp/example/ZhttpStaticServer.hh:505` - replace `fileOrDir()` pathname
  `symlink_status()` authorization.
- `zhttp/example/ZhttpStaticServer.hh:546` - replace `regularFile()` pathname
  `file_size()`/`last_write_time()` metadata with handle-backed metadata.
- `zhttp/example/ZhttpStaticServer.hh:636` - convert directory listing after
  file-serving security work is complete.
- `zhttp/example/ZhttpStaticServer.hh:704` - keep or adapt `parseForward()`
  depending on the final `ZtCLI` conversion approach.
- `zhttp/example/ZhttpStaticServer.hh:710` - keep `parseAuth()` as post-parse
  validation unless a custom CLI string format is introduced.
- `zhttp/example/zhttpserver.cc:67` - remove `parseUInt()` when `ZtCLI` owns
  numeric loading.
- `zhttp/example/zhttpserver.cc:79` - replace `loadOptions()` with
  `ZtCLI`-based loading plus compatibility handling for `--forward`.
- `zhttp/example/zhttpserver.cc:428` - rewrite `sendBody()` around bounded
  chunk helpers and authorized handles.
- `zhttp/example/zhttpserver.cc:528` - convert HTTP listening diagnostic to
  `ZiLOG`.
- `zhttp/example/zhttpserver.cc:813` - convert post-logging-init runtime errors
  to `ZiLOG`.
- `zhttp/example/zhttpclient.cc:33` - reuse the existing `ZtStruct` CLI pattern
  as the model for server parsing.
- `zhttp/example/zhttpclient.cc:81` - consolidate `ClientTimeout` and
  `MaxRedirects` with nearby example constants.
- `zhttp/example/zhttpclient.cc:350` - remove stale extra H1 flush.
- `zhttp/example/zhttpclient.cc:369` - convert response diagnostics to `ZiLog`.
- `zhttp/example/zhttpclient.cc:500` - remove lazy raw `new Parser`.
- `zhttp/example/zhttpclient.cc:538` - remove manual parser `delete`.
- `zhttp/test/ZhttpStaticServerTest.cc:20` - reuse existing temp-file helpers
  and extend with symlink/large-body cases.
- `zhttp/test/Makefile.am:24` - static-server test binary already exists; add a
  new CLI test here only if CLI coverage does not fit the existing test.
- `zt/src/ZtCLI.hh:1090` - actual long/short option scan behavior that the
  server CLI conversion must respect.
- `zt/test/ZtCLITest.cc:104` - existing CLI test pattern for parser/handler
  round-trips.

## Detailed Test Plan

- `ZiFile` API tests:
  - fd-relative open succeeds for a child file;
  - no-follow final symlink rejection works on POSIX;
  - directory open reports directory metadata;
  - regular file metadata reports size and mtime;
  - rejected names include absolute paths and names containing `/`;
  - duplicated handle ownership is independent.

- Static planner tests:
  - normal file GET and HEAD;
  - missing file 404;
  - final symlink escape rejected on POSIX;
  - directory slash redirect;
  - directory index precedence over listing;
  - listing generation and sort order;
  - hidden dotfiles rejected and hidden listing entries omitted;
  - single-file mode accepts `/` and `/<leaf>` only;
  - auth success/failure;
  - forward-all and host-specific forward;
  - `If-Modified-Since` 304;
  - explicit, suffix, invalid, and unsatisfiable ranges.

- Body emission tests:
  - generated body write;
  - small file write;
  - ranged file write;
  - helper-level >4GiB chunking with a fake sink;
  - no single emitted span exceeds `FileChunk` or `UINT_MAX`.

- CLI tests:
  - `--name value` and `--name=value` forms;
  - root positional handling;
  - repeated `--forward host url`;
  - numeric validation;
  - auth validation;
  - transport defaulting.

- Client parser/flush tests:
  - H1 smoke request still completes after removing extra flush;
  - H3 smoke or interop test still completes;
  - parser lifetime test by repeated connect/disconnect where practical.

- Logging tests:
  - foreground stderr sink receives `ZiLog` messages;
  - usage remains stderr;
  - daemon/syslog path does not emit normal runtime diagnostics to stderr.

Commands:

- `make -j zhttp/example/zhttpserver zhttp/example/zhttpclient`
- `make -j zhttp/test/ZhttpStaticServerTest`
- `./zhttp/test/ZhttpStaticServerTest`
- `make -j zhttp/test/Zhttp3InteropTest` if QUIC/TLS dependencies are enabled
- `./zt/test/ZtCLITest` if `ZtCLI` behavior is touched
- `make -j` before final integration if the local configuration is complete

## Acceptance Criteria

- Static file serving cannot serve a final symlink target outside the configured
  root on POSIX when not chrooted.
- File metadata used for `Content-Length`, `Last-Modified`, range handling, and
  sending comes from the same authorized file handle.
- A response body larger than 4GiB is emitted in bounded chunks without
  truncating `fileLength` to `unsigned`.
- Existing documented example behavior remains intact for normal files,
  directories, listings, ranges, HEAD, auth, redirects, chroot, single-file
  mode, HTTPS, and H3.
- Repeated magic limits are replaced with named constants.
- `zhttpserver` option parsing is structured through `ZtCLI` where feasible and
  preserves existing command spellings.
- `zhttpclient` no longer owns the response parser with raw `new`/`delete`.
- Runtime diagnostics use `ZiLog` after logging initialization.
- The stale client flush FIXME is removed.
- Added tests fail on the old symlink/chunking behavior and pass with the new
  implementation.

## Non-goals

- Backward compatibility for internal APIs is not required; dependent code in
  this repository should be updated directly.
- No broad rewrite of `Ztcp`, `Ztls`, `Zquic`, or `Zhttp` parser/builder
  internals.
- No new top-level test runner.
- No attempt to make Windows file traversal exactly equivalent to POSIX
  `openat()` if the platform cannot provide those semantics cleanly in this
  pass.
- No change to HTTP semantics beyond fixing identified unsafe/incorrect
  behavior.
- No performance micro-optimization beyond removing obvious heap/string churn
  and fixed-limit hazards identified in the findings.

## Options and Open Questions

Resolved decisions:

- The TOCTOU fix should be handle-backed and fd-relative; pathname
  revalidation/reopen is not acceptable for regular-file serving.
- If mmap cannot operate on the authorized handle, disable mmap for static file
  responses until a handle-backed mmap API exists.
- Directory listing cleanup can follow the security fix; it must not delay safe
  regular-file serving.
- `--forward host url` compatibility is more important than a pure `ZtCLI`
  conversion. Use a custom CLI format only if it stays simple; otherwise keep a
  narrow compatibility pre-scan.

Implementation options:

- `ZiFile` mmap support:
  - Option A: add `ZiMMapFile::mmap(ZiFile &&file, Offset length, ...)` and
    keep mmap fast path handle-backed.
  - Option B: stream static responses with `pread()` only and defer
    handle-backed mmap. This is simpler and safer for the example; choose it if
    the mmap API grows beyond a small addition.

- Windows no-follow behavior:
  - Option A: best-effort no-follow using reparse-point-aware `CreateFile()`
    flags and explicit metadata checks.
  - Option B: compile the API on Windows but keep POSIX-only symlink security
    guarantees documented in tests. Choose this if exact Windows behavior
    becomes disproportionately complex.

- MIME hash:
  - Option A: `ZmHashKV` with normalized lowercase `ZtString<>` keys.
  - Option B: `ZmLHashKV` if the map is small and initialized once. Prefer the
    simplest Z container that preserves override behavior and avoids STL churn.

No blocking open questions remain. The only conditional choices above are
implementation tradeoffs to resolve locally while coding, not requirement
ambiguities.
