#!/bin/sh

set -eu

if test "${ZWS_BINANCE:-0}" != 1; then
  echo "set ZWS_BINANCE=1 to run the public Binance integration test" >&2
  exit 77
fi

ca=${ZWS_CA_PATH:-/etc/ssl/certs}
client=${ZWS_CLIENT:-../src/zws}
output=$(mktemp)
trap 'rm -f "$output"' EXIT HUP INT TERM

timeout 30 "$client" --ca="$ca" --timeout=25 \
  --messages=2 --ping-interval=1 --pong-timeout=5 --require-pong \
  --message='{"method":"SUBSCRIBE","params":["btcusdt@trade"],"id":1}' \
  wss://stream.binance.com:9443/ws >"$output"

LC_ALL=C grep -Eq '"(result":null|"e":"trade")' "$output"
