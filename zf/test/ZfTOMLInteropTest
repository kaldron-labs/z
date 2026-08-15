#!/bin/sh

if ! command -v python3 >/dev/null 2>&1; then
  echo '1..0 # SKIP python3 unavailable'
  exit 0
fi

"$1" --interop | python3 "$2"
