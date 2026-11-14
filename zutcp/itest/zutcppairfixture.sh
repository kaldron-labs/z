#!/usr/bin/env bash
set -euo pipefail
# Scope every network object to this private parent and two task-owned children.
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
family=$1
mode=$2
out=$3
root=$(cd "$(dirname "$0")/../.." && pwd)
cli=zutcp-cli-$$
srv=zutcp-srv-$$
cli_made=0 srv_made=0 bridge_made=0 cli_link_made=0 srv_link_made=0
server_pid= client_pid= capture_cli= capture_srv= inject_pid= origin_monitor= bootstrap_cli= bootstrap_srv=
stop_capture() {
  for capture in "$bootstrap_cli" "$bootstrap_srv"; do
    if [[ -n $capture ]]; then
      kill -INT "$capture" 2>/dev/null || true
      wait "$capture" 2>/dev/null || true
    fi
  done
  bootstrap_cli= bootstrap_srv=
  if [[ -n "$capture_cli" ]]; then
    kill -INT "$capture_cli" 2>/dev/null || true
    wait "$capture_cli" 2>/dev/null || true
    capture_cli=
  fi
  if [[ -n "$capture_srv" ]]; then
    kill -INT "$capture_srv" 2>/dev/null || true
    wait "$capture_srv" 2>/dev/null || true
    capture_srv=
  fi
}
cleanup() {
  if [[ -n $origin_monitor ]]; then
    kill "$origin_monitor" 2>/dev/null || true
    wait "$origin_monitor" 2>/dev/null || true
  fi
  if [[ -n "$inject_pid" ]]; then
    kill "$inject_pid" 2>/dev/null || true
    wait "$inject_pid" 2>/dev/null || true
  fi
  stop_capture
  if [[ -n "$client_pid" ]]; then
    kill "$client_pid" 2>/dev/null || true
    wait "$client_pid" 2>/dev/null || true
  fi
  if [[ -n "$server_pid" ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  # A closed AF_XDP socket can keep its former namespace alive through kernel
  # retirement. Delete our parent veth ends explicitly before dropping names.
  if ((cli_link_made)); then ip link del zutcpca 2>/dev/null || true; fi
  if ((srv_link_made)); then ip link del zutcpsa 2>/dev/null || true; fi
  if ((cli_made)); then ip netns del "$cli" 2>/dev/null || true; fi
  if ((srv_made)); then ip netns del "$srv" 2>/dev/null || true; fi
  if ((bridge_made)); then ip link del zutcpbr 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'printf "fixture failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
trap 'exit 1' INT TERM
ip netns add "$cli"
cli_made=1
ip netns add "$srv"
srv_made=1
ip link add zutcpbr type bridge
bridge_made=1
# A kernel bridge hop lets netem affect actual wire traffic. AF_XDP direct Tx
# bypasses its NIC's ordinary egress qdisc, so netem belongs on these middle ports.
ip link add zutcpca numrxqueues 1 numtxqueues 1 type veth peer name zutcpa
cli_link_made=1
ip link add zutcpsa numrxqueues 1 numtxqueues 1 type veth peer name zutcpb
srv_link_made=1
ip link set zutcpa netns "$cli"
ip link set zutcpb netns "$srv"
ip link set zutcpca master zutcpbr
ip link set zutcpsa master zutcpbr
ip link set zutcpca up
ip link set zutcpsa up
ip link set zutcpbr up
ip -n "$cli" link set lo up
ip -n "$srv" link set lo up
ip -n "$cli" link set zutcpa up
ip -n "$srv" link set zutcpb up
ip -n "$cli" addr add 198.18.1.1/32 dev zutcpa
ip -n "$srv" addr add 198.18.1.2/32 dev zutcpb
ip -n "$cli" -6 addr add 2001:db8:1::1/128 dev zutcpa nodad
ip -n "$srv" -6 addr add 2001:db8:1::2/128 dev zutcpb nodad
cli_mac=$(ip -n "$cli" -o link show zutcpa | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
srv_mac=$(ip -n "$srv" -o link show zutcpb | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip -n "$cli" route add 198.18.1.2/32 dev zutcpa src 198.18.1.1
ip -n "$srv" route add 198.18.1.1/32 dev zutcpb src 198.18.1.2
ip -n "$cli" -6 route add 2001:db8:1::2/128 dev zutcpa src 2001:db8:1::1
ip -n "$srv" -6 route add 2001:db8:1::1/128 dev zutcpb src 2001:db8:1::2
ip -n "$cli" neigh replace 198.18.1.2 lladdr "$srv_mac" nud permanent dev zutcpa
ip -n "$srv" neigh replace 198.18.1.1 lladdr "$cli_mac" nud permanent dev zutcpb
ip -n "$cli" -6 neigh replace 2001:db8:1::2 lladdr "$srv_mac" nud permanent dev zutcpa
ip -n "$srv" -6 neigh replace 2001:db8:1::1 lladdr "$cli_mac" nud permanent dev zutcpb
ip netns exec "$cli" ethtool -K zutcpa tx off tso off gso off gro off >/dev/null
ip netns exec "$srv" ethtool -K zutcpb tx off tso off gso off gro off >/dev/null
server=198.18.1.2
if [[ $family == 6 ]]; then server=2001:db8:1::2; fi
server_options=()
if [[ $mode == wait-eof ]]; then server_options=(--wait-eof); fi
ip netns exec "$srv" env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 timeout 35 \
  "$root/zutcp/example/zutcpserver" zutcpb "$server" 18181 "${server_options[@]}" \
  > "$out/server.out" 2> "$out/server.err" &
server_pid=$!
ready=0
for ((i=0;i<300;i++)); do
  state=$(ip -n "$srv" -o link show zutcpb)
  if [[ $state == *xdp* ]]; then ready=1; break; fi
  if ! kill -0 "$server_pid" 2>/dev/null; then cat "$out/server.err" >&2; exit 1; fi
  sleep .01
done
((ready))
fault() {
  local port=$1 direction=$2 kind=$3
  # Unrelated ARP/IPv6 multicast stays in band 1. Select this family's transport
  # flight by port or established DATA geometry; check the resulting drop count.
  local protocol=ip
  if [[ $family == 6 ]]; then protocol=ipv6; fi
  tc qdisc add dev "$port" root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  if [[ $kind == reorder ]]; then
    tc qdisc add dev "$port" parent 1:3 handle 30: netem delay 100ms reorder 100% gap 2
  else
    tc qdisc add dev "$port" parent 1:3 handle 30: netem "$kind" 100%
  fi
  if [[ $mode == ack-loss || $mode == partial-ack ]]; then
    local short_offset=40 length_offset=2 protocol_offset=9
    if [[ $family == 6 ]]; then short_offset=60 length_offset=4 protocol_offset=6; fi
    local tcp_ack=$((short_offset - 12))
    # Exact ACK interval is installed after captured SYN origins, before the
    # client's control-thread application gate releases any request bytes.
    tc qdisc add dev "$port" parent 1:2 handle 20: netem delay 4s
    # Every reverse DATA/FIN header can acknowledge the request, including
    # the small final segment. Delay all of them during the ACK-loss interval.
    for flag in 0x08 0x01; do
      tc filter add dev "$port" parent 1: protocol "$protocol" pref 2 u32 \
        match u8 6 0xff at "$protocol_offset" \
        match u8 0 0x80 at "$short_offset" \
        match u8 "$flag" "$flag" at "$((short_offset - 7))" flowid 1:2
    done
  elif [[ $mode == data-loss || $mode == reorder ]]; then
    # This fixture emits fixed IPv4/IPv6 and 20-byte TCP headers. Select
    # established Short packets of 1024..2047 IP bytes (IPv6 payload bytes).
    # Initial/bootstrap and small ACK/control packets cannot enter this band.
    local short_offset=40 length_offset=2 protocol_offset=9
    if [[ $family == 6 ]]; then
      short_offset=60 length_offset=4 protocol_offset=6
    fi
    tc filter add dev "$port" parent 1: protocol "$protocol" pref 1 u32 \
      match u8 6 0xff at "$protocol_offset" \
      match u8 0 0x80 at "$short_offset" \
      match u8 0x04 0xfc at "$length_offset" flowid 1:3
  else
    tc filter add dev "$port" parent 1: protocol "$protocol" pref 1 flower \
      ip_proto tcp "$direction" 18181 classid 1:3
  fi
  tc filter show dev "$port" parent 1: > "$out/$port.filter"
}
netemStats() {
  tc -s qdisc show dev "$1" | awk '
    $1 == "qdisc" { selected = ($2 == "netem" && $3 == "30:") }
    selected { print }
  '
}
case $mode in
  loss|data-loss)
    fault zutcpsa dst_port loss
    fault zutcpca src_port loss
    ;;
  duplicate)
    fault zutcpsa dst_port duplicate
    fault zutcpca src_port duplicate
    ;;
  reorder)
    fault zutcpsa dst_port reorder
    fault zutcpca src_port reorder
    ;;
  ack-loss|partial-ack) fault zutcpca src_port loss ;;
  plain|wait-eof) ;;
  *) exit 2 ;;
esac
if [[ $mode == wait-eof || $mode == reorder || $mode == ack-loss ||
      $mode == partial-ack ]]; then
  if [[ $mode == partial-ack ]]; then
    python3 "$root/zutcp/itest/zutcprecovery.py" inject "$out/injection.json" \
      > "$out/injection.log" 2>&1 &
    inject_pid=$!
    for ((i=0;i<1000;i++)); do
      if [[ -f "$out/injection.ready" ]]; then break; fi
      kill -0 "$inject_pid"
      sleep .01
    done
    [[ -f "$out/injection.ready" ]]
  fi
  tcpdump --immediate-mode -B 8192 -i zutcpsa -Q out -n -s 0 -U -w "$out/to-server.pcap" \
    'tcp port 18181' > "$out/to-server.capture" 2>&1 &
  capture_srv=$!
  tcpdump --immediate-mode -B 8192 -i zutcpca -Q out -n -s 0 -U -w "$out/to-client.pcap" \
    'tcp port 18181' > "$out/to-client.capture" 2>&1 &
  capture_cli=$!
  for direction in server client; do
    for ((i=0;i<100;i++)); do
      if grep -q 'listening on' "$out/to-$direction.capture"; then break; fi
      sleep .01
    done
    grep -q 'listening on' "$out/to-$direction.capture"
  done
fi
gate_env=()
if [[ $mode == ack-loss || $mode == partial-ack ]]; then
  mkfifo "$out/wire-origins" "$out/wire-release"
  python3 "$root/zutcp/itest/zutcprecovery.py" wire-gate "$out" zutcpca 18181 &
  origin_monitor=$!
  for ((i=0;i<1000;i++)); do
    if [[ -f $out/wire-monitor-ready ]]; then break; fi
    kill -0 "$origin_monitor"
    sleep .01
  done
  [[ -f $out/wire-monitor-ready ]]
  gate_env=(ZUTCP_WIRE_GATE_DIR="$out")
fi
if [[ $mode == loss ]]; then
  for side in client server; do
    device=zutcpca
    if [[ $side == server ]]; then device=zutcpsa; fi
    tcpdump --immediate-mode -B 8192 -i "$device" -n -s 0 -U \
      -w "$out/bootstrap-$side.pcap" 'tcp port 18181' \
      > "$out/bootstrap-$side.capture" 2>&1 &
    if [[ $side == client ]]; then bootstrap_cli=$!; else bootstrap_srv=$!; fi
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/bootstrap-$side.capture"; then break; fi
      kill -0 "$!"
      sleep .01
    done
    grep -q 'listening on' "$out/bootstrap-$side.capture"
  done
fi
request=$(python3 -c 'print("native-zxdp-" * 600, end="")')
ip netns exec "$cli" env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "${gate_env[@]}" timeout 30 \
  "$root/zutcp/example/zutcpclient" zutcpa "$server" 18181 "$request" \
  > "$out/client.out" 2> "$out/client.err" &
client_pid=$!
if [[ $mode == ack-loss || $mode == partial-ack ]]; then
  wire_values=$(timeout 15 cat "$out/wire-origins")
  read -r client_base server_base <<< "$wire_values"
  [[ $client_base =~ ^[0-9]+$ && $server_base =~ ^[0-9]+$ ]]
  wait "$origin_monitor"
  origin_monitor=
  short_offset=40 protocol_offset=9
  if [[ $family == 6 ]]; then short_offset=60 protocol_offset=6; fi
  tcp_ack=$((short_offset - 12))
  python3 "$root/zutcp/itest/zutcprecovery.py" ack-bands "$client_base" 1024 8191 \
    > "$out/wire-ack-bands"
  while read -r value mask; do
    tc filter add dev zutcpca parent 1: protocol "$([[ $family == 6 ]] && echo ipv6 || echo ip)" \
      pref 1 u32 match u8 6 0xff at "$protocol_offset" \
      match u8 0 0x80 at "$short_offset" \
      match u8 0x10 0x19 at "$((short_offset - 7))" \
      match u16 0 0xfc00 at "$([[ $family == 6 ]] && echo 4 || echo 2)" \
      match u32 "$value" "$mask" at "$tcp_ack" flowid 1:3
  done < "$out/wire-ack-bands"
  tc filter show dev zutcpca parent 1: > "$out/wire-ack-filter"
  printf '%s %s\n' "$client_base" "$server_base" > "$out/wire-bases"
  timeout 10 bash -c 'printf R > "$1"' bash "$out/wire-release"
fi
if [[ $mode == loss || $mode == data-loss || $mode == ack-loss || $mode == partial-ack ]]; then
  ports="zutcpsa zutcpca"
  if [[ $mode == ack-loss || $mode == partial-ack ]]; then ports=zutcpca; fi
  for port in $ports; do
    dropped=0
    for ((i=0;i<500;i++)); do
      stats=$(netemStats "$port")
      if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
      if ((dropped)); then break; fi
      if ! kill -0 "$client_pid" 2>/dev/null; then cat "$out/client.err" >&2; exit 1; fi
      sleep .01
    done
    printf '%s\n' "$stats" > "$out/$port.loss"
    ((dropped))
    printf '# IPv%s %s dropped %s transport packets\n' "$family" "$port" "$dropped"
    if [[ $mode == ack-loss || $mode == partial-ack ]]; then
      sleep 1.1
      tc filter del dev "$port" parent 1: protocol "$([[ $family == 6 ]] && echo ipv6 || echo ip)" pref 1
    else
      tc qdisc del dev "$port" root
    fi
  done
fi
wait "$client_pid"
client_pid=
wait "$server_pid"
server_pid=
if [[ -n "$inject_pid" ]]; then
  wait "$inject_pid"
  inject_pid=
fi
if [[ -n "$capture_cli" || -n "$capture_srv" ]]; then sleep .05; fi
stop_capture
if [[ $mode == reorder || $mode == ack-loss || $mode == partial-ack ]]; then
  for port in zutcpsa zutcpca; do
    tc -s -d qdisc show dev "$port" > "$out/$port.final-qdisc"
  done
fi
printf 'echo: %s\n' "$request" > "$out/expected"
cmp "$out/expected" "$out/client.out"
if [[ $mode == duplicate ]]; then
  for port in zutcpsa zutcpca; do
    stats=$(netemStats "$port")
    printf '%s\n' "$stats" > "$out/$port.duplicate"
    packets=$(printf '%s\n' "$stats" | awk '$1 == "Sent" { print $4; exit }')
    [[ $stats == *"duplicate 100%"* ]]
    ((packets > 0))
    printf '# IPv%s %s duplicated transport traffic; netem sent %s packets\n' \
      "$family" "$port" "$packets"
  done
fi

if [[ $mode == reorder || $mode == ack-loss || $mode == partial-ack ]]; then
  python3 "$root/zutcp/itest/zutcprecovery.py" check "$out" "$mode"
fi

if [[ $mode == wait-eof ]]; then
  python3 "$root/zutcp/itest/zutcprecovery.py" half-close "$out"
fi

if [[ $mode == loss ]]; then
  for side in client server; do
    grep -q '0 packets dropped by kernel' "$out/bootstrap-$side.capture"
  done
  python3 "$root/zutcp/itest/zutcprecovery.py" syn-evidence "$out" retransmit
fi
