#!/bin/sh
set -eu

srcdir=${1:?source directory required}
builddir=${2:?build directory required}
. "$srcdir/zi/itest/zi-test-residue.sh"
zi_residue_init zhttp-qir-script-test

tmp=$ZI_RESIDUE_DIR
cleanup()
{
  status=$?
  zi_residue_finish "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

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
grep -q 'logs/a/log.txt' "$tmp/report.md"
grep -q 'stderr' "$tmp/report.md"

"$srcdir/scripts/qir-report" \
  --in "$srcdir/zhttp/interop/testdata/qir-results/fixture" \
  --out "$tmp/fixture-report.md"
grep -q 'fixture-kernel' "$tmp/fixture-report.md"
grep -q '| `zquic` | `ngtcp2` | `handshake` | 0 |' \
  "$tmp/fixture-report.md"
grep -q '| `zquic` | `ngtcp2` | `transfer` | 0 |' \
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
grep -q 'docker_version=' "$tmp/run/metadata.env"
"$srcdir/scripts/qir-report" \
  --in "$tmp/run" \
  --out "$tmp/run-report.md"
grep -q '| `zquic` | `ngtcp2` | `handshake` | 0 |' "$tmp/run-report.md"
grep -q '| `zquic` | `ngtcp2` | `transfer` | 0 |' "$tmp/run-report.md"
grep -q '| `zquic` | `ngtcp2` | `http3` | 0 |' "$tmp/run-report.md"
grep -q '| `ngtcp2` | `zquic` | `handshake` | 0 |' "$tmp/run-report.md"
grep -q '| `ngtcp2` | `zquic` | `transfer` | 0 |' "$tmp/run-report.md"
grep -q '| `ngtcp2` | `zquic` | `http3` | 0 |' "$tmp/run-report.md"

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
