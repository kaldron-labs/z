# QUIC Interop Runner Implementation Plan

## Objective

Implement a standard QUIC Interop Runner endpoint for `zhttp`/`zquic` under
`zhttp/interop`.

This is not a new benchmark protocol. It is a Docker-compatible endpoint for
the upstream QUIC Interop Runner:

- `https://github.com/quic-interop/quic-interop-runner`
- `https://raw.githubusercontent.com/quic-interop/quic-interop-runner/master/quic.md`

The endpoint must run in both roles:

- server: serve `/www` over HTTP/3 on UDP port `443`
- client: download `REQUESTS` URLs over HTTP/3 into `/downloads`

Unsupported upstream test cases must exit with status `127`.

Important protocol split:

- The upstream `http3` testcase uses HTTP/3.
- The upstream transport-oriented cases such as `handshake`, `transfer`,
  `rebind-port`, and `rebind-addr` use HTTP/0.9-style QUIC interop transfer
  with ALPN `hq-interop`.

The implementation must therefore include both:

- an HTTP/3 path backed by `zhttp`/`zhttpd`
- a bench-local `hq-interop` path backed directly by `zquic`

## Build Layout

Create this directory:

```text
zhttp/interop/
```

Add these files:

```text
zhttp/interop/Makefile.am
zhttp/interop/ZhttpQIR.cc
zhttp/interop/ZhttpQIR.hh
zhttp/interop/ZhttpQIRTest.cc
zhttp/interop/ZhttpQIRSmokeTest.cc
zhttp/interop/ZhttpdRun.cc
zhttp/interop/zhttpqir.cc
zhttp/interop/qir_endpoint.sh
zhttp/interop/Dockerfile.interop
zhttp/interop/README.md
```

Use `QIR` in file/type names for the QUIC Interop Runner adapter. Do not call
the binary `zhttpinterop`; use `zhttpqir` so the executable name is short and
specific.

Add top-level lifecycle scripts:

```text
scripts/qir-build-image
scripts/qir-upload-image
scripts/qir-run-local
scripts/qir-collect-results
scripts/qir-report
```

## Automake Wiring

Update `zhttp/Makefile.am` exactly to include `bench`:

```make
METASOURCES = AUTO
SUBDIRS = src test interop

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C interop test
```

Update `configure.ac` by adding `zhttp/interop/Makefile` to the existing
`AC_CONFIG_FILES` list next to the other `zhttp` makefiles:

```m4
zhttp/Makefile zhttp/src/Makefile zhttp/test/Makefile \
zhttp/interop/Makefile zhttp/example/Makefile
```

Use this `zhttp/interop/Makefile.am` skeleton:

```make
METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zquic/test \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zhttp/test \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zhttpqir ZhttpQIRTest ZhttpQIRSmokeTest
zhttpqir_SOURCES = zhttpqir.cc ZhttpQIR.cc ZhttpdRun.cc
ZhttpQIRTest_SOURCES = ZhttpQIRTest.cc ZhttpQIR.cc ZhttpdRun.cc
ZhttpQIRSmokeTest_SOURCES = ZhttpQIRSmokeTest.cc ZhttpQIR.cc ZhttpdRun.cc
noinst_HEADERS = ZhttpQIR.hh
dist_noinst_SCRIPTS = qir_endpoint.sh
EXTRA_DIST = Dockerfile.interop README.md qir-script-test.sh

.PHONY: test
test: $(noinst_PROGRAMS)
	LSAN_OPTIONS="$${LSAN_OPTIONS:+$$LSAN_OPTIONS:}suppressions=$(top_builddir)/.lsan-suppressions" \
	prove -j1 ZhttpQIRTest ZhttpQIRSmokeTest

.PHONY: qir-stage
qir-stage: zhttpqir
	test -n "$$ZHTTP_QIR_ROOT"
	$(MKDIR_P) "$$ZHTTP_QIR_ROOT$(prefix)/bin"
	$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) zhttpqir \
	  "$$ZHTTP_QIR_ROOT$(prefix)/bin/zhttpqir"
	$(INSTALL_SCRIPT) "$(srcdir)/qir_endpoint.sh" \
	  "$$ZHTTP_QIR_ROOT$(prefix)/bin/qir_endpoint.sh"
```

The `zhttp/test` include path is allowed because the first implementation will
reuse `Zhttpd.hh` static-file serving code instead of duplicating it. Do not
list `../test/zhttpd.cc` directly in `*_SOURCES`; use `ZhttpdRun.cc` as a
bench-local wrapper that defines `ZHTTPD_NO_MAIN` and includes
`../test/zhttpd.cc`. Listing the out-of-directory source directly can make
Automake write `zhttp/test/zhttpd.o` without `main`, breaking the standalone
`zhttpd` build.

Update the top-level `Makefile.am` with the canonical image target:

```make
EXTRA_DIST = scripts/qir-build-image scripts/qir-upload-image \
	scripts/qir-run-local scripts/qir-collect-results scripts/qir-report

ZHTTP_QIR_INSTALL_DIRS = zu/src zm/src zt/src ze/src zi/src ztls/src \
	zquic/src zhttp/src

.PHONY: zhttp-qir-build
zhttp-qir-build:
	$(MAKE) $(AM_MAKEFLAGS) -C zu/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zm/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zt/src all
	$(MAKE) $(AM_MAKEFLAGS) -C ze/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zi/src all
	$(MAKE) $(AM_MAKEFLAGS) -C ztls/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zquic/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zhttp/src all
	$(MAKE) $(AM_MAKEFLAGS) -C zhttp/interop zhttpqir

.PHONY: zhttp-qir-image
zhttp-qir-image: zhttp-qir-build
	test "$(prefix)" = "/usr/local"
	rm -rf "$(abs_builddir)/zhttp/interop/qir-root"
	@for subdir in $(ZHTTP_QIR_INSTALL_DIRS); do \
	  $(MAKE) $(AM_MAKEFLAGS) -C $$subdir install \
	    DESTDIR="$(abs_builddir)/zhttp/interop/qir-root" || exit $$?; \
	done
	$(MAKE) $(AM_MAKEFLAGS) -C zhttp/interop qir-stage \
	  ZHTTP_QIR_ROOT="$(abs_builddir)/zhttp/interop/qir-root"
	"$(abs_srcdir)/scripts/qir-build-image" \
	  --root "$(abs_builddir)/zhttp/interop/qir-root" \
	  --tag "$${ZHTTP_QIR_IMAGE:-zhttp-qir:local}" \
	  --no-configure --no-clean --allow-dirty
```

This target must be the documented way to create the Docker image from this
repository. It intentionally builds and stages only the dependency closure
needed by `zhttpqir`, not every enabled test/example binary. It still uses
normal autotools `make install DESTDIR=...` staging for the dependency
libraries, so the image contains the same installed Z libraries that a normal
install would provide:

- `libZu`
- `libZm`
- `libZt`
- `libZe`
- `libZi`
- `libZtls`
- `libZquic`
- `libZhttp`

`zhttpqir` is `noinst`, so `qir-stage` installs it into the staged image root
explicitly after the dependency libraries have been staged by top-level
`make install`.

The top-level image target must require `prefix=/usr/local`. If the source tree
is configured with `/opt/z` or another developer prefix, reconfigure before
building the image:

```sh
./z.config -c -Q -L /usr/local
make clean
make -j8 zhttp-qir-image
```

Do not make `zhttp-qir-image` part of `all`, `install`, `check`, or `test`.
Docker is an external packaging step.

## Executable Contract

`zhttpqir` must implement this command line:

```text
zhttpqir server
zhttpqir client
zhttpqir --help
```

Exit codes:

- `0`: success
- `1`: endpoint error
- `2`: command-line or environment usage error
- `127`: unsupported `TESTCASE`

The executable must read these environment variables:

```text
TESTCASE
REQUESTS
SSLKEYLOGFILE
```

It must use these fixed paths:

```text
/www
/downloads
/certs/ca.pem
/certs/priv.key
/certs/cert.pem
```

For non-Docker smoke tests, add optional path override environment variables:

```text
ZHTTP_QIR_WWW
ZHTTP_QIR_DOWNLOADS
ZHTTP_QIR_CA
ZHTTP_QIR_CERT
ZHTTP_QIR_KEY
ZHTTP_QIR_PORT
```

Defaults remain the upstream paths and port `443`. The upstream Docker
entrypoint must not set the override variables.

## Entrypoint Script

`qir_endpoint.sh` must be a thin `exec` wrapper. It must accept either an
explicit `server`/`client` argument or the upstream runner's `ROLE`
environment variable when no argument is supplied:

```sh
#!/bin/sh
set -eu

role=${1:-${ROLE:-}}

case "$role" in
  server)
    exec /usr/local/bin/zhttpqir server
    ;;
  client)
    exec /usr/local/bin/zhttpqir client
    ;;
  *)
    echo "qir_endpoint.sh: expected server or client" >&2
    exit 2
    ;;
esac
```

Do not parse `REQUESTS`, `TESTCASE`, paths, or certificates in shell. All
endpoint behavior belongs in C++.

## C++ Structure

`zhttpqir.cc` contains only:

- `main`
- top-level command dispatch
- conversion of uncaught endpoint errors into exit status `1` or `2`

`ZhttpQIR.hh` declares:

```text
namespace Zhttp::QIR {
  enum Role { Client, Server };
  enum Case { Handshake, Transfer, HTTP3, RebindPort, RebindAddr,
    ConnectionMigration, Unsupported };

  struct Env;
  struct Request;

  int run(Role);
}
```

Use project naming style in the actual code. Keep data members public only for
plain structs. Do not introduce virtual interfaces.

`ZhttpQIR.cc` implements:

- environment loading
- testcase parsing
- path mapping
- server startup
- client downloads
- local smoke-test helpers if needed

Keep all fixed paths and supported-case tables near the top of `ZhttpQIR.cc` as
named constants. Do not scatter literals such as `/downloads` or `443`.

## Required zhttp/zhttpd Changes

Do not extend `zhttp` or `zhttpd` for HTTP/0.9. The `hq-interop` protocol is
specific to QUIC transport interop testing and belongs in `zhttp/interop` using
direct `zquic` APIs. It is not HTTP/1.1 over QUIC, and it is not HTTP/3.

Add these `zhttp` client features only if the existing `Zhttp` client API cannot
already express them for `zhttpqir`:

- explicit output path per request, not only one `-o` base expanded as
  `.0`, `.1`, ...
- multiple distinct URLs in one client process
- concurrent HTTP/3 requests sharing the same multiplexer and connection
  policy
- response completion callback that reports status and file-write failure per
  request
- key-log propagation from `SSLKEYLOGFILE` without relying on the CLI parser

Do not add a `zhttp --qir` mode. If reusable client support is missing, add it
as library/test helper code that `zhttpqir` can call directly. The endpoint must
not shell out to the `zhttp` CLI for each URL.

`zhttpd` already has the command-line features needed for the HTTP/3 server
case:

- `--http3`
- `--addr`
- `--port`
- `--cert`
- `--key`
- `--key-log`
- `--no-listing`
- `--no-server-id`
- `--log`

If `zhttpqir` reuses `Zhttpd.hh` directly, do not add new `zhttpd` CLI options
for the initial endpoint. If reuse requires moving code, move reusable static
file server pieces out of `zhttp/test/Zhttpd.hh` into a shared header instead
of adding a test-only include dependency to production code.

If local debugging later needs to run the regular `zhttpd` binary as a QIR
server, add exactly one option:

```text
--qir-http3
```

`--qir-http3` must be shorthand for:

```text
--http3 --addr 0.0.0.0 --port 443 --no-listing --no-server-id --log /dev/null
```

It must still require explicit `--cert` and `--key`, and it must not enable
HTTP/1.1-over-TCP or HTTPS/H1-over-TLS listeners.

## Relevant Z Framework Components

Use the existing framework facilities below. Do not add STL equivalents,
ad-hoc buffers, ad-hoc path helpers, or manual time arithmetic where these
components already solve the problem.

### `zu`

- `ZuSpan`, `ZuCSpan`, and `ZuBSpan`: use for zero-copy request lines, URL
  path slices, stream payload slices, and file write spans. Do not convert to
  temporary contiguous STL strings just to parse.
- `ZuTokenizer`: use `ZuTokenizer::WhiteSpace` for splitting `REQUESTS` and
  `ZuTokenizer::Delimited<'/'>` for path component validation. It is zero-copy
  and intentionally suitable for HTTP paths and header-like values.
- `ZuTime`: use for every endpoint timeout and interval. Use `ZuTime{sec, nsec}`
  or `ZuTime{seconds}` as appropriate; do not introduce `timeUS`,
  millisecond-to-nanosecond multiplication, or raw `timespec` arithmetic.
- `ZuBox`: use for scanning numeric environment overrides such as
  `ZHTTP_QIR_PORT` and for concise numeric formatting in diagnostics.
- `ZuPrint`, `ZuFmt`, and stream insertion: use for status/error text and
  generated request lines. Avoid `std::ostringstream`.
- `ZuTuple`: use where small multi-value async completions are needed,
  especially with `ZmBlock`.
- `ZuGuard`: use for simple scope cleanup when a full object wrapper would be
  excessive, for example cancelling a partially initialized local smoke server.
- `ZuDerive` and `ZuStringT`: use for concise derived array/string types and
  heap IDs, following existing `zhttp` and `zquic` local type patterns.

### `zt`

- `ZtString`: use for mutable endpoint strings, paths before conversion to
  `Zi::Path`, generated request lines, and error text that must outlive the
  current expression.
- `ZtArray`: use for dynamic request lists, request state arrays, and stream
  state arrays. Give arrays explicit `ZtArrayHeapID` names.
- `ZtBuiltin`: use for request lists or small buffers with a known common-case
  size and heap fallback. This is appropriate for the common small `REQUESTS`
  set.
- `ZtLocalArray` and `ZtLocalString`: use for hot-path scratch buffers such as
  request-line construction and path-component scratch. Prefer these over heap
  allocation when the buffer does not escape the stack frame.
- `ZtCLI` and `ZtStruct`: use for the `zhttpqir` command line and for any new
  `zhttp`/`zhttpd` CLI options. Follow the existing `zhttp/test/zhttp.cc`,
  `zhttp/test/zhttpd.cc`, and `zhttp/test/zhttpmatrix.cc` pattern.
- `ZtURI` and `ZuPercent`: use their percent-decoding mechanics if URL path
  percent-decoding is needed. Do not use `ZtURI`'s object/query mapper as a
  full URL parser; QIR URL mapping only needs scheme/authority/path splitting
  and path safety checks.
- `ZtEnum`: use only if run-time enum names are useful in logs or status
  output. Otherwise use the plain enum style already specified for `Role` and
  `Case`.
- `ZtJSON` and `ZtCSV`: do not use in the endpoint implementation. QIR does
  not require JSON or CSV parsing in `zhttpqir`; the external upstream runner
  implementation registry is edited in a separate runner checkout. Qlog JSON
  remains the responsibility of existing `zquic` diagnostics and must stay
  inside `ZquicLOG`/logger-thread paths.

### `zm`

- `ZmRef`, `ZmObject`, and `ZmPolymorph`: use for all asynchronous request,
  stream, link, and server state that can outlive the initiating stack frame.
  Do not store intrusive objects by value in containers.
- `ZmFn`: use for callbacks into `zquic`, `zhttp`, timers, and completion
  continuations. Prefer built-in context capture forms over heap-allocating
  lambda captures when the callback only needs `this` or a `ZmRef`.
- `ZmScheduler` through `ZiMultiplex`: use the multiplexer-owned scheduler for
  timers and cross-thread posts. Do not create a separate scheduler for the QIR
  endpoint.
- `ZmSemaphore`: use only for process-level smoke tests and top-level shutdown
  waits. Do not use it to make data-path callbacks synchronous.
- `ZmBlock`: use only at process boundaries, test/smoke helpers, and final
  shutdown drains, matching existing `zhttp`/`zquic` tests. Do not call it from
  Rx/Tx callbacks.
- `ZmLock`, `ZmPLock`, and `ZmGuard`: use for small shared process-control
  state such as signal/shutdown flags. Do not add locks around Rx-owned or
  Tx-owned protocol state.
- `ZmHash`, `ZmLHash`, `ZmList`, `ZmQueue`, and `ZmPQueue`: use these if the
  endpoint needs request lookup, pending stream queues, or retransmission-like
  ordering. Do not use STL containers.
- `ZmHeap`, `ZmVHeap`, `ZmAlloc`, and explicit heap IDs: use for any endpoint
  heap allocation that is not already managed by a framework container.
- `ZmTimeout` and `ZmBackoff`: use only for retry/backoff behavior. Fixed
  endpoint completion deadlines should be direct `ZmScheduler` timers with
  `ZuTime` constants.

### `ze`

- `ZeError`: use for OS and resolver errors returned by `ZiFile`, `ZiIP`, and
  lower platform helpers. Preserve and print it directly instead of flattening
  to `errno` strings early.
- `ZeString`: use for small heap-backed error/log strings that need to survive
  asynchronous logging.
- `ZiLogEvent`/`ZeException` integration: use the existing logging path for
  exceptions or asynchronous endpoint failures. Do not invent a parallel error
  reporting format.
- `Ze::errNo()` and `Ze::sockErrNo()`: use only at the immediate failing OS
  call site when a lower-level Z wrapper is not available.

### `zi`

- `ZiMultiplex`: use as the single I/O engine for the endpoint. Construct one
  multiplexer per `zhttpqir` process, start it once, and shut it down once.
- `ZiMultiplex::rxRun`/`rxInvoke` and `txRun`/`txInvoke`: use the proper shard
  dispatch through `zquic`/`zhttp` APIs. Do not read or mutate Rx-owned state
  from Tx callbacks or process threads.
- `ZiIOBuf`: use pooled I/O buffers for QUIC stream send/receive payloads and
  file-transfer chunks. Do not allocate raw byte arrays for I/O buffers.
- `ZiFile`: use for all file operations:
  - `exists`/`isdir` for environment validation
  - `absolute`, `dirname`, `leafname`, and `append` for safe path construction
  - `mkdir` for creating `/downloads` parents
  - `open`, `read`, `write`, `pread`, and `pwrite` for transfer data
  - `NoFollow` where opening served files under `/www`
- `ZiDir`: use only if directory scanning becomes necessary. QIR file serving
  should not need directory listing.
- `Zi::Path` and `Zi::Name`: use for platform-correct filesystem paths.
  Convert only at the boundary from URL path text to filesystem path.
- `ZiIP` and `ZiSockAddr`: use for local bind addresses and peer addresses.
  Avoid raw `sockaddr` manipulation in endpoint code.
- `ZiLog`: initialize logging in `zhttpqir`, set a quiet default level, and use
  `ZiLOG` for endpoint failures. Capture log lambda data by value, following
  repository logging guidance.
- `ZiDaemon`: do not use for the Docker endpoint. The interop runner owns the
  container process lifecycle.
- `ZiGlob`: not needed for QIR. Do not use globbing or completion helpers for
  fixed `/www`, `/downloads`, and `/certs` paths.

## Guidelines Alignment Requirements

Apply `GUIDELINES.md` and `zquic/GUIDELINES.md` as hard requirements for this
implementation.

C++ and file layout:

- Use GNU C++2b in the existing project style. Do not use C++ concepts,
  `requires`, anonymous namespaces, or STL containers.
- Use file-scope `static` for private translation-unit helpers.
- Add standard Z file headers, editor modelines, include guards, and direct
  includes in any new `.hh` files.
- Prefer C headers where equivalent, and prefer Z framework APIs over
  `std::filesystem`, `std::string`, `std::vector`, `std::map`,
  `std::function`, or stream builders.
- Keep names short and conventional: `rx`, `tx`, `cli`, `srv`, `cxn`, `pkt`,
  `req`, `res`, `prot`, `param`, `ack`, `ackd`, `nak`, and `nakd`. Keep names
  under the repository's 32-byte limit.
- Use trailing underscores only for thread/shard-owned internal methods such
  as `rxOpen_` or `txDrain_`.

Code shape:

- Do not add virtual interfaces. Use CRTP/templates/local helpers where
  factoring is needed.
- Do not add explicit derived-to-base casts for CRTP/app bases. Bring base
  APIs into scope with `using Base::x` and call `x()` directly.
- Do not add unnecessary casts among Z string/span/array types.
- Put all fixed paths, ports, ALPN names, timeout values, testcase tables, and
  Docker paths behind named constants near the top of the owning file.
- Use enum constants for int-sized constants. Use typed enums only where their
  names are useful in logs or CLI output.
- Initialize objects directly with aggregate or constructor initialization; do
  not default-initialize and then assign fields unless the API requires it.

Time, timers, and shutdown:

- Represent every interval with `ZuTime`; do not use `timeUS`, raw
  microsecond/nanosecond arithmetic, or hand-built `timespec` values.
- Timer names must describe the recovery or endpoint behavior they protect,
  for example `ConnectTimeout`, `TotalTimeout`, `H3QuietTimeout`, and
  `H3StallTimeout`.
- Any `ZmScheduler::Timer` added for the endpoint must follow the three-phase
  teardown pattern from the guidelines: cancel with a completion callback,
  wait for that callback during shutdown, then release the timer reference.
- Do not use sleep/poll loops in tests or smoke helpers. Use completion
  callbacks with `ZmBlock`/`ZmSemaphore` only at process/test boundaries.

Allocation and buffers:

- Avoid hidden heap allocation and hidden copies in request parsing, URL path
  mapping, stream send, and file receive paths.
- Avoid fixed-size raw arrays and unexplained capacities. When a fixed capacity
  is needed, give it a named constant and choose storage based on the expected
  QIR workload.
- Use `ZtLocalArray`/`ZtLocalString` for stack scratch, `ZtBuiltin` for small
  common-case request lists, `ZtArray` with explicit heap IDs for dynamic
  request state, and `ZmHeap`/`ZmVHeap`/`ZmAlloc` when heap allocation is
  unavoidable.
- Use pooled `ZiIOBuf` for file and stream payloads. Do not allocate raw byte
  arrays for I/O data.
- When using direct `zquic` APIs, use the role-specific buffer allocators from
  `ZquicBuf.hh`: `PktRxBufAlloc`, `PktTxBufAlloc`, `StreamTxBufAlloc`,
  `CryptoRxBufAlloc`, and `CryptoTxBufAlloc`.
- Do not treat `BufSize` as the UDP payload limit; it is a 1472-byte buffer
  capacity used by `zquic`.
- Preserve `zquic`'s Rx behavior: decrypt packet data in place, keep STREAM Rx
  payload references to the decrypted packet buffer, and avoid copying STREAM
  frame payloads into frame-owned storage. CRYPTO reassembly may copy only
  where the existing `zquic` path requires it.

Rx/Tx ownership:

- Keep Rx-owned state on the Rx shard and Tx-owned state on the Tx shard.
  Public entry points must be thin dispatchers that immediately use
  `rxRun`/`rxInvoke` or `txRun`/`txInvoke` and then call trailing-underscore
  methods on the owning shard.
- Do not add locks to make wrong-shard protocol access appear safe.
- Cross-shard posts may capture only small fixed metadata by value. Put large
  or variable-size data in destination-owned buffers/queues and capture only a
  handle plus fixed metadata.
- Do not call `ZmBlock` from Rx/Tx callbacks. It is allowed only for process
  startup/shutdown, local smoke tests, and final drains.

Diagnostics and qlog:

- The endpoint may initialize normal `ZiLog`, but diagnostic lambdas must
  capture by value only. Do not capture references, stack pointers, `this`, or
  mutable runtime objects in async log callbacks.
- Any new qlog event or qlog helper must be gated by `ZquicLOG` and compiled
  out when `Zquic_DEBUG` is off. No qlog-only expression, formatting, JSON
  construction, or buffer walk may run in release builds with qlog disabled.
- Do not use `ZquicLogger::enabled()` as a general fast path. It is allowed
  only for narrow accumulator cases that are conditionally compiled and
  constexpr-false when diagnostics are disabled.
- Keep debug text logging separate from qlog event capture. Text diagnostics
  must not cause qlog data preparation to compile or run.
- Qlog events must carry bounded metadata only. Do not log payload bytes,
  crypto material, TLS secrets, raw packet bodies, or request/response bodies.
- Qlog JSON serialization belongs on the logger thread through `ZtJSON` and
  `ZtStruct`. Do not serialize qlog JSON on Rx or Tx shards.
- Use typed qlog fields and `ZtEnumMap` for enum names where new event fields
  are required.

Build and verification:

- Verify release-image work from the top level with:

```sh
./z.config -c -Q -L /usr/local
make clean
make -j8 zhttp-qir-image
```

- Do not validate release packaging with a partial rebuild of only
  `zhttp/interop`.
- Run source-tree binaries through `libtool exec`; do not run `.libs`
  executables directly.
- Docker, registry upload, and upstream runner execution are bench lifecycle
  steps. They must not become prerequisites of `make test`.

## Supported Cases

Implement this testcase table first:

| `TESTCASE` | status | behavior |
|---|---|---|
| `handshake` | supported | `hq-interop` download of all `REQUESTS` |
| `transfer` | supported | `hq-interop` parallel download of all `REQUESTS` |
| `http3` | supported | HTTP/3 parallel download of all `REQUESTS` |
| `rebind-port` | supported | `hq-interop`; simulator changes client port |
| `rebind-addr` | supported | `hq-interop`; simulator changes client address |
| `connectionmigration` | unsupported initially | return `127` until preferred-address support is verified |
| `versionnegotiation` | unsupported | return `127` |
| `chacha20` | unsupported | return `127` |
| `keyupdate` | unsupported | return `127` |
| `retry` | unsupported | return `127` |
| `resumption` | unsupported | return `127` |
| `zerortt` | unsupported | return `127` |
| `v2` | unsupported | return `127` |

Do not advertise support for `connectionmigration` until an upstream run proves
that zquic server preferred-address behavior matches the runner's pcap checks.

`handshake`, `transfer`, `rebind-port`, and `rebind-addr` must share the same
`hq-interop` implementation. `http3` must use the existing HTTP/3 stack.

## Server Implementation

Server mode must:

1. Load `TESTCASE` and return `127` if unsupported.
2. Resolve paths from env overrides or defaults.
3. Validate:
   - www root exists
   - cert file exists
   - key file exists
4. Start a `ZiMultiplex`.
5. For `http3`, start an HTTP/3-only static file server bound to
   `0.0.0.0:port`.
6. For `hq-interop` cases, start a bench-local QUIC server using ALPN
   `hq-interop`.
7. Use cert/key paths from `/certs`.
8. Use `SSLKEYLOGFILE` for TLS key logging when set.
9. For `http3`, serve files from the www root using the `Zhttpd.hh` static-file
   path.
10. For `hq-interop`, parse one request line per stream:

```text
GET /path
```

    Then send the raw file bytes on the same stream and close the send side.

11. Disable directory listing and nonessential access logging.
12. Run until SIGTERM/SIGINT from the runner.
13. Drain and stop `ZiMultiplex` cleanly on termination.

Do not enable HTTP/1.1-over-TCP or HTTPS/H1-over-TLS listeners in this
endpoint. Those are separate TCP/TLS protocols, not HTTP/1.1 over QUIC, and
extra listeners make runner failures harder to interpret.

Use `Zhttpd.hh` directly for the static file response behavior. If that header
pulls in too much test-only logic, move the reusable static-file support into a
bench-local helper copied from `Zhttpd.hh` in the same patch. Do not link
against `zhttp/test/zhttpd.o`.

## Client Implementation

Client mode must:

1. Load `TESTCASE` and return `127` if unsupported.
2. Require `REQUESTS` to be non-empty for supported cases.
3. Parse `REQUESTS` as space-separated URLs.
4. Build one `ZiMultiplex`.
5. For `http3`, force HTTP/3 for every request.
6. For `hq-interop` cases, negotiate ALPN `hq-interop`.
7. Trust the CA bundle from `/certs/ca.pem`.
8. Use `SSLKEYLOGFILE` for TLS key logging when set.
9. Start all requests concurrently for `transfer`, `http3`, `rebind-port`, and
   `rebind-addr`.
10. Permit sequential behavior only if there is exactly one request.
11. Write each response body to its mapped file under `/downloads`.
12. Wait for all requests to complete or for a fixed endpoint timeout.
13. Return `1` if any response status is not successful or any file write
    fails.

Do not shell out to `zhttp` per URL. The C++ client must use the same underlying
client APIs as `zhttp/test/zhttp.cc` so the benchmark exercises one process,
one multiplexer, and concurrent HTTP/3 streams.

For `hq-interop`, do not use `zhttp` APIs. Implement the minimal client in
`ZhttpQIR.cc` on top of `zquic`:

- open one QUIC connection
- open one bidirectional stream per requested file
- send one HTTP/0.9-style request line on each stream:

```text
GET /path
```

- write received bytes directly to the mapped `/downloads` file
- close the connection after all streams complete

Use these endpoint timeouts initially:

```text
Connect timeout: 5s
Total timeout: 60s
H3 quiet timeout: 15s
H3 stall timeout: 30s
```

Represent these with `ZuTime`, not manual microsecond arithmetic.

## URL and File Mapping

Implement URL-to-output mapping in C++ and test it before running Docker.

Required mapping:

```text
https://server:443/a/b/file.bin -> /downloads/a/b/file.bin
https://server/a.txt            -> /downloads/a.txt
```

Rules:

- Accept only `https://` URLs.
- Ignore host and port.
- Strip query and fragment.
- Reject empty path.
- Reject absolute-path escapes.
- Reject any `..` component.
- Preserve ordinary path components exactly.
- Create parent directories under `/downloads`.

Do not use `std::filesystem`. Use existing Z string/path utilities and POSIX
directory creation where needed.

## Automated Test Coverage

Automated coverage is required for every implementation slice. Keep local unit
and smoke coverage under `zhttp/interop`; keep Docker and upstream runner checks
as explicit bench lifecycle commands.

Coverage is part of each deliverable, not a follow-up task. A slice is not
accepted until its tests fail without the implementation change, pass with the
implementation change, and are named in that slice's acceptance criteria.

Every new externally visible behavior must land with one of these coverage
types:

- pure helper tests in `ZhttpQIRTest` for parsing, mapping, constants, and
  unsupported-case decisions
- loopback/process smoke tests in `ZhttpQIRSmokeTest` for endpoint behavior
  that requires `ZiMultiplex`, sockets, files, signals, or child processes
- shell script tests for lifecycle-script argument parsing, dry-run behavior,
  collection, and report generation
- manual Docker/upstream-runner acceptance commands for image packaging and
  third-party interop that cannot be prerequisites of `make test`

Do not mark a behavior as supported in the testcase table until the matching
local automated test exists. Do not publish or upload an image until the
matching Docker and upstream-runner acceptance commands have been run and the
report has been saved.

### Coverage Matrix by Slice

| slice | automated coverage required before acceptance | external coverage required before acceptance |
|---|---|---|
| 1: build skeleton and CLI contract | `ZhttpQIRTest` covers role parsing, help, usage exits, testcase parsing, default/override env loading | none |
| 2: pure mapping and unsupported cases | `ZhttpQIRTest` covers `REQUESTS` parsing, URL mapping, path rejection, `hq-interop` request lines, named `ZuTime` constants, unsupported exit decisions | none |
| 3: HTTP/3 local endpoint | `ZhttpQIRSmokeTest` covers unsupported exit, H3 single-file, H3 multi-file, bad-path rejection, SIGTERM shutdown | none |
| 4: `hq-interop` local endpoint | `ZhttpQIRSmokeTest` covers HQ single-file, HQ multi-file, same implementation for `handshake`/`transfer`/rebinding cases | none |
| 5: Docker packaging | script syntax/help tests remain green; `make test` still excludes Docker | `make -j8 zhttp-qir-image`; Docker missing-role, unsupported-case, HTTP/3 file-transfer smoke, and `hq-interop` file-transfer smoke |
| 6: lifecycle scripts and reporting | `qir-script-test.sh` or equivalent smoke coverage covers every `scripts/qir-*` help path, bad option, dry-run, collect, and report fixture | optional dry-run only; no upstream runner required |
| 7: documentation and manual lifecycle | README command examples are covered by script dry-run or Docker smoke where possible | saved local smoke report and saved Docker smoke report |
| 8: upstream interop validation | all local automated tests still pass from a clean release rebuild | upstream runner matrix report saved for every required peer/direction/testcase |

### `ZhttpQIRTest`

`ZhttpQIRTest.cc` is a TAP-emitting `ZuTestUtil` binary. It must not create
sockets, Docker containers, or long-running timers.

Required tests:

- command role parsing:
  - `server`
  - `client`
  - `--help`
  - missing role returns usage status `2`
  - unknown role returns usage status `2`
- testcase parsing:
  - supported: `handshake`, `transfer`, `http3`, `rebind-port`,
    `rebind-addr`
  - unsupported: `retry`, `resumption`, `zerortt`, `versionnegotiation`,
    `chacha20`, `keyupdate`, `v2`, `connectionmigration`
  - missing `TESTCASE` returns usage status `2`
  - unknown `TESTCASE` returns unsupported status `127`
- environment loading:
  - default Docker paths
  - override paths for non-Docker tests
  - default port `443`
  - override port validation
  - empty `REQUESTS` rejected for supported client cases
  - empty `REQUESTS` allowed for server mode
- `REQUESTS` parsing:
  - one URL
  - multiple whitespace-separated URLs
  - repeated whitespace
  - invalid empty token handling
  - request ordering preserved
- URL-to-output mapping:
  - normal nested path
  - implicit port
  - explicit port
  - query stripped
  - fragment stripped
  - percent-encoded ordinary path component, if implemented
  - empty path rejected
  - absolute escape rejected
  - `..` component rejected
  - path ending in slash rejected unless directory output support is added
- `hq-interop` request-line handling:
  - valid `GET /path`
  - trailing CRLF accepted
  - missing path rejected
  - non-`GET` method rejected
  - path escape rejected
- timeout constants:
  - exported or locally testable constants match `5s`, `60s`, `15s`, and
    `30s`
  - constants are represented as `ZuTime`, not manual integer conversions

Expose enough pure helper functions from `ZhttpQIR.hh` for these tests. Keep
helpers bench-local and do not add public `zhttp` API unless the production
endpoint needs it.

### `ZhttpQIRSmokeTest`

`ZhttpQIRSmokeTest.cc` is a TAP-emitting non-Docker integration test. It may
open loopback UDP sockets and temporary files, but it must not require Docker,
the upstream interop runner, caddy, curl, or network access outside localhost.

Required tests:

- `unsupported-exit`: run `zhttpqir client` with `TESTCASE=retry` and assert
  exit status `127`.
- `h3-single-file`: start `zhttpqir server` with `TESTCASE=http3`, download one
  file with `zhttpqir client`, and byte-compare source and destination.
- `h3-multi-file`: download at least three files in one client process and
  verify every output path.
- `hq-single-file`: run the `hq-interop` server/client path for one file.
- `hq-multi-file`: run the `hq-interop` path with at least three concurrent
  streams.
- `bad-path`: request a URL whose path maps outside `/downloads` and assert
  the client fails without creating the escaped file.
- `shutdown`: send SIGTERM to a running server and assert it exits cleanly
  after draining `ZiMultiplex`.

Use temporary directories for www, downloads, and certs. Generate test
certificates inside the test or reuse an existing local test helper; do not
depend on files under `/certs`.

The generated certificate must include `subjectAltName` entries matching every
hostname or IP address used in `REQUESTS`. At minimum, local smoke certificates
must cover `DNS:localhost`, `IP:127.0.0.1`, and `IP:::1`. Docker smoke
certificates must also cover the Docker server name used in the URL, for
example `DNS:qir-smoke-server`. A certificate with only `/CN=localhost` is not
a valid smoke certificate because the TLS verifier rejects it before transfer
completion.

Use `ZmBlock` or `ZmSemaphore` only at process/test boundaries. Do not add
polling sleeps; coordinate startup and completion through process readiness,
callbacks, or bounded `ZuTime` timers.

### Script Coverage

The script layer must have automated coverage that can run without Docker
when Docker is unavailable:

- `sh -n` for:
  - `scripts/qir-build-image`
  - `scripts/qir-upload-image`
  - `scripts/qir-run-local`
  - `scripts/qir-collect-results`
  - `scripts/qir-report`
- `--help` for every script exits `0`.
- invalid option for every script exits non-zero.
- `scripts/qir-report` can summarize a small checked-in fixture directory
  under `zhttp/interop/testdata/qir-results/`.
- `scripts/qir-collect-results` can copy a synthetic runner directory with
  `logs/`, `logs_*`, `results/`, `results_*`, stdout, stderr, and status files
  into a temp output directory.
- `scripts/qir-build-image --root <bad-root>` fails before invoking Docker.

Add a bench-local script test only if shell assertions become too large for
`ZhttpQIRSmokeTest`. If a shell test is added, name it
`zhttp/interop/qir-script-test.sh`, include it in `EXTRA_DIST`, and invoke it
from the bench `test` target after the C++ tests.

### Docker and Runner Coverage

Docker and upstream runner checks are required before final acceptance, but
they must remain outside `make test`.

Required Docker checks:

- `make -j8 zhttp-qir-image` builds `zhttp-qir:local` from a `/usr/local` staged
  install root.
- Running the image with role `server` and `TESTCASE=retry` exits `127`.
- Running the image with missing role exits `2`.
- Running the image with `TESTCASE=http3` can serve the same single-file and
  multi-file cases as `ZhttpQIRSmokeTest`.
- Running the image with `TESTCASE=transfer` can serve the same single-file and
  multi-file `hq-interop` cases as `ZhttpQIRSmokeTest`.
- `scripts/qir-build-image --root zhttp/interop/qir-root --tag zhttp-qir:local`
  can rebuild from an existing staged root.

Docker smoke setup must create the test certificate with SAN entries for the
request hostnames. When using a Docker bridge network and URLs such as
`https://qir-smoke-server:9443/...`, include `DNS:qir-smoke-server`. When using
host networking and URLs such as `https://127.0.0.1:9443/...`, include
`IP:127.0.0.1`. Do not diagnose CN-only certificate failures as Docker sandbox
or UDP failures.

Required upstream runner checks:

- `scripts/qir-run-local` runs `zquic` as server and client against `ngtcp2`
  for `handshake`, `transfer`, and `http3`.
- After `ngtcp2` passes, the same core cases run against `quic-go`, `quiche`,
  and `msquic` where local runner images are available.
- Rebinding cases run only after core cases pass:
  - `rebind-port`
  - `rebind-addr`
- `scripts/qir-collect-results` preserves logs before each runner rerun.
- `scripts/qir-report` produces a markdown report that includes image digest,
  z revision, runner revision, host facts, and pass/fail rows.

## Dockerfile

`Dockerfile.interop` must produce an image that contains:

```text
/usr/local/bin/zhttpqir
/usr/local/bin/qir_endpoint.sh
```

The Docker image install prefix is fixed at:

```text
/usr/local
```

Use `/usr/local` because the endpoint entrypoint executes
`/usr/local/bin/zhttpqir`, the support script is installed as
`/usr/local/bin/qir_endpoint.sh`, and installed shared libraries are expected
under `/usr/local/lib`.

For interop builds that need qlog and QUIC diagnostics, configure the image
build as a release build with diagnostics:

```sh
./z.config -Q -L /usr/local
```

Do not use `/opt/z` inside the Docker image unless the Dockerfile is changed to
copy that prefix and either install wrapper scripts under `/usr/local/bin` or
change the entrypoint paths. `/opt/z` remains fine for host-side developer
builds.

Use a runtime-only Dockerfile that copies a staged autotools install tree:

```dockerfile
FROM <runtime-base>
ARG ZHTTP_QIR_ROOT=qir-root
COPY ${ZHTTP_QIR_ROOT}/usr/local /usr/local
RUN apt-get update && apt-get install -y --no-install-recommends \
    ethtool iproute2 net-tools netcat-openbsd \
  && rm -rf /var/lib/apt/lists/* \
  && echo /usr/local/lib >/etc/ld.so.conf.d/z.conf && ldconfig \
  && mkdir -p /logs/qlog
```

The networking tools are runtime dependencies for the upstream simulator
topology. `qir_endpoint.sh` must use them to disable checksum offload and route
`193.167.0.0/16` plus `fd00:cafe:cafe::/48` through the simulator only when it
detects the upstream runner's `193.167.*` container addresses. Local Docker
smoke networks must not have their routes rewritten.

The Dockerfile must not run `./z.config`, `make`, or `make install`. The build
has already happened through the top-level automake target, which is the only
place that knows the complete dependency graph and configured build type.

Expose UDP port `443`:

```dockerfile
EXPOSE 443/udp
ENTRYPOINT ["/usr/local/bin/qir_endpoint.sh"]
```

Direct build command for debugging only:

```sh
docker build -f zhttp/interop/Dockerfile.interop \
  --build-arg ZHTTP_QIR_ROOT=zhttp/interop/qir-root \
  -t zhttp-qir:local .
```

Do not make Docker mandatory for `make test`. Docker lifecycle scripts are
manual bench tools.

## Docker Image Lifecycle Scripts

Add executable scripts under top-level `scripts/`. They must be plain POSIX
shell scripts, use `set -eu`, print the commands they run, and exit non-zero on
any failure. Keep them dependency-light: `sh`, `git`, `docker`, and optionally
`docker buildx`. Shell is orchestration only; it must not parse QUIC, HTTP, or
runner result formats beyond copying files and invoking tools.

### `scripts/qir-build-image`

Purpose: build the local endpoint image.

Usage:

```text
scripts/qir-build-image --root ROOT [--tag TAG] [--platform PLATFORM] [--push]
```

Defaults:

```text
TAG=zhttp-qir:local
PLATFORM unset, using Docker's local host default
```

Required behavior:

- Run from any working directory by resolving the repository root from the
  script path.
- Require `--root` and verify that it contains:
  - `usr/local/bin/zhttpqir`
  - `usr/local/bin/qir_endpoint.sh`
  - `usr/local/lib`
- Create a temporary Docker build context containing:
  - `Dockerfile.interop`
  - `qir-root/`, copied or linked from `--root`
  Docker `COPY` cannot read arbitrary absolute paths outside the build
  context, so do not point `docker build` at the repository root unless the
  staged root is inside that context.
- Refuse a dirty source tree unless `ZHTTP_QIR_ALLOW_DIRTY=1` is set.
- Add OCI labels:
  - `org.opencontainers.image.source`
  - `org.opencontainers.image.revision`
  - `org.opencontainers.image.created`
  - `org.opencontainers.image.title=zhttp-qir`
- Use `docker buildx build` when `--platform` or `--push` is present.
- Use plain `docker build` for the default local build.
- Pass the git revision as `ZHTTP_QIR_REV` build arg.
- Pass `qir-root` as Docker build arg `ZHTTP_QIR_ROOT` inside the temporary
  build context.

Required examples:

```sh
make -j8 zhttp-qir-image
ZHTTP_QIR_IMAGE=ghcr.io/<org>/zhttp-qir:$(git rev-parse --short HEAD) \
  make -j8 zhttp-qir-image
scripts/qir-build-image --root zhttp/interop/qir-root \
  --platform linux/amd64,linux/arm64 \
  --tag ghcr.io/<org>/zhttp-qir:$(git rev-parse --short HEAD) --push
```

### `scripts/qir-upload-image`

Purpose: tag and push an already-built local image.

Usage:

```text
scripts/qir-upload-image --source SOURCE_TAG --target TARGET_TAG
```

Required behavior:

- Verify `docker image inspect SOURCE_TAG` succeeds before tagging.
- Run `docker tag SOURCE_TAG TARGET_TAG`.
- Run `docker push TARGET_TAG`.
- Print the pushed image digest using `docker image inspect` when available.

This script is for registries such as Docker Hub or GHCR. It must not assume a
specific registry host.

### `scripts/qir-run-local`

Purpose: run an external upstream `quic-interop-runner` checkout using a local
or published `zquic` image.

Usage:

```text
scripts/qir-run-local --runner PATH --image IMAGE [OPTION]...
```

Options:

```text
--peers LIST             comma-separated peer implementations
--url URL                source URL for temporary runner registration
--tests LIST             comma-separated tests
--build-type TYPE        release/debug/asan label written to metadata
--out DIR                copy logs and generated report here
--keep-logs              do not delete existing runner logs before run
--python PYTHON          default python3
```

Defaults:

```text
tests=handshake,transfer,http3
peers=ngtcp2,quic-go,quiche,msquic
build-type=unspecified
out=zhttp/interop/qir-results/<timestamp>-<short-rev>
```

Required behavior:

- Do not vendor or modify the upstream runner permanently.
- Write a temporary implementation config or temporary patch in the runner
  checkout that registers:

```json
"zquic": {
  "image": "IMAGE",
  "url": "https://github.com/<final-upstream>/z",
  "role": "both"
}
```

- Run both directions for each peer:

```sh
python3 run.py -s zquic -c <peer> -t <tests>
python3 run.py -s <peer> -c zquic -t <tests>
```

- Copy `logs/`, `logs_*`, `results/`, and `results_*` from the runner checkout
  after every run into the output directory before starting the next run,
  because the upstream runner may overwrite or rotate logs by timestamp.
- Add a runner-local Python compatibility shim if needed so pyshark can parse
  pcaps on Python versions where `asyncio.get_event_loop()` no longer creates
  the main-thread event loop implicitly.
- Save:
  - exact command lines
  - image tag and digest
  - git revision
  - runner revision
  - start/end timestamps
  - stdout/stderr for each run
  - copied runner logs and pcaps

### `scripts/qir-collect-results`

Purpose: copy one runner invocation's artifacts into a stable output directory.

Usage:

```text
scripts/qir-collect-results --runner PATH --out DIR [OPTION]...
```

Options:

```text
--name NAME        run directory name, default timestamp
--stdout FILE     stdout file to copy
--stderr FILE     stderr file to copy
--status STATUS   command exit status to record
```

Required behavior:

- Copy `logs/`, `logs_*`, `results/`, and `results_*` from the runner checkout
  when present.
- Preserve stdout, stderr, exit status, runner revision, and collection time.
- Never delete prior output directories.

### `scripts/qir-report`

Purpose: summarize copied interop-runner logs into a markdown report.

Usage:

```text
scripts/qir-report --in DIR --out REPORT.md
```

Required report contents:

- image tag and digest
- z repository revision
- runner revision
- build type: release/debug/asan, supplied manually or inferred from
  `z.config` output when practical
- host kernel, CPU model, and Docker version
- matrix table by server, client, and testcase
- pass/fail/unsupported status
- paths to failed logs and pcaps
- benchmark/measurement rows if the runner produces measurement data

Do not claim comparative performance from one run without labelling it as a
single-run result.

## Manual Registry and Upstream Registration

Publishing and upstream registration are intentionally manual steps.

Manual image publication:

1. Choose a registry and repository, for example:

```text
ghcr.io/<org>/zhttp-qir
docker.io/<org>/zhttp-qir
```

2. Authenticate with the registry using `docker login`.
3. Configure the repository for the image prefix and build the staged image:

```sh
./z.config -c -Q -L /usr/local
make clean
ZHTTP_QIR_IMAGE=ghcr.io/<org>/zhttp-qir:<git-short-rev> \
  make -j8 zhttp-qir-image
```

4. Push the validated revision tag, or buildx-push a multi-platform tag from
   the staged root:

```sh
scripts/qir-upload-image \
  --source ghcr.io/<org>/zhttp-qir:<git-short-rev> \
  --target ghcr.io/<org>/zhttp-qir:<git-short-rev>

scripts/qir-build-image --root zhttp/interop/qir-root \
  --platform linux/amd64,linux/arm64 \
  --tag ghcr.io/<org>/zhttp-qir:<git-short-rev> --push
```

5. After the image has passed the interop matrix, optionally tag the same image
   as `latest` or another stable release tag using
   `scripts/qir-upload-image`.

Manual local runner validation:

1. Clone the upstream runner outside this repo.
2. Install the runner prerequisites from its README.
3. Run `scripts/qir-run-local` against that checkout. The script must
   temporarily add or update the local `zquic` implementation entry in
   `implementations_quic.json`, then restore the runner file before exit.
4. Preserve the generated `zhttp/interop/qir-results/...` directory with the
   tested image digest.

Manual upstream registration:

1. Publish a stable public image for `linux/amd64`; multi-platform
   `linux/amd64,linux/arm64` is preferred.
2. Confirm the public image passes at least:

```text
ngtcp2 <-> zquic: handshake,transfer,http3
quic-go <-> zquic: handshake,transfer,http3
quiche <-> zquic: handshake,transfer,http3
```

3. Prepare an upstream runner PR adding `zquic` to the QUIC implementations
   registry with:
   - image name
   - project URL
   - role `both`
   - maintainer/contact if the upstream schema requires it
4. Include the latest local report and image digest in the PR description.
5. Do not add credentials, private registry names, or local paths to this repo
   or to the upstream PR.

## Full-Lifecycle Benchmark Procedure

Use this procedure for repeatable release interop benchmarking:

1. Start from a release-configured tree using the Docker image prefix:

```sh
./z.config -Q -L /usr/local
make clean
make -j8 zhttp-qir-image
```

Use `/opt/z` only for host-side developer builds that will not invoke
`make -j8 zhttp-qir-image`.

2. Commit or explicitly allow a dirty tree:

```sh
git status --short
```

3. Build the local image:

```sh
ZHTTP_QIR_IMAGE=zhttp-qir:$(git rev-parse --short HEAD) \
  make -j8 zhttp-qir-image
```

4. Run local smoke tests from `zhttp/interop/README.md`.
5. Run upstream core interop:

```sh
scripts/qir-run-local --runner /path/to/quic-interop-runner \
  --image zhttp-qir:$(git rev-parse --short HEAD) \
  --url https://github.com/<final-upstream>/z \
  --tests handshake,transfer,http3
```

6. Run rebinding after core interop is clean:

```sh
scripts/qir-run-local --runner /path/to/quic-interop-runner \
  --image zhttp-qir:$(git rev-parse --short HEAD) \
  --url https://github.com/<final-upstream>/z \
  --tests rebind-port,rebind-addr
```

7. Generate a report:

```sh
scripts/qir-report --in zhttp/interop/qir-results/<run-dir> \
  --out zhttp/interop/qir-results/<run-dir>/report.md
```

8. If publishing, push the exact tested image digest or tag. Do not rebuild
   between validation and publication unless the new image is revalidated.

Debug, ASAN, and development images must be tagged distinctly:

```text
zhttp-qir:<rev>-debug
zhttp-qir:<rev>-asan
zhttp-qir:<rev>-dirty
```

Never compare debug/ASAN results against release results without labelling the
build type in the report.

## Local Smoke Tests

Add smoke-test instructions to `zhttp/interop/README.md`, not to automake `test`.
Docker and the upstream runner are external dependencies, so they must not be
required by `make test`.

Required non-Docker smoke sequence:

1. Create temp dirs for www, downloads, and certs.
2. Generate a local cert/key pair with SAN entries for `localhost`,
   `127.0.0.1`, and `::1`.
3. Copy or generate the trust anchor used by the client:

```sh
cp $tmp/certs/cert.pem $tmp/certs/ca.pem
```
4. Start:

```sh
TESTCASE=http3 \
ZHTTP_QIR_WWW=$tmp/www \
ZHTTP_QIR_DOWNLOADS=$tmp/downloads \
ZHTTP_QIR_CERT=$tmp/certs/cert.pem \
ZHTTP_QIR_KEY=$tmp/certs/priv.key \
ZHTTP_QIR_PORT=9443 \
libtool exec ./zhttp/interop/zhttpqir server
```

5. Run:

```sh
TESTCASE=http3 \
REQUESTS=https://server:9443/file.bin \
ZHTTP_QIR_DOWNLOADS=$tmp/downloads \
ZHTTP_QIR_CA=$tmp/certs/ca.pem \
ZHTTP_QIR_PORT=9443 \
libtool exec ./zhttp/interop/zhttpqir client
```

6. Compare `$tmp/www/file.bin` and `$tmp/downloads/file.bin`.
7. Verify `TESTCASE=retry` exits `127`.

Required Docker smoke sequence:

1. Create temp dirs for www, downloads, and certs.
2. Generate a local cert/key pair with SAN entries for `localhost`,
   `127.0.0.1`, `::1`, and the Docker server container name used in
   `REQUESTS`.
3. Create a private Docker network.
4. Start `zhttp-qir:local server` on that network with `TESTCASE=http3`.
5. Run `zhttp-qir:local client` on that network with at least two URLs under
   the Docker server name and compare downloads to source files.
6. Repeat steps 4 and 5 with `TESTCASE=transfer`.
7. Verify missing-role exits `2` and `TESTCASE=retry` exits `127` in the image.

## Upstream Runner Verification

In an external `quic-interop-runner` checkout, add a temporary local
implementation entry for the image:

```json
"zquic": {
  "image": "zhttp-qir:local",
  "url": "https://github.com/<final-upstream>/z",
  "role": "both"
}
```

Run these first:

```sh
python3 run.py -s zquic -c ngtcp2 -t handshake,transfer,http3
python3 run.py -s ngtcp2 -c zquic -t handshake,transfer,http3
```

Then expand:

```sh
python3 run.py -s zquic -c quic-go -t handshake,transfer,http3
python3 run.py -s quic-go -c zquic -t handshake,transfer,http3
python3 run.py -s zquic -c quiche -t handshake,transfer,http3
python3 run.py -s quiche -c zquic -t handshake,transfer,http3
```

Run rebinding only after the core cases pass:

```sh
python3 run.py -s zquic -c ngtcp2 -t rebind-port,rebind-addr
python3 run.py -s ngtcp2 -c zquic -t rebind-port,rebind-addr
```

Preserve failed runner logs and pcaps from `logs/<server>_<client>/<testcase>/`
before rerunning, because the runner overwrites logs on each run.

For full-lifecycle runs, prefer `scripts/qir-run-local` over manually invoking
`python3 run.py`, because the script captures the image digest, runner
revision, copied logs, pcaps, stdout/stderr, and command lines.

## Delivery Slices and Acceptance Criteria

Implement the runner in the slices below. Do not move to a later slice while
the current slice has failing required tests.

Each slice has three required outputs:

- code/config/script changes listed under `Deliverables`
- automated tests that exercise the delivered behavior
- acceptance evidence from the commands listed in that slice

Record each slice's evidence before moving on. For local automated work, the
evidence is the exact command and pass/fail result. For external Docker or
upstream-runner work, the evidence is the command, image tag, image digest when
available, output directory, and report path.

If a slice changes behavior already covered by an earlier slice, update the
earlier slice's test before treating the later slice as complete. If an
external prerequisite is unavailable, record the skipped command and reason in
the report; do not convert the missing external check into a passing result.

### Slice 1: Build Skeleton and CLI Contract

Deliverables:

- `zhttp/interop/Makefile.am`
- `zhttp/interop/ZhttpQIR.hh`
- `zhttp/interop/ZhttpQIR.cc`
- `zhttp/interop/zhttpqir.cc`
- `zhttp/interop/ZhttpQIRTest.cc`
- `zhttp/Makefile.am` updated to run `interop test`
- `configure.ac` updated with `zhttp/interop/Makefile`

Automated tests:

- `ZhttpQIRTest` role parsing
- `ZhttpQIRTest` help/usage status
- `ZhttpQIRTest` testcase parsing
- `ZhttpQIRTest` default and override environment loading

Acceptance criteria:

- `./z.config -c -Q -L /usr/local` regenerates build files.
- `make -j8 -C zhttp/interop zhttpqir ZhttpQIRTest` succeeds.
- `libtool exec ./zhttp/interop/zhttpqir --help` exits `0` and documents only
  `server`, `client`, and `--help`.
- Missing or unknown role exits `2`.
- `ZhttpQIRTest` passes role parsing, testcase parsing, and default/override
  environment tests.
- `make -C zhttp/interop test` runs `ZhttpQIRTest` through `prove`.

### Slice 2: Pure Mapping and Unsupported Cases

Deliverables:

- request-list parser
- URL-to-output path mapper
- `hq-interop` request-line parser
- unsupported testcase handling
- named `ZuTime` endpoint constants

Automated tests:

- `ZhttpQIRTest` request-list parser coverage
- `ZhttpQIRTest` URL-to-output mapping coverage
- `ZhttpQIRTest` `hq-interop` request-line parsing coverage
- `ZhttpQIRTest` unsupported-case and empty-`REQUESTS` coverage
- `ZhttpQIRTest` timeout constant coverage

Acceptance criteria:

- `ZhttpQIRTest` passes all request parsing, URL mapping, request-line, and
  timeout constant tests.
- `TESTCASE=retry libtool exec ./zhttp/interop/zhttpqir client` exits `127`.
- Supported client cases with empty `REQUESTS` exit `2`.
- Unsupported cases do not start `ZiMultiplex`, open sockets, or create output
  files.
- No new manual time arithmetic, raw fixed-capacity arrays, or STL containers
  are introduced in `zhttp/interop`.

### Slice 3: HTTP/3 Local Endpoint

Deliverables:

- HTTP/3-only server path using the reusable `Zhttpd.hh` static-file behavior
- HTTP/3 client path using direct `Zhttp` APIs
- `ZhttpQIRSmokeTest.cc`
- local certificate/temp-directory helpers

Automated tests:

- `ZhttpQIRSmokeTest` unsupported exit
- `ZhttpQIRSmokeTest` HTTP/3 single-file transfer
- `ZhttpQIRSmokeTest` HTTP/3 multi-file transfer
- `ZhttpQIRSmokeTest` bad-path rejection
- `ZhttpQIRSmokeTest` server shutdown

Acceptance criteria:

- `make -C zhttp/interop test` passes `ZhttpQIRTest` and `ZhttpQIRSmokeTest`.
- `ZhttpQIRSmokeTest` passes:
  - `unsupported-exit`
  - `h3-single-file`
  - `h3-multi-file`
  - `bad-path`
  - `shutdown`
- The client starts all HTTP/3 requests in one process using one
  `ZiMultiplex`; it does not shell out to `zhttp` per URL.
- The server opens only UDP/H3 listeners for this endpoint; it does not enable
  HTTP/1.1-over-TCP or HTTPS/H1-over-TLS.
- `make test` from the top level includes the interop tests and passes.

### Slice 4: `hq-interop` Local Endpoint

Deliverables:

- direct `zquic` server path for ALPN `hq-interop`
- direct `zquic` client path for ALPN `hq-interop`
- shared transfer/download state for `handshake`, `transfer`, `rebind-port`,
  and `rebind-addr`
- smoke coverage for single-file and multi-file `hq-interop` transfers

Automated tests:

- `ZhttpQIRSmokeTest` `hq-interop` single-file transfer
- `ZhttpQIRSmokeTest` `hq-interop` multi-file transfer
- `ZhttpQIRTest` or `ZhttpQIRSmokeTest` verifies the same HQ path is selected
  for `handshake`, `transfer`, `rebind-port`, and `rebind-addr`

Acceptance criteria:

- `ZhttpQIRSmokeTest` passes:
  - `hq-single-file`
  - `hq-multi-file`
- `handshake`, `transfer`, `rebind-port`, and `rebind-addr` use the same
  `hq-interop` implementation, with only runner/network behavior differing by
  testcase.
- Stream payloads use pooled `ZiIOBuf` and the appropriate `ZquicBuf.hh`
  allocators; no STREAM Rx payload copies are added outside existing `zquic`
  behavior.
- Rx/Tx state ownership follows the shard rules in `zquic/GUIDELINES.md`.
- Release build with `Zquic_DEBUG` off does not compile qlog-only work into
  the endpoint path.

### Slice 5: Docker Packaging

Deliverables:

- `zhttp/interop/qir_endpoint.sh`
- `zhttp/interop/Dockerfile.interop`
- `zhttp/interop/qir-stage` automake target
- top-level `zhttp-qir-image` automake target
- `scripts/qir-build-image`

Automated tests:

- existing `ZhttpQIRTest` and `ZhttpQIRSmokeTest` remain green
- script syntax/help coverage includes `scripts/qir-build-image`
- bad staged root is rejected before Docker is invoked

External checks:

- top-level release rebuild
- local image creation
- Docker role/unsupported/http3 smoke

Acceptance criteria:

- `./z.config -c -Q -L /usr/local && make clean && make -j8` succeeds.
- `make -j8 zhttp-qir-image` creates `zhttp-qir:local` from a staged install
  root.
- The image contains `/usr/local/bin/zhttpqir`,
  `/usr/local/bin/qir_endpoint.sh`, and the required installed Z shared
  libraries.
- The Dockerfile does not run `./z.config`, `make`, or `make install`.
- Docker role smoke checks pass:
  - missing role exits `2`
  - `TESTCASE=retry` exits `127`
  - `TESTCASE=http3` single-file transfer succeeds
  - `TESTCASE=transfer` single-file `hq-interop` transfer succeeds
- Docker multi-file smoke checks pass for:
  - `TESTCASE=http3`
  - `TESTCASE=transfer`
- Docker is not required by `make test`.

### Slice 6: Lifecycle Scripts and Reporting

Deliverables:

- `scripts/qir-upload-image`
- `scripts/qir-run-local`
- `scripts/qir-collect-results`
- `scripts/qir-report`
- optional `zhttp/interop/qir-script-test.sh` if C++ smoke tests do not cover
  script behavior cleanly
- fixture data under `zhttp/interop/testdata/qir-results/` for report tests

Automated tests:

- `sh -n` for every `scripts/qir-*` file
- help path for every `scripts/qir-*` file
- unknown-option failure for every `scripts/qir-*` file
- collector test using synthetic runner output
- report test using checked-in fixture data
- `qir-run-local` dry-run validation or equivalent script test coverage

Acceptance criteria:

- `sh -n` passes for every `scripts/qir-*` file.
- Every `scripts/qir-* --help` exits `0`.
- Every script exits non-zero for an unknown option.
- `scripts/qir-build-image --root <bad-root>` fails before invoking Docker.
- `scripts/qir-collect-results` copies synthetic `logs/`, `logs_*`, `results/`,
  `results_*`, stdout, stderr, status, and runner revision into a temp output
  directory.
- `scripts/qir-report` generates markdown from the checked-in fixture and from
  one collected synthetic run.
- `scripts/qir-run-local` can run in a dry-run or no-op validation mode that
  verifies runner path, image argument, peer list parsing, testcase parsing,
  and output directory creation without invoking the upstream runner. If dry
  run is not added, equivalent validation must be covered by a shell test.

### Slice 7: Documentation and Manual Lifecycle

Deliverables:

- `zhttp/interop/README.md`
- updated `quic_interop.md` if implementation decisions differ from this plan
- saved local smoke report
- saved Docker smoke report

Automated tests:

- `make -C zhttp/interop test` remains green after README examples and script
  paths are finalized
- lifecycle script dry-runs cover every README command that can be validated
  without Docker or an upstream runner checkout

External checks:

- execute the documented local non-Docker smoke sequence
- execute the documented Docker smoke sequence
- save both reports under the documented results location

Acceptance criteria:

- README includes exact commands for:
  - local non-Docker smoke
  - release configure/build
  - image creation
  - Docker smoke
  - local upstream runner registration
  - remote image upload
  - upstream registration PR preparation
- README documents supported and unsupported testcases with the same statuses
  as this plan.
- README documents the `/usr/local` image prefix requirement and the reason it
  differs from common host-side `/opt/z` developer builds.
- README states that Docker and upstream runner checks are bench lifecycle
  steps, not `make test` prerequisites.

### Slice 8: Upstream Interop Validation

Deliverables:

- release image tagged with the tested git revision
- preserved `scripts/qir-run-local` output directory
- markdown report from `scripts/qir-report`
- optional pushed public image after local validation

Automated tests:

- full local automated suite passes from a clean release rebuild immediately
  before running the upstream matrix
- `scripts/qir-report` validates the collected matrix directory after each
  runner invocation group

External checks:

- upstream runner against `ngtcp2` in both directions for core cases
- upstream runner against available `quic-go`, `quiche`, and `msquic` images
  in both directions for core cases
- rebinding cases after the core matrix passes
- optional upload only after the exact tested digest is recorded

Acceptance criteria:

- `scripts/qir-run-local` passes both directions against `ngtcp2` for:
  - `handshake`
  - `transfer`
  - `http3`
- After `ngtcp2` passes, both directions pass against available reference
  images for:
  - `quic-go`
  - `quiche`
  - `msquic`
- `rebind-port` and `rebind-addr` are run after core cases and either pass or
  are explicitly downgraded to unsupported in the supported-case table and
  README with a concrete reason.
- The generated report records image tag, image digest, z revision, runner
  revision, build type, host facts, pass/fail rows, and paths to logs/pcaps.
- No image is uploaded as `latest` or proposed upstream until the exact digest
  has passed the required matrix.

## Build Verification

After adding `zhttp/interop`, regenerate and rebuild from the top level:

```sh
./z.config -c -Q -L /usr/local
make clean
make -j8 zhttp-qir-image
```

Do not verify this by compiling only `zhttp/interop`; the endpoint links through
`zhttp`, `zquic`, `ztls`, `zi`, and lower libraries, and the image must be
created from the top-level staged install.

## Final Acceptance Criteria

The implementation is complete only when every delivery slice above has met
its acceptance criteria and:

- every item in the coverage matrix has either passed or is documented as
  unsupported/skipped with a concrete external reason
- every supported testcase has local automated coverage and an upstream-runner
  result before it is advertised as supported
- `make -j8` builds `zhttp/interop/zhttpqir`.
- `make test` runs and passes the `zhttp/interop` automated tests as part of the
  normal `zhttp` test path.
- `ZhttpQIRTest` covers role parsing, testcase parsing, environment loading,
  `REQUESTS` parsing, URL mapping, `hq-interop` request-line parsing, and
  timeout constants.
- `ZhttpQIRSmokeTest` covers unsupported exit, HTTP/3 single/multi-file
  transfer, `hq-interop` single/multi-file transfer, path rejection, and clean
  shutdown.
- `zhttpqir --help` documents `server` and `client`.
- Unsupported testcases exit `127`.
- Non-Docker smoke transfer succeeds.
- Docker image builds as `zhttp-qir:local`.
- Docker smoke transfer succeeds for both HTTP/3 and `hq-interop`, including
  at least one multi-file run for each path.
- `scripts/qir-build-image` can produce a local image and a registry-ready tag.
- `scripts/qir-upload-image` can push an already validated image tag.
- `scripts/qir-run-local` can execute an external upstream runner checkout without permanent
  runner modifications and preserve logs for every direction.
- `scripts/qir-collect-results` can preserve runner logs, pcaps, stdout,
  stderr, status, and runner revision for every invocation.
- `scripts/qir-report` can summarize the preserved runner logs into markdown.
- Upstream runner passes `handshake`, `transfer`, and `http3` against `ngtcp2`
  in both directions.
- Upstream runner passes `handshake`, `transfer`, and `http3` against
  `quic-go`, `quiche`, and `msquic` where those reference images are available
  locally.
- `rebind-port` and `rebind-addr` are either passing in both directions
  against `ngtcp2` or explicitly left out of the supported-case table with a
  short reason in `zhttp/interop/README.md`.
- `zhttp/interop/README.md` documents all manual steps for registry login,
  image publication, local runner registration, and upstream registration PR
  preparation.
- The final saved report identifies any skipped peer/testcase combination as
  skipped or unsupported, never as passed.
