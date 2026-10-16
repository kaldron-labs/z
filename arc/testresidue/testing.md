# Test residue and logging implementation specification

## Goals and invariants

Replace the current manifest-based `ZiTestResidue` implementation with a small,
compiled test-support facility and apply it consistently to Z tests that depend
on `zi`. Generated logs are untracked runtime artifacts: “test-produced” below
describes lifecycle ownership, never files that should be checked into the
repository.

“Residue” means a lifecycle-owned artifact that exists at finalization. A file
whose creation, rename, or deletion is itself the operation under test may be
mutated by that test; it is not reconstructed after an asserted deletion.
Routine teardown must not use that exception: diagnostic output and fixtures
not consumed by the tested operation remain registered until finalization.

The resulting behavior must satisfy these invariants:

1. A successful test run leaves no filesystem residue from that run.
2. A failed test or handled interruption retains all generated filesystem
   residue for diagnosis with age depth eight: the current failed generation
   plus at most eight archived generations. This includes logs and generated
   fixtures whose contents are used by assertions. Recovery from hard crashes
   and `SIGKILL` is out of scope; the next run's bounded aging is sufficient for
   files they leave behind.
3. Every exact test-produced file is aged before it is opened, either by an
   explicit `ZiFile::age` call or, for `ZiLog`, by a sink configured with a path
   and age count. A variable file family is confined to a deterministic
   test-scoped directory that is aged as one generation before the producer
   runs.
4. Tests launched by `make test` receive `ZI_LOGDIR` set to the top build
   directory's `logs/` directory. A directly launched test falls back to its
   current working directory when `ZI_LOGDIR` is unset.
5. Generated files and their containing directories are removed on success and
   retained on failure. Non-filesystem resources such as processes, sockets,
   and named shared-memory objects cannot be aged as diagnostic files and are
   always released by their owner; `ZiTestResidue` continues to clean the named
   shared-memory bases registered with it.
6. No `.ZiTestResidue` directory or manifest file is created anywhere.
7. QIR and Autobahn retain ownership of their prescribed result layouts and
   cleanup policies.
8. `zws` is included. `zdash`, `zum`, `zcmd`, `zproxy`, and `zgtk` remain
   excluded.

## Current-state findings

- `zi/test/ZiTestResidue.hh` contains all state and function implementations as
  `inline` definitions even though none is a template.
- The header contains an anonymous namespace, prohibited by `GUIDELINES.md`.
- `State::lock` currently serializes all mutable state (`initialized`,
  `cleaned`, paths, test name, counter, and the file/directory/shared-memory
  arrays), but the layout does not communicate that ownership.
- The state is a raw function-static object, its lifecycle is represented by
  separate booleans, and its `ZtArray` registries have no `ZmHeapID`; all three
  conflict with the singleton, sentinel-state, and allocation audit guidance.
- `mkdirIfNeeded_` has an unnecessarily verbose name; `trimLine_`, `endsWith_`,
  and all manifest parsing/writing exist only to support the manifest design.
- The manifest is written beside every run directory and replayed by a later
  process. This adds filesystem I/O and parsing yet has still left `.ZiTestResidue`
  trees at the repository root and under `zi/test` and `zi/itest`.
- Only nine Zi tests currently include `ZiTestResidue.hh`; none of the requested
  downstream modules uses it yet.
- `ZiFileSink` and `ZiCSVSink` age file-backed logs, while `ZiDebugSink` does not.
  A default Unix `ZiLog` sink can therefore create a log in the process working
  directory unless each test supplies an explicit sink path.
- Before enabling and rerunning all tests, the current tree already contains 383
  non-configuration, non-QIR files matching `*.log*` and nine non-QIR sqlogs.
  The largest groups are under `zi/test`, `zhttp/test`, `zquic/test`, `zv/test`,
  `zdb/test`, and the existing top-level `log/` directory. These are a baseline,
  not a complete post-test inventory.
- Many module `SUBDIRS` assignments still comment out `test`, so top-level
  `make -j8` does not currently prebuild the complete test set even though most
  module-level `test` targets enter those directories manually.

## 1. Convert `ZiTestResidue` into compiled test support

The facility remains named `ZiTestResidue`; do not introduce `ZiTestLog`.

1. Reduce `zi/test/ZiTestResidue.hh` to declarations and the minimum direct
   includes needed by its public API. Move all non-template definitions and
   process-global state to a new `zi/test/ZiTestResidue.cc`. Give both files the
   repository modelines, copyright/license block, and brief component comment.
   In the header, retain the include guard, include `ZiLib.hh` first, and then
   include only the direct `ZuDerive`, `ZtArray`, and `ZiPlatform` dependencies
   in foundation order. Put lock, singleton, filesystem, glob, and test-manager
   dependencies in the `.cc`, not the header.
2. Use file-scope `static` internal functions and define `State` in the `.cc`;
   let `ZmSingleton<State>` own the state instance. Do not introduce an
   anonymous namespace or a raw static state object.
3. Rename `mkdirIfNeeded_` to `mkdir_`. Delete `trimLine_`, `endsWith_`,
   `writeManifest_`, `cleanupFromManifest_`, and `cleanupStale_` along with the
   manifest path and all manifest file creation/parsing.
4. Keep one process-local state object and make the lock's protection explicit
   through indentation:

   ```cpp
   struct State {
     enum { Uninit, Active, Final };

     using Guard = ZmGuard<ZmLock>;
     ZuDerive(Names, (
       ZtArray<Zi::Name, ZtArrayHeapID<"ZiTestResidue.Names">>));

     ZmLock              lock;
       Zi::Path            rootDir;
       Zi::Path            basePath;
       Zi::Name            testName;
       Paths               files;
       Paths               dirs;
       Names               shmBases;
       unsigned            counter = 0;
       int                 phase = Uninit;
   };
   ```

   Access every indented member only while holding `lock`; static-local
   initialization itself does not take that lock. Obtain the one process-wide
   `State` through `ZmSingleton<State>`, not a raw function-static object or
   `thread_local`. `phase` is the single lifecycle value; do not reintroduce
   separate initialized/finalized booleans. Resolve `rootDir` from
   `ZI_LOGDIR`/cwd and set `basePath` to `<rootDir>/<testName>.`, the stable
   prefix used by `path`, not a manifest or per-process directory.
   Registration and name allocation occur on the main/control thread before
   workers start. The lock protects initialization, lifecycle arbitration,
   `counter`, and all registries against handled cleanup/finalization. Never
   hold it while waiting for a child, blocking on concurrency, invoking a
   callback, or logging. `final` moves the registries into heap-tagged local
   containers while locked, sets `phase = Final`, unlocks, and then performs
   filesystem/shared-memory cleanup; concurrent or repeated finalization sees
   `Final` and returns.
5. Apply one lifecycle to all filesystem residue rather than distinguishing
   logs from fixtures: age before creation, remove the current generation on
   success, and retain it on failure. Keep non-filesystem resources in a
   separate always-cleaned registry.
6. Export exactly this namespace API from `ZiTestResidue.hh`:

   ```cpp
   namespace ZiTestResidue {

   enum { Age = 8 }; // current failure plus eight diagnostic archives

   ZuDerive(Paths, (
     ZtArray<Zi::Path, ZtArrayHeapID<"ZiTestResidue.Paths">>));

   void init(const char *testName);
   Zi::Path path(const Zi::Name &name);
   Zi::Path file(const Zi::Name &name);
   Zi::Path dir(const Zi::Name &name);
   void add(const Zi::Path &path);
   Paths glob(const Zi::Path &dir, const Zi::Name &prefix);
   void del(const Zi::Path &dir, const Zi::Name &prefix);
   Zi::Name uniqueName(const char *tag = nullptr);
   void addShm(const Zi::Name &name);
   void final(bool passed);
   void cleanup();

   } // ZiTestResidue
   ```

   Define the nested `State::Names` container with the shown heap ID.
   `Zi::Path` and `Zi::Name` already use the framework's `Zi.Path` and
   `Zi.Name` heap IDs. Do not retain `tempRoot`, `addFile`, `addDir`,
   `addShmBase`, or `cleanupNow` as compatibility forwarders; migrate every
   caller.
7. Give those functions the following fixed contracts:
   - `init` is idempotent for the same `testName`; use `ZiAssert` on a different
     name in a subsequent call. Register `cleanup` with `atexit` once and install
     the `ZuTestMgr` final-result callback once. A test name is a non-empty
     basename without a path separator and is neither `.` nor `..`. Resolve a
     relative `ZI_LOGDIR` against `ZiFile::cwd()`, create the root, canonicalize
     it, and store only that absolute canonical path. Construct/access the
     `ZmSingleton<State>` before registering `atexit`, so `cleanup` runs before
     singleton destruction.
   - `path` uses `ZiAssert` to require that its logical artifact name is
     non-empty and contains no path separator or `..`, then returns
     `<root>/<testName>.<name>`. It performs no aging or registration.
     Matrix harnesses encode their explicit child/case namespace in the logical
     name, for example `case03.reader.stderr`; the fixed case number makes the
     path stable across harness runs.
   - `file` calls `path`, calls `ZiFile::age(result, Age)`, registers the current
     path, and returns it. Use it for every exact-path producer that does not age
     its own output.
   - `add` uses `ZiAssert` to require that its argument equals a path obtainable
     from `path` for the initialized test, then registers it without aging. Use
     it only before starting a `ZiLog` file/CSV sink configured with `age(Age)`
     for that same path. That sink must age only during initial open. A test
     that explicitly exercises `ZiLog::age()` uses a `dir` owner for the whole
     family instead.
   - `dir` calls `path`, ages the complete directory family by `Age`, creates
     the empty current directory, registers that directory, and returns it. Use
     it for every producer that chooses filenames dynamically or creates a tree.
   - `glob` returns the current entries selected by `ZiGlob` for `prefix` inside
     a directory returned by `dir`. Return its `Paths` by elision/move; do not
     copy through an intermediate container. `del` deletes matching regular
     files from that same current directory. Both use `ZiAssert` on the residue
     root, exact-file namespaces, archived directory names, and paths outside the
     initialized test namespace. Use `del` only when deletion is part of the
     asserted test operation or when preparing an already registered current
     directory for its producer; never use it for end-of-test teardown.
   - `uniqueName` is only for non-filesystem names such as shared memory. It
     retains the existing test-name, PID, counter, and optional-tag scheme.
   - `addShm` registers the exact base consumed by `cleanupShm_`.
   - `final(true)` releases shared memory, removes current files, recursively
     removes current directories, and marks the state finalized.
     `final(false)` releases shared memory, retains current filesystem entries,
     and marks the state finalized. Both are idempotent. Use `ZiAssert` on an
     unexpected owned-resource removal failure during successful finalization
     so a passing process cannot silently leave residue.
   - `cleanup` is exactly the conservative `final(false)` entry point used by
     `atexit` and handled-interruption paths.
8. Implement `glob`/`del` with Z framework strings, paths, arrays, `ZiGlob`, and
   `ZiFile`; implement `ZiFile::removeTree` with `ZiDir` and `ZiFile`. Do not
   replace the deleted `trimLine_` parser with C-string parsing in callers.
   Scope each `ZiGlob`/`ZiDir` iterator to the traversal that consumes it; close
   the traversal before mutating the registry or invoking cleanup. Process
   registered files before recursively removing registered directory
   trees during successful finalization. Use `ZiAssert` on duplicate
   registration and overlap between an exact file and an enclosing registered
   directory so finalization never processes the same path twice. Do not follow
   symbolic links or Windows reparse points while recursively deleting a
   directory family.
9. Build the implementation once as the no-install libtool convenience library
   `libZiTest.la` from `zi/test/Makefile.am`:

   ```make
   noinst_LTLIBRARIES = libZiTest.la
   libZiTest_la_SOURCES = ZiTestResidue.cc
   ```

   Put `libZiTest.la` first in `zi/test` program `LDADD`. In each adopting
   downstream `test`/`itest` Makefile, add `-I$(top_srcdir)/zi/test` to
   `AM_CPPFLAGS` and `$(top_builddir)/zi/test/libZiTest.la` before the module
   libraries in `LDADD`. Do not compile `ZiTestResidue.cc` into individual
   executables.
10. Add lowercase `zitestresidue.cc`/`zitestresidue` to
    `zi/itest/noinst_PROGRAMS`, not `zi/test`: its default parent mode spawns
    short-lived modes of itself, making it an integration test under the module
    layout rules. Use `ZuTest`/`ZuTestUtil` and emit TAP. Test pass and failure
    child modes through `ZfCLI` and `ZtEnum`, not ad hoc `argv`
    parsing or chained string comparisons. Use process completion/IPC as the
    success condition and a one-second timeout only as a stuck-child watchdog.
    Test finalization without finalizing the parent's registry, initialization
    idempotence, rejection of a second test name and invalid paths,
    duplicate/overlap rejection, unique non-filesystem names, full-path
    resolution, exact-file and directory-family aging, pattern deletion,
    recursive directory cleanup, shared-memory cleanup, successful residue
    deletion, failed residue retention, and repeated finalization.

## 2. Define residue location, aging, and success/failure lifecycle

1. Establish `$(abs_top_builddir)/logs` as the filesystem-residue root used by
   the build. The directory name remains `logs` even though it also contains
   generated fixtures retained from failed tests. Replace the ignored top-level
   `log/` convention with `/logs/` in `.gitignore` and create the directory
   lazily through `mkdir_`. Generated residue remains ignored runtime output,
   not repository content.
2. Add this target-specific export to every top-level, module, `test`, and
   `itest` Makefile containing a `test` target:

   ```make
   test: export ZI_LOGDIR = $(abs_top_builddir)/logs
   ```

   Recursive makes, `prove`, and their children inherit the absolute value.
   Add the same export to repository matrix targets invoked independently of
   `test`. `ZiTestResidue` reads `ZI_LOGDIR`; if it is unset, it uses
   `ZiFile::cwd()` so direct binary execution keeps residue in the caller's
   working directory.
3. Give each test a deterministic namespace and each artifact a stable base name
   under `ZI_LOGDIR`, for example `<test>.<file>` for exact files and
   `<test>.<family>/` for a producer-owned tree. Concurrent independent
   instances of the same test are unsupported user error. A matrix harness that
   intentionally runs duplicate children assigns each case/role a fixed logical
   namespace such as `case03.reader` and passes only paths derived from it.
4. Before opening or handing off a new current file, assign exactly one aging
   owner. Use `file()` for an ordinary producer and for `ZiDebugSink`, which
   does not age itself. For a self-aging `ZiFileSink` or `ZiCSVSink`, use
   `path()` plus `add()` and configure the sink with that path and age count
   `Age`. Never combine both operations for one generation. `Age` means one
   current path plus up to eight archived paths (`.1` through `.8`), not eight
   total. Successful runs remove their current paths; archives therefore
   represent retained failures.
5. Implement file and directory aging without duplicating the rotation loop:
   - change `ZiFile::age` to take `const Path &`; add
     `ZiFile::removeTree(const Path &, ZeError *)` and
     `ZiFile::ageTree(const Path &, unsigned)`. Construct `prevName`,
     `nextName`, and `sideName` as `Path` so the implementation handles native
     Windows paths and appends the numeric suffix directly;
   - implement one file-local `age_` loop in `ZiFile.cc`; `age` calls it with
     ordinary-file removal and `ageTree` calls it with recursive-tree removal.
     Keep the removal choice as a direct internal branch, not a public callback
     or type-erased function;
   - delete `ZiLog.cc`'s file-local `ageFile`; call `ZiFile::age` from
     `ZiFileSink::init`, `ZiFileSink::age`, `ZiCSVSink::init`, and
     `ZiCSVSink::age`;
   - update every direct caller found by `rg 'ZiFile::age\('` for the `Path`
     signature, including `ZquicLog`, `ZiFileAgeTest`, and the excluded WIP
     `zcmd` source so the breaking API change is propagated without enabling its
     tests. Give `ZiFileAgeTest`'s deliberately smaller depth a named enum with
     a coverage comment instead of repeating literal `3`;
   - make `dir` call `ZiFile::ageTree(path, Age)` and make successful
     finalization call `ZiFile::removeTree`; do not implement another recursive
     walker in `ZiTestResidue.cc`;
   - never separately register or lifecycle-age an individual file whose
     enclosing directory is the registered directory-family owner. A test such
     as `ZiFileAgeTest` or `ZiLogTest` may still exercise file aging inside that
     directory as the operation under test.
6. Never use the default file path of `ZiFileSink`, `ZiCSVSink`, or
   `ZiDebugSink` in a test. Stderr-only sinks (`&2`) are out of scope. A harness
   that explicitly redirects a child's stderr to a file owns that file as a
   child capture, not as a stderr sink migration.
7. Add this dependency-free callback API to `ZuTestMgr`:

   ```cpp
   using FinalFn = void (*)(bool);

   static void finalFn(FinalFn fn) { instance().finalFn_(fn); }
   ```

   Add private `FinalFn m_finalFn = nullptr` and one untyped run-state enum/value
   (`Idle`, `Running`, `Ended`) rather than a separate ended boolean. Set it in
   `start_` and the root arm of `end_`. The `finalFn_` setter uses plain
   `assert` to require that the root has not ended and that an existing non-null
   callback is not replaced by a different function. In the root arm of
   `end_`, compute `passed = !m_context->failed`, set the state to `Ended`, copy
   `m_finalFn` to a local and null it, invoke the local callback exactly once,
   and only then call `exit(m_context->failed)` on failure. This adds no
   dependency from `zu` to `zi`.

   `ZiTestResidue::init()` calls
   `ZuTestMgr::finalFn(&ZiTestResidue::final)`. Tests that do not use `ZuTest`
   call `final(true)` or `final(false)` explicitly after stopping their writers
   and children.
8. Rename `cleanupNow()` to `cleanup()`; “now” adds no useful distinction.
   `cleanup()` is the idempotent failure-path entry point used by handled-signal
   and `atexit` callbacks: it retains current filesystem residue and releases
   registered non-filesystem resources. Test and harness owners remain
   responsible for stopping writers before orderly finalization; the residue
   facility does not claim ownership of arbitrary sinks. Remove explicit
   completion-time `cleanupNow()` calls rather than mechanically renaming them;
   successful completion comes only from the `ZuTestMgr` final-result callback
   (or explicit `final(true)` for a non-`ZuTest` program). Update
   `ZmTrap::sigintFn` registrations to `cleanup()` and do not retain a
   compatibility forwarder.
9. Give each path one lifecycle owner in a multi-process test. Normally the
    parent harness prepares and registers the entire child fixture/capture
    namespace and makes the retention decision from the overall case result;
    children then receive paths but do not independently delete them. A child
    may instead own an independent namespace, but parent and child must never
    register the same path. Ensure every writer is stopped or closed before the
    owner finalizes, including on Windows where an open file cannot generally be
    removed or renamed.
10. Test age depth eight across ordinary logs, exact fixture files, and directory
    families. Once the current generation and `.1` through `.8` exist, each
    subsequent preparation discards the oldest archive before rotation. A
    successful run still rotates the prior failed current generation, then
    deletes only the newly created current generation.
11. Do not add manifest replacement or stale-process recovery for hard crashes
    and `SIGKILL`. Files left by those cases are handled only by bounded aging
    on subsequent runs.

## 3. Remove manifests and legacy residue safely

1. Before deleting anything, record a categorized baseline of:
   - every `.ZiTestResidue` directory and manifest;
   - the existing top-level `log/` contents;
   - `*.log*`, `*.sqlog*`, and other test-produced files/directories under
     module `test/` and `itest/` directories;
   - interoperability results, especially `zhttp/interop/qir-results`, which
     must not be included in general cleanup.
2. After all users have migrated, remove the repository-root `.ZiTestResidue`
   tree and the nested `zi/test/.ZiTestResidue` and
   `zi/itest/.ZiTestResidue` trees. Verify with a repository-wide search that no
   code mentions the old root name and no test recreates it.
3. After recording the inventory and migrating all owners, delete the obsolete
   top-level `log/` directory, every historical ignored test log, and all stale
   test-produced logs from module working directories. Do this before the
   authoritative full-suite run so its output cannot be confused with legacy
   residue. Do not delete QIR or Autobahn results or their externally prescribed
   artifacts as part of this migration.
4. Add a clean-tree assertion to the verification workflow: successful tests
   may leave aged residue from deliberately injected failures, but may not
   create `.ZiTestResidue`, manifests, new files outside `logs/`, or temporary
   side names such as `*.log.N_`.

## 4. Enable the complete non-WIP test build

1. Normalize every active module's `SUBDIRS` to the guideline order `src`,
   `test`, `util`, `itest`, then other enabled directories such as `example`,
   `bench`, and `interop`; keep the documented `zdb_pq/ext` exception after the
   standard directories. This includes the foundation modules and requested
   dependents:
   `zu`, `zm`, `zt`, `ze`, `zf`, `zi`, `zrl`, `ztcp`, `ztls`, `zquic`,
   `zhttp`, `zrest`, `zmcp`, `zfb`, `zv`, `zdb`, conditional `zdb_pq`, and
   `zdf`, and adds `zws` to the top-level active traversal. Do not enable
   `zdash`, `zum`, `zcmd`, `zproxy`, or `zgtk`.
2. Preserve each module's existing `make test` semantics, including integration
   tests where its module target already invokes them. Keep `interop` after
   `itest` in modules that already build it. QIR and Autobahn execution stays
   behind its existing framework-specific targets and is not added to the
   ordinary unit/integration test run.
3. Apply the target-specific `ZI_LOGDIR` export prescribed in section 2 to the
   top-level, module, `test`, and `itest` targets. Preserve that environment in
   every matrix child environment rather than constructing a second log root.
4. Regenerate affected Automake outputs once after the Makefile edits, without
   changing or replacing the existing clang debug configuration. Then use that
   configured build as required by `GUIDELINES.md`:

   ```sh
   make -j8
   make test
   ```

   Fix build ordering and test failures before treating the residue inventory as
   authoritative; a partially executed suite is not sufficient evidence.
5. Record module, binary, exit status, and created/modified residue paths for the
   complete run. Take full before/after filesystem snapshots of `logs/` and the
   module `test/`/`itest/` working directories rather than relying only on known
   log extensions. This becomes the migration checklist and prevents old files
   from being mistaken for output of the current run.

## 5. Audit every requested `zi`-dependent test program

Audit both `test/` and `itest/` in `zrl`, `ztcp`, `ztls`, `zquic`, `zhttp`,
`zrest`, `zmcp`, `zfb`, `zv`, `zdb`, `zdb_pq`, `zdf`, and `zws`. Exclude QIR,
Autobahn, and `zdash`, `zum`, `zcmd`, `zproxy`, and `zgtk`.

For every built test executable:

1. Determine whether it can create a file through an explicit `ZiLog` sink, a
   default/lazy `ZiLog` sink reached from library code, direct `ZiFile` or C file
   I/O, child stdout/stderr capture, qlog/sqlog generation, TLS key/error logs,
   database telemetry, or a spawned utility/server.
2. If it cannot create filesystem residue, record that fact in the audit table.
   Absence of a `ZiLog::init` call alone is not proof because tests create many
   non-log fixtures and dependent library code may lazily initialize logging.
3. Route every untracked test-produced path through the shared facility, assign
   its one aging owner, register it, and close it before finalization. Replace
   the residue responsibilities of local `testPath_`, `removeTestLog_`,
   `*_TEST_KEEP`, temporary-directory wrappers, and ad hoc glob cleanup with the
   shared ownership/finalization API, not with immediate `del` calls. Retain
   only unrelated process or fixture-generation behavior in those local types.
4. Do not change stderr-only sinks (`&2`); they are outside this audit.
5. Apply the same aging and failure-retention policy to qlog/sqlog files, TLS key
   or error logs, child access logs, daemon debug logs, generated certificates,
   database files, and assertion fixtures. None is checked into the repository.
6. For spawned programs that accept a log option, pass an explicit path under
   `ZI_LOGDIR` and register it with the parent. Never rely on the child's working
   directory or default sink filename under `make test`.

Implement the migrations in this first-pass source inventory, then use the full
run to find additional producers:

| Module | Required migration and runtime check |
| --- | --- |
| `zrl` | No current source-level file-log producer found; prove this at runtime. |
| `ztcp` | Known `ZiLog` users explicitly sink to stderr. |
| `ztls` | `ZtlsAsyncTest` uses `ZiLog` without an explicit sink; `ZtlsBufHookTest` creates `tls12-err.log` in test-owned temporary storage. |
| `zquic` | Replace default `ZiLog` paths, unit-test qlog/sqlog `testPath_`/`removeTestLog_`, `TempDir::keep`, and spawned interop-server log ownership; remove `ZQUIC_TEST_KEEP` and update `zquic/GUIDELINES.md` without changing `ZquicLOG` hot-path instrumentation. |
| `zhttp` | Historical parser/client/service logs exist in `zhttp/test`; the HTTP matrix creates an access log; explicit stderr-only tests can remain unchanged. |
| `zrest` | No current source-level file-log producer found; include all spawned example/server paths in the runtime audit. |
| `zmcp` | No current file-log producer found in tests; verify child/example invocations and lazy library logging. |
| `zfb` | No current source-level file-log producer found; prove this at runtime. |
| `zv` | `ZvEngineTest` uses stderr, while historical `ZtcAppTest` and `ZtcAlertTest` logs show lazy/default logging that must be routed explicitly. |
| `zdb` | Integration tests use stderr, but `ZdbTelTest` has left aged files in its working directory. |
| `zdb_pq` | Known integration logging uses stderr; verify the conditional test and integration suites when `LIBPQ` is enabled. |
| `zdf` | Known test and integration sinks use stderr; prove that no child or library default creates a file. |
| `zws` | Enable its currently disabled tests, exclude its Autobahn framework output, and inventory any default/lazy `ZiLog` files produced by the unit tests. |

## 6. Keep interoperability cleanup framework-specific

1. Exclude QIR (`zhttp/interop/qir-results` and its prescribed log, key-log,
   qlog, and result filenames) and Autobahn outputs from `ZiTestResidue`.
2. Add cleanup to the owning QIR or Autobahn runner for transient working files
   that are not intended result artifacts. Do not rename required
   external-format files merely to fit Z test-log conventions.
3. Route any file-backed Z wrapper diagnostics around those frameworks through
   `ZI_LOGDIR`; keep external framework outputs inside that framework's result
   tree and lifecycle.

## 7. `GUIDELINES.md` conformance constraints

Apply these constraints to every implementation and migration diff, including
the complete audit-flag list rather than only the examples named elsewhere:

1. Language and layout:
   - compile as GNU C++2b; add no concepts, `requires`, typed/`enum class`
     enums, anonymous namespaces, STL facilities, non-specific lambda captures,
     unnecessary casts, forwarding compatibility functions, or dead code;
   - use file-scope `static` functions with concise names and trailing `_` for
     internal variants (`state_`, `mkdir_`, `age_`); use hard tabs with
     two-column logical indentation in code and preserve local tabular member
     alignment;
   - keep structs all-public and data-first; use no `m_` members in `State`.
2. Storage, allocation, and lifetime:
   - use the prescribed `ZmSingleton`, heap-tagged `Paths`/`Names`, and existing
     heap-tagged `Zi::Path`/`Zi::Name`; add no untagged heap allocation, STL
     container, fixed-capacity array, lookup table, or separately maintained
     array length;
   - `Age` is the sole named bounded-history constant and its maintenance
     comment records the diagnostic-retention decision; do not repeat literal
     archive depths at call sites;
   - use plain cold-path `ZtArray` storage for the small process-lifetime
     registries, move it during finalization, and release it deterministically.
     Do not scan for stale processes/files, defer garbage collection, or permit
     registry growth after `phase` leaves `Active`. `State` is cold control
     state, not independently hot thread-exclusive shards, so do not add cache
     line padding/alignment.
3. Data movement and construction:
   - build paths and suffixes directly in `Zi::Path`/`Zi::Name` buffers with
     `<<`, `ZuBox`, and existing conversions; do not create temporary contiguous
     C-string copies, use `printf` formatting, or default-construct buffers only
     to overwrite them;
   - return `Paths` through return-value elision/move and move finalization
     registries with `ZuMv`; do not snapshot/copy complete registries.
4. Algorithms and control flow:
   - keep one file-local aging loop behind `ZiFile::age`/`ageTree`; keep one
     recursive deletion implementation in `ZiFile::removeTree`; factor other
     repeated cleanup blocks into file-local functions without adding weak
     abstractions;
   - cache invariant lengths/paths outside loops, avoid repeated array indexing,
     scope iterators to their traversals, use early exits to flatten error paths,
     and use `switch` for dispatch on a single discrete value;
   - recursive traversal is permitted only on the main/control thread and only
     within a validated test-owned directory. It must never run on an I/O shard
     or scheduler thread servicing mixed work.
5. Framework fit and assertions:
   - use `ZiFile`, `ZiDir`, `ZiGlob`, Z strings/arrays, and `ZiLog`; do not add
     `std::filesystem`, `FILE`, ad hoc parsing/formatting, hashing, or a second
     path/aging abstraction;
   - use `ZiAssert` in `ZiTestResidue`, plain `assert` in `ZuTestMgr` (below
     `zm`), and the existing `_WIN32` split only for unavoidable platform shared
     memory or filesystem behavior;
   - introduce no external dependency and no virtual/runtime-polymorphic test
     support API.
6. Locking, callbacks, and teardown:
   - the residue lock protects only the state documented in section 1. Do not
     make I/O shard state accessible by adding locks, hold the lock across a
     wait/callback/log operation, or add atomics for diagnostic state;
   - the raw `ZuTestMgr::FinalFn` is the deliberate below-`zm` exception to
     `ZmFn`: it is captureless, allocation-free, and avoids a `zu` dependency on
     `zm`. Do not generalize it into a type-erased callback facility;
   - stop log/qlog writers, cancel/drain timers and I/O, reap children, and close
     Windows file handles before `final`; do not retain references from
     short-lived child/callback objects to longer-lived harness owners.
7. Tests and liveness:
   - every new repository-owned unit/integration test uses `ZuTest`/`ZuTestUtil`,
     emits TAP, and resides in `test`/`itest` with the prescribed CamelCase or
     lowercase basename convention. Any new harness that launches another
     complete process belongs in `itest`, even when its child is the same
     executable;
   - use `ZmBlock` or `ZmSemaphore` for in-process completion. Do not poll or use
     elapsed time as proof of completion. A bounded subprocess timeout is only
     a failure-containment watchdog; normal completion comes from child exit,
     IPC, or an explicit continuation;
   - introduce no background cleanup thread, timer-based garbage collection,
     or potentially unbounded loop on a mixed-workload scheduler thread.
8. Zquic-specific audit:
   - change only test-side qlog path/lifecycle configuration. Do not move qlog
     work outside `ZquicLOG`, alter runtime gating, add hot-path formatting,
     capture references/owners/payloads, or change qlog schema/event modeling;
   - replace `ZQUIC_TEST_KEEP` with result-driven `ZiTestResidue` ownership and
     update the qlog contract in `zquic/GUIDELINES.md` in the same change so the
     supplementary guidance no longer advertises the removed switch.
9. Build and diagnostic acceptance:
   - introduce no warnings or warning suppressions. Always build an affected
     `src` default target before its dependent test directory and build the test
     directory's default target before running `make test`;
   - use `libtool exec` for Valgrind/debugger runs and never execute a binary
     from `.libs` directly;
   - preserve the currently configured clang debug build exactly. Run targeted
     Valgrind leak checks in that build. Its flags do not enable ASan/LSan, so
     the separate sanitizer-configured acceptance run required by
     `GUIDELINES.md` is explicitly deferred by the instruction not to change
     the build configuration; do not claim that sanitizer verification ran.

## 8. Implementation order

Implement and rebuild in this order; do not interleave the repository-wide
migration with redesign of the support API:

1. Add `ZuTestMgr::FinalFn`, `finalFn`, `finalFn_`, and root-result invocation.
   Rebuild `zu/src` and its tests and add non-spawning callback registration and
   successful-root coverage to the existing `ZuTest` unit-test binary. Cover a
   failed root through the `zitestresidue` integration child instead of adding
   process launch to a `zu/test` binary.
2. Change `ZiFile::age` to `Path`, add `removeTree`/`ageTree`, replace every
   `ZiLog.cc::ageFile` call, and delete `ageFile`. Extend `ZiFileAgeTest` for
   tree removal/aging, non-following of symbolic links/reparse points where the
   platform exposes them, and removal-error reporting. Rebuild `zi/src` then
   `zi/test`, and run `ZiFileAgeTest` and `ZiLogTest`.
3. Implement `ZiTestResidue.cc`, its exact public API, `libZiTest.la`, and
   `zi/itest/zitestresidue` on top of those `ZiFile` operations. Rebuild
   `zi/test` and `zi/itest` once, then run the residue integration test plus the
   existing nine adopters.
4. Migrate all remaining `zi/test` and `zi/itest` producers, including explicit
   sink paths and removal of per-test `atexit` registration. Rebuild and run the
   two Zi test directories once. Apply these ownership choices to the existing
   adopters:
   - use `dir` for `ZiFileTest`, `ZiFileAgeTest`, `ZiGlobTest`, `ZiDirTest`,
     `ZiCSVTest`, `ZiLogTest`, and `ZiDaemonTest`, then derive every fixture path
     below the returned directory without separately registering its contents;
   - use `uniqueName` plus `addShm` for `ZiRingTest` and `ZiRingTest2`;
   - delete their per-program `atexit` calls, change handled-interruption hooks
     to `cleanup`, and remove the explicit successful cleanup at the end of
     `ZiDaemonTest`.
5. Migrate downstream modules in dependency order: `zrl`, `ztcp`, `ztls`,
   `zquic`, `zhttp`, `zrest`, `zmcp`, `zfb`, `zv`, `zdb`, conditional
   `zdb_pq`, `zdf`, and `zws`. After finishing each module's source edits,
   rebuild and run that module's affected test/itest binaries once; defer a full
   repository build until all modules are migrated.
6. Apply all `SUBDIRS`, `ZI_LOGDIR`, `.gitignore`, and Automake library/link
   edits as one build-system batch. Regenerate Automake outputs once through the
   existing build machinery; do not rerun `z.config` or change compiler/debug
   options.
7. Record and remove legacy residue only after all owners are migrated, then run
   the full verification sequence below.

## 9. Verification sequence

1. Start from a recorded baseline; do not use existing ignored files as evidence
   of a new leak.
2. Build all enabled non-WIP tests with `make -j8`, then run top-level
   `make test` to completion.
3. Run the requested raw inventory exactly:

   ```sh
   find . -name '*.log*'
   ```

   Classify `config.log`, QIR and Autobahn outputs, retained injected-failure
   residue, and unmanaged test-produced files separately. Supplement it with
   searches for `*.sqlog*`, `*.qlog*`, key logs, database files, certificates,
   and module-specific fixture formats and directories.
4. Run the complete suite a second time with all tests passing. Compare
   before/after snapshots. Permit only the expected rotation of already-retained
   failures; require that no current residue from the successful run remains
   under `logs/` and no generated file appears in a module working directory.
5. Force representative failures in:
   - a direct `ZiLog` file-sink test;
   - an exact-path assertion fixture and a glob-discovered fixture family;
   - a parent harness with child capture;
   - a test owning temporary files and shared memory.

   Verify nonzero status, retained and readable filesystem residue under
   `logs/`, the current generation plus archives `.1` through `.8`, release of
   non-filesystem resources, and no manifest creation.
6. Exercise handled interruption and ordinary abnormal child exit. Verify
   children are reaped, sinks are closed, shared memory is removed, and
   filesystem residue remains. Do not add hard-crash or `SIGKILL` recovery tests.
7. Run targeted leak checks through libtool in the current clang debug build:

   ```sh
   libtool exec valgrind --leak-check=full --error-exitcode=1 \
     ./zi/itest/zitestresidue
   libtool exec valgrind --leak-check=full --error-exitcode=1 \
     ./zi/test/ZiLogTest
   ```

   Do not run `.libs` binaries. Record the ASan/LSan acceptance run as deferred
   by the fixed-build constraint; the current configuration has neither
   sanitizer enabled.
8. Run `git diff --check`, scan for anonymous namespaces in the new `.cc`, scan
   for `inline` non-template `ZiTestResidue` implementations, and verify no
   references to `.ZiTestResidue`, manifest helpers, old local residue paths,
   unaged generated files, or ad hoc fixture glob/removal utilities remain.
   Review every changed source file against each red/amber audit flag in
   `GUIDELINES.md` and the zquic supplementary checklist; treat warnings and
   unexplained exceptions as failures.
9. Keep the implementation and harness platform-neutral: use portable Z path,
   process, and child-launch APIs; preserve Windows file-closing/removal and
   aging semantics; and add no Windows exclusions. Execute the checks in the
   existing clang debug build. A separate Windows test run is not required for
   this work.

## Acceptance criteria

- `ZiTestResidue.hh` contains declarations only; `ZiTestResidue.cc` contains the
  implementation with file-scope `static` internal functions,
  `ZmSingleton<State>` ownership, and no anonymous namespace.
- The state layout visibly identifies every member protected by `State::lock`.
- `mkdir_` replaces `mkdirIfNeeded_`; `trimLine_` and the complete manifest
  mechanism are gone.
- `cleanup()` replaces `cleanupNow()` throughout the API and callers, with no
  compatibility forwarder.
- No `.ZiTestResidue` directory or manifest is created, and all existing copies
  have been removed after migration.
- `ZiTestResidue` supplies common test-scoped full-path, aging/registration,
  directory, glob-family, pattern-removal, and intentional-child-namespace
  capabilities through the exact `path`, `file`, `dir`, `add`, `glob`,
  `del`, `uniqueName`, `addShm`, `final`, and `cleanup` API using Z framework
  types.
- `ZiFile::age` accepts `Path`; `ZiFile::removeTree` and `ageTree` provide the
  shared directory operations without a second aging loop. The duplicate
  `ZiLog.cc::ageFile` implementation is gone and both self-aging sink types,
  `ZiFileSink` and `ZiCSVSink`, call `ZiFile::age`.
- `ZuTestMgr` reports the final root result through the prescribed `FinalFn`
  callback, and `ZiTestResidue` uses it to distinguish successful cleanup from
  failed-residue retention without per-test finalization boilerplate.
- All active non-WIP test directories, including `zws` and excluding `zdash`,
  participate in guideline-order top-level `make -j8`; top-level `make test`
  passes completely with `ZI_LOGDIR=$(abs_top_builddir)/logs`.
- Every exact lifecycle-owned file in scope, including assertion fixtures, is
  aged exactly once before its producer with depth eight: through `file()`, or
  by a registered `ZiFileSink`/`ZiCSVSink` designated as the aging owner.
  Variable families use `ZiFile::ageTree` because ordinary file removal cannot
  remove non-empty directories. Additional aging performed
  inside an owned directory specifically as the operation under test does not
  create a second lifecycle owner. Current residue disappears after success and
  remains after failure; registered non-filesystem resources are always
  released.
- Under `make test`, no generated residue is left in a module `test/` or
  `itest/` working directory. Direct runs fall back to their current working
  directory when `ZI_LOGDIR` is unset.
- QIR and Autobahn result files remain governed by their specific frameworks;
  only framework-designated transient files are removed by those runners.
- Stderr-only sinks are unchanged.
- Two consecutive successful full-suite runs produce no new unmanaged residue;
  forced failures prove retention, aging, and cleanup behavior.
- The current clang debug build is warning-free, targeted libtool/Valgrind leak
  checks pass, and the report accurately marks ASan/LSan as not run because the
  fixed configuration does not enable them.

## Open questions and ambiguities

No implementation-blocking questions remain. All test-produced filesystem
residue—including diagnostic logs and generated assertion fixtures—is untracked,
aged either exactly or as an enclosing directory generation, removed on success,
and retained on failure. QIR and Autobahn remain the only framework-owned
exceptions identified for this work.
