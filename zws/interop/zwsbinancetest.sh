#!/bin/sh

set -eu

if test "${ZWS_BINANCE:-0}" != 1; then
  echo "set ZWS_BINANCE=1 to run the public Binance integration test" >&2
  exit 77
fi

client=${ZWS_CLIENT:-../src/zws}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$here/../../zi/itest/zi-test-residue.sh"
zi_residue_init zws-binance
output=$(zi_residue_path output)
trap 'status=$?; zi_residue_finish "$status"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

set -- --timeout=25 --messages=2 --ping-interval=1 --pong-timeout=5 \
  --require-pong \
  --message='{"method":"SUBSCRIBE","params":["btcusdt@trade"],"id":1}' \
  wss://stream.binance.com:9443/ws
if test -n "${ZWS_CA_PATH:-}"; then
  set -- --ca="$ZWS_CA_PATH" "$@"
fi

timeout 30 "$client" "$@" >"$output"

LC_ALL=C grep -Eq '"(result":null|"e":"trade")' "$output"
