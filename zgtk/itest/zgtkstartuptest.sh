#!/bin/sh
set -eu
if ! command -v xvfb-run >/dev/null 2>&1; then
  echo '1..0 # SKIP xvfb-run is required for the isolated GTK startup test'
  exit 0
fi
exec xvfb-run -a "${ZGTK_STARTUP_BIN:?}"
