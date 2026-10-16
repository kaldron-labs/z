# Test residue audit and cleanup plan

## Contract and scope

`zi/test/ZiTestResidue.hh` and `.cc` are the C++ contract. A test registers its
owned files, directory trees, and shared-memory ring names before creating
them. `init()` uses `ZI_LOGDIR` or the working directory, ages previous runs
(`Age = 8`), registers an `atexit` fallback and a `ZuTestMgr` final callback.
`final(true)` removes files and trees; `final(false)` retains filesystem
diagnostics; either result unlinks registered shared memory. A signal handler
can call `cleanup()` to preserve diagnostics while releasing shared memory.
`ZiLog` without a file path defaults to stderr on Unix, so `ZiLog::init()` by
itself is not evidence of a log file. TAP/prove output and compiled test
binaries are build outputs, not test residue.

This is a source and `Makefile.am` audit of repository-owned `test`, `itest`,
and `interop` programs and their shared fixtures. It does not claim a runtime
inventory of every optional service or Docker combination. `GUIDELINES.md`
puts end-to-end tests in `itest`, permits native external harnesses in
`interop`, and requires `make test` to run repository-owned test programs.

## Baseline findings

| Area | Current behavior | Work needed |
| --- | --- | --- |
| Existing C++ coverage | `zi/test` file, directory, log, PID and ring tests; `zv/test` fixture writers; `zquic/test` qlog tests; `zhttp/test` hub tests; `zhttp/itest` app, daemon, fallback, lifecycle, matrix, multi-request and stream tests; `zrest/itest/zrestmatrix.cc`; `ztc/test` alert and app tests; and `ztc/itest/ztcagenttest.cc` use `ZiTestResidue` in whole or in part. `zi/itest/zitestresidue.cc` tests the manager itself. | Keep these registrations; check every secondary output and resource, especially PID directories, qlog sidecars and process shutdown, when touching each test. |
| Local C++ temporary directories | `zquic/test/ZquicInteropTest.hh` switches between `ZiTestResidue` and a `mkdtemp`/destructor path with `Z_TEST_RESIDUE`. `zhttp/interop/zhttpqirtest.cc`, `zhttpqirsmoketest.cc`, `zhttp3interoptest.cc`, `zhttpdarkhttpdcompattest.cc` and `zhttpfallbacktest.cc` use that helper but their `Makefile.am` does not define the macro or link `libZiTest`. `zmcp/itest/zmcphttptest.cc` and `zws/bench/zwsbench.cc` also use the helper without initializing or linking `ZiTestResidue`. `zquic/test/ZquicHandshakeTest.cc`, `zi/itest/zringmatrix.cc`, `ztls/test/ZtlsVaultTest.cc`, `ztls/itest/ztlsvaultdbustest.cc` and `ztls/interop/ztlsvaultinterop.cc` use local temporary directories or `ZuGuard` deletion. | Make test-owned directories use `ZiTestResidue::dir()` and remove routine success/failure-blind deletion. Keep local cleanup only for resources that must be closed before `final()`. Link `libZiTest` wherever newly used; remove the conditional helper path once all callers are migrated. |
| Other C++ writers | `ztls/test/ZtlsAgeTest.cc` manually removes `ZtlsAgeTest.age`; `zi/test/ZiPIDFileTest.cc` and `ztc/test/ZtcAppTest.cc` have paths under the system temp directory outside their registered tree. `zhttp/interop/zhttpdarkhttpdcompattest.cc` creates a document root under its temporary tree. `zhttp/itest/zhttpmatrix.cc` writes an access log under its test directory. | Register every path or move it below one registered directory. Check the exact output path for cases where a test deliberately exercises an external directory. Do not treat a normal read of checked-in certificates as residue. |
| SQLite and database fixtures | `zdb_sqlite/itest/Makefile.am`, `zum/itest/Makefile.am` and `ztc/itest/Makefile.am` create temporary SQLite locations and delete them with shell traps, including `-wal` and `-shm` sidecars. Four direct SQLite TAP scripts (`zdbslplantest`, `zdbslconfigtest`, `zdbslschematest`, `zdbslinspecttest`) use `mktemp` and delete their DBs regardless of TAP results; `zdbslconfigtest` is omitted from Git because the catch-all ignore rule excludes names without an extension. `zdb/itest` and `zdb_pq/itest` primarily use external `ZDB_CONNECT`; `zdb/itest/clean.sh` removes `*.log.*` broadly. | Give each test an owned database directory or explicit registration, including sidecars, and retain failed database state for diagnosis. Make tests that require a caller-owned DB leave it untouched. Replace broad log deletion with ownership-based cleanup. Track the omitted TAP script and check DB shutdown before deleting sidecars. |
| QIR and other HTTP interop | `zhttp/interop/qir-script-test.sh` creates a fixed `$$` temporary tree and unconditionally deletes it on exit. `zhttph2interoptest.sh` uses its own temp tree and retains failures, but has no bounded aging. The five C++ interop tests above use the conditional temp helper; QIR emits qlogs. `scripts/qir-run-local` writes timestamped result trees under `zhttp/interop/qir-results` by default and cleans runner logs separately. `qir_endpoint.sh` writes into runner-owned `/logs`. | Use `ZiTestResidue` for C++ drivers and one reusable shell helper for script tests. Retain and age failed QIR script fixtures and HTTP/2 artifacts. Treat `qir-run-local` result trees as explicit user-facing interop results: document their retention/age policy separately; never delete a caller-specified `--out`, runner directory or runner-owned `/logs` from the endpoint. |
| Python integration | `zum/itest/zumhttp.py`, `zumfederation.py`, `zumcluster.py`, `zumssf.py`, `zumdvault.py`, `zumdrekey.py`, `zumdztctest.py`, `zumdztccluster.py`, `zumdztcgctest.py`; `ztc/itest/ztchubtest.py`; `zdash/itest/zdashoauthtest.py`; and `zum/itest/zumpingdiscoverytest.py` create temporary trees and/or registry and `/dev/shm` entries. Some retain a failed directory, others remove it unconditionally; each has bespoke logic. Helper modules such as `zumhttp.py` and `zumidp.py` also spawn services and write logs through their drivers. | Add a reusable Python residue manager in `zi/itest` and migrate the owning drivers. Register logs, DBs and sidecars, configs, registry directories and shared memory. Stop and reap child processes before final cleanup. Preserve diagnostic trees on failure, unlink shared memory on either outcome, age older failures, and print the retained location in TAP diagnostics. Keep pure fixture/helper modules dependent on the driver's owner. |
| Go interop | `zmcp/interop/interop_test.go` uses `testing.T.Cleanup` for child processes and pipes; it does not write persistent files in the audited paths. `zrest/itest/zrestprobe.go`, `zrestua.go` and `zrest/interop/go/{client,server}.go` likewise show no runtime file writes; the Go `Makefile` builds binaries as ordinary build products. The two interop directories are separate Go modules. | Provide a reusable Go residue package under `zi/itest` for future and any newly discovered file-producing Go tests. Use `t.Cleanup`, `t.Failed()`, per-test owned paths and aging; ensure each module can import it without a network dependency or module cycle. Do not add residue directories to the existing file-free Go tests merely for uniformity. |
| Other interop scripts | `zws/interop/zwsbinancetest.sh` deletes its captured output even on failure. `zws/interop/autobahn/run-*.sh` mount the interop directory into Docker; their JSON configs write reports below that mount. `zum/interop/jwt-openssl.sh` and `zdash/test/ZdashTest.sh` also delete test-owned temporary fixtures unconditionally. `zv/interop/zvtomlinteroptest.sh` and `zum/interop/jwt-fixture.py` are output/fixture producers whose caller owns the destination. | Use the shell helper for test-owned captures and temporary fixtures. Redirect Autobahn reports into an owned mount and retain failures. Register the named ring in the dashboard shell test. Leave caller-owned outputs to their caller. |

## Prescribed implementation

### Shared behavior

1. Keep `ZiTestResidue` as the C++ API and `Age = 8` as the retention limit.
   Put the other language implementations in `zi/itest` and document one
   common contract in `zi/itest/README.md`. The root is a canonicalized
   `ZI_LOGDIR`, or the current working directory when unset. Reject empty
   names, separators, `..`, absolute paths and paths that escape the root.
   Never register or delete a caller-provided absolute path. For tests whose
   contract requires `ZiFile::tmpDir()` (which is `/tmp` on Unix), add narrow
   managed-temp APIs instead of weakening the root-path validation:
   `ZiTestResidue::tmpFile(tag)` and `tmpDir(tag)` allocate and register
   unique test-prefixed entries there; the Python manager supplies
   `tmp_dir(tag)` and `tmp_file(tag)` with the same ownership rule. These are the only permitted
   automatic deletions outside `ZI_LOGDIR`.
2. For Python, Go and shell, allocate a private directory atomically under
   the root with a test-name prefix and a unique suffix. All files created by
   that test, including SQLite sidecars, go below it except for managed
   system-temp entries. Mark an active run so
   aging never removes another running test's directory. On pass, remove the
   directory; on failure or interruption, mark it failed and print its path
   as a TAP diagnostic. Prune only completed failed directories with the
   exact test-name prefix, retaining the newest eight. Keep pruning safe
   when two instances of the same test run concurrently. Do not recursively
   follow symlinks while removing an owned tree.
3. Process and service teardown precedes filesystem finalization. Unlink
   explicitly registered POSIX shared-memory `<name>.ctrl` and `<name>.data`
   on pass and failure. Make finalization idempotent. An unhandled exception,
   failed assertion, nonzero exit or caught termination signal preserves
   diagnostics. `SIGKILL` and hard crashes cannot run cleanup; the next run
   must identify and age abandoned owned directories without touching a
   live run. Do not count an abandoned directory as a passing run.
4. Use one directory owner per test process or make recipe. Child processes
   receive paths within it through existing configuration/environment
   arguments; they do not create independent owners for the same artifacts.
   `ZDB_CONNECT`, QIR `--out` and `/logs` are borrowed when supplied by an
   external caller. A test may use a caller-owned path, but must not register
   or remove it.

### Reusable APIs in `zi/itest`

| File | API and lifecycle |
| --- | --- |
| `zi/itest/zi_test_residue.py` | Define `Residue(name)` with `root`, `path(name)`, `dir(name)`, `tmp_dir(tag)`, `tmp_file(tag)`, `shm(name)` and `finish(passed)`. `path()` returns a validated child path; `dir()` creates one; `tmp_dir()` creates a unique, registered system-temp directory only for APIs fixed to that location; `tmp_file()` creates a registered blocking file for negative registry tests; `shm()` registers an exact base name. Supply a context manager that calls `finish(False)` on exceptions and requires an explicit success indication on the normal path, so a TAP failure that does not throw cannot be mistaken for success. Keep child-process stop/reap code in each driver, before `finish`. |
| `zi/itest/zi-test-residue.sh` | Sourceable shell functions `zi_residue_init NAME`, `zi_residue_path NAME`, `zi_residue_shm NAME` and `zi_residue_finish STATUS`. `init` sets and exports `ZI_RESIDUE_DIR`; `path` prints a validated child path; `shm` registers a ring base; `finish 0` removes the owned tree, while nonzero status retains and reports it. The caller installs one `EXIT` trap that saves `$?`, stops/waits for children, then calls `zi_residue_finish`; `INT`/`TERM` handlers exit nonzero through that trap. The helper must not overwrite an existing caller trap. |
| `zi/itest/residue/go.mod` and `zi/itest/residue/residue.go` | Use a small local Go module/package with `New(t *testing.T, name string)`, `Path(name string) string`, `Dir(name string) string` and `Shm(name string)`. `New` registers a `t.Cleanup` callback that observes `t.Failed()`, then finalizes once. Register it before process cleanup callbacks so Go's last-in-first-out cleanup stops processes first. Use local `replace` directives from consumer Go modules only when they actually import the package; no downloaded runtime dependency. Go programs outside `testing.T` can use an explicit `Finish(bool)` constructor variant if a writer is introduced later. |
| `zi/itest/Makefile.am` | Distribute the Python, shell and Go helper sources and their focused tests; make helper tests runnable from `make test`. Add a short `README.md` with source-tree import/source examples and the naming/retention contract. |

Implement the shared lifecycle with language-native filesystem operations.
Avoid a shell subprocess in the Python or Go cleanup path. Expose cleanup
errors rather than silently claiming a pass; preserve the original test
failure when cleanup also fails. Shared-memory names must be registered
before the producer starts. Restrict deletion to the owned namespace and
explicit shared-memory names.

### C++ call-site changes

1. In `zquic/test/ZquicInteropTest.hh`, make `TempDir::init(prefix)` call
   `ZiTestResidue::dir()` unconditionally; remove `Z_TEST_RESIDUE`,
   `mkdtemp`, the destructor cleanup branch and `cleanup()`. Its callers
   already use the returned `path`; preserve that interface. Add
   `ZiTestResidue::init()` to each C++ interop executable before the first
   `TempDir`, using its program name. Add `zi/test` to include paths and
   `libZiTest.la` to `zhttp/interop/Makefile.am` and
   `zmcp/itest/Makefile.am` and `zws/bench/Makefile.am`; initialize
   `ZiTestResidue` in `zmcp/itest/zmcphttptest.cc` and
   `zws/bench/zwsbench.cc` before their secure fixtures. Finish the bench
   manager with its process result. Keep the existing
   `Z_TEST_RESIDUE` users in `zhttp/test`, `zhttp/itest`, `zrest/itest` and
   `zws/test` building while removing that compile definition once the
   helper no longer needs it.
2. Extend `zi/test/ZiTestResidue.hh` and `.cc` with the two managed-temp
   functions described above. Give each name the `ZiTest.<test>.<pid>`
   prefix plus a counter and tag, validate it before use, age only prior
   entries matching the test prefix (across PIDs), and store these registrations
   separately from root-relative `file()`/`dir()` entries. Retain files on
   failure and remove them on pass. Never let `add()` accept an arbitrary
   external path.
3. Replace the local directory owners in
   `zquic/test/ZquicHandshakeTest.cc`, `zi/itest/zringmatrix.cc`,
   `ztls/test/ZtlsVaultTest.cc`, `ztls/itest/ztlsvaultdbustest.cc` and
   `ztls/interop/ztlsvaultinterop.cc` with `ZiTestResidue::dir()`. Use
   distinct leaf names for multiple fixtures in one process. Remove
   `ZuGuard`/destructor cleanup and `zringmatrix`'s bespoke preservation
   switch; let the TAP outcome control retention. Add the needed include
   paths and `libZiTest.la` to those directories' `Makefile.am` files.
4. Move `ZtlsAgeTest.age` below a registered `dir()` or obtain it from
   `file()`. For `ZiPIDFileTest` and `ZtcAppTest`, route test-owned default
   temp and registry paths through `tmpFile()`/`tmpDir()` while preserving
   the system-temp behavior being tested. Set `ZTC_DIR` to the registered
   directory's leaf name, since the product appends it beneath
   `ZiFile::tmpDir()`. Use `tmpDir()` for vault fixtures that specifically
   exercise the system temp location. Check all manually removed qlog
   paths are already children of registered directories. Do not register
   stderr (`&2`) sinks.
5. Initialize before creating any file or shared memory. Stop servers and
   close qlog/log/DB writers before returning from `main` or calling
   `ZiTestResidue::final(passed)`. Use `ZmTrap::sigintFn(&ZiTestResidue::cleanup)`
   for drivers that already handle interruption, while ensuring child
   processes are stopped before the handler leaves.

### Python, database and script call-site changes

1. Migrate each Python owner listed in the audit table to `Residue`.
   Replace `tempfile.mkdtemp`, `TemporaryDirectory` and conditional
   `shutil.rmtree` with `dir()`/`path()` under its owned root. `zumhttp.py`
   and `zumidp.py` keep their fixture responsibilities and receive the
   owner's directory; no second lifecycle. `ztchubtest.py`,
   `zumdztctest.py`, `zumdztccluster.py` and `zumdztcgctest.py` register
   every created ring base before launching the producer, and remove their
   duplicate `/dev/shm` loops. Create `ZTC_DIR` directories with
   `tmp_dir()` and pass only their leaf names to child processes. This
   preserves the product's `/tmp/<ZTC_DIR>` lookup while making the
   directory an owned artifact.
2. In `zdb_sqlite/itest/Makefile.am`, create the `ZDB_CONNECT` path beneath
   a shell-helper directory and finalize after the entire `prove` command.
   The directory owns the main DB, `-wal`, `-shm`, leader and follower
   files. Also migrate `zdbslplantest`, `zdbslconfigtest`,
   `zdbslschematest` and `zdbslinspecttest`: each creates SQLite databases
   directly with `mktemp`, so each must source the shell helper, place its
   DBs under its own directory, and retain them when a TAP assertion fails.
   Apply the same wrapper to the single database directory in
   `zum/itest/Makefile.am` and the three database runs in
   `ztc/itest/Makefile.am`. On failure, print one retained directory path;
   on pass, remove it after all child tests and DB writers stop. Remove
   `zdb/itest/clean.sh` from any test cleanup path that uses broad `*.log.*`
   deletion; keep caller-owned database connections untouched.
3. Source the shell helper from `zhttp/interop/qir-script-test.sh`,
   `zhttph2interoptest.sh`, `zws/interop/zwsbinancetest.sh`,
   `zum/interop/jwt-openssl.sh` and `zdash/test/ZdashTest.sh`. Put their
   generated files under `ZI_RESIDUE_DIR`. Keep the HTTP/2 script's process
   kill/wait logic ahead of finalization and replace its custom
   `KEEP_ARTIFACTS` branch with the shared success/failure rule. Inspect
   Autobahn's actual report destination; if it writes into the mounted
   source directory, redirect it into an owned mount/subdirectory and use
   the same shell lifecycle. The QIR endpoint's `/logs` and published
   `qir-run-local` result trees stay owned by their runner/caller.
   Add `zi_residue_shm(name)` to the shell helper for the dashboard's named
   ring and unlink its `.ctrl` and `.data` objects after the dashboard stops.
4. Keep `zmcp/interop/interop_test.go` and the file-free `zrest` Go programs
   on their existing process cleanup. Use the Go package when a Go test
   begins writing diagnostics. Do not treat `go build` outputs as runtime
   residue.

### Verification and acceptance

Add focused helper tests under `zi/itest`: pass removes a tree, failure
retains it, repeated failures retain at most eight archives, concurrent runs
do not delete one another, malformed/escaping names are rejected, repeated
finalization is safe, and POSIX shared-memory `.ctrl`/`.data` objects are
unlinked on both outcomes. Exercise a Python exception, Go `t.Fatal`, and
shell nonzero exit and `TERM`; verify each prints the retained path. Use
`zi/itest/zitestresidue.cc` for any newly needed C++ contract check.

Build the affected targets before testing. Run `make test` in `zi/itest`,
`zquic/test`, `zhttp/test`, `zhttp/itest`, `zhttp/interop`, `zrest/itest`,
`zws/test`, `ztls/test`, `ztls/itest`, `ztls/interop`,
`zdb_sqlite/itest`, `zum/itest`, `ztc/test`, `ztc/itest`,
`zmcp/itest` and `zmcp/interop` where their documented optional dependencies
are available.
For one representative test in each language, compare its owned root before
and after a passing run and an induced failure. A pass leaves no current
artifacts; a failure leaves an identified diagnostic directory; neither
leaves registered shared memory or a child process. Repeat a test under
parallel execution and with `ZI_LOGDIR` set to a relative path.

## Completion criteria

Every repository-owned test program that creates persistent residue has one
owner and a registered cleanup path. A passing run leaves no test-owned
filesystem or shared-memory artifacts; a failing run keeps bounded, named
filesystem diagnostics and releases shared memory and child processes.
Independent and parallel runs do not collide. Caller-owned database paths,
QIR published results and build outputs remain outside automatic cleanup.
