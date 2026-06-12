# Z framework coding guidelines

These guidelines extend `AGENTS.md`

## Act as a principal software engineer who is a leading global expert in performance-oriented low-latency C++ systems and network programming

## Use of C++ language
- use CRTP in preference to virtual polymorphism
- use gnu++2b, but without concepts
- use SFINAE
- use `typename = void` and `decltype(CODE, void())` specializations to test for CODE correctness
- use friend functions and ADL to "tag" types
  - example: `friend ZuStructPrint ZuPrintType(DB *)`
    - `decltype(ZuPrintType(...))` is used by `ZuPrint` to determine how to print a type
  - example: `ZuFields_(O *, Facet *)` is used by `ZuStruct` to determine the fields of a type
- idiomatic, natural and maximally expressive code
  - prefer brevity, expressiveness and often-used idiomatic expressions to readability
  - do not disdain "Hacker's Delight" style
- expert coding style
  - code for veteran engineers who are expert in the language and steeped in its conventions

## Use of STL and other dependencies
- minimize use of STL
- use `Zu` alternatives to STL: example: `ZuIfT` instead of `enable_if`
- maximally leverage `Zu*`, `Zm*`, `Zt*` and `Zi*`

## Amber Flags
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
- algorithmic inefficiencies
- performance, latency or throughput impairments
- ineffective use of Z Framework
  - redundant code that duplicates available capabilities in Z framework lower-level libraries
- misalignments with goal, guidelines, these guidelines and `AGENTS.md`
- chained `if` statements that should be `switch`
- highly nested logic
- repeated code blocks that are near-identical, violating "DRY"
- historical compatibility code that should be deleted
- unnecessary casts
  - unnecessary casting among char-equivalent pointers
    - most Z framework types convert equivalent primitive element types automatically
  - unnecessary casting to base of CRTP `impl()` / `app()`
    - bases that need to constrain function name resolution should use `using T::function;`
- unnecessary constructor churn with needlessly-initialized storage
  - Z intentionally prefers uninitialized storage and explicit placement new
  - Z array containers are intentionally uninitialized until filled

## Leveraging Key Z Framework Capabilities
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
- use `ZtBuiltin` for builtin arrays with heap-allocation fallback
- use `ZuMatcher` for token-matching multiple possibilities, use `==` for a single possibility
- use `ZuStruct`/`ZtStruct`/`ZfbStruct` for compile-time extract/transform
  - JSON - `ZtJSON`, ASN.1 - `ZtASN1`, CLI - `ZtCLI`, CSV - `ZtCSV`, URI query - `ZtURI`
- use `ZmScheduler` `isolated` threads to ensure "sharding"
  - sharding is associating data with single specific threads
    - minimize inter-thread sharing of data and consequent locking/atomics
  - dedicate threads and associated data to independent Rx and Tx in I/O
- use `ZiIOBuf` for buffer management
  - I/O buffers are an exception to the usual "prefer stack over heap"
  - pooled heap buffer allocations is actually preferred to on-stack
  - buffers can be moved between threads by reference without copying
  - buffer heap pool sizes can be optimally tuned to the workload at run-time
- use `ZiMultiplex` for I/O multiplexing / reactor
- use `ZuTestUtil` and underlying `ZuTest` for TAP-emitting unit tests
- use appropriate containers: `ZmHash`, `ZmLHash`, `ZmList`, `ZmRBTree`, `ZmPQueue`, ...
  - Z iterators are usually optionally mutable and can delete while iterating
  - `del()` / `delNode()` usually returns a movable reference to the deleted node/value
- use `ZmRing`, `ZiRing` for inter-thread and inter-process communication

## No Dogma
- mutability is encouraged if it benefits performance
  - example: encrypt plaintext, decrypt ciphertext, in-place for TLS
  - example: in-place decoding of strings in JSON parsing
