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
- add external dependencies in `configure.ac`
  - use `PKG_CHECK_MODULES` if the dependency is in pkg-config, falling back to dependency-specific `m4` if not

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

### Portability and compiler features
- Target gcc and clang; use `__GNUC__`, intrinsics, `int128_t`, and `uint128_t` where appropriate.
- Target Linux and Windows via msys2/mingw 64-bit; use `_WIN32` for Windows-specific paths.

## Use of STL and other dependencies
- minimize use of STL
- use `Zu` alternatives to STL: example: `ZuIfT` instead of `enable_if`
- maximally leverage `Zu*`, `Zm*`, `Zt*` and `Zi*`

## Amber flags
- fixed-size arrays
  - if accompanied by explicitly and separately maintained lengths, these should almost certainly be replaced by `ZuArray`/`ZtArray`/`ZtString`/`ZtLocalArray` etc.
  - if lookup tables, `ZmHash`/`ZmLHash` are probably more appropriate, with appopriate locking, hash IDs, etc. to permit run-time sizing/tuning
  - hard-coded size/capacity limits are always questionable
    - too low? do they impair scaling?
    - too high? do they bloat stack or heap? is the capacity mostly unused?
    - mandated by RFC, other standard or authority?
    - aligned with other mainstream implementations of the same functionality?
  - array rules:
    - if required, hard upper limits on array size should be explicitly enforced in code
    - arrays in hot paths should be dynamic with a builtin size (`ZtBuiltin`) that covers 95-99% of usage in real-world workloads
    - arrays in cold paths should be dynamic with no builtin size (`ZtArray`)
    - arrays in hot paths that can be scratch should be stack-allocated with heap fallback (`ZtLocalArray`)
  - hard-coded numbers, e.g. 16, should always be library-defined compile-time constants that are prominently located in the code with a comment so they can be maintained
- heap allocations
  - heap allocations that should be scratch stack allocations
  - heap allocations that do not leverage `ZmHeap` / `ZmVHeap`
  - default-identified heap allocations that do not specify a heap ID
  - heap allocations in a hot path are a red flag
  - necessary heap allocations should:
    - leverage `ZmHeap` for fixed-size, `ZmVHeap` for variable size
    - be identified with `ZmHeapID` for run-time telemetry and tuning
    - be consolidated with intrusive reference counting and container overhead
      - see `ZmPolyCache` for an example of nesting a hash table node, a list node and reference-counting in a single allocated node
  - prefer callbacks with stack-allocated scratch temporaries (using `ZtLocalArray` and other such) to heap-allocated context
  - ensure `ZmHeap`-optimized buffer management and queue node management
  - I/O buffers are an exception to the usual "prefer stack over heap" - see below
- copying of string/byte data to/from temporaries
  - this is legitimate to achieve a contiguous span in:
    - stack-allocated scratch buffers which will be passed to CRTP callbacks
    - long-lived heap memory which needs to be retained in the program state
  - this is not legitimate in almost all other cases, including encoding/decoding and encryption/decryption
    - encoding/decoding and encryption/decryption should be done in place using mutable buffers if possible, unless
    - encoding/decoding/encrypting/decrypting to a destination that is uninitialized storage elides copying to that destination
- unnecessary default- or zero-initialization
  - if memory is going to be overwritten immediately, use uninitialized storage
- algorithmic inefficiencies
- performance, latency or throughput impairments
- ineffective use of Z Framework
  - redundant code that duplicates available capabilities in Z framework lower-level libraries
- misalignments with goal, guidelines, these guidelines and `AGENTS.md`
- chained `if` statements that should be `switch`
- highly nested logic
- repeated code blocks that are near-identical, violating "DRY"
- dead code that should be deleted
  - delete historical compatibility code unless explicitly required
- unnecessary casts
  - unnecessary casting among char-equivalent pointers
    - most Z framework types convert equivalent primitive element types automatically
  - unnecessary casting to base of CRTP `impl()` / `app()`
    - bases that need to constrain function name resolution should use `using T::function;`
- unnecessary constructor churn with needlessly-initialized storage
  - Z intentionally prefers uninitialized storage and explicit placement new
  - Z array containers are intentionally uninitialized until filled
- direct use of `FILE`, `syslog`, etc.
  - use `ZiLog`, `ZiFile`
- use of `*printf` variable args
  - use Z framework types with `<<`
  - use `ZuBox` and `ZuFmt` for formatting
- use of separate `bool` instead of sentinel values to signal unset, uninitialized or null
  - Z prefers sentinel values

## Leveraging Key Z Framework Capabilities
- assertions:
  - use `ZuAssert` for compile-time assertions
  - run-time assertions:
      - below `zm`, use plain `assert`
      - below `zi`, use `ZmAssert`
      - in `zi` or above, use `ZiAssert`
        - always use `ZiAssert` for run-time assertions that need graceful failure handling
- linker symbol length control for complex template aliases
  - use `ZuDerive(x, ([complex template instantiation]))` in place of
    `using X = [complex template instantiation]`; this introduces a new
    explicit type ID, which helps the compiler
- `ZuSpan` `*Array` and `*String` interoperate smoothly without explicit casting:
  - do not unnecessarily cast them - for example, `ZuCSpan` and `ZuBSpan` silently convert
- `ZuTypeList` and `ZuSeq` encode tables, associative containers and sequences at compile-time 
  - use `ZuSwitch` instead of static lookup tables
  - use `ZuUnroll` to iterate over compile-time type lists and sequences
  - use `ZuType`, `ZuTypeIndex`, `ZuTypeMap`, `ZuTypeSlice`, etc. to transform type lists and sequences
- use `ZuString` and `ZuStringT` for compile-time strings
- use `ZuStringTL` for compile-time lists of strings
- use `ZtLocalArray` for stack-allocated scratch, will fallback to heap-allocation
  - ensure appropriate heap identification of the underlying array
- use `ZmAlloc` for large single-object stack allocations with heap fallback
- use `ZmSpecific` instead of `thread_local`
- use `ZmSingleton` for global singletons
- use `ZtBuiltin` for builtin arrays with heap-allocation fallback
- use `ZuMatcher` for token-matching multiple possibilities, use `==` for a single possibility
- use `ZuBox`, `ZuFmt`, `ZuPrint` for printing
  - stream directly to `Z*String` and `Z*Array` types
  - stream directly to `std::cout` etc.
- use `ZuStruct`/`ZtStruct`/`ZfbStruct` for compile-time extract/transform metadata
  - JSON - `ZtJSON`, ASN.1 - `ZtASN1`, CLI - `ZtCLI`, CSV - `ZtCSV`, URI query - `ZtURI`
  - Framebuffers - `ZfbStruct`
- use `ZmScheduler` for thread pools
  - use `ZmScheduler` `isolated` threads to ensure "sharding"
  - sharding is associating data with single specific threads
    - minimize inter-thread sharing of data and consequent locking/atomics
  - dedicate threads and associated data to independent Rx and Tx in I/O
- use `ZiIOBuf` for buffer management
  - I/O buffers are an exception to the usual "prefer stack over heap"
  - pooled heap buffer allocations is actually preferred to on-stack
  - buffers can be moved between threads by reference without copying
  - buffer heap pool sizes can be optimally tuned to the workload at run-time
- use `ZuTestUtil` and underlying `ZuTest` for TAP-emitting unit tests
- use appropriate containers: `ZmHash`, `ZmLHash`, `ZmList`, `ZmRBTree`, `ZmPQueue`, ...
  - Z iterators are usually optionally mutable and can delete while iterating
  - `del()` / `delNode()` usually returns a movable reference to the deleted node/value
- use `ZmRing`, `ZiRing` for inter-thread and inter-process communication
- use `ZiLog` for logging
- use `ZiFile` for file I/O, `ZiMMapFile` for memory-mapped I/O, `ZiMultiplex` for network I/O multiplexing
- use `ZiEventLoop` for interoperability with other event loops and types of handles
- use `Zdb` for relational data persistency
  - use sagas for transactional integrity
- comparisons and sentinels:
  - use `operator *` to detect sentinel null
  - see `ZuCmp` for sentinel values and associated logic
  - use `ZuCmp::cmp` in preference to `operator <=>` because `cmp` returns a plain int
- use `ZmFn` for type-erased lambdas
  - try and consolidate captures into a single 64bit value (e.g. a `ZmRef`)
    - use `ZmFn`'s built-in capture to elide heap-allocation for lambda instances
    - leverage `mvFn`
- combine `ZmFn` with `ZiIOFn` and `ZiIOContext` to optimize I/O processing
- intrusive containers:
  - containers like `ZmHash` can intrude their container node into the application's node type; this is preferred to passing in/out entire keys and values, because:
    - it reduces copying of keys and values
    - it reduces heap churn when the value is a reference-counted smart pointer to application data
      - two allocations (one for the application data, one for the container node) are consolidated into one
  - stacked intrusion:
    - when application nodes are used with multiple containers simultaneously (e.g. `ZmCache`, which wraps a LRU `ZmList` and a `ZmHash`), container node intrusions can be stacked in the same object
    - in such cases, `HeapID<"">` is used to disable the usual `ZmHeap` allocation for the nodes in the inner containers (the full size of the node is only available at the outermost container node)

## Code Style
- library headers must follow the following format:
  - example `[header]`: "ZuString", "ZhttpQPack"
  - example `[component]`: "Zu", "Zhttp"
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
  - headers must include all their **direct** dependencies
  - headers must not include **indirect** dependencies unless they use related code/definitions
- indentation:
  - match the prevailing style in `{zu,zm,zt,ze,zi}/src/*.{hh,cc}`; when
    these rules, an editor's interpretation of `cino`, and local code disagree,
    code precedent wins
  - use hard tabs for indentation (`noet`, `ts=8`) and a 2-column logical C++
    indent (`sw=2`); do not replace leading tabs with spaces
  - indent block contents by one logical level:
    - namespace/class/struct/function bodies, control-flow bodies and lambda
      bodies use one extra logical level
    - `public:`, `protected:` and `private:` labels are flush with the class
      declaration body; members following them are indented one logical level
    - `friend` declarations are flush with the class declaration body
      - `friend` declarations should go at top in the default private section
    - `case`/`default` labels follow the local switch style; statements under a
      label are indented one logical level from the label
  - for multi-line declarations and expressions, prefer the existing visual
    alignment style over mechanically adding fixed-width continuation indents:
    - split long template parameter lists, inheritance lists, base constructor
      calls and function argument lists after a natural delimiter
    - continue nested template/base/member initializer expressions one logical
      level deeper than their containing line
    - align continuation lines with the open construct when that is what nearby
      code does
    - example function multi-line style (note the `{` on a line of its own):
      ```
      template <typename Client>
      int run(
        ZiMultiplex &mx, const Options &options, URL url,
        RequestResult *result = nullptr)
      {
        Client client;
      ```
    - example constructor multi-line style (note the `:` and `{` on a line of their own):
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
  - keep short, simple functions and statements on one line when the surrounding
    code does so; split only when line length or expression shape makes the code
    clearer
  - preserve tabular alignment for data members, macro bodies and compact tables
    that already use tabs to line up names, initializers or comments
  - do not deeply indent lambdas, example with large multiline captures and parameters:
    ```
    auto lambda = [
      ... /* multiline captures */
    ](
      ... /* multiline parameters */)
    {
      ... /* body */
    };
    ```
- `class` vs `struct`
  - `struct`s must be all-public data members
    - not prefixed with `m_`
    - declared at top before function members (if any)
  - `class`es must be all-private data members
    - prefixed with `m_`
    - declared at bottom before closing `};`
  - no hybrid `class` or `struct` with mixed public/private data members
- right-align members with hard TABs
- `*` and `&` go with the member, not the type
  - `void<TAB>*m_`, not `void *<TAB>m_`

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

Shutdown remains a multi-phase operation: stop ingress, drain or cancel queued
dependent work on each owning shard, then release owning objects.

## Naming
- names must be concise; no names can exceed 32 bytes (hard upper limit)
  - while "packet" would be fine as a name, "reservePacketProtection" would not
    - use standard abbreviations common in code:
      `reserve` -> `res`
      `packet` -> `pkt`
      `client` -> `cli`
      `server` -> `srv`
      `protection` -> `prot`
      ... etc.
    - resulting name would be "resPktProt"
- names are generally camelCase, not snake_case
  - exceptions are made for external dependencies
- `m_` is reserved for private data members of classes
- use overloads for getters/setters, do not name them independently
  - example: `int x() const` and `void x(int)`
    - it's also ok to `decltype(auto) x(this &&self) { ... }` to properly handle move context
  - `int &x()` might be ok as a setter, if thread-safety is guaranteed by the calling context, but this pattern is an amber flag
    - in such cases `int x` should probably be a public data member
- use trailing underscores to indicate:
  - internal-only or thread-dedicated functions/types
    - e.g. `send` might use `txInvoke` (which checks the running thread) to possibly enqueue a call to `send_` on the tx thread so it can be called from any thread, while `send_` can assume the caller is already running on the right thread and elide the check and potential context switch
    - e.g. `template <typename T> struct Foo : public Foo_ { ... };` - `Foo` is the API, while `Foo_` is a `T`-independent common base.
- "utilties" not "helpers", "duplex" not "bidi", etc.
  - no imprecise or primitive use of the English language designed to make it more accessible to non-native/non-technical/non-veteran readers
  - accessibility of language or naming is a hard non-goal

## Debugging
- use `libtool exec` to run test programs under debugging tools within the source tree
  - do not run the binaries in  `.libs` directly - library paths will be incorrect
  - `libtool exec` should be used to run `gdb`, `valgrind`, etc.
