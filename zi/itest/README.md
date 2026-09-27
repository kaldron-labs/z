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
