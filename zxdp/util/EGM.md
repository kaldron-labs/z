# Zxdp packet utility and fixture

## Overview

Raw Ethernet/IPv4/UDP stop-and-wait packet exercise over Zxdp, with a Linux network-namespace wrapper or caller-configured physical-interface mode.

## Interface

- [zxdppacket.cc](./zxdppacket.cc), `options`: `zxdppacket client|server --interface IFACE --peer-mac MAC --local-ip IP --peer-ip IP`; optional local/peer ports, count, payload, frames, spin, timeout, deadline, and readiness file. `Options` defaults to ports 40000/40001, count 32, payload 128, Rx frames 2, spin 0, timeout 1 ms, and deadline 10 seconds.
- [zxdpfixture.sh](./zxdpfixture.sh): accepts `--output NEW_DIR`, `--utility PATH`, `--count`, `--payload`, and `--mode wait|spin|ll`. `--physical` requires output and forwards arguments after `--` to the utility.
- [Makefile.am](./Makefile.am), `noinst_PROGRAMS`: builds the non-installed `zxdppacket` program and distributes its fixture script.

## Invariants

- `options` requires positive count/frames/deadline, payload at least four bytes, valid IPv4/MAC addresses, and ports 1–65535. `Endpoint::run` requires payload within MTU minus 28 and Rx frames covering interface queues; the Tx pool has two buffers. [zxdppacket.cc](./zxdppacket.cc)
- `Endpoint::transmit` returns the caller allocation lease after successful send; the library retains kernel submission lifetime. `receive` returns buffers even for invalid/unrelated packets. Teardown joins Tx, retries close for up to ten seconds, and asserts retirement before releasing the instance. [zxdppacket.cc](./zxdppacket.cc)
- Default fixture mode requires UID zero and network-namespace privileges, re-execs through `unshare --net`, and refuses to create resources in the namespace of PID 1. Output must be a new directory. Physical mode leaves interface configuration to the caller; kernel operations still require their normal privileges. [zxdpfixture.sh](./zxdpfixture.sh), `ZXDP_FIXTURE_INNER` and `--physical`.

## Side Effects

- `Endpoint::run` opens AF_XDP, exchanges packets with checksummed headers and sequence-pattern payloads, optionally creates a readiness file exclusively with mode 0600, and prints result counters. Server replies XOR payload bytes with `0x5a`; usage exits two, run success/failure exits zero/one. [zxdppacket.cc](./zxdppacket.cc)
- Default fixture mode creates one-queue `zxdpp0`/`zxdpp1` veth peers, addresses and permanent neighbors inside its anonymous namespace; writes snapshots/logs to a 0700 output directory; runs server/client under timeouts; and requires both PASS results. `cleanup` terminates/waits for the server and records the exit status. [zxdpfixture.sh](./zxdpfixture.sh)
- Physical mode records arguments, captures `endpoint.log`, and propagates utility status without configuring links or addresses. [zxdpfixture.sh](./zxdpfixture.sh), `--physical`.

## Verification

From a configured repository root, run `make -j8 && make -C zxdp/test test`. The utility/fixture were inspected but not executed; fixture execution changes network namespace resources and writes output artifacts.
