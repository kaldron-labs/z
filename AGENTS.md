# Repository guidance map

Read only the documents and sections relevant to the task. These documents are
repository policy and take precedence over generic C++ preferences. Before code
changes, read `GUIDELINES.md` and applicable task-specific guidance below.
Also read the affected module's `GUIDELINES.md` when present (for example,
`zquic/GUIDELINES.md`); it supplies additional library-specific rules.

| Document | Read when | Contents |
| --- | --- | --- |
| [GUIDELINES.md](GUIDELINES.md) | Designing or changing code | Engineering principles, performance priorities, supported targets |
| [BUILD.md](BUILD.md) | Configuring, building, testing, debugging, or preparing a PR | Module layout, commands, dependencies, verification, contribution conventions |
| [CPP.md](CPP.md) | Writing or reviewing C++ | Language rules, templates, constraints, enums, STL policy, portability |
| [STYLE.md](STYLE.md) | Editing declarations, headers, or implementation | Header skeleton, indentation, layout, class shape, naming, accessors |
| [FRAMEWORK.md](FRAMEWORK.md) | Choosing or using framework facilities | Types, strings, storage, formatting, metadata, concurrency, timers, I/O, tests, persistence |
| [IO.md](IO.md) | Changing I/O or cross-shard behavior | Rx/Tx ownership, dispatch, handoffs, lifetime, teardown |
| [AUDIT.md](AUDIT.md) | Auditing, reviewing, or repairing flagged code | Audit scope, repair procedure, red/amber flags, problems and fixes |

Modules use short prefixes (`zu`, `zm`, `zt`, `ze`, `zf`, `zi`, `zdb`, etc.).
Library code lives in `<module>/src`; unit tests in `<module>/test`.
See `BUILD.md` for other directory roles. Top-level build inputs are
`configure.ac`, `Makefile.am`, and `m4/`; `z.config` centralizes configuration.
See [README.md](README.md) for platform setup and `scripts/` for utilities.
