# Repository guidance map

Read only the documents and sections relevant to the task. These documents are
repository policy and take precedence over generic C++ preferences. Before code
changes, read `docs/GUIDELINES.md` and applicable task-specific guidance below.
Also read the affected module's `docs/GUIDELINES.md` when present (for example,
`zquic/GUIDELINES.md`); it supplies additional library-specific rules.

Use this map to select guidance as the task develops. Do not concatenate the
guidance corpus into a temporary manual or bulk-read it as a prerequisite.
Read relevant sections directly; expand when the task requires additional
guidance.

| Document | Read when | Contents |
| --- | --- | --- |
| [GUIDELINES.md](docs/GUIDELINES.md) | Designing or changing code | Engineering principles, performance priorities, supported targets |
| [BUILD.md](docs/BUILD.md) | Configuring, building, testing, debugging, or preparing a PR | Module layout, commands, dependencies, verification, contribution conventions |
| [CPP.md](docs/CPP.md) | Writing or reviewing C++ | Language rules, templates, constraints, enums, STL policy, portability |
| [STYLE.md](docs/STYLE.md) | Editing declarations, headers, or implementation | Header skeleton, indentation, layout, class shape, naming, accessors |
| [FRAMEWORK.md](docs/FRAMEWORK.md) | Choosing or using framework facilities | Types, strings, storage, formatting, metadata, concurrency, timers, I/O, tests, persistence |
| [IO.md](docs/IO.md) | Changing I/O or cross-shard behavior | Rx/Tx ownership, dispatch, handoffs, lifetime, teardown |
| [AUDIT.md](docs/AUDIT.md) | Auditing, reviewing, or repairing flagged code | Audit scope, repair procedure, red/amber flags, problems and fixes |

Modules use short prefixes (`zu`, `zm`, `zt`, `ze`, `zf`, `zi`, `zdb`, etc.).
Library code lives in `<module>/src`; unit tests in `<module>/test`.
See `docs/BUILD.md` for other directory roles. Top-level build inputs are
`configure.ac`, `Makefile.am`, and `m4/`; `z.config` centralizes configuration.
See [README.md](docs/README.md) for platform setup and `scripts/` for utilities.
