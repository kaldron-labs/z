# Build, test, and development

Part of the repository guidance mapped in [AGENTS.md](AGENTS.md).

## Build system

### Module layout
- The Z modules are bundles of libraries and programs
- Every Z module is succinctly named `z*`, e.g. `zu`, `zdb`
- Z libraries scope naming with a corresponding prefix, e.g. `Zu`, `Zdb`
- Every Z module has these required subdirectories:
  - `src`: library and program source code
  - `test`: unit tests using `ZuTest`/`ZuTestUtil` and emitting TAP
- A module may also have these optional subdirectories when it has content of
  the corresponding role:
  - `util`: utility programs and scripts used by `itest`, `interop`, or `bench`
  - `itest`: integration tests using `ZuTest`/`ZuTestUtil` and emitting TAP
  - `example`: dependent programs demonstrating use of the module
  - `bench`: benchmark harnesses
  - `interop`: external interoperability framework testing
- The standard directories are not an exhaustive allow-list. Additional
  module-specific directories are permitted as documented exceptions when a
  distinct facility does not fit a standard role; for example, `zdb_pq/ext`
  contains a PostgreSQL extension, follows PostgreSQL/PGXS naming, and is built
  after the standard directories. Document each exception in the module's
  README or `GUIDELINES.md`, including its naming and build convention.
- `test` is for isolated unit tests. Tests which launch complete programs,
  require multiple services, exercise persistence or networking end to end, or
  require environment setup belong in `itest`. Tests specifically exercising a
  third-party implementation or conformance framework belong in `interop`.
- Repository-owned unit and integration test programs use
  `ZuTest`/`ZuTestUtil`, emit valid TAP, and run from the corresponding
  directory's `make test` target. Shell scripts may be fixtures invoked by a
  ZuTest driver; external harness scripts in `interop` may remain native.
- `util` is independently buildable. `itest` may depend on programs or scripts
  built by `util`; `util` must not depend on `itest`.
- Module build traversal sequence is: `src`, `test`, `util`, `itest`, other
  directories in dependency order
- Headers should be organized as a directed acyclic graph with idempotent
  inclusion guards. Tail inclusion should NOT be used unless there is an
  exceptional reason. Circular depndencies should be avoided by factoring
  out common code (example: `ZmFn_.hh` is factored out from `ZmFn.hh` to
  avoid inducing circular dependencies in dependents).

### Build configuration
- use the `configure` wrapper named `z.config` to reconfigure the build
  - usage: `z.config -h`
- Linux executables and shared libraries use zstd-compressed DWARF at link time
  - requires a linker and debugger with zstd-compressed ELF debug-section support
  - retains full debug information; existing input objects can be reused
- this is a hierarchy of Makefiles that **intentionally** do not automatically rebuild dependencies in other directories
  - use a top-level `make -j8` to ensure all dependencies are rebuilt/refreshed
- add external dependencies in `configure.ac`
  - use `PKG_CHECK_MODULES` if the dependency is in pkg-config, falling back to dependency-specific `m4` if not
- always recompile with `make -j8`
  - if not rebuilding everything, if library code has changed in a layer under development, always rebuild `src` before dependent `test`/`util`/`itest`/`example`/`bench`/`interop`, e.g. `make -C zquic/src -j8`
  - always build default target `all` before `make test`, e.g. `make -C zquic/test -j8 && make -C zquic/test test`
  - after build type has changed (release, debug, asan, etc.) do a top-level `make clean && make -j8` to rebuild
  - release-build verification must be configured through `z.config`; after configuring the release build, run top-level `make clean` and then top-level `make -j8` so all dependent Z libraries and tests are rebuilt consistently
  - do not verify a release build by manually compiling selected modules or by rebuilding only dependent subdirectories; that can mix stale objects, stale generated dependency files, or libraries built under a different build type
- no warnings in build
  - they should be suppressed in the code if false-positive

## Acceptance
- test suites must pass
  - clang: address sanitizer, leak sanitizer
  - valgrind: memcheck with leak checking

## Debugging
- use `libtool exec` to run programs from `src`, `test`, `util`, `itest`,
  `example`, `bench`, or `interop` under debugging tools within the source tree
  - do not run the binaries in  `.libs` directly - library paths will be incorrect
  - `libtool exec` should be used to run `gdb`, `valgrind`, etc.

## Quick commands

- `./z.config /opt/z` configures the build with a prefix; use `./z.config -h` for options. Common flags include `-c` to rerun `autoreconf`, `-d` for debug, `-L` for clang, `-G` to disable Gtk, `-P` to disable PostgreSQL, and `-M` for MinGW.
- `make -j8` builds all enabled modules plus their test binaries.
- `make install` installs libraries/headers into the configured prefix.
- `make test` runs tests.
- Use `libtool exec` to run source-tree test binaries under `gdb`, `valgrind`, and similar tools; do not run binaries from `.libs` directly.
- Some optional features (for example libbfd backtracing) require external builds; see `README.md` for platform-specific steps.


## Testing Guidelines
- Tests are built as standalone binaries under each module’s `test/` directory.
- Run specific tests directly from the build tree, for example
  `./zt/test/ZtArrayTest` or `./zdb/itest/zdbsmoketest`.
- Use `ZuTestUtil` and underlying `ZuTest` for TAP-emitting unit tests.
- No top-level coverage target is defined; document any new test entry points in the module `test/` directory.


## Commit & Pull Request Guidelines
- Existing history uses extremely short subject lines (for example `.`); keep subjects concise and focused, and add a module prefix when it improves clarity (for example `zdb: add repl test`).
- For PRs, include a short summary, list touched modules, and note tests run plus platform/toolchain details.


## Dependencies & Configuration Notes
Core dependencies include `libck`, `hwloc`, `pcre`, and `zpicotls`. Canonical `flatbuffers` and `flatc` are used only by explicit FlatBuffers interop targets. Optional components include `libpq` (PostgreSQL) and `gtk+3`; enable/disable via `z.config` (`-P`, `-G`) or `configure` flags.
Add external dependencies in `configure.ac`; use `PKG_CHECK_MODULES` when the dependency is available through pkg-config, falling back to dependency-specific `m4` logic when needed.
