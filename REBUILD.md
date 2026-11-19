# Goal

Execute Phases 1-20 (see below), exploiting concurrency (see below).

IMPORTANT use current build configuration only; other build configurations are out of scope
- skip modules that would be skipped by top-level `Makefile.am`
  - example: if postgres is not enabled, skip `zdb_pq`

IMPORTANT re-use builds, don't `make clean` unnecessarily

IMPORTANT batch rebuilds, they are expensive

IMPORTANT use subagents

IMPORTANT use a make jobserver with concurrency: 8 across all subagents

IMPORTANT keep `/tmp` clean, remove residue once each phase is complete

IMPORTANT keep worktree clean, remove test and crash residue once each phase is complete

IMPORTANT you have authorization for full access to network and system outside sandbox
- run listeners and other code outside sandbox
- run debugging and ptrace outside sandbox
- work autonomously, do **not** prompt interactively

IMPORTANT use valgrind for debugging memory corruption
- if valgrind is needed, run it outside the sandbox

# Phase 1

1. build `zu`, `zm`, `zt`, `ze`, `zf`, `zi`, `zv`

2. run test suites for `zu`, `zm`, `zt`, `ze`, `zf`, `zi`, `zv` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 1 is frozen once it has completed
- subsequent phases should NOT modify `zu`, `zm`, `zt`, `ze`, `zf`, `zi`, `zv`

# Phase 2 - depends on Phase 1

1. build and run test suites for `ztcp` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 2 is frozen once it has completed
- subsequent phases should NOT modify `ztcp`

# Phase 3 - depends on Phase 1

1. build and run test suites for `zdbus` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. build and run test suites for `ztls` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 3 is frozen once it has completed
- subsequent phases should NOT modify `zdbus`, `ztls`

# Phase 4 - depends on Phases 2, 3

1. build and run test suites for `zquic` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 4 is frozen once it has completed
- subsequent phases should NOT modify `zquic`

# Phase 5 - depends on Phase 1 (Linux only)

1. build `zxdp` and run its unit tests (`make -C zxdp/test test`)
2. run the [Zxdp integration tests](zxdp/itest/README.md) with the documented
   private network fixtures and include DPDK coverage when enabled
   - root cause diagnose and repair any defects

IMPORTANT: Phase 5 is frozen once it has completed
- subsequent phases should NOT modify `zxdp`

# Phase 6 - depends on Phases 2, 3, 5 (Linux only)

1. build `zutcp` and run its unit tests (`make -C zutcp/test test`)
2. run the [Zutcp integration tests](zutcp/itest/README.md) with the documented
   private network fixtures, including the Ztcp and Ztls application drivers
   and DPDK coverage when enabled
3. run the [independent kernel TCP interoperability suite](zutcp/interop/README.md)
   - root cause diagnose and repair any defects

# Phase 7 - depends on Phases 2, 3, and Phase 6 when enabled

1. build and run test suites for `zfix` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. on Linux, build `zfixbench` (`make -C zfix/bench zfixbench`) and compare
   kernel TCP and AF_XDP, plus DPDK when enabled in the current build
   configuration. Use the [Zfix benchmark instructions](zfix/bench/README.md#managed-plain-fix-over-tcp).

# Phase 8 - depends on Phase 4

1. build and run test suites for `zhttp` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. build and run the QIR interoperability suite
  - root cause diagnose and repair any defects

IMPORTANT: Phase 8 is frozen once it has completed
- subsequent phases should NOT modify `zhttp`

# Phase 9 - depends on Phase 8

1. build and run test suites for `zws` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 9 is frozen once it has completed
- subsequent phases should NOT modify `zws`

# Phase 10 - depends on Phase 8

1. build and run test suites for `zfb` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 10 is frozen once it has completed
- subsequent phases should NOT modify `zfb`

# Phase 11 - depends on Phase 8

1. build and run test suites for `zpb` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. build and run test suites for `zgrpc` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 11 is frozen once it has completed
- subsequent phases should NOT modify `zpb`, `zgrpc`

# Phase 12 - depends on Phase 9

1. build and run test suites for `zrest` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 12 is frozen once it has completed
- subsequent phases should NOT modify `zrest`

# Phase 13 - depends on Phase 9

1. build and run test suites for `zjrpc` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. build and run test suites for `zmcp` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 13 is frozen once it has completed
- subsequent phases should NOT modify `zjrpc`, `zmcp`

# Phase 14 - depends on Phases 2, 3

1. build and run test suites for `zdb` (`make -C .../test test`)
  - root cause diagnose and repair any defects

# Phase 15 - depends on Phase 14

1. build and run test suites for `zdb_pq`, `zdb_sqlite`, `zdf`
  - root cause diagnose and repair any defects
  - **these modules can be built/tested concurrently**

IMPORTANT: Phase 14, 15 is frozen once they have completed
- subsequent phases should NOT modify `zdb`, `zdb_pq`, `zdb_sqlite`, `zdf`

# Phase 16 - depends on Phases 2, 3

1. build and run test suites for `zrl`,
  - root cause diagnose and repair any defects

# Phase 17 - depends on Phase 15

1. build and run test suites for `ztc`
  - root cause diagnose and repair any defects

IMPORTANT: Phase 17 is frozen once it has completed
- subsequent phases should NOT modify `ztc`

# Phase 18 - depends on Phase 17

1. build and run test suites for `zum`
  - root cause diagnose and repair any defects

# Phase 19 - depends on phases 2, 3

1. build and run test suites for `zgtk`
  - root cause diagnose and repair any defects

# Phase 20 - depends on phases 18, 19

1. build and run test suites for `zdash`
  - root cause diagnose and repair any defects

# Phase Concurrency

- Phases 2, 3, and 5 can run concurrently after Phase 1
- Phase 4 can run alongside Phases 6 and 7 once each phase's dependencies are met
- Phases 9, 10, 11 can be concurrent
- Phases 12, 13 can be concurrent
- Phases 14, 16, 19 can be concurrent with any phases that follow 2 and 3
- Phase 15 can build its modules concurrently
