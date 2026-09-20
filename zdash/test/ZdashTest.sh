#!/bin/sh
# Run the actual Glade front end on an isolated display, never the desktop.
set -eu
if ! command -v xvfb-run >/dev/null 2>&1; then
  echo '1..0 # SKIP xvfb-run is required for the hidden GTK session'
  exit 0
fi
fixture=$(mktemp -d)
trap 'rm -f "$fixture/dashboard.cf"; rmdir "$fixture"' EXIT HUP INT TERM
printf 'gtkGlade: "%s/zdash.glade", telRing: { name: "zdash", size: 16384 }\n' \
  "$ZDASH_SRCDIR" > "$fixture/dashboard.cf"
unset WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
export GDK_BACKEND=x11 NO_AT_BRIDGE=1 G_DEBUG=fatal-warnings
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}"
export LSAN_OPTIONS="${LSAN_OPTIONS:+$LSAN_OPTIONS:}suppressions=$(dirname "$0")/fontconfig.lsan"
xvfb-run -a "$ZDASH_BIN" --config="$fixture/dashboard.cf"
