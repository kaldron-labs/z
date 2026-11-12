# Z — Zero Overhead framework

Z is a vertically integrated C++ framework for latency-sensitive applications
and services: from vocabulary types, intrusive containers and NUMA-aware memory
management to sharded I/O, modern network protocols, replicated databases,
identity services and telemetry. Its layers share the same object model,
reflection metadata, buffers, scheduling and resource tuning, so applications
can carry data from a socket through a codec into storage without assembling
unrelated runtime frameworks.

Z targets current GCC and Clang in GNU C++23 mode on Linux and Windows
(MSYS2/MinGW), with x64 and ARM as target architectures. It combines templates
and headers with versioned shared libraries/DLLs. Development is active;
individual module documentation describes supported protocol profiles and
limitations.

[Build dependencies](docs/DEPENDENCIES.md) · [Testing and environment setup](docs/TESTING.md) ·
[Repository guidance map](AGENTS.md)

See [Z, STL, Boost, Folly, BDE and NTF equivalents](docs/Z_STL_BOOST_FOLLY_BDE.md) for feature comparisons
and the benefits and tradeoffs of Z facilities.

Development guidance is split by task; start with the map and read the relevant
documents:

- [Engineering principles](docs/GUIDELINES.md): performance priorities and target systems.
- [Build and development](docs/BUILD.md): module layout, commands, verification and PR conventions.
- [C++ rules](docs/CPP.md): language idioms, constraints and portability.
- [Code style](docs/STYLE.md): header layout, indentation, class shape and naming.
- [Framework facilities](docs/FRAMEWORK.md): Z types, storage, concurrency and other APIs.
- [I/O ownership](docs/IO.md): Rx/Tx sharding, handoffs, lifetime and teardown.
- [Audit flags and fixes](docs/AUDIT.md): audit scope, repair procedure and review criteria.

Individual modules may also provide a `GUIDELINES.md` with library-specific rules.

## Design priorities

- **Latency and predictable resource use.** Minimize copies, allocations and
  contention; expose heap sizing, hash sizing, queue capacity, CPU affinity and
  NUMA placement to the application. Allocation is explicit and tunable, not
  forbidden.
- **Sharding and message passing.** Bind mutable state to a thread and post work
  to its owner. Ring buffers and continuations connect stages; applications
  control thread creation and placement.
- **Intrusive ownership and containers.** Reference counts and container hooks
  live with the objects they describe, avoiding separate shared-pointer control
  blocks and allowing nodes to participate in multiple indices.
- **Compile-time specialization.** CRTP, traits, SFINAE, ADL and named template
  policies select behavior; reflection drives dispatch and serialization.
- **Direct access to data.** Borrow spans, decode mutable input in place, and
  serialize into destination buffers when lifetimes permit. Pooled I/O buffers
  pass by reference through asynchronous operations.
- **One coherent C++ stack.** Minimal STL use with deliberate interoperability;
  no production dependency on Boost, Folly or BDE. C interfaces and platform
  facilities remain accessible. Short component prefixes ([`Zu`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIOResult.hh), [`Zm`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDateTime.hh), [`Zi`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiProgram.hh),
  etc.) work consistently with both C++ and the preprocessor.

## Core highlights

### Containers, ownership and memory

[`ZmHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHash.hh) is a policy-based, separately chained hash table
with configurable node ownership, optional locking and lock striping. Its
heap and bucket configuration can be tuned centrally through [`ZmHashMgr`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHashMgr.hh),
and its statistics feed telemetry. [`ZmList`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmList.hh) and [`ZmRBTree`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRBTree.hh) provide the same
framework approach to linked lists and ordered indices (red/black trees).

[`ZmRef`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRef.hh) supplies intrusive reference counting; [`ZmFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmFn_.hh) stores a function and
small context directly, allocating only for larger captures. [`ZmNHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh) and
[`ZmVHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmVHeap.hh) provide named fixed- and variable-size allocation pools with partition
and NUMA-aware tuning. [`ZiHeapTune`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiHeapTune.hh) connects runtime allocation measurements
to configuration. See [heap sizing](zm/README.md) for the precise capacity model.

### Rings, scheduling and I/O

[`ZmRing`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh) carries fixed- or variable-size messages with
single or multiple writers and readers. Multiple-reader mode is **broadcast**:
every reader receives every message. Shard writes across rings for unicast
work distribution. [`ZmRingFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingFn.hh) carries captured functions through the ring;
[`ZmScheduler`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScheduler.hh) adds controlled worker threads, timers and destination-specific
execution. `run()` queues work; `invoke()` can execute immediately when already
on the destination thread.

[`ZiRing`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRing.hh) extends this design across processes using shared
memory, with broadcast readers. [`ZiMultiplex`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh) integrates
socket I/O with scheduling using Linux epoll or Windows I/O completion ports.
Explicit Rx and Tx ownership, pooled [`ZiIOBuf`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOBuf.hh) buffers and configurable thread
placement carry through the transport and application layers. [Zi](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiProgram.hh) also supplies
file I/O, asynchronous DNS resolution, logging integration and allocation tuning.

### Compile-time strings, matching and reflection

[`ZuString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh) is a structural compile-time string usable
directly as a template argument: `template <`[`ZuString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh)` Name> struct Field;`
permits `Field<"price">`. It is distinct from the runtime owning [`ZtString`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh)
and borrowed [`ZuSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh)/[`ZuCSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh) types. Compile-time strings are not unique to Z
([Boost.Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) has one), but Z uses them throughout field metadata and dispatch.

[`ZuMatcher`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMatcher.hh) constructs an Aho–Corasick finite-state
machine at compile time for a declared set of strings. It supports prefix,
exact and search matching, useful for field names, protocol tokens and headers.
This is a specialized multi-string matcher, rather than a regular-expression
engine. Its compile-time construction and integration with Z metadata are
notable features; large instantiations also have a compile-time cost.

[`ZuStruct`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh) and [`ZfStruct`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfStruct.hh) describe existing C++ objects, including members,
accessors, defaults, bounds, keys and format-specific field views (facets).
The same metadata supports JSON, URI, CLI, CSV, ASN.1 and D-Bus adapters, plus
native FlatBuffers and protobuf codecs. Borrowed views, typed dispatch and
direct buffer construction avoid unnecessary intermediate object trees.
`zflatc` and `zprotoc` supply schema tooling; normal codec use does not require
the canonical FlatBuffers or protobuf C++ runtimes.

### Network services, state and operations

Z supplies TCP, TLS and QUIC transports, HTTP/1.1, HTTP/2 and HTTP/3 clients
and servers, WebSockets, typed REST endpoints, JSON-RPC and MCP. The HTTP layer
owns connection pools, request streams, framing and protocol selection; higher
layers reuse its lifetimes and Rx/Tx scheduling. See each module's documentation
for protocol support rather than assuming every extension is implemented.

[`Zdb`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh) combines typed tables and indices, asynchronous backing stores, row
caches, sharding, replication, leader election/failover and recoverable sagas.
Its replication is asynchronous and its election is RAFT-like; it is not a claim
of synchronous Raft consensus. SQLite and PostgreSQL backing stores are supplied.
[`Zdf`](https://github.com/kaldron-labs/z/blob/main/zdf/src/ZdfSeries.hh) builds compressed typed dataframes and time series on [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh), with historical
readers that become live subscribers, seeks, retention and rolling statistics.

[`Zum`](https://github.com/kaldron-labs/z/blob/main/zum/src/zumd_saga.hh) adds an embeddable authentication library and an OAuth 2.0/OIDC identity
server with persistent authorization state. [`Ztc`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcTypes.hh) publishes runtime telemetry
through shared-memory rings, collects it with an agent and serves it through
a hub. Heap, hash, thread and I/O statistics make the framework's resource model
observable. Terminal editing and GTK integration support operational tools.

## Finding familiar capabilities

These are equivalents **in purpose**, not interchangeable APIs, data layouts or
performance claims. A dash means no close counterpart is listed. In particular,
ordinary STL containers do not supply Z's intrusive ownership and tuning model.

### Containers and value storage

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZtArray`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtArray.hh), [`ZuArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh) | [`std::vector`](https://en.cppreference.com/cpp/container/vector), [`std::inplace_vector`](https://en.cppreference.com/cpp/container/inplace_vector) (C++26); [`std::array`](https://en.cppreference.com/cpp/container/array) has a fixed logical size | [Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) [`vector`](https://github.com/boostorg/container/blob/master/include/boost/container/vector.hpp), [`static_vector`](https://github.com/boostorg/container/blob/master/include/boost/container/static_vector.hpp) | `fbvector`, `small_vector` | [`bsl::vector`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_vector.h), [`bsl::array`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_array.h) |
| [`ZtString`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh), [`ZuCSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh) | [`std::string`](https://en.cppreference.com/cpp/string/basic_string), [`std::string_view`](https://en.cppreference.com/cpp/string/basic_string_view) | [Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) [`string`](https://github.com/boostorg/container/blob/master/include/boost/container/string.hpp), [Utility](https://github.com/boostorg/utility/blob/master/include/boost/utility.hpp) [`string_view`](https://github.com/boostorg/utility/blob/master/include/boost/utility/string_view.hpp) | `fbstring`, `StringPiece` | [`bsl::string`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h), [`bsl::string_view`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string_view.h) |
| [`ZuTuple`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTuple.hh), [`ZuUnion`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnion.hh) | [`std::tuple`](https://en.cppreference.com/cpp/utility/tuple), [`std::variant`](https://en.cppreference.com/cpp/utility/variant) | [Tuple](https://github.com/boostorg/tuple/blob/master/include/boost/tuple/tuple.hpp), [Variant2](https://github.com/boostorg/variant2/blob/master/include/boost/variant2/variant.hpp) | — | [`bsl::tuple`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h), [`bsl::variant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_variant.h) |
| [`ZmList`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmList.hh) | [`std::list`](https://en.cppreference.com/cpp/container/list) | [Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html) [`list`](https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/list.hpp) | `IntrusiveList`, `CountedIntrusiveList` | Intrusive [`bslalg::BidirectionalLinkListUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_bidirectionallinklistutil.h); owning [`bsl::list`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_list.h) |
| [`ZmStack`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmStack.hh), [`ZmQueue`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmQueue.hh) | [`std::stack`](https://en.cppreference.com/cpp/container/stack), [`std::deque`](https://en.cppreference.com/cpp/container/deque), [`std::queue`](https://en.cppreference.com/cpp/container/queue) | [Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) [`vector`](https://github.com/boostorg/container/blob/master/include/boost/container/vector.hpp), [`deque`](https://github.com/boostorg/container/blob/master/include/boost/container/deque.hpp); [CircularBuffer](https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp) | `AtomicLinkedList`, `MPMCQueue` for related stack/queue operations | [`bdlcc::Deque`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_deque.h), [`BoundedQueue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_boundedqueue.h) |
| [`ZmRBTree`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRBTree.hh) | [`std::map`](https://en.cppreference.com/cpp/container/map), [`std::set`](https://en.cppreference.com/cpp/container/set) and multi variants | [Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html) [`rbtree`](https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/rbtree.hpp), [`set`](https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/set.hpp) | `ConcurrentSkipList` for an ordered index | Intrusive [`bslalg::RbTreeUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_rbtreeutil.h); owning [`bsl::map`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_map.h), [`bsl::set`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_set.h) |
| [`ZmHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHash.hh), [`ZmLHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLHash.hh) | [`std::unordered_map`](https://en.cppreference.com/cpp/container/unordered_map), [`std::unordered_set`](https://en.cppreference.com/cpp/container/unordered_set) | [Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html) [`unordered_set`](https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/unordered_set.hpp); [Unordered](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html) node/flat hash containers | `F14NodeMap`, `F14ValueMap`, `ConcurrentHashMap` | [`bsl::unordered_map`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_unordered_map.h), [`bdlc::FlatHashMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashmap.h), [`bdlcc::StripedUnorderedMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_stripedunorderedmap.h) |
| [`ZmPolyHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolyHash.hh) | Multiple containers plus coordinated index maintenance | [MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) | — | Multiple indices plus application coordination |
| [`ZmCache`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCache.hh), [`ZmPolyCache`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolyCache.hh) | Hash/list composition; multiple-key cache needs coordinated indices | [MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) with hashed/sequenced indices and eviction policy | `EvictingCacheMap` for a single-key cache | [`bdlcc::Cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h) for a single-key cache |
| [`ZmPQueue`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh), [`ZmPQRx`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh), [`ZmPQTx`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh) | [`std::map`](https://en.cppreference.com/cpp/container/map) plus sequence-range/reassembly logic | [ICL](https://github.com/boostorg/icl/blob/master/include/boost/icl/interval_set.hpp) plus an ordered index | `ConcurrentSkipList` plus sequence/reassembly logic | [`bdlcc::SkipList`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_skiplist.h) plus sequence/reassembly logic |

[`ZmPQueue`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh) is a sequence-reassembly queue with gaps, overlaps and retained
messages. Its counterparts above are building blocks for those semantics.
The [container verb Rosetta stone](docs/Z_STL_BOOST_FOLLY_BDE.md#container-verb-rosetta-stone)
shows insertion, node transfer, lookup, iteration and removal idioms.

### Foundation and runtime facilities

| Z facility | STL | Boost | Folly | BDE / NTF |
| --- | --- | --- | --- | --- |
| [`ZmRef`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRef.hh) | [`std::shared_ptr`](https://en.cppreference.com/cpp/memory/shared_ptr) serves shared ownership, but uses a control block | [`boost::intrusive_ptr`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ptr.hpp) | — | [`bsl::shared_ptr`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h) serves shared ownership |
| [`ZuTL`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTraits`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTraits.hh), [`ZuPP`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh) | Type traits and integer sequences | [Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html), [TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html), [Preprocessor](https://www.boost.org/doc/libs/latest/libs/preprocessor/doc/index.html) | Traits utilities | [`bslmf`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) metaprogramming utilities |
| [`ZuString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh) | No standard C++23 structural string class for literal template arguments | [`hana::string`](https://github.com/boostorg/hana/blob/master/include/boost/hana/string.hpp) is a related compile-time string | — | — |
| [`ZuMatcher`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMatcher.hh) | No direct compile-time multi-pattern automaton | [Regex](https://www.boost.org/doc/libs/latest/libs/regex/doc/html/boost_regex/ref.html)/[Xpressive](https://www.boost.org/doc/libs/latest/doc/html/xpressive/reference.html) solve related pattern problems, not the same fixed-string dispatch | — | — |
| [`ZmFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmFn_.hh) | [`std::function`](https://en.cppreference.com/cpp/utility/functional/function), [`std::move_only_function`](https://en.cppreference.com/cpp/utility/functional/move_only_function) | [Function](https://www.boost.org/doc/libs/latest/doc/html/function.html) | `Function` | [`bsl::function`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h) |
| [`ZmNHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh), [`ZmVHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmVHeap.hh) | [`std::pmr`](https://en.cppreference.com/cpp/memory/memory_resource) pool resources | [Pool](https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp) | `Arena` (different lifetime model) | [`bdlma::ConcurrentPool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentpool.h), [`bdlma::ConcurrentPoolAllocator`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentpoolallocator.h) |
| [`ZmScheduler`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScheduler.hh) | Threads and synchronization primitives, no equivalent sharded scheduler | [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`thread_pool`](https://github.com/boostorg/asio/blob/master/include/boost/asio/thread_pool.hpp), [`io_context`](https://github.com/boostorg/asio/blob/master/include/boost/asio/io_context.hpp), timers | `CPUThreadPoolExecutor`, `EventBase` | [`bdlmt::MultiQueueThreadPool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_multiqueuethreadpool.h), [`bdlmt::EventScheduler`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_eventscheduler.h) |
| [`ZiMultiplex`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh), [`ZiIOBuf`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOBuf.hh) | No standard C++23 asynchronous socket runtime | [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`io_context`](https://github.com/boostorg/asio/blob/master/include/boost/asio/io_context.hpp), sockets and buffer sequences | `EventBase`, `AsyncSocket`, `IOBuf` | [`ntci::Interface`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_interface.h), [`Reactor`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_reactor.h)/[`Proactor`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_proactor.h); [`bdlbb::Blob`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h) for segmented buffers |
| [`ZuStruct`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh), [`ZfStruct`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfStruct.hh) | No standard C++23 reflection facility | [Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html), [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) Struct adaptation | — | [`bdlat`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h) attribute/selection protocols |
| [`ZfJSON`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfJSON.hh), [`ZtRegex`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtRegex.hh), [`ZiLog`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh) | No JSON codec; [`std::regex`](https://en.cppreference.com/cpp/regex/basic_regex); streams cover only basic output | [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp), [Regex](https://www.boost.org/doc/libs/latest/libs/regex/doc/html/boost_regex/ref.html), [Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) | `dynamic`/JSON utilities, logging | [`baljsn`](https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_encoder.h), [`bdlpcre`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlpcre/bdlpcre_regex.h), [`ball`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_log.h) |

### Inter-thread communication: ZmRing

[`ZmRing`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh) communicates between threads through fixed- or variable-sized records,
with single/multiple producer policies and optional broadcast readers. Its
readers explicitly retire records. [`ZmRingFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingFn.hh) supplies in-ring callable dispatch.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmRing`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh) | No corresponding inter-thread ring facility | [Lockfree](https://www.boost.org/doc/libs/latest/doc/html/lockfree.html) [`spsc_queue`](https://github.com/boostorg/lockfree/blob/master/include/boost/lockfree/spsc_queue.hpp), [`queue`](https://github.com/boostorg/lockfree/blob/master/include/boost/lockfree/queue.hpp) for FIFO communication | [`ProducerConsumerQueue`](https://github.com/facebook/folly/blob/main/folly/ProducerConsumerQueue.h) for an SPSC FIFO ring; [`LockFreeRingBuffer`](https://github.com/facebook/folly/blob/main/folly/concurrency/container/LockFreeRingBuffer.h) for a concurrent cursor-based overwrite ring | [`bdlcc::SingleProducerSingleConsumerBoundedQueue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_singleproducersingleconsumerboundedqueue.h), [`FixedQueue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_fixedqueue.h) for typed bounded communication |
| [`ZmRingFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingFn.hh) | Callable wrappers plus a communication channel | [Function](https://www.boost.org/doc/libs/latest/doc/html/function.html) plus a [Lockfree](https://www.boost.org/doc/libs/latest/doc/html/lockfree.html) queue | `Function` plus a queue | [`bsl::function`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h) plus a bounded queue |

Folly's FIFO and overwrite-history rings provide related communication
facilities with their own delivery policies. [ZmRing](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh) additionally supports
variable-sized records and broadcast-reader retirement.

### Inter-process communication: ZiRing

[`ZiRing`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRing.hh) communicates between processes using shared memory. It builds on
[ZmRing](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh)'s record transport, with named shared mappings and broadcast readers.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZiRing`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRing.hh) | No shared-memory IPC ring facility | [Interprocess](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html) shared memory/message queues provide related building blocks | No corresponding shared-memory IPC ring facility | No corresponding shared-memory IPC ring facility |

[`ZiMultiplex`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh) combines I/O service with Z's explicit shard and buffer ownership.

Reference documentation: [Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html),
[Boost.Hana strings](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/structboost_1_1hana_1_1string.html),
[Folly components](https://github.com/facebook/folly/blob/main/folly/docs/Overview.md),
[Folly's single-producer queue](https://github.com/facebook/folly/blob/main/folly/ProducerConsumerQueue.h),
and [BDE striped hash maps](https://bloomberg.github.io/bde-resources/doxygen/bde_api_prod/group__bdlcc__stripedunorderedmap.html).

## Module map

| Module | Scope |
| --- | --- |
| [zu](zu/README.md) | Framework underlying foundation: traits, metaprogramming, strings, spans, tuples, unions, reflection, formatting, fixed/decimal arithmetic, time, UTF and C APIs |
| [zm](zm/README.md) | Multithreading foundation: threads, synchronization, intrusive containers, reference counting, delegates, rings, scheduler, heaps, topology and backtraces |
| [zt](zt/README.md) | Framework types: owning arrays/strings, scratch storage, regex, time zones, bitmaps and formatting utilities |
| [ze](ze/README.md) | Framework error handling: platform errors, event and log vocabulary; Zi supplies asynchronous log sinks |
| [zf](zf/README.md) | Structured metadata and format adapters |
| [zi](zi/README.md) | Framework I/O: files, sockets, DNS, multiplexing, pooled buffers, shared-memory rings and heap tuning |
| [zxdp](zxdp/README.md) | Linux AF_XDP packet I/O, registered arenas and raw packet pools |
| [zutcp](zutcp/README.md) | User-mode TCP/IP stack over Zxdp on Linux, integrated with ZiMultiplex; implementation in progress |
| [zpb](zpb/README.md) | Protobuf implementation: native codec and schema compiler |
| [zfb](zfb/README.md) | Flatbuffers: native codec and schema compiler |
| [zv](zv/README.md) | Framework server-side utilities: configuration trees, TOML/YAML and runtime parameter binding |
| [zdbus](zdbus/README.md) | Native Linux D-Bus client/server and typed message routing |
| [ztcp](ztcp/README.md) | Asynchronous TCP links |
| [ztls](ztls/README.md) | TLS transport, cryptography and credential storage |
| [zquic](zquic/README.md) | QUIC transport, streams, recovery, flow control, path migration and qlog |
| [zhttp](zhttp/README.md) | HTTP/1.1, HTTP/2 and HTTP/3 applications, pools and streams |
| [zws](zws/README.md) | Websockets over H1/H2/H3 |
| [zrest](zrest/README.md) | REST APIs: typed request/response catalogs, clients/servers and SSE |
| [zgrpc](zgrpc/README.md) | GRPC over HTTP/2 and HTTP/3 |
| [zjrpc](zjrpc/README.md) | JSON RPC: envelopes, correlation and dispatch |
| [zmcp](zmcp/README.md) | Model Context Protocol endpoints |
| [zdb](zdb/README.md) | Typed, sharded, replicated database and recoverable sagas |
| [zdb_sqlite](zdb_sqlite/README.md), [zdb_pq](zdb_pq/README.md) | SQLite/PostgreSQL persistence; PostgreSQL type integration |
| [zdf](zdf/README.md) | Compressed dataframes, time series, live readers and statistics |
| [zum](zum/README.md) | Authentication, authorization, OAuth 2.0/OIDC server and management tools |
| [ztc](ztc/README.md) | Telemetry publishers, collector agent and hub |
| [zrl](zrl/README.md), [zgtk](zgtk/README.md) | Terminal line editing/CLI and GTK application/model integration |
| [zdash](zdash/README.md) | Telemetry dashboard; currently outside the default top-level build |

## Build and explore

Install the requirements in [DEPENDENCIES.md](docs/DEPENDENCIES.md), then:

```sh
./z.config -c -G -P /opt/z
make -j
make install
```

This builds without GTK or PostgreSQL, retaining SQLite. The wrapper defaults
to GCC and an x64 Linux build; `-L` selects Clang and `-M` selects MinGW.
Its current defaults are not an ARM configuration recipe. Run `./z.config -h`
for options and use platform-appropriate configure/compiler flags when needed.
Installation may require permission to write the selected prefix.

io_uring is enabled by default when available. Use `-u` to build without it;
each process still checks runtime availability.

Installed headers use `<zlib/Component.hh>` and applications link the owning
module libraries. Start with the [reflection examples](zf/README.md),
[HTTP applications](zhttp/README.md), [REST examples](zrest/example/), or
[database lifecycle](zdb/README.md). Module `example/` directories contain
runnable programs. Test commands, fixture services, logs and port assignments
are in [TESTING.md](docs/TESTING.md).
