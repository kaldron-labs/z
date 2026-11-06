# Audit flags and fixes

Part of the repository guidance mapped in [AGENTS.md](AGENTS.md).

## Audit flags
- Audit scope: unless a request explicitly narrows it, every audit against these
  flags covers all repository-owned C++ source directories in each in-scope
  module: `src`, `test`, `util`, `itest`, `example`, `bench`, and `interop`
  where present. Inspect both uses and declarations relevant to each flag; do
  not treat `src` as the whole module.

### Repair procedure
- Audit scope is not edit scope. For each repair, identify the requirement or
  observed failure, violated invariant, and owning component. Report unproven
  concerns; do not edit merely to clear a flag.
- Establish the contract before adding validation, references, locks,
  containers, retries, or APIs. Respect caller responsibilities and framework
  guarantees; validate untrusted input at its boundary without rechecking
  states guaranteed by types.
- Make the smallest demonstrated fix. Judge storage by actual bounds and
  workload, and account for added allocations, copies, references, lookups,
  and hot-path work.
- Preserve unrelated code, tests, diagnostics, standard streams, and
  platform-normalizing APIs. Test an agreed invariant or reproduced failure,
  not a speculative new contract.
- Review every changed line for necessity, then verify the intended behavior
  on relevant platforms using the current build configuration. A green build
  does not justify scope expansion. When reverting, remove the named change
  and its direct dependents without disturbing other work.

### Code structure
- Amber Flag: a data-member type is too long to preserve tabular member alignment.
  Problem: the member name and initializer no longer align with adjacent declarations, obscuring the composite layout.
  Fix: introduce a concise local `using` alias, then declare the member with that alias; place the alias immediately before the exposed data it supports.
- Red Flag: exposed data members declared after constructors or other member functions.
  Problem: the composite's layout and dependency-facing state are hidden among behavior, making ownership and storage review unreliable.
  Fix: move exposed `struct` data to the beginning of the type, after only required local aliases.
- Red Flag: declarations nested inside templates, that do not depend on template parameters
  Problem: bloats debug info and linker symbol space with template noise
  Fix: move these declarations out of the enclosing template
- Red Flag: invariant function call in a loop conditional
  Problem: inefficient repeated calling of a function that returns an invariant value
  Fix: use a local variable to cache the value before the loop
- Red Flag: repeated string comparisons in a lookup loop or `ZuUnroll`.
  Problem: each key is inefficiently compared with multiple candidates.
  Fix: use the indexed matching pattern in "Compile-time data and matching".
- Red Flag: repeated invariant array operator use, e.g. `if (x[i] == 'y' || x[i] == 'z') ...`
  Problem: inefficient repeated calling of `operator []`, potential memory contention
  Fix: use a local variable to cache the value: `auto c = x[i]; if (c == 'y' || c == 'z') ...`
- Red Flag: a `constexpr` or `static constexpr` data object declared as
  `ZuSpan`/`ZuCSpan`.
  Problem: a span is a non-owning run-time view and discards the literal extent
  that is available at compile time.
  Fix: use `constexpr auto name = "text"_z` so the object is a `ZuString`.
  For a heterogeneous literal collection, use a `constexpr` C-string-pointer
  array and convert each element to a span only at its use boundary.

### Storage and capacity
- Red Flag: `operator new` overload outside `ZmNHeap`, except for objects
  managed by `ZmSingleton` or `ZmSpecific`
  Problem: fixed-size allocations outside `ZmSingleton` and `ZmSpecific`
  should be trackable with `ZmNHeap`
  Fix: replace other custom overloads with use of `ZmNHeap`
- Red Flag: a module-local object allocator/base class which hides `ZmNHeap` or
  `ZmVHeap` behind inherited `operator new`.
  Problem: it erases the concrete allocation size and heap identity, permits a
  variable-size heap for fixed-size objects, and makes allocation audits miss
  dependents.
  Fix: each fixed-size concrete type inherits its own `ZmNHeap` specialization
  (first base), with the concrete type as its CRTP argument and a
  `friend inline ZuStringT<"..."> ZmNHeapID(Type *);` declaration. Do not declare
  `operator new`/`operator delete` merely to forward to a heap, and do not
  introduce generic `Object`, `ObjectAlloc`, or equivalent allocation bases.
- Red Flag: hard-coded capacities such as `16`.
  Problem: unexplained limits may impair scaling when too low, bloat stack/heap when too high, or leave mostly unused capacity.
  Fix: use prominently located, named library-defined compile-time constants with a maintenance comment: RFC/standard mandate, mainstream alignment, or measured scaling/footprint trade-off.
- Red Flag: scanning containers for objects to garbage collect
  Problem: garbage collection should be deterministic and immediate - zombie objects should not linger in containers - their memory should be made available to the recycling block allocator
  Fix: delete such scans and ensure that short-lived objects are deterministically removed from their owners when their state becomes final
- Red Flag: intrusively reference-counted objects as values (including all Z hash tables)
  Problem: intrusively reference-counted objects should only be destroyed by a corresponding smart pointer to prevent multiple destruction / double-free
  Fix: store/pass by reference: replace the object value with the corresponding smart pointer
- Red Flag: disorderly structs
  Problem: disordered members induce unnecessary padding due to alignment
  Fix: strike a balance between organizing members into logical groups and ordering by size from largest-to-smallest; immutable members should still come first, followed by mutable shared members, then groups of thread-exclusive members
  Note: C++ member order is storage layout, while `ZuStruct`/`ZfStruct` metadata is intentionally schema/logical ordering
- Red Flag: cache line contended structs
  Problem: multiple threads contend for data shared in the same cache line
  Fix: begin thread-exclusive groups of data members with `alignas(Zm::CacheLineSize)`
- Amber Flag: fixed-size arrays with arbitrary capacities, especially with separately maintained lengths.
  Problem: capacity is easy to desynchronize, hard to tune, and often either caps scaling or wastes stack/heap.
  Small primitive arrays are appropriate when the prevailing API, ABI, protocol, or test contract guarantees their upper bound (for example, the two FDs returned by `pipe`); do not replace them solely because they are fixed-size.
  Fix: otherwise use `ZuArray`, `ZtArray`, `ZtString`, `ZtScratch`, etc.; enforce any required hard upper limit in code.
- Amber Flag: fixed-size lookup tables.
  Problem: static sizing prevents run-time tuning and often misses required locking/hash-ID integration.
  Fix: use `ZmHash`/`ZmLHash` with appropriate locking and hash IDs.
- Amber Flag: arrays on hot or cold paths without workload-aware storage.
  Problem: the wrong storage choice adds allocation latency, stack pressure, or unused capacity.
  Fix: use `ZtBuiltin` sized for 95-99% of hot-path usage, plain `ZtArray` for cold paths, and `ZtScratch` for scratch hot-path storage with heap fallback.

### Heap allocation
- Red Flag: buffer or queue-node management bypassing optimized Z heap paths.
  Problem: common data-path allocations miss pooling and telemetry.
  Fix: use `ZmNHeap`-optimized buffer and queue-node management.
- Amber Flag: heap allocation in hot paths or for scratch state.
  Problem: allocator latency and contention directly hurt tail latency and throughput.
  Fix: use stack scratch such as `ZtScratch` and pass it through callbacks; I/O buffers are the exception, see [I/O ownership and lifecycle](IO.md).
- Red Flag: heap allocation without `ZmNHeap`, `ZmVHeap`, or `ZmNHeapID`,
  except for objects managed by `ZmSingleton` or `ZmSpecific`.
  Problem: allocation behavior becomes opaque and loses Z heap telemetry/tuning.
  Fix: use `ZmNHeap` for fixed-size allocations, `ZmVHeap` for variable-size allocations, and identify allocations with `ZmNHeapID`. Objects managed by `ZmSingleton` or `ZmSpecific` are exempt from heap identification.
- Red Flag: a fixed-size object type allocated through `ZmVHeap`.
  Problem: a variable-size heap loses fixed-size allocation telemetry and tuning.
  Fix: use a concrete `ZmNHeap`; reserve `ZmVHeap` for allocations whose size
  genuinely varies at run time.
- Amber Flag: separate allocations for object, refcount, and container nodes.
  Problem: fragmented allocation adds memory overhead and pointer chasing.
  Fix: consolidate with intrusive reference counting and container nodes; see
  `ZmPolyCache` nesting a hash node, list node, and refcount in one allocated
  node.

### Containers and intrusion
- Red Flag: a `ZmList`, `ZmQueue`, `ZmHash`, `ZmRBTree`, `ZmPQueue`, or derive
  macro whose value is `ZmRef<T>` for an application-owned,
  reference-counted `T`.
  Problem: the separate container node adds an allocation, an ownership hop,
  and pointer chasing even though the application object can carry its links.
  Fix: make the application object intrusive in every container it owns. For a
  list owned by `Owner`, expose the allocated node as `using T = Owner::Node`;
  do not use `ZmList<ZmRef<T>>`. Stack intrusive nodes when an object belongs
  to more than one container, using a shadow node for every inner container.
  A `ZmRef<T>` container value is allowed only for genuine shared ownership of
  an object that cannot own that node; document that exception at the
  declaration.

### Data movement and initialization
- Red Flag: calling `ZiLOG` with a lambda that captures pointers or by reference.
  Problem: the logger runs lambdas on a dedicated logger thread at a later time
  Fix: capture by copy the specific data needed for the log trace; use `ZeString` for scratch strings
- Amber Flag: copying string/byte data through temporaries.
  Problem: avoidable memory traffic dominates many encode/decode and crypto paths.
  Fix: operate in place on mutable buffers, or write directly into uninitialized destination storage when that elides a copy; copies are not legitimate in almost all other cases.
- Amber Flag: temporary contiguous copies.
  Problem: hidden allocation/copy costs.
  Fix: allow them only for stack scratch passed to CRTP callbacks or for long-lived heap state that must retain data.
- Amber Flag: default/zero initialization or constructor churn before immediate overwrite.
  Problem: it burns cycles and cache bandwidth for data that will not be read.
  Fix: use uninitialized storage and explicit placement new where appropriate; Z array containers are intentionally uninitialized until filled.

### Algorithm and control flow
- Red Flag: near-identical repeated blocks.
  Problem: duplication hides divergent fixes and violates DRY.
  Fix: factor common code with templates, CRTP, or local helpers that preserve performance.
- Red Flag: mistakenly assuming that `ZmScheduler` `invoke` or `run` is blocking
  Problem: reading of results before work has been executed
  Fix: read results and execute followon code in a continuation of the posted function, not after the call to `run`/`invoke`
- Red Flag: carrying saga-step validity through local variables or continuation captures.
  Problem: a failed prerequisite can schedule needless work or reach a write path before the step fails.
  Fix: at the continuation that establishes each prerequisite, call `complete(false)` and return immediately; only schedule the next operation after success.
- Red Flag: polling, blocking manually
  Problem: inefficient, introduces stochastic delays
  Fix: use `ZmBlock` or (if not a good fit) `ZmSemaphore`
- Red Flag: tests relying on time intervals for completion of concurrent work
  Problem: non-deterministic, leads to "flaky" tests
  Fix: use `ZmBlock` or (if not a good fit) `ZmSemaphore`
- Red Flag: incorrect blocking using `ZmBlock` or `ZmSemaphore`
  Problem: risks deadlock, causes latency hiccups
  Fix: post work and use asynchronous continuations, do not block on synchronous returns
- Red Flag: container iterators whose lifetime extends beyond the logical traversal
  Problem: iterators may hold locks, pin container state, or obscure later code that mutates or clears the container
  Fix: limit iterator scope with a block; finish traversal before follow-on state changes, cleanup, callbacks, or cross-thread posts that do not require the iterator
- Red Flag: snapshotting a container count before iterating when the count is meant to describe traversed elements
  Problem: the snapshot can drift from actual iteration semantics and becomes wrong if filtering, tombstones, mutation, or callback side effects are introduced
  Fix: initialize the count to zero and increment it while successfully visiting each element
- Amber Flag: needless local aliases for member containers immediately before iteration
  Problem: aliases obscure ownership/lifetime and can hide stale snapshots or lock-holding iterator lifetimes
  Fix: iterate the member container directly unless the local variable transfers ownership or materially shortens a complex expression
- Amber Flag: redundant null checks on values obtained from iterator conditions such as `while (Ref ref = i.val())`
  Problem: the loop condition already proves the value is non-null, and the extra branch hides the traversal invariant
  Fix: remove the redundant check; if null values are valid elements, use an explicit loop shape that documents that invariant
- Amber Flag: algorithmic inefficiency, latency regression, throughput impairment, or avoidable work.
  Problem: small local costs often become system-level throughput or tail-latency limits.
  Fix: choose the lower-complexity or lower-allocation design and validate hot paths.
- Amber Flag: chained `if` statements that should be `switch`.
  Problem: intent and dispatch shape are harder for both readers and compilers to see.
  Fix: use `switch` when branching on one discrete value.
- Amber Flag: highly nested logic.
  Problem: state and error handling become difficult to audit.
  Fix: flatten control flow with early exits, helper functions, or clearer state transitions.
- Red Flag: hand-rolled hash functions, particularly using `ZuHash_FNV` directly and inappropriately
  Problem: redundant over-engineering
  Fix: use Z's built-in hash functions, e.g. `ZuSpan::hash`; XOR the `uint32_t` hash codes to hash together multiple data members.

### Framework fit
- Red Flag: reimplementing lower-level Z Framework capabilities.
  Problem: duplicate code misses established semantics, optimizations, and maintenance paths.
  Fix: use the existing foundational libraries: `zu`, `zm`, `zt`, `zf`, `ze`, `zi`
- Amber Flag: direct use of `FILE`, `syslog`, etc.
  Problem: it bypasses Z I/O and logging conventions.
  Fix: use `ZiFile`, `ZiLog`, etc.
- Amber Flag: `*printf` varargs formatting.
  Problem: varargs are weakly typed and bypass Z formatting conventions.
  Fix: use Z framework types with `<<`, `ZuBox`, and `ZuFmt`.
- Amber Flag: separate `bool` flags for unset, uninitialized, or null state.
  Problem: extra flags can diverge from the value they describe.
  Fix: use sentinel values.

### Dynamic modules
- Red Flag: a loadable Z component that does not use the canonical `ZiModule`
  factory/interface pattern.
  Fix: use the `ZiModule` contract under “Leveraging Key Z Framework
  Capabilities”.
- Red Flag: unloading a dynamically loaded Z component after its factory has
  returned an object or registered callbacks.
  Fix: leave it resident through process teardown; unload only before invoking
  the factory.

### Bad casting
- Red Flag: unnecessary casts.
  Problem: casts hide type-system mistakes and make ownership/aliasing harder to audit.
  Fix: rely on existing Z conversions and fix the type boundary.
- Red Flag: casts among char-equivalent pointers.
  Problem: most Z types already convert equivalent primitive element types automatically.
  Fix: remove the cast unless a real representation change is required.
- Amber Flag: `reinterpret_cast` used to initialize or convert span, string or array data
  Problem: probably unnecessary cast, obfuscates code
  Fix: check if direct-construction, implicit conversion or assignment of the type can be used
- Red Flag: casts to CRTP `impl()`/`app()` bases.
  Problem: they obscure name lookup and static dispatch.
  Fix: use `using T::function;` in bases that need constrained function lookup.

### Bad code structure
- Red Flag: accessing private `m_`-prefixed data members via `->` or `.` (other than `this` and other pointers/references to the same containing type)
  Problem: violates encapsulation
  Fix: either provide a narrow accessor member function, or migrate the exposed data members into either a `...Data` base struct or similar
- Red Flag: unnecessary chained-`if` or `switch` mapping between `enum` values and integers
  Problem: inefficiency
  Fix: use `enum` values directly as integers
- Amber Flag: `this->m_...`
  Problem: unnecessary decoration of what should be plain `m_...`
  Fix: delete `this->` unless plain `m_...` is ambiguous, but if `m_...` is in a template base via protected inheritance, prefer `using Base::m_...` once to adorning `m_...` with `this->` at every reference site

### Scope and cleanup
- Red Flag: code that does not align with the goal, these guidelines, or `AGENTS.md`.
  Problem: local changes can erode project architecture and review expectations.
  Fix: realign the change or call out the required exception explicitly.
- Red Flag: dead code or historical compatibility code.
  Problem: unreachable or obsolete executable paths, unused declarations, and
  obsolete compatibility branches increase audit, test, and maintenance burden.
  Fix: delete them unless compatibility is explicitly required. Deliberately
  commented-out code, especially diagnostic `ZiLOG` probes kept for future
  troubleshooting, is not dead code merely because it does not compile or run
  in the current build. Preserve it unless there is specific evidence that it
  is obsolete or its removal is requested.
- Red Flag: short-lived objects holding reference counts to longer-lived owners, either directly or indirectly via callback lambda captures
  Problem: causes reference-count churn in the owner and risks ownership cycles
  Fix: short-lived objects should use raw pointers back to longer-lived owners; owners must teardown carefully to ensure that they cannot be outlived by short-lived objects that they own; owners should not attempt to delete owned objects in their destructors, they should instead assert that no such objects remain

### Unnecessary atomic operations, locking and copies
- Red Flag: diagnostic, statistics or telemetry data (e.g. counters) without any requirement for accuracy or stable reads are needlessly atomic or guarded by locks or snapshotted
  Problem: atomic operations and locking induce latency volatility
  Fix: use primitive types, do not guard with locks, permit unclean reads from other threads

### Liveness
- Red Flag: potentially long-running loops or container iterations in threads that service mixed workloads, particularly I/O threads
  Problem: under load, pending work in the scheduler queue will be starved by a looping turn that does not return to the scheduler
  Fix: cap the work performed in each turn - batch it and post continuations for the remainder
