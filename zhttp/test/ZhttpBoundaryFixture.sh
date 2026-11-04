#!/bin/sh

set -eu

util=$1
src=$2
files='zhttp.cc zhttpd.cc zhttpdutil.cc zhttpd.hh zhttpput.hh runtime.hh'
bad='ZiResolver|parseHTTPS|discoverH3|AltSvcCache|ClientPool|ServerLink|ServerSession|H1ReqParser|H3ReqParser|H1RespBuilder|H3RespBuilder|HPack|H2::(Frame|Session|Wire)|H2_(Client|Server|Logical)|disconnect_|Multiplexed|Zhttp::Runtime|Zhttp::Hubs|Ztls::|\.alpn[[:space:]]*\(|ZmBlock'

# Negated grep lists suppress errexit even when a forbidden match is found.
# Reject both matches and grep failures; status1 alone means no match.
reject()
{
  if grep "$@"; then
    exit 1
  else
    status=$?
    test "$status" -eq 1 || exit "$status"
  fi
}

cd "$util"

reject -En "$bad" $files
reject -En '#include <zlib/(Ztcp|Ztls|Zquic)' $files
grep -Eq 'Zhttp::Client<' zhttp.cc
grep -Eq 'Zhttp::Server<' zhttpd.cc
reject -En \
  '(responseBody|requestBody|void[[:space:]]+body)[[:space:]]*\([^)]*ZuBSpan' \
  $files

test -f README.md
grep -q 'zhttp.cc' README.md
grep -q 'zhttpd' README.md
grep -q 'zhttpd.hh' README.md
grep -q 'zhttpput.hh' README.md

sources=$(
  sed -n \
    -e 's/^zhttp_SOURCES[[:space:]]*=[[:space:]]*//p' \
    -e 's/^zhttpd_SOURCES[[:space:]]*=[[:space:]]*//p' \
    Makefile.am | tr '\n' ' ' | sed 's/[[:space:]]*$//'
)
test "$sources" = 'zhttp.cc zhttpd.cc'
grep -Eq \
  '^libzhttputil_la_SOURCES[[:space:]]*=[[:space:]]*zhttpd\.cc zhttpdutil\.cc$' \
  Makefile.am

stream_policy='websocket|sec-websocket|permessage-deflate|continuation[ _-]+opcode|binary[ _-]+opcode|text[ _-]+opcode|close[ _-]+code|masking[ _-]+key'
reject -Ein "$stream_policy" "$src"/*.cc "$src"/*.hh
