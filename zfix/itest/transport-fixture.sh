#!/usr/bin/env bash
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

set -euo pipefail
[[ $# -gt 0 && $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]

fixture=$(realpath "$0")
if [[ $# != 1 || $1 != --reset ]]; then
  export ZFIX_AFXDP_FIXTURE="$fixture"
  exec "$@"
fi
[[ ${ZFIX_AFXDP_FIXTURE:-} == "$fixture" ]]

# AF_XDP socket release can defer queue unbinding past the program's close.
# Each independent mux receives a fresh test device in this namespace.
if ip link show dev zfixc >/dev/null 2>&1; then ip link del zfixc; fi
ip link set lo up
ip link add zfixc numrxqueues 1 numtxqueues 1 type veth \
  peer name zfixr numrxqueues 1 numtxqueues 1
ip link set zfixc address 02:00:00:00:92:01 up
ip link set zfixr up
for device in zfixc zfixr; do
  ethtool -K "$device" tx off tso off gso off gro off >/dev/null
done
# The peer ingress and route back to the source differ in this private
# reflection topology. Disable effective reverse-path filtering on that peer.
sysctl -qw net.ipv4.conf.all.rp_filter=0 net.ipv4.conf.zfixr.rp_filter=0

# Both FIX hubs share the native mux. Reflect its output across the veth peer
# so each direction traverses AF_XDP Tx and Rx while retaining the same owners.
tc qdisc add dev zfixr clsact
# The silent TLS peer uses kernel TCP. Deliver its replies at the peer before
# reflecting native addresses back through the XDP-owned interface.
tc filter add dev zfixr ingress pref 1 protocol ip u32 \
  match ip dst 198.18.92.100/32 action skbedit ptype host action pass
tc filter add dev zfixr ingress pref 2 protocol ip matchall \
  action mirred egress redirect dev zfixr

# The dummy provides a preferred client source in route lookups constrained to
# zfixc. Neither test endpoint has a local route on the native device.
if ! ip link show dev zfixsrc >/dev/null 2>&1; then
  ip link add zfixsrc type dummy
  ip link set zfixsrc up
  ip addr add 198.18.92.1/32 dev zfixsrc
  ip addr add 198.18.92.100/32 dev zfixsrc
fi
for address in 198.18.92.1 198.18.92.2 198.18.92.100; do
  ip route add "$address/32" dev zfixc src 198.18.92.1
  ip neigh replace "$address" lladdr 02:00:00:00:92:01 nud permanent dev zfixc
done
