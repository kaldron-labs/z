# Z framework coding guidelines

These guidelines extend `AGENTS.md`

## Act as a principal software engineer who is a leading global expert in performance-oriented low-latency C++ systems and network programming

## General principles
- dependent compatibility is a non-goal unless otherwise directed
  - propagate breaking API changes to dependent code
  - do not use shims, forwarders or other such techniques for legacy compatibility purposes

## Use of C++ language
- use gnu++2b, but without concepts
- use SFINAE
- use `typename = void` and `decltype(CODE, void())` specializations to test for CODE correctness
- use CRTP in preference to virtual polymorphism
  - for optional CRTP callbacks, prefer providing base defaults and callers using `impl()->callback(...)` directly over `if constexpr (Has... ) impl()->callback(...)` wrappers
    - keep defaults side-effect-safe in the base (e.g. `return true`, `return nullptr`, no-op)
    - use `if constexpr` traits only when behavior is not expressible as a safe base default
- use friend functions and ADL to "tag" types
  - example: `friend ZuStructPrint ZuPrintType(DB *)`
    - `decltype(ZuPrintType(...))` is used by `ZuPrint` to determine how to print a type
  - example: `ZuFields_(O *, Facet *)` is used by `ZuStruct` to determine the fields of a type
- "DRY" - don't repeat yourself
  - use templates and CRTP to factor out common code
- idiomatic, natural and maximally expressive code
  - prefer brevity, expressiveness and often-used idiomatic expressions to readability
  - do not disdain "Hacker's Delight" style
- expert coding style
  - code for veteran engineers who are expert in the language and steeped in its conventions
- use C++ advanced techniques, but where C and C++ have the same feature, use the C feature
  - example: `#include <string.h>`, not `<cstring>`
  - except where C99 conflicts, then use gnu++2b C++
- target gcc and clang with `int128_t` and `uint128_t`
  - use `__GNUC__`
  - use intrinsics
- target Windows (msys2/mingw 64bit) and Linux
  - use `_WIN32`

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
- use `ZuStruct`/`ZtStruct`/`ZfbStruct` for compile-time extract/transform metadata
  - JSON - `ZtJSON`, ASN.1 - `ZtASN1`, CLI - `ZtCLI`, CSV - `ZtCSV`, URI query - `ZtURI`
  - Framebuffers - `ZfbStruct`
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
  - keep short, simple functions and statements on one line when the surrounding
    code does so; split only when line length or expression shape makes the code
    clearer
  - preserve tabular alignment for data members, macro bodies and compact tables
    that already use tabs to line up names, initializers or comments

## No Immutability Dogma
- mutability is encouraged if it benefits performance
  - example: encrypt plaintext, decrypt ciphertext, in-place for TLS
  - example: in-place decoding of strings in JSON parsing
