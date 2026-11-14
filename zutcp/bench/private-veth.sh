#!/usr/bin/env bash
# Called by run-compare.sh inside its fresh network namespace.
set -euo pipefail
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
[[ $# == 16 ]]
binary=$1 backend=$2 family=$3 out=$4
crx=$5 ctx=$6 srx=$7 stx=$8
messages=$9 warmup=${10} pipeline=${11} connections=${12}
reserve=${13} spin=${14} label=${15} capture=${16}
client_backend=$backend server_backend=$backend matched=1
case $backend in
  kernel|native) ;;
  native-client) client_backend=native server_backend=kernel matched=0 ;;
  native-server) client_backend=kernel server_backend=native matched=0 ;;
  *) exit 2 ;;
esac
if [[ $label == check ]]; then matched=0; fi
[[ $family == 4 || $family == 6 ]]
[[ $label == check || $label == release || $label == pg ]]
[[ $capture == 0 || $capture == 1 ]]
topology=${BENCH_TOPOLOGY:-direct}
[[ $topology == direct || $topology == bridge ]]
recovery=${BENCH_RECOVERY:-none}
case $recovery in none|loss|reorder) ;; *) exit 2 ;; esac
if [[ $recovery != none ]]; then
  [[ $topology == bridge && $capture == 1 && $connections == 1 && $warmup == 0 ]]
  [[ $pipeline =~ ^[0-9]+$ && $messages =~ ^[0-9]+$ ]]
  ((10#$pipeline >= 16 && 10#$messages >= 16))
fi
script=$(cd "$(dirname "$0")" && pwd)
[[ ! -e $out && ! -L $out ]]
mkdir -p "$out/client" "$out/server"
out=$(realpath "$out")
cli=zbench-c-$$ srv=zbench-s-$$
cli_made=0 srv_made=0 bridge_made=0 cli_link_made=0 srv_link_made=0
client_pid= server_pid= fault_pid=
capture_pids=()
stopCapture() {
  for pid in "${capture_pids[@]}"; do
    kill -INT "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  capture_pids=()
}
cleanup() {
  stopCapture
  for child in "$client_pid" "$server_pid" "$fault_pid"; do
    if [[ -n $child ]]; then
      kill "$child" 2>/dev/null || true
      wait "$child" 2>/dev/null || true
    fi
  done
  if ((cli_link_made)); then ip link del zbenchca 2>/dev/null || true; fi
  if ((srv_link_made)); then ip link del zbenchsa 2>/dev/null || true; fi
  if ((bridge_made)); then ip link del zbenchbr 2>/dev/null || true; fi
  if ((cli_made)); then ip netns del "$cli"; fi
  if ((srv_made)); then ip netns del "$srv"; fi
}
trap cleanup EXIT
trap 'exit 1' INT TERM
trap 'printf "benchmark fixture failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
ip netns add "$cli"; cli_made=1
ip netns add "$srv"; srv_made=1
if [[ $topology == bridge ]]; then
  ip link add zbenchbr type bridge
  bridge_made=1
  ip link add zbenchca numrxqueues 1 numtxqueues 1 type veth \
    peer name zbenchc numrxqueues 1 numtxqueues 1
  cli_link_made=1
  ip link add zbenchsa numrxqueues 1 numtxqueues 1 type veth \
    peer name zbenchs numrxqueues 1 numtxqueues 1
  srv_link_made=1
  ip link set zbenchca master zbenchbr
  ip link set zbenchsa master zbenchbr
  ip link set zbenchca up
  ip link set zbenchsa up
  ip link set zbenchbr up
else
  ip link add zbenchc numrxqueues 1 numtxqueues 1 type veth \
    peer name zbenchs numrxqueues 1 numtxqueues 1
fi
ip link set zbenchc netns "$cli"
ip link set zbenchs netns "$srv"
for ns in "$cli" "$srv"; do ip -n "$ns" link set lo up; done
ip -n "$cli" link set zbenchc up
ip -n "$srv" link set zbenchs up
ip -n "$cli" addr add 198.18.87.1/32 dev zbenchc
ip -n "$srv" addr add 198.18.87.2/32 dev zbenchs
ip -n "$cli" -6 addr add 2001:db8:87::1/128 dev zbenchc nodad
ip -n "$srv" -6 addr add 2001:db8:87::2/128 dev zbenchs nodad
cmac=$(ip -n "$cli" -o link show zbenchc | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
smac=$(ip -n "$srv" -o link show zbenchs | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip -n "$cli" route add 198.18.87.2/32 dev zbenchc src 198.18.87.1
ip -n "$srv" route add 198.18.87.1/32 dev zbenchs src 198.18.87.2
ip -n "$cli" -6 route add 2001:db8:87::2/128 dev zbenchc src 2001:db8:87::1
ip -n "$srv" -6 route add 2001:db8:87::1/128 dev zbenchs src 2001:db8:87::2
ip -n "$cli" neigh replace 198.18.87.2 lladdr "$smac" nud permanent dev zbenchc
ip -n "$srv" neigh replace 198.18.87.1 lladdr "$cmac" nud permanent dev zbenchs
ip -n "$cli" -6 neigh replace 2001:db8:87::2 lladdr "$smac" nud permanent dev zbenchc
ip -n "$srv" -6 neigh replace 2001:db8:87::1 lladdr "$cmac" nud permanent dev zbenchs
snapshot() {
  local phase=$1
  ip -n "$cli" -s -s link show zbenchc > "$out/client/link.$phase"
  ip -n "$srv" -s -s link show zbenchs > "$out/server/link.$phase"
  if [[ $topology == bridge ]]; then
    ip -s -s link show zbenchca > "$out/bridge-client.link.$phase"
    ip -s -s link show zbenchsa > "$out/bridge-server.link.$phase"
  fi
  for side in client server; do
    local ns=$cli dev=zbenchc
    if [[ $side == server ]]; then ns=$srv dev=zbenchs; fi
    ip netns exec "$ns" ethtool -S "$dev" > "$out/$side/device.$phase"
    ip netns exec "$ns" nstat -az > "$out/$side/netstat.$phase"
  done
  if [[ $recovery != none ]]; then
    python3 -c 'import time; print(time.time())' > "$out/counter-snapshot.$phase"
  fi
}
for side in client server; do
  ns=$cli dev=zbenchc
  if [[ $side == server ]]; then ns=$srv dev=zbenchs; fi
  ip netns exec "$ns" ethtool -K "$dev" tx off tso off gso off gro off >/dev/null
  ip netns exec "$ns" ethtool -k "$dev" > "$out/$side/offload"
  ip netns exec "$ns" ethtool -l "$dev" > "$out/$side/channels"
  ip netns exec "$ns" sysctl net.ipv4.tcp_sack net.ipv4.tcp_timestamps \
    net.ipv4.tcp_window_scaling net.ipv4.tcp_ecn > "$out/$side/kernel-options"
done
if [[ $topology == bridge ]]; then
  for dev in zbenchca zbenchsa; do
    ethtool -K "$dev" tx off tso off gso off gro off >/dev/null
    ethtool -k "$dev" > "$out/$dev.offload"
    ethtool -l "$dev" > "$out/$dev.channels"
  done
fi
uname -a > "$out/kernel.version"
sha256sum "$binary" > "$out/binary.sha256"
object=$(dirname "$binary")/.libs/$(basename "$binary")
if [[ -f $object ]]; then sha256sum "$object" >> "$out/binary.sha256"; fi
printf '%s\n' "backend=$backend" "family=$family" "build_label=$label" "topology=$topology" \
  "client_backend=$client_backend" "server_backend=$server_backend" \
  "performance_comparison=$matched" \
  "capture=$capture" "messages=$messages" "warmup=$warmup" "pipeline=$pipeline" \
  "connections=$connections" "reserve_requested=$reserve" "spin=$spin" \
  "workload=${BENCH_WORKLOAD:-order}" "burst_size=${BENCH_BURST_SIZE:-0}" \
  "recovery=$recovery" \
  "burst_interval_us=${BENCH_BURST_US:-1000}" \
  "client_cpus=$crx,$ctx" "server_cpus=$srx,$stx" > "$out/settings"
snapshot before
if [[ $capture == 1 ]]; then
  if [[ $topology == bridge ]]; then
    printf '%s\n' 'capture_topology=bridge' >> "$out/settings"
    for side in client server; do
      dev=zbenchca
      if [[ $side == server ]]; then dev=zbenchsa; fi
      tcpdump --immediate-mode -B 8192 -i "$dev" -Q out -n -s 0 -U \
        -w "$out/to-$side.pcap" 'tcp port 18187' > "$out/capture-$side.log" 2>&1 &
      capture_pids+=("$!")
      for ((i=0;i<100;i++)); do
        if grep -q 'listening on' "$out/capture-$side.log"; then break; fi
        kill -0 "${capture_pids[-1]}"; sleep .01
      done
      grep -q 'listening on' "$out/capture-$side.log"
    done
  else
    # Endpoint capture is diagnostic; native/native may bypass its AF_PACKET tap.
    capture_ns=$cli capture_dev=zbenchc capture_side=client
    if [[ $client_backend == native && $server_backend == kernel ]]; then
      capture_ns=$srv capture_dev=zbenchs capture_side=server
    fi
    printf '%s\n' 'capture_topology=endpoint' "capture_endpoint=$capture_side" >> "$out/settings"
    ip netns exec "$capture_ns" tcpdump --immediate-mode -B 8192 -i "$capture_dev" -n -s 0 -U \
      -w "$out/diagnostic.pcap" 'tcp port 18187' > "$out/capture.log" 2>&1 &
    capture_pids+=("$!")
    for ((i=0;i<100;i++)); do
      if grep -q 'listening on' "$out/capture.log"; then break; fi
      kill -0 "${capture_pids[-1]}"; sleep .01
    done
    grep -q 'listening on' "$out/capture.log"
  fi
fi
if [[ $recovery != none ]]; then
  for source in source control-source; do
    dev=zbenchca
    if [[ $source == control-source ]]; then dev=zbenchsa; fi
    tcpdump --immediate-mode -B 8192 -i "$dev" -Q in -n -s 0 -U \
      -w "$out/fault-$source.pcap" 'tcp port 18187' > "$out/fault-$source.log" 2>&1 &
    capture_pids+=("$!")
    for ((i=0;i<100;i++)); do
      if grep -q 'listening on' "$out/fault-$source.log"; then break; fi
      kill -0 "${capture_pids[-1]}"; sleep .01
    done
    grep -q 'listening on' "$out/fault-$source.log"
  done
  mkfifo "$out/fault-origins" "$out/fault-release" "$out/fault-done"
  protocol=ip iphead=20 protocol_offset=9 length_offset=2
  if [[ $family == 6 ]]; then protocol=ipv6 iphead=40 protocol_offset=6 length_offset=4; fi
  tc qdisc add dev zbenchca root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  tc qdisc add dev zbenchca parent 1:3 handle 30: netem loss 100%
  # Hold an actual SYNACK, not application production or a synthetic peer message.
  tc filter add dev zbenchca parent 1: protocol "$protocol" pref 1 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u16 18187 0xffff at "$iphead" \
    match u8 0x12 0x17 at "$((iphead + 13))" flowid 1:3
  python3 -B "$script/bench-recovery.py" monitor "$out" "$recovery" \
    > "$out/fault-monitor.out" 2> "$out/fault-monitor.err" &
  fault_pid=$!
  for ((i=0;i<1000;i++)); do
    if [[ -f $out/fault-monitor-ready ]]; then break; fi
    kill -0 "$fault_pid"; sleep .01
  done
  [[ -f $out/fault-monitor-ready ]]
fi

address=198.18.87.2
if [[ $family == 6 ]]; then address=2001:db8:87::2; fi
# cd gives each role its own ordinary gmon.out; GMON_OUT_PREFIX also separates PIDs
# on runtimes which implement it. No clock or transport behavior is changed.
ip netns exec "$srv" sh -c 'cd "$1"; shift; exec "$@"' sh "$out/server" \
  env "GMON_OUT_PREFIX=$out/server/gmon" /usr/bin/time -v -o "$out/server/time" \
  timeout 150 "$binary" server "$server_backend" zbenchs "$address" "$messages" "$warmup" \
  "$pipeline" "$connections" "$reserve" "$srx" "$stx" "$spin" \
  > "$out/server/output" 2> "$out/server/errors" &
server_pid=$!
for ((i=0;i<1000;i++)); do
  if grep -q '^ready=1$' "$out/server/output"; then break; fi
  kill -0 "$server_pid"; sleep .01
done
grep -q '^ready=1$' "$out/server/output"
ip netns exec "$cli" sh -c 'cd "$1"; shift; exec "$@"' sh "$out/client" \
  env "GMON_OUT_PREFIX=$out/client/gmon" /usr/bin/time -v -o "$out/client/time" \
  timeout 150 "$binary" client "$client_backend" zbenchc "$address" "$messages" "$warmup" \
  "$pipeline" "$connections" "$reserve" "$crx" "$ctx" "$spin" "$out/client/samples.csv" \
  > "$out/client/output" 2> "$out/client/errors" &
client_pid=$!
if [[ $recovery != none ]]; then
  read -r client_base observed_family < <(timeout 15 cat "$out/fault-origins")
  [[ $client_base =~ ^[0-9]+$ && $observed_family == "$family" ]]
  tc qdisc add dev zbenchsa root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  if [[ $recovery == loss ]]; then
    tc qdisc add dev zbenchsa parent 1:3 handle 30: netem loss 100%
  else
    tc qdisc add dev zbenchsa parent 1:3 handle 30: netem delay 20ms
  fi
  # TCP controls are below128 IP bytes even with maximal standard options.
  # Every selected FIX request is >=137 payload bytes. No PSH assumption needed.
  tc filter add dev zbenchsa parent 1: protocol "$protocol" pref 10 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u16 0 0xff80 at "$length_offset" flowid 1:1
  tc filter add dev zbenchsa parent 1: protocol "$protocol" pref 20 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u16 18187 0xffff at "$((iphead + 2))" \
    match u8 0x10 0x17 at "$((iphead + 13))" \
    match u32 "$client_base" 0xffffffff at "$((iphead + 4))" flowid 1:3
  tc -s filter show dev zbenchsa parent 1: > "$out/fault-data-filter"
  held=0
  for ((i=0;i<1000;i++)); do
    tc -s qdisc show dev zbenchca > "$out/fault-control-qdisc"
    if python3 -c 'import re,sys; sys.exit(not any(int(n)>0 for n in
      re.findall(r"dropped (\d+)",open(sys.argv[1]).read())))' "$out/fault-control-qdisc"; then
      held=1; break
    fi
    kill -0 "$fault_pid"; sleep .01
  done
  ((held))
  python3 -c 'import time; print(time.time())' > "$out/fault-control-open-time"
  tc filter del dev zbenchca parent 1: protocol "$protocol" pref 1 u32
  timeout 10 bash -c 'printf "R\n" > "$1"' bash "$out/fault-release"
  if [[ $recovery == loss ]]; then
    lost=0
    for ((i=0;i<1000;i++)); do
      tc -s qdisc show dev zbenchsa > "$out/fault-data-initial-qdisc"
      if python3 -c 'import re,sys; sys.exit(not any(int(n)>0 for n in
        re.findall(r"dropped (\d+)",open(sys.argv[1]).read())))' \
          "$out/fault-data-initial-qdisc"; then lost=1; break; fi
      kill -0 "$fault_pid"; sleep .01
    done
    ((lost))
    python3 -c 'import time; print(time.time())' > "$out/fault-filter-release-time"
    tc filter del dev zbenchsa parent 1: protocol "$protocol" pref 20 u32
  fi
  timeout 30 cat "$out/fault-done" > "$out/fault-done-result"
  wait "$fault_pid"; fault_pid=
  if [[ $recovery == reorder ]]; then
    tc filter del dev zbenchsa parent 1: protocol "$protocol" pref 20 u32
  fi
fi
wait "$client_pid"; client_pid=
wait "$server_pid"; server_pid=
if [[ $recovery != none ]]; then
  drained=0
  for ((i=0;i<500;i++)); do
    tc -s qdisc show dev zbenchsa > "$out/fault-data-qdisc"
    if awk '/backlog/ {seen=1; if ($3 != "0p") busy=1} END {exit (!seen || busy)}' \
        "$out/fault-data-qdisc"; then drained=1; break; fi
    sleep .01
  done
  ((drained))
  # Allow immediate CLOSED-host replies to the last queued original into both taps.
  sleep .05
fi
snapshot after
stopCapture
for side in client server; do
  expected=$client_backend
  if [[ $side == server ]]; then expected=$server_backend; fi
  grep -q "^result role=$side backend=$expected exact=1 " "$out/$side/output"
done
printf '%s\n' "observation=complete topology=$topology build=$label backend=$backend" \
  "performance_comparison=$matched" > "$out/complete"

if [[ $recovery != none ]]; then
  python3 -B "$script/bench-recovery.py" check "$out"
fi
