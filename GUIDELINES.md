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
- algorithmic inefficiencies
- performance, latency or throughput impairments
- ineffective use of Z Framework
  - redundant code that duplicates available capabilities in Z framework lower-level libraries
- misalignments with goal, guidelines, these guidelines and `AGENTS.md`
- chained `if` statements that should be `switch`
- highly nested logic
- repeated code blocks that are near-identical, violating "DRY"
- historical compatibility code that should be deleted

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
- use `ZuMatcher` for token-matching multiple possibilities, use `==` for a single possibility
- use `ZuStruct`/`ZtStruct`/`ZfbStruct` for compile-time extract/transform
  - JSON - `ZtJSON`, ASN.1 - `ZtASN1`, CLI - `ZtCLI`, CSV - `ZtCSV`, URI query - `ZtURI`
- use `ZmScheduler` `isolated` threads to ensure "sharding"
  - sharding is associating data with single specific threads
    - minimize inter-thread sharing of data and consequent locking/atomics
  - dedicate threads and associated data to independent Rx and Tx in I/O
- use `ZiIOBuf` for buffer management
- use `ZiMultiplex` for I/O multiplexing / reactor
