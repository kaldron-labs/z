#!/bin/sh
set -eu

role=${1:-${ROLE:-}}

fail()
{
  echo "qir_endpoint.sh: $*" >&2
  exit 1
}

# Query each family independently. ip's address records are not ordered by family.
iface_addrs()
{
  family=$1
  iface=$2
  case "$iface" in
    ''|-*|*[!a-zA-Z0-9_.:@-]*) return 1 ;;
  esac
  records=$(ip -o "-$family" addr show dev "$iface" scope global) || return 1
  printf '%s\n' "$records" | awk '
    / tentative( |$)| deprecated( |$)/ { next }
    { for (i = 1; i < NF; ++i)
        if ($i == "inet" || $i == "inet6") {
          split($(i + 1), a, "/"); print a[1]; break
        }
    }' | sort -u
}

one_addr()
{
  addresses=$(iface_addrs "$1" "$2") ||
    fail "address discovery failed for $2 IPv$1"
  count=$(printf '%s\n' "$addresses" | awk 'NF { ++n } END { print n+0 }')
  case "$count" in
    0) printf '%s\n' off ;;
    1) printf '%s\n' "$addresses" ;;
    *) fail "multiple IPv$1 addresses on $2; set ZHTTP_QIR_ORIG$1 explicitly" ;;
  esac
}

resolve_originals()
{
  iface=${ZHTTP_QIR_IFACE-eth0}
  # Validate even when both families are explicitly configured.
  case "$iface" in
    ''|-*|*[!a-zA-Z0-9_.:@-]*) fail "invalid ZHTTP_QIR_IFACE" ;;
  esac
  if [ "${ZHTTP_QIR_ORIG4+x}" != x ]; then
    ZHTTP_QIR_ORIG4=$(one_addr 4 "$iface") || exit 1
    export ZHTTP_QIR_ORIG4
  fi
  if [ "${ZHTTP_QIR_ORIG6+x}" != x ]; then
    ZHTTP_QIR_ORIG6=$(one_addr 6 "$iface") || exit 1
    export ZHTTP_QIR_ORIG6
  fi
  if [ "$ZHTTP_QIR_ORIG4" = off ] && [ "$ZHTTP_QIR_ORIG6" = off ]; then
    fail "no original address available"
  fi
}

runner_addrs()
{
  addresses=$(iface_addrs 4 eth0) || fail "eth0 IPv4 discovery failed"
  ip4=$(printf '%s\n' "$addresses" | awk '/^193\.167\./ { print; exit }')
  addresses=$(iface_addrs 6 eth0) || fail "eth0 IPv6 discovery failed"
  ip6=$(printf '%s\n' "$addresses" | awk '/^fd00:cafe:cafe:/ { print; exit }')
}

setup_runner_net()
{
  runner_addrs
  if [ -z "$ip4" ] && [ -z "$ip6" ]; then return 0; fi

  mkdir -p /logs/qlog
  echo "Setting up routes..."
  ethtool -K eth0 tx off || true

  if [ -n "$ip4" ]; then
    gateway=${ip4%.*}.2
    unneeded=${ip4%.*}.0
    echo "Endpoint's IPv4 address is $ip4"
    route add -net 193.167.0.0 netmask 255.255.0.0 gw "$gateway" || true
    route del -net "$unneeded" netmask 255.255.255.0 || true
  fi

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
  if [ -z "$ip4" ] && [ -z "$ip6" ]; then return 0; fi

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

if [ "$role" = server ] && [ "${TESTCASE:-}" = connectionmigration ]; then
  resolve_originals
fi

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
