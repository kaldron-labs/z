# Working in Z

## Before writing or reviewing code

1. Read [docs/GUIDELINES.md](docs/GUIDELINES.md) for Z's engineering priorities.
   For C++ source, headers, examples, and tests, also read the relevant sections
   of [docs/CPP.md](docs/CPP.md) and [docs/STYLE.md](docs/STYLE.md). These are
   repository rules and take precedence over generic C++ conventions and
   conflicting examples in nearby code.
2. Read the relevant sections of each affected module's `GUIDELINES.md` if
   present, checking the module root and its `docs/` directory. For example,
   see [zpkt](zpkt/GUIDELINES.md), [zquic](zquic/GUIDELINES.md), and
   [zutcp](zutcp/GUIDELINES.md). Repeat this check when adding a module to scope.
3. Use the table to open task-specific rules before making the relevant
   implementation decisions. Read the relevant sections directly in each guide.

## Find the applicable rules

| When the task involves | Open |
| --- | --- |
| Choosing types, containers, strings, storage or ownership, errors, callbacks, metadata, synchronization, timers, or persistence | [docs/FRAMEWORK.md](docs/FRAMEWORK.md). Before choosing an STL or third-party facility, check the [API comparison](docs/Z_STL_BOOST_FOLLY_BDE_RUST.md#contents) for a Z equivalent. |
| Changing I/O, Rx/Tx ownership, dispatch, cross-shard handoffs, lifetime, or teardown | [docs/IO.md](docs/IO.md) and the relevant I/O sections of [docs/FRAMEWORK.md](docs/FRAMEWORK.md#io-and-system-integration). |
| Changing build files, placing code or tests, configuring, building, debugging, or preparing a PR | [docs/BUILD.md](docs/BUILD.md). See [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) for toolchains, libraries, and optional peers. |
| Writing or running tests, setting up fixtures, or checking interoperability and benchmarks | [docs/TESTING.md](docs/TESTING.md) and the affected test directory's README when present. |
| Reviewing code for repository policy violations, auditing, or fixing flagged code | [docs/AUDIT.md](docs/AUDIT.md), especially the sections that match the change. |

Library code lives in `<module>/src` and unit tests in `<module>/test`.
Top-level build inputs are `configure.ac`, `Makefile.am`, and `m4/`.
`z.config` centralizes configuration. See [README.md](README.md) for platform setup.

## Before handing back code

Review the final diff against the applicable repository and module rules.
Check the changed declarations and naming, choices of Z APIs and types, and
any ownership or allocation changes against the corresponding guide.
Use [docs/TESTING.md](docs/TESTING.md) to select checks for the affected
behavior. Report the checks run and any required verification still pending.
