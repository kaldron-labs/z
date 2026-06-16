# Repository Guidelines

These instructions summarize repository layout and day-to-day commands.
`GUIDELINES.md` extends this file and is authoritative for engineering style,
performance expectations, Z Framework idioms, and review criteria. When editing
code, read the relevant parts of `GUIDELINES.md` first and apply them over any
generic C++ preference.

## Project Structure & Module Organization
The repository is organized by module prefix (for example `zu`, `zm`, `zt`, `ze`, `zi`, `zdb`). Each module typically contains `src/` for library code and `test/` for module-specific test binaries. Top-level build inputs live in `configure.ac`, `Makefile.am`, and `m4/`. Helper scripts live under `scripts/` and the `z.config` wrapper centralizes configure flags.

## Build, Test, and Development Commands
- `./z.config /opt/z` configures the build with a prefix; use `./z.config -h` for options. Common flags include `-c` to rerun `autoreconf`, `-d` for debug, `-L` for clang, `-G` to disable Gtk, `-P` to disable PostgreSQL, and `-M` for MinGW.
- `make -j` builds all enabled modules plus their test binaries.
- `make install` installs libraries/headers into the configured prefix.
- `make test` runs tests.
- Use `libtool exec` to run source-tree test binaries under `gdb`, `valgrind`, and similar tools; do not run binaries from `.libs` directly.
- Some optional features (for example libbfd backtracing) require external builds; see `README.md` for platform-specific steps.

## Engineering Posture
- Act as a principal engineer for performance-oriented, low-latency C++ systems and network programming.
- Backward/dependent compatibility is not a goal unless explicitly requested; propagate breaking API changes through dependent code instead of adding shims or forwarders.
- Prefer pragmatic performance over generic safety dogma: mutability, type punning, and target-system-specific behavior are acceptable when correct for the supported platforms.
- Target current gcc/clang, x64 Intel/AMD and ARM, Linux and Windows via msys2/mingw.

## C++ and Framework Style
- C++ is compiled as GNU C++2b; headers use `.hh`, sources use `.cc`.
- Do not use C++ concepts or `requires`; express constraints with SFINAE, detector traits, and established `ZuIfT` patterns.
- Prefer CRTP, templates, compile-time dispatch, ADL tagging, and side-effect-safe base defaults over virtual polymorphism or runtime dispatch.
- Minimize STL use. Prefer C headers where equivalent, and prefer existing `Zu*`, `Zm*`, `Zt*`, `Zi*`, and module-local facilities over standard-library or ad hoc replacements.
- Use Z conventions for assertions, formatting, containers, metadata, I/O, scheduling, callbacks, and persistence: for example `ZuAssert`/`ZmAssert`/`ZiAssert`, `ZuBox`/`ZuFmt`/`ZuPrint`, `ZmHash`/`ZmList`, `ZtJSON`, `ZiIOBuf`, `ZmScheduler`, `ZmFn`, and `Zdb`.
- Avoid unnecessary casts among Z span/string/array types; these types usually interoperate directly.

## Layout and Naming
- Follow existing file headers and nearby module style.
- IMPORTANT - Indentation uses hard tabs (`noet`, `ts=8`) with a tab-stop of 8, with a 2-column logical C++ indent (`sw=2`); local code precedent wins when details disagree.
- Library headers should follow the skeleton in `GUIDELINES.md`: editor modelines, copyright/license block, include guard, component library include, ordered direct dependencies, then body.
- Keep names concise; 32 bytes is a hard upper limit. Use project abbreviations such as `pkt`, `cli`, `srv`, `prot`, and `res`.
- Names are generally camelCase. `m_` is reserved for private data members of classes. Structs are all-public data without `m_`; classes have private data at the bottom.
- Use overloaded getters/setters (`x() const`, `x(value)`) rather than separate names. Use trailing underscores for internal-only or thread-dedicated variants.

## Performance and Amber Flags
- Treat heap allocation, hidden copies, temporary contiguous conversions, default initialization before overwrite, and allocation without `ZmHeap`/`ZmVHeap`/`ZmHeapID` as review issues unless justified.
- Avoid fixed-size arrays, fixed-size lookup tables, unexplained hard-coded capacities, and arrays whose storage choice is not workload-aware. Prefer `ZuArray`, `ZtArray`, `ZtString`, `ZtLocalArray`, `ZtBuiltin`, `ZmHash`, or related framework containers as appropriate.
- Operate in place on mutable buffers or write directly into uninitialized destination storage when it avoids copies; Z array containers are intentionally uninitialized until filled.
- Prefer `switch` for branching on one discrete value, flatten nested control flow, and factor repeated blocks with templates, CRTP, or local helpers that preserve performance.
- Delete dead or historical compatibility code unless compatibility is explicitly required.

## I/O and Sharding
- Keep Rx-owned state on the Rx thread and Tx-owned state on the Tx thread. Do not make non-owner access acceptable by adding locks.
- Public entry points callable from either shard should be thin dispatchers that capture only destination-owned data and immediately `rxRun`/`rxInvoke` or `txRun`/`txInvoke` to trailing-underscore variants.
- Snapshot small fixed-size data by value for cross-shard posts. Put large or variable-size data into destination-owned buffers or queues and capture only handles plus fixed metadata.
- I/O buffers are the exception to stack-preferred scratch storage: use pooled `ZiIOBuf`-style heap buffers, move them by reference, and preserve pool tuning.
- Use raw back-pointers for I/O dependent structures when that is the local pattern; owners must drain or cancel dependent work during shutdown.

## Testing Guidelines
- Tests are built as standalone binaries under each module’s `test/` directory.
- Run specific tests directly from the build tree, for example `./zt/test/ZtArrayTest` or `./zdb/test/zdbsmoketest`.
- Use `ZuTestUtil` and underlying `ZuTest` for TAP-emitting unit tests.
- No top-level coverage target is defined; document any new test entry points in the module `test/` directory.

## Commit & Pull Request Guidelines
- Existing history uses extremely short subject lines (for example `.`); keep subjects concise and focused, and add a module prefix when it improves clarity (for example `zdb: add repl test`).
- For PRs, include a short summary, list touched modules, and note tests run plus platform/toolchain details.

## Dependencies & Configuration Notes
Core dependencies include `libck`, `hwloc`, `pcre`, `zpicotls`, and `flatbuffers`. Optional components include `libpq` (PostgreSQL) and `gtk+3`; enable/disable via `z.config` (`-P`, `-G`) or `configure` flags.
Add external dependencies in `configure.ac`; use `PKG_CHECK_MODULES` when the dependency is available through pkg-config, falling back to dependency-specific `m4` logic when needed.
