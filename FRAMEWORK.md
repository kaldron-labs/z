# Z framework facilities

Part of the repository guidance mapped in [AGENTS.md](AGENTS.md).

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
    - `ZtString` and `ZtArray` can be tagged with a compile-time `ZmNHeapID`
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
- Do not use linear or quadratic searches that repeatedly compare strings or integers,
  in order to match a candidate against a set of possible values: this includes
  loops, `ZuUnroll`, and chains of comparisons.
  - For fixed string field IDs, map each parsed name to its field index with a
    compile-time `ZuMatcher`, then record the input position in a run-time
    fixed-size primitive `lookup[]` indexed by field. Load fields through
    `lookup[]`, following `ZfJSON` as the canonical control-flow example.
    Put `ZuMatcher` implementations in `.cc` files.
  - For integer field IDs, follow the same field-indexed `lookup[]` flow, but
    use a `ZmLHash<..., ZmLHashStatic<...>>` populated once from compile-time
    field metadata instead of `ZuMatcher`. Do not construct the map per parsed
    object or on another hot path.
  - A one-time linear search during program startup, initialization or initial
    configuration is acceptable
- Apply the format's duplicate rule to recognized fields. Detecting duplicate
  unrecognized keys is a non-goal.

### Storage and lifetime
- Use `ZmNHeap` and `ZmVHeap` to adopt a recycling zero-overhead block allocator, with heap identification, telemetry and configurable tuning
  - Use `ZmNHeap` for fixed-size concrete types, except objects managed by
    `ZmSingleton` or `ZmSpecific`, which do not require heap identification
    - Inherit the heap directly with the concrete type and tag its ID with ADL:
    ```
    class X : public ZmNHeap<X> {
    friend inline ZuStringT<"X"> ZmNHeapID(X *);
      ...
    };
    ```
    - Declare `friend inline ZuTrue ZmNHeapSharded(X *);` for sharded heaps.
    - An omitted or empty ID disables recycling.
  - Use `ZmVHeap` for variable-sized dynamic allocations (strings, etc.)
- Use `ZtScratch` for stack scratch with heap fallback; ensure the underlying array has appropriate heap identification.
- Dependents of `ZmNHeap`, `ZmVHeap`, `ZmScratch`, `ZtScratch`, and related
  heap or scratch allocators must not check allocation results for `nullptr`
  or use `if (!result)` failure branches. `ZmHeapFail` handles allocation
  failure; it is not a caller concern.
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
- Use `ZuStruct` and `ZfStruct` for compile-time extract/transform metadata.
- For CLI and configuration (`ZfCLI`, `ZfCf`, `ZfTOML`, `ZfYAML`), keep
  defaults in C++ constructors/member initializers. Apply configuration with
  `update()` to fields marked `Mutable`, preserving omitted values. Do not
  duplicate these defaults with `Deflt<>` or add `Ctor<>` unless a consumer
  actually constructs the object from field arguments.
- `Range<Min, Max>` bounds may have different types; do not cast bounds merely
  to make their types match. Use representation limits such as `INT_MAX`
  rather than arbitrary powers of two for maxima unless a protocol or
  implementation imposes a smaller bound.
- Use metadata integrations instead of ad hoc parsing: JSON `ZfJSON`, ASN.1 `ZfASN1`, CLI `ZfCLI`, CSV `ZfCSV`, URI query `ZfURI`, and FlatBuffers `Zfb`.
- Use `Zfb::save`, `Zfb::load`, typed views, and `Zfb::handler` for
  object construction, loading and updates from `ZfStruct` metadata.
  Use `Zfb::IOBuilder` for pooled I/O buffers. Already serialized nested
  roots are copied as byte spans, preserving field presence and defaults.
- Canonical FlatBuffers APIs and generated schemas belong only in explicit
  interoperability fixtures. Production reflection uses in-memory BFBS spans
  generated by `Zfb`; verify external data at ingress, without re-verifying
  trusted spans generated or handed off within the process.

### Concurrency and sharding
- Use `ZmScheduler` for thread pools.
- Use `ZmScheduler` `isolated` threads for sharding: associate data with specific threads to minimize sharing, locking, and atomics.
- Dedicate threads and their data to independent Rx and Tx in I/O.
- Use `ZmRing` and `ZiRing` for inter-thread and inter-process communication.

### Timers
- Schedule every timer event with an absolute time, never a relative interval.
- Calculate an absolute deadline by adding the interval to the current time on
  the timer's clock, then pass that deadline to the scheduler.
- `ZmScheduler::Timer` is used for timers
  - each `Timer` instance should be used for a separate individual timer/timeout
  - `Timer`s are intended to be contained by value as data members in owning structs/classes
    - they are referenced by raw pointer from the scheduler
  - `Timer`s must be cancelled with `del` before owner destruction during close/shutdown/stop to prevent stale pointer dereference
#### Timer Teardown
- timer teardown requires a 3-phase process, similar to [I/O teardown](IO.md#teardown)
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

### Tx streams
- Expose `failed()` as the stream's failure status. Failure is sticky for the
  current output operation; subsequent appends must not resume output or clear
  the failure.
- Retain the last error for diagnostics, on the stream or its existing owning
  file/link as appropriate. Keep stream failure independent of later changes
  to the owner's error state.
- Report capacity-limit, write, and send/admission failures through state and
  return values; do not throw framework exceptions. Allocation and growth use
  the Z heap/scratch failure path; do not check for nullable allocation/growth
  results. This does not require catching arbitrary exceptions from
  application printers or callbacks, or declaring the stream interface
  `noexcept`.
- `flush()` returns `bool ok`. An earlier valid prefix may flush once after a
  rejected printable, but failure remains set and `flush()` returns false.
  Check explicit flush at the operation's completion boundary; destructor
  flushing is best effort.
- Propagate synchronous rejection to the producing stream. Asynchronous
  transport errors remain with the owning link/request on its owning shard.

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
- Use `HeapID<"">` to disable inner-container `ZmNHeap` allocation when the full node size is only available at the outermost container.

### Callbacks
- Use `ZmFn` for type-erased lambdas.
- Consolidate captures into a single 64-bit value where possible, e.g. `ZmRef`; use `ZmFn` built-in capture to elide heap allocation and leverage `mvFn`.
- Combine `ZmFn` with `ZiIOFn` and `ZiIOContext` to optimize I/O processing.

### Tests
- C++ TAP emitters use `ZuTestUtil`, or underlying `ZuTest` when the utilities
  are not a fit. Use `log` for diagnostics in `ZuTestUtil` dependents.
  Initialize verbosity through `ZuTestUtil::parse`, which honors
  `HARNESS_ACTIVE`; do not duplicate that environment check in callers.
- Intentional whole-program skips may emit their TAP skip plan manually.
  Non-C++ test programs may emit TAP manually.
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
