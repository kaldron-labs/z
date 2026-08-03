#!/bin/sh

set -eu

src=$1
files="$src/ZwsCodec.hh $src/ZwsTx.hh"
forbidden='Ztcp|Ztls|Zquic|Zhttp::H2|Zhttp::H3|H2::|H3::|HPack|QPack|disconnect[[:space:]]*\(|flowControl|flow_control'

! LC_ALL=C grep -En "$forbidden" $files
