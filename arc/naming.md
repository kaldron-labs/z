# Module layout and naming migration plan

## Objective

Give every Z module a standard directory vocabulary and make the names within
each standard directory communicate the role of the code:

| Directory | Required | Contents | Naming |
|---|---:|---|---|
| `src` | yes | library and program source | CamelCase library components; lowercase program-specific files |
| `test` | yes | unit tests using `ZuTest` and emitting TAP | CamelCase |
| `util` | no | utilities used by `itest`, `interop`, or `bench` | lowercase |
| `itest` | no | integration tests using `ZuTest` and emitting TAP | lowercase |
| `example` | no | dependent example programs demonstrating the module API | lowercase |
| `bench` | no | benchmark harnesses | lowercase |
| `interop` | no | external interoperability frameworks and their fixtures | lowercase |

This is a source-layout migration, not a compatibility exercise. Rename and
move files directly and update all dependants; do not leave forwarding files,
duplicate targets, or legacy path aliases.

## Naming decisions to add to `GUIDELINES.md`

Add a **Module layout** subsection under **Build system** and extend **Naming /
Casing and member prefixes** with the following rules:

1. Every module has `src` and `test`. The other five standard directories are
   created only when the module has content of that role. Do not add empty
   optional directories. These names are not an exhaustive allow-list:
   additional module-specific directories are permitted as exceptions when
   they represent a distinct facility which does not fit a standard role. For
   example, `zdb_pq/ext` is a PostgreSQL extension and remains `ext`. Document
   each exception in the module's README or `GUIDELINES.md`; do not invent a
   new directory where a standard role already applies. An exceptional
   directory follows its native domain convention or a convention documented
   with the exception; it does not inherit casing merely from its position
   after the standard directories.
2. `test` is for isolated unit tests. A test which launches complete programs,
   needs multiple services, exercises persistence or networking end to end, or
   requires environment setup belongs in `itest`. Tests specifically exercising
   a third-party implementation or conformance framework belong in `interop`.
3. Unit and integration test programs use `ZuTest`/`ZuTestUtil`, print valid
   TAP, and are run by the corresponding directory's `make test` target.
   Convert repository-owned shell-only TAP tests in `test` or `itest` to
   ZuTest-based C++ test drivers; shell scripts may remain as fixtures invoked
   by those drivers. External harness scripts may remain native in `interop`.
   Benchmarks and examples are not part of `make test`. An `interop` directory
   may contain both TAP self-tests and an explicitly invoked external harness.
4. `util` owns reusable executables and scripts needed by `itest`, `interop`,
   or `bench`; those consumers refer to the utility build directory rather than
   copying or compiling utility implementation files into their own targets.
5. Repository-owned library/component basenames in `src`, and
   repository-owned unit-test code/data basenames in `test`, are CamelCase and
   use the module prefix, such as `ZmLib`, `ZmTest`, or `ZhttpParserTest`.
   Programs built in `src` are the exception: their program target and
   program-specific source/resource basenames are lowercase. Repository-owned
   code/data names in `util`, `itest`, `example`, `bench`, and `interop` are
   also lowercase. Preserve
   normal extensions (`.cc`, `.hh`, `.c`, `.h`, `.fbs`, `.sh`, and so on).
   Conventional control filenames such as `Makefile.am`, `README.md`,
   `Dockerfile.*`, and `.gitignore` are not renamed merely to satisfy directory
   casing.
6. A program built in `src` has a succinct lowercase name suitable for
   command-line use. Its entry-point file normally matches the command; for
   example, `bin_PROGRAMS = zcmd` uses `zcmd_SOURCES = zcmd.cc`. Additional
   source or resource files owned solely by that program are likewise
   lowercase. Existing compliant names such as `zcmd`, `zdash`, `ztotp`,
   `zum`, `zproxy`, `zrest`, `zws`, and `zwsd` remain unchanged. Lowercase
   is the requirement; separators are a separate naming decision governed by
   succinctness and existing convention.
7. Native names required by an external tool or protocol are exceptions:
   PostgreSQL extension/control/SQL names, FlatBuffers field/schema contracts,
   generated files, Dockerfile conventions, and imported fixtures retain their
   mandated spelling. FlatBuffers schema filenames specifically retain their
   established lowercase/underscore basenames, together with matching
   `Makefile.am` generation rules and generated-header names. State the
   exception next to the relevant build or distribution list; do not use it for
   ordinary Z-owned C++ files.
8. Module build traversal has a fixed dependency prefix: `src`, `test`,
   `util`, then `itest`, omitting an absent optional directory but preserving
   that relative order. Everything else--`example`, `bench`, `interop`, and
   exceptional directories--comes afterward and is ordered according to its
   dependencies. Document why each exceptional directory exists and where it
   sits in that trailing order; for example, with no `util` or `itest`,
   `zdb_pq/ext` may remain after `test`.
9. `itest` may depend on programs or scripts built by `util`; the placement of
   `util` immediately before `itest` is the build-order guarantee for that
   dependency. Keep `util` independently buildable and do not introduce a
   reverse dependency from `util` to `itest`.
10. A module commented out of top-level `Makefile.am` `SUBDIRS` is work in
    progress. Apply the same structural moves and renames to it, and update its
    source Automake inputs, but do not uncomment it or attempt to build or test
    it. Determine this set from `Makefile.am` at the start of the migration;
    currently it is `zum`, `zcmd`, `zproxy`, `zgtk`, and `zrest`. Conditional
    but uncommented modules such as `zdb_pq` are not WIP under this rule. For
    validation, "active" means reachable through uncommented top-level
    `SUBDIRS` in the relevant configured feature set. A module which is neither
    active nor commented out is "standalone"; it may be built/tested directly
    when all of its dependencies are active, but its validation must never
    cause a WIP dependency to be built. The current standalone modules are
    `zdash` and `zws`.

Also update the existing build examples so they mention all applicable roles
(`src`, `test`, `util`, `itest`, `example`, `bench`, and `interop`), and document
that `libtool exec` applies to executables in any of them.

## Migration method

Start with a role manifest of every module target and tracked file being moved:
library component, `src` program, unit test, utility, integration test, example,
benchmark, interop, exceptional-domain file, generated file, imported fixture,
or conventional control file. Use that classification for both destination and
casing; do not infer a file's role from its current directory alone.

Perform renames with `git mv`, including case-only renames through a temporary
name so the same commits work on case-insensitive filesystems. In each module
change all of the following together:

- `Makefile.am` targets, `_SOURCES`, headers, scripts, data, `SUBDIRS`, and
  relative include/library paths;
- `configure.ac` `AC_CONFIG_FILES` entries;
- C/C++ includes, generated-file rules, shell commands, runtime sibling-binary
  lookup, Docker staging, and test-driver paths;
- documentation and developer references in `README.md`, `CODEBASE.md`, module
  docs, top-level scripts, and comments that name real paths;
- clean/distribution lists so old names cannot survive in release tarballs.

Before each module move, use `rg` to enumerate references to every old path and
basename. After it, verify both that every new reference resolves and that
`git ls-files` plus `rg` find no stale old path. Do not treat ignored build-tree
artifacts (`.deps`, `.libs`, generated Makefiles) as source; clean and regenerate
them instead.

All phase-level build and test acceptance below is mandatory for active
modules. Directly validate a changed standalone module when all dependencies
needed for that validation are active; otherwise record the WIP dependency and
limit that module to source/reference audits. For WIP modules, perform
tracked-source, Automake-input, and reference audits only; do not compile,
link, execute, run generated-code checks, or claim build correctness.

## Phase 1: establish the directory contract

1. Add the rules above to `GUIDELINES.md`.
2. Remove empty optional directories rather than retaining placeholders;
   currently `zhttp/example` contains only `Makefile.am` and should be removed
   from `configure.ac` and `zhttp/Makefile.am` unless real examples are added.
3. Add required `test/Makefile.am` files and meaningful TAP unit-test targets
   where absent. The current missing `test` directories are `zdash/test` and
   `zproxy/test`; `ze/test` and `zum/test` currently have no test source. If an
   entry point has no unit-testable seam, first factor parsing or configuration
   into a source component, using CamelCase for library code or lowercase for
   program-specific code, and test that component; do not add a test that only
   prints a passing TAP line. Only restructure WIP modules such as
   `zproxy` and `zum`; do not build or run them. `zdash` is currently
   standalone but depends on WIP `zgtk`, so do not build it unless that
   dependency becomes active independently of this migration.
4. Audit every executable retained in `test` or moved to `itest`, not just its
   filename. Convert legacy exit-status, hand-written TAP, or shell-only tests
   to `ZuTest`/`ZuTestUtil` drivers, retaining scripts only as fixtures where
   useful. Standardize directory and module `make test` targets so they build
   the default target first and run the TAP executables; helper libraries and
   non-executable fixtures are not themselves tests. Apply the source changes
   but do not execute this audit's WIP targets.
5. Normalize every module `SUBDIRS` line to build `src`, `test`, `util`, and
   `itest` first in that dependency order, followed by everything else in its
   own dependency order. Enable required `test` directories within active
   modules where they are currently omitted from the module build (`zu`, `zm`,
   `zt`, `ze`, `zf`, `zi`, and any similarly disabled active module at
   migration time). Preserve every top-level WIP comment; restructuring does
   not authorize enabling a module.
6. Add the new Makefiles to `configure.ac`, rerun `./z.config -c ...`, and make
   the clean configured active tree the baseline for later phases. Generating
   a WIP module's Makefile is permitted; compiling or executing its targets is
   not.

Acceptance: every active configured Z module builds `src` and `test`; every
active module's `make -C <module>/test test` emits valid TAP and succeeds.
Eligible standalone modules receive the same direct validation. WIP modules,
and standalone modules blocked by WIP dependencies, have the intended source
layout with no build or test result claimed; WIP modules remain commented out.

## Phase 2: split mixed-purpose directories

Move complete functional groups, including shared headers and data, rather than
leaving cross-directory `../test/*.cc` source inclusion.

### `zhttp`

- Create `util` and move the `zhttp` and `zhttpd` programs out of `test`, along
  with `ZhttpdUtil`, `Zhttpd`, `ZhttpPut`, and their boundary documentation.
  Rename all moved files to lowercase (`zhttpdutil.cc`, `zhttpd.hh`,
  `zhttpput.hh`, and so on). Keep the source-boundary check in `test`, convert
  it from `ZhttpBoundaryTest.sh` to a ZuTest-based `ZhttpBoundaryTest.cc`, and
  update it to inspect the new `util` paths.
- Create `itest` and move `zhttpmatrix` there. Move process-level tests which
  exercise the complete Z-owned `zhttp`/`zhttpd` programs into `itest`,
  including the app, multi-request, lifecycle/transport, client-fallback, and
  H2 matrix coverage after confirming each test's actual boundary. Rename their
  files and targets to lowercase. `zhttpmatrix` remains the integration-test
  exemplar even though some matrix rows optionally invoke third-party peers;
  external-framework-specific suites still belong in `interop`.
- Keep parser, codec, field, frame, session, message, engine, URL, Alt-Svc,
  discovery, and other isolated library unit tests in `test` with CamelCase
  names.
- Move tests whose purpose is compatibility with Caddy, curl, darkhttpd,
  nghttp, or another external implementation into `interop`; fold the current
  `Zhttp3InteropTest` and `ZhttpH2InteropTest.sh` into that harness and rename
  all existing QIR C++ files/targets to lowercase (`zhttpqirtest`,
  `zhttpqirsmoketest`, `zhttpqir`, etc.).
- Make `itest` and `interop` consume the built `util/zhttp` and `util/zhttpd`.
  Put reusable utility implementation behind a lowercase `util` convenience
  library or equivalent linkable target, and stop compiling
  `../test/ZhttpdUtil.cc` or including `../test/zhttpd.cc` from interop. Move
  tests of utility parsing/behavior to `itest`, or factor genuinely reusable
  logic into a CamelCase `src` component tested by `test`; `test` must not gain
  a dependency on the later-built `util` directory.
- Update `zhttp/README.md`, the renamed `util/zhttpboundary.md`, top-level QIR
  targets, image scripts, and runtime binary discovery to the new paths.

### `zws`

- Create `bench`; move `ZwsBench.cc` to `bench/zwsbench.cc` and rename its
  target and displayed program name.
- Create `interop`; move `ZwsAutobahnClient.cc`, the `autobahn/` configuration
  and runner scripts, and the Binance external-service test there, using
  lowercase Z-owned names. Keep imported Autobahn field/file spelling only
  where its tools require it.
- Leave protocol and H1/H2/H3 engine unit tests in `test`.
- Convert `ZwsSourceBoundaryTest.sh` to a CamelCase ZuTest C++ unit test. Keep
  runner scripts only as lowercase interop fixtures where external tooling
  requires them.

### Other misplaced programs

- Move `zgtk/test/zgtkdemo.{cc,glade}` to
  `zgtk/example/zgtkdemo.{cc,glade}`; add an actual `zgtk/test` unit-test target
  if none remains.
- Move `zrest/test/zrclient.cc` and `zrserver.cc` to `zrest/example`: they are
  dependent demonstration programs and do not emit TAP. Add a distinct
  CamelCase unit test under `zrest/test` so the required directory remains a
  real test suite.
- Move `zrest/go` under `zrest/interop/go`, because it is an external-language
  peer implementation; update its Makefile/certificate paths and add the
  module-level `interop/Makefile.am` driver.
- Treat `zcmd/test/cmdtest.cc` plus its database/key setup scripts as an
  integration fixture: move them to `zcmd/itest`, rename them lowercase, and
  make the harness emit TAP. Add or retain separate isolated CamelCase unit
  tests in the now-required `zcmd/test` suite.
- Audit the database smoke/replication programs and configuration scripts in
  `zdb/test`. Keep genuine in-process unit tests in `test` after CamelCase
  renaming; move multi-process, persistent-store, replication, and shell-driven
  scenarios to `zdb/itest` with lowercase names.
- Keep `zdb_pq/ext` as a documented module-specific directory: it is a
  production PostgreSQL extension and does not match any of the seven standard
  roles. Add the exception and its native naming/build convention to the
  module README or `GUIDELINES.md`. Audit its PGXS paths and module `SUBDIRS`
  entry during the migration, but do not move or rename the directory.
  PostgreSQL-mandated extension, SQL, and regression basenames retain native
  lowercase spelling under the external-tool exception.

Acceptance: in active and directly validated standalone modules, `test`
contains no driver that merely starts a service and no non-TAP executable;
`itest` targets emit TAP; examples and benchmarks are not run by `make test`;
interop self-tests pass when their documented prerequisites are available and
skip cleanly when they are not. WIP modules satisfy the same source
classification without being built or run.

## Phase 3: normalize lowercase directories

Rename repository-owned source, header, script, and program stems in optional
lowercase directories, updating internal symbols only where they represent the
program rather than public library API:

- `zi/example`: `ZiMxClient`, `ZiMxServer`, `ZiMxUDPClient`,
  `ZiMxUDPServer`, `ZiLogTest`, `Global`, and `HttpHeader` to lowercase names;
  reconsider `ZiLogTest` as a unit test if it asserts behavior rather than
  demonstrating usage.
- `ztcp/example`, `ztls/example`, and `zquic/example`: rename the `*Client` and
  `*Server` files/targets to `ztcpclient`/`ztcpserver`,
  `ztlsclient`/`ztlsserver`, and `zquicclient`/`zquicserver`.
- `zu/bench`: rename `ZuBoxBench` and `ZuSearchBench` to `zuboxbench` and
  `zusearchbench`.
- `zm/bench`: rename all `Zm*` files/targets to lowercase. Classify
  `ZmHeapTest2` and `ZmLockTest2`: move correctness tests to `zm/test` as
  CamelCase TAP tests, otherwise give them benchmark names. Rename
  `ZmHashThread` according to what it measures, with a `bench` suffix.
- Apply the same lowercase rule to the new `util`, `itest`, `bench`, and
  `interop` files created in Phase 2, including QIR and Autobahn support C++.

Acceptance: an audit of tracked, repository-owned code/data files in lowercase
directories reports no unexplained uppercase basename; conventional control
files, generated files, imported fixtures, and documented native-domain names
are excluded. All Z-owned Automake program stems in those directories are
lowercase.

## Phase 4: normalize `src` library components and `test`

### Library, support, and C/C++ source names

Preserve succinct lowercase program targets and their program-specific source
and resource files in `src`. In particular, do not rename `zcmd.cc`,
`zdash.cc`, `zproxy.cc`, `zrest.cc`, `ztotp.cc`, `zum.cc`, `zws.cc`, or
`zwsd.cc`; also preserve lowercase resources owned solely by those programs,
such as `zdash.glade` and the `zdash` FlatBuffers schemas. Rename lowercase
Z-owned library and support files that are not owned solely by a program:

- Rename internal lowercase support files: `zdf/src/stats_tree.hh` ->
  `ZdfStatsTree.hh`, `zi/src/zi_netlink.h` -> `ZiNetlinkC.h`, and the
  repository-owned Zu C API/implementation files to `Zu*` CamelCase names.
  Rename `msvc_intrin.cc` to a `Zu`-prefixed CamelCase name. Preserve exported
  C symbol spelling; this phase changes files, not ABI.

Normalize lowercase unit-test families and their helpers, including:

- `zcmd/test/cmdtest` -> `ZcmdTest` if any unit portion remains;
- `zdb/test/zdbtest`, `zdbsmoketest`, `zdbreptest`, and `zdbreptest2` ->
  `ZdbTest`, `ZdbSmokeTest`, `ZdbRepTest`, and `ZdbRepTest2` for cases that
  remain unit tests;
- `zdb_pq/test/zdbpqtest` -> `ZdbPQTest`;
- all `zdf/test/zdf*test` -> corresponding `Zdf*Test` names;
- `zfbtest`, `zfbtest2`, and `zfbtest3` -> `ZfbTest`, `ZfbTest2`, and
  `ZfbTest3`;
- `zm/test/libZmBTTest*.cc` -> names beginning with `Zm`, such as
  `ZmBTTestLib.cc`, while retaining library target semantics.

Rename Z-owned scripts, maps, configuration, and tuning data in `src`/`test` to
CamelCase where their consumers do not impose spelling. FlatBuffers schemas are
an explicit exception: preserve the established lowercase/underscore
`fbs/*.fbs` families in `zcmd`, `zdb`, `zdf`, `zfb`, `zum`, and `zv`, including
their `Makefile.am` generation rules, generated-header names, includes, install
lists, and internal schema references. Record specific exceptions for SQL/PGXS,
generated FlatBuffers output, standard `README`/`Makefile`/Docker names,
protocol fixtures, and any runtime filename that cannot be changed.

Acceptance: a tracked-file audit reports no unexplained lowercase Z-owned
library/support basename in `src` and no unexplained lowercase Z-owned
code/data basename in `test`; lowercase `src` program-specific source/resource
basenames are accepted when they follow the program's succinct command-line
name, and conventional/native/generated exceptions are excluded. In active and
directly validated standalone modules, all includes and generated-file
dependencies are resolved from a clean build; WIP modules receive reference
audits only.

## Phase 5: build and repository-wide verification

1. Re-read top-level `Makefile.am` and record active, WIP, and standalone
   modules. Exclude WIP modules from every build, test, sanitizer, valgrind, and
   dependent-rebuild command below. Validate changed standalone modules
   directly only when doing so does not build a WIP dependency. Search all
   modules, including WIP modules, for every removed directory and old basename
   in tracked files. Pay particular attention to `CODEBASE.md`, module READMEs,
   shell scripts,
   `EXTRA_DIST`, Docker build contexts, source-boundary tests, runtime sibling
   lookup, and comments used as maintenance instructions.
2. Run `autoreconf` through `z.config -c`, then a top-level `make clean` and
   `make -j8` with the normal configured feature set. Because WIP modules remain
   absent from top-level `SUBDIRS`, these commands must not enter them. Do not
   validate against stale generated Makefiles or `.deps` files.
3. Run top-level `make test`, preserving the module build sequence `src`,
   `test`, `util`, `itest`, then everything else. Run each optional directory's
   intended target: `make -C <module>/itest test`,
   `make -C <module>/interop test`, and explicit builds of `util`, `example`,
   and `bench`, for active modules and directly validatable standalone modules.
4. Repeat the clean build and tests with clang; run the TAP suites under ASan/
   LSan and representative moved binaries under `libtool exec` with valgrind.
5. On a case-insensitive checkout (Windows/msys2), verify that no case-only
   collision or stale include remains.
6. Build a distribution tarball and inspect its file list for old paths,
   omitted fixtures, accidental build products, and casing violations.

Final acceptance: every module, including WIP modules, follows the standard
directory contract or has its additional directories documented as explicit
exceptions; all Z-owned names follow the casing of their role; active and
directly validated standalone unit/integration tests use ZuTest and emit TAP;
clean gcc, clang, and MinGW builds of active modules resolve only the new paths;
standalone validation does not build WIP dependencies; WIP modules remain
disabled and have not been built or tested; and `rg` finds no undocumented
legacy name.

## Commit sequence

Keep the migration bisectable:

1. guidelines and test-directory scaffolding;
2. one commit per module's role-based moves, beginning with `zhttp` and `zws`;
3. one commit per module's lowercase-directory normalization;
4. one commit per module's `src`/`test` normalization;
5. repository-wide documentation, generated-file, distribution, and platform
   verification fixes.

Each commit affecting active modules must configure, build the changed active
module and its active dependants, and run the affected TAP suites. Directly
validate changed standalone modules only when their dependency closure excludes
WIP modules. Commits affecting only WIP modules receive source/reference audits
but no build or test. Do not combine unrelated behavioral changes with the
moves.
