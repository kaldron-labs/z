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
