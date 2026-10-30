#!/bin/sh
set -eu
if test "$#" -ne 3; then
  echo "usage: codec-test.sh NATIVE_CODEC QUICKFIX_CODEC FIX44.xml" >&2
  exit 2
fi
zfix_native=$1
zfix_peer=$2
zfix_dict=$3
. "$(dirname "$0")/quickfix.lock"
printf '%s  %s\n' "$QUICKFIX_FIX44_SHA256" "$zfix_dict" | sha256sum -c -
zfix_tmp=$(mktemp -d "${TMPDIR:-/tmp}/zfix-codec-peer.XXXXXXXX")
trap 'rm -rf "$zfix_tmp"' EXIT HUP INT TERM
"$zfix_native" --frames "$zfix_tmp"
if ! "$zfix_peer" "$zfix_dict" "$zfix_tmp/order.fix" "$zfix_tmp/groups.fix" \
  "$zfix_tmp/ack.fix" "$zfix_tmp/fill.fix" "$zfix_tmp/cancel.fix" \
  "$zfix_tmp/replace.fix" "$zfix_tmp/binary.fix" "$zfix_tmp/text.fix" "$zfix_tmp/heavy.fix" > "$zfix_tmp/peer.tap"; then
  cat "$zfix_tmp/peer.tap"
  exit 1
fi
prove --exec cat "$zfix_tmp/peer.tap"
"$zfix_native" --check-frames "$zfix_tmp"
