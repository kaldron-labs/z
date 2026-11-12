# Plugin startup failure prerequisite

A controlled invalid-mask startup returns false and leaves mx reporting stopped.
The unmodified Zi implementation nevertheless executes a newly posted Rx task.
The disposable probe exits 2 on that observation. Its patched shadow variant
exits 0 and observes no worker executing the task. See `results.json` and logs.

`zi-start-failure.patch` adds scheduler worker drain before returning plugin
startup failure. It is prepared for review; frozen Zi source/tests and the
build-tree library have not been changed. Approval would require applying this
scoped exception and running the affected reused Clang/debug/ASan dependency
closure and startup/failure/retry coverage. Shadow execution alone is not that
acceptance.

Build the diagnostic with the existing itest flags, from the repository root:

```sh
make -C zutcp/itest -f Makefile \
  -f ../../review_context/zutcp-execution/start-failure-current/probe.mk \
  -j8 startup-probe startup-probe-fixed
```

The shadow target additionally needs a temporary copy of ZiMultiplex.cc with
only the prepared patch applied, named `ZiMultiplex.probe.cc` in this directory.
Run either wrapper inside the existing privileged namespace fixture. The probe
intentionally uses `_exit` to avoid destructing mx over live workers on the
broken baseline, so neither variant is leak acceptance. The shadow executable
intentionally duplicates Zi method/type globals; only its diagnostic run disables
ASan ODR detection. Normal development and acceptance flags remain unchanged.
