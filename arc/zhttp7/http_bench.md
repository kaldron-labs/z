## Summary

Add a Linux benchmark harness under `zhttp/bench` for static-file HTTP serving
comparisons across HTTP/1.1 cleartext, HTTP/1.1 over TLS, and HTTP/3 over QUIC.
The harness generates one deterministic random response file per benchmark
series, reuses it for every matrix row, runs bounded-concurrency GET workloads,
and records elapsed time plus separate client/server CPU usage from cgroup v2
CPU accounting.

The initial product requirement is the 18-row nominal matrix:

| Protocol | Client | Server |
| --- | --- | --- |
| `h1` HTTP/1.1 over TCP, `http:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |
| `h1tls` HTTP/1.1 over TLS/TCP, `https:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |
| `h3` HTTP/3 over QUIC/UDP, `https:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |

Rows that cannot run are first-class results with `status=skipped` and a stable
`skip_reason`. Examples include `darkhttpd` for TLS/H3, missing executables,
curl builds without HTTP/3, and the current `zhttp` example client until it has
a native multi-request benchmark mode. Failed rows are distinct from skipped
rows: if a declared-capable adapter fails to start, probe, or complete, write
`status=failed` and preserve available logs and counters.

The runner should be a native C++ program, not a shell script. It should use
local Z Framework facilities where they fit: `ZtCLI` for option parsing,
`ZiFile` and `ZiDir`/path helpers for filesystem work, `Ztls::MD<SHA256>` for
file checksums, `ZiCSV`/`ZtCSV` for structured result output, `ZuBox`/`ZuFmt`
for formatting, and direct POSIX APIs for process groups, `fork`/`exec`,
signals, `waitpid`, pipes, and cgroup v2 file operations. The benchmark harness
is intentionally Linux-only at first; the `zhttp/bench` directory may exist on
all platforms, but `zhttpbench` itself is not built on MinGW.

Research checks against public documentation affect the design:

- curl's `--parallel`/`--parallel-max` options are the right single-process
  multi-transfer path; `--http3-only` is preferable for H3 because it is defined
  to avoid fallback to earlier HTTP versions.
- A curl config file can carry repeated URL/output entries; use per-transfer
  `output = "/dev/null"` and write-out data when byte/status/protocol
  validation is needed.
- Caddy's `file_server` serves static files under `root`; the `encode`
  directive enables compression and is therefore not a valid way to disable
  compression. Omit `encode` unless an installed Caddy version has a verified
  `encode off` behavior.
- Caddy H3 needs UDP for QUIC and also uses the HTTPS TCP listener for TLS/H1
  and advertisement/probing behavior; reserve both TCP and UDP on the selected
  port.
- cgroup v2 `cpu.stat` exposes `usage_usec`, `user_usec`, and `system_usec`;
  moving a process into a cgroup is done by writing its PID to `cgroup.procs`.
- darkhttpd is a simple HTTP/1.1 static file server with `--addr` and `--port`;
  it has no native TLS or HTTP/3 support.

No `plan.feedback.md` was present when this revision was written.

## Architecture Documentation

### New Components

- `zhttp/bench/Makefile.am` builds `zhttpbench` on non-MinGW platforms and
  links against the same Z libraries used by `zhttp/example`.
- `zhttp/bench/zhttpbench.cc` contains the benchmark runner and the adapter
  implementations. Keep this as a single translation unit for the first
  implementation unless it becomes unmanageably large; benchmark code is not a
  reusable library yet.
- `zhttp/bench/README.md` or an update to `zhttp/README.md` documents runtime
  prerequisites: Linux cgroup v2 write access, `curl`, `darkhttpd`, `caddy`,
  `openssl`, and build-tree locations for `zhttp`/`zhttpd`.
- `zhttp/example/zhttp.cc` is later extended with native benchmark mode so the
  matrix can measure the Z HTTP client without one process per request.

### Changed Build Integration

- Update `configure.ac` `AC_CONFIG_FILES` to include `zhttp/bench/Makefile`.
- Update `zhttp/Makefile.am` from `SUBDIRS = src test example` to
  `SUBDIRS = src test example bench`. Keeping `bench` after `example` lets the
  harness prefer build-tree `zhttp/example/zhttp` and `zhttp/example/zhttpd`
  after those binaries have been built.
- `zhttp/bench/Makefile.am` should use include paths and `LDADD` consistent
  with `zhttp/example/Makefile.am`, adding no new third-party dependency for
  the initial implementation. OpenSSL is already present through `ztls`/QUIC;
  the harness can call the `openssl` executable for certificate generation.

### Processes and Threads

- The harness process orchestrates the run; it does not create benchmark worker
  threads in the initial version.
- Each server is launched as a foreground child in a new process group so the
  full server tree can be terminated.
- Each client workload is launched as a foreground child process. The measured
  client CPU cgroup includes all client processes for that row.
- For cgroup placement, the child should pause after fork until the parent has
  written the child PID to the correct `cgroup.procs`, then the child `exec`s.
  This avoids charging process startup to the parent and avoids races where the
  child performs meaningful work before cgroup assignment.

### Interfaces

Command line:

```text
zhttpbench [OPTION]...
```

Core options:

| Option | Default | Meaning |
| --- | ---: | --- |
| `--requests N` | `10000` | Total GET requests attempted per measured run. |
| `--concurrency N` | `10` | Maximum in-flight requests. |
| `--file-size N` | `1048576` | Generated response body size in bytes. |
| `--seed N` | `1` | Deterministic PRNG seed. |
| `--work-dir PATH` | `zhttp/bench/run` | Owned directory for data, configs, certs, logs, tmp, and results. |
| `--file-name NAME` | `bench.dat` | Relative URL and filesystem leaf name. Reject names with `/`, `..`, or empty components. |
| `--out PATH` | `<work-dir>/results.csv` | Append or create result CSV. |
| `--protocol LIST` | `h1,h1tls,h3` | Comma-separated protocol filter. |
| `--client LIST` | `curl,zhttp` | Comma-separated client filter. |
| `--server LIST` | `darkhttpd,caddy,zhttpd` | Comma-separated server filter. |
| `--port-base N` | `18080` | First candidate TCP/UDP port. |
| `--repetitions N` | `1` | Repetitions per supported row. |
| `--warmup N` | `0` | Warmup requests before measured CPU sampling. |
| `--timeout SEC` | `60` | Per-run wall-clock timeout. |
| `--startup-timeout SEC` | `10` | Server readiness timeout. |
| `--regenerate-file` | disabled | Replace mismatched generated file and metadata. |
| `--keep-going` | enabled | Continue after row failure and log the failure. |
| `--print-commands` | disabled | Print exact child argv vectors before launch. |

Tool path overrides:

| Option | Default lookup |
| --- | --- |
| `--curl PATH` | `PATH` lookup for `curl` |
| `--caddy PATH` | `PATH` lookup for `caddy` |
| `--darkhttpd PATH` | `PATH` lookup for `darkhttpd` |
| `--openssl PATH` | `PATH` lookup for `openssl` |
| `--zhttp PATH` | build-tree `zhttp/example/zhttp`, then `PATH` |
| `--zhttpd PATH` | build-tree `zhttp/example/zhttpd`, then `PATH` |

Protocol records:

| ID | URL scheme | Transport | Host in URL | Bind address | Notes |
| --- | --- | --- | --- | --- | --- |
| `h1` | `http` | TCP | `127.0.0.1` | `127.0.0.1` | HTTP/1.1 cleartext. |
| `h1tls` | `https` | TLS/TCP | `localhost` | `127.0.0.1` | HTTP/1.1 over TLS using generated cert. |
| `h3` | `https` | QUIC/UDP | `localhost` | `127.0.0.1` | HTTP/3 over QUIC using generated cert. |

Adapter interface shape:

```text
ServerAdapter:
  id()
  staticSupports(protocol)
  preflight(tool paths, protocol) -> available/skipped
  command(protocol, port, paths) -> argv/env/cwd
  readyProbe(protocol, url, certPath) -> ok/error

ClientAdapter:
  id()
  staticSupports(protocol)
  preflight(tool paths, protocol) -> available/skipped
  prepareWorkload(protocol, url, requests, concurrency, paths)
  command(protocol, workload, certPath) -> argv/env/cwd
  parseCompletion(logs/write-out) -> completed/failed/bytes/protocol
```

Adapters return structured status. Only `ResultWriter` writes CSV.

### Data Flows

1. Parse options with `ZtCLI`.
2. Resolve tool paths and dynamic capabilities.
3. Create/verify the benchmark series directory:

```text
run/
  www/
    bench.dat
    bench.dat.meta
  tls/
    cert.pem
    key.pem
  caddy/
    <run-id>.Caddyfile
  workload/
    <run-id>.curl
    <run-id>.zhttp
  logs/
    <run-id>.server.log
    <run-id>.client.log
  tmp/
  results.csv
```

4. Generate `www/<file-name>` if absent, stream PRNG bytes to disk, and compute
   SHA-256 while writing. If present, verify size and stored checksum before
   reuse. Mismatch is fatal unless `--regenerate-file` is supplied.
5. Generate one localhost certificate per series with SAN entries for
   `DNS:localhost` and `IP:127.0.0.1`.
6. For each matrix row, write workload/config files, launch server, probe,
   optionally warm up, measure client run, stop server, and write one CSV row.

### Event and Timer Processing

- Use monotonic time for readiness polling, timeouts, and measured elapsed time.
- Readiness probes should retry with short sleeps until success or
  `--startup-timeout`.
- Per-run timeout kills the client process group first, then the server group,
  and records a failed row.
- Optional warmup is excluded from measured request counters and CPU deltas.

### Network Programming

- Port allocation must consider both TCP and UDP for `h3`; use the same port
  number for HTTPS TCP and QUIC UDP when testing Caddy or `zhttpd`.
- Binding/probing remains loopback-only.
- Do not add cache-busting query strings. The benchmark intentionally measures
  repeated static-file serving of a hot object unless a future storage-focused
  mode is added.
- The OS page cache remains warm across matrix rows. Dropping caches requires
  elevated privileges and would change the benchmark from HTTP serving overhead
  to storage overhead.

### Data Stores

- Result data is appendable CSV with schema version `1`.
- Series metadata is a small text or CSV/JSON file containing file size, seed,
  checksum, and generation options. Do not infer checksum validity from file
  existence alone.
- Logs are plain files under `logs/` and are referenced from result rows by
  relative path.

## Detailed Design and Implementation Plan

### Phase 1: Buildable Harness Skeleton and Deterministic Skip Rows

Create the harness as a buildable C++ program that can enumerate the matrix,
preflight obvious capabilities, and emit valid CSV rows for skipped entries
without launching servers or clients yet.

Files:

- Add `zhttp/bench/Makefile.am`.
- Add `zhttp/bench/zhttpbench.cc`.
- Update `zhttp/Makefile.am`.
- Update `configure.ac`.

Implementation details:

- Define `Options`, `ProtocolSpec`, `ToolSpec`, `Combination`, `RunID`,
  `Paths`, `Result`, `ResultWriter`, and small enum tables for protocols,
  clients, servers, and statuses.
- Use `ZtCLI` for command-line parsing, matching the existing pattern in
  `zhttp/example/zhttp.cc` and `zhttp/example/Zhttpd.hh`.
- Implement comma-list parsing for protocol/client/server filters with
  `ZuMatcher` or simple Z string scanning; reject unknown IDs up front.
- Implement `Result` as a `ZtStruct` mapped for CSV and write with
  `ZiCSV::writeFile` or a small append writer using `ZtCSV` quoting. Prefer
  `ZiCSV` because local tests already exercise file-backed CSV read/write.
- Add header management: when the output file does not exist or is empty, write
  the schema header; otherwise append rows. Do not duplicate headers.
- Implement static capability skips:
  - `darkhttpd` supports only `h1`.
  - `caddy` supports `h1`, `h1tls`, and potentially `h3`.
  - `zhttpd` supports `h1`, `h1tls`, and `h3`.
  - `curl` supports `h1`, `h1tls`, and dynamically detected `h3`.
  - `zhttp` rows are initially skipped until native benchmark mode is added.
- Implement tool path resolution for `PATH` and build-tree defaults. Missing
  executables produce skipped rows for dependent combinations.
- Implement `--print-commands` as a no-op until command adapters exist, but
  keep the option parsed for CLI stability.

Validation:

- `./z.config -c /opt/z` or the repository's configured autoreconf path after
  adding the Makefile.
- `make -C zhttp/bench zhttpbench`.
- Run `zhttpbench --protocol h3 --server darkhttpd --client curl --requests 1`
  and verify one skipped CSV row with stable columns.

### Phase 2: Work Directory, File Generation, Checksum, Certificate, and Cgroup Preflight

Add the shared benchmark-series state needed before any real run executes.
This phase still does not need a successful HTTP benchmark row.

Implementation details:

- Create `Paths` helpers for `www`, `tls`, `caddy`, `workload`, `logs`, and
  `tmp` using `ZiFile::append`, `ZiFile::mkdir`, and existence checks.
- Validate `--file-name` as a relative leaf name. Reject empty names, `/`,
  backslash on Windows for defensive clarity, `.` and `..`.
- Implement deterministic file generation:
  - Use a small fixed PRNG such as xorshift64* or splitmix64 seeded from
    `--seed`.
  - Write with `ZiFile` in chunks; avoid fixed large stack arrays. A
    `ZtLocalArray<uint8_t, 64<<10>` or similar scratch buffer is appropriate.
  - Update `Ztls::MD<Ztls::SHA256>` while streaming bytes.
  - Store checksum as lowercase hex in sidecar metadata.
- Implement file verification:
  - Verify size with `ZiFile::size()` or stat.
  - Recompute SHA-256 and compare with metadata.
  - Fatal on mismatch unless `--regenerate-file` is supplied.
- Implement certificate generation using `openssl req -x509 -newkey rsa:2048`
  with `-nodes`, one-day validity, CN `localhost`, SAN
  `DNS:localhost,IP:127.0.0.1`, and output paths under `tls/`.
  If certificate generation fails, skip TLS/H3 rows that require the cert with
  `skip_reason=certificate generation failed` rather than failing cleartext H1.
- Implement cgroup v2 preflight:
  - Locate the cgroup v2 mount by parsing `/proc/self/mountinfo`.
  - Determine the current writable cgroup path from `/proc/self/cgroup`.
  - Create a benchmark parent cgroup and `client`/`server` probe child
    cgroups.
  - Fork a short-lived child, move it by writing PID to `cgroup.procs`, wait,
    and read `cpu.stat`.
  - Verify `usage_usec`, `user_usec`, and `system_usec`.
  - Remove temporary cgroups after child exit.
- Treat failed cgroup preflight as a setup error and abort before matrix
  execution. There is no `/proc` or `getrusage()` fallback for benchmark
  results.

Validation:

- Run with only unsupported rows and verify the generated file, metadata, cert,
  and skipped CSV remain coherent.
- Add a small unit-like self-test mode only if useful; otherwise cover through
  direct harness runs.

### Phase 3: Process Control, Cgroup Accounting, and First Real Row (`h1` + `darkhttpd` + `curl`)

Implement the first end-to-end vertical slice using the least complex external
participants: cleartext HTTP/1.1, darkhttpd, and curl.

Implementation details:

- Add a `Process` helper:
  - Build argv as `ZtArray<ZtString<>>` and convert to `char *const *` only at
    fork/exec boundary.
  - Create pipes for child stdout/stderr redirection to log files or open log
    files directly before exec.
  - Call `setpgid(0, 0)` in the child.
  - Use a parent/child pipe handshake so the parent can place the child in the
    selected cgroup before exec.
  - Kill process groups with `SIGTERM`, then `SIGKILL` after a grace timeout.
- Add `CpuAccount`:
  - Create one cgroup per run role: `server` and `client`.
  - Snapshot `cpu.stat` immediately before and after the measured section.
  - Compute unsigned deltas for total/user/system usec.
  - Remove run cgroups after all member processes have exited.
- Implement port selection:
  - Start at `--port-base`.
  - For `h1`, reserve/probe TCP only.
  - If bind fails or readiness logs show address-in-use, try the next port and
    record the actual URL in the row.
- Implement darkhttpd adapter:

```text
darkhttpd <www-root> --addr 127.0.0.1 --port <port> --log <server-log>
```

  Do not use daemon mode.
- Implement curl h1 adapter:
  - Generate a curl config file with the target URL repeated exactly
    `--requests` times.
  - Pair each URL with `output = "/dev/null"`.
  - Run one curl process:

```text
curl --http1.1 --fail --silent --show-error --parallel \
  --parallel-max <concurrency> --config <curl-workload-file>
```

  - Capture stderr/stdout to the client log.
  - Initially treat exit status `0` as all requests completed and non-zero as
    failed; later phases can add write-out validation.
- Implement readiness probe for h1:
  - Prefer curl if available: one `curl --http1.1 --fail --silent --output
    /dev/null <url>`.
  - Verify the downloaded byte count when using `--write-out %{size_download}`.
  - Probe before measured CPU snapshots, so readiness CPU is not counted.
- Implement run lifecycle exactly:
  1. Create run ID and log paths.
  2. Start server in server cgroup.
  3. Probe readiness until success or startup timeout.
  4. Warm up if requested.
  5. Snapshot CPU counters.
  6. Start monotonic timer.
  7. Run client in client cgroup.
  8. Stop timer and snapshot CPU counters.
  9. Terminate server process group.
  10. Write result row.

Validation:

- Run one local benchmark:

```sh
zhttpbench --protocol h1 --server darkhttpd --client curl \
  --requests 10 --concurrency 2 --work-dir /tmp/zhttpbench
```

- Verify `status=ok`, `requests_completed=10`, non-empty elapsed and CPU
  fields, and server/client logs are present.

### Phase 4: zhttpd H1 and Shared Server/Client Correctness Checks

Extend the real-run slice to the in-tree `zhttpd` cleartext server before TLS
or H3 complexity is introduced.

Implementation details:

- Resolve build-tree `zhttp/example/zhttpd` before `PATH`.
- Add zhttpd h1 command:

```text
zhttpd <www-root> --http --addr 127.0.0.1 --port <port> \
  --no-server-id --timeout 0 --log <server-log>
```

  `--timeout 0` matches existing zhttpd CLI and removes idle-timeout noise.
- Factor server readiness and termination so darkhttpd and zhttpd use the same
  lifecycle.
- Improve curl result validation:
  - Add `--write-out` per transfer only if it can be written cheaply to a file
    without flooding stdout for large request counts.
  - At minimum, verify curl exit status and total expected body bytes by using
    a curl-supported aggregate strategy or by parsing generated per-transfer
    write-out lines.
  - If validation is too expensive for large runs, make it opt-in while still
    validating readiness body size before measurement.
- Ensure failed rows preserve `server_log`, `client_log`, `error`, elapsed time
  if available, and partial CPU deltas if measurement started.

Validation:

- Run h1 curl against `darkhttpd` and `zhttpd` with low request counts.
- Kill a server manually or use a busy port to verify failed rows and cleanup.

### Phase 5: Caddy H1/H1TLS and TLS Correctness

Add Caddy for cleartext and TLS, then add TLS mode for zhttpd. This phase
delivers most HTTP/1.1 rows except the native zhttp client.

Implementation details:

- Generate one Caddyfile per run to avoid concurrent log/config collisions.
- For cleartext:

```text
http://127.0.0.1:<port> {
	root * <www-root>
	file_server
	log {
		output file <log-path>
	}
}
```

- For TLS:

```text
https://localhost:<port> {
	tls <cert.pem> <key.pem>
	root * <www-root>
	file_server
	log {
		output file <log-path>
	}
}
```

- Do not include `encode off`; Caddy documentation describes `encode` as the
  directive that enables compression. Omit the directive and keep generated data
  incompressible.
- Caddy command:

```text
caddy run --config <Caddyfile> --adapter caddyfile
```

- Add zhttpd TLS command:

```text
zhttpd <www-root> --https --addr 127.0.0.1 --port <port> \
  --cert <cert.pem> --key <key.pem> --no-server-id --timeout 0 \
  --log <server-log>
```

- Add curl TLS command:

```text
curl --http1.1 --fail --silent --show-error --cacert <cert.pem> \
  --parallel --parallel-max <concurrency> --config <curl-workload-file>
```

- TLS/H3 URL host must be `localhost` so certificate verification succeeds.
- If the generated certificate is a self-signed leaf rather than a CA, verify
  curl accepts it through `--cacert`; if not, generate a local CA cert and sign
  the server cert, then pass the CA cert to clients.

Validation:

- Run `h1` and `h1tls` with `curl` against Caddy and zhttpd.
- Verify certificate mismatch failures are not silently ignored; do not use
  `curl -k` for measured rows.

### Phase 6: HTTP/3 Rows for curl, Caddy, and zhttpd

Add H3 support only after the H1/TLS lifecycle is stable.

Implementation details:

- Dynamic curl preflight:
  - Run `curl --version` and require an HTTP/3 feature token before enabling H3.
  - Run `curl --help all` or a cheap probe to confirm `--http3-only`,
    `--parallel`, and `--parallel-max` are accepted.
  - If `--http3-only` is unavailable but `--http3` is available, keep H3 rows
    skipped unless write-out validation proves `%{http_version}` is `3` for
    every transfer. Silent fallback must never count as H3.
- Curl H3 command:

```text
curl --http3-only --fail --silent --show-error --cacert <cert.pem> \
  --parallel --parallel-max <concurrency> --config <curl-workload-file>
```

- zhttpd H3 command:

```text
zhttpd <www-root> --http3 --addr 127.0.0.1 --port <port> \
  --cert <cert.pem> --key <key.pem> --no-server-id --timeout 0 \
  --log <server-log>
```

- Caddy H3:
  - Use the same TLS Caddyfile shape as Phase 5.
  - Reserve both TCP and UDP for the port.
  - Probe with H3-capable curl using `--http3-only`; if probe fails, mark the
    row failed because Caddy was considered capable after preflight.
- Add protocol verification:
  - For measured H3 rows, capture curl write-out protocol/version lines in a
    compact file and require every completed transfer to report HTTP/3 when the
    adapter supports that reporting.
  - If this creates excessive output for large runs, sample validation during
    warmup and document that measured validation is exit-status based.

Validation:

- Run low-count `h3` rows for `curl+caddy` and `curl+zhttpd`.
- Verify UDP/TCP port cleanup after failures.

### Phase 7: Native Multi-Request Mode in `zhttp/example/zhttp.cc`

Add zhttp client benchmark capability and then enable `zhttp` client matrix
rows. Do not use one process per request as a fallback.

Implementation details:

- Extend `Options` in `zhttp/example/zhttp.cc`:
  - `--requests N`
  - `--concurrency N`
  - `--discard`
  - optionally `--pipeline-depth N` for HTTP/1.1 when supported
  - optionally `--stats PATH` for machine-readable completed/failed/bytes data
- Preserve the current one-shot URL behavior as the default. Benchmark mode is
  activated when `--requests` or `--concurrency` is supplied.
- Reuse existing URL parsing, TLS CA handling, H1/H3 request builders, response
  parser, and body accounting in `zhttp/example/zhttp.cc`.
- For HTTP/1.1:
  - Maintain one or more persistent connections sufficient to honor
    `--concurrency`.
  - Reuse keep-alive connections.
  - Avoid pipelining by default unless the implementation already handles
    response ordering robustly; expose pipelining as explicit later tuning.
- For HTTPS:
  - Reuse TLS connections similarly to H1.
- For HTTP/3:
  - Prefer concurrent request streams on one QUIC connection where existing
    `Zquic`/`Zhttp::H3` APIs support it.
  - If stream concurrency is not currently supported by the example client,
    mark H3 zhttp rows skipped with a precise reason rather than faking
    concurrency through multiple processes.
- `--discard` consumes response bodies without writing them to disk. It must
  still count bytes and verify content length when available.
- Emit exact `requests_completed`, `requests_failed`, and `bytes_received`.
  Prefer a small CSV or key-value stats file over parsing human console output.
- Add harness detection:
  - Run `zhttp --help` and require benchmark options before enabling zhttp
    rows.
  - Parse the stats file after completion.

Validation:

- Add focused tests or low-count example runs for:
  - one request H1 to zhttpd;
  - many requests with concurrency > 1;
  - `--discard` does not create response files;
  - failure counts when the server exits mid-run.
- Run matrix rows for `zhttp` client only after stats are reliable.

### Phase 8: Reporting, Polish, and Documentation

Finalize the human-facing output and runtime documentation after the core rows
work.

Implementation details:

- Print a compact console summary sorted by protocol, server, client:

```text
protocol server    client status   req/s   MiB/s client_cpu_us/req server_cpu_us/req
h1       zhttpd    curl   ok      12345 12345.0             15.0              11.0
h3       darkhttpd curl   skipped server darkhttpd does not support h3
```

- Keep CSV as the stable artifact. Console summary is not parsed by tests.
- Add `--shuffle` only as a follow-up option if fixed matrix order is shown to
  bias results through thermals or cache state.
- Add README instructions for:
  - configuring and building `zhttpbench`;
  - required cgroup v2 permissions;
  - installing external tools;
  - interpreting skipped versus failed rows;
  - example commands.

Validation:

- Run a reduced full matrix with `--requests 5 --concurrency 2` and inspect CSV
  plus summary.
- Run build/test targets for touched modules.

## Code References to Impacted Code

- `configure.ac:233` - add `zhttp/bench/Makefile` to configured Makefile list.
- `zhttp/Makefile.am:2` - add `bench` to `SUBDIRS` after `example`.
- `zhttp/example/Makefile.am:1` - copy include/link patterns for
  `zhttp/bench/Makefile.am`.
- `zhttp/example/zhttp.cc:21` - extend `Options` and CLI mapping for native
  benchmark mode in Phase 7.
- `zhttp/example/zhttp.cc:883` - route `main()` into one-shot or benchmark mode.
- `zhttp/example/zhttpd.cc:33` - existing server CLI confirms `--http`,
  `--https`, `--http3`, `--cert`, `--key`, `--timeout`, and `--no-server-id`.
- `zhttp/example/Zhttpd.hh:68` - existing `Zhttpd::Options` and `ZtCLI` mapping
  confirm server option names and types.
- `zhttp/src/Zhttp.hh:14` - shared H1/H3 parser and builder API used by the
  example client/server.
- `zt/src/ZtCLI.hh:40` - CLI parsing framework to use for `zhttpbench`.
- `zt/src/ZtCSV.hh:14` - CSV formatting and quoting support for result rows.
- `zi/test/ZiCSVTest.cc:75` - local file-backed CSV write/read examples.
- `ztls/src/ZtlsMD.hh:27` - SHA-256 digest wrapper for file checksum.
- `zi/src/ZiFile.hh` and `zi/src/ZiFile.cc` - file/path helpers for work
  directory, generated data, logs, metadata, and cleanup.
- `zi/test/ZiDaemonTest.cc:169` - local fork/exec/wait pattern for POSIX child
  process tests.

## Detailed Test Plan

- Build tests:
  - Re-run autoreconf/configure path after adding `zhttp/bench/Makefile.am`.
  - `make -C zhttp/bench zhttpbench`.
  - `make -C zhttp/example zhttp zhttpd` after modifying `zhttp` client.
- Parser/config tests:
  - Unknown protocol/client/server list values fail fast.
  - Empty or invalid `--file-name` fails fast.
  - Missing external tools produce skipped rows, not crashes.
  - CSV header appears once when appending.
- Generated file tests:
  - Same seed and size produce same SHA-256.
  - Existing file with matching metadata is reused.
  - Existing file with mismatched size/checksum fails unless
    `--regenerate-file` is passed.
- Cgroup tests:
  - Missing cgroup v2 write permission aborts before matrix execution.
  - Successful preflight verifies all three counters.
  - Run cgroups are removed after child exit.
- Process lifecycle tests:
  - Server startup timeout records a failed row and leaves no child process.
  - Client timeout kills client process group and then server process group.
  - Logs are written for failed rows.
- Adapter tests:
  - `darkhttpd+h1+curl` low-count run succeeds.
  - `darkhttpd+h1tls` and `darkhttpd+h3` are skipped with stable reasons.
  - `zhttpd+h1/h1tls/h3+curl` low-count runs succeed when local build supports
    those protocols.
  - `caddy+h1/h1tls/h3+curl` low-count runs succeed when installed Caddy
    supports the modes.
  - curl without HTTP/3 skips H3 curl rows.
  - curl H3 rows using `--http3-only` fail rather than silently falling back.
- zhttp client tests:
  - One-shot behavior remains compatible.
  - `--requests 1 --concurrency 1 --discard` works for H1.
  - `--requests N --concurrency M --discard --stats PATH` reports exact
    completed/failed/bytes.
  - If H3 stream concurrency is incomplete, H3 zhttp rows remain skipped with a
    precise reason.
- Result format tests:
  - `ok`, `skipped`, and `failed` rows have identical column counts.
  - Raw fields are sufficient to recompute req/s, MiB/s, CPU usec/request, and
    CPU usec/MiB.

## Acceptance Criteria

- `zhttpbench` builds on Linux and is omitted on MinGW without breaking the
  `zhttp/bench` directory.
- The harness generates and verifies one deterministic random file per series.
- The harness writes schema-versioned CSV with one row per selected matrix
  entry and repetition.
- Unsupported combinations are logged as `skipped` with machine-readable
  reasons.
- Supported combinations launch one server process tree and one long-lived
  client process per measured row.
- The curl adapter uses native parallel transfers, not one process per request.
- H3 curl rows use `--http3-only` unless strict protocol verification is added
  for `--http3`.
- CPU accounting comes from cgroup v2 `cpu.stat` deltas for separate client and
  server cgroups.
- Server and client logs are preserved and referenced from CSV rows.
- Failures clean up child process groups and cgroups before proceeding when
  `--keep-going` is enabled.
- `zhttp` client rows remain skipped until native multi-request mode exists;
  once implemented, rows use the native mode and parse exact stats.

## Non-goals

- Windows benchmark execution in the initial implementation.
- One-process-per-request fallback for benchmark clients.
- API compatibility for old benchmark prototypes or old option names.
- Kernel page-cache dropping, disk benchmarking, or storage-isolation controls.
- Percentile latency and per-request timing in the initial harness.
- Compression benchmarking.
- Benchmarking HTTP/2.
- Using privileged operations by default.
- In-process X.509 certificate generation in the first implementation.
- Treating console summary as a stable machine-readable interface.

## Options and Open Questions

- **Curl measured validation volume:** Per-transfer `--write-out` gives strong
  status/byte/protocol checks but can create large logs for high request
  counts. Default to readiness validation plus curl exit status for the first
  measured slice; add compact write-out validation where it does not distort
  the workload.
  - ANSWER: no write-out validation
- **Caddy H3 capability detection:** Caddy version/build support can vary. The
  pragmatic approach is to preflight executable presence and then treat H3
  startup/probe failure as `failed`, not `skipped`, because Caddy is expected to
  support H3 in current builds.
  - ANSWER: agreed
- **Self-signed certificate trust model:** If `curl --cacert cert.pem` rejects a
  self-signed leaf certificate on tested curl/OpenSSL combinations, switch to
  generating a local CA cert and signing a localhost server cert. This is an
  implementation detail, not a product ambiguity.
  - ANSWER: agreed
- **zhttp H3 concurrency:** Existing `zhttp/example/zhttp.cc` is a one-shot
  client. If the current H3 client/session API cannot issue concurrent streams
  over one QUIC connection without invasive refactoring, keep zhttp H3 rows
  skipped and implement H1/H1TLS native multi-request first.
  - ANSWER: `zhttp` has been upgraded with `-n` and `-j`
- **Installed tool variance:** External tool behavior differs by build. The
  harness must report skipped/failed rows precisely instead of normalizing away
  those differences.
  - ANSWER: agreed
- **Result append semantics:** Appending to an existing CSV is convenient, but
  mixed option sets can coexist in one file. Include all option values needed to
  interpret each row, and rely on `schema` for future column changes.
  - ANSWER: agreed

## Research References

- curl man page: `--parallel`, `--parallel-max`, `--config`, and
  `--http3-only` behavior: https://curl.se/docs/manpage.html
- Linux cgroup v2 CPU accounting and `cpu.stat` fields:
  https://docs.kernel.org/admin-guide/cgroup-v2.html
- Caddy `file_server` directive:
  https://caddyserver.com/docs/caddyfile/directives/file_server
- Caddy `encode` directive enables compression:
  https://caddyserver.com/docs/caddyfile/directives/encode
- Caddy TLS directive:
  https://caddyserver.com/docs/caddyfile/directives/tls
- darkhttpd command-line behavior and HTTP-only scope:
  https://github.com/emikulic/darkhttpd
