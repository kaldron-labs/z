#!/usr/bin/env bash
# No implicit configure/build, no physical-interface changes, no numeric perf gate.
set -euo pipefail
[[ $# == 3 ]]
binary=$(realpath "$1")
out=$(realpath -m "$2")
label=$3
[[ $label == check || $label == release || $label == pg ]]
[[ -x $binary ]]
script=$(cd "$(dirname "$0")" && pwd)
[[ ! -e $out && ! -L $out ]]
mkdir -p "$out"
# All overrides apply to BOTH transport selections. Pin explicit available CPUs.
read -r crx ctx srx stx < <(python3 - <<'PY'
import os
cpus = sorted(os.sched_getaffinity(0))
print(*(cpus[i % len(cpus)] for i in range(4)))
PY
)
crx=${BENCH_CLIENT_RX_CPU:-$crx} ctx=${BENCH_CLIENT_TX_CPU:-$ctx}
srx=${BENCH_SERVER_RX_CPU:-$srx} stx=${BENCH_SERVER_TX_CPU:-$stx}
"$binary" --selftest
for family in ${BENCH_FAMILIES:-4 6}; do
  [[ $family == 4 || $family == 6 ]]
  for backend in ${BENCH_BACKENDS:-kernel native}; do
    case $backend in kernel|native|native-client|native-server) ;; *) exit 2 ;; esac
    # The caller supplies privileges; only this newly unshared topology is changed.
    unshare -n -- bash "$script/private-veth.sh" "$binary" "$backend" "$family" \
      "$out/$family-$backend" "$crx" "$ctx" "$srx" "$stx" \
      "${BENCH_MESSAGES:-10000}" "${BENCH_WARMUP:-1000}" "${BENCH_PIPELINE:-1}" \
      "${BENCH_CONNECTIONS:-1}" "${BENCH_RESERVE:-4096}" "${BENCH_SPIN:-1000}" \
      "$label" "${BENCH_CAPTURE:-0}"
  done
done
