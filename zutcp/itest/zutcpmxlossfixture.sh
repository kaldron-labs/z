#!/usr/bin/env bash
set -euo pipefail
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 && ${ZUTCP_MX_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
family=$1
out=$2
child=
mode=${3:-loss}
capture_cli= capture_srv= monitor= origin_monitor=
stop_capture() {
  for capture in "$capture_cli" "$capture_srv"; do
    if [[ -n $capture ]]; then
      kill -INT "$capture" 2>/dev/null || true
      wait "$capture" 2>/dev/null || true
    fi
  done
  capture_cli= capture_srv=
}
bridge=0 a=0 b=0
cleanup() {
  if [[ -n $origin_monitor ]]; then
    kill "$origin_monitor" 2>/dev/null || true
    wait "$origin_monitor" 2>/dev/null || true
  fi
  if [[ -n $monitor ]]; then kill "$monitor" 2>/dev/null || true; wait "$monitor" 2>/dev/null || true; fi
  stop_capture
  if [[ -n $child ]]; then
    kill "$child" 2>/dev/null || true
    wait "$child" 2>/dev/null || true
  fi
  if ((a)); then ip link del zutcpmla 2>/dev/null || true; fi
  if ((b)); then ip link del zutcpmlb 2>/dev/null || true; fi
  if ((bridge)); then ip link del zutcpmlbr 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 1' INT TERM
ip link add zutcpmlbr type bridge
bridge=1
ip link add zutcpmla numrxqueues 1 numtxqueues 1 type veth \
  peer name zutcpmlpa numrxqueues 1 numtxqueues 1
a=1
ip link add zutcpmlb numrxqueues 1 numtxqueues 1 type veth \
  peer name zutcpmlpb numrxqueues 1 numtxqueues 1
b=1
ip link set zutcpmlpa master zutcpmlbr
ip link set zutcpmlpb master zutcpmlbr
for dev in zutcpmlbr zutcpmla zutcpmlb zutcpmlpa zutcpmlpb; do ip link set "$dev" up; done
for dev in zutcpmla zutcpmlb zutcpmlpa zutcpmlpb; do
  ethtool -K "$dev" tx off tso off gso off gro off >/dev/null
done
a_mac=$(ip -o link show zutcpmla | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
b_mac=$(ip -o link show zutcpmlb | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip route add 198.18.0.21/32 dev zutcpmla
ip route add 198.18.0.20/32 dev zutcpmlb
ip -6 route add 2001:db8::21/128 dev zutcpmla
ip -6 route add 2001:db8::20/128 dev zutcpmlb
ip neigh replace 198.18.0.21 lladdr "$b_mac" nud permanent dev zutcpmla
ip neigh replace 198.18.0.20 lladdr "$a_mac" nud permanent dev zutcpmlb
ip -6 neigh replace 2001:db8::21 lladdr "$b_mac" nud permanent dev zutcpmla
ip -6 neigh replace 2001:db8::20 lladdr "$a_mac" nud permanent dev zutcpmlb
export ZXDP_RX_TEST_IFINDEX="$(ip -o link show zutcpmla | cut -d: -f1)"
export ZXDP_RX_TEST_PEER_IFINDEX="$(ip -o link show zutcpmlb | cut -d: -f1)"
netemStats() {
  tc -s qdisc show dev "$1" | awk '
    $1 == "qdisc" { selected = ($2 == "netem" && $3 == "30:") }
    selected { print }
  '
}
start_wire_gate() {
  mkfifo "$out/wire-origins" "$out/wire-release"
  python3 ./zutcprecovery.py wire-gate "$out" zutcpmlpa 18191 &
  origin_monitor=$!
  for ((i=0;i<1000;i++)); do
    if [[ -f $out/wire-monitor-ready ]]; then break; fi
    kill -0 "$origin_monitor"
    sleep .01
  done
  [[ -f $out/wire-monitor-ready ]]
  export ZUTCP_WIRE_GATE_DIR="$out"
}
read_wire_origins() {
  local wire_values
  wire_values=$(timeout 15 cat "$out/wire-origins")
  read -r client_base server_base <<< "$wire_values"
  [[ $client_base =~ ^[0-9]+$ && $server_base =~ ^[0-9]+$ ]]
  printf '%s %s\n' "$client_base" "$server_base" > "$out/wire-bases"
  wait "$origin_monitor"
  origin_monitor=
}
release_wire_gate() {
  timeout 10 bash -c 'printf R > "$1"' bash "$out/wire-release"
}
case $mode in
  native-history-client|native-history-server|abort-fatal-client|abort-fatal-server|fin-client|fin-server|lost-window)
    start_wire_gate ;;
esac
if [[ $mode == passive-tuple || $mode == paws-wrap ]]; then
  for side in client server; do
    dev=zutcpmlpa
    if [[ $side == server ]]; then dev=zutcpmlpb; fi
    tcpdump --immediate-mode -i "$dev" -n -s 0 -U -w "$out/$side-side.pcap" \
      'tcp port 18191 or tcp port 18195' > "$out/$side-side.capture" 2>&1 &
    if [[ $side == client ]]; then capture_cli=$!; else capture_srv=$!; fi
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/$side-side.capture"; then break; fi
      kill -0 "$!"
      sleep .01
    done
    grep -q 'listening on' "$out/$side-side.capture"
  done
  if [[ $mode == paws-wrap ]]; then
    env ZUTCP_PAWS_WRAP="$family" ./zutcpmxtest > "$out/native.out" 2> "$out/native.err" &
  else
    env ZUTCP_PASSIVE_TUPLE="$family" ZUTCP_PASSIVE_TUPLE_DIR="$out" \
      ./zutcpmxtest > "$out/native.out" 2> "$out/native.err" &
  fi
  child=$!
  wait "$child"
  child=
  sleep 1.1 # Capture drain only; route work uses explicit worker/Rx completion gates.
  stop_capture
  for side in client server; do
    grep -q '0 packets dropped by kernel' "$out/$side-side.capture"
  done
  prove --exec cat "$out/native.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py "$mode" "$out"
  exit 0
fi
if [[ $mode == positive-window ]]; then
  mkfifo "$out/reopen" "$out/reopen-second"
  for side in client server; do
    dev=zutcpmlpa
    if [[ $side == server ]]; then dev=zutcpmlpb; fi
    tcpdump --immediate-mode -i "$dev" -n -s 0 -U -w "$out/$side-side.pcap" \
      'tcp port 18191' > "$out/$side-side.capture" 2>&1 &
    if [[ $side == client ]]; then capture_cli=$!; else capture_srv=$!; fi
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/$side-side.capture"; then break; fi
      kill -0 "$!"
      sleep .01
    done
    grep -q 'listening on' "$out/$side-side.capture"
  done
  python3 ./zutcprecovery.py positive-window-probe "$out" zutcpmlpa &
  monitor=$!
  for ((i=0;i<1000;i++)); do
    if [[ -f $out/monitor-ready ]]; then break; fi
    kill -0 "$monitor"
    sleep .01
  done
  [[ -f $out/monitor-ready ]]
  env ZUTCP_POSITIVE_WINDOW="$family" ZUTCP_POSITIVE_WINDOW_DIR="$out" \
    ./zutcpmxtest > "$out/native.out" 2> "$out/native.err" &
  child=$!
  wait "$monitor"
  monitor=
  wait "$child"
  child=
  sleep 1.1 # libpcap drain after actual owner and callback completion gates.
  stop_capture
  for side in client server; do
    grep -q '0 packets dropped by kernel' "$out/$side-side.capture"
  done
  prove --exec cat "$out/native.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py positive-window "$out"
  exit 0
fi
if [[ $mode == native-zero-close || $mode == native-hub-stop ||
      $mode == native-error-client || $mode == native-error-server ]]; then
  role=()
  printf '%s\n' "$mode" > "$out/lifecycle-mode"
  abrupt=0 loss_dev=zutcpmlpa cli_offset=2
  if [[ $mode == native-hub-stop ]]; then role=(ZUTCP_NATIVE_HUB_STOP=1); fi
  if [[ $mode == native-error-client || $mode == native-error-server ]]; then
    role=(ZUTCP_NATIVE_NUMERIC=1)
    if [[ $mode == native-error-server ]]; then role+=(ZUTCP_NATIVE_SERVER_ERROR=1);
    else loss_dev=zutcpmlpb cli_offset=0; fi
  fi
  if [[ $mode != native-zero-close ]]; then
    abrupt=1
    mkfifo "$out/stop"
    protocol=ip iphead=20 protocol_offset=9
    if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
    tc qdisc add dev "$loss_dev" root handle 1: prio bands 3 \
      priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
    tc qdisc add dev "$loss_dev" parent 1:3 handle 30: netem loss 100%
    tc filter add dev "$loss_dev" parent 1: protocol "$protocol" pref 1 u32 \
      match u8 6 0xff at "$protocol_offset" \
      match u16 18192 0xffff at "$((iphead + cli_offset))" \
      match u8 0x08 0x0d at "$((iphead + 13))" flowid 1:3
  fi
  for side in client server; do
    dev=zutcpmlpa
    if [[ $side == server ]]; then dev=zutcpmlpb; fi
    tcpdump --immediate-mode -i "$dev" -n -s 0 -U -w "$out/$side-side.pcap" \
      'tcp portrange 18191-18195' > "$out/$side-side.capture" 2>&1 &
    if [[ $side == client ]]; then capture_cli=$!; else capture_srv=$!; fi
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/$side-side.capture"; then break; fi
      kill -0 "$!"
      sleep .01
    done
    grep -q 'listening on' "$out/$side-side.capture"
  done
  env ZUTCP_NATIVE_LIFECYCLE="$family" ZUTCP_NATIVE_LIFECYCLE_DIR="$out" "${role[@]}" \
    ./zutcpmxtest > "$out/native.out" 2> "$out/native.err" &
  child=$!
  if ((abrupt)); then
    dropped=0
    for ((i=0;i<3000;i++)); do
      stats=$(netemStats "$loss_dev")
      if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
      if ((dropped)); then break; fi
      kill -0 "$child" 2>/dev/null || break
      sleep .01
    done
    printf '%s\n' "$stats" > "$out/hub-stop.loss"
    ((dropped))
    timeout 10 bash -c 'printf R > "$1"' bash "$out/stop"
  fi
  wait "$child"
  child=
  if ((abrupt)); then tc qdisc del dev "$loss_dev" root; fi
  sleep 1.1 # libpcap drain after actual owner and callback completion gates.
  stop_capture
  for side in client server; do
    grep -q '0 packets dropped by kernel' "$out/$side-side.capture"
  done
  prove --exec cat "$out/native.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py native-lifecycle "$out"
  exit 0
fi
if [[ $mode == shared-active || $mode == shared-passive || $mode == conservative-tuple ]]; then
  role=()
  if [[ $mode == shared-passive ]]; then role=(ZUTCP_SHARED_PASSIVE=1); fi
  printf '%s\n' "$mode" > "$out/shared-mode"
  for side in client server; do
    dev=zutcpmlpa
    if [[ $side == server ]]; then dev=zutcpmlpb; fi
    tcpdump --immediate-mode -i "$dev" -n -s 0 -U -w "$out/$side-side.pcap" \
      'tcp portrange 18191-18199' > "$out/$side-side.capture" 2>&1 &
    if [[ $side == client ]]; then capture_cli=$!; else capture_srv=$!; fi
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/$side-side.capture"; then break; fi
      kill -0 "$!"
      sleep .01
    done
    grep -q 'listening on' "$out/$side-side.capture"
  done
  variable=ZUTCP_SHARED_ADMISSION
  if [[ $mode == conservative-tuple ]]; then variable=ZUTCP_CONSERVATIVE_TUPLE; fi
  env "$variable=$family" "${role[@]}" \
    ./zutcpmxtest > "$out/native.out" 2> "$out/native.err" &
  child=$!
  wait "$child"
  child=
  sleep 1.1 # libpcap drain after all callback/owner gates completed.
  stop_capture
  for side in client server; do
    grep -q '0 packets dropped by kernel' "$out/$side-side.capture"
  done
  prove --exec cat "$out/native.out" > "$out/tapcheck.log"
  if [[ $mode == conservative-tuple ]]; then
    grep -q 'client tuple remains in closed history' "$out/native.err"
    python3 ./zutcprecovery.py conservative-tuple "$out"
  else
    python3 ./zutcprecovery.py shared-admission "$out"
  fi
  exit 0
fi
if [[ $mode == native-history-client || $mode == native-history-server ]]; then
  role=()
  active=18192 dev=zutcpmlpb cli_offset=0
  if [[ $mode == native-history-server ]]; then
    role=(ZUTCP_NATIVE_SERVER_FIRST=1)
    active=18191 dev=zutcpmlpa cli_offset=2
  fi
  printf '%s\n' "$active" > "$out/native-active"
  mkfifo "$out/retired-gate"
  protocol=ip iphead=20 protocol_offset=9
  if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
  tc qdisc add dev "$dev" root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  tc qdisc add dev "$dev" parent 1:3 handle 30: netem loss 100%
  tcpdump --immediate-mode -i zutcpmlpa -n -s 0 -U -w "$out/client-side.pcap" \
    'tcp port 18191' > "$out/client-side.capture" 2>&1 &
  capture_cli=$!
  tcpdump --immediate-mode -i zutcpmlpb -n -s 0 -U -w "$out/server-side.pcap" \
    'tcp port 18191' > "$out/server-side.capture" 2>&1 &
  capture_srv=$!
  for side in client server; do
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/$side-side.capture"; then break; fi
      kill -0 "$capture_cli" && kill -0 "$capture_srv"
      sleep .01
    done
    grep -q 'listening on' "$out/$side-side.capture"
  done
  env ZUTCP_NATIVE_HISTORY="$family" ZUTCP_NATIVE_HISTORY_DIR="$out" "${role[@]}" \
    ./zutcpmxtest > "$out/native.out" 2> "$out/native.err" &
  child=$!
  read_wire_origins
  ack_base=$server_base
  if [[ $active == 18191 ]]; then ack_base=$client_base; fi
  wire_ack=$(((ack_base + 20) & 0xffffffff))
  tc filter add dev "$dev" parent 1: protocol "$protocol" pref 1 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u16 18192 0xffff at "$((iphead + cli_offset))" \
    match u8 0x10 0x1d at "$((iphead + 13))" \
    match u32 "$wire_ack" 0xffffffff at "$((iphead + 8))" flowid 1:3
  printf '%s %s %s 20\n' "$dev" "$wire_ack" "$ack_base" > "$out/wire-ack-selector"
  tc filter show dev "$dev" parent 1: > "$out/wire-ack-filter"
  release_wire_gate
  dropped=0
  for ((i=0;i<3000;i++)); do
    stats=$(netemStats "$dev")
    if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
    if ((dropped)); then break; fi
    kill -0 "$child" 2>/dev/null || break
    sleep .01
  done
  printf '%s\n' "$stats" > "$out/native-final-ack.loss"
  ((dropped))
  timeout 10 bash -c 'IFS= read -r -n1 token < "$1"; [[ $token == R ]]' \
    bash "$out/retired-gate"
  [[ -f $out/retired ]]
  tc qdisc del dev "$dev" root
  wait "$child"
  child=
  sleep 1.1 # libpcap drain only; fault release required native owner retirement and loss.
  stop_capture
  for side in client server; do
    grep -q '0 packets dropped by kernel' "$out/$side-side.capture"
  done
  prove --exec cat "$out/native.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py native-history "$out"
  exit 0
fi
if [[ $mode == abort-reject || $mode == abort-fatal-client || $mode == abort-fatal-server ]]; then
  role=()
  victim=18192 monitor_dev=zutcpmlpa source_dev=zutcpmlpb
  if [[ $mode == abort-reject || $mode == abort-fatal-server ]]; then
    role=(ZUTCP_ABORT_SERVER=1)
    victim=18191 monitor_dev=zutcpmlpb source_dev=zutcpmlpa
  fi
  if [[ $mode == abort-reject ]]; then
    role+=(ZUTCP_ABORT_REJECT=1)
  else
    mkfifo "$out/ready" "$out/injected"
    protocol=ip iphead=20 protocol_offset=9 cli_offset=0
    if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
    if [[ $victim == 18191 ]]; then cli_offset=2; fi
    tc qdisc add dev "$source_dev" root handle 1: prio bands 3 \
      priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
    tc qdisc add dev "$source_dev" parent 1:3 handle 30: netem loss 100%
    # Hold real sender feedback while the raw peer proves an accepted out-of-order
    # byte through standard SACK. Observe it before the opposite-port drop.
    # Only pure ACKs for the first tuple/data frontier match; RST and port18193 pass.
  fi
  printf '%s\n' "$victim" > "$out/abort-victim"
  printf '%s\n' "$mode" > "$out/abort-mode"
  tcpdump --immediate-mode -i zutcpmlpa -n -s 0 -U -w "$out/client-side.pcap" \
    'tcp port 18191' > "$out/client-side.capture" 2>&1 &
  capture_cli=$!
  tcpdump --immediate-mode -i zutcpmlpb -n -s 0 -U -w "$out/server-side.pcap" \
    'tcp port 18191' > "$out/server-side.capture" 2>&1 &
  capture_srv=$!
  for side in client server; do
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/$side-side.capture"; then break; fi
      kill -0 "$capture_cli" && kill -0 "$capture_srv"
      sleep .01
    done
    grep -q 'listening on' "$out/$side-side.capture"
  done
  if [[ $mode != abort-reject ]]; then
    timeout 25 python3 ./zutcprecovery.py abort-inject "$out" "$monitor_dev" &
    monitor=$!
    for ((i=0;i<1000;i++)); do
      if [[ -f $out/monitor-ready ]]; then break; fi
      kill -0 "$monitor"
      sleep .01
    done
    [[ -f $out/monitor-ready ]]
  fi
  env ZUTCP_MX_ABORT="$family" ZUTCP_ABORT_DIR="$out" "${role[@]}" \
    ./zutcpmxtest > "$out/mapped.out" 2> "$out/mapped.err" &
  child=$!
  if [[ $mode != abort-reject ]]; then
    read_wire_origins
    ack_base=$server_base
    if [[ $victim == 18191 ]]; then ack_base=$client_base; fi
    wire_ack=$(((ack_base + 19) & 0xffffffff))
    tc filter add dev "$source_dev" parent 1: protocol "$protocol" pref 1 u32 \
      match u8 6 0xff at "$protocol_offset" \
      match u16 18192 0xffff at "$((iphead + cli_offset))" \
      match u8 0x10 0x1d at "$((iphead + 13))" \
      match u32 "$wire_ack" 0xffffffff at "$((iphead + 8))" flowid 1:3
    printf '%s %s %s 19\n' "$source_dev" "$wire_ack" "$ack_base" > "$out/wire-ack-selector"
    tc filter show dev "$source_dev" parent 1: > "$out/wire-ack-filter"
    release_wire_gate
  fi
  if [[ -n $monitor ]]; then wait "$monitor"; monitor=; fi
  wait "$child"
  child=
  if [[ $mode != abort-reject ]]; then
    stats=$(netemStats "$source_dev")
    printf '%s\n' "$stats" > "$out/injected-ack.loss"
    [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]
    ((${BASH_REMATCH[1]} > 0))
  fi
  sleep 1.1 # libpcap drain only; actual callbacks and accepted OOO ACK gate the abort.
  stop_capture
  for side in client server; do
    grep -q '0 packets dropped by kernel' "$out/$side-side.capture"
  done
  prove --exec cat "$out/mapped.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py abort "$out"
  exit 0
fi
if [[ $mode == rst-client || $mode == rst-server ]]; then
  role=()
  dev=zutcpmlpb initiator=18192 cli_offset=0
  if [[ $mode == rst-server ]]; then
    role=(ZUTCP_RST_SERVER=1)
    dev=zutcpmlpa initiator=18191 cli_offset=2
  fi
  printf '%s\n' "$initiator" > "$out/rst-role"
  mkfifo "$out/close"
  protocol=ip iphead=20 protocol_offset=9
  if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
  tc qdisc add dev "$dev" root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  tc qdisc add dev "$dev" parent 1:3 handle 30: netem loss 100%
  # Drop only DATA on the first tuple; the RST and fresh port18193 pass normally.
  tc filter add dev "$dev" parent 1: protocol "$protocol" pref 1 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u16 18192 0xffff at "$((iphead + cli_offset))" \
    match u8 0x08 0x0d at "$((iphead + 13))" flowid 1:3
  tcpdump --immediate-mode -i zutcpmlpa -n -s 0 -U -w "$out/client-side.pcap" \
    'tcp port 18191' > "$out/client-side.capture" 2>&1 &
  capture_cli=$!
  tcpdump --immediate-mode -i zutcpmlpb -n -s 0 -U -w "$out/server-side.pcap" \
    'tcp port 18191' > "$out/server-side.capture" 2>&1 &
  capture_srv=$!
  for direction in client server; do
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/$direction-side.capture"; then break; fi
      kill -0 "$capture_cli" && kill -0 "$capture_srv"
      sleep .01
    done
    grep -q 'listening on' "$out/$direction-side.capture"
  done
  env ZUTCP_MX_RST="$family" ZUTCP_RST_DIR="$out" "${role[@]}" \
    ./zutcpmxtest > "$out/mapped.out" 2> "$out/mapped.err" &
  child=$!
  dropped=0
  for ((i=0;i<1500;i++)); do
    stats=$(netemStats "$dev")
    if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
    if ((dropped)); then break; fi
    kill -0 "$child" 2>/dev/null || break
    sleep .01
  done
  printf '%s\n' "$stats" > "$out/rst-data.loss"
  ((dropped))
  timeout 5 bash -c 'printf R > "$1"' bash "$out/close"
  wait "$child"
  child=
  sleep 1.1 # libpcap drain only; abrupt close was gated by the actual DATA drop.
  stop_capture
  for direction in client server; do
    grep -q '0 packets dropped by kernel' "$out/$direction-side.capture"
  done
  prove --exec cat "$out/mapped.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py rst "$out"
  exit 0
fi
if [[ $mode == fin-window-client || $mode == fin-window-server ]]; then
  role=()
  initiator=18192 monitor_dev=zutcpmlpa
  if [[ $mode == fin-window-server ]]; then
    role=(ZUTCP_FIN_SERVER=1)
    initiator=18191 monitor_dev=zutcpmlpb
  fi
  printf '%s\n' "$initiator" > "$out/fin-window-role"
  mkfifo "$out/reopen"
  tcpdump --immediate-mode -i zutcpmlpa -Q out -n -s 0 -U -w "$out/to-client.pcap" \
    'tcp port 18191' > "$out/to-client.capture" 2>&1 &
  capture_cli=$!
  tcpdump --immediate-mode -i zutcpmlpb -Q out -n -s 0 -U -w "$out/to-server.pcap" \
    'tcp port 18191' > "$out/to-server.capture" 2>&1 &
  capture_srv=$!
  for direction in client server; do
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/to-$direction.capture"; then break; fi
      kill -0 "$capture_cli" && kill -0 "$capture_srv"
      sleep .01
    done
    grep -q 'listening on' "$out/to-$direction.capture"
  done
  python3 ./zutcprecovery.py fin-window-probe "$out" "$monitor_dev" &
  monitor=$!
  for ((i=0;i<1000;i++)); do
    if [[ -f $out/monitor-ready ]]; then break; fi
    kill -0 "$monitor"
    sleep .01
  done
  [[ -f $out/monitor-ready ]]
  env ZUTCP_MX_FIN_WINDOW="$family" ZUTCP_FIN_DIR="$out" "${role[@]}" \
    ./zutcpmxtest > "$out/mapped.out" 2> "$out/mapped.err" &
  child=$!
  wait "$monitor"
  monitor=
  wait "$child"
  child=
  sleep 1.1 # libpcap drain only; correctness is gated by the actual persist packet.
  stop_capture
  for direction in client server; do
    grep -q '0 packets dropped by kernel' "$out/to-$direction.capture"
  done
  prove --exec cat "$out/mapped.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py fin-window "$out"
  exit 0
fi
if [[ $mode == fin-client || $mode == fin-server ]]; then
  dev=zutcpmlpb
  role=()
  if [[ $mode == fin-server ]]; then dev=zutcpmlpa; role=(ZUTCP_FIN_SERVER=1); fi
  protocol=ip iphead=20 protocol_offset=9
  if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
  tc qdisc add dev "$dev" root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  tc qdisc add dev "$dev" parent 1:3 handle 30: netem loss 100%
  # The fixed graceful message has 19 bytes; FIN advances its ACK frontier to 20.
  # Match pure ACK, allowing FIN+ACK through to retire the active closer.
  tcpdump --immediate-mode -i "$dev" -Q out -n -s 0 -U -w "$out/final-ack.pcap" \
    'tcp port 18191' > "$out/final-ack.capture" 2>&1 &
  capture_cli=$!
  fin_dev=zutcpmlpa
  if [[ $mode == fin-server ]]; then fin_dev=zutcpmlpb; fi
  tcpdump --immediate-mode -i "$fin_dev" -Q out -n -s 0 -U -w "$out/peer-fin.pcap" \
    'tcp port 18191' > "$out/peer-fin.capture" 2>&1 &
  capture_srv=$!
  for ((i=0;i<1000;i++)); do
    if grep -q 'listening on' "$out/peer-fin.capture"; then break; fi
    kill -0 "$capture_srv"
    sleep .01
  done
  grep -q 'listening on' "$out/peer-fin.capture"
  for ((i=0;i<1000;i++)); do
    if grep -q 'listening on' "$out/final-ack.capture"; then break; fi
    kill -0 "$capture_cli"
    sleep .01
  done
  grep -q 'listening on' "$out/final-ack.capture"
  env ZUTCP_MX_FIN="$family" ZUTCP_FIN_DIR="$out" "${role[@]}" \
    ./zutcpmxtest > "$out/mapped.out" 2> "$out/mapped.err" &
  child=$!
  read_wire_origins
  ack_base=$server_base
  if [[ $mode == fin-server ]]; then ack_base=$client_base; fi
  wire_ack=$(((ack_base + 20) & 0xffffffff))
  tc filter add dev "$dev" parent 1: protocol "$protocol" pref 1 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u8 0x10 0x19 at "$((iphead + 13))" \
    match u32 "$wire_ack" 0xffffffff at "$((iphead + 8))" flowid 1:3
  printf '%s %s %s 20\n' "$dev" "$wire_ack" "$ack_base" > "$out/wire-ack-selector"
  tc filter show dev "$dev" parent 1: > "$out/wire-ack-filter"
  release_wire_gate
  dropped=0
  for ((i=0;i<3000;i++)); do
    stats=$(netemStats "$dev")
    if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
    if ((dropped)) && [[ -f $out/retired ]]; then break; fi
    kill -0 "$child" 2>/dev/null || break
    sleep .01
  done
  printf '%s\n' "$stats" > "$out/final-ack.loss"
  ((dropped))
  [[ -f $out/retired ]]
  tc qdisc del dev "$dev" root
  wait "$child"
  child=
  sleep 1.1
  stop_capture
  grep -q '0 packets dropped by kernel' "$out/final-ack.capture"
  prove --exec cat "$out/mapped.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py final-ack "$out"
  exit 0
fi
if [[ $mode == window || $mode == restart || $mode == lost-window ]]; then
  if [[ $mode == lost-window ]]; then
    protocol=ip iphead=20 protocol_offset=9
    if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6; fi
    for dev in zutcpmlpa zutcpmlpb; do
      tc qdisc add dev "$dev" root handle 1: prio bands 3 \
        priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
      tc qdisc add dev "$dev" parent 1:3 handle 30: netem loss 100%
    done
  fi
  tcpdump --immediate-mode -i zutcpmlpa -Q out -n -s 0 -U -w "$out/to-client.pcap" \
    'tcp port 18191' > "$out/to-client.capture" 2>&1 &
  capture_cli=$!
  tcpdump --immediate-mode -i zutcpmlpb -Q out -n -s 0 -U -w "$out/to-server.pcap" \
    'tcp port 18191' > "$out/to-server.capture" 2>&1 &
  capture_srv=$!
  for direction in client server; do
    for ((i=0;i<1000;i++)); do
      if grep -q 'listening on' "$out/to-$direction.capture"; then break; fi
      kill -0 "$capture_cli" && kill -0 "$capture_srv"
      sleep .01
    done
    grep -q 'listening on' "$out/to-$direction.capture"
  done
  variable=ZUTCP_MX_WINDOW
  if [[ $mode == restart ]]; then variable=ZUTCP_MX_RESTART; fi
  env "$variable=$family" ./zutcpmxtest > "$out/mapped.out" 2> "$out/mapped.err" &
  child=$!
  if [[ $mode == lost-window ]]; then
    read_wire_origins
    for dev in zutcpmlpa zutcpmlpb; do
      ack_base=$client_base
      if [[ $dev == zutcpmlpb ]]; then ack_base=$server_base; fi
      wire_ack=$(((ack_base + 1024) & 0xffffffff))
      tc filter add dev "$dev" parent 1: protocol "$protocol" pref 1 u32 \
        match u8 6 0xff at "$protocol_offset" \
        match u8 0x10 0x19 at "$((iphead + 13))" \
        match u32 "$wire_ack" 0xffffffff at "$((iphead + 8))" \
        match u16 1024 0xffff at "$((iphead + 14))" flowid 1:3
      printf '%s %s %s 1024\n' "$dev" "$wire_ack" "$ack_base" >> "$out/wire-ack-selector"
      tc filter show dev "$dev" parent 1: > "$out/$dev.wire-ack-filter"
    done
    release_wire_gate
  fi
  if [[ $mode == lost-window ]]; then
    for dev in zutcpmlpa zutcpmlpb; do
      dropped=0
      for ((i=0;i<1500;i++)); do
        stats=$(netemStats "$dev")
        if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
        if ((dropped)); then break; fi
        kill -0 "$child" 2>/dev/null || break
        sleep .01
      done
      printf '%s\n' "$stats" > "$out/$dev.opening-loss"
      printf '%s\n' "$dropped" > "$out/$dev.opening-drops"
      ((dropped))
      tc qdisc del dev "$dev" root
    done
  fi
  wait "$child"
  child=
  sleep 1.1 # final libpcap drain on the fixture control caller
  stop_capture
  prove --exec cat "$out/mapped.out" > "$out/tapcheck.log"
  python3 ./zutcprecovery.py "$mode" "$out"
  exit 0
fi
[[ $mode == loss ]]
protocol=ip short_offset=40 length_offset=2 protocol_offset=9
if [[ $family == 6 ]]; then
  protocol=ipv6 short_offset=60 length_offset=4 protocol_offset=6
fi
# AF_XDP direct Tx bypasses the raw NIC's qdisc. Drop established DATA at
# these middle bridge ports; exclude Initial and small ACK/control packets.
for dev in zutcpmlpa zutcpmlpb; do
  tc qdisc add dev "$dev" root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  tc qdisc add dev "$dev" parent 1:3 handle 30: netem loss 100%
  tc filter add dev "$dev" parent 1: protocol "$protocol" pref 1 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u8 0x08 0x0e at "$((short_offset - 7))" \
    match u8 0x04 0xfc at "$length_offset" flowid 1:3
done
env ZUTCP_MX_LOSS="$family" ./zutcpmxtest > "$out/mapped.out" 2> "$out/mapped.err" &
child=$!

for dev in zutcpmlpa zutcpmlpb; do
  dropped=0
  for ((i=0;i<3000;i++)); do
    stats=$(netemStats "$dev")
    if [[ $stats =~ dropped[[:space:]]+([0-9]+) ]]; then dropped=${BASH_REMATCH[1]}; fi
    if ((dropped)); then break; fi
    kill -0 "$child" 2>/dev/null || break
    sleep .01
  done
  printf '%s\n' "$stats" > "$out/$dev.loss"
  ((dropped))
  tc qdisc del dev "$dev" root
done
wait "$child"
child=
prove --exec cat "$out/mapped.out" > "$out/tapcheck.log"
