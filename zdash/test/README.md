# Dashboard module tests

`make -C zdash test` runs the schema test and the real dashboard with the
libtool-built `test/.libs/zdash_test.so` module. Both emit TAP, evaluated by
`prove`. The shell fixture requires `xvfb-run`, forces X11 on a disposable
display and never maps the dashboard onto the desktop. The small mirrored
ring exercises byte ownership, wrap-spanning records, bounded GTK updates,
EOS retention, generation replacement and window-close events. Populated
telemetry checks every row's key columns and grouping labels, including heap
identity fields, both socket endpoints, enum names, hexadecimal hash addresses,
database RAG and GTK path round trips. Two databases with matching child IDs
check separate grouping, child-created placeholders, repeated updates and
source teardown. Heap identity has five columns; `vshift` is not a key field.

The module exports `ZdashModule`; `zdash` loads it with `ZiModule` only when
`ZDASH_TEST` names a module, resolves the factory, and invokes the
returned `ZDash::Module` interface. Test state, synthetic telemetry and
assertions live in the module. The host interface documents Rx/GTK ownership;
the module remains loaded through process teardown, like Zdb store backends.

For an authenticated live hub, run the same module with the normal
`--config`, `--wss`, `--device-id` and `--ca` options, setting
`ZDASH_TEST_TOKEN` to a pre-issued client token. Use an isolated Xvfb display.
The module then checks receipt/rendering and closes via the GTK event loop.
`ZDASH_TEST_TIMEOUT` sets a 1–3600 second deadline (default 15).
Without a WSS URL the module runs the synthetic phases. No test CLI flags
or synthetic telemetry are compiled into `zdash.cc`.

ASan/LSan remain enabled when supplied by the current build configuration.
The fixture suppresses only Fontconfig's process-lifetime cache; other leaks
and sanitizer errors remain fatal.
