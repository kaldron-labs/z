# Optimize and Correct Design and Implementation

## Act as a principal software engineer who is the leading global expert in performance-oriented C++ systems and network programming

## Goal

### Review and improve the implementation of `http3_qpack.new.md`.

`Zhttp` and `Zquic` should be a maximally-efficient, highly-performant, scalable, resilient, secure and lightweight HTTP/3 implementation that maximally leverages lower-level `Ztls`, `Zi`, `Ze`, `Zt`, `Zm`, and `Zu` libraries.
- focus on maximally leveraging `Zu*`, `Zm*`, `Zt*` and `Zi*`
  - be extremely skeptical of fixed-size arrays
    - if accompanied by explicitly and separately maintained lengths these should probably be replaced by `ZuArray`/`ZtArray`/`ZtString`/`ZtScratch` etc.
    - if lookup tables, probably `ZmHash`/`ZmLHash` would be more appropriate, with appopriate locking, hash IDs, etc. to permit run-time sizing/tuning
- ensure "sharding", i.e. minimal inter-thread sharing of data
  - dedicated threads + data for Rx and Tx
- be extremely skeptical of any implied or explicit heap allocation, particularly in the hot path
  - ensure minimal use of heap allocation
  - heap allocations should all leverage `ZmHeap` for fixed-size, `ZmVHeap` for variable size, and be identified for run-time telemetry and tuning
  - prefer callbacks with stack-allocated scratch temporaries (using `ZtScratch` and other such) to heap-allocated context
  - ensure `ZmHeap`-optimized buffer management and queue node management
- ensure alignment with `AGENTS.md` guidelines, particularly indentation (tabstop 8, shiftwidth 2, mixed TABs/spaces, Linux kernel style)

## Guidelines

1. USE idiomatic, natural and maximally expressive code:
  - PREFER brevity, expressiveness and often-used idiomatic expressions of the programming language to readability
2. ENSURE threading and sharding - not shared-nothing, but shared-minimal
  - shared data needs protecting
3. EXPERT CODING STYLE - code for veteran engineers who are expert in the language and steeped in its conventions

## Review

SCRUTINIZE the `http3_qpack.new.md` implementation, functions and logic
- FOCUS on recent changes
- DISCOVER:
  - algorithmic inefficiencies
  - performance, latency or throughput impairments
  - ineffective use of `Zu*`, `Zt*`, `Zm*`, `Zi*`, and other dependencies
  - use of heap allocations where stack-allocated scratch with functional-style would be more performant
  - misalignments with goal, guidelines and `AGENTS.md`
  - chained `if` statements that should be `switch`
  - highly nested logic
  - repeated code blocks that are near-identical
  - redundant code that duplicates available capabilities in lower-level libraries
  - historical compatibility code that should be deleted

## Plan

Write a plan `http3_qpack.iterate.md` to:
1. UNIFY repeated code/logic by factoring out common code:
  - move common code to the most appropriate source file based on the structure and conventions of the existing codebase
2. SIMPLIFY redundant/complex logic such as:
  - chained `if` statements that should be `switch`
  - highly nested logic
  - repeated code blocks that are near-identical
  - redundant code that duplicates available capabilities in lower-level libraries
  - "DRY" principle violations
3. DELETE legacy and redundant code that is no longer needed
4. ensure test coverage of the new code
5. correct any regressions introduced
