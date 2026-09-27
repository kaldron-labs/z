#!/bin/sh
set -eu

verifier=$1
fixture=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$here/../../zi/itest/zi-test-residue.sh"
zi_residue_init zum-jwt-openssl
tmp=$ZI_RESIDUE_DIR
trap 'status=$?; zi_residue_finish "$status"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

openssl ecparam -name prime256v1 -genkey -noout -out "$tmp/key.pem" 2>/dev/null
openssl ec -in "$tmp/key.pem" -pubout -outform DER \
	-out "$tmp/public.der" 2>/dev/null
python3 "$fixture" "$tmp"
openssl dgst -sha256 -sign "$tmp/key.pem" \
	-out "$tmp/signature.der" "$tmp/input"
python3 "$fixture" "$tmp" finish

"$verifier" "$(cat "$tmp/token")" "$(cat "$tmp/public.hex")"
