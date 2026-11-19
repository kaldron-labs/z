#!/usr/bin/env bash
set -euo pipefail
# Run only inside the existing task-owned private namespace fixture.
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
[[ -n ${ZXDP_RX_TEST_IFINDEX:-} && -n ${ZXDP_RX_TEST_PEER_IFINDEX:-} ]]
ip address add 198.18.0.2/32 dev zxdprxb
ip route add 198.18.0.1/32 dev zxdprxb src 198.18.0.2
ip -6 address add 2001:db8::2/128 dev zxdprxb nodad
ip -6 route add 2001:db8::1/128 dev zxdprxb src 2001:db8::2
zutcp_test_mac=$(ip -o link show dev zxdprxa | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
[[ -n "$zutcp_test_mac" ]]
ip neighbor replace 198.18.0.1 lladdr "$zutcp_test_mac" nud permanent dev zxdprxb
ip -6 neighbor replace 2001:db8::1 lladdr "$zutcp_test_mac" nud permanent dev zxdprxb
# Cold route-query fixtures: direct, missing-neighbor, dynamic-neighbor and gateway.
ip route add 198.18.0.3/32 dev zxdprxb src 198.18.0.2
ip route add 198.18.0.4/32 dev zxdprxb src 198.18.0.2
ip neighbor replace 198.18.0.4 lladdr "$zutcp_test_mac" nud reachable dev zxdprxb
ip route add 198.18.0.5/32 via 198.18.0.1 dev zxdprxb onlink src 198.18.0.2 mtu 1400
ip -6 route add 2001:db8::3/128 dev zxdprxb src 2001:db8::2
ip -6 route add 2001:db8::4/128 dev zxdprxb src 2001:db8::2
ip -6 neighbor replace 2001:db8::4 lladdr "$zutcp_test_mac" nud reachable dev zxdprxb
ip -6 route add 2001:db8::5/128 via 2001:db8::1 dev zxdprxb onlink src 2001:db8::2 mtu 1400
# Nonlocal next hop for asynchronous setup on the raw Instance's own interface.
# No kernel address is added to the raw side: peer traffic must traverse veth.
zutcp_peer_mac=$(ip -o link show dev zxdprxb | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip route add 198.18.0.6/32 dev zxdprxa
ip -6 route add 2001:db8::6/128 dev zxdprxa
ip neighbor replace 198.18.0.6 lladdr "$zutcp_peer_mac" nud permanent dev zxdprxa
ip -6 neighbor replace 2001:db8::6 lladdr "$zutcp_peer_mac" nud permanent dev zxdprxa
# Veth otherwise carries CHECKSUM_PARTIAL metadata, unlike a real Ethernet wire.
ethtool -K zxdprxb tx off tso off gso off gro off >/dev/null

# Each independent XDP instance gets its own interface tuple. A new
# registration does not depend on asynchronous release of the previous one.
carriage_pair() {
  local family=$1 raw=$2 peer=$3 local_ip=$4 peer_ip=$5 next_hop=$6 prefix=$7
  ip link add "$raw" numrxqueues 1 numtxqueues 1 type veth peer name "$peer" numrxqueues 1 numtxqueues 1
  ip link set "$raw" up
  ip link set "$peer" up
  if [[ $family == 6 ]]; then
    ip -6 address add "$peer_ip/$prefix" dev "$peer" nodad
  else
    ip address add "$peer_ip/$prefix" dev "$peer"
  fi
  local raw_mac peer_mac
  raw_mac=$(ip -o link show dev "$raw" | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
  peer_mac=$(ip -o link show dev "$peer" | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
  ip -"$family" route add "$local_ip/$prefix" dev "$peer" src "$peer_ip"
  ip -"$family" neighbor replace "$local_ip" lladdr "$raw_mac" nud permanent dev "$peer"
  ip -"$family" route add "$next_hop/$prefix" dev "$raw"
  ip -"$family" neighbor replace "$next_hop" lladdr "$peer_mac" nud permanent dev "$raw"
  ethtool -K "$peer" tx off tso off gso off gro off >/dev/null
}
carriage_pair 6 zip6a zip6b 2001:db8:6::1 2001:db8:6::2 2001:db8:6::6 128
carriage_pair 4 zitcp4a zitcp4b 198.18.4.1 198.18.4.2 198.18.4.6 32
carriage_pair 6 zitcp6a zitcp6b 2001:db8:7::1 2001:db8:7::2 2001:db8:7::6 128
export ZUTCP_IP_FIXTURE=1
exec "$@"
