#!/usr/bin/env bash
# (c) Copyright 2026 Huw Rogers; MIT license (see LICENSE).
set -euo pipefail
[[ ${ZPKT_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
[[ $# == 3 && ( $1 == 4 || $1 == 6 ) ]]
family=$1 out=$2 program=$3
client_ns=zutcp-afp-c-$$ server_ns=zutcp-afp-s-$$
client_dev=zafpc server_dev=zafps
client_pid= server_pid= client_capture= server_capture=

cleanup() {
  for pid in "$client_pid" "$server_pid"; do
    if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
  done
  stop_capture
  ip netns del "$client_ns" 2>/dev/null || :
  ip netns del "$server_ns" 2>/dev/null || :
}
stop_capture() {
  for pid in "$client_capture" "$server_capture"; do
    if [[ -n $pid ]]; then kill -INT "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
  done
  client_capture= server_capture=
}
trap cleanup EXIT
trap 'printf "AFP fixture failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
trap 'exit 1' INT TERM

if [[ $family == 4 ]]; then
  client_addr=198.18.7.1 server_addr=198.18.7.2 prefix=32
else
  client_addr=2001:db8:7::1 server_addr=2001:db8:7::2 prefix=128
fi
ip netns add "$client_ns"
ip netns add "$server_ns"
ip link add "$client_dev" numrxqueues 1 numtxqueues 1 type veth \
  peer name "$server_dev" numrxqueues 1 numtxqueues 1
ip link set "$client_dev" netns "$client_ns"
ip link set "$server_dev" netns "$server_ns"
for side in client server; do
  if [[ $side == client ]]; then ns=$client_ns dev=$client_dev address=$client_addr
  else ns=$server_ns dev=$server_dev address=$server_addr; fi
  ip -n "$ns" link set lo up
  ip -n "$ns" link set "$dev" up
  address_args=()
  if [[ $family == 6 ]]; then address_args=(nodad); fi
  ip -"$family" -n "$ns" addr add "$address/$prefix" dev "$dev" "${address_args[@]}"
  ip netns exec "$ns" ethtool -K "$dev" rx off tx off tso off gso off gro off
done
client_mac=$(ip -n "$client_ns" -o link show "$client_dev" | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
server_mac=$(ip -n "$server_ns" -o link show "$server_dev" | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip -"$family" -n "$client_ns" route add "$server_addr/$prefix" dev "$client_dev" src "$client_addr"
ip -"$family" -n "$server_ns" route add "$client_addr/$prefix" dev "$server_dev" src "$server_addr"
ip -"$family" -n "$client_ns" neigh add "$server_addr" lladdr "$server_mac" nud permanent dev "$client_dev"
ip -"$family" -n "$server_ns" neigh add "$client_addr" lladdr "$client_mac" nud permanent dev "$server_dev"

for side in client server; do
  if [[ $side == client ]]; then
    ns=$client_ns dev=$client_dev local=$client_addr peer=$server_addr
  else
    ns=$server_ns dev=$server_dev local=$server_addr peer=$client_addr
  fi
  ip netns exec "$ns" tcpdump --immediate-mode -B 8192 -Q in -nn -s 2048 -U -i "$dev" \
    -w "$out/to-$side.pcap" "tcp port 18187 and src host $peer and dst host $local" \
    > "$out/to-$side.capture" 2>&1 &
  pid=$!
  if [[ $side == client ]]; then client_capture=$pid; else server_capture=$pid; fi
  for ((attempt=0; attempt<200; ++attempt)); do
    if grep -q 'listening on' "$out/to-$side.capture"; then break; fi
    kill -0 "$pid"
    sleep .01
  done
  grep -q 'listening on' "$out/to-$side.capture"
done

ip netns exec "$server_ns" timeout --kill-after=2s 22s "$program" server "$server_dev" "$server_addr" \
  > "$out/server.out" 2> "$out/server.err" &
server_pid=$!
for ((attempt=0; attempt<300; ++attempt)); do
  if grep -q '^READY$' "$out/server.err"; then break; fi
  kill -0 "$server_pid"
  sleep .01
done
grep -q '^READY$' "$out/server.err"
ip netns exec "$client_ns" timeout --kill-after=2s 22s "$program" client "$client_dev" "$server_addr" \
  > "$out/client.out" 2> "$out/client.err" &
client_pid=$!
wait "$client_pid"; client_pid=
wait "$server_pid"; server_pid=
sleep .05
stop_capture

PYTHONDONTWRITEBYTECODE=1 python3 - "$(dirname "$0")" "$out" <<'PY'
import json
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
from zutcprecovery import decode, pcap_frames, wire_tcp

out = Path(sys.argv[2])
payload_size = 4096
streams = {}
for side, byte in (('server', ord('a')), ('client', ord('A'))):
    footer = (out / ('to-' + side + '.capture')).read_text().splitlines()
    assert '0 packets dropped by kernel' in footer, (side, footer)
    path = out / ('to-' + side + '.pcap')
    for stamp, frame in pcap_frames(path):
        packet = wire_tcp(frame)
        assert packet and not packet['flags'] & 4, ('RST or unexpected frame', side, stamp)
    payload = bytes(byte + (i % 26) for i in range(payload_size))
    stream = decode(path, payload)
    assert len(stream['tcp_origins']) == 1
    origin = stream['tcp_origins'][0]
    assert origin['syns'] and origin['synacks'] and origin['final_acks'], origin
    assert len(stream['first_seen_offsets']) > 1, stream
    streams[side] = {**stream, 'tcp_resets': 0}
for side, peer in (('client', 'server'), ('server', 'client')):
    fins = [event['time'] for event in streams[side]['controls'] if event['type'] == 'fin']
    assert fins and any(ack['next'] == payload_size + 1 and ack['time'] >= min(fins)
                        for ack in streams[peer]['acks']), ('missing FIN ACK', side)
(out / 'wire-evidence.json').write_text(json.dumps(streams, indent=2) + '\n')
summary = [f"{side} verified payload bytes {stream['payload_bytes_covered']}"
           for side, stream in streams.items()]
(out / 'wire-summary').write_text(' | '.join(summary) + ' | FINs acknowledged, zero RSTs\n')
PY
