#!/bin/sh
set -eu

role=${1:-${ROLE:-}}

setup_runner_net()
{
  mkdir -p /logs/qlog

  ips=$(hostname -I 2>/dev/null || true)
  set -- $ips
  ip4=${1:-}
  case "$ip4" in
    193.167.*) ;;
    *) return 0 ;;
  esac

  echo "Setting up routes..."
  ethtool -K eth0 tx off || true

  gateway=${ip4%.*}.2
  unneeded=${ip4%.*}.0
  echo "Endpoint's IPv4 address is $ip4"
  route add -net 193.167.0.0 netmask 255.255.0.0 gw "$gateway" || true
  route del -net "$unneeded" netmask 255.255.255.0 || true

  ip6=${2:-}
  if [ -n "$ip6" ]; then
    gateway=${ip6%:*}:2
    unneeded=${ip6%:*}:
    echo "Endpoint's IPv6 address is $ip6"
    ip -d route add fd00:cafe:cafe::/48 via "$gateway" || true
    ip -d route del "$unneeded/64" || true
  fi
}

wait_for_sim()
{
  ips=$(hostname -I 2>/dev/null || true)
  set -- $ips
  case "${1:-}" in
    193.167.*) ;;
    *) return 0 ;;
  esac

  i=0
  while [ "$i" -lt 30 ]; do
    if nc -z sim 57832 >/dev/null 2>&1; then
      return 0
    fi
    i=$((i + 1))
    sleep 1
  done
  return 1
}

setup_runner_net

case "$role" in
  --help|-h)
    exec /usr/local/bin/zhttpqir --help
    ;;
  server)
    exec /usr/local/bin/zhttpqir server
    ;;
  client)
    wait_for_sim
    exec /usr/local/bin/zhttpqir client
    ;;
  *)
    echo "qir_endpoint.sh: expected server or client" >&2
    exit 2
    ;;
esac
