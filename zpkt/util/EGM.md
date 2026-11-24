# Zpkt packet utility and fixture

## Overview

Raw Ethernet/IPv4/UDP stop-and-wait packet exercise over Zpkt, with a Linux network-namespace wrapper or caller-configured physical-interface mode.

## Interface

- [zpktpacket.cc](./zpktpacket.cc), `options`: `zpktpacket client|server --interface IFACE --peer-mac MAC --local-ip IP --peer-ip IP`; optional local/peer ports, count, payload, frames, spin, timeout, deadline, and readiness file. `Options` defaults to ports 40000/40001, count 32, payload 128, Rx frames 2, spin 0, timeout 1 ms, and deadline 10 seconds.
- [zpktfixture.sh](./zpktfixture.sh): accepts `--output NEW_DIR`, `--utility PATH`, `--count`, `--payload`, and `--mode wait|spin|ll`. `--physical` requires output and forwards arguments after `--` to the utility.
- [Makefile.am](./Makefile.am), `noinst_PROGRAMS`: builds the non-installed `zpktpacket` program and distributes its fixture script.

## Invariants

- `options` requires positive count/frames/deadline, payload at least four bytes, valid IPv4/MAC addresses, and ports 1–65535. `Endpoint::run` requires payload within MTU minus 28 and Rx frames covering interface queues; the Tx pool has two buffers. [zpktpacket.cc](./zpktpacket.cc)
- `Endpoint::transmit` returns the caller allocation lease after successful send; the library retains kernel submission lifetime. `receive` returns buffers even for invalid/unrelated packets. Teardown joins Tx, stops XDP and returns retained backing before finalization. [zpktpacket.cc](./zpktpacket.cc)
- Default fixture mode requires UID zero and network-namespace privileges, re-execs through `unshare --net`, and refuses to create resources in the namespace of PID 1. Output must be a new directory. Physical mode leaves interface configuration to the caller; kernel operations still require their normal privileges. [zpktfixture.sh](./zpktfixture.sh), `ZPKT_FIXTURE_INNER` and `--physical`.

## Side Effects

- `Endpoint::run` opens AF_XDP, exchanges packets with checksummed headers and sequence-pattern payloads, optionally creates a readiness file exclusively with mode 0600, and prints result counters. Server replies XOR payload bytes with `0x5a`; usage exits two, run success/failure exits zero/one. [zpktpacket.cc](./zpktpacket.cc)
- Default fixture mode creates one-queue `zpktp0`/`zpktp1` veth peers, addresses and permanent neighbors inside its anonymous namespace; writes snapshots/logs to a 0700 output directory; runs server/client under timeouts; and requires both PASS results. `cleanup` terminates/waits for the server and records the exit status. [zpktfixture.sh](./zpktfixture.sh)
- Physical mode records arguments, captures `endpoint.log`, and propagates utility status without configuring links or addresses. [zpktfixture.sh](./zpktfixture.sh), `--physical`.

## Verification

From a configured repository root, run `make -j8 && make -C zpkt/test test`. The utility/fixture were inspected but not executed; fixture execution changes network namespace resources and writes output artifacts.
