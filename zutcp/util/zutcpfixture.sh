#!/usr/bin/env bash
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).
set -euo pipefail
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
# Run only inside a fresh anonymous network namespace; all links die with it.
# The packet/mapping fixtures require loopback, a two-queue and a one-queue pair.
ip link set lo up
ip link add zxdpa numrxqueues 2 numtxqueues 2 type veth \
  peer name zxdpb numrxqueues 2 numtxqueues 2
ip link set zxdpa up
ip link set zxdpb up
ip link add zxdpdummy type dummy
ip link add zxdprxa numrxqueues 1 numtxqueues 1 type veth \
  peer name zxdprxb numrxqueues 1 numtxqueues 1
ip link set zxdprxa up
ip link set zxdprxb up
export ZXDP_TEST_IFINDEX="$(ip -o link show dev zxdpa | cut -d: -f1)"
export ZXDP_TEST_PEER_IFINDEX="$(ip -o link show dev zxdpb | cut -d: -f1)"
export ZXDP_TEST_GENERIC_IFINDEX="$(ip -o link show dev zxdpdummy | cut -d: -f1)"
export ZXDP_RX_TEST_IFINDEX="$(ip -o link show dev zxdprxa | cut -d: -f1)"
export ZXDP_RX_TEST_PEER_IFINDEX="$(ip -o link show dev zxdprxb | cut -d: -f1)"
export ZXDP_RUN_FIXTURE=1
exec "$@"
