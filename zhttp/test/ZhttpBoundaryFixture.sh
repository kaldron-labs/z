#!/bin/sh

set -eu

util=$1
src=$2
files='zhttp.cc zhttpd.cc zhttpdutil.cc zhttpd.hh zhttpput.hh runtime.hh'
bad='ZiResolver|parseHTTPS|discoverH3|AltSvcCache|ClientPool|ServerLink|ServerSession|H1ReqParser|H3ReqParser|H1RespBuilder|H3RespBuilder|HPack|H2::(Frame|Session|Wire)|H2_(Client|Server|Logical)|disconnect_|Multiplexed|Zhttp::Runtime|Zhttp::Hubs|Ztls::|\.alpn[[:space:]]*\(|ZmBlock'

cd "$util"

! grep -En "$bad" $files
! grep -En '#include <zlib/(Ztcp|Ztls|Zquic)' $files
grep -Eq 'Zhttp::Client<' zhttp.cc
grep -Eq 'Zhttp::Server<' zhttpd.cc
! grep -En \
  '(responseBody|requestBody|void[[:space:]]+body)[[:space:]]*\([^)]*ZuBSpan' \
  $files

test -f zhttpboundary.md
grep -q 'zhttp.cc' zhttpboundary.md
grep -q 'zhttpd' zhttpboundary.md
grep -q 'zhttpd.hh' zhttpboundary.md
grep -q 'zhttpput.hh' zhttpboundary.md

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
! grep -Ein "$stream_policy" "$src"/*.cc "$src"/*.hh
