#!/bin/sh

set -eu

echo 'TAP version 14'
echo '1..7'

files='zhttp.cc zhttpd.cc Zhttpd.hh ZhttpPut.hh'
bad='ZiResolver|parseHTTPS|discoverH3|AltSvcCache|ClientPool|ServerLink|ServerSession|H1ReqParser|H3ReqParser|H1RespBuilder|H3RespBuilder|HPack|H2::(Frame|Session|Wire)|H2_(Client|Server|Logical)|disconnect_|Multiplexed|Zhttp::Runtime|Zhttp::Engines|Ztls::|\.alpn[[:space:]]*\(|ZmBlock|ZmSemaphore'

if grep -En "$bad" $files >/dev/null; then
  echo 'not ok 1 - executable closure excludes reusable HTTP mechanisms'
  grep -En "$bad" $files | sed 's/^/# /'
  exit 1
fi
echo 'ok 1 - executable closure excludes reusable HTTP mechanisms'

if grep -En '#include <zlib/(Ztcp|Ztls|Zquic)' $files >/dev/null; then
  echo 'not ok 2 - executable closure excludes native protocol headers'
  exit 1
fi
echo 'ok 2 - executable closure excludes native protocol headers'

if grep -Eq 'Zhttp::Agent<' zhttp.cc &&
    grep -Eq 'Zhttp::Service<' zhttpd.cc; then
  echo 'ok 3 - executables use public Agent and Service'
else
  echo 'not ok 3 - executables use public Agent and Service'
  exit 1
fi

if grep -En \
    '(responseBody|requestBody|streamProcess|void[[:space:]]+body)[[:space:]]*\([^)]*ZuBSpan' \
    $files >/dev/null; then
  echo 'not ok 4 - executable closure excludes borrowed-span body callbacks'
  exit 1
fi
echo 'ok 4 - executable closure excludes borrowed-span body callbacks'

fixture=$(mktemp)
trap 'rm -f "$fixture"' EXIT HUP INT TERM
echo 'void violation() { link.disconnect_(); }' >"$fixture"
if grep -Eq "$bad" "$fixture"; then
  echo 'ok 5 - negative scan rejects representative violation'
else
  echo 'not ok 5 - negative scan rejects representative violation'
  exit 1
fi

if test -f ZhttpBoundary.md &&
    grep -q 'zhttp.cc' ZhttpBoundary.md &&
    grep -q 'zhttpd.cc' ZhttpBoundary.md &&
    grep -q 'Zhttpd.hh' ZhttpBoundary.md &&
    grep -q 'ZhttpPut.hh' ZhttpBoundary.md; then
  echo 'ok 6 - executable closure has a retained manifest'
else
  echo 'not ok 6 - executable closure has a retained manifest'
  exit 1
fi

sources=$(
  sed -n \
    -e 's/^zhttp_SOURCES[[:space:]]*=[[:space:]]*//p' \
    -e 's/^zhttpd_SOURCES[[:space:]]*=[[:space:]]*//p' \
    Makefile.am | tr '\n' ' ' | sed 's/[[:space:]]*$//'
)
headers=$(
  sed -n 's/^[[:space:]]*#include[[:space:]]*"\([^"]*\)".*/\1/p' \
    $files | sort -u
)
expected_headers='ZhttpPut.hh
Zhttpd.hh'
if test "$sources" = 'zhttp.cc zhttpd.cc' &&
    test "$headers" = "$expected_headers"; then
  echo 'ok 7 - reviewed manifest covers the complete program-only closure'
else
  echo 'not ok 7 - unreviewed program-only source entered the closure'
  echo "# sources: $sources"
  echo "# headers: $headers"
  exit 1
fi
