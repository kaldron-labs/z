# Z framework coding guidelines

These guidelines extend `AGENTS.md`

## Act as a principal software engineer who is a leading global expert in performance-oriented low-latency C++ systems and network programming

## General principles
- dependent compatibility is a non-goal unless otherwise directed
  - propagate breaking API changes to dependent code
  - do not use shims, forwarders or other such techniques for legacy compatibility purposes

## No dogma
- immutability
  - pervasive immutability is a non-goal
  - mutability is encouraged if it benefits performance
    - example: in-place encrypt plaintext, decrypt ciphertext for TLS
    - example: in-place decoding of strings in JSON parsing
- type punning
  - type punning is endorsed, not discouraged
- undefined behavior
  - many things that are technically UB can actually be used reliably with the systems that Z targets (see "Target systems")
  - UB is only a concern when it actually creates a tangible risk to correct behavior in targeted environments

## Target systems
- compilers: current gcc, clang
- CPU: x64 intel/amd, ARM
- OS: linux, windows

## Build system
- use the `configure` wrapper named `z.config` to reconfigure the build
  - usage: `z.config -h`
- this is a hierarchy of Makefiles that **intentionally** do not automatically rebuild dependencies in other directories
  - use a top-level `make -j8` to ensure all dependencies are rebuilt/refreshed
- add external dependencies in `configure.ac`
  - use `PKG_CHECK_MODULES` if the dependency is in pkg-config, falling back to dependency-specific `m4` if not
- always recompile with `make -j8`
  - if not rebuilding everything, if library code has changed in a layer under development, always rebuild `src` before dependent `test`/`bench`/`example`, e.g. `make -C zquic/src -j8`
  - always build default target `all` before `make test`, e.g. `make -C zquic/test -j8 && make -C zquic/test test`
  - after build type has changed (release, debug, asan, etc.) do a top-level `make clean && make -j8` to rebuild

## Use of C++ language
### Language level
- Compile as GNU C++2b, but do not use C++ concepts or `requires`.
- Use advanced C++ where it is expressive and efficient; where C and C++ offer the same facility, prefer the C form.
  - Example: `#include <string.h>`, not `<cstring>`.
  - Where C99 conflicts, use the GNU C++2b form.

### Static polymorphism and constraints
- Prefer CRTP, templates, and compile-time dispatch over virtual polymorphism.
- Use templates and CRTP to factor common code; keep logic DRY without adding weak abstractions.
- Express constraints with SFINAE and detector traits.
  - Prefer `typename = void` plus `decltype(CODE, void())` specializations to test whether `CODE` is well-formed.
  - Do not replace established `ZuIfT`/detector idioms with concepts.
- For optional CRTP callbacks, prefer side-effect-safe base defaults and direct calls such as `impl()->callback(...)`.
  - Defaults should be harmless: `return true`, `return nullptr`, or no-op.
  - Use `if constexpr` traits only when a safe base default cannot express the behavior.

### ADL tagging
- Use friend functions and ADL to tag types and expose compile-time metadata.
  - Example: `friend ZuStructPrint ZuPrintType(DB *)`; `decltype(ZuPrintType(...))` lets `ZuPrint` choose formatting.
  - Example: `ZuFields_(O *, Facet *)` lets `ZuStruct` discover fields.

### Style
- Write idiomatic, natural, maximally expressive code for veteran C++ engineers.
- Prefer brevity, common expert idioms, and precise structure over beginner-oriented readability.
- Do not disdain "Hacker's Delight" style when it is clear, correct, and faster.
- `auto ptr = ...` not `auto *ptr = ...`

### Portability and compiler features
- Target gcc and clang; use `__GNUC__`, intrinsics, `int128_t`, and `uint128_t` where appropriate.
- Target Linux and Windows via msys2/mingw 64-bit; use `_WIN32` for Windows-specific paths.

### Enums
- do not use `enum class`
  - if the enum needs to be type-aliased by dependents, use `struct E { enum { ... } };`
  - otherwise use `namespace E { enum { ... } };` because this permits dependents to `using namespace E`
  - above `zm` use `ZtEnum`:
    - if an external API type
    - if callers need name lookup at run-time

### Constant literals
- when a constant literal is integral or boolean and does not need to be wider than `int`, use `enum { X = 42 };` in favor of `static constexpr T = 42`

## Use of STL and other dependencies
- minimize use of STL
- use `Zu` alternatives to STL: example: `ZuIfT` instead of `enable_if`
- maximally leverage `Zu*`, `Zm*`, `Zt*` and `Zi*`

## Amber flags
### Storage and capacity
- Flag: fixed-size arrays, especially with separately maintained lengths.
  Problem: capacity is easy to desynchronize, hard to tune, and often either caps scaling or wastes stack/heap.
  Fix: use `ZuArray`, `ZtArray`, `ZtString`, `ZtLocalArray`, etc.; enforce any required hard upper limit in code.
- Flag: fixed-size lookup tables.
  Problem: static sizing prevents run-time tuning and often misses required locking/hash-ID integration.
  Fix: use `ZmHash`/`ZmLHash` with appropriate locking and hash IDs.
- Flag: hard-coded capacities such as `16`.
  Problem: unexplained limits may impair scaling when too low, bloat stack/heap when too high, or leave mostly unused capacity.
  Fix: use prominently located, named library-defined compile-time constants with a maintenance comment: RFC/standard mandate, mainstream alignment, or measured scaling/footprint trade-off.
- Flag: arrays on hot or cold paths without workload-aware storage.
  Problem: the wrong storage choice adds allocation latency, stack pressure, or unused capacity.
  Fix: use `ZtBuiltin` sized for 95-99% of hot-path usage, plain `ZtArray` for cold paths, and `ZtLocalArray` for scratch hot-path storage with heap fallback.

### Heap allocation
- Flag: heap allocation in hot paths or for scratch state.
  Problem: allocator latency and contention directly hurt tail latency and throughput.
  Fix: use stack scratch such as `ZtLocalArray` and pass it through callbacks; I/O buffers are the exception, see the I/O guidance below.
- Flag: heap allocation without `ZmHeap`, `ZmVHeap`, or `ZmHeapID`.
  Problem: allocation behavior becomes opaque and loses Z heap telemetry/tuning.
  Fix: use `ZmHeap` for fixed-size allocations, `ZmVHeap` for variable-size allocations, and identify allocations with `ZmHeapID`.
- Flag: separate allocations for object, refcount, and container nodes.
  Problem: fragmented allocation adds memory overhead and pointer chasing.
  Fix: consolidate with intrusive reference counting and container nodes where practical; see `ZmPolyCache` nesting a hash node, list node, and refcount in one allocated node.
- Flag: buffer or queue-node management bypassing optimized Z heap paths.
  Problem: common data-path allocations miss pooling and telemetry.
  Fix: use `ZmHeap`-optimized buffer and queue-node management.

### Data movement and initialization
- Flag: copying string/byte data through temporaries.
  Problem: avoidable memory traffic dominates many encode/decode and crypto paths.
  Fix: operate in place on mutable buffers, or write directly into uninitialized destination storage when that elides a copy; copies are not legitimate in almost all other cases.
- Flag: temporary contiguous copies.
  Problem: they are usually hidden allocation/copy costs.
  Fix: allow them only for stack scratch passed to CRTP callbacks or for long-lived heap state that must retain data.
- Flag: default/zero initialization or constructor churn before immediate overwrite.
  Problem: it burns cycles and cache bandwidth for data that will not be read.
  Fix: use uninitialized storage and explicit placement new where appropriate; Z array containers are intentionally uninitialized until filled.
- Flag: calling `ZiLOG` with a lambda that captures pointers or by reference.
  Problem: the logger runs lambdas on a dedicated logger thread at a later time
  Fix: capture by copy the specific data needed for the log trace; use `ZeString` for scratch strings

### Algorithm and control flow
- Flag: algorithmic inefficiency, latency regression, throughput impairment, or avoidable work.
  Problem: small local costs often become system-level throughput or tail-latency limits.
  Fix: choose the lower-complexity or lower-allocation design and validate hot paths.
- Flag: chained `if` statements that should be `switch`.
  Problem: intent and dispatch shape are harder for both readers and compilers to see.
  Fix: use `switch` when branching on one discrete value.
- Flag: highly nested logic.
  Problem: state and error handling become difficult to audit.
  Fix: flatten control flow with early exits, helper functions, or clearer state transitions.
- Flag: near-identical repeated blocks.
  Problem: duplication hides divergent fixes and violates DRY.
  Fix: factor common code with templates, CRTP, or local helpers that preserve performance.

### Framework fit
- Flag: reimplementing lower-level Z Framework capabilities.
  Problem: duplicate code misses established semantics, optimizations, and maintenance paths.
  Fix: use the existing `Zu*`, `Zm*`, `Zt*`, and `Zi*` facilities.
- Flag: direct use of `FILE`, `syslog`, etc.
  Problem: it bypasses Z I/O and logging conventions.
  Fix: use `ZiFile`, `ZiLog`, etc.
- Flag: `*printf` varargs formatting.
  Problem: varargs are weakly typed and bypass Z formatting conventions.
  Fix: use Z framework types with `<<`, `ZuBox`, and `ZuFmt`.
- Flag: separate `bool` flags for unset, uninitialized, or null state.
  Problem: extra flags can diverge from the value they describe.
  Fix: use sentinel values.

### Type and API friction
- Flag: unnecessary casts.
  Problem: casts hide type-system mistakes and make ownership/aliasing harder to audit.
  Fix: rely on existing Z conversions and fix the type boundary.
- Flag: casts among char-equivalent pointers.
  Problem: most Z types already convert equivalent primitive element types automatically.
  Fix: remove the cast unless a real representation change is required.
- Flag: casts to CRTP `impl()`/`app()` bases.
  Problem: they obscure name lookup and static dispatch.
  Fix: use `using T::function;` in bases that need constrained function lookup.

### Scope and cleanup
- Flag: code that does not align with the goal, these guidelines, or `AGENTS.md`.
  Problem: local changes can erode project architecture and review expectations.
  Fix: realign the change or call out the required exception explicitly.
- Flag: dead code or historical compatibility code.
  Problem: unused code increases audit, test, and maintenance burden.
  Fix: delete it unless compatibility is explicitly required.
- Flag: short-lived objects holding reference counts to longer-lived objects
  Problem: causes reference-count churn in the owner and risks ownership cycles
  Fix: short-lived objects should use raw pointers back to longer-lived owners; owners must teardown carefully to ensure that they cannot be outlived by short-lived objects that they own; owners should not attempt to delete owned objects in their destructors, they should instead assert that no such objects remain

### Unnecessary atomic operations, locking and copies
- Flag: diagnostic, statistics or telemetry data (e.g. counters) without any requirement for accuracy or stable reads are needlessly atomic or guarded by locks or snapshotted
  Problem: atomic operations and locking induce latency volatility
  Fix: use primitive types, do not guard with locks, permit unclean reads from other threads

## Leveraging Key Z Framework Capabilities
### Assertions and type mechanics
- Assertions: use `ZuAssert` at compile time; at run time use plain `assert` below `zm`, `ZmAssert` below `zi`, and `ZiAssert` in `zi` or above.
- Use `ZiAssert` whenever run-time assertion failure needs graceful handling.
- For complex template aliases, prefer `ZuDerive(x, ([complex template instantiation]))` over `using X = ...`; the explicit type ID reduces compiler/linker symbol lengths and eases debugging.
- Comparisons and sentinels: use `operator *` to detect sentinel null, use `ZuCmp` sentinel logic, and prefer `ZuCmp::cmp` over `operator <=>` because it returns plain `int`.
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
    - `ZtLocalString` (macro) - scratch on-stack null-terminated `ZtString` with heap fallback
    - `ZtLocalArray` (macro) - scratch on-stack null-terminated `ZtArray` with heap fallback
  - specific-purpose strings:
    - `ZtString` and `ZtArray` can be tagged with a compile-time `ZmHeapID`
    - certain types with specific heap IDs are re-used throughout the framework:
      - `ZeString`: `ZtString` with heap ID for error messages, logs and diagnostics

### Compile-time data and matching
- Use `ZuTypeList` and `ZuSeq` for compile-time tables, associative containers, and sequences.
- Use `ZuSwitch` instead of static lookup tables.
- Use `ZuUnroll` to iterate compile-time type lists and sequences.
- Use `ZuType`, `ZuTypeIndex`, `ZuTypeMap`, `ZuTypeSlice`, etc. to transform type lists and sequences.
- Use `ZuString`/`ZuStringT` for compile-time strings and `ZuStringTL` for compile-time string lists.
- Use `ZuMatcher` for token matching among multiple possibilities; use `==` for a single possibility.

### Storage and lifetime
- Use `ZtLocalArray` for stack scratch with heap fallback; ensure the underlying array has appropriate heap identification.
- Use `ZtBuiltin` for builtin arrays with heap-allocation fallback.
- Use `ZmAlloc` for large single-object stack allocations with heap fallback.
- Use `ZmSpecific` instead of `thread_local`.
- Use `ZmSingleton` for global singletons.

### Formatting
- Use `ZuBox`, `ZuFmt`, and `ZuPrint` for printing.
- Stream directly to `Z*String`, `Z*Array`, and `std::cout`-style outputs.

### Metadata
- Use `ZuStruct`, `ZtStruct`, and `ZfbStruct` for compile-time extract/transform metadata.
- Use metadata integrations instead of ad hoc parsing: JSON `ZtJSON`, ASN.1 `ZtASN1`, CLI `ZtCLI`, CSV `ZtCSV`, URI query `ZtURI`, and Framebuffers `ZfbStruct`.

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
  1. cancel the timer (`ZmScheduler::del`)
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

### Containers and intrusion
- Use appropriate containers: `ZmHash`, `ZmLHash`, `ZmList`, `ZmRBTree`, `ZmPQueue`, etc.
- Z iterators are usually optionally mutable and can delete while iterating.
- `del()`/`delNode()` usually returns a movable reference to the deleted node/value.
- Prefer intrusive container nodes when application nodes can own them; this reduces key/value copying and, for reference-counted application data, consolidates the application object and container node from two allocations into one.
- Stack container intrusions when one application node participates in multiple containers, e.g. `ZmCache` combining an LRU `ZmList` and `ZmHash`.
- Use `HeapID<"">` to disable inner-container `ZmHeap` allocation when the full node size is only available at the outermost container.

### Callbacks
- Use `ZmFn` for type-erased lambdas.
- Consolidate captures into a single 64-bit value where possible, e.g. `ZmRef`; use `ZmFn` built-in capture to elide heap allocation and leverage `mvFn`.
- Combine `ZmFn` with `ZiIOFn` and `ZiIOContext` to optimize I/O processing.

### Tests
- Use `ZuTestUtil` and underlying `ZuTest` for TAP-emitting unit tests.

### Tracing and debug logging
- use `ZmBackTrace` for backtracing
  - use `ZmBackTracer` to efficiently capture/log a ring of backtraces
- demangling: below `zm`, use `ZuDemangle`, otherwise use `ZmDemangle`
  - demangling post-processing can be extended with `ZtDemangle`

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
- Indent namespace/class/struct/function bodies, control-flow bodies, and lambda bodies one logical level.
- Keep `public:`, `protected:`, and `private:` flush with the class declaration body; indent following members one logical level.
- Keep `friend` declarations flush with the class declaration body, at the top in the default private section.
- Follow local switch style for `case`/`default`; indent statements one logical level from the label.

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
    ZtVFieldArray fields, ZtVKeyFieldArray keyFields,
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
- `*` and `&` go with the member, not the type: `void<TAB>*m_`, not `void *<TAB>m_`.

### Class and struct shape
- `struct`s are all-public data: members are not prefixed with `m_` and appear at the top before function members.
- `class`es have all-private data: members are prefixed with `m_` and appear at the bottom before closing `};`.
- Do not create hybrid `class` or `struct` types with mixed public/private data members.

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
- 1. stop ingress and post a teardown continuation on the rx thread to drain rx activity
- 2. (in the rx thread continuation) post a teardown continuation on the tx thread to drain tx activity
- 3. (in the tx thread continuation) complete the teardown and release object ownership
- do NOT block except in the main thread, this is async continuation code
- I/O buffers, queued lambdas etc. are populous short-lived objects owned by fewer longer-lived objects such as connections/links/sessions/streams
  - populous short-lived objects should hold reference counts back to their owners, this anti-pattern:
    - causes reference-count churn in the owner
    - risks ownership cycles
  - such back-pointers should be raw pointers (example: `ZiIOBuf::owner`)
  - owners are responsible for draining all activity using orderly teardown as described above, ensuring that objects with stale backpointers cannot outlive their owners

## Naming
### Length and abbreviations
- Names must be concise; 32 bytes is the hard upper limit.
- Use standard in-code abbreviations for long names: `reserve` -> `res`, `packet` -> `pkt`, `client` -> `cli`, `server` -> `srv`, `protection` -> `prot`, `generation` -> `gen`, etc.
- Example: `packet` is fine; `reservePacketProtection` is too long; use `resPktProt`.

### Casing and member prefixes
- Names are generally camelCase, not snake_case.
- External dependencies may keep their native naming.
- `m_` is reserved for private data members of classes.

### Accessors
- Use overloads for getters/setters; do not invent separate names.
- Example: `int x() const` and `void x(int)`.
- `decltype(auto) x(this &&self) { ... }` is acceptable to preserve move context.
- `int &x()` as a setter is an amber flag; use it only when thread-safety is guaranteed by the calling context. If direct mutation is intended, `int x` should probably be a public data member.

### Trailing underscores
- Use trailing underscores for internal-only or thread-dedicated functions/types.
- Example: `send` may `txInvoke` a `send_` call on the Tx thread; `send_` can assume the correct thread and elide the check/context switch.
- Example: `template <typename T> struct Foo : public Foo_ { ... };` where `Foo` is the API and `Foo_` is a `T`-independent common base.

### Vocabulary
- Prefer precise project vocabulary: "utilities" not "helpers", "duplex" not "bidi", etc.
- Do not choose imprecise or primitive English for accessibility to non-native, non-technical, or non-veteran readers; accessible naming is a hard non-goal.

## Debugging
- use `libtool exec` to run test programs under debugging tools within the source tree
  - do not run the binaries in  `.libs` directly - library paths will be incorrect
  - `libtool exec` should be used to run `gdb`, `valgrind`, etc.
