#!/usr/bin/env bash
# The same application/corpus/shape/settings are used for each kernel/native pair.
set -euo pipefail
[[ $# == 3 ]]
binary=$(realpath "$1")
out=$(realpath -m "$2")
label=$3
[[ $label == check || $label == release || $label == pg ]]
[[ ! -e $out && ! -L $out ]]
mkdir -p "$out"
script=$(cd "$(dirname "$0")" && pwd)
for workload in ${BENCH_WORKLOADS:-order marketdata updates}; do
  case $workload in order|marketdata|updates) ;; *) exit 2 ;; esac
  for shape in ${BENCH_SHAPES:-pingpong pipeline sustained burst}; do
    case $shape in
      pingpong) pipeline=1 connections=1 messages=10000 warmup=1000 burst=0 ;;
      pipeline) pipeline=8 connections=4 messages=100000 warmup=1000 burst=0 ;;
      sustained) pipeline=32 connections=4 messages=250000 warmup=1000 burst=0 ;;
      burst) pipeline=16 connections=4 messages=100000 warmup=1024 burst=16 ;;
      *) exit 2 ;;
    esac
    env BENCH_WORKLOAD="$workload" BENCH_PIPELINE="$pipeline" \
      BENCH_CONNECTIONS="$connections" BENCH_MESSAGES="${BENCH_MESSAGES:-$messages}" \
      BENCH_WARMUP="${BENCH_WARMUP:-$warmup}" BENCH_BURST_SIZE="$burst" \
      BENCH_BURST_US="${BENCH_BURST_US:-1000}" \
      bash "$script/run-compare.sh" "$binary" "$out/$workload-$shape" "$label"
  done
done
