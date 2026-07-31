## Summary

Add a Linux benchmark harness under `zhttp/bench` for static-file HTTP serving
comparisons across HTTP/1.1 cleartext, HTTP/1.1 over TLS, and HTTP/3 over QUIC.
The harness generates one deterministic random response file per benchmark
series, reuses it for every selected matrix row, runs bounded-concurrency GET
workloads, and records elapsed time plus separate client/server CPU usage from
cgroup v2 CPU accounting.

The nominal product matrix is:

| Protocol | Client | Server |
| --- | --- | --- |
| `h1` HTTP/1.1 over TCP, `http:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |
| `h1tls` HTTP/1.1 over TLS/TCP, `https:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |
| `h3` HTTP/3 over QUIC/UDP, `https:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |

Rows that cannot run are first-class results with `status=skipped` and a stable
`skip_reason`. Examples include `darkhttpd` for TLS/H3, missing executables,
curl builds without HTTP/3, and `zhttp` rows until the example client exposes
discarded-output stats suitable for benchmarking. Failed rows are distinct:
if a declared-capable adapter starts but cannot probe or complete, write
`status=failed` and preserve available logs and counters.

The runner is a native C++ program, not a shell script. Use Z Framework where
it fits: `ZtCLI` for option parsing, `ZiFile`/`ZiDir` for filesystem work,
`Ztls::MD<Ztls::SHA256>` for file checksums, `ZtCSV::Writer` plus `ZiFile` for
appendable result CSV, `ZuBox`/`ZuFmt` for formatting, and direct POSIX APIs
for process groups, `fork`/`exec`, signals, `waitpid`, pipes, and cgroup v2
file operations. The benchmark is intentionally Linux-only at first; the
`zhttp/bench` directory may exist on all platforms, but `zhttpbench` is not
built on MinGW.

Research and local API review resolve the key requirements:

- curl's `--parallel`/`--parallel-max` options are the right single-process
  multi-transfer path, and `--http3-only` is required for H3 because fallback
  to earlier HTTP versions would invalidate the row.
- curl config files can carry repeated URL/output entries; use repeated
  `url = ...` plus `output = "/dev/null"` for the curl adapter.
- Do not use curl per-transfer `--write-out` in measured runs. Validate
  readiness body size before measurement and treat curl exit status as the
  measured-run correctness signal.
- Caddy `file_server` serves static files under `root`; Caddy `encode` enables
  compression and must be omitted, not disabled with an unverified `encode off`.
- Caddy H3 requires the QUIC UDP listener and still uses HTTPS/TCP behavior for
  TLS/H1 and advertisement/probing; reserve both TCP and UDP on the selected
  H3 port.
- cgroup v2 `cpu.stat` exposes `usage_usec`, `user_usec`, and `system_usec`;
  moving a process into a cgroup is done by writing its PID to `cgroup.procs`.
- `darkhttpd` is a simple HTTP/1.1 static file server with `--addr`,
  `--port`, and `--log`; it has no native TLS or HTTP/3 support.
- Local `zhttp/example/zhttp.cc` already supports multi-request mode via
  `-n/--requests` and `-j/--jobs`, including H3. It still writes response
  bodies and lacks machine-readable stats, so only `--discard` and `--stats`
  need to be added before enabling `zhttp` client rows.
- Local `ZiCSV::PushFile` appends but always emits a header; `ZiCSV::writeFile`
  rewrites the file. Implement a small result writer using `ZtCSV::Writer` and
  `ZiFile` so headers appear only once.

No `plan.feedback.md` was present when this revision was written.

## Architecture Documentation

### New Components

- `zhttp/bench/Makefile.am` builds `zhttpbench` on non-MinGW platforms and
  links against the same Z libraries used by `zhttp/example`.
- `zhttp/bench/zhttpbench.cc` contains the benchmark runner and adapter
  implementations. Keep this as one translation unit initially; benchmark
  orchestration is not a reusable library yet.
- `zhttp/bench/README.md` documents runtime prerequisites: Linux cgroup v2
  write access, `curl`, `darkhttpd`, `caddy`, `openssl`, and build-tree
  locations for `zhttp`/`zhttpd`.
- `zhttp/example/zhttp.cc` is enhanced with `--discard` and `--stats PATH` so
  the existing `-n/-j` multi-request implementation can serve benchmark rows
  without body-file write amplification.

### Changed Build Integration

- Update `configure.ac` `AC_CONFIG_FILES` to include
  `zhttp/bench/Makefile`.
- Update `zhttp/Makefile.am` from `SUBDIRS = src test example` to
  `SUBDIRS = src test example bench`. Keeping `bench` after `example` lets the
  harness prefer build-tree `zhttp/example/zhttp` and `zhttp/example/zhttpd`.
- `zhttp/bench/Makefile.am` should copy the include and link pattern from
  `zhttp/example/Makefile.am`, with `noinst_PROGRAMS = zhttpbench`.
- Add no new linked third-party dependency for the harness. Call the `openssl`
  executable for certificate generation.

### Processes and Threads

- The harness process orchestrates the run; it does not create benchmark worker
  threads.
- Each server is launched as a foreground child in a new process group so the
  full server tree can be terminated.
- Each client workload is launched as a foreground child process. The measured
  client CPU cgroup includes all client processes for that row.
- For cgroup placement, a child pauses after `fork` until the parent writes the
  child PID to the target `cgroup.procs`; then the child `exec`s. This avoids
  charging meaningful child work to the parent and avoids cgroup assignment
  races.
- `zhttp` client concurrency remains internal to `zhttp`; the harness still
  sees one client process per row.

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
| `--file-name NAME` | `bench.dat` | Relative URL and filesystem leaf name. Reject names with `/`, `\`, `.`, `..`, or empty components. |
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
  parseCompletion(logs/stats) -> completed/failed/bytes/protocol
```

Adapters return structured status. Only `ResultWriter` writes CSV.

### Data Flows

1. Parse options with `ZtCLI`.
2. Resolve tool paths and dynamic capabilities.
3. Create or verify the benchmark series directory:

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
    <run-id>.zhttp.stats
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

- Use monotonic time for readiness polling, timeouts, and measured elapsed
  time.
- Readiness probes retry with short sleeps until success or `--startup-timeout`.
- Per-run timeout kills the client process group first, then the server group,
  and records a failed row.
- Optional warmup is excluded from measured request counters and CPU deltas.

### Network Programming

- Port allocation must consider both TCP and UDP for `h3`; use the same port
  number for HTTPS TCP and QUIC UDP when testing Caddy or `zhttpd`.
- Binding and probing remain loopback-only.
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
without launching servers or clients.

Files:

- Add `zhttp/bench/Makefile.am`.
- Add `zhttp/bench/zhttpbench.cc`.
- Add `zhttp/bench/README.md`.
- Update `zhttp/Makefile.am`.
- Update `configure.ac`.

Implementation details:

- Define `Options`, `ProtocolSpec`, `ToolSpec`, `Combination`, `RunID`,
  `Paths`, `Result`, `ResultWriter`, and small enum tables for protocols,
  clients, servers, and statuses.
- Use `ZtCLI` for command-line parsing, matching the pattern in
  `zhttp/example/zhttp.cc` and `zhttp/example/Zhttpd.hh`.
- Implement comma-list parsing for protocol/client/server filters; reject
  unknown IDs up front.
- Implement `Result` as a `ZtStruct` mapped for CSV.
- Implement `ResultWriter` with `ZtCSV::Writer<Result>` and `ZiFile`:
  inspect whether the output file exists and has non-zero size, write the
  header only for a new/empty file, then append rows.
- Implement static capability skips:
  - `darkhttpd` supports only `h1`.
  - `caddy` supports `h1`, `h1tls`, and expected `h3`.
  - `zhttpd` supports `h1`, `h1tls`, and `h3`.
  - `curl` supports `h1`, `h1tls`, and dynamically detected `h3`.
  - `zhttp` rows are initially skipped unless `zhttp --help` advertises
    `--discard` and `--stats`.
- Implement tool path resolution for `PATH` and build-tree defaults. Missing
  executables produce skipped rows for dependent combinations.
- Implement `--print-commands` as a parsed no-op until command adapters exist.

Validation:

- Run the repository configure/autoreconf path after adding the Makefile.
- Build with `make -C zhttp/bench zhttpbench`.
- Run:

```sh
zhttpbench --protocol h3 --server darkhttpd --client curl --requests 1
```

- Verify one skipped CSV row with stable columns and no duplicate header after
  a second append.

### Phase 2: Work Directory, File Generation, Checksum, Certificate, and Cgroup Preflight

Add the shared benchmark-series state needed before real runs execute. This
phase still does not need a successful HTTP benchmark row.

Implementation details:

- Create `Paths` helpers for `www`, `tls`, `caddy`, `workload`, `logs`, and
  `tmp` using `ZiFile`/`ZiDir` facilities and direct POSIX APIs where local
  wrappers do not cover the operation.
- Validate `--file-name` as a relative leaf name. Reject empty names, `/`,
  backslash, `.`, and `..`.
- Implement deterministic file generation:
  - Use a small fixed PRNG such as splitmix64 seeded from `--seed`.
  - Write with `ZiFile` in chunks; avoid fixed large stack arrays.
  - Use `ZtLocalArray<uint8_t, 64<<10>` or comparable scratch storage.
  - Update `Ztls::MD<>` while streaming bytes.
  - Store checksum as lowercase hex in sidecar metadata.
- Implement file verification:
  - Verify size with `ZiFile`/`stat`.
  - Recompute SHA-256 and compare with metadata.
  - Fatal on mismatch unless `--regenerate-file` is supplied.
- Generate a local CA-style self-signed certificate using the command pattern
  already documented in `zhttp/README.md`:

```text
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj /CN=localhost \
  -addext basicConstraints=critical,CA:TRUE \
  -addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign \
  -addext subjectAltName=DNS:localhost,IP:127.0.0.1 \
  -keyout <key.pem> -out <cert.pem>
```

- If certificate generation fails, skip TLS/H3 rows that require the cert with
  `skip_reason=certificate generation failed`; cleartext H1 rows still run.
- Implement cgroup v2 preflight:
  - Locate the cgroup v2 mount by parsing `/proc/self/mountinfo`.
  - Determine the current cgroup path from `/proc/self/cgroup`.
  - Create a benchmark parent cgroup and `client`/`server` probe cgroups.
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
- Verify cgroup preflight failure is reported before any server/client launch.

### Phase 3: Process Control, Cgroup Accounting, and First Real Row (`h1` + `darkhttpd` + `curl`)

Implement the first end-to-end vertical slice using the least complex external
participants: cleartext HTTP/1.1, darkhttpd, and curl.

Implementation details:

- Add a `Process` helper:
  - Build argv as Z arrays/strings and convert to `char *const *` only at the
    fork/exec boundary.
  - Redirect stdout/stderr to log files opened before `exec`.
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

- Do not use daemon mode.
- Implement curl h1 adapter:
  - Generate a curl config file with the target URL repeated exactly
    `--requests` times and `output = "/dev/null"` paired with each URL.
  - Run one curl process:

```text
curl --http1.1 --fail --silent --show-error --parallel \
  --parallel-max <concurrency> --config <curl-workload-file>
```

  - Capture stderr/stdout to the client log.
  - Treat exit status `0` as all requests completed and non-zero as failed.
- Implement readiness probe for h1:
  - Use curl when available:

```text
curl --http1.1 --fail --silent --output /dev/null \
  --write-out %{size_download} <url>
```

  - Require downloaded byte count to equal the generated file size.
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

```sh
zhttpbench --protocol h1 --server darkhttpd --client curl \
  --requests 10 --concurrency 2 --work-dir /tmp/zhttpbench
```

- Verify `status=ok`, `requests_completed=10`, non-empty elapsed and CPU
  fields, and server/client logs are present.

### Phase 4: zhttpd H1, Shared Correctness Checks, and Failure Rows

Extend the real-run slice to the in-tree `zhttpd` cleartext server before TLS
or H3 complexity is introduced. Also harden failure recording while the setup
is still simple.

Implementation details:

- Resolve build-tree `zhttp/example/zhttpd` before `PATH`.
- Add zhttpd h1 command:

```text
zhttpd <www-root> --http --addr 127.0.0.1 --port <port> \
  --no-server-id --timeout 0 --log <server-log>
```

- Factor server readiness and termination so darkhttpd and zhttpd share the
  lifecycle.
- Preserve `server_log`, `client_log`, `error`, elapsed time if available, and
  partial CPU deltas if measurement started.
- Add explicit startup failure and timeout paths that still clean process
  groups and cgroups.

Validation:

- Run h1 curl against `darkhttpd` and `zhttpd` with low request counts.
- Use a busy port or broken command path to verify failed rows and cleanup.

### Phase 5: Caddy H1/H1TLS and TLS Correctness

Add Caddy for cleartext and TLS, then add TLS mode for zhttpd. This delivers
the curl HTTP/1.1 rows except unsupported `darkhttpd` TLS.

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

- Do not include `encode`.
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
- Do not use `curl -k` in measured or readiness rows.

Validation:

- Run `h1` and `h1tls` with `curl` against Caddy and zhttpd.
- Verify certificate mismatch failures are not silently ignored.

### Phase 6: HTTP/3 Rows for curl, Caddy, and zhttpd

Add H3 support only after the H1/TLS lifecycle is stable.

Implementation details:

- Dynamic curl preflight:
  - Run `curl --version` and require an HTTP/3 feature token before enabling H3.
  - Run `curl --help all` or a cheap probe to confirm `--http3-only`,
    `--parallel`, and `--parallel-max` are accepted.
  - If `--http3-only` is unavailable but `--http3` is available, keep H3 rows
    skipped. Silent fallback must never count as H3.
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
    row failed because Caddy was considered capable after executable preflight.

Validation:

- Run low-count `h3` rows for `curl+caddy` and `curl+zhttpd`.
- Verify UDP/TCP port cleanup after failures.

### Phase 7: zhttp Client Benchmark Stats and Rows

Enable `zhttp` client rows by enhancing the existing multi-request client with
discard and stats support. Do not replace the existing `-n/-j` implementation
and do not use one process per request.

Implementation details:

- Extend `Options` in `zhttp/example/zhttp.cc`:
  - `--discard`
  - `--stats PATH`
- Preserve current one-shot behavior and current `-n/--requests` plus
  `-j/--jobs` behavior.
- In `ResponseSink::body`, count `bodyBytes` regardless of `--discard`; skip
  opening/writing `bodyFile` when discard is enabled.
- At process exit, emit exact machine-readable stats when `--stats` is set:
  - `requests`
  - `requests_completed`
  - `requests_failed`
  - `bytes_received`
  - `protocol` or requested mode (`h1`/`h3`)
  - optionally per-request status counts if cheap to expose
- Prefer a small CSV or key-value stats file over parsing human logs.
- Add harness detection:
  - Run `zhttp --help` and require `--requests`, `--jobs`, `--discard`, and
    `--stats` before enabling zhttp rows.
  - Parse the stats file after completion.
- zhttp h1 command:

```text
zhttp -n <requests> -j <concurrency> --discard --stats <stats-path> \
  -o <tmp-output-base> http://127.0.0.1:<port>/<file-name>
```

- zhttp h1tls command:

```text
zhttp -c <cert.pem> -n <requests> -j <concurrency> \
  --discard --stats <stats-path> -o <tmp-output-base> \
  https://localhost:<port>/<file-name>
```

- zhttp h3 command:

```text
zhttp --http3 -c <cert.pem> -n <requests> -j <concurrency> \
  --discard --stats <stats-path> -o <tmp-output-base> \
  https://localhost:<port>/<file-name>
```

Validation:

- Extend `zhttp/test/ZhttpMultiRequestTest.cc` to cover:
  - `--discard` does not create response files;
  - `--stats` reports exact completed/failed/bytes;
  - `-n 4 -j 2 --discard --stats` works against zhttpd;
  - H3 multi-request stats work when local QUIC support is available.
- Run matrix rows for `zhttp` client only after stats are reliable.

### Phase 8: Reporting, Polish, and Documentation

Finalize human-facing output and runtime documentation after the core rows
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

- Run a reduced full matrix:

```sh
zhttpbench --requests 5 --concurrency 2
```

- Inspect CSV plus summary.
- Run build/test targets for touched modules.

## Code References to Impacted Code

- `configure.ac:233` - add `zhttp/bench/Makefile` to configured Makefile list.
- `zhttp/Makefile.am:2` - add `bench` to `SUBDIRS` after `example`.
- `zhttp/example/Makefile.am:1` - copy include/link patterns for
  `zhttp/bench/Makefile.am`.
- `zhttp/example/zhttp.cc:29` - existing `Options` already include
  `-n/--requests` and `-j/--jobs`; add `--discard` and `--stats`.
- `zhttp/example/zhttp.cc:77` - `outputPath()` currently creates per-request
  body paths for `requests > 1`; discard mode should bypass body file creation.
- `zhttp/example/zhttp.cc:577` - `ResponseSink::body()` currently opens and
  writes body files; modify it to count bytes while discarding output.
- `zhttp/example/zhttp.cc:1422` - `runH1Pool()` already implements multi-request
  H1/H1TLS client mode.
- `zhttp/example/zhttp.cc:1471` - `runH3Multi()` already implements multi-stream
  H3 client mode.
- `zhttp/example/zhttp.cc:1712` - `main()` dispatches one-shot, H1 pool,
  H1TLS pool, and H3 multi modes; add final stats emission here.
- `zhttp/example/Zhttpd.hh:68` - existing `Zhttpd::Options` and `ZtCLI` mapping
  confirm `--http`, `--https`, `--http3`, `--cert`, `--key`, `--timeout`,
  `--log`, and `--no-server-id`.
- `zhttp/README.md:54` - local certificate command already uses SAN and
  CA-style self-signed options suitable for curl `--cacert`.
- `zhttp/test/ZhttpMultiRequestTest.cc:51` - existing multi-request CLI tests
  should be extended for `--discard` and `--stats`.
- `zhttp/test/ZhttpAppTest.cc:55` - existing zhttp/zhttpd transport integration
  patterns for H1, TLS, and H3.
- `zhttp/test/ZhttpDarkhttpdCompatTest.cc:59` - existing darkhttpd/curl
  availability and compatibility pattern.
- `zt/src/ZtCLI.hh:40` - CLI parsing framework to use for `zhttpbench`.
- `zt/src/ZtCSV.hh:12` - CSV formatting and quoting support for result rows.
- `zi/src/ZiCSV.hh:43` - `PushFile` appends but writes a header on construction;
  avoid it for benchmark append semantics.
- `zi/test/ZiCSVTest.cc:75` - local file-backed CSV read/write examples.
- `ztls/src/ZtlsMD.hh:27` - SHA-256 digest wrapper for file checksum.
- `zi/src/ZiFile.hh` and `zi/src/ZiFile.cc` - file/path helpers for work
  directory, generated data, logs, metadata, and cleanup.
- `zi/test/ZiDaemonTest.cc:169` - local fork/exec/wait pattern for POSIX child
  process tests.

## Detailed Test Plan

- Build tests:
  - Re-run autoreconf/configure after adding `zhttp/bench/Makefile.am`.
  - `make -C zhttp/bench zhttpbench`.
  - `make -C zhttp/example zhttp zhttpd` after modifying `zhttp` client.
- Parser/config tests:
  - Unknown protocol/client/server list values fail fast.
  - Empty or invalid `--file-name` fails fast.
  - Missing external tools produce skipped rows, not crashes.
  - CSV header appears once when appending.
- Generated file tests:
  - Same seed and size produce the same SHA-256.
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
  - Existing one-shot behavior remains compatible.
  - Existing `-n/-j` behavior remains compatible.
  - `--requests 1 --discard --stats PATH` works for H1.
  - `--requests N --jobs M --discard --stats PATH` reports exact
    completed/failed/bytes.
  - `--discard` does not create response body files.
  - H3 zhttp stats work with `--http3` when local QUIC support is available.
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
- H3 curl rows use `--http3-only`.
- CPU accounting comes from cgroup v2 `cpu.stat` deltas for separate client and
  server cgroups.
- Server and client logs are preserved and referenced from CSV rows.
- Failures clean up child process groups and cgroups before proceeding when
  `--keep-going` is enabled.
- `zhttp` client rows use existing native `-n/-j` multi-request mode plus new
  `--discard` and `--stats`; no one-process-per-request fallback is used.

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

There are no unresolved open questions for the initial implementation plan.
Resolved decisions:

- Curl measured validation: no per-transfer write-out validation in measured
  runs; use readiness body-size validation and curl exit status.
- Caddy H3 capability: if Caddy is present, treat H3 startup/probe failure as
  `failed`, not `skipped`.
- Certificate trust: generate a CA-style self-signed localhost certificate as
  already documented locally and pass it via curl/zhttp CA options.
- zhttp client: use existing `-n/--requests` and `-j/--jobs`; add only
  `--discard` and `--stats` before enabling rows.
- Installed tool variance: report precise skipped/failed rows instead of hiding
  tool differences.
- Result append semantics: include option values needed to interpret each row
  and write the CSV header only once.

## Research References

- curl man page for `--parallel`, `--parallel-max`, `--config`, and
  `--http3-only`: https://curl.se/docs/manpage.html
- Linux cgroup v2 CPU accounting and `cpu.stat` fields:
  https://docs.kernel.org/admin-guide/cgroup-v2.html
- Caddy global HTTP/3/QUIC behavior:
  https://caddyserver.com/docs/caddyfile/options
- Caddy `file_server` directive:
  https://caddyserver.com/docs/caddyfile/directives/file_server
- Caddy `encode` directive:
  https://caddyserver.com/docs/caddyfile/directives/encode
- Caddy `tls` directive:
  https://caddyserver.com/docs/caddyfile/directives/tls
- darkhttpd command-line behavior and HTTP-only scope:
  https://github.com/emikulic/darkhttpd
