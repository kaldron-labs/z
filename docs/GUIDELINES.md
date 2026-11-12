# Z framework engineering principles

Part of the repository guidance mapped in [AGENTS.md](AGENTS.md).
Read before code changes; consult the map for task-specific rules.

**Act as a principal software engineer who is a leading global expert
in performance-oriented low-latency C++ systems and network programming.**

## Target systems
- compilers: current gcc, clang
- CPU: x64 intel/amd, ARM
- OS: linux, windows

## General principles
- time-to-market / engineering velocity is less important than:
  - design/implementation integrity
  - run-time performance
- latency is more important than throughput
- copying and heap memory allocations are minimized
  - where heap allocations are necessary, they use tunable heaps, except for
    objects managed by `ZmSingleton` or `ZmSpecific`
- dependent compatibility is a non-goal unless otherwise directed
  - propagate breaking API changes to dependent code
  - do not use shims, forwarders or other such techniques for legacy compatibility purposes
- software must be capable of running 24x7 indefinitely without process kill
  - prohibit monotonically growing memory consumption
  - all long-lived containers must be actively garbage collected
- shutdown/teardown must be graceful and clean of leaks
- no background garbage collection or scanning for GC
  - garbage collection must be deterministic, timely and not timer-based
- intrusive reference counting and container nodes
- sharding:
  - controlled thread creation
  - threads dedicated to specific work and thread-affined data
  - fast inter-thread communication with ring buffers
- asynchronous continuation-based functional style

### No dogma
- immutability
  - pervasive immutability is a non-goal
  - mutability is encouraged if it benefits performance
    - example: in-place encrypt plaintext, decrypt ciphertext for TLS
    - example: in-place decoding of strings in JSON parsing
- type punning and undefined behavior
  - avoiding UB that is purely theoretical or limited to non-targeted systems is a non-goal
  - UB type punning is endorsed unless it actually creates a **tangible correctness or security risk** for targeted systems
- not header-only
  - **intentionally** not header-only
  - Z libraries are traditional hybrid builds
    - headers with accompanying binary versioned shared libraries / DLLs
- intrusive + intrinsic
  - non-intrusive or extrinsic storage is a non-goal (no STL containers)
- sentinel values
  - sentinel values for logical null or false, not extrinsic booleans
- Postel's Law, unabashedly
  - “Be conservative in what you send, be liberal in what you accept.”
  - conformance policing is a non-goal
  - elide all excessive correctness and validation in favor of performance
  - this principle applied to all protocols and formats for maximum resilience
