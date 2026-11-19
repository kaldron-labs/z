#!/usr/bin/env bash
set -euo pipefail
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
family=${1:-4}
mode=${2:-mixed}
[[ $family == 4 || $family == 6 ]]
[[ $mode == udp || $mode == mixed ]]

root=$(cd "$(dirname "$0")" && pwd)
client=zutcp-udp-client-$$
server=zutcp-udp-server-$$
cleanup() {
  ip -n "$client" link del zutcpua 2>/dev/null || true
  ip netns del "$client" 2>/dev/null || true
  ip netns del "$server" 2>/dev/null || true
}
trap cleanup EXIT
ip netns add "$client"
ip netns add "$server"
ip -n "$client" link add zutcpua numrxqueues 1 numtxqueues 1 type veth peer name zutcpub \
  numrxqueues 1 numtxqueues 1
ip -n "$client" link set zutcpub netns "$server"
ip -n "$client" link set zutcpua up
ip -n "$server" link set zutcpub up
if [[ $family == 6 ]]; then
  a=2001:db8:2::1 b=2001:db8:2::2 prefix=128
  ip -n "$client" -6 addr add "$a/$prefix" dev zutcpua nodad
  ip -n "$server" -6 addr add "$b/$prefix" dev zutcpub nodad
else
  a=198.18.2.1 b=198.18.2.2 prefix=32
  ip -n "$client" addr add "$a/$prefix" dev zutcpua
  ip -n "$server" addr add "$b/$prefix" dev zutcpub
fi
mac_a=$(ip -n "$client" -o link show zutcpua | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
mac_b=$(ip -n "$server" -o link show zutcpub | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip -n "$client" "-$family" route add "$b/$prefix" dev zutcpua src "$a"
ip -n "$server" "-$family" route add "$a/$prefix" dev zutcpub src "$b"
ip -n "$client" "-$family" neigh add "$b" lladdr "$mac_b" nud permanent dev zutcpua
ip -n "$server" "-$family" neigh add "$a" lladdr "$mac_a" nud permanent dev zutcpub
ip netns exec "$client" ethtool -K zutcpua tx off tso off gso off gro off >/dev/null
ip netns exec "$server" ethtool -K zutcpub tx off tso off gso off gro off >/dev/null

python3 - "$root/zutcpudptest" "$client" "$server" "$a" "$b" "$mode" <<'PY'
import select
import subprocess
import sys

program, client, server, a, b, mode = sys.argv[1:]
peers = []
try:
    for ns, iface, local, peer, role in (
        (server, "zutcpub", b, a, "server"), (client, "zutcpua", a, b, "client")
    ):
        argv = ["ip", "netns", "exec", ns, program, iface, local, peer, role, mode]
        peers.append(subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      text=True, bufsize=1))
    for cycle in range(2):
        for label in ("ready", "passed"):
            for process in peers:
                readable, _, _ = select.select([process.stdout], [], [], 20)
                line = process.stdout.readline().strip() if readable else "<timeout>"
                if line != f"{label} {cycle}":
                    raise RuntimeError(f"expected {label} {cycle}, received {line!r}")
            for process in peers:
                process.stdin.write("g\n")
                process.stdin.flush()
    for process in peers:
        if process.wait(timeout=15):
            raise RuntimeError(f"fixture peer exited with status {process.returncode}")
finally:
    for process in peers:
        if process.poll() is None:
            process.kill()
        process.wait()
PY
