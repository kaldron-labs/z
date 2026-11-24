#!/bin/sh
set -eu

srcdir=${1:?source directory required}
builddir=${2:?build directory required}
. "$srcdir/zi/itest/zi-test-residue.sh"
zi_residue_init zhttp-qir-script-test

tmp=$ZI_RESIDUE_DIR
# These tests use a synthetic runner. Keep metadata and cleanup calls away
# from the host daemon, which may be serving a concurrent real QIR run.
mkdir -p "$tmp/bin"
cat >"$tmp/bin/docker" <<'EOF'
#!/bin/sh
case "$1" in
  version) printf '%s\n' fixture ;;
  image|rm) ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$tmp/bin/docker"
PATH="$(CDPATH= cd -- "$tmp/bin" && pwd):$PATH"
export PATH

cleanup()
{
  status=$?
  zi_residue_finish "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Exercise entrypoint discovery without network changes or the real endpoint.
cat >"$tmp/bin/ip" <<'EOF'
#!/bin/sh
if [ "$1" = -d ]; then
  printf '%s\n' "ip $*" >>"$QIR_ROUTE_LOG"
  exit 0
fi
[ "$1" = -o ] || exit 99
[ "$3" = addr ] && [ "$4" = show ] && [ "$5" = dev ] || exit 99
[ "$7" = scope ] && [ "$8" = global ] || exit 99
[ "$6" != missing ] || exit 1
[ "${QIR_DISCOVERY_FAIL:-0}" = 0 ] || exit 1
case "$2" in
  -4) printf '%s\n' "${QIR_ADDR4:-}" ;;
  -6) printf '%s\n' "${QIR_ADDR6:-}" ;;
  *) exit 99 ;;
esac
EOF
cat >"$tmp/bin/endpoint" <<'EOF'
#!/bin/sh
printf '%s|%s|%s\n' "$1" "${ZHTTP_QIR_ORIG4-unset}" "${ZHTTP_QIR_ORIG6-unset}"
EOF
chmod +x "$tmp/bin/ip" "$tmp/bin/endpoint"
sed "s|/usr/local/bin/zhttpqir|$tmp/bin/endpoint|g" \
  "$srcdir/zhttp/interop/qir_endpoint.sh" >"$tmp/entrypoint"

QIR_ADDR4='2: eth0 inet 172.28.44.10/24 brd 172.28.44.255 scope global eth0'
QIR_ADDR6='2: eth0 inet6 2001:db8::10/64 scope global'
export QIR_ADDR4 QIR_ADDR6
unset ZHTTP_QIR_ORIG4 ZHTTP_QIR_ORIG6 ZHTTP_QIR_IFACE
TESTCASE=connectionmigration sh "$tmp/entrypoint" server >"$tmp/discovered"
test "$(cat "$tmp/discovered")" = 'server|172.28.44.10|2001:db8::10'
# IPv6 arriving before IPv4 in the address source cannot affect family selection.
QIR_ADDR6='2: eth0 inet6 2001:db8::bad/64 scope global tentative
2: eth0 inet6 2001:db8::dead/64 scope global deprecated
2: eth0 inet6 2001:db8::10/64 scope global'
export QIR_ADDR6
TESTCASE=connectionmigration sh "$tmp/entrypoint" server >"$tmp/discovered"
test "$(cat "$tmp/discovered")" = 'server|172.28.44.10|2001:db8::10'
QIR_ADDR6=''
export QIR_ADDR6
TESTCASE=connectionmigration sh "$tmp/entrypoint" server >"$tmp/discovered"
test "$(cat "$tmp/discovered")" = 'server|172.28.44.10|off'
QIR_ADDR4="$QIR_ADDR4
2: eth0 inet 172.28.44.11/24 scope global eth0"
export QIR_ADDR4
if TESTCASE=connectionmigration sh "$tmp/entrypoint" server >"$tmp/discovered" 2>&1; then
  echo 'entrypoint accepted ambiguous originals' >&2
  exit 1
fi
TESTCASE=connectionmigration ZHTTP_QIR_ORIG4=127.0.0.1 \
  ZHTTP_QIR_ORIG6=off sh "$tmp/entrypoint" server >"$tmp/discovered"
test "$(cat "$tmp/discovered")" = 'server|127.0.0.1|off'
# Duplicate records denote one address, not an ambiguous configuration.
QIR_ADDR4='2: eth0 inet 172.28.44.10/24 scope global eth0
2: eth0 inet 172.28.44.10/24 scope global eth0'
export QIR_ADDR4
TESTCASE=connectionmigration sh "$tmp/entrypoint" server >"$tmp/discovered"
test "$(cat "$tmp/discovered")" = 'server|172.28.44.10|off'
for iface in missing '-eth0' 'eth0 eth1' ''; do
  if TESTCASE=connectionmigration ZHTTP_QIR_IFACE="$iface" \
      sh "$tmp/entrypoint" server >"$tmp/discovered" 2>&1; then
    echo 'entrypoint accepted invalid or missing interface' >&2
    exit 1
  fi
done
if TESTCASE=connectionmigration QIR_DISCOVERY_FAIL=1 \
    sh "$tmp/entrypoint" server >"$tmp/discovered" 2>&1; then
  echo 'entrypoint accepted failed discovery' >&2
  exit 1
fi
if TESTCASE=connectionmigration ZHTTP_QIR_ORIG4=off ZHTTP_QIR_ORIG6=off \
    sh "$tmp/entrypoint" server >"$tmp/discovered" 2>&1; then
  echo 'entrypoint accepted no original family' >&2
  exit 1
fi
# Ordinary modes retain their binding configuration.
TESTCASE=transfer sh "$tmp/entrypoint" server >"$tmp/discovered"
test "$(cat "$tmp/discovered")" = 'server|unset|unset'

# Runner route setup uses eth0 originals, independently of a preferred IP.
mkdir -p "$tmp/netbin"
for command in route ethtool nc mkdir; do
  cat >"$tmp/netbin/$command" <<'EOF'
#!/bin/sh
printf '%s\n' "${0##*/} $*" >>"$QIR_ROUTE_LOG"
EOF
  chmod +x "$tmp/netbin/$command"
done
QIR_ADDR4='2: eth0 inet 193.167.100.100/24 scope global eth0'
QIR_ADDR6='2: eth0 inet6 fd00:cafe:cafe:100::100/64 scope global'
QIR_ROUTE_LOG="$tmp/routes"
export QIR_ADDR4 QIR_ADDR6 QIR_ROUTE_LOG
TESTCASE=connectionmigration ZHTTP_QIR_PREF4=192.0.2.44 \
  PATH="$tmp/netbin:$PATH" sh "$tmp/entrypoint" server >"$tmp/discovered"
grep -q 'gw 193.167.100.2' "$QIR_ROUTE_LOG"
grep -q 'via fd00:cafe:cafe:100::2' "$QIR_ROUTE_LOG"
! grep -q '192.0.2.44' "$QIR_ROUTE_LOG"
test "$(tail -n 1 "$tmp/discovered")" = \
  'server|193.167.100.100|fd00:cafe:cafe:100::100'
QIR_ADDR4=''
export QIR_ADDR4
: >"$QIR_ROUTE_LOG"
TESTCASE=transfer PATH="$tmp/netbin:$PATH" sh "$tmp/entrypoint" client \
  >"$tmp/discovered"
grep -q 'via fd00:cafe:cafe:100::2' "$QIR_ROUTE_LOG"
grep -q 'nc -z sim 57832' "$QIR_ROUTE_LOG"

# Validate the preferred-address rule without Docker or a packet capture.
python3 - "$srcdir/zhttp/interop/qir-run.py" <<'PYTEST'
import runpy
import sys
from types import SimpleNamespace

rules = runpy.run_path(sys.argv[1])
probes = rules["server_probes"]
matched = rules["matched_responses"]
def field(values):
    return SimpleNamespace(all_fields=[SimpleNamespace(show=v) for v in values])
def packet(path, frames, challenge=None, response=None):
    quic = SimpleNamespace(frame_type=field([hex(f) for f in frames]))
    if challenge is not None:
        setattr(quic, "path_challenge.data", field([challenge]))
    if response is not None:
        setattr(quic, "path_response.data", field([response]))
    return {"path": path, "quic": quic}
def path_of(p):
    return p["path"]

original = packet("original", [6])
response = packet("preferred", [0x1b, 0], response="client-probe")
challenge = packet("preferred", [0x1a, 0], challenge="server-probe")
data = packet("preferred", [8])
assert probes([original, response, challenge, data], path_of) == {"server-probe"}
assert probes([original, challenge, data], path_of) == {"server-probe"}
assert probes([original, response], path_of) is None
assert probes([original, response, data, challenge], path_of) is None
assert probes([original], path_of) is None
assert probes([original, challenge, packet("other", [0x1b], response="x")],
              path_of) is None
assert matched({"server-probe"}, [packet("preferred", [0x1b],
                                       response="server-probe")])
# RFC 9000 section 8.2.3 permits the response to arrive on another path.
assert matched({"server-probe"}, [packet("original", [0x1b],
                                       response="server-probe")])
assert not matched({"server-probe"}, [])
assert not matched({"server-probe"}, [response])
PYTEST

mkdir -p "$tmp/runner/logs/a" "$tmp/runner/logs_extra/c" \
  "$tmp/runner/results/b" "$tmp/runner/results_extra/d" "$tmp/out"
: >"$tmp/runner/run.py"
cat >"$tmp/runner/implementations_quic.json" <<'EOF'
{
  "ngtcp2": {
    "image": "ngtcp2:test",
    "url": "https://example.invalid/ngtcp2",
    "role": "both"
  },
  "quiche": {
    "image": "quiche:test",
    "url": "https://example.invalid/quiche",
    "role": "both"
  },
  "quic-go": {
    "image": "quic-go:test",
    "url": "https://example.invalid/quic-go",
    "role": "both"
  },
  "msquic": {
    "image": "msquic:test",
    "url": "https://example.invalid/msquic",
    "role": "both"
  }
}
EOF
cp "$tmp/runner/implementations_quic.json" "$tmp/implementations_quic.json.orig"
printf 'log\n' >"$tmp/runner/logs/a/log.txt"
printf 'prefixed log\n' >"$tmp/runner/logs_extra/c/log.txt"
printf 'result\n' >"$tmp/runner/results/b/result.txt"
printf 'prefixed result\n' >"$tmp/runner/results_extra/d/result.txt"
printf 'stdout\n' >"$tmp/stdout"
printf 'stderr\n' >"$tmp/stderr"

for script in \
  qir-build-image \
  qir-upload-image \
  qir-run-local \
  qir-collect-results \
  qir-report
do
  sh -n "$srcdir/scripts/$script"
  "$srcdir/scripts/$script" --help >/dev/null 2>&1
  if "$srcdir/scripts/$script" --bad-option >/dev/null 2>&1; then
    echo "$script accepted an invalid option" >&2
    exit 1
  fi
done
sh -n "$srcdir/zhttp/interop/qir_endpoint.sh"

"$srcdir/scripts/qir-collect-results" \
  --runner "$tmp/runner" \
  --out "$tmp/out" \
  --name collected \
  --stdout "$tmp/stdout" \
  --stderr "$tmp/stderr" \
  --status 7

test -f "$tmp/out/collected/logs/a/log.txt"
test -f "$tmp/out/collected/logs_extra/c/log.txt"
test -f "$tmp/out/collected/results/b/result.txt"
test -f "$tmp/out/collected/results_extra/d/result.txt"
test -f "$tmp/out/collected/stdout"
test -f "$tmp/out/collected/stderr"
test "$(cat "$tmp/out/collected/status")" = 7
test -f "$tmp/out/collected/collected_at"
test -f "$tmp/out/collected/runner.rev"
printf '%s\n' "python run.py -s collected -c collected -t unknown" \
  >"$tmp/out/collected/command"
printf '%s\n' collected >"$tmp/out/collected/server"
printf '%s\n' collected >"$tmp/out/collected/client"
printf '%s\n' unknown >"$tmp/out/collected/tests"

mkdir -p "$tmp/out/unsupported"
printf '%s\n' "python run.py -s zquic -c ngtcp2 -t retry" \
  >"$tmp/out/unsupported/command"
printf '%s\n' zquic >"$tmp/out/unsupported/server"
printf '%s\n' ngtcp2 >"$tmp/out/unsupported/client"
printf '%s\n' retry >"$tmp/out/unsupported/tests"
printf '%s\n' 127 >"$tmp/out/unsupported/status"

mkdir -p "$tmp/out/mixed" "$tmp/out/missing"
printf '%s\n' 'python run.py -s zquic -c msquic -t handshake,http3,transfer,retry' \
  >"$tmp/out/mixed/command"
printf '%s\n' zquic >"$tmp/out/mixed/server"
printf '%s\n' msquic >"$tmp/out/mixed/client"
printf '%s\n' handshake,http3,transfer,retry >"$tmp/out/mixed/tests"
printf '%s\n' 1 >"$tmp/out/mixed/status"
cat >"$tmp/out/mixed/results.json" <<'EOF'
{"results": [[
  {"name": "handshake", "abbr": "H", "result": "succeeded"},
  {"name": "http3", "abbr": "3", "result": "unsupported"},
  {"name": "transfer", "abbr": "DC", "result": "failed"}
]]}
EOF
printf '%s\n' 'python run.py -s missing -c missing -t handshake' \
  >"$tmp/out/missing/command"
printf '%s\n' 0 >"$tmp/out/missing/status"

"$srcdir/scripts/qir-collect-results" \
  --runner "$tmp/runner" \
  --out "$tmp/timestamped" \
  --status 0
set -- "$tmp/timestamped"/*
test -d "$1"
test "$1" != "$tmp/timestamped/collected"
test -f "$1/collected_at"

"$srcdir/scripts/qir-report" \
  --in "$tmp/out" \
  --out "$tmp/report.md"
grep -q 'QUIC Interop Report' "$tmp/report.md"
grep -q 'collected' "$tmp/report.md"
grep -q '| `zquic` | `ngtcp2` | `retry` | 127 | unsupported |' \
  "$tmp/report.md"
grep -q '| `collected` | `collected` | `unknown` | 7 | fail |' \
  "$tmp/report.md"
grep -q '| `zquic` | `msquic` | `handshake` | 1 | pass |' "$tmp/report.md"
grep -q '| `zquic` | `msquic` | `http3` | 1 | unsupported |' "$tmp/report.md"
grep -q '| `zquic` | `msquic` | `transfer` | 1 | fail |' "$tmp/report.md"
grep -q '| `zquic` | `msquic` | `retry` | 1 | unknown |' "$tmp/report.md"
grep -q '| `missing` | `missing` | `handshake` | 0 | unknown |' "$tmp/report.md"
grep -q 'logs/a/log.txt' "$tmp/report.md"
grep -q 'stderr' "$tmp/report.md"

cp -R "$srcdir/zhttp/itest/testdata/qir-results/fixture" "$tmp/fixture"
python3 "$srcdir/scripts/toml-to-json" \
  "$tmp/fixture/zquic-server_ngtcp2-client/results.toml" \
  >"$tmp/fixture/zquic-server_ngtcp2-client/results.json"

"$srcdir/scripts/qir-report" \
  --in "$tmp/fixture" \
  --out "$tmp/fixture-report.md"
grep -q 'fixture-kernel' "$tmp/fixture-report.md"
grep -q '| `zquic` | `ngtcp2` | `handshake` | 0 | pass |' \
  "$tmp/fixture-report.md"
grep -q '| `zquic` | `ngtcp2` | `transfer` | 0 | pass |' \
  "$tmp/fixture-report.md"

"$srcdir/scripts/qir-run-local" \
  --runner "$tmp/runner" \
  --image zhttp-qir:test \
  --url https://example.invalid/z \
  --peers ngtcp2 \
  --tests handshake,transfer,http3 \
  --build-type debug \
  --out "$tmp/run" \
  --dry-run >/dev/null 2>&1
test -f "$tmp/run/zquic-server_ngtcp2-client/command"
test -f "$tmp/run/zquic-server_ngtcp2-client/server"
test -f "$tmp/run/zquic-server_ngtcp2-client/client"
test -f "$tmp/run/zquic-server_ngtcp2-client/tests"
test -f "$tmp/run/zquic-server_ngtcp2-client/started_at"
test -f "$tmp/run/zquic-server_ngtcp2-client/finished_at"
test -f "$tmp/run/ngtcp2-server_zquic-client/command"
test -f "$tmp/run/python_compat/sitecustomize.py"
test -f "$tmp/run/report.md"
test "$(cat "$tmp/run/zquic-server_ngtcp2-client/status")" = 0
test "$(cat "$tmp/run/ngtcp2-server_zquic-client/status")" = 0
grep -q -- '-t handshake,transfer,http3' \
  "$tmp/run/zquic-server_ngtcp2-client/command"
cmp "$tmp/implementations_quic.json.orig" "$tmp/runner/implementations_quic.json"
grep -q 'registration=' "$tmp/run/metadata.env"
grep -q 'source_url=https://example.invalid/z' "$tmp/run/metadata.env"
grep -q 'build_type=debug' "$tmp/run/metadata.env"
grep -q 'started_at=' "$tmp/run/metadata.env"
grep -q 'finished_at=' "$tmp/run/metadata.env"
grep -q 'host_kernel=' "$tmp/run/metadata.env"
grep -q 'cpu_model=' "$tmp/run/metadata.env"
grep -q '^docker_version=fixture$' "$tmp/run/metadata.env"
"$srcdir/scripts/qir-report" \
  --in "$tmp/run" \
  --out "$tmp/run-report.md"
grep -q '| `zquic` | `ngtcp2` | `handshake` | 0 |' "$tmp/run-report.md"
grep -q '| `zquic` | `ngtcp2` | `transfer` | 0 |' "$tmp/run-report.md"
grep -q '| `zquic` | `ngtcp2` | `http3` | 0 |' "$tmp/run-report.md"
grep -q '| `ngtcp2` | `zquic` | `handshake` | 0 |' "$tmp/run-report.md"
grep -q '| `ngtcp2` | `zquic` | `transfer` | 0 |' "$tmp/run-report.md"
grep -q '| `ngtcp2` | `zquic` | `http3` | 0 |' "$tmp/run-report.md"
grep -q '| `zquic` | `ngtcp2` | `http3` | 0 | planned |' "$tmp/run-report.md"

"$srcdir/scripts/qir-run-local" \
  --runner "$tmp/runner" \
  --image zhttp-qir:test \
  --url https://example.invalid/z \
  --peers quiche,quic-go,msquic \
  --tests rebind-port,rebind-addr \
  --build-type debug \
  --out "$tmp/rebind-run" \
  --dry-run >/dev/null 2>&1
grep -q -- '-t rebind-port,rebind-addr' \
  "$tmp/rebind-run/zquic-server_quiche-client/command"
grep -q -- '-t rebind-port,rebind-addr' \
  "$tmp/rebind-run/quiche-server_zquic-client/command"
grep -q -- '-t rebind-port,rebind-addr' \
  "$tmp/rebind-run/zquic-server_quic-go-client/command"
grep -q -- '-t rebind-port,rebind-addr' \
  "$tmp/rebind-run/quic-go-server_zquic-client/command"
grep -q -- '-t rebind-port,rebind-addr' \
  "$tmp/rebind-run/zquic-server_msquic-client/command"
grep -q -- '-t rebind-port,rebind-addr' \
  "$tmp/rebind-run/msquic-server_zquic-client/command"

"$srcdir/scripts/qir-run-local" \
  --runner "$tmp/runner" --image zhttp-qir:test \
  --peers ngtcp2,quiche,quic-go,msquic --out "$tmp/full" \
  --dry-run >/dev/null 2>&1
python3 - "$tmp/full" <<'PY'
from pathlib import Path
import sys
runs = list(Path(sys.argv[1]).glob("*/tests"))
assert len(runs) == 8
for run in runs:
    tests = run.read_text().strip().split(",")
    assert len(tests) == len(set(tests)) == 22
    assert {"multiplexing", "connectionmigration", "rebind-port", "rebind-addr"} <= set(tests)
PY

cat >"$tmp/runner/run.py" <<'PY'
import argparse
import json

parser = argparse.ArgumentParser()
parser.add_argument("-s")
parser.add_argument("-c")
parser.add_argument("-t")
parser.add_argument("--json", required=True)
args = parser.parse_args()
with open(args.json, "w", encoding="utf-8") as f:
    json.dump({"results": [[
        {"name": "handshake", "result": "succeeded"},
        {"name": "transfer", "result": "succeeded"},
        {"name": "http3", "result": "unsupported"}
    ]]}, f)
PY
"$srcdir/scripts/qir-run-local" \
  --runner "$tmp/runner" \
  --image zhttp-qir:test \
  --peers msquic \
  --tests handshake,transfer,http3 \
  --out "$tmp/json-run" >/dev/null 2>&1
test -f "$tmp/json-run/zquic-server_msquic-client/results.json"
test -f "$tmp/json-run/msquic-server_zquic-client/results.json"
grep -q '| `zquic` | `msquic` | `handshake` | 0 | pass |' \
  "$tmp/json-run/report.md"
grep -q '| `zquic` | `msquic` | `http3` | 0 | unsupported |' \
  "$tmp/json-run/report.md"
grep -q '| `msquic` | `zquic` | `http3` | 0 | unsupported |' \
  "$tmp/json-run/report.md"

sed -i 's/"transfer", "result": "succeeded"/"transfer", "result": "failed"/' "$tmp/runner/run.py"
if "$srcdir/scripts/qir-run-local" \
    --runner "$tmp/runner" --image zhttp-qir:test --peers msquic \
    --tests handshake,transfer,http3 --out "$tmp/json-failed" >/dev/null 2>&1; then
  echo "qir-run-local accepted a failed JSON cell with a zero runner exit" >&2
  exit 1
fi
test "$(cat "$tmp/json-failed/zquic-server_msquic-client/status")" = 1
# Older output may contain zero status despite a failed cell.
printf '0\n' >"$tmp/json-failed/zquic-server_msquic-client/status"
"$srcdir/scripts/qir-report" --in "$tmp/json-failed" --out "$tmp/json-failed/report.md"
grep -q '^### zquic-server_msquic-client' "$tmp/json-failed/report.md"

if "$srcdir/scripts/qir-run-local" \
    --runner "$tmp/runner" \
    --image zhttp-qir:test \
    --peers ngtcp2, \
    --dry-run >/dev/null 2>&1; then
  echo "qir-run-local accepted an empty peer list item" >&2
  exit 1
fi

cat >"$tmp/runner/run.py" <<'PY'
print("Not compliant, skipping")
PY
if "$srcdir/scripts/qir-run-local" \
    --runner "$tmp/runner" \
    --image zhttp-qir:test \
    --peers ngtcp2 \
    --tests handshake \
    --out "$tmp/noncompliant" >/dev/null 2>&1; then
  echo "qir-run-local accepted runner non-compliance as success" >&2
  exit 1
fi
test "$(cat "$tmp/noncompliant/zquic-server_ngtcp2-client/status")" = 1
test "$(cat "$tmp/noncompliant/ngtcp2-server_zquic-client/status")" = 1
grep -q 'Not compliant' \
  "$tmp/noncompliant/zquic-server_ngtcp2-client/stdout"

if "$srcdir/scripts/qir-build-image" \
    --root "$tmp/missing-root" --allow-dirty >/dev/null 2>&1; then
  echo "qir-build-image accepted a missing root" >&2
  exit 1
fi

test -d "$builddir"
