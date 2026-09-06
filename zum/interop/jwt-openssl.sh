#!/bin/sh
set -eu

verifier=$1
fixture=$2
tmp=${TMPDIR:-/tmp}/zum-jwt-$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp"

openssl ecparam -name prime256v1 -genkey -noout -out "$tmp/key.pem" 2>/dev/null
openssl ec -in "$tmp/key.pem" -pubout -outform DER \
	-out "$tmp/public.der" 2>/dev/null
python3 "$fixture" "$tmp"
openssl dgst -sha256 -sign "$tmp/key.pem" \
	-out "$tmp/signature.der" "$tmp/input"
python3 "$fixture" "$tmp" finish

"$verifier" "$(cat "$tmp/token")" "$(cat "$tmp/public.hex")"
