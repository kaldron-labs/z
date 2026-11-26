#!/usr/bin/env bash
# (c) Copyright 2026 Huw Rogers; MIT license (see LICENSE).
set -euo pipefail

[[ $# == 1 ]] || { echo 'usage: zpktafp_fixture.sh TEST_BINARY' >&2; exit 2; }
if [[ ${ZPKT_AFP_PRIVATE:-0} != 1 ]]; then
  exec unshare --net env ZPKT_AFP_PRIVATE=1 "$0" "$1"
fi
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]] || {
  echo 'AFP veth fixture requires a private network namespace' >&2
  exit 1
}
ip link add zafp0 type veth peer name zafp1
for name in zafp0 zafp1; do
  sysctl -qw "net.ipv6.conf.$name.disable_ipv6=1"
  ip link set "$name" up
done
exec timeout --kill-after=2s 25s "$1"
