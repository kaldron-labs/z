# Z framework coding guidelines

**Act as a principal software engineer who is a leading global expert
in performance-oriented low-latency C++ systems and network programming.**

These guidelines extend `AGENTS.md`.
Additional library-specific guidelines may exist in `[directory]/GUIDELINES.md`:
- Example: `zquic/GUIDELINES.md` contains additional guidelines for that library

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
  - avoiding UB that is entirely theoretical or only relevant to non-targeted systems is a non-goal
  - UB and type punning are endorsed unless they actually create a **tangible correctness or security risk** for targeted systems (compilers: gcc, clang; architectures: x64, ARM64)
- not header-only
  - intentionally not header-only
  - Z libraries are traditional hybrid builds
    - headers with accompanying binary versioned shared libraries / DLLs
- intrusive + intrinsic
  - non-intrusive or extrinsic storage is a non-goal (no STL containers)
- sentinel values
  - sentinel values for logical null or false, not extrinsic booleans
- unabashed alignment with Postel's Law
  - “Be conservative in what you send, be liberal in what you accept.”
  - conformance policing is a non-goal
  - elide all excessive correctness and validation in favor of performance
  - this principle applied to all protocols and formats for maximum resilience

## Target systems
- compilers: current gcc, clang
- CPU: x64 intel/amd, ARM
- OS: linux, windows

## Build system

### Module layout
- The Z modules are bundles of libraries and programs
- Every Z module is succinctly named `z*`, e.g. `zu`, `zdb`
- Z libraries scope naming with a corresponding prefix, e.g. `Zu`, `Zdb`
- Every Z module has these required subdirectories:
  - `src`: library and program source code
  - `test`: unit tests using `ZuTest`/`ZuTestUtil` and emitting TAP
- A module may also have these optional subdirectories when it has content of
  the corresponding role:
  - `util`: utility programs and scripts used by `itest`, `interop`, or `bench`
  - `itest`: integration tests using `ZuTest`/`ZuTestUtil` and emitting TAP
  - `example`: dependent programs demonstrating use of the module
  - `bench`: benchmark harnesses
  - `interop`: external interoperability framework testing
- The standard directories are not an exhaustive allow-list. Additional
  module-specific directories are permitted as documented exceptions when a
  distinct facility does not fit a standard role; for example, `zdb_pq/ext`
  contains a PostgreSQL extension, follows PostgreSQL/PGXS naming, and is built
  after the standard directories. Document each exception in the module's
  README or `GUIDELINES.md`, including its naming and build convention.
- `test` is for isolated unit tests. Tests which launch complete programs,
  require multiple services, exercise persistence or networking end to end, or
  require environment setup belong in `itest`. Tests specifically exercising a
  third-party implementation or conformance framework belong in `interop`.
- Repository-owned unit and integration test programs use
  `ZuTest`/`ZuTestUtil`, emit valid TAP, and run from the corresponding
  directory's `make test` target. Shell scripts may be fixtures invoked by a
  ZuTest driver; external harness scripts in `interop` may remain native.
- `util` is independently buildable. `itest` may depend on programs or scripts
  built by `util`; `util` must not depend on `itest`.
- Module build traversal sequence is: `src`, `test`, `util`, `itest`, other
  directories in dependency order
- Headers should be organized as a directed acyclic graph with idempotent
  inclusion guards. Tail inclusion should NOT be used unless there is an
  exceptional reason. Circular depndencies should be avoided by factoring
  out common code (example: `ZmFn_.hh` is factored out from `ZmFn.hh` to
  avoid inducing circular dependencies in dependents).

### Build configuration
- use the `configure` wrapper named `z.config` to reconfigure the build
  - usage: `z.config -h`
- Linux executables and shared libraries use zstd-compressed DWARF at link time
  - requires a linker and debugger with zstd-compressed ELF debug-section support
  - retains full debug information; existing input objects can be reused
- this is a hierarchy of Makefiles that **intentionally** do not automatically rebuild dependencies in other directories
  - use a top-level `make -j8` to ensure all dependencies are rebuilt/refreshed
- add external dependencies in `configure.ac`
  - use `PKG_CHECK_MODULES` if the dependency is in pkg-config, falling back to dependency-specific `m4` if not
- always recompile with `make -j8`
  - if not rebuilding everything, if library code has changed in a layer under development, always rebuild `src` before dependent `test`/`util`/`itest`/`example`/`bench`/`interop`, e.g. `make -C zquic/src -j8`
  - always build default target `all` before `make test`, e.g. `make -C zquic/test -j8 && make -C zquic/test test`
  - after build type has changed (release, debug, asan, etc.) do a top-level `make clean && make -j8` to rebuild
  - release-build verification must be configured through `z.config`; after configuring the release build, run top-level `make clean` and then top-level `make -j8` so all dependent Z libraries and tests are rebuilt consistently
  - do not verify a release build by manually compiling selected modules or by rebuilding only dependent subdirectories; that can mix stale objects, stale generated dependency files, or libraries built under a different build type
- no warnings in build
  - they should be suppressed in the code if false-positive

## Use of C++ language
### Core language
- Compile as GNU C++2b
- DO NOT USE:
  - C++ concepts or `requires`
  - anonymous namespaces in `.cc` files (use file-scope `static`)
  - non-specific capture packs `[&]` or `[=]`
  - `enum class` or `enum T`
- Use advanced C++ where it is expressive and efficient
  - Where C and C++ offer the same facility, prefer the C form:
      - Example: `#include <string.h>`, not `<cstring>`.
      - Where C99 conflicts with C++, use the GNU C++2b form.
- Where possible, do not define forwarding functions to bases, use `using` declarations
- Use `ZuLib.hh` functions in preference to STL, in particular:
  `ZuAssert`, `ZuMv`, `ZuFwd`, `ZuDecay`, `ZuDeref`, `ZuStrip`, `ZuIfT`, `ZuIsConvertible`, `ZuIsConstructible`, `ZuLaunder`, `ZuPun`, `ZuCanOverlap`

### Primitive integer types
- use `int` and plain unadorned `unsigned` as the primary local variable integers
  - the following are static asserted in the foundational Z header `ZuLib.hh`:
    - `sizeof(int) == sizeof(unsigned)`
    - `sizeof(int) >= sizeof(int32_t)`
    - `sizeof(unsigned) >= sizeof(uint32_t)`
- the principle is that `int` and `unsigned` will naturally match the CPU's registers
- where 64bits is required, use `int64_t` and `uint64_t`
- for types that might be stored in high volumes, i.e. where memory pressure may be significant, use the smallest type that spans the required range of values:
  - enums are often `int8_t` (they must be signed)
- do NOT unnecessarily assert, for example checking that a `uint32_t` is `<= UINT_MAX`

### Static polymorphism and constraints
- Prefer CRTP, templates, and compile-time dispatch over virtual polymorphism.
- Use templates and CRTP to factor common code; keep logic DRY without adding weak abstractions.
- Express constraints with SFINAE and detector traits.
  - Keep function return types free of SFINAE expressions; put the test in a trailing `typename = ...` template parameter to avoid encoding it in every symbol.
  - Consolidate overloads that would otherwise become redeclarations with `if constexpr`, preserving their combined participation constraint and result types.
  - Generic hidden friends from different class specializations need a class-specific template discriminator; defaulted type constraints alone do not distinguish their declarations.
  - Prefer `typename = void` plus `decltype(CODE, void())` specializations to test whether `CODE` is well-formed.
  - Do not replace established `ZuIfT`/detector idioms with concepts.
- For optional CRTP callbacks, prefer side-effect-safe base defaults and direct calls such as `impl()->callback(...)`.
  - Defaults should be harmless: `return true`, `return nullptr`, or no-op.
  - Use `if constexpr` traits only when a safe base default cannot express the behavior.
- Control inherited CRTP interfaces with access-changing `using` declarations.
  - Hide a base function with `private: using Base::X;` when the derived class encapsulates it and dependent callers must not access it directly, or when the derived class intentionally replaces it in the public interface with a new function of the same name.
  - Hoist a base function into the public interface with `public: using Base::X;`.

### ADL tagging
- Use friend functions and ADL to tag types and expose compile-time metadata.
  - Example: `friend ZuStructPrint ZuPrintType(DB *)`; `decltype(ZuPrintType(...))` lets `ZuPrint` choose formatting.
  - Example: `ZuFields_(O *, Facet *)` lets `ZuStruct` discover fields.

### Style
- Write idiomatic, natural, maximally expressive code for veteran C++ engineers.
- Prefer brevity, common expert idioms, and precise structure over beginner-oriented readability.
- Do not disdain "Hacker's Delight" style when it is clear, correct, and faster.
- Use shifts for integer multiplication and division by power-of-two literals:
  `x<<3` instead of `x*8`, and `x>>3` instead of `x/8`. For signed division,
  shift only when the value is nonnegative, since rounding otherwise differs.
- Do not put spaces on either side of `/` in division expressions: write `x/y`.
- `auto ptr = ...` not `auto *ptr = ...`
- Strongly prefer `T(u)` to `static_cast<T>(u)` in all cases where they're equivalent

### Portability and compiler features
- Target gcc and clang; use `__GNUC__`, intrinsics, `int128_t`, and `uint128_t` where appropriate.
- Target Linux and Windows via msys2/mingw 64-bit; use `_WIN32` for Windows-specific paths.

### Enums
- do not use `enum class` or `enum T`
  - Z **intentionally** uses enums as fully convertible with integers
    - enum values should be directly usable as compile-time array indexes, etc.
    - if a type is integral and size <= `sizeof(int)`, enum values are used in preference to `static constexpr` constants
  - if the enum needs to be type-aliased by dependents, use `struct E { enum { ... } };`
  - otherwise use `namespace E { enum { ... } };` because this permits dependents to `using namespace E`
  - above `zm` use `ZtEnum`:
    - if an external API type
    - if callers need name lookup at run-time

### Constant literals
- when a constant literal is integral or boolean and does not need to be wider than `int`, use `enum { X = 42 };` in favor of `static constexpr T = 42`
- Declare a compile-time string as `constexpr auto name = "text"_Zu`; do not
  declare a `constexpr ZuSpan`/`ZuCSpan`. `ZuString` preserves the literal's
  extent in its type, whereas a span is a non-owning run-time view. For a
  heterogeneous collection of literals, use a `constexpr` array of C-string
  pointers and construct a span only at the use boundary.

## Use of STL and other dependencies
- minimize use of STL
- use `Zu` alternatives to STL: example: `ZuIfT` instead of `enable_if`
- maximally leverage the Z framework foundation libraries:
  - `zu`, `zm`, `zt`, `zf`, `ze`, `zi`

## Windows
- MSVC/Intel compatibility is a non-goal
- Targets are gcc and clang under MinGW / MSYS2, 64bit only
- do not use the `*W()` variant names, the build is always `wchar_t`-enabled:
  - use `GetTempPath`, not `GetTempPathW`

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
- Red Flag: repeated invariant array operator use, e.g. `if (x[i] == 'y' || x[i] == 'z') ...`
  Problem: inefficient repeated calling of `operator []`, potential memory contention
  Fix: use a local variable to cache the value: `auto c = x[i]; if (c == 'y' || c == 'z') ...`
- Red Flag: a `constexpr` or `static constexpr` data object declared as
  `ZuSpan`/`ZuCSpan`.
  Problem: a span is a non-owning run-time view and discards the literal extent
  that is available at compile time.
  Fix: use `constexpr auto name = "text"_Zu` so the object is a `ZuString`.
  For a heterogeneous literal collection, use a `constexpr` C-string-pointer
  array and convert each element to a span only at its use boundary.

### Storage and capacity
- Red Flag: `operator new` overload outside `ZmHeap`, except for objects
  managed by `ZmSingleton` or `ZmSpecific`
  Problem: fixed-size allocations outside `ZmSingleton` and `ZmSpecific`
  should be trackable with `ZmHeap`
  Fix: replace other custom overloads with use of `ZmHeap`
- Red Flag: a module-local object allocator/base class which hides `ZmHeap` or
  `ZmVHeap` behind inherited `operator new`.
  Problem: it erases the concrete allocation size and heap identity, permits a
  variable-size heap for fixed-size objects, and makes allocation audits miss
  dependents.
  Fix: each fixed-size concrete type inherits its own `ZmHeap` specialization
  (first base), using the established `Type_<Heap = ZuVoid>` pattern when its
  final size is needed: define `using Type =
  Type_<ZmHeap<"...", Type_<>>>` after the template. Do not declare
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
  Note: C++ member order is storage layout, while `ZuStruct`/`ZfStruct`/`ZfbStruct` metadata is intentionally schema/logical ordering
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
  Fix: use `ZmHeap`-optimized buffer and queue-node management.
- Amber Flag: heap allocation in hot paths or for scratch state.
  Problem: allocator latency and contention directly hurt tail latency and throughput.
  Fix: use stack scratch such as `ZtScratch` and pass it through callbacks; I/O buffers are the exception, see the I/O guidance below.
- Red Flag: heap allocation without `ZmHeap`, `ZmVHeap`, or `ZmHeapID`,
  except for objects managed by `ZmSingleton` or `ZmSpecific`.
  Problem: allocation behavior becomes opaque and loses Z heap telemetry/tuning.
  Fix: use `ZmHeap` for fixed-size allocations, `ZmVHeap` for variable-size allocations, and identify allocations with `ZmHeapID`. Objects managed by `ZmSingleton` or `ZmSpecific` are exempt from heap identification.
- Red Flag: a fixed-size object type allocated through `ZmVHeap`.
  Problem: a variable-size heap loses fixed-size allocation telemetry and tuning.
  Fix: use a concrete `ZmHeap`; reserve `ZmVHeap` for allocations whose size
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

## Leveraging Key Z Framework Capabilities

### Assertions
- Assertions:
  - compile-time assertions: use `ZuAssert` (NOT `static_assert`)
  - run-time fatal internal integrity violations that should always abort in both debug and release builds: use `ZmAssert_`
  - run-time assertions that should abort in debug builds but be elided (skipped) in release builds: use `ZmAssert` (1-arg version)
  - run-time assertions with graceful fallback behavior for resilience in release builds:
    - below `zi`: use `ZmAssert` (2-arg version, second arg is fallback code)
    - at or above `zi`: use `ZiAssert` (logs via `ZiLog` in addition to release build fallback)

### Type mechanics
- For complex template aliases, prefer `ZuDerive(x, ([complex template instantiation]))` over `using X = ...`; the explicit type ID reduces compiler/linker symbol lengths and eases debugging.
- Comparisons and sentinels: use `operator *` to detect sentinel null, use `ZuCmp` sentinel logic, and prefer `ZuCmp::cmp` over `operator <=>` because it returns plain `int`.
  - some types have two different sentinel values: zero, and a default-constructed null value
    - `bool operator !()` evaluates "is this false/zero": `!(T(0))` should be `true`
    - `bool operator *()` evaluates "is this non-null": `*(T{})` should be `false`
  - Use `ZuOpBool` to add boolean evaluation to a type with `bool operator !()`
- Traits: `ZuTraits` provides the type traits used to distinguish string types, etc.

### Strings
- Z string types:
  - always cache length
  - all these types normally inter-convert without explicit casts
    - they will automatically `strlen` a passed value if the value is a C string
  - compile-time strings: `ZuString`
  - string spans: `ZuSpan<const char>` aka `ZuCSpan`
    - mutable spans are often used in Z: `ZuSpan<char>`
  - fixed-width strings: `ZuArray<char, N>` aka `ZuCArray<N>`
  - heap-allocated strings:
    - `ZtString`: builtin size, null-terminated
    - `ZtArray`: no builtin size, usually not null-terminated
    - `ZtBuiltin`: `ZtArray` with builtin size
  - bytes:
    - spans: `ZuSpan<const uint8_t>` aka `ZuBSpan`.
    - fixed-width: `ZuArray<uint8_t, N>` aka `ZuBArray<N>`
    - heap-allocated: `ZtArray<uint8_t>` aka `ZtBArray`
  - scratch strings/buffers:
    - `ZtScratch` (macro) - scratch on-stack `ZtArray`/`ZtString` with heap fallback
  - specific-purpose strings:
    - `ZtString` and `ZtArray` can be tagged with a compile-time `ZmHeapID`
    - certain types with specific heap IDs are re-used throughout the framework:
      - `ZeString`: `ZtString` with heap ID for error messages, logs and diagnostics

### Fixed and floating point
- use CPU/GPU device-native types for quantitative analysis
- use `ZuDecimal` for precise decimal calculations of financial values
- use `ZuFixed` for consistent decimal printing/scanning

### Compile-time data and matching
- Use `ZuTypeList` and `ZuSeq` for compile-time tables, associative containers, and sequences.
- Use `ZuSwitch` instead of static lookup tables.
- Use `ZuUnroll` to iterate compile-time type lists and sequences.
- Use `ZuType`, `ZuTypeIndex`, `ZuTypeMap`, `ZuTypeSlice`, etc. to transform type lists and sequences.
- Use `ZuString`/`ZuStringT` for compile-time strings and `ZuStringTL` for compile-time string lists.
- Use `ZuMatcher` for token matching among multiple possibilities; use `==` for a single possibility.

### Storage and lifetime
- Use `ZmHeap` and `ZmVHeap` to adopt a recycling zero-overhead block allocator, with heap identification, telemetry and configurable tuning
  - Use `ZmHeap` for fixed-size concrete types, except objects managed by
    `ZmSingleton` or `ZmSpecific`, which do not require heap identification
    - Always declare the `ZmHeap` type as an alias before using it:
    ```
    class X { ... };
    ```
    becomes:
    ```
    template <typename Heap = ZuVoid>
    class X_ : public Heap { ... };
    ZuDerive(X_Heap, (ZmHeap<"X", X_<>>));
    ZuDerive(X, X_<X_Heap>);
    ```
    - Key properties of the pattern:
      - `Heap = ZuVoid` is defaulted
      - `X_<>` is abbreviated
      - `X_Heap` is aliased
      - `X` is `ZuDerive`d to ensure a new type ID and symbol compression
  - Use `ZmVHeap` for variable-sized dynamic allocations (strings, etc.)
- Use `ZtScratch` for stack scratch with heap fallback; ensure the underlying array has appropriate heap identification.
- Use `ZtBuiltin` for builtin arrays with heap-allocation fallback.
- Use `ZmAlloc` for large single-object stack allocations with heap fallback.
- Use `ZmSpecific` instead of `thread_local`.
- Use `ZmSingleton` for global singletons.
- Intrusive reference-counting:
  - Non-atomic reference-counted thread-affined objects:
    - Use `ZuObject` for non-polymorphic types (no vtbl)
    - Use `ZuPolymorph` for polymorphic types (with vtbl)
  - Atomic reference-counted shared objects:
    - Use `ZmObject` for non-polymorphic types (no vtbl)
    - Use `ZmPolymorph` for polymorphic types (with vtbl)
- Smart pointers:
  - use `ZmRef` for potentially shared objects
  - use `ZuRef` for thread-affined objects

### Formatting
- Use `ZuBox`, `ZuFmt`, and `ZuPrint` for printing.
- Stream directly to `Z*String`, `Z*Array`, and `std::cout`-style outputs.

### Metadata
- Use `ZuStruct`, `ZfStruct`, and `ZfbStruct` for compile-time extract/transform metadata.
- Use metadata integrations instead of ad hoc parsing: JSON `ZfJSON`, ASN.1 `ZfASN1`, CLI `ZfCLI`, CSV `ZfCSV`, URI query `ZfURI`, and FlatBuffers `ZfbStruct`.
- Strongly discourage direct use of the FlatBuffers C/C++ APIs in application and library code.
  - Use `ZfbStruct::save`, `ZfbStruct::ctor`, `ZfbStruct::alloc`, `ZfbStruct::new_`, `ZfbStruct::load`, and `ZfbStruct::update` instead.
  - Direct FlatBuffers API use is reserved for implementing or extending the `Zfb`/`ZfbStruct` integration itself, generated-code boundaries that `ZfbStruct` cannot represent, and explicitly justified verification or reflection work.
  - Do not bypass `ZfbStruct` merely to call generated builders, accessors, object APIs, `Pack`/`UnPack`, or native-table APIs directly.

### Concurrency and sharding
- Use `ZmScheduler` for thread pools.
- Use `ZmScheduler` `isolated` threads for sharding: associate data with specific threads to minimize sharing, locking, and atomics.
- Dedicate threads and their data to independent Rx and Tx in I/O.
- Use `ZmRing` and `ZiRing` for inter-thread and inter-process communication.

### Timers
- `ZmScheduler::Timer` is used for timers
  - each `Timer` instance should be used for a separate individual timer/timeout
  - `Timer`s are intended to be contained by value as data members in owning structs/classes
    - they are referenced by raw pointer from the scheduler
  - `Timer`s must be cancelled with `del` before owner destruction during close/shutdown/stop to prevent stale pointer dereference
#### Timer Teardown
- timer teardown requires a 3-phase process, similar to I/O teardown (see below)
  1. cancel the timer (`ZmScheduler::del`) and set a flag preventing timer callbacks from doing further work
  2. post a teardown continuation on the thread to drain any late callbacks
  3. (in the continuation) complete the teardown with late callbacks drained
- the continuation needs to be posted on the same thread the timer callback would run on
- do NOT block except in the main thread
- as with I/O buffers, timer callbacks are short-lived objects owned by longer-lived objects
  - short-lived objects should hold reference counts back to their owners, this anti-pattern:
    - causes reference-count churn in the owner
    - risks ownership cycles
  - such back-pointers should be raw pointers
  - owners are responsible for draining all activity using orderly teardown as described above, ensuring that objects with stale backpointers cannot outlive their owners

### I/O and system integration
- Use `ZiIOBuf` for buffer management.
- I/O buffers are the exception to "prefer stack over heap": pooled heap buffers are preferred, move between threads by reference without copying, and allow run-time pool tuning.
- Use `ZiLog` for logging.
- Use `ZiFile` for file I/O, `ZiMMapFile` for memory-mapped I/O, and `ZiMultiplex` for network I/O multiplexing.
- Use `ZiEventLoop` for interoperability with other event loops and handle types.

### Dynamic modules
- `ZiModule` loadable Z components use the canonical factory/interface pattern;
  `Zdb_::Store` and `ZdbStore` are the reference implementation.
- Define the component's abstract interface as a `ZmPolymorph` base in its
  installed interface header. The base owns the virtual initialization and
  operational functions that a concrete module implementation fulfills.
- In the interface namespace, declare the factory type as `typedef Base
  *(*FactoryFn)()`. In an `extern "C"` block, declare its C-linkage alias, and
  define a fixed `...FnSym` string for the well-known exported symbol:
  ```cpp
  namespace Component_ {
  class Base : public ZmPolymorph { /* virtual interface */ };
  typedef Base *(*FactoryFn)();
  }
  extern "C" { typedef Component_::FactoryFn ComponentFactoryFn; }
  #define ComponentFactoryFnSym "ComponentFactory"
  ```
- A module declares and exports `ComponentFactory` with `extern "C"`; it
  returns a concrete `Component_::Base` instance. Do not expose function
  tables, opaque contexts, host-mutation callbacks, or bespoke callback
  protocols as a Z component's module interface.
- The host loads the module with `ZiModule`, resolves `...FnSym`,
  `reinterpret_cast`s the result to `FactoryFn`, checks both factory and result,
  then invokes the returned base's virtual initialization and operational
  interface. Module and host consequently share the same compatible Z headers,
  compiler, and C++ runtime ABI.
- Load the module without `ZiModule::GC`. After the factory is invoked, neither
  unload the module nor permit its code to be unloaded: its returned object,
  vtable, or framework-owned callbacks can remain reachable. A local
  `ZiModule` may leave scope because its default finalization drops the handle
  without unloading it. Explicit unloading is allowed only before invoking the
  factory, for example when resolution fails.
- Generic `ZiModule` loader tests that resolve ordinary system-library symbols
  are infrastructure tests, not loadable Z components, and are outside this
  component contract.

### Containers and intrusion
- Use appropriate containers: `ZmHash`, `ZmLHash`, `ZmList`, `ZmRBTree`, `ZmPQueue`, etc.
- Z iterators are usually optionally mutable and can delete while iterating.
- `del()`/`delNode()` usually returns a movable reference to the deleted node/value.
- Stack container intrusions when one application node participates in multiple containers, example: `ZmCache` has nodes that participate in both an LRU `ZmList` and `ZmHash`.
- Use `HeapID<"">` to disable inner-container `ZmHeap` allocation when the full node size is only available at the outermost container.

### Callbacks
- Use `ZmFn` for type-erased lambdas.
- Consolidate captures into a single 64-bit value where possible, e.g. `ZmRef`; use `ZmFn` built-in capture to elide heap allocation and leverage `mvFn`.
- Combine `ZmFn` with `ZiIOFn` and `ZiIOContext` to optimize I/O processing.

### Tests
- Use `ZuTestUtil` and underlying `ZuTest` for TAP-emitting unit tests.
- Use `ZmBlock` or (if not a good fit) `ZmSemaphore` to block on concurrent work
  - do not rely on polling or time intervals

### Tracing and debug logging
- use `ZmBackTrace` for backtracing
  - use `ZmBackTracer` to efficiently capture/log a ring of backtraces
- demangling: below `zm`, use `ZuDemangle`, otherwise use `ZmDemangle`
  - demangling post-processing can be extended with `ZtDemangle`
- use `ZiLOG(Debug, ...)` for debug text logging
- keep module-specific structured trace guidance in module docs

### Persistence
- Use `Zdb` for relational data persistency; use sagas for transactional integrity.

## Code style
### Header layout
- Library headers must follow this skeleton (`[header]`: `ZuString`, `ZhttpQPack`; `[component]`: `Zu`, `Zhttp`):
  ```
  //  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
  //  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

  // (c) Copyright [year] [author]
  // This code is licensed by the MIT license (see LICENSE for details)

  // [title and brief description of header]

  #ifndef [header]_HH
  #define [header]_HH

  #ifndef [component]Lib_HH
  #include <zlib/[component]Lib.hh>
  #endif

  [lowest-level includes, e.g. system library includes]

  [next-lowest-level includes, e.g. Zu includes]

  [next-lowest-level includes, e.g. Zt includes]

  [same-level component includes]

  [body of header]
  ```
- Headers must include all direct dependencies.
- Headers must not include indirect dependencies unless they use related code/definitions.

### Indentation
- Match prevailing style in `{zu,zm,zt,ze,zi}/src/*.{hh,cc}`; when these rules, editor `cino`, and local precedent disagree, code precedent wins.
- Use hard tabs for indentation (`noet`, `ts=8`) and a 2-column logical C++ indent (`sw=2`); do not replace leading tabs with spaces.
- Indent class/struct/function bodies, control-flow bodies, and lambda bodies one logical level.
- Namespaces:
  - Large namespace bodies (>20 lines):
    - not indented
    - closing braces are commented with `// [namespace]`
  - Small namespace bodies (<= 20 lines):
    - indented
    - closing braces are uncommented
- Keep `public:`, `protected:`, and `private:` flush with the class declaration body; indent following members one logical level.
- Keep `friend` declarations flush with the class declaration body, at the top in the default private section.
- Follow local switch style for `case`/`default`; indent statements one logical level from the label.
- For a braced `case`/`default` body, place its terminating `break` after the closing brace on the same line (`} break;`), not inside the braces on a separate line.

### Multiline layout
- Prefer existing visual alignment over mechanical fixed-width continuation indents.
- Split long template parameter lists, inheritance lists, base constructor calls, and function argument lists after natural delimiters.
- Continue nested template/base/member-initializer expressions one logical level deeper than their containing line.
- Align continuation lines with the open construct when nearby code does.
- Function multiline style, with `{` on its own line:
  ```
  template <typename Client>
  int run(
    ZiMultiplex &mx, const Options &options, URL url,
    RequestResult *result = nullptr)
  {
    Client client;
  ```
- Constructor multiline style, with `:` and `{` on their own lines:
  ```
  StoreTbl(
    Store *store, IDString id, unsigned nShards,
    ZfVFieldArray fields, ZfVKeyFieldArray keyFields,
    const reflection::Schema *schema, IOBufAllocFn bufAllocFn)
  :
    m_store{store}, m_id{ZuMv(id)},
    m_fields{ZuMv(fields)}, m_keyFields{ZuMv(keyFields)},
    m_bufAllocFn{ZuMv(bufAllocFn)}
  {
    // introspect fields and flatbuffers reflection data, building
  ```
- Do not deeply indent lambdas; large multiline captures/parameters use this shape:
  ```
  auto lambda = [
    ... /* multiline captures */
  ](
    ... /* multiline parameters */)
  {
    ... /* body */
  };
  ```

### Compact layout and alignment
- Keep short, simple functions and statements on one line when surrounding code does; split only when line length or expression shape makes it clearer.
- Preserve tabular alignment for data members, macro bodies, and compact tables already using tabs to align names, initializers, or comments.
- Right-align members with hard tabs.
- Do not let a verbose member type defeat tabular alignment. Give a complex or long member type a short local `using` alias when that keeps the member declaration aligned; place that alias immediately before the data declarations that use it.
- `*` and `&` go with the member, not the type: `void<TAB>*m_`, not `void *<TAB>m_`.

### Class and struct shape
- `struct`s are all-public data: members are not prefixed with `m_` and appear at the top, before constructors and other function members; required local `using` declarations may precede them.
- Put directly shared, unprefixed data at the top of its composite type before member functions; required local `using` declarations may precede it.
- `class`es have all-private data: members are prefixed with `m_` and appear in a trailing `private:` section at the bottom before closing `};`.
- Do not create hybrid `class` or `struct` types with mixed public/private data members.
- `m_` marks state owned and encapsulated by its class. Only the owning class's implementation accesses it directly; friendship does not relax this rule. Direct base/derived implementation access is the narrow exception.
- Model state shared at field granularity as an unprefixed member of a `struct`, not as a friend's view into a class's `m_` state. If a type needs both shared data and encapsulated state, put the former in a `...Data` struct or expose the latter through member functions.

## I/O sharding
Every data member involved in I/O rx/tx should have one owning shard:

- Rx-owned state is accessed only on the Rx thread.
- Tx-owned state is accessed only on the Tx thread.
- A function must not access Rx-owned and Tx-owned data in the same thread
  context.
- Cross-shard work is transferred by posting a function to the owning shard,
  not by locking around the non-owning shard's data.

State that is inescapably shared by both rx and tx should be rx-owned,
and tx activity should enqueue mutations; locking/atomics may be permitted,
exceptionally, if the tx side just needs read access without the overhead
of posting work onto the rx thread just to read then posting back again
the remainder

`ZmPQRx` is Rx-owned in its entirety. `ZmPQTx` is Tx-owned in its entirety.
If a class inherits both, as `Zquic::Stream` does, each base subobject still
keeps its own shard ownership. Rx code may use only the `ZmPQRx` side; Tx code
may use only the `ZmPQTx` side.

### I/O Function Organization
Functions should be organized by owning shard:

- Rx-only functions assert or are otherwise guaranteed to run on `rxThread`.
  They may read/write Rx-owned members only.
- Tx-only functions assert or are otherwise guaranteed to run on `txThread`.
  They may read/write Tx-owned members only.
- Public entry points that may be called from either shard should be thin
  dispatchers that copy/capture only the data needed by the destination shard
  and immediately `rxRun`/`rxInvoke` or `txRun`/`txInvoke` to trailing
  underscored variants that can assume correct thread ownership and
  assert accordingly in debug mode
  - the thread-specific variants can be publicly accessible for callers to use
    when the owning thread can be relied on, to elide the overhead of `*Invoke`

Avoid functions that are "mostly Rx" but opportunistically touch Tx queues, or
"mostly Tx" but inspect Rx stream state. Split those into two functions with an
explicit handoff.

I/O functions should align with this convention:
- `[function]` uses `rxInvoke` or `txInvoke` to call `[function]_`
- `[function]_` uses `ZiAssert` to assert that it is on-shard and does the work
  - in a release build, this elides `invoke`
- callers use `[function]_` if they are already assured to be on-shard, `[function]` otherwise
- `[function]` only exists if off-shard calling is supported (i.e. public app-facing functions)

### Cross-Shard Handoffs
Small fixed-size information may be snapshotted and captured by value when
posting to another shard. Examples include scalar counters, packet numbers, or
bounded ACK ranges.

Large or variable-size information should not be captured directly by lambda.
Write it into a buffer or queue owned by the destination shard, then capture
only the handle and fixed metadata needed to consume it. For Tx work, this
usually means writing into a Tx buffer on the Tx side.

Do not use `ZmLock` to make non-owner access acceptable. A lock can protect
memory, but it does not preserve the sharding model and tends to hide work
running on the wrong I/O thread.

### Lifetime
I/O buffers and dependent structures should use raw back-pointers to owning
link/session/connection/... objects where that is the local pattern.
The owner is responsible for draining or cancelling dependent structures during
shutdown before destruction. Do not keep links alive by adding buffer-level reference
churn unless the ownership model explicitly requires it.

### Teardown
Sharded I/O teardown requires a 3-phase asynchronous process:
1. prevent further ingress, timer and I/O work (e.g. `m_up = false`), and post a teardown continuation on the rx thread to drain rx activity
2. (in the rx thread continuation) post a teardown continuation on the tx thread to drain tx activity
3. (in the tx thread continuation) complete the teardown and release object ownership
- do NOT block except in the main thread, this is async continuation code
- I/O buffers, queued lambdas etc. are populous short-lived objects owned by fewer longer-lived objects such as connections/links/sessions/streams
  - populous short-lived objects should not hold reference counts back to their owners, this anti-pattern:
    - causes reference-count churn in the owner
    - risks ownership cycles
  - such back-pointers should be raw pointers (example: `ZiIOBuf::owner`)
  - owners are responsible for draining all activity using orderly teardown as described above, ensuring that objects with stale backpointers cannot outlive their owners

## Naming
### Length and abbreviations
- Names must be concise; 28 bytes is the hard upper limit.
- Use industry standard in-code abbreviations for long names:
  - `receive` -> `rx`
  - `transmit` -> `tx`
  - `reserve` -> `res`
  - `recovery` -> `rec`
  - `request` -> `req`
  - `receive` -> `recv`
  - `event` -> `evt`
  - `client` -> `cli`
  - `server` -> `srv`
  - `connection` -> `cxn`
  - `packet` -> `pkt`
  - `protection` -> `prot`
  - `security` -> `sec`
  - `generation` -> `gen`
  - `version` -> `ver`
  - `original` -> `orig`
  - `previous` -> `prev`
  - `parameter` -> `param`
  - `interface` -> `if`
  - `implementation` -> `impl`
  - ... and so on (this is not an exhaustive list)
- Example: `packet` is fine; `reservePacketProtection` is too long; use `resPktProt`.
- use "ack" and "ackd", not "ackd"; example: `packetAckd`
- use "nak and "nakd", not "nack", "nackd" or "nacked"; example: `packetNakd`
- do not prefix or namespace file-scoped `static` functions in `.cc` files:
  - use short meaningful names, e.g. in `ZiIP.cc`: `pton4` not `ZiIP_pton4`
- do not use ambiguous abbreviations:
  - bad: `bytesInFlight` -> `bif`: `if` is typically read as `interface`
  - bad: `congestionBytes` -> `congBytes`: `cong` is a non-standard and counter-intuitive abbreviation
- elide redundant words in names:
  - `runtimeDiag` -> `diag`: "runtime" is implied, delete it
  - `Engine::stopEngine` -> `stop`: "engine" is implied - this is a member function of `Engine`

### Casing and member prefixes
- Names are generally camel-case, not snake-case, but there are numerous exceptions.
- Repository-owned library/component basenames in `src` and unit-test
  code/data basenames in `test` are CamelCase and use the module prefix, e.g.
  `ZmLib`, `ZmTest`, and `ZhttpParserTest`.
- Programs built in `src` have succinct lowercase command-line names. Their
  program-specific source and resource basenames are also lowercase, e.g.
  `zcmd`, `zdash`, `ztotp`, and `zwsd`.
- C compatibility-layer headers and implementations intended for direct use by
  C applications retain their snake-case C API names, e.g. `zu_bitmap.h` and
  `zu_bitmap.cc`.
- Repository-owned code/data basenames in `util`, `itest`, `example`, `bench`,
  and `interop` are lowercase.
- Flabuffers schema files are snake-case.
- Conventional control filenames such as `Makefile.am`, `README.md`,
  `Dockerfile.*`, and `.gitignore` retain their conventional casing.
- Native names mandated by an external tool or protocol retain their spelling;
  this includes PostgreSQL extension/control/SQL names, generated files,
  imported fixtures, and FlatBuffers schema filenames and contract fields.
  FlatBuffers schemas use their established lowercase/underscore basenames;
  keep the corresponding generation rules and generated-header names aligned.
  Do not apply this exception to ordinary repository-owned C++ files.
- `m_` is reserved for encapsulated private data members of classes; see the class-shape ownership rule above.

### Accessors
- Use overloads for getters/setters; do not invent separate names.
- Example: `int x() const` and `void x(int)`.
- `decltype(auto) x(this &&self) { ... }` is acceptable to preserve move context.
- `int &x()` as a setter is an amber flag; use it only when thread-safety is guaranteed by the calling context. If direct mutation is intended, `int x` should probably be a public data member.
- When an outer fluent parameter type contains or extends another fluent
  parameter type, preserve inline configuration with the
  `ZiMxParams::scheduler()` pattern: provide accessors for the inner/base
  parameters and a same-named lambda overload that applies the lambda and
  returns the outer type as an rvalue. Do not forward every inner/base setter,
  and do not permit an inherited setter's narrower return type to decay an
  inline expression to the base type.

### Trailing underscores
- Use trailing underscores for internal-only or thread-dedicated functions/types.
- Example: `send` may `txInvoke` a `send_` call on the Tx thread; `send_` can assume the correct thread and elide the check/context switch.
- Example: `template <typename T> struct Foo : public Foo_ { ... };` where `Foo` is the API and `Foo_` is a `T`-independent common base.

### Vocabulary
- Prefer precise project vocabulary: "utilities" not "helpers", "duplex" not "bidi", etc.
- Do not choose imprecise or primitive English for accessibility to non-native, non-technical, or non-veteran readers; accessible naming is a hard non-goal.

### Standard function name vocabulary
- `push()` - appends a value at tail
- `unshift()` - prepends a value at head
- `pop()` - removes a value from tail
- `shift()` - removes a value from head
- `add()` - inserts a value
- `del()` - deletes a value
- `find()` - finds a value
- `iter()` - returns an iterator (`citer()` for const)
- `riter()` - returns a reverse iterator (`criter()` for const)
- `clear()`/`clr()` - clears to a default initial state
  - containers: clears certain contained values
- `clean()` - removes and frees **all** contained data
- `reset()` - both `clear` and `clean`
- `null()` (as a mutable member function) - same as `reset` for objects with a semantic "null" value, with the postcondition that the object's value is "null"
- `init()` - initializes an object post-constructor
- `final()` - finalizes an object pre-destructor
- `start()` - start running (should be idempotent)
- `stop()` - stop running (should be idempotent)

## Acceptance
- test suites must pass
  - clang: address sanitizer, leak sanitizer
  - valgrind: memcheck with leak checking

## Debugging
- use `libtool exec` to run programs from `src`, `test`, `util`, `itest`,
  `example`, `bench`, or `interop` under debugging tools within the source tree
  - do not run the binaries in  `.libs` directly - library paths will be incorrect
  - `libtool exec` should be used to run `gdb`, `valgrind`, etc.
