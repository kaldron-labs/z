#!/usr/bin/env bash
set -euo pipefail
# Scope every network object to this private parent and two task-owned children.
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
family=$1
mode=$2
out=$3
tfo_profile=authorized
tfo_count=2
if [[ $mode == tfo-matrix ]]; then
  for profile in authorized sender-denied receiver-denied disabled-service stale \
      setup-loss setup-duplicate response-loss; do
    run="$out/$profile"
    mkdir -p "$run"
    env ZUTCP_TEST_TFO_PROFILE="$profile" bash "$0" "$family" tfo "$run"
  done
  exit 0
fi
if [[ $mode == tfo ]]; then
  tfo_profile=${ZUTCP_TEST_TFO_PROFILE:-authorized}
  case $tfo_profile in
    authorized|sender-denied|receiver-denied|disabled-service|stale|setup-loss|setup-duplicate|\
      response-loss) ;;
    *) exit 2 ;;
  esac
  if [[ $tfo_profile == stale ]]; then tfo_count=3; fi
  unset ZUTCP_TEST_TFO_SEND_DENIED ZUTCP_TEST_TFO_RECEIVE_DENIED
  unset ZUTCP_TEST_TFO_SERVER_FIRST ZUTCP_TEST_TFO_SERVER_DISABLED
  if [[ $tfo_profile == sender-denied ]]; then export ZUTCP_TEST_TFO_SEND_DENIED=1; fi
  if [[ $tfo_profile == receiver-denied ]]; then export ZUTCP_TEST_TFO_RECEIVE_DENIED=1; fi
  if [[ $tfo_profile == disabled-service ]]; then export ZUTCP_TEST_TFO_SERVER_DISABLED=1; fi
  export ZUTCP_TEST_TFO=1 ZUTCP_TEST_TFO_COUNT="$tfo_count"
  printf '%s\n' "$tfo_profile" > "$out/tfo-profile"
fi
if [[ $mode == tail-matrix ]]; then
  for profile in 7 3; do
    run="$out/$profile-$profile"
    mkdir -p "$run"
    env ZUTCP_TEST_CLIENT_OPTIONS="$profile" ZUTCP_TEST_SERVER_OPTIONS="$profile" \
      bash "$0" "$family" tail "$run"
  done
  exit 0
fi
if [[ $mode == ecn-matrix ]]; then
  for profiles in 15:15 15:7 7:15 8:8; do
    client_options=${profiles%:*}
    server_options=${profiles#*:}
    run="$out/$client_options-$server_options"
    mkdir -p "$run"
    env ZUTCP_TEST_CLIENT_OPTIONS="$client_options" \
      ZUTCP_TEST_SERVER_OPTIONS="$server_options" \
      bash "$0" "$family" ecn "$run"
  done
  exit 0
fi
if [[ $mode == option-matrix ]]; then
  for profiles in 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7 7:0 0:7 5:3 3:5; do
    client_options=${profiles%:*}
    server_options=${profiles#*:}
    run="$out/$client_options-$server_options"
    mkdir -p "$run"
    env ZUTCP_TEST_CLIENT_OPTIONS="$client_options" \
      ZUTCP_TEST_SERVER_OPTIONS="$server_options" \
      bash "$0" "$family" options "$run"
  done
  for scenario in 0:ack-loss 1:sack-holes; do
    profile=${scenario%:*}
    fault_mode=${scenario#*:}
    run="$out/$profile-$profile-$fault_mode"
    mkdir -p "$run"
    env ZUTCP_TEST_CLIENT_OPTIONS="$profile" ZUTCP_TEST_SERVER_OPTIONS="$profile" \
      bash "$0" "$family" "$fault_mode" "$run"
  done
  exit 0
fi
if [[ $mode == sack-holes || $mode == sack-reneging ]]; then
  export ZUTCP_TEST_CLIENT_OPTIONS=${ZUTCP_TEST_CLIENT_OPTIONS:-7}
  export ZUTCP_TEST_SERVER_OPTIONS=${ZUTCP_TEST_SERVER_OPTIONS:-7}
  ((ZUTCP_TEST_CLIENT_OPTIONS & 1))
  ((ZUTCP_TEST_SERVER_OPTIONS & 1))
fi
if [[ $mode == tail ]]; then
  export ZUTCP_TEST_CLIENT_OPTIONS=${ZUTCP_TEST_CLIENT_OPTIONS:-7}
  export ZUTCP_TEST_SERVER_OPTIONS=${ZUTCP_TEST_SERVER_OPTIONS:-7}
  export ZUTCP_TEST_TLP_DELAY_US=0
fi
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
ip link set zutcpca mtu 1500 up
ip link set zutcpsa mtu 1500 up
ip link set zutcpbr up
ip -n "$cli" link set lo up
ip -n "$srv" link set lo up
ip -n "$cli" link set zutcpa mtu 1500 up
ip -n "$srv" link set zutcpb mtu 1500 up
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
# Bound the capture snap length by the actual task-owned untagged Ethernet MTU.
ip -j link show dev zutcpca > "$out/capture-client-port.json"
ip -j link show dev zutcpsa > "$out/capture-server-port.json"
ip -n "$cli" -j link show dev zutcpa > "$out/capture-client-link.json"
ip -n "$srv" -j link show dev zutcpb > "$out/capture-server-link.json"
server=198.18.1.2
if [[ $family == 6 ]]; then server=2001:db8:1::2; fi
server_options=()
if [[ $mode == wait-eof ]]; then server_options=(--wait-eof); fi
if [[ $mode == tfo ]]; then
  server_options=("$tfo_count")
  if [[ $tfo_profile == stale ]]; then server_options=(1); fi
fi
startServer() {
  ip netns exec "$srv" env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 timeout 35 \
  "$root/zutcp/example/zutcpserver" zutcpb "$server" 18181 "${server_options[@]}" \
    >> "$out/server.out" 2>> "$out/server.err" &
  server_pid=$!
  local ready=0
  for ((i=0;i<300;i++)); do
    state=$(ip -n "$srv" -o link show zutcpb)
    if [[ $state == *xdp* ]]; then ready=1; break; fi
    if ! kill -0 "$server_pid" 2>/dev/null; then cat "$out/server.err" >&2; exit 1; fi
    sleep .01
  done
  ((ready))
}
: > "$out/server.out"
: > "$out/server.err"
startServer
host_tcp() {
  local side=$1 ns=$2 phase=$3
  ip netns exec "$ns" python3 -c '
import json, time
lines = open("/proc/net/snmp").read().splitlines()
for i, line in enumerate(lines):
    if line.startswith("Tcp:"):
        fields = dict(zip(line.split()[1:], map(int, lines[i + 1].split()[1:])))
        print(json.dumps({"time": time.time(), "out_rsts": fields["OutRsts"]}))
        break
else:
    raise SystemExit("missing kernel TCP counters")
' > "$out/$side-host-$phase.json"
}
if [[ $mode == reorder ]]; then
  host_tcp client "$cli" before
  host_tcp server "$srv" before
fi
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
        match u8 "$flag" "$flag" at "$((short_offset - 7))" flowid 1:2
    done
  elif [[ $mode == data-loss || $mode == reorder ]]; then
    # TCP flags stay at the base-header offset when options are present.
    # Select positive DATA of 1024..2047 IP bytes (IPv6 payload bytes).
    local short_offset=40 length_offset=2 protocol_offset=9
    if [[ $family == 6 ]]; then
      short_offset=60 length_offset=4 protocol_offset=6
    fi
    tc filter add dev "$port" parent 1: protocol "$protocol" pref 1 u32 \
      match u8 6 0xff at "$protocol_offset" \
      match u8 0x08 0x0e at "$((short_offset - 7))" \
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
  tail)
    protocol=ip iphead=20 protocol_offset=9
    if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
    tc qdisc add dev zutcpsa root handle 1: prio bands 3 \
      priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
    tc qdisc add dev zutcpsa parent 1:3 handle 30: netem loss 100%
    tc filter add dev zutcpsa parent 1: protocol all pref 1 handle 0x5a18 fw flowid 1:1
    # Install the exact last original DATA selector after the setup/producer gate.
    ;;
  sack-holes|sack-reneging)
    tc qdisc add dev zutcpsa root handle 1: prio bands 3 \
      priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
    tc qdisc add dev zutcpsa parent 1:3 handle 30: netem loss 100%
    if [[ $mode == sack-holes || $mode == sack-reneging ]]; then
      tc qdisc add dev zutcpca root handle 1: prio bands 3 \
        priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
      tc qdisc add dev zutcpca parent 1:3 handle 30: netem loss 100%
      protocol=ip iphead=20 protocol_offset=9
      if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
      # Hold all ordinary post-SYN receiver feedback, including DATA piggyback ACKs.
      # Holes holds initial feedback until the actual receiver has two SACK blocks.
      # Reneging forwards selected actual carriers through its marked injector.
      tc filter add dev zutcpca parent 1: protocol "$protocol" pref 10 u32 \
        match u8 6 0xff at "$protocol_offset" \
        match u8 0x10 0x16 at "$((iphead + 13))" flowid 1:3
    fi
    ;;
  plain|wait-eof|options|ecn|tfo) ;;
  *) exit 2 ;;
esac
# Keep actual directional TCP evidence for every scenario.
if [[ -n $mode ]]; then
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
  tcpdump --immediate-mode -B 8192 -i zutcpsa -Q out -n -s 2048 -U -w "$out/to-server.pcap" \
    'tcp port 18181' > "$out/to-server.capture" 2>&1 &
  capture_srv=$!
  tcpdump --immediate-mode -B 8192 -i zutcpca -Q out -n -s 2048 -U -w "$out/to-client.pcap" \
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
if [[ $mode == reorder ]]; then
  tcpdump --immediate-mode -B 8192 -i zutcpca -Q in -n -s 2048 -U \
    -w "$out/client-source.pcap" 'tcp port 18181' > "$out/client-source.capture" 2>&1 &
  bootstrap_cli=$!
  tcpdump --immediate-mode -B 8192 -i zutcpsa -Q in -n -s 2048 -U \
    -w "$out/server-source.pcap" 'tcp port 18181' > "$out/server-source.capture" 2>&1 &
  bootstrap_srv=$!
  for side in client server; do
    for ((i=0;i<100;i++)); do
      if grep -q 'listening on' "$out/$side-source.capture"; then break; fi
      sleep .01
    done
    grep -q 'listening on' "$out/$side-source.capture"
  done
fi
gate_env=()
if [[ $mode == tfo && ($tfo_profile == stale || $tfo_profile == setup-* ||
      $tfo_profile == response-loss) ]]; then
  mkfifo "$out/tfo-next"
  gate_env=(ZUTCP_TEST_TFO_GATE_DIR="$out")
fi
if [[ $mode == tfo && $tfo_profile == setup-loss ]]; then
  tcpdump --immediate-mode -B 8192 -i zutcpca -Q in -n -s 2048 -U \
    -w "$out/tfo-client-source.pcap" 'tcp port 18181' \
    > "$out/tfo-client-source.capture" 2>&1 &
  bootstrap_cli=$!
  for ((i=0;i<100;i++)); do
    if grep -q 'listening on' "$out/tfo-client-source.capture"; then break; fi
    sleep .01
  done
  grep -q 'listening on' "$out/tfo-client-source.capture"
fi
if [[ $mode == tfo && $tfo_profile == response-loss ]]; then
  tcpdump --immediate-mode -B 8192 -i zutcpsa -Q in -n -s 2048 -U \
    -w "$out/tfo-server-source.pcap" 'tcp port 18181' \
    > "$out/tfo-server-source.capture" 2>&1 &
  bootstrap_srv=$!
  for ((i=0;i<100;i++)); do
    if grep -q 'listening on' "$out/tfo-server-source.capture"; then break; fi
    sleep .01
  done
  grep -q 'listening on' "$out/tfo-server-source.capture"
fi
if [[ $mode == ecn || $mode == tail || $mode == ack-loss || $mode == partial-ack ||
      $mode == sack-holes || $mode == sack-reneging ]]; then
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
    tcpdump --immediate-mode -B 8192 -i "$device" -n -s 2048 -U \
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
if [[ $mode == tfo ]]; then
  request=$(python3 -c 'print("tfo-original-" * 20, end="")')
fi
if [[ $mode == options || $mode == ecn || $mode == tail || $mode == sack-holes || $mode == sack-reneging ]]; then
  request=$(python3 -c 'print("native-zxdp-" * 6800, end="")')
fi
if [[ $mode == sack-holes || $mode == sack-reneging ]]; then
  python3 "$root/zutcp/itest/zutcpoptions.py" fault "$out" "$mode" zutcpca zutcpsa \
    > "$out/sack-monitor.log" 2>&1 &
  inject_pid=$!
  for ((i=0;i<1000;i++)); do
    if [[ -f "$out/sack-monitor-ready" ]]; then break; fi
    kill -0 "$inject_pid"
    sleep .01
  done
  [[ -f "$out/sack-monitor-ready" ]]
fi
if [[ $mode == tail ]]; then
  python3 "$root/zutcp/itest/zutcpoptions.py" tail-fault "$out" \
    > "$out/tail-monitor.log" 2>&1 &
  inject_pid=$!
  for ((i=0;i<1000;i++)); do
    if [[ -f "$out/tail-monitor-ready" ]]; then break; fi
    kill -0 "$inject_pid"
    sleep .01
  done
  [[ -f "$out/tail-monitor-ready" ]]
fi
ip netns exec "$cli" env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "${gate_env[@]}" timeout 30 \
  "$root/zutcp/example/zutcpclient" zutcpa "$server" 18181 "$request" \
  > "$out/client.out" 2> "$out/client.err" &
client_pid=$!
if [[ $mode == tfo && ($tfo_profile == stale || $tfo_profile == setup-* ||
      $tfo_profile == response-loss) ]]; then
  for ((i=0;i<1000;i++)); do
    if [[ -f $out/tfo-first-complete ]]; then break; fi
    kill -0 "$client_pid"
    sleep .01
  done
  [[ -f $out/tfo-first-complete ]]
  if [[ $tfo_profile == stale ]]; then
    wait "$server_pid"
    server_pid=
    server_options=(2)
    export ZUTCP_TEST_TFO_SERVER_FIRST=2
    startServer
  else
    protocol=ip iphead=20 protocol_offset=9
    if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
    kind=loss
    tfo_fault_port=zutcpsa
    tfo_flags=0x02
    tfo_port_offset=$((iphead + 2))
    if [[ $tfo_profile == setup-duplicate ]]; then kind=duplicate; fi
    if [[ $tfo_profile == response-loss ]]; then
      tfo_fault_port=zutcpca
      tfo_flags=0x12
      tfo_port_offset=$iphead
    fi
    tc qdisc add dev "$tfo_fault_port" root handle 1: prio bands 3 \
      priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
    tc qdisc add dev "$tfo_fault_port" parent 1:3 handle 30: netem "$kind" 100%
    tc filter add dev "$tfo_fault_port" parent 1: protocol "$protocol" pref 1 u32 \
      match u8 6 0xff at "$protocol_offset" \
      match u8 "$tfo_flags" 0x12 at "$((iphead + 13))" \
      match u16 18181 0xffff at "$tfo_port_offset" flowid 1:3
  fi
  timeout 10 bash -c 'printf R > "$1"' bash "$out/tfo-next"
  if [[ $tfo_profile == setup-loss || $tfo_profile == response-loss ]]; then
    dropped=0
    for ((i=0;i<500;i++)); do
      stats=$(netemStats "$tfo_fault_port")
      if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
      if ((dropped)); then break; fi
      kill -0 "$client_pid"
      sleep .01
    done
    printf '%s\n' "$stats" > "$out/tfo-fault"
    ((dropped))
    tc qdisc del dev "$tfo_fault_port" root
  fi
fi
if [[ $mode == tail ]]; then
  wire_values=$(timeout 15 cat "$out/wire-origins")
  read -r client_base server_base <<< "$wire_values"
  wait "$origin_monitor"
  origin_monitor=
  producer=0
  for ((i=0;i<1000;i++)); do
    producer=$(sed -n 's/^wire producer=//p' "$out/client.err" | head -1)
    if [[ $producer =~ ^[0-9]+$ ]] && ((producer)); then break; fi
    kill -0 "$client_pid"
    sleep .01
  done
  ((producer > 0 && producer < 81600))
  tail_offset=$(((81600 - 1) / producer * producer))
  tail_sequence=$(((client_base + tail_offset) & 0xffffffff))
  protocol=ip iphead=20 protocol_offset=9
  if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
  tc filter add dev zutcpsa parent 1: protocol "$protocol" pref 10 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u8 0x08 0x0f at "$((iphead + 13))" \
    match u32 "$tail_sequence" 0xffffffff at "$((iphead + 4))" flowid 1:3
  printf '%s %s\n' "$client_base" "$server_base" > "$out/wire-bases"
  printf '%s %s\n' "$producer" "$tail_offset" > "$out/tail-producer"
  timeout 10 bash -c 'printf R > "$1"' bash "$out/wire-release"
fi
if [[ $mode == ecn ]]; then
  wire_values=$(timeout 15 cat "$out/wire-origins")
  read -r client_base server_base <<< "$wire_values"
  [[ $client_base =~ ^[0-9]+$ && $server_base =~ ^[0-9]+$ ]]
  wait "$origin_monitor"
  origin_monitor=
  protocol=ip iphead=20 protocol_offset=9 ect=0x02 mask=0x03 ce=0x03 keep=0x03
  checksum=()
  if [[ $family == 6 ]]; then
    protocol=ipv6 iphead=40 protocol_offset=6 ect=0x20 mask=0x30 ce=0x30 keep=0x30
  else
    checksum=(pipe action csum ip4h)
  fi
  tc qdisc add dev zutcpsa clsact
  # Mark only the first eligible new original; later CWR DATA stays unmarked.
  tc filter add dev zutcpsa egress protocol "$protocol" pref 1 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u8 0x08 0x0e at "$((iphead + 13))" \
    match u32 "$client_base" 0xffffffff at "$((iphead + 4))" \
    match u8 "$ect" "$mask" at 1 \
    action pedit munge offset 1 u8 set "$ce" retain "$keep" "${checksum[@]}"
  printf '%s %s\n' "$client_base" "$server_base" > "$out/wire-bases"
  timeout 10 bash -c 'printf R > "$1"' bash "$out/wire-release"
fi
if [[ $mode == sack-holes || $mode == sack-reneging ]]; then
  wire_values=$(timeout 15 cat "$out/wire-origins")
  read -r client_base server_base <<< "$wire_values"
  [[ $client_base =~ ^[0-9]+$ && $server_base =~ ^[0-9]+$ ]]
  wait "$origin_monitor"
  origin_monitor=
  protocol=ip iphead=20 protocol_offset=9
  if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
  python3 "$root/zutcp/itest/zutcprecovery.py" ack-bands "$client_base" 4096 8191 \
    > "$out/sack-seq-bands"
  if [[ $mode == sack-holes ]]; then
    printf '0x%08x 0xffffffff\n' "$client_base" >> "$out/sack-seq-bands"
  fi
  while read -r value mask; do
    tc filter add dev zutcpsa parent 1: protocol "$protocol" pref 1 u32 \
      match u8 6 0xff at "$protocol_offset" \
      match u8 0x08 0x0e at "$((iphead + 13))" \
      match u32 "$value" "$mask" at "$((iphead + 4))" flowid 1:3
  done < "$out/sack-seq-bands"
  tc filter show dev zutcpsa parent 1: > "$out/sack-seq-filter"
  printf '%s %s\n' "$client_base" "$server_base" > "$out/wire-bases"
  timeout 10 bash -c 'printf R > "$1"' bash "$out/wire-release"
fi
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
      # Dispose of held reverse DATA/FIN as well as the ACK-loss filter. Keeping
      # the four-second queue after either Stack stops feeds stale copies to
      # the host kernel. The real sender still owns those unacknowledged bytes
      # and must recover them through the now-unimpeded native TCP path.
      tc -s qdisc show dev "$port" > "$out/$port.release-qdisc"
    fi
    tc qdisc del dev "$port" root
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
if [[ $mode == reorder ]]; then
  # Source processes are reaped. Observe all remaining controlled netem copies,
  # including kernel CLOSED replies after AF_XDP detaches, before closing taps.
  for port in zutcpsa zutcpca; do
    drained=0
    for ((i=0;i<100;i++)); do
      stats=$(tc -s qdisc show dev "$port")
      if awk '$1 == "backlog" && $3 != "0p" { busy=1 } END { exit busy }' <<< "$stats"; then
        drained=1; break
      fi
      sleep .01
    done
    printf '%s\n' "$stats" > "$out/$port.drain-qdisc"
    ((drained))
  done
fi
if [[ -n "$capture_cli" || -n "$capture_srv" ]]; then sleep .05; fi
if [[ $mode == reorder ]]; then
  host_tcp client "$cli" after
  host_tcp server "$srv" after
fi
stop_capture
# Refuse an incomplete observation before protocol/coverage checks can claim success.
python3 - "$root" "$out" <<'CAPTURE'
import json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / 'zutcp/itest'))
from zutcprecovery import pcap_frames
out = Path(sys.argv[2])
links = {}
for name in ('client-port', 'server-port', 'client-link', 'server-link'):
    records = json.loads((out / ('capture-' + name + '.json')).read_text())
    assert len(records) == 1 and records[0]['mtu'] == 1500, (name, records)
    links[name] = {'name': records[0]['ifname'], 'mtu': records[0]['mtu']}
files = {}
for path in sorted(out.glob('*.pcap')):
    footer = path.with_suffix('.capture').read_text().splitlines()
    complete = any(re.fullmatch(r'[0-9]+ packets captured', line) for line in footer)
    complete &= any(re.fullmatch(r'[0-9]+ packets received by filter', line) for line in footer)
    assert complete and '0 packets dropped by kernel' in footer, ('incomplete capture', path, footer)
    raw = path.read_bytes()
    little = raw[:4] in (bytes.fromhex('d4c3b2a1'), bytes.fromhex('4d3cb2a1'))
    endian = 'little' if little else 'big'
    snap = int.from_bytes(raw[16:20], endian)
    assert snap == 2048, (path, snap)
    count = maximum = 0
    for stamp, frame in pcap_frames(path): # also requires caplen == original wire length
        assert len(frame) <= 1514, ('capture exceeds private MTU plus Ethernet', path, len(frame))
        count += 1
        maximum = max(maximum, len(frame))
    files[path.name] = {'packets': count, 'max_wire_bytes': maximum, 'snaplen': snap,
                        'kernel_drops': 0, 'untruncated': True}
assert files, 'missing complete directional captures'
(out / 'capture-evidence.json').write_text(json.dumps(
    {'links': links, 'buffer_kib': 8192, 'captures': files}, indent=2) + '\n')
CAPTURE
if [[ $mode == tfo && $tfo_profile == setup-duplicate ]]; then
  netemStats zutcpsa > "$out/tfo-fault"
fi
if [[ $mode == reorder || $mode == ack-loss || $mode == partial-ack ]]; then
  for port in zutcpsa zutcpca; do
    tc -s -d qdisc show dev "$port" > "$out/$port.final-qdisc"
  done
fi
printf 'echo: %s\n' "$request" > "$out/expected"
if [[ $mode == tfo ]]; then
  for ((i=1;i<tfo_count;i++)); do printf 'echo: %s\n' "$request" >> "$out/expected"; done
  printf '%s' "$request" > "$out/request"
fi
cmp "$out/expected" "$out/client.out"
if [[ $mode == tfo ]]; then
  python3 "$root/zutcp/itest/zutcptfo.py" "$out"
elif [[ $mode == options ]]; then
  python3 "$root/zutcp/itest/zutcpoptions.py" "$out"
elif [[ $mode == sack-holes || $mode == sack-reneging ]]; then
  python3 "$root/zutcp/itest/zutcpoptions.py" selective "$out" "$mode"
elif [[ $mode == ecn ]]; then
  tc -s filter show dev zutcpsa egress > "$out/ecn-filter"
  python3 "$root/zutcp/itest/zutcpoptions.py" ecn "$out"
elif [[ $mode == tail ]]; then
  tc -s qdisc show dev zutcpsa > "$out/tail-loss"
  python3 "$root/zutcp/itest/zutcpoptions.py" tail-check "$out"
fi
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

if [[ $mode == plain || $mode == wait-eof || $mode == loss || $mode == data-loss ||
      $mode == duplicate || $mode == reorder || $mode == ack-loss ]]; then
  pair_check=pair
  if [[ $mode == reorder ]]; then pair_check=pair-reorder; fi
  python3 "$root/zutcp/itest/zutcprecovery.py" "$pair_check" "$out"
fi
