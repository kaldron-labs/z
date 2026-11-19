#!/usr/bin/env bash
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

set -euo pipefail
[[ $# -gt 0 && $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
[[ $(readlink /proc/self/ns/mnt) != $(readlink /proc/1/ns/mnt) ]]

client=zfix-interop-c-$$ server=zfix-interop-s-$$
client_active=0 server_active=0
cleanup() {
  local result=$?
  trap - EXIT
  if ((client_active)); then ip netns del "$client" || result=1; fi
  if ((server_active)); then ip netns del "$server" || result=1; fi
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

ip netns add "$client"; client_active=1
ip netns add "$server"; server_active=1
ip link add zfixc numrxqueues 1 numtxqueues 1 type veth \
  peer name zfixs numrxqueues 1 numtxqueues 1
ip link set zfixc netns "$client" name zfixio
ip link set zfixs netns "$server" name zfixio

client_ip=198.18.93.1 server_ip=198.18.93.2
client_mac=02:00:00:00:93:01 server_mac=02:00:00:00:93:02
configure() {
  local namespace=$1 address=$2 mac=$3 peer_ip=$4 peer_mac=$5
  ip -n "$namespace" link set lo up
  ip -n "$namespace" link set zfixio address "$mac" up
  ip -n "$namespace" addr add "$address/30" dev zfixio
  ip -n "$namespace" neigh replace "$peer_ip" lladdr "$peer_mac" nud permanent dev zfixio
  ip netns exec "$namespace" ethtool -K zfixio tx off tso off gso off gro off >/dev/null
}
configure "$client" "$client_ip" "$client_mac" "$server_ip" "$server_mac"
configure "$server" "$server_ip" "$server_mac" "$client_ip" "$client_mac"

export ZFIX_INTEROP_AFXDP=1 ZFIX_INTEROP_CLIENT_NS="$client"
export ZFIX_INTEROP_SERVER_NS="$server" ZFIX_INTEROP_SERVER_IP="$server_ip"
"$@"
