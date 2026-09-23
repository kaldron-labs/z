# `zdbus` interoperability tests

Build `zdbus/util` first, then run `make -C zdbus/interop test` in the current
Linux configuration. If `libdbus-1` is absent, this directory has no test
target. The TAP driver launches the reusable private-bus fixture and the
libdbus reference peer. It checks calls, returns, structured named errors,
signals, routing headers, signatures, and reply-serial correlation in both
native-client/libdbus-service and libdbus-caller/native-service directions.
It also starts the reference peer in standalone subscriber mode to verify a
native-service signal's sender, serial, routing headers, signature, and body.
The complex payload covers a nested struct, an array, an `a{sv}` map, and a
variant in both directions. The native client also verifies that a mismatched
reply signature is a typed-dispatch fault and that an undeclared named error
retains its name and validated body for dynamic handling.
The TAP driver skips only when `dbus-daemon` is absent from `PATH`; a present
daemon or peer that fails to start remains a test failure.
Children are waited through pidfd readiness with bounded escalation to
`SIGKILL`; failure to reap is a test failure. The test never polls or sleeps
for protocol completion.
