#!/usr/bin/env bash
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details).
set -euo pipefail
[[ $(readlink /proc/self/ns/net) != $(readlink /proc/1/ns/net) ]]
# Run only inside a fresh anonymous network namespace; all links die with it.
# The packet/mapping fixtures require loopback, a two-queue and a one-queue pair.
ip link set lo up
ip link add zpkta numrxqueues 2 numtxqueues 2 type veth \
  peer name zpktb numrxqueues 2 numtxqueues 2
ip link set zpkta up
ip link set zpktb up
# Two experimental EtherTypes steer the AF_XDP wake fixture to distinct queues.
if command -v tc >/dev/null && tc qdisc add dev zpktb clsact &&
   tc filter add dev zpktb egress protocol 0x88b5 matchall action skbedit queue_mapping 0 &&
   tc filter add dev zpktb egress protocol 0x88b6 matchall action skbedit queue_mapping 1; then
  export ZPKT_TEST_QUEUE_STEERING=1
fi
ip link add zpktdummy type dummy
ip link add zpktrxa numrxqueues 1 numtxqueues 1 type veth \
  peer name zpktrxb numrxqueues 1 numtxqueues 1
ip link set zpktrxa up
ip link set zpktrxb up
export ZPKT_TEST_IFINDEX="$(ip -o link show dev zpkta | cut -d: -f1)"
export ZPKT_TEST_PEER_IFINDEX="$(ip -o link show dev zpktb | cut -d: -f1)"
export ZPKT_TEST_GENERIC_IFINDEX="$(ip -o link show dev zpktdummy | cut -d: -f1)"
export ZPKT_RX_TEST_IFINDEX="$(ip -o link show dev zpktrxa | cut -d: -f1)"
export ZPKT_RX_TEST_PEER_IFINDEX="$(ip -o link show dev zpktrxb | cut -d: -f1)"
export ZPKT_RUN_FIXTURE=1
exec "$@"
