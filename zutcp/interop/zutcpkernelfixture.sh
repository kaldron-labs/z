#!/usr/bin/env bash
set -euo pipefail
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
family=$1
direction=$2
out=$3
profile=${4:-baseline}
[[ $profile == baseline || $profile == absent || $profile == loss || \
   $profile == window || $profile == repeat || $profile == reset || \
   $profile == tfo || $profile == tfo-fallback || $profile == reorder ]]
[[ $family == 4 || $family == 6 ]]
[[ $direction == native-client || $direction == kernel-client ]]
mkdir -p "$out"
root=$(cd "$(dirname "$0")/../.." && pwd)
cli=zutcp-kcli-$$
srv=zutcp-ksrv-$$
cli_made=0 srv_made=0 bridge_made=0 cli_link_made=0 srv_link_made=0
client_pid= server_pid= capture_cli= capture_srv= capture_source= observer_pid=
stopCapture() {
  for capture in "$capture_cli" "$capture_srv" "$capture_source"; do
    if [[ -n $capture ]]; then
      kill -INT "$capture" 2>/dev/null || true
      wait "$capture" 2>/dev/null || true
    fi
  done
  capture_cli= capture_srv= capture_source=
}
cleanup() {
  stopCapture
  for child in "$client_pid" "$server_pid" "$observer_pid"; do
    if [[ -n $child ]]; then
      kill "$child" 2>/dev/null || true
      wait "$child" 2>/dev/null || true
    fi
  done
  if ((cli_link_made)); then ip link del ztcpkca 2>/dev/null || true; fi
  if ((srv_link_made)); then ip link del ztcpksa 2>/dev/null || true; fi
  if ((cli_made)); then ip netns del "$cli" 2>/dev/null || true; fi
  if ((srv_made)); then ip netns del "$srv" 2>/dev/null || true; fi
  if ((bridge_made)); then ip link del ztcpkbr 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'printf "kernel fixture failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
trap 'exit 1' INT TERM
ip netns add "$cli"
cli_made=1
ip netns add "$srv"
srv_made=1
ip link add ztcpkbr type bridge
bridge_made=1
ip link add ztcpkca numrxqueues 1 numtxqueues 1 type veth peer name ztcpkc
cli_link_made=1
ip link add ztcpksa numrxqueues 1 numtxqueues 1 type veth peer name ztcpks
srv_link_made=1
ip link set ztcpkc netns "$cli"
ip link set ztcpks netns "$srv"
ip link set ztcpkca master ztcpkbr
ip link set ztcpksa master ztcpkbr
ip link set ztcpkca up
ip link set ztcpksa up
ip link set ztcpkbr up
ip -n "$cli" link set lo up
ip -n "$srv" link set lo up
ip -n "$cli" link set ztcpkc up
ip -n "$srv" link set ztcpks up
ip -n "$cli" addr add 198.18.8.1/32 dev ztcpkc
ip -n "$srv" addr add 198.18.8.2/32 dev ztcpks
ip -n "$cli" -6 addr add 2001:db8:8::1/128 dev ztcpkc nodad
ip -n "$srv" -6 addr add 2001:db8:8::2/128 dev ztcpks nodad
cli_mac=$(ip -n "$cli" -o link show ztcpkc | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
srv_mac=$(ip -n "$srv" -o link show ztcpks | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip -n "$cli" route add 198.18.8.2/32 dev ztcpkc src 198.18.8.1
ip -n "$srv" route add 198.18.8.1/32 dev ztcpks src 198.18.8.2
ip -n "$cli" -6 route add 2001:db8:8::2/128 dev ztcpkc src 2001:db8:8::1
ip -n "$srv" -6 route add 2001:db8:8::1/128 dev ztcpks src 2001:db8:8::2
ip -n "$cli" neigh replace 198.18.8.2 lladdr "$srv_mac" nud permanent dev ztcpkc
ip -n "$srv" neigh replace 198.18.8.1 lladdr "$cli_mac" nud permanent dev ztcpks
ip -n "$cli" -6 neigh replace 2001:db8:8::2 lladdr "$srv_mac" nud permanent dev ztcpkc
ip -n "$srv" -6 neigh replace 2001:db8:8::1 lladdr "$cli_mac" nud permanent dev ztcpks
ip netns exec "$cli" ethtool -K ztcpkc tx off tso off gso off gro off >/dev/null
ip netns exec "$srv" ethtool -K ztcpks tx off tso off gso off gro off >/dev/null
ip netns exec "$cli" ethtool -k ztcpkc > "$out/client.offload"
ip netns exec "$srv" ethtool -k ztcpks > "$out/server.offload"
server=198.18.8.2
if [[ $family == 6 ]]; then server=2001:db8:8::2; fi
python3 - "$out" "$profile" <<'PY'
from pathlib import Path
import sys
out = Path(sys.argv[1])
request = bytes(range(256)) * (128 if sys.argv[2] == 'window' else 32)
(out / 'request').write_bytes(request)
copies = 2 if sys.argv[2] in ('repeat', 'tfo', 'tfo-fallback') else 1
(out / 'expected').write_bytes((b'echo: ' + request) * copies)
PY
server_command=("$root/zutcp/util/zutcpkernelpeer" server "$server" 18181 1)
client_command=("$root/zutcp/util/zutcppair" client ztcpkc "$server" 18181 "$out/request")
if [[ $direction == kernel-client ]]; then
  server_command=("$root/zutcp/util/zutcppair" server ztcpks "$server" 18181 1)
  client_command=("$root/zutcp/util/zutcpkernelpeer" client "$server" 18181 "$out/request")
fi
if [[ $profile == repeat || $profile == reset || $profile == tfo || \
      $profile == tfo-fallback ]]; then
  server_command[${#server_command[@]}-1]=2
  client_command+=(2)
fi
printf '%s\n' "direction=$direction" "family=$family" "profile=$profile" > "$out/roles"
uname -a > "$out/kernel.version"
kernel_namespace=$srv
if [[ $direction == kernel-client ]]; then kernel_namespace=$cli; fi
server_environment=() client_environment=()
if [[ $profile == tfo || $profile == tfo-fallback ]]; then
  ip netns exec "$kernel_namespace" sysctl -qw net.ipv4.tcp_fastopen=3
  server_environment+=(ZUTCP_KERNEL_TFO=1)
  client_environment+=(ZUTCP_KERNEL_TFO=1)
  if [[ $profile == tfo-fallback ]]; then
    if [[ $direction == native-client ]]; then client_environment+=(ZUTCP_KERNEL_TFO_DENY=1)
    else server_environment+=(ZUTCP_KERNEL_TFO_DENY=1); fi
  fi
fi
if [[ $profile == absent ]]; then
  ip netns exec "$kernel_namespace" sysctl -qw net.ipv4.tcp_sack=0 \
    net.ipv4.tcp_window_scaling=0 net.ipv4.tcp_timestamps=0 net.ipv4.tcp_ecn=0
  server_environment+=(ZUTCP_PEER_OPTIONS=0)
  client_environment+=(ZUTCP_PEER_OPTIONS=0)
fi
if [[ $profile == reset ]]; then
  mkfifo "$out/reset-ready" "$out/reset-gate"
  server_environment+=(ZUTCP_KERNEL_EXPECT_RESET=1)
  client_environment+=("ZUTCP_KERNEL_RESET_GATE=$out/reset-gate")
fi
if [[ $profile == loss || $profile == reorder ]]; then
  mkfifo "$out/origins" "$out/start-gate"
  if [[ $profile == reorder ]]; then mkfifo "$out/reorder-ready"; fi
  client_environment+=("ZUTCP_KERNEL_START_GATE=$out/start-gate")
fi
if [[ $profile == window ]]; then
  mkfifo "$out/window-ready" "$out/read-gate"
  server_environment+=("ZUTCP_KERNEL_READ_GATE=$out/read-gate")
  if [[ $direction == native-client ]]; then
    client_environment+=(ZUTCP_KERNEL_WINDOW_CLIENT=1)
  fi
fi
ip netns exec "$kernel_namespace" sysctl net.ipv4.tcp_sack net.ipv4.tcp_window_scaling \
  net.ipv4.tcp_timestamps net.ipv4.tcp_ecn net.ipv4.tcp_fastopen > "$out/kernel.options"
ip netns exec "$srv" env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  "${server_environment[@]}" timeout 40 "${server_command[@]}" \
  > "$out/server.out" 2> "$out/server.err" &
server_pid=$!
for ((i=0;i<1000;i++)); do
  if grep -q 'role=server ready=1' "$out/server.err"; then break; fi
  kill -0 "$server_pid"
  sleep .01
done
grep -q 'role=server ready=1' "$out/server.err"
for side in client server; do
  device=ztcpkca
  if [[ $side == server ]]; then device=ztcpksa; fi
  tcpdump --immediate-mode -B 8192 -i "$device" -Q out -n -s 0 -U \
    -w "$out/to-$side.pcap" 'tcp port 18181' > "$out/to-$side.capture" 2>&1 &
  if [[ $side == client ]]; then capture_cli=$!; else capture_srv=$!; fi
  for ((i=0;i<100;i++)); do
    if grep -q 'listening on' "$out/to-$side.capture"; then break; fi
    sleep .01
  done
  grep -q 'listening on' "$out/to-$side.capture"
done
if [[ $profile == loss || $profile == reorder ]]; then
  tcpdump --immediate-mode -B 8192 -i ztcpkca -Q in -n -s 0 -U \
    -w "$out/fault-source.pcap" 'tcp port 18181' > "$out/fault-source.capture" 2>&1 &
  capture_source=$!
  for ((i=0;i<100;i++)); do
    if grep -q 'listening on' "$out/fault-source.capture"; then break; fi
    sleep .01
  done
  grep -q 'listening on' "$out/fault-source.capture"
fi
if [[ $profile == loss || $profile == window || $profile == reset || \
      $profile == reorder ]]; then
  mode=origins
  if [[ $profile == window ]]; then mode=window; fi
  if [[ $profile == reset ]]; then mode=reset; fi
  if [[ $profile == reorder ]]; then mode=reorder; fi
  python3 "$root/zutcp/interop/zutcptcp.py" observe "$out" ztcpkca "$mode" \
    > "$out/observer.out" 2> "$out/observer.err" &
  observer_pid=$!
  for ((i=0;i<100;i++)); do
    [[ -f $out/observer-ready ]] && break
    kill -0 "$observer_pid"
    sleep .01
  done
  [[ -f $out/observer-ready ]]
fi
ip netns exec "$cli" env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  "${client_environment[@]}" timeout 35 "${client_command[@]}" \
  > "$out/client.out" 2> "$out/client.err" &
client_pid=$!
if [[ $profile == loss || $profile == reorder ]]; then
  read -r client_base server_base < <(timeout 15 cat "$out/origins")
  if [[ $profile == loss ]]; then
    wait "$observer_pid"
    observer_pid=
  fi
  protocol=ip iphead=20 length_offset=2 protocol_offset=9
  if [[ $family == 6 ]]; then
    protocol=ipv6 iphead=40 length_offset=4 protocol_offset=6
  fi
  tc qdisc add dev ztcpksa root handle 1: prio bands 3 \
    priomap 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  if [[ $profile == loss ]]; then
    tc qdisc add dev ztcpksa parent 1:3 handle 30: netem loss 100%
  else
    tc qdisc add dev ztcpksa parent 1:3 handle 30: netem delay 20ms
  fi
  # Exact first DATA sequence plus a 1024..2047-byte IP/TCP packet. This excludes
  # final handshake ACKs sharing the sameSEQ while both actual peers use MSS1200.
  tc filter add dev ztcpksa parent 1: protocol "$protocol" pref 10 u32 \
    match u8 6 0xff at "$protocol_offset" \
    match u16 0x0400 0xfc00 at "$length_offset" \
    match u32 "$client_base" 0xffffffff at "$((iphead + 4))" flowid 1:3
  timeout 10 bash -c 'printf R > "$1"' bash "$out/start-gate"
  if [[ $profile == loss ]]; then
    dropped=0
    for ((i=0;i<2000;i++)); do
      tc -s qdisc show dev ztcpksa > "$out/fault-loss"
      dropped=$(sed -n 's/.*dropped \([0-9][0-9]*\).*/\1/p' "$out/fault-loss" | \
        sort -n | tail -1)
      if [[ ${dropped:-0} -gt 0 ]]; then break; fi
      kill -0 "$client_pid"
      sleep .01
    done
    [[ ${dropped:-0} -gt 0 ]]
    tc filter del dev ztcpksa parent 1: protocol "$protocol" pref 10 u32
  else
    # Do not flush a delayed original merely because a fast retry finished first.
    timeout 25 cat "$out/reorder-ready" > "$out/reorder-ready-result"
    wait "$observer_pid"
    observer_pid=
  fi
fi
if [[ $profile == reset ]]; then
  timeout 25 cat "$out/reset-ready" > "$out/reset-ready-result"
  wait "$observer_pid"
  observer_pid=
  timeout 10 bash -c 'printf R > "$1"' bash "$out/reset-gate"
fi
if [[ $profile == window ]]; then
  timeout 25 cat "$out/window-ready" > "$out/window-ready-result"
  wait "$observer_pid"
  observer_pid=
  python3 - "$out/window-release-time" <<'PY'
import sys, time
from pathlib import Path
Path(sys.argv[1]).write_text(str(time.time()) + '\n')
PY
  timeout 10 bash -c 'printf R > "$1"' bash "$out/read-gate"
fi
wait "$client_pid"
client_pid=
wait "$server_pid"
server_pid=
if [[ $profile == reorder ]]; then
  drained=0
  for ((i=0;i<500;i++)); do
    tc -s qdisc show dev ztcpksa > "$out/fault-reorder"
    if awk '/backlog/ {seen=1; if ($3 != "0p") busy=1} END {exit (!seen || busy)}' \
        "$out/fault-reorder"; then drained=1; break; fi
    sleep .01
  done
  ((drained))
  tc -s filter show dev ztcpksa parent 1: > "$out/reorder-filter"
fi
sleep .05
stopCapture
cmp "$out/expected" "$out/client.out"
[[ ! -s "$out/server.out" ]]
python3 "$root/zutcp/interop/zutcptcp.py" "$out" "$direction" "$family" "$profile"
