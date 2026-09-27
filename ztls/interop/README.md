# Secret Service interoperability

`ztlsvaultinterop` exercises `Ztls::Vault` against GNOME Keyring's Secret
Service implementation. It starts a private session bus and a daemon with a
temporary `HOME` and XDG directories, unlocks with an empty test password,
and exercises both `KeyRing + Direct` and `KeyRing + Indirect`, including
`Auto + Indirect` recovery. It removes the temporary files after shutting down
the daemon and does not use the desktop keyring. The TAP test skips when
`dbus-daemon` or `gnome-keyring-daemon` is unavailable.

Build `zdbus/util/zdbusbus` first, then run
`make -C ztls/interop -j8 && make -C ztls/interop test` in the current build.
The existing `ztlsageinterop` driver remains a separate optional age-format
interoperability tool.
