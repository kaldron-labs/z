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

See [Z, STL and Boost equivalents](docs/Z_STL_BOOST.md) for feature comparisons
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
  facilities remain accessible. Short component prefixes (`Zu`, `Zm`, `Zi`,
  etc.) work consistently with both C++ and the preprocessor.

## Core highlights

### Containers, ownership and memory

[`ZmHash`](zm/src/ZmHash.hh) is a policy-based, separately chained hash table
with configurable node ownership, optional locking and lock striping. Its
heap and bucket configuration can be tuned centrally through `ZmHashMgr`,
and its statistics feed telemetry. `ZmList` and `ZmRBTree` provide the same
framework approach to linked lists and ordered indices (red/black trees).

`ZmRef` supplies intrusive reference counting; `ZmFn` stores a function and
small context directly, allocating only for larger captures. `ZmNHeap` and
`ZmVHeap` provide named fixed- and variable-size allocation pools with partition
and NUMA-aware tuning. `ZiHeapTune` connects runtime allocation measurements
to configuration. See [heap sizing](zm/README.md) for the precise capacity model.

### Rings, scheduling and I/O

[`ZmRing`](zm/src/ZmRing.hh) carries fixed- or variable-size messages with
single or multiple writers and readers. Multiple-reader mode is **broadcast**:
every reader receives every message. Shard writes across rings for unicast
work distribution. `ZmRingFn` carries captured functions through the ring;
`ZmScheduler` adds controlled worker threads, timers and destination-specific
execution. `run()` queues work; `invoke()` can execute immediately when already
on the destination thread.

[`ZiRing`](zi/src/ZiRing.hh) extends this design across processes using shared
memory, with broadcast readers. [`ZiMultiplex`](zi/src/ZiMultiplex.hh) integrates
socket I/O with scheduling using Linux epoll or Windows I/O completion ports.
Explicit Rx and Tx ownership, pooled `ZiIOBuf` buffers and configurable thread
placement carry through the transport and application layers. Zi also supplies
file I/O, asynchronous DNS resolution, logging integration and allocation tuning.

### Compile-time strings, matching and reflection

[`ZuString`](zu/src/ZuString.hh) is a structural compile-time string usable
directly as a template argument: `template <ZuString Name> struct Field;`
permits `Field<"price">`. It is distinct from the runtime owning `ZtString`
and borrowed `ZuSpan`/`ZuCSpan` types. Compile-time strings are not unique to Z
(Boost.Hana has one), but Z uses them throughout field metadata and dispatch.

[`ZuMatcher`](zu/src/ZuMatcher.hh) constructs an Aho–Corasick finite-state
machine at compile time for a declared set of strings. It supports prefix,
exact and search matching, useful for field names, protocol tokens and headers.
This is a specialized multi-string matcher, rather than a regular-expression
engine. Its compile-time construction and integration with Z metadata are
notable features; large instantiations also have a compile-time cost.

`ZuStruct` and `ZfStruct` describe existing C++ objects, including members,
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

`Zdb` combines typed tables and indices, asynchronous backing stores, row
caches, sharding, replication, leader election/failover and recoverable sagas.
Its replication is asynchronous and its election is RAFT-like; it is not a claim
of synchronous Raft consensus. SQLite and PostgreSQL backing stores are supplied.
`Zdf` builds compressed typed dataframes and time series on Zdb, with historical
readers that become live subscribers, seeks, retention and rolling statistics.

`Zum` adds an embeddable authentication library and an OAuth 2.0/OIDC identity
server with persistent authorization state. `Ztc` publishes runtime telemetry
through shared-memory rings, collects it with an agent and serves it through
a hub. Heap, hash, thread and I/O statistics make the framework's resource model
observable. Terminal editing and GTK integration support operational tools.

## Finding familiar capabilities

These are equivalents **in purpose**, not interchangeable APIs, data layouts or
performance claims. A dash means no close counterpart is listed. In particular,
ordinary STL containers do not supply Z's intrusive ownership and tuning model.

| Z facility | STL / standard C++ | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmHash` | `std::unordered_map`, `std::unordered_set` | `boost::unordered_map`; `boost::intrusive::unordered_set` for intrusive nodes | `F14NodeMap`, `F14NodeSet` for node-based hash lookup | `bsl::unordered_map`; `bdlcc::StripedUnorderedMap` for striped locking |
| `ZmList`, `ZmRBTree` | `std::list`, `std::map`, `std::set` | Intrusive `list`, `rbtree`, `set` | — | `bsl::list`, `bsl::map`, `bsl::set` |
| `ZmRef` | `std::shared_ptr` serves shared ownership, but uses a control block | `boost::intrusive_ptr` | — | `bsl::shared_ptr` serves shared ownership |
| `ZtArray`, `ZuArray` | `std::vector`, `std::inplace_vector` (C++26); fixed storage differs from `std::array`'s fixed logical size | Container `vector`, `static_vector` | `fbvector`, `small_vector` | `bsl::vector` |
| `ZtString`, `ZuCSpan` | `std::string`, `std::string_view` | Container `string`, Utility `string_view` | `fbstring`, `StringPiece` | `bsl::string`, `bsl::string_view` |
| `ZuTuple`, `ZuUnion` | `std::tuple`, `std::variant` | Tuple, Variant2 | — | `bsl::tuple`, `bsl::variant` |
| `ZuTL`, `ZuTraits`, `ZuPP` | Type traits and integer sequences | Mp11, TypeTraits, Preprocessor | Traits utilities | `bslmf` metaprogramming utilities |
| `ZuString` | No standard C++23 structural string class for literal template arguments | `hana::string` is a related compile-time string | — | — |
| `ZuMatcher` | No direct compile-time multi-pattern automaton | Regex/Xpressive solve related pattern problems, not the same fixed-string dispatch | — | — |
| `ZmFn` | `std::function`, `std::move_only_function` | Function | `Function` | `bsl::function` |
| `ZmNHeap`, `ZmVHeap` | `std::pmr` pool resources | Pool | `Arena` (different lifetime model) | `bdlma::ConcurrentPool`, `bdlma::ConcurrentPoolAllocator` |
| `ZmRing` | No concurrent ring container | Lockfree `spsc_queue`, `queue` for queue use cases | `ProducerConsumerQueue`, `MPMCQueue` for queue use cases | `bdlcc::FixedQueue` for bounded queues |
| `ZmScheduler` | Threads and synchronization primitives, no equivalent sharded scheduler | Asio `thread_pool`, `io_context`, timers | `CPUThreadPoolExecutor`, `EventBase` | `bdlmt::MultiQueueThreadPool`, `bdlmt::EventScheduler` |
| `ZiMultiplex`, `ZiIOBuf` | No standard C++23 asynchronous socket runtime | Asio `io_context`, sockets and buffer sequences | `EventBase`, `AsyncSocket`, `IOBuf` | No direct equivalent in core BDE |
| `ZuStruct`, `ZfStruct` | No standard C++23 reflection facility | Describe, Hana Struct adaptation | — | `bdlat` attribute/selection protocols |
| `ZfJSON`, `ZtRegex`, `ZiLog` | No JSON codec; `std::regex`; streams cover only basic output | JSON, Regex, Log | `dynamic`/JSON utilities, logging | `baljsn`, `bdlpcre`, `ball` |

`ZmRing`'s variable-size records and broadcast semantics go beyond the queue
analogues above. `ZiRing` combines shared-memory transport with those semantics;
Boost.Interprocess supplies related shared-memory building blocks, not the same
ready-made ring. `ZiMultiplex` additionally combines the reactor/completion
mechanism with Z's explicit shard and buffer ownership.

Reference documentation: [Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html),
[Boost.Hana strings](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/structboost_1_1hana_1_1string.html),
[Folly components](https://github.com/facebook/folly/blob/main/folly/docs/Overview.md),
[Folly's single-producer queue](https://github.com/facebook/folly/blob/main/folly/ProducerConsumerQueue.h),
and [BDE striped hash maps](https://bloomberg.github.io/bde-resources/doxygen/bde_api_prod/group__bdlcc__stripedunorderedmap.html).

## Module map

| Module | Scope |
| --- | --- |
| [zu](zu/README.md) | Universal foundation: traits, metaprogramming, strings, spans, tuples, unions, reflection, formatting, fixed/decimal arithmetic, time, UTF and C APIs |
| [zm](zm/README.md) | Threads, synchronization, intrusive containers, reference counting, delegates, rings, scheduler, heaps, topology and backtraces |
| [zt](zt/README.md) | Owning arrays/strings, scratch storage, regex, time zones, bitmaps and formatting utilities |
| [ze](ze/README.md) | Platform errors, event and log vocabulary; Zi supplies asynchronous log sinks |
| [zf](zf/README.md) | Structured metadata and format adapters |
| [zi](zi/README.md) | Files, sockets, DNS, multiplexing, pooled buffers, shared-memory rings and heap tuning |
| [zpb](zpb/README.md), [zfb](zfb/README.md) | Native protobuf and FlatBuffers codecs and schema compilers |
| [zv](zv/README.md) | Configuration trees, TOML/YAML and runtime parameter binding |
| [zdbus](zdbus/README.md) | Native Linux D-Bus client/server and typed message routing |
| [ztcp](ztcp/README.md), [ztls](ztls/README.md) | TCP/TLS; TLS module also provides HPKE, age encryption, TOTP and credential Vault |
| [zquic](zquic/README.md) | QUIC transport, streams, recovery, flow control, path migration and qlog |
| [zhttp](zhttp/README.md), [zws](zws/README.md) | HTTP/1.1–3 applications, pools and streams; WebSockets over H1/H2/H3 |
| [zrest](zrest/README.md) | Typed request/response catalogs, REST clients/servers and SSE |
| [zjrpc](zjrpc/README.md), [zmcp](zmcp/README.md) | JSON-RPC envelopes, correlation and dispatch; Model Context Protocol endpoints |
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
