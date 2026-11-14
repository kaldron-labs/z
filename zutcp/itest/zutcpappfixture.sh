#!/usr/bin/env bash
set -euo pipefail
[[ ${ZXDP_RUN_FIXTURE:-0} == 1 ]]
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
family=$1
out=$2
program=$(pwd)/zutcpapptest
if [[ ${3:-Ztcp} == Ztls ]]; then
  program=$(pwd)/zutcptlsapptest
  openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
    -addext basicConstraints=critical,CA:TRUE \
    -addext subjectAltName=DNS:localhost,IP:198.18.2.2,IP:2001:db8:2::2 \
    -keyout "$out/key.pem" -out "$out/cert.pem" > "$out/cert.log" 2>&1
  export ZUTCP_TEST_CERT="$out/cert.pem" ZUTCP_TEST_KEY="$out/key.pem"
fi
cli=zutcp-app-cli-$$
srv=zutcp-app-srv-$$
cli_made=0 srv_made=0 server_pid=
cleanup() {
  if [[ -n "$server_pid" ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  if ((cli_made)); then
    ip -n "$cli" link del zutcpac 2>/dev/null || true
    ip netns del "$cli" 2>/dev/null || true
  fi
  if ((srv_made)); then
    ip -n "$srv" link del zutcpas 2>/dev/null || true
    ip netns del "$srv" 2>/dev/null || true
  fi
}
trap cleanup EXIT
ip netns add "$cli"
cli_made=1
ip netns add "$srv"
srv_made=1
ip link add zutcpac numrxqueues 1 numtxqueues 1 type veth \
  peer name zutcpas numrxqueues 1 numtxqueues 1
ip link set zutcpac netns "$cli"
ip link set zutcpas netns "$srv"
ip -n "$cli" link set lo up
ip -n "$srv" link set lo up
ip -n "$cli" link set zutcpac up
ip -n "$srv" link set zutcpas up
ip -n "$cli" addr add 198.18.2.1/32 dev zutcpac
ip -n "$srv" addr add 198.18.2.2/32 dev zutcpas
ip -n "$cli" -6 addr add 2001:db8:2::1/128 dev zutcpac nodad
ip -n "$srv" -6 addr add 2001:db8:2::2/128 dev zutcpas nodad
cli_mac=$(ip -n "$cli" -o link show zutcpac | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
srv_mac=$(ip -n "$srv" -o link show zutcpas | sed -n 's/.*link\/ether \([^ ]*\).*/\1/p')
ip -n "$cli" route add 198.18.2.2/32 dev zutcpac src 198.18.2.1
ip -n "$srv" route add 198.18.2.1/32 dev zutcpas src 198.18.2.2
ip -n "$cli" -6 route add 2001:db8:2::2/128 dev zutcpac src 2001:db8:2::1
ip -n "$srv" -6 route add 2001:db8:2::1/128 dev zutcpas src 2001:db8:2::2
ip -n "$cli" neigh replace 198.18.2.2 lladdr "$srv_mac" nud permanent dev zutcpac
ip -n "$srv" neigh replace 198.18.2.1 lladdr "$cli_mac" nud permanent dev zutcpas
ip -n "$cli" -6 neigh replace 2001:db8:2::2 lladdr "$srv_mac" nud permanent dev zutcpac
ip -n "$srv" -6 neigh replace 2001:db8:2::1 lladdr "$cli_mac" nud permanent dev zutcpas
ip netns exec "$cli" ethtool -K zutcpac tx off tso off gso off gro off >/dev/null
ip netns exec "$srv" ethtool -K zutcpas tx off tso off gso off gro off >/dev/null
server_role=--server client_role=--client
if [[ ${4:-native} == kernel ]]; then
  server_role=--kernel-server client_role=--kernel-client
fi
server=198.18.2.2
if [[ $family == 6 ]]; then server=2001:db8:2::2; fi
ip netns exec "$srv" timeout 25 "$program" "$server_role" zutcpas "$server" 18181 \
  > "$out/server.tap" 2> "$out/server.err" &
server_pid=$!
ready=0
for ((i=0;i<300;i++)); do
  if grep -q '^READY$' "$out/server.err"; then ready=1; break; fi
  if ! kill -0 "$server_pid" 2>/dev/null; then cat "$out/server.err" >&2; exit 1; fi
  sleep .01
done
((ready))
ip netns exec "$cli" timeout 25 "$program" "$client_role" zutcpac "$server" 18181 \
  > "$out/client.tap" 2> "$out/client.err"
wait "$server_pid"
server_pid=
prove --exec cat "$out/client.tap" "$out/server.tap"
