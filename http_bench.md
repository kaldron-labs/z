# zhttp HTTP Benchmark Harness Design

## Goal

Add a benchmark harness under `zhttp/bench` that serves one generated random
file through each supported protocol/client/server combination, runs a fixed
request workload with bounded concurrency, and logs elapsed time plus CPU time
used separately by the client side and server side.

The same test file is generated once per benchmark series and reused for every
run so that comparisons across clients and servers are not affected by response
payload differences.

Target matrix:

| Protocol | Client | Server |
| --- | --- | --- |
| H1 over TCP, `http:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |
| H1 over TLS/TCP, `https:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |
| H3 over QUIC, `https:` | `curl`, `zhttp` | `darkhttpd`, `caddy`, `zhttpd` |

The full matrix has 18 nominal combinations. The harness records skipped
combinations with a reason when a participant cannot support the selected
protocol, for example `darkhttpd` with HTTPS/H3 or a `curl` binary built without
HTTP/3 support.

## Deliverables

The initial implementation should add:

- `zhttp/bench/Makefile.am`: builds the harness when `zhttp` examples/tests are
  enabled.
- `zhttp/bench/zhttpbench.cc`: the benchmark runner.
- `http_bench.md`: this design.

The runner is a native C++ program rather than a shell script. That keeps
process lifecycle, request scheduling, CPU accounting, and result writing
deterministic and avoids measuring shell wrappers as part of client CPU.

## Command Line

Proposed usage:

```sh
zhttpbench [OPTION]...
```

Core options:

| Option | Default | Meaning |
| --- | ---: | --- |
| `--requests N` | `10000` | Total successful GET requests attempted per run. |
| `--concurrency N` | `10` | Maximum in-flight client requests per run. |
| `--file-size N` | `1048576` | Size in bytes of the generated response body. |
| `--seed N` | `1` | Seed for deterministic pseudo-random file contents. |
| `--work-dir PATH` | `zhttp/bench/run` | Directory for generated data, configs, logs, certs, and results. |
| `--file-name NAME` | `bench.dat` | Relative URL/file name served by all servers. |
| `--out PATH` | `results.csv` | Append `ZtCSV` CSV result rows here. |
| `--protocol LIST` | `h1,h1tls,h3` | Comma-separated protocol filter. |
| `--client LIST` | `curl,zhttp` | Comma-separated client filter. |
| `--server LIST` | `darkhttpd,caddy,zhttpd` | Comma-separated server filter. |
| `--port-base N` | `18080` | First candidate TCP/UDP port. |
| `--repetitions N` | `1` | Repeat each supported combination this many times. |
| `--warmup N` | `0` | Optional warmup requests before CPU sampling begins. |
| `--timeout SEC` | `60` | Per-run wall-clock timeout. |
| `--keep-going` | enabled | Continue after failures and log failed runs. |
| `--print-commands` | disabled | Emit exact server and client commands before each run. |

Tool path overrides:

| Option | Default lookup |
| --- | --- |
| `--curl PATH` | `PATH` lookup for `curl` |
| `--caddy PATH` | `PATH` lookup for `caddy` |
| `--darkhttpd PATH` | `PATH` lookup for `darkhttpd` |
| `--zhttp PATH` | build-tree `zhttp/example/zhttp`, then `PATH` |
| `--zhttpd PATH` | build-tree `zhttp/example/zhttpd`, then `PATH` |

Example:

```sh
zhttpbench --requests 10000 --concurrency 10 --file-size 1048576 \
  --work-dir /tmp/zhttpbench --out /tmp/zhttpbench/results.csv
```

## Work Directory Layout

The harness owns a single work directory for a complete benchmark series:

```text
run/
  www/
    bench.dat
  tls/
    cert.pem
    key.pem
  caddy/
    Caddyfile
  logs/
    <run-id>.server.log
    <run-id>.client.log
  tmp/
  results.csv
```

`www/bench.dat` is generated before the first run. If the file already exists,
the harness verifies its size and SHA-256 before reuse. A size or checksum
mismatch is fatal unless `--regenerate-file` is explicitly supplied.

## Test File Generation

The response file should be deterministic random data:

1. Seed a small fixed PRNG from `--seed`.
2. Stream bytes to `www/<file-name>` until `--file-size` is reached.
3. Compute and store SHA-256 in the series metadata.
4. Reuse the same file for all repetitions and matrix combinations.

Deterministic generated data gives reproducibility, avoids external entropy
cost during the benchmark, and prevents compression-friendly payloads from
distorting results. The servers should not be configured for compression.

## Protocol Definitions

The harness uses explicit protocol records:

| ID | URL scheme | Transport | URL host | Notes |
| --- | --- | --- | --- | --- |
| `h1` | `http` | TCP | `127.0.0.1` | HTTP/1.1 cleartext. |
| `h1tls` | `https` | TLS/TCP | `localhost` | HTTP/1.1 over TLS with generated localhost cert. |
| `h3` | `https` | QUIC/UDP | `localhost` | HTTP/3 over QUIC with generated localhost cert. |

Use `localhost` for TLS/H3 so the generated certificate can include
`DNS:localhost` and `IP:127.0.0.1`. Bind servers to `127.0.0.1` unless a
specific override is added later.

## Capability Model and Skip Rules

Each protocol, client, and server has a capability table. A run is supported
only if all three participants support the selected protocol.

Initial static capabilities:

| Participant | `h1` | `h1tls` | `h3` |
| --- | --- | --- | --- |
| `darkhttpd` server | yes | no | no |
| `caddy` server | yes | yes | yes |
| `zhttpd` server | yes | yes | yes |
| `curl` client | yes | yes | detected |
| `zhttp` client | yes | yes | yes |

Dynamic checks refine this table:

- Missing executable: skip all combinations using that executable.
- `curl --version` must contain HTTP/3 support before enabling `curl`/`h3`.
- `curl` must support `--parallel` and `--parallel-max`; otherwise mark the
  `curl` client unavailable for this harness because per-request process
  spawning would not exercise curl's connection reuse or multiplexing path.
- `zhttp` must expose the native multi-request benchmark options described
  below before enabling `zhttp` client rows.
- A server startup failure marks only that specific combination failed, not
  skipped, because the capability table said it should work.

Skipped records are written to the result log with `status` set to `skipped`
and a machine-readable `skip_reason`.

## Server Adapters

Every server adapter exposes:

- `supports(protocol)`.
- `command(protocol, port, wwwRoot, cert, key, logPath)`.
- `ready_probe(protocol, port, url)`.
- `shutdown()`.

Server processes are launched in the foreground, in a new process group, and in
the run's server CPU accounting domain.

### darkhttpd

Supported protocol:

- `h1`.

Command shape:

```sh
darkhttpd <www-root> --addr 127.0.0.1 --port <port>
```

The harness does not use daemon mode. Readiness is a successful HTTP GET for the
test file.

### caddy

Supported protocols:

- `h1`.
- `h1tls`.
- `h3` when the installed Caddy supports HTTP/3.

The harness writes a per-run `Caddyfile`.

Cleartext HTTP:

```text
http://127.0.0.1:<port> {
	root * <www-root>
	file_server
	encode off
	log {
		output file <log-path>
	}
}
```

TLS/H3:

```text
https://localhost:<port> {
	tls <cert.pem> <key.pem>
	root * <www-root>
	file_server
	encode off
	log {
		output file <log-path>
	}
}
```

Command shape:

```sh
caddy run --config <Caddyfile> --adapter caddyfile
```

For H3, Caddy must listen on UDP and TCP for the selected HTTPS port. The
harness uses the same port number for TCP and UDP and probes with an H3-capable
client.

### zhttpd

Supported protocols:

- `h1`.
- `h1tls`.
- `h3`.

Command shapes:

```sh
zhttpd <www-root> --http --addr 127.0.0.1 --port <port> --no-server-id
zhttpd <www-root> --https --addr 127.0.0.1 --port <port> \
  --cert <cert.pem> --key <key.pem> --no-server-id
zhttpd <www-root> --http3 --addr 127.0.0.1 --port <port> \
  --cert <cert.pem> --key <key.pem> --no-server-id
```

Use `--timeout 0` if idle timeout noise shows up in benchmark traces.

## Client Adapters

Every client adapter exposes:

- `supports(protocol)`.
- `prepare_workload(protocol, url, requests, concurrency)`.
- `command(protocol, workloadPath, outputPath, certPath)`.
- `success(exitStatus, completed, failed, bytes)`.

The harness should not default to one process per request. That measures process
startup and prevents connection reuse, HTTP/1.1 keep-alive, HTTP/1.1 pipelining
where supported, and HTTP/3 concurrent streams from being exercised. Each client
adapter should use the client's native multi-request mode when available.

The workload contract is:

- `--requests` is the exact number of GET requests attempted.
- `--concurrency` is the maximum number of in-flight requests.
- All requests target the same generated file URL.
- Response bodies are fully consumed and discarded.
- The measured client CPU domain includes all processes used by the adapter.

Both supported clients must run one long-lived process with native multi-request
scheduling. This keeps the comparison focused on comparable client capabilities:
bounded concurrency, connection reuse, and protocol-level multiplexing or
stream concurrency where available.

### curl

The `curl` adapter should use one curl process for the whole run. Generate a
curl config file that repeats the target URL exactly `--requests` times, then
run curl with `--parallel` and `--parallel-max <concurrency>`. This uses curl's
multi-transfer path, allows connection reuse within the invocation, and lets
HTTP/3 use concurrent streams when curl and the server negotiate H3.

The workload file should be a curl config file, not `--url @file`, so the
harness can pair every repeated URL with `output = "/dev/null"` without relying
on remote-name output behavior.

HTTP:

```sh
curl --http1.1 --fail --silent --show-error --parallel \
  --parallel-max <concurrency> --config <curl-workload-file>
```

HTTPS:

```sh
curl --http1.1 --fail --silent --show-error --cacert <cert.pem> \
  --parallel --parallel-max <concurrency> --config <curl-workload-file>
```

H3:

```sh
curl --http3-only --fail --silent --show-error --cacert <cert.pem> \
  --parallel --parallel-max <concurrency> --config <curl-workload-file>
```

If `--http3-only` is unavailable but `--http3` is available, the harness may use
`--http3` only when it can verify from curl's write-out data that HTTP/3 was
actually negotiated. Silent fallback to HTTP/1.1 must not be counted as an H3
run.

### zhttp

The `zhttp` example client should be improved before using it for default
benchmark results. It needs a native multi-request benchmark mode with:

- `--requests N`;
- `--concurrency N`;
- `--discard` or equivalent output-to-null behavior;
- one process for the whole workload;
- HTTP/1.1 keep-alive connection reuse;
- optional HTTP/1.1 pipelining depth when the server/protocol mode supports it;
- HTTPS connection reuse over one or more TLS connections;
- HTTP/3 concurrent request streams over a single QUIC connection where possible;
- exact completed/failed request counts for the harness.

HTTP:

```sh
zhttp --requests <requests> --concurrency <concurrency> --discard \
  http://127.0.0.1:<port>/<file-name>
```

HTTPS:

```sh
zhttp -c <cert.pem> --requests <requests> --concurrency <concurrency> \
  --discard https://localhost:<port>/<file-name>
```

H3:

```sh
zhttp --http3 -c <cert.pem> --requests <requests> \
  --concurrency <concurrency> --discard \
  https://localhost:<port>/<file-name>
```

Until this mode exists, mark `zhttp` benchmark rows as skipped with
`skip_reason` set to `zhttp client lacks native multi-request benchmark mode`.
Do not silently fall back to per-request `zhttp` processes for default results.

## Run Lifecycle

For each repetition and supported matrix row:

1. Pick an unused port starting at `--port-base`.
2. Create run IDs and log paths.
3. Start the server in its accounting domain.
4. Probe readiness until success or startup timeout.
5. Run optional warmup requests; warmup is not included in measured request
   counts.
6. Reset CPU counters for the client and server accounting domains.
7. Start the wall-clock timer.
8. Run the client adapter workload until `--requests` complete or the adapter
   reports failure.
9. Stop the wall-clock timer.
10. Read client and server CPU counters.
11. Terminate the server process group with `SIGTERM`, then `SIGKILL` after a
    short grace period.
12. Write one result record.

If a request fails, the run is marked `failed`. The runner still drains or
terminates outstanding children, shuts down the server, records CPU/time data
available so far, and continues to the next combination when `--keep-going` is
enabled.

## CPU Accounting

Under Linux, cgroup v2 CPU accounting is required. The harness must validate
this before starting the matrix and abort up front if it cannot create writable
benchmark cgroups, move child processes into them, or read `cpu.stat`.

The preflight should:

- Locate the active cgroup v2 mount.
- Create a temporary parent cgroup for the benchmark.
- Create separate temporary child cgroups for client and server accounting.
- Move a short-lived probe child into one of those cgroups.
- Read `cpu.stat` and verify `usage_usec`, `user_usec`, and `system_usec` are
  available.
- Remove the temporary cgroups.

Measured runs use cgroup v2 CPU accounting:

- Create one cgroup for the server process tree.
- Create one cgroup for all client request processes in the measured run.
- Move each child PID into the appropriate cgroup immediately after fork and
  before exec.
- Read `cpu.stat` before and after the measured section.
- Log `usage_usec`, `user_usec`, and `system_usec` deltas for client and server.

This captures:

- all client request processes;
- server helper processes, if any;
- all threads;
- CPU consumed while requests are concurrent.

There is no `/proc` or `getrusage()` fallback for Linux benchmark results. A
run without cgroup accounting is a setup error, not a degraded benchmark mode.

## Timing and Counters

Use a monotonic clock for elapsed time. Record:

- `started_at_unix_ns`.
- `elapsed_ns`.
- `requests_requested`.
- `requests_completed`.
- `requests_failed`.
- `concurrency`.
- `bytes_expected`.
- `bytes_received` when the adapter can report it.
- `client_cpu_user_usec`.
- `client_cpu_system_usec`.
- `client_cpu_total_usec`.
- `server_cpu_user_usec`.
- `server_cpu_system_usec`.
- `server_cpu_total_usec`.

Derived metrics can be printed in the console summary, but the raw log should
remain sufficient to recompute:

- requests per second;
- MiB per second;
- client CPU usec per request;
- server CPU usec per request;
- total CPU usec per MiB.

## Result Format

Write CSV using `ZtCSV`. The file has one header row followed by one row per
matrix entry and repetition. The column set is stable across `ok`, `skipped`,
and `failed` rows; fields that do not apply to skipped or failed runs are left
blank.

Columns:

```text
schema,status,run_id,repetition,protocol,client,server,url,
requests_requested,requests_completed,requests_failed,concurrency,
file_name,file_size,file_sha256,elapsed_ns,
client_cpu_user_usec,client_cpu_system_usec,client_cpu_total_usec,
server_cpu_user_usec,server_cpu_system_usec,server_cpu_total_usec,
cpu_accounting,server_log,client_log,skip_reason,error
```

Successful run:

```csv
schema,status,run_id,repetition,protocol,client,server,url,requests_requested,requests_completed,requests_failed,concurrency,file_name,file_size,file_sha256,elapsed_ns,client_cpu_user_usec,client_cpu_system_usec,client_cpu_total_usec,server_cpu_user_usec,server_cpu_system_usec,server_cpu_total_usec,cpu_accounting,server_log,client_log,skip_reason,error
1,ok,h1-curl-zhttpd-r1,1,h1,curl,zhttpd,http://127.0.0.1:18080/bench.dat,10000,10000,0,10,bench.dat,1048576,...,1234567890,100000,50000,150000,80000,30000,110000,cgroup-v2,logs/h1-curl-zhttpd-r1.server.log,logs/h1-curl-zhttpd-r1.client.log,,
```

Skipped run:

```csv
schema,status,run_id,repetition,protocol,client,server,url,requests_requested,requests_completed,requests_failed,concurrency,file_name,file_size,file_sha256,elapsed_ns,client_cpu_user_usec,client_cpu_system_usec,client_cpu_total_usec,server_cpu_user_usec,server_cpu_system_usec,server_cpu_total_usec,cpu_accounting,server_log,client_log,skip_reason,error
1,skipped,h3-curl-darkhttpd-r1,1,h3,curl,darkhttpd,,,,,,,,,,,,,,,,,,,server darkhttpd does not support h3,
```

Failed run:

```csv
schema,status,run_id,repetition,protocol,client,server,url,requests_requested,requests_completed,requests_failed,concurrency,file_name,file_size,file_sha256,elapsed_ns,client_cpu_user_usec,client_cpu_system_usec,client_cpu_total_usec,server_cpu_user_usec,server_cpu_system_usec,server_cpu_total_usec,cpu_accounting,server_log,client_log,skip_reason,error
1,failed,h3-curl-caddy-r1,1,h3,curl,caddy,,,,,,,,,,,,,,,,,logs/h3-curl-caddy-r1.server.log,,,server readiness probe timed out
```

## Readiness and Correctness Checks

Before measuring:

- Wait for the server port to accept the selected transport.
- Fetch the generated file once with a known-good client for that protocol.
- Verify HTTP status is 200.
- Verify the response body length equals `--file-size`.

During measured requests:

- Treat any non-zero client exit as request failure.
- For adapters that can expose response code and byte count cheaply, require
  status 200 and exact byte count.
- Disable caches outside the server process. Do not add random query strings,
  because the goal is static-file serving of the same object.

The OS page cache is intentionally left warm after the first combination unless
a future `--drop-caches` option is added. Dropping kernel caches would require
elevated privileges and would measure storage effects rather than HTTP serving
overhead.

## TLS Certificate

Generate one temporary localhost certificate per series:

```sh
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj /CN=localhost \
  -addext basicConstraints=critical,CA:TRUE \
  -addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign \
  -addext subjectAltName=DNS:localhost,IP:127.0.0.1 \
  -keyout tls/key.pem -out tls/cert.pem
```

The harness may call `openssl` initially. A later implementation can replace
that with in-process certificate generation if required.

## Console Summary

After all runs, print a compact table sorted by protocol, server, client:

```text
protocol server    client status   req/s   MiB/s client_cpu_us/req server_cpu_us/req
h1       zhttpd    curl   ok      12345 12345.0             15.0              11.0
h3       darkhttpd curl   skipped server darkhttpd does not support h3
```

The console summary is for humans only. The CSV file is the stable artifact.

## Build Integration

Add `zhttp/bench` to `zhttp/Makefile.am` subdirectories once the harness exists:

```make
SUBDIRS = src example test bench
```

`zhttp/bench/Makefile.am` should build this `zhttpbench` harness only for
non-Windows builds, while leaving the directory available for future benchmark
programs that may support Windows:

```make
if !MINGW
bin_PROGRAMS = zhttpbench
endif
```

`zhttpbench` should use the same compiler settings as the rest of the tree and
link against the Z libraries needed for:

- CLI parsing;
- process management helpers if available;
- file I/O;
- hashing;
- logging.
- CSV serialization with `ZtCSV`.

The first implementation can use direct POSIX APIs for `fork`, `exec`, `wait4`,
signals, and cgroup file writes. The build system should skip this harness
under Windows; it should not skip the entire `zhttp/bench` directory.

## Implementation Structure

Suggested internal types:

```text
Options
ProtocolSpec
ToolSpec
ServerAdapter
ClientAdapter
Combination
RunContext
CpuAccount
Process
Scheduler
ResultWriter
```

The matrix should be data-driven:

```text
for repetition in repetitions:
  for protocol in protocols:
    for server in servers:
      for client in clients:
        run_or_skip(protocol, server, client)
```

Adapters should not write result records directly. They return structured
status to the runner, and `ResultWriter` is the only component that serializes
CSV through `ZtCSV`. That keeps column changes localized.

## Open Follow-Ups

- Add a `--shuffle` option if fixed matrix order is shown to bias results
  through thermal state or page-cache effects.
- Add percentile latency only after the basic throughput and CPU accounting
  path is stable; per-request timing for process-based clients will otherwise
  mostly measure process scheduling and startup.
