# Repository Guidelines

## Project Structure & Module Organization
The repository is organized by module prefix (for example `zu`, `zm`, `zt`, `ze`, `zi`, `zdb`). Each module typically contains `src/` for library code and `test/` for module-specific test binaries. Top-level build inputs live in `configure.ac`, `Makefile.am`, and `m4/`. Helper scripts live under `scripts/` and the `z.config` wrapper centralizes configure flags.

## Build, Test, and Development Commands
- `./z.config /opt/z` configures the build with a prefix; use `-c` to rerun `autoreconf`, `-d` for debug, `-L` for clang, `-G` to disable Gtk, `-P` to disable PostgreSQL, `-M` for MinGW.
- `make -j` builds all enabled modules plus their test binaries.
- `make install` installs libraries/headers into the configured prefix.
- Some optional features (for example libbfd backtracing) require external builds; see `README.md` for platform-specific steps.

## Coding Style & Naming Conventions
- C++ is compiled as GNU C++2b; headers use `.hh`, sources use `.cc`.
- Indentation uses tabs (width 8) with a 2-space logical offset; follow existing file headers for editor settings.
- Names are short and prefixed by module (`Zu`, `Zm`, `Zt`, etc.); internal namespaces often use `Zxx_`.
- Keep STL usage minimal and mirror nearby patterns/macros (for example `ZuInline`, `ZuAssert`).

## Testing Guidelines
- Tests are built as standalone binaries under each module’s `test/` directory.
- Run specific tests directly from the build tree, for example `./zt/test/ZtArrayTest` or `./zdb/test/zdbsmoketest`.
- No top-level test runner or coverage target is defined; document any new test entry points in the module `test/` directory.

## Commit & Pull Request Guidelines
- Existing history uses extremely short subject lines (for example `.`); keep subjects concise and focused, and add a module prefix when it improves clarity (for example `zdb: add repl test`).
- For PRs, include a short summary, list touched modules, and note tests run plus platform/toolchain details.

## Dependencies & Configuration Notes
Core dependencies include `libck`, `hwloc`, `pcre`, `mbedtls`, and `flatbuffers`. Optional components include `libpq` (PostgreSQL) and `gtk+3`; enable/disable via `z.config` (`-P`, `-G`) or `configure` flags.
