#!/usr/bin/env bash
# (c) Copyright 2026 Huw Rogers; MIT license (see LICENSE).
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
utility="$script_dir/zxdppacket"
output=
count=32
payload=128
mode=wait
physical=false

usage() {
  echo 'usage: zxdpfixture.sh [--output NEW_DIR --utility PATH --count N --payload N]' >&2
  echo '  [--mode wait|spin|ll]' >&2
  echo '  --physical --output NEW_DIR -- [zxdppacket arguments]' >&2
  exit 2
}
while (($#)); do
  case "$1" in
    --output) (($# >= 2)) || usage; output=$2; shift 2;;
    --utility) (($# >= 2)) || usage; utility=$2; shift 2;;
    --count) (($# >= 2)) || usage; count=$2; shift 2;;
    --payload) (($# >= 2)) || usage; payload=$2; shift 2;;
    --mode) (($# >= 2)) || usage; mode=$2; shift 2;;
    --physical) physical=true; shift;;
    --) shift; break;;
    *) usage;;
  esac
done
[[ -x $utility ]] || { echo "packet utility unavailable: $utility" >&2; exit 1; }

if $physical; then
  [[ -n $output && $# -gt 0 ]] || usage
  mkdir -m 700 -- "$output"
  printf 'backend=caller-owned-interface\nutility=%q\n' "$utility" > "$output/state.txt"
  printf 'argument=%q\n' "$@" >> "$output/state.txt"
  # All interface configuration is caller-owned. The utility protects existing
  # XDP users and only retires its own binding. Preserve logs on either result.
  set +e
  "$utility" "$@" > "$output/endpoint.log" 2>&1
  result=$?
  set -e
  printf 'exit=%s\n' "$result" >> "$output/state.txt"
  exit "$result"
fi

[[ $# == 0 && $count =~ ^[1-9][0-9]*$ && $payload =~ ^[1-9][0-9]*$ ]] || usage
case "$mode" in wait|spin|ll) ;; *) usage;; esac
[[ $(id -u) == 0 ]] || { echo 'fixture requires network-namespace setup privileges' >&2; exit 1; }
if [[ ${ZXDP_FIXTURE_INNER:-0} != 1 ]]; then
  if [[ -z $output ]]; then
    output=$(mktemp -d /tmp/zxdp-fixture-parent.XXXXXX)/run
  fi
  # The entire resource-owning shell runs inside this anonymous namespace.
  # No named namespace, host route, interface, neighbor or attachment is changed.
  exec unshare --net env ZXDP_FIXTURE_INNER=1 "$0" --output "$output" \
    --utility "$utility" --count "$count" --payload "$payload" --mode "$mode"
fi

[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]] || {
  echo 'refusing fixture resource creation in the initial network namespace' >&2
  exit 1
}
mkdir -m 700 -- "$output"
server_pid=
cleanup() {
  result=$?
  trap - EXIT
  if [[ -n $server_pid ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  printf 'fixture_exit=%s\n' "$result" >> "$output/state.txt"
  echo "Zxdp fixture artifacts: $output"
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
printf 'backend=veth\nnamespace=anonymous\nmode=%s\ncount=%s\npayload=%s\n' \
  "$mode" "$count" "$payload" > "$output/state.txt"
printf 'owned_links=zxdpp0,zxdpp1\nreserved_endpoints=192.0.2.1:40000,192.0.2.2:40001/udp\n' \
  >> "$output/state.txt"
ip link add zxdpp0 numrxqueues 1 numtxqueues 1 type veth \
  peer name zxdpp1 numrxqueues 1 numtxqueues 1
ip addr add 192.0.2.1/32 dev zxdpp0
ip addr add 192.0.2.2/32 dev zxdpp1
ip link set zxdpp0 up
ip link set zxdpp1 up
client_mac=$(ip -o link show dev zxdpp0 | awk '{for(i=1;i<=NF;i++)
  if($i=="link/ether") print $(i+1)}')
server_mac=$(ip -o link show dev zxdpp1 | awk '{for(i=1;i<=NF;i++)
  if($i=="link/ether") print $(i+1)}')
ip neigh add 192.0.2.2 lladdr "$server_mac" nud permanent dev zxdpp0
ip neigh add 192.0.2.1 lladdr "$client_mac" nud permanent dev zxdpp1
ip -details link show > "$output/links.txt"
ip addr show > "$output/addresses.txt"
ip neigh show > "$output/neighbors.txt"
wait_args=(--spin 0 --timeout 1)
case "$mode" in
  spin) wait_args=(--spin 3 --timeout 2);;
  ll) wait_args=(--spin 1000 --timeout 0);;
esac
common=(--count "$count" --payload "$payload" --frames 2 --deadline 15 "${wait_args[@]}")
timeout --signal=TERM --kill-after=2s 25s "$utility" server \
  --interface zxdpp1 --peer-mac "$client_mac" \
  --local-ip 192.0.2.2 --peer-ip 192.0.2.1 --local-port 40001 --peer-port 40000 \
  --ready-file "$output/server.ready" "${common[@]}" > "$output/server.log" 2>&1 &
server_pid=$!
for ((attempt=0; attempt<200; ++attempt)); do
  [[ ! -e $output/server.ready ]] || break
  kill -0 "$server_pid" 2>/dev/null || { wait "$server_pid"; exit 1; }
  sleep 0.05
done
[[ -e $output/server.ready ]] || { echo 'server readiness deadline expired' >&2; exit 1; }
timeout --signal=TERM --kill-after=2s 25s "$utility" client \
  --interface zxdpp0 --peer-mac "$server_mac" \
  --local-ip 192.0.2.1 --peer-ip 192.0.2.2 --local-port 40000 --peer-port 40001 \
  "${common[@]}" > "$output/client.log" 2>&1
wait "$server_pid"
server_pid=
grep -q '^PASS role=client ' "$output/client.log"
grep -q '^PASS role=server ' "$output/server.log"
printf 'peer_results=PASS,PASS\n' >> "$output/state.txt"
