#!/bin/sh
set -eu
zfix_tmp=$(mktemp -d "${TMPDIR:-/tmp}/zfix-demo-tls.XXXXXXXX")
trap 'rm -rf "$zfix_tmp"' EXIT HUP INT TERM
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj /CN=localhost -addext subjectAltName=DNS:localhost,IP:127.0.0.1 \
  -keyout "$zfix_tmp/key.pem" -out "$zfix_tmp/cert.pem" \
  > "$zfix_tmp/openssl.log" 2>&1
export ZFIX_TEST_CERT="$zfix_tmp/cert.pem" ZFIX_TEST_KEY="$zfix_tmp/key.pem"
exec_cmd=$1
shift
sh "$(dirname "$0")/zfix_demo_check.sh" "$exec_cmd" --tls "$@"
