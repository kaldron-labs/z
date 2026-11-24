#!/usr/bin/env bash
set -euo pipefail
[[ ${ZPKT_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
a_mac=$(ip -o link show dev zpktrxa | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
b_mac=$(ip -o link show dev zpktrxb | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip route add 198.18.0.11/32 dev zpktrxa
ip route add 198.18.0.10/32 dev zpktrxb
ip -6 route add 2001:db8::11/128 dev zpktrxa
ip -6 route add 2001:db8::10/128 dev zpktrxb
ip neigh replace 198.18.0.11 lladdr "$b_mac" nud permanent dev zpktrxa
ip neigh replace 198.18.0.10 lladdr "$a_mac" nud permanent dev zpktrxb
ip -6 neigh replace 2001:db8::11 lladdr "$b_mac" nud permanent dev zpktrxa
ip -6 neigh replace 2001:db8::10 lladdr "$a_mac" nud permanent dev zpktrxb
ethtool -K zpktrxa tx off tso off gso off gro off >/dev/null
ethtool -K zpktrxb tx off tso off gso off gro off >/dev/null
export ZUTCP_MX_FIXTURE=1
exec "$@"
