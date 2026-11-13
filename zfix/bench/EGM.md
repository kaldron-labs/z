# Zfix benchmarks and observers

## Overview

Codec/dispatch/body microbenchmarks, paired native/QuickFIX workload orchestration, retained-result comparison, and optional Linux/glibc allocation/call observers.

## Interface

- [codec.hh](./codec.hh), [nativecodec.cc](./nativecodec.cc), and [quickfixcodec.cc](./quickfixcodec.cc): framed encode/decode timings and clock-overhead quantiles.
- [dispatchcodec.cc](./dispatchcodec.cc), [binarycodec.cc](./binarycodec.cc), and [txbodycodec.cc](./txbodycodec.cc): selected dispatch modes, binary/text body phases, and dense/sparse immutable-body encoding.
- [load.py](./load.py): peer pairings, TCP/TLS, scalar/group/text/fragmentation/replay/overload profiles, repetitions, workload/retention settings, local or remote client operation.
- [results.py](./results.py): retained-case comparison and host-specific latency budgets. [heaptrace.cc](./heaptrace.cc) and [audittrace.cc](./audittrace.cc): optional diagnostic shared libraries.
- [Makefile.am](./Makefile.am) builds non-installed benchmarks; QuickFIX is conditional. Its codec-bench target runs native and optional QuickFIX; test is empty.

## Invariants

- Microbenchmarks separate warmup from measured samples and report clock overhead. Native and QuickFIX codecs include different ownership/allocation work; binary-body tests exclude framing/transport/signing, while TxBody timings release the prior body outside the measured interval. See [codec.hh](./codec.hh), [binarycodec.cc](./binarycodec.cc), and [txbodycodec.cc](./txbodycodec.cc).
- The local load harness requires POSIX wait4, an empty artifact root, a dictionary matching quickfix.lock, and metadata for all four peer binaries even for native-only selection. Cases validate completion/counts and relay evidence. Remote mode leaves server lifecycle/resources/identity outside its control. [load.py](./load.py)
- Diagnostic observers alter execution and do not measure all allocation/copy paths. Heap interposition counts dynamic glibc calls; audit hooks count selected PLT calls and can count nested copies. Diagnostic reruns are separate from timing comparisons. [heaptrace.cc](./heaptrace.cc), [audittrace.cc](./audittrace.cc), [load.py](./load.py)
- Result comparison requires successful case coverage and matching provenance/workload evidence, excludes diagnostic runs, and compares existing quantiles rather than merging them. Optional codec inputs have weaker provenance checks. [results.py](./results.py)

## Side Effects

- Benchmarks allocate workload buffers, execute repeated encode/decode operations, and print JSON metrics. [codec.hh](./codec.hh)
- load.py invokes metadata tools and launches server/client/optional proxy process groups, opens sockets, can generate TLS credentials, and retains manifests, logs, JSONL, resource usage and summaries. Cleanup terminates/reaps child groups. Observer reruns set LD_PRELOAD or LD_AUDIT and write diagnostic JSON. [load.py](./load.py)
- results.py writes Markdown, comparison JSON, and budgets.json derived from host results; it does not establish portable performance guarantees. [results.py](./results.py)
- Saved results and diagnostic archives under results/ are excluded from this contract bootstrap.

## Verification

From a configured repository root, run `make -j8 && make -C zfix test`. For intentional timing runs, `make -C zfix/bench codec-bench` runs the configured codecs; load.py additionally needs pinned peer artifacts and a fresh output directory. Commands were not run for this documentation change.
