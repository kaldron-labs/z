#!/bin/sh

set -eu

echo "1..1"

if test -n "${srcdir:-}"; then
  test_dir=$srcdir
else
  test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fi
source_dir=$test_dir/../src
files="$source_dir/ZwsCodec.hh $source_dir/ZwsTx.hh"
forbidden='Ztcp|Ztls|Zquic|Zhttp::H2|Zhttp::H3|H2::|H3::|HPack|QPack|disconnect[[:space:]]*\(|flowControl|flow_control'

if LC_ALL=C grep -En "$forbidden" $files; then
  echo "not ok 1 - established WebSocket stream source boundary"
  exit 1
fi

echo "ok 1 - established WebSocket stream source boundary"
exit 0
