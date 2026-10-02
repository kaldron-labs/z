#!/bin/sh
# Run the actual Glade front end on an isolated display, never the desktop.
set -eu
if ! command -v xvfb-run >/dev/null 2>&1; then
  echo '1..0 # SKIP xvfb-run is required for the hidden GTK session'
  exit 0
fi
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$here/../../zi/itest/zi-test-residue.sh"
zi_residue_init zdash-test
fixture=$ZI_RESIDUE_DIR
ring="ZiTest.${ZI_RESIDUE_DIR##*/}"
zi_residue_shm "$ring"
trap 'status=$?; zi_residue_finish "$status"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
printf 'gtkGlade: "%s/zdash.glade", telRing: { name: "%s", size: 65536 }\n' \
  "$ZDASH_SRCDIR" "$ring" > "$fixture/dashboard.cf"
unset WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
export GDK_BACKEND=x11 NO_AT_BRIDGE=1 G_DEBUG=fatal-warnings
export ZDASH_TEST_MAPPED=1
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}"
export LSAN_OPTIONS="${LSAN_OPTIONS:+$LSAN_OPTIONS:}suppressions=$(dirname "$0")/fontconfig.lsan"
xvfb-run -a "$ZDASH_BIN" --config="$fixture/dashboard.cf"
