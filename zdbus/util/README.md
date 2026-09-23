# `zdbus` test utilities

Build with `make -C zdbus/util -j8` before the integration and interoperability
tests. `zdbusbus` launches a private `dbus-daemon`, writes one address line to
stdout when ready, and stops and reaps the daemon when its stdin closes.
`ZdbusTestTool` waits for control-pipe lines and child exit through
`ZiEventLoop`; its pidfd child wait escalates to `SIGKILL` after a bounded
deadline and reports whether the child was reaped.
When `libdbus-1` is installed, `zdbuspeer` builds independently of `libZdbus`.
Invoke it as `zdbuspeer ADDRESS service|caller|subscriber`; each mode writes
`READY` to stdout after bus registration. The service accepts `Echo`, `Fail`,
`Emit`, and `Quit` calls, while the caller validates native service returns,
named errors, and signals.
