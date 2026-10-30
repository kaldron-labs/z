#!/bin/sh
set -eu
zfix_demo=$1
shift
for zfix_mode in 4 44A T11; do
  for zfix_case in rpc groups events reject policy; do
    "$zfix_demo" "$zfix_case" --mode "$zfix_mode" "$@"
  done
  "$zfix_demo" policy --mode "$zfix_mode" --auth example "$@"
  "$zfix_demo" rpc --mode "$zfix_mode" --auth example --bad-auth "$@"
done
