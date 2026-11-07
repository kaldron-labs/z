# Goal

Execute Phases 1-18 (see below), exploiting concurrency (see below).

IMPORTANT use clang debug only; asan, lsan, gcc, mingw, arm are all out of scope

IMPORTANT re-use builds, don't `make clean` unnecessarily

IMPORTANT batch rebuilds, they are expensive

IMPORTANT use subagents

IMPORTANT use a make jobserver with concurrency: 8 across all subagents

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

# Phase 5 - depends on Phases 2, 3

1. build and run test suites for `zfix` (`make -C .../test test`)
  - root cause diagnose and repair any defects

# Phase 6 - depends on Phase 4

1. build and run test suites for `zhttp` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. build and run the QIR interoperability suite
  - root cause diagnose and repair any defects

IMPORTANT: Phase 6 is frozen once it has completed
- subsequent phases should NOT modify `zhttp`

# Phase 7 - depends on Phase 6

1. build and run test suites for `zws` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 7 is frozen once it has completed
- subsequent phases should NOT modify `zws`

# Phase 8 - depends on Phase 6

1. build and run test suites for `zfb` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 8 is frozen once it has completed
- subsequent phases should NOT modify `zfb`

# Phase 9 - depends on Phase 6

1. build and run test suites for `zpb` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. build and run test suites for `zgrpc` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 9 is frozen once it has completed
- subsequent phases should NOT modify `zpb`, `zgrpc`

# Phase 10 - depends on Phase 7

1. build and run test suites for `zrest` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 10 is frozen once it has completed
- subsequent phases should NOT modify `zrest`

# Phase 11 - depends on Phase 7

1. build and run test suites for `zjrpc` (`make -C .../test test`)
  - root cause diagnose and repair any defects

2. build and run test suites for `zmcp` (`make -C .../test test`)
  - root cause diagnose and repair any defects

IMPORTANT: Phase 11 is frozen once it has completed
- subsequent phases should NOT modify `zjrpc`, `zmcp`

# Phase 12 - depends on Phases 2, 3

1. build and run test suites for `zdb` (`make -C .../test test`)
  - root cause diagnose and repair any defects

# Phase 13 - depends on Phase 12

1. build and run test suites for `zdb_pq`, `zdb_sqlite`, `zdf`
  - root cause diagnose and repair any defects
  - **these modules can be built/tested concurrently**

IMPORTANT: Phase 12, 13 is frozen once they have completed
- subsequent phases should NOT modify `zdb`, `zdb_pq`, `zdb_sqlite`, `zdf`

# Phase 14 - depends on Phases 2, 3

1. build and run test suites for `zrl`,
  - root cause diagnose and repair any defects

# Phase 15 - depends on Phase 13

1. build and run test suites for `ztc`
  - root cause diagnose and repair any defects

IMPORTANT: Phase 15 is frozen once it has completed
- subsequent phases should NOT modify `ztc`

# Phase 16 - depends on Phase 15

1. build and run test suites for `zum`
  - root cause diagnose and repair any defects

# Phase 17 - depends on phases 2, 3

1. build and run test suites for `zgtk`
  - root cause diagnose and repair any defects

# Phase 18 - depends on phases 16, 17

1. build and run test suites for `zdash`
  - root cause diagnose and repair any defects

# Phase Concurrency

- Phases 2, 3 can be concurrent
- Phases 4, 5 can be concurrent
- Phases 7, 8, 9 can be concurrent
- Phases 10, 11 can be concurrent
- Phase 12, 14, 17 can be concurrent with any phases that follow 2 and 3
- Phase 13 can build its modules concurrently
