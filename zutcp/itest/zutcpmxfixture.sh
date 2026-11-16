#!/usr/bin/env bash
set -euo pipefail
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
a_mac=$(ip -o link show dev zxdprxa | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
b_mac=$(ip -o link show dev zxdprxb | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip route add 198.18.0.11/32 dev zxdprxa
ip route add 198.18.0.10/32 dev zxdprxb
ip -6 route add 2001:db8::11/128 dev zxdprxa
ip -6 route add 2001:db8::10/128 dev zxdprxb
ip neigh replace 198.18.0.11 lladdr "$b_mac" nud permanent dev zxdprxa
ip neigh replace 198.18.0.10 lladdr "$a_mac" nud permanent dev zxdprxb
ip -6 neigh replace 2001:db8::11 lladdr "$b_mac" nud permanent dev zxdprxa
ip -6 neigh replace 2001:db8::10 lladdr "$a_mac" nud permanent dev zxdprxb
ethtool -K zxdprxa tx off tso off gso off gro off >/dev/null
ethtool -K zxdprxb tx off tso off gso off gro off >/dev/null
export ZUTCP_MX_FIXTURE=1
exec "$@"
