# Test residue helpers

C++ tests use `ZiTestResidue` from `zi/test`. Python, shell, and Go tests use
the helpers in this directory. Set `ZI_LOGDIR` to choose the artifact root;
otherwise it is the current directory. Each test creates an owned directory,
removes it on success, and prints and retains it on failure. At most eight
completed failed directories per test name are retained. Shared-memory names
registered with a helper are unlinked on either outcome.

Python drivers add `zi/itest` to `PYTHONPATH`, then use
`with Residue("test-name") as residue:` from `zi_test_residue`. Create files
under `residue.directory` or use `residue.path("name")`. Call
`residue.success()` only after all checks pass and all children stop. Use
`residue.tmp_dir("tag")` only when the tested API requires `/tmp`. Use
`residue.tmp_file("tag")` when a test must deliberately place a file at a
system-temp path; it is retained as `*.failed` after failure.

Shell drivers source `zi-test-residue.sh`, call `zi_residue_init test-name`,
write beneath `$ZI_RESIDUE_DIR`, and install an EXIT trap that saves `$?`,
stops children, and calls `zi_residue_finish "$status"`.
Call `zi_residue_shm "$name"` before starting any process that creates a
named ring.

Go tests import the local `zlib/zi/itest/residue` module, use
`residue.New(t, "test-name")` before registering child-process cleanup, and
write beneath `r.Directory` or `r.Path("name")`. The helper's `t.Cleanup`
observes `t.Failed()` after later cleanup callbacks have run. Consumer Go
modules use a local `replace` directive to point at this directory.

The QIR runner `scripts/qir-run-local` publishes results below
`zhttp/interop/qir-results` by default, or at its caller's `--out` path.
Those are explicit results rather than test residue and are retained until
the caller removes them. Tests do not delete a caller-provided database path,
QIR output path, or runner-owned `/logs` mount.

## Heap tuning

Build the configured module tree using the job counts in
[heaptune.md](../../heaptune.md), then run `./zi/itest/zheaptune` or
`prove -v ./zi/itest/zheaptune`. It also runs from `make -C zi/itest test`
and `make -C zi test`. The `zheapwork` fixture is built alongside the TAP
driver and is launched by it; it is not a standalone TAP driver.
Set `Z_HEAPTUNE_CASE` to a case name (for example `cold`, `selective`, or
`cpuset`) to run that case alone. Unknown names fail the driver.
Use separate `ZI_LOGDIR` roots when running multiple copies concurrently;
residue paths use the test name.

Each case runs the fixture in fresh processes: call `ZiHeapTune::load()` first
in `main`, run deterministic bursts, overwrite `Z_HEAPTUNE` with
`ZiHeapTune::save()`, and replay the resulting configuration. Explicit
headroom uses `save(headroom)`; open/write failures fail the fixture. The late
case uses `ZiHeapTune::init` for its separate input configuration. The driver
checks initial and drained telemetry, allocation counts, fallback peaks,
independent partition/class capacities, sizing arithmetic, unused arenas,
and file replacement. Size-boundary cases verify the full payload.

The fixture syntax is
`zheapwork scenario count repeat report [headroom|-] [late-config]`.
Scenarios are `fixed`, `multi`, `unusedpair`, `variable`, `shift1`,
`partitions`, `sequential`, `keys`, `late`, `shared`, `unused`, and
`boundaries`. Omitting headroom or using `-` calls `ZiHeapTune::save()` with
its default headroom.
Set `Z_HEAPTUNE` to an owned configuration file when invoking the fixture
manually: it overwrites that file. The driver supplies a child-only path
and leaves the caller's environment and configuration file alone.

The default child timeout is 30 seconds. `Z_HEAPTUNE_TIMEOUT` overrides it
(1 through 3600 seconds); it bounds execution without imposing a performance
threshold. On failure, the owned `ZiTestResidue` directory retains each
generation's input/output CSV, structured snapshots, `heapCSV()` output,
and child TAP log. Successful runs remove the directory.

Use Valgrind only when investigating observed memory corruption or crashes.
To instrument both the parent and children, for example:

```sh
Z_HEAPTUNE_TIMEOUT=120 \
Z_HEAPTUNE_CHILD='libtool exec valgrind --leak-check=full --error-exitcode=99' \
  libtool exec valgrind --leak-check=full --error-exitcode=99 ./zi/itest/zheaptune
```

Use the configured build's `libtool` by absolute path if it is not on `PATH`.
Never launch `.libs` binaries directly.
## Hash tuning

The `zhashtune` TAP driver launches `zhashwork` in fresh processes, starting
with an empty or seeded `Z_HASHTUNE` configuration. The fixture loads through
`ZiHashTune::load()` first in `main` and saves through `ZiHashTune::save()`
while its workload tables remain registered. Only `bits` changes: a resized
run exports its final bits; a run without resizing uses the retained peak
and headroom, allowing shrinkage below the default.

Once rebuilding is resumed, build with Clang/debug, `-j8` through `ztc` and
`-j4` thereafter, then run `./zi/itest/zhashtune` or
`prove -v ./zi/itest/zhashtune`. It is also included in `make -C zi/itest test`
and `make -C zi test`. Set `Z_HASHTUNE_CASE` to a named case such as `grown`,
`below-default`, `same-id-overlap`, `linear-load`, or `utility` for a focused
run. Unknown case names fail.

The fixture syntax is
`zhashwork scenario count repeat report [headroom|-] [late-config]`.
The driver supplies child-only configuration paths. It retains every input
and output generation, structured snapshot, diagnostic hash CSV, and child
TAP log on failure through `ZiTestResidue`, and removes owned residue on
success. Use separate `ZI_LOGDIR` roots for concurrent driver runs.

`Z_HASHTUNE_TIMEOUT` sets the child timeout (default 30 seconds, valid range
1–3600). `Z_HASHTUNE_CHILD` supplies an optional instrumentation command
prefix. The launcher kills and reaps timed-out children using the established
heap harness process-group/job conventions. Use Valgrind only if needed to
investigate observed corruption or crashes, through `libtool exec` and normal
wrapper paths. Do not launch `.libs` binaries directly.
