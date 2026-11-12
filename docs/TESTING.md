# Testing Z

See [DEPENDENCIES.md](DEPENDENCIES.md) for build requirements and optional test
peers. Module `test/`, `itest/`, `interop/`, and `bench/` directories contain
unit tests, integration tests, interoperability fixtures, and benchmarks.
Their READMEs describe individual entry points and coverage.

## Build and run

From the repository root, after installing dependencies:

```sh
./z.config -c -d -G -P /opt/z
make -j
make test
```

This example enables debug assertions and SQLite, and disables GTK and
PostgreSQL. Omit `-G` or `-P` to exercise those components with their dependencies
installed. `./z.config -h` lists all switches. `make test` builds first, traverses
the enabled modules, and sets `ZI_LOGDIR` to the build tree's `logs` directory.
Optional interoperability and benchmark targets have their own prerequisites;
a passing default suite does not imply that every optional peer was exercised.

Run narrower suites or individual binaries from the configured build tree:

```sh
make -C zm/test test
make -C zhttp/itest test
./zt/test/ZtArrayTest
```

Use the source-tree libtool wrappers when debugging, never `.libs` binaries:

```sh
./libtool exec gdb --args ./zt/test/ZtArrayTest
./libtool exec valgrind --leak-check=full ./zt/test/ZtArrayTest
```

For TLS Memcheck runs with the dependency/tool pair documented in
`openssl.valgrind.supp`, enable that reviewed suppression explicitly:

```sh
./libtool exec valgrind --error-exitcode=99 --leak-check=full \
  --errors-for-leak-kinds=definite,indirect \
  --suppressions=openssl.valgrind.supp ./ztls/test/ZtlsBufHookTest -q
```

The rule suppresses `Memcheck:Cond` on the reviewed libcrypto tag-verification
stack; invalid accesses, allocation errors and leaks remain checked. Revalidate the
rule when OpenSSL or Valgrind changes. Keep accelerated crypto enabled.

For a Clang AddressSanitizer build, reconfigure with `./z.config -c -D -G -P
/opt/z`, then rebuild. `-D` selects Clang and enables debug assertions and ASan;
use leak detection where supported. See [GUIDELINES.md](GUIDELINES.md) for
acceptance criteria. Compare performance with matching release builds, not
sanitized or debug builds; record compiler, CPU, affinity and heap tuning.

## Environment and fixtures

- Network tests need permission to bind loopback TCP/UDP sockets and the ports
  below available. Do not run two copies of a fixture on the same fixed ports.
  Some integration fixtures allocate ephemeral ports; consult their local README.
- TLS fixtures use the OpenSSL command-line tool and local test certificates.
  Keep fixture trust and keys separate from deployment credentials.
- Shared-memory tests need writable system temporary/shared-memory storage.
  `ZiRing` uses `/dev/shm` on Linux and named mappings on Windows.
- Linux D-Bus integration uses a private `dbus-daemon`; Vault interoperability
  additionally uses `gnome-keyring-daemon` with disposable HOME/XDG directories.
  See [Secret Service interoperability](ztls/interop/README.md).
- Database fixtures need the selected backing-store module. Consult
  [Zdb integration](zdb/itest/README.md), [SQLite](zdb_sqlite/README.md), and
  [PostgreSQL](zdb_pq/README.md) for store-specific setup.
- Go-backed REST/MCP matrices require the Go version and packages declared in
  their `go.mod` files; prefetch modules if the test environment is offline.
- GUI tests use Xvfb (`xvfb-run`). `zdash` is currently excluded from the top-level
  build traversal; its [test guide](zdash/test/README.md) describes separate use.
- [HTTP interoperability](zhttp/interop/README.md) covers external H2/H3 peers
  and the Docker QUIC Interop Runner. [Autobahn](zws/interop/autobahn/README.md)
  is opt-in and uses port 9001 outside the allocations below.

## Zxdp packet tests

After a top-level `make -j8`, run the unprivileged synthetic-ring, pool and
ownership suite with `make -C zxdp/test test`. Actual AF_XDP/BPF coverage needs
the anonymous-namespace setup in [Zxdp integration tests](../zxdp/itest/README.md),
followed by `make -C zxdp/itest test` inside that fixture. Missing privileges or
fixture variables produce visible skips; skips do not establish packet coverage.
`ZXDP_RUN_FIXTURE=1` enables the separate-process packet fixture tests.

The independently usable [packet helper](../zxdp/util/README.md) runs veth
request/reply with `sudo -n ./zxdp/util/zxdpfixture.sh`. Its `wait`, `spin` and
`ll` modes cover indefinite waits, finite spin/wait and low-latency polling.
Use `--count 10000 --timer-ms 1` for timer progress during sustained traffic.
Logs and configuration remain in the printed artifact directory on either result.

Run these suites in configured release and Clang `-D` builds with
`ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`; change build modes with a top-level
clean rebuild. Run Memcheck under a non-ASan build. The Nif driver isolates its
expected fatal-Tx cases in child processes. Run its successful `--kernel-probe`
directly under Memcheck as well, because default Memcheck does not follow exec
children. Veth results record copy mode and establish no physical zero-copy or
latency claim. Physical comparison and profiling use matching Clang release
builds, with the selected interfaces, peers, affinity and actual mode recorded.

## Logs and cleanup

For direct invocations, set `ZI_LOGDIR` to a writable artifact directory;
otherwise residue helpers use the working directory. Tests using those helpers
remove owned artifacts on success, retain and print them on failure, and keep
at most eight completed failed directories per test name. Registered shared
memory is unlinked on either outcome. Caller-owned database paths and explicit
QIR result directories are retained. See [test residue helpers](zi/itest/README.md)
for C++, shell, Python, and Go integration.

## Test port allocations

Network tests use fixed, non-overlapping 100-port blocks so that module test
suites can run concurrently.  An integration-test (`itest`) directory has its
own block.  Ports not assigned below remain reserved for future tests in that
directory; tests which do not open a listening socket do not consume a port.

| Directory | Allocated ports |
| --- | --- |
| `zi/test` | 20000-20099 |
| `ztcp/test` | 20100-20199 |
| `ztls/test` | 20200-20299 |
| `zquic/test` | 20300-20399 |
| `zhttp/test` | 20400-20499 |
| `zhttp/itest` | 20500-20599 |
| `zrest/test` | 20600-20699 |
| `zrest/itest` | 20700-20799 |
| `zws/test` | 20800-20899 |
| `zhttp/interop` | 20900-20999 |
| `zmcp/itest` | 21000-21099 |

Subsidiary allocations within those blocks are:

| Directory | Test | Ports |
| --- | --- | --- |
| `zi/test` | `ZiIPTest` | 20000 |
| `zi/test` | `ZiResolverTest` | 20010 |
| `zi/test` | `ZiEventLoopTest` | 20020 |
| `zi/test` | `ZiMxLoopTest` | 20030-20033 |
| `ztcp/test` | `ZtcpLoopTest` | 20100-20101 |
| `ztls/test` | `ZtlsBufHookTest` | 20200-20299 |
| `zquic/test` | `ZquicSockTest` | 20300-20301 |
| `zquic/test` | `ZquicRuntimeTest` | 20310 |
| `zquic/test` | interoperability fixtures | 20320-20329 |
| `zhttp/test` | `ZhttpH1HubTest` | 20400-20411 |
| `zhttp/test` | `ZhttpH3HubTest` | 20412-20416 |
| `zhttp/test` | `ZhttpH2HubTest` | 20417-20422 |
| `zhttp/test` | `ZhttpMessageH1TCPTest` | 20423 |
| `zhttp/test` | `ZhttpMessageH1TLSTest` | 20424 |
| `zhttp/test` | `ZhttpMessageH2Test` | 20425 |
| `zhttp/test` | `ZhttpMessageH3Test` | 20426 |
| `zhttp/test` | `ZhttpServerIdleTest` | 20427-20436 |
| `zhttp/test` | `ZhttpClientCancelTest` | 20437-20448 |
| `zhttp/test` | `ZhttpClientPoolTest` | 20449-20482 |
| `zhttp/itest` | `zhttpmatrix` | 20500 |
| `zhttp/itest` | `zhttpapptest` | 20510-20516 |
| `zhttp/itest` | `zhttpmultirequesttest` | 20520 |
| `zhttp/itest` | `zhttplifecycletest` | 20530-20532 |
| `zhttp/itest` | `zhttpclientfallbacktest` | 20540 |
| `zhttp/itest` | `zhttpserverstreamtest` | 20550-20569 |
| `zrest/itest` | `zrestmatrix` | 20700-20709 |
| `zws/test` | `ZwsH1HubTest` | 20800-20809 |
| `zws/test` | `ZwsH2HubTest` | 20810-20819 |
| `zws/test` | `ZwsH3HubTest` | 20820-20829 |
| `zws/bench` | `zwsbench` | 20830 |
| `zhttp/interop` | QIR smoke tests | 20900-20903 |
| `zhttp/interop` | HTTP/3 interoperability | 20910-20914 |
| `zhttp/interop` | darkhttpd compatibility | 20920-20921 |
| `zmcp/itest` | plain and legacy HTTP | 21000-21002 |
| `zmcp/itest` | secure H1/H2/H3 | 21010-21012 |
