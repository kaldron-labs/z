# Z, STL, Boost, Folly, BDE/NTF and Rust equivalents

See the [container verb Rosetta stone](ROSETTA.md) for Z and STL insertion,
lookup, iteration and removal idioms.

The Rust column prefers language and standard-library APIs, then Cargo crates.
Links lead to canonical reference or crate documentation; `latest` follows releases.

**Partial** names the nearest facility or building blocks; custom integration may
be needed. **None** / **N/A** means no direct equivalent; any links provide
related language rules or alternatives. These entries do not imply matching wire
formats, allocation, ownership or lifecycle guarantees. Scroll wide tables
horizontally to compare columns.

<!-- Nonbreaking header padding sets column minima on GitHub, which removes
     inline CSS. Keep the padding when editing table headings. -->

## Contents

- [Zu - Framework underlying foundation](#zu---framework-underlying-foundation)
  - [Abbreviated forwarding and move operations](#abbreviated-forwarding-and-move-operations)
  - [Compile-time constants](#compile-time-constants)
  - [Abbreviated type transformations and normalizations](#abbreviated-type-transformations-and-normalizations)
  - [Qualifier and reference constraints](#qualifier-and-reference-constraints)
  - [Type identity and inheritance constraints](#type-identity-and-inheritance-constraints)
  - [Conversion, construction and exception traits](#conversion-construction-and-exception-traits)
  - [Conditional types and compile-time queries](#conditional-types-and-compile-time-queries)
  - [Compiler hints](#compiler-hints)
  - [Assertions, declarations and preprocessor utilities](#assertions-declarations-and-preprocessor-utilities)
  - [Object storage and lifetime operations](#object-storage-and-lifetime-operations)
  - [Array element storage and access](#array-element-storage-and-access)
  - [Primitive numeric types and traits](#primitive-numeric-types-and-traits)
  - [Checked arithmetic and floating-point intrinsics](#checked-arithmetic-and-floating-point-intrinsics)
  - [Byte order and byte swapping](#byte-order-and-byte-swapping)
  - [I/O status and demangling](#io-status-and-demangling)
  - [C interfaces](#c-interfaces)
  - [Callable construction and invocation](#callable-construction-and-invocation)
  - [Lambda traits and constraints](#lambda-traits-and-constraints)
  - [Compile-time switch and unroll](#compile-time-switch-and-unroll)
  - [Constant sequences and dispatch](#constant-sequences-and-dispatch)
  - [Constant-sequence conversions](#constant-sequence-conversions)
  - [Type-list construction, access and slicing](#type-list-construction-access-and-slicing)
  - [Type-list algorithms](#type-list-algorithms)
  - [Compile-time strings](#compile-time-strings)
  - [Ownership and scope cleanup](#ownership-and-scope-cleanup)
  - [Comparison, hashing and null sentinels](#comparison-hashing-and-null-sentinels)
  - [Bitmaps, bitfields and bit streams](#bitmaps-bitfields-and-bit-streams)
  - [Boxed scalars and formatting adapters](#boxed-scalars-and-formatting-adapters)
  - [Arrays, spans and string interoperability](#arrays-spans-and-string-interoperability)
  - [Tuples, unions and empty values](#tuples-unions-and-empty-values)
  - [Array algorithms, iterators, sorting and searching](#array-algorithms-iterators-sorting-and-searching)
  - [Text tokenization](#text-tokenization)
  - [Numeric input](#numeric-input)
  - [Numeric output and buffer sizing](#numeric-output-and-buffer-sizing)
  - [Formatting, streams and joining](#formatting-streams-and-joining)
  - [Time, identifiers and versions](#time-identifiers-and-versions)
  - [Fixed-point and decimal arithmetic](#fixed-point-and-decimal-arithmetic)
  - [Unicode conversion and measurement](#unicode-conversion-and-measurement)
  - [Binary-text and percent encoding](#binary-text-and-percent-encoding)
  - [Testing](#testing)
  - [Structure reflection and facets](#structure-reflection-and-facets)
  - [ZuMatcher — compile-time multi-string matching](#zumatcher--compile-time-multi-string-matching)
- [Zm - Multithreading foundation](#zm---multithreading-foundation)
  - [Platform and cache alignment](#platform-and-cache-alignment)
  - [Atomic operations and locks](#atomic-operations-and-locks)
  - [Conditions, semaphores and blocking adapters](#conditions-semaphores-and-blocking-adapters)
  - [Random number generation](#random-number-generation)
  - [Clocks, timing statistics and retry policies](#clocks-timing-statistics-and-retry-policies)
  - [Stack and scratch allocation](#stack-and-scratch-allocation)
  - [Heaps and allocator integration](#heaps-and-allocator-integration)
  - [Scratch-buffer codecs](#scratch-buffer-codecs)
  - [Intrusive ownership and callbacks](#intrusive-ownership-and-callbacks)
  - [Thread-local state and cleanup](#thread-local-state-and-cleanup)
  - [Assertions and diagnostic utilities](#assertions-and-diagnostic-utilities)
  - [Containers, indices and caches](#containers-indices-and-caches)
  - [ZmRing — inter-thread communication](#zmring--inter-thread-communication)
  - [Topology and threads](#topology-and-threads)
  - [Scheduling and engine lifecycle](#scheduling-and-engine-lifecycle)
  - [Runtime telemetry](#runtime-telemetry)
- [Zt - Framework types](#zt---framework-types)
  - [Platform and environment](#platform-and-environment)
  - [Boolean and enum vocabulary](#boolean-and-enum-vocabulary)
  - [Text formatting](#text-formatting)
  - [Character case and encoding conversion](#character-case-and-encoding-conversion)
  - [Owning arrays, strings and scratch buffers](#owning-arrays-strings-and-scratch-buffers)
  - [Bitmaps and sliding windows](#bitmaps-and-sliding-windows)
  - [Timezone conversion](#timezone-conversion)
  - [Demangling](#demangling)
  - [Regular expressions](#regular-expressions)
- [Ze - Framework error handling](#ze---framework-error-handling)
  - [Native errors](#native-errors)
  - [Diagnostic metadata and events](#diagnostic-metadata-and-events)
  - [Exceptions](#exceptions)
- [Zf - Structured metadata and format adapters](#zf---structured-metadata-and-format-adapters)
  - [Configuration trees and typed loading](#configuration-trees-and-typed-loading)
  - [Structure reflection and serialization metadata](#structure-reflection-and-serialization-metadata)
  - [Text and web formats](#text-and-web-formats)
  - [Configuration formats](#configuration-formats)
  - [Command-line binding](#command-line-binding)
  - [Binary wire formats](#binary-wire-formats)
- [Zi - Framework I/O](#zi---framework-io)
  - [Native paths and scatter/gather descriptors](#native-paths-and-scattergather-descriptors)
  - [Process and module utilities](#process-and-module-utilities)
  - [Directories and path completion](#directories-and-path-completion)
  - [File operations and structured output](#file-operations-and-structured-output)
  - [Addresses and name resolution](#addresses-and-name-resolution)
  - [I/O buffers, allocation and contexts](#io-buffers-allocation-and-contexts)
  - [Receive and transmit streams](#receive-and-transmit-streams)
  - [Message framing and transmission](#message-framing-and-transmission)
  - [Event loops and connection multiplexing](#event-loops-and-connection-multiplexing)
  - [ZiRing — inter-process communication using shared memory](#ziring--inter-process-communication-using-shared-memory)
  - [Logging and assertions](#logging-and-assertions)
  - [Resource tuning](#resource-tuning)
  - [I/O telemetry](#io-telemetry)
- [Zxdp - Linux AF_XDP packet I/O](#zxdp---linux-af_xdp-packet-io)
  - [Registered packet storage](#registered-packet-storage)
  - [Raw packet I/O and service](#raw-packet-io-and-service)
- [Zutcp - User-mode TCP/IP stack](#zutcp---user-mode-tcpip-stack)
  - [User-mode TCP/IP stack](#user-mode-tcpip-stack)
  - [TCP connection and recovery](#tcp-connection-and-recovery)
- [Zv - Framework server-side utilities](#zv---framework-server-side-utilities)
  - [Thread, stack and ring parameter binding](#thread-stack-and-ring-parameter-binding)
  - [Connection and multiplexer parameter binding](#connection-and-multiplexer-parameter-binding)
  - [Configuration file loading and saving](#configuration-file-loading-and-saving)
- [Zpb - Protobuf implementation](#zpb---protobuf-implementation)
  - [Protobuf wire format and typed messages](#protobuf-wire-format-and-typed-messages)
  - [Schema reflection and compiler](#schema-reflection-and-compiler)
- [Zfb - Flatbuffers](#zfb---flatbuffers)
  - [FlatBuffers construction and access](#flatbuffers-construction-and-access)
  - [Schema reflection and compiler](#schema-reflection-and-compiler-1)
- [Zdbus - D-Bus clients, servers and typed message routing](#zdbus---d-bus-clients-servers-and-typed-message-routing)
  - [Addresses, authentication and framing](#addresses-authentication-and-framing)
  - [Typed message adapters and runtime](#typed-message-adapters-and-runtime)
- [Ztcp - Asynchronous TCP links](#ztcp---asynchronous-tcp-links)
  - [Buffer policies and byte streams](#buffer-policies-and-byte-streams)
  - [TCP links and endpoints](#tcp-links-and-endpoints)
- [Ztls - TLS transport, cryptography and credential storage](#ztls---tls-transport-cryptography-and-credential-storage)
  - [Cryptographic primitives](#cryptographic-primitives)
  - [Keys, encapsulation and encrypted files](#keys-encapsulation-and-encrypted-files)
  - [Credential storage](#credential-storage)
  - [TLS byte streams and endpoints](#tls-byte-streams-and-endpoints)
- [Zquic - QUIC transport](#zquic---quic-transport)
  - [Packet, frame and crypto codecs](#packet-frame-and-crypto-codecs)
  - [Recovery, paths and stream ownership](#recovery-paths-and-stream-ownership)
  - [Transport endpoints and diagnostics](#transport-endpoints-and-diagnostics)
- [Zhttp - HTTP/1.1, HTTP/2 and HTTP/3](#zhttp---http11-http2-and-http3)
  - [URLs, fields and protocol configuration](#urls-fields-and-protocol-configuration)
  - [Discovery and event framing](#discovery-and-event-framing)
  - [Messages, body streams and response routes](#messages-body-streams-and-response-routes)
  - [HTTP wire protocols and endpoints](#http-wire-protocols-and-endpoints)
- [Zws - Websockets](#zws---websockets)
  - [URI, handshake and frame codecs](#uri-handshake-and-frame-codecs)
  - [WebSocket endpoints](#websocket-endpoints)
- [Zrest - REST APIs](#zrest---rest-apis)
  - [Typed request/response contracts](#typed-requestresponse-contracts)
  - [Adapters, completion and server-sent events](#adapters-completion-and-server-sent-events)
  - [REST endpoints](#rest-endpoints)
- [Zgrpc - GRPC](#zgrpc---grpc)
  - [Method, metadata and status vocabulary](#method-metadata-and-status-vocabulary)
  - [Envelope, protobuf and HTTP adapters](#envelope-protobuf-and-http-adapters)
  - [Call ownership and endpoints](#call-ownership-and-endpoints)
- [Zjrpc - JSON RPC](#zjrpc---json-rpc)
  - [JSON-RPC envelopes and declarations](#json-rpc-envelopes-and-declarations)
  - [Correlation, completion and batches](#correlation-completion-and-batches)
  - [Transport bindings and endpoints](#transport-bindings-and-endpoints)
- [Zmcp - Model Context Protocol](#zmcp---model-context-protocol)
  - [Typed tools and protocol vocabulary](#typed-tools-and-protocol-vocabulary)
  - [MCP endpoints](#mcp-endpoints)
- [Zdb - Typed, sharded, replicated database](#zdb---typed-sharded-replicated-database)
  - [Schema, rows and indices](#schema-rows-and-indices)
  - [Database operations and lifecycle](#database-operations-and-lifecycle)
  - [Replication and recoverable workflows](#replication-and-recoverable-workflows)
  - [Pluggable backing stores](#pluggable-backing-stores)
- [ZdbPQ - PostgreSQL backing store and native types](#zdbpq---postgresql-backing-store-and-native-types)
  - [PostgreSQL backing store](#postgresql-backing-store)
- [ZdbSL - SQLite backing store](#zdbsl---sqlite-backing-store)
  - [SQLite backing store and inspection](#sqlite-backing-store-and-inspection)

# Zu - Framework underlying foundation

## Abbreviated forwarding and move operations

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L334"><code>ZuFwd</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/forward"><code>std::forward</code></a></td>
  <td><a href="https://github.com/boostorg/move/blob/master/include/boost/move/utility_core.hpp"><code>boost::forward</code></a> (<a href="https://www.boost.org/doc/libs/latest/doc/html/move.html">Move</a>)</td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h"><code>bslmf::Util::forward</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/expressions.html#place-expressions-and-value-expressions">moves and borrows</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L343"><code>ZuMv</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/move"><code>std::move</code></a></td>
  <td><a href="https://github.com/boostorg/move/blob/master/include/boost/move/utility_core.hpp"><code>boost::move</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_movableref.h"><code>bslmf::MovableRefUtil::move</code></a></td>
  <td><a href="https://doc.rust-lang.org/reference/expressions.html#moved-and-copied-types">implicit moves</a>; <a href="https://doc.rust-lang.org/reference/types/closure.html#capture-modes"><code>move</code> captures</a> move captured ownership</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuMvPtr</code></a><br>➕ nulls a moved raw pointer as well as moving smart pointers</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/exchange"><code>std::exchange</code></a>, <a href="https://en.cppreference.com/cpp/utility/move"><code>std::move</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/exchange.hpp"><code>boost::exchange</code></a>, <a href="https://github.com/boostorg/move/blob/master/include/boost/move/utility_core.hpp"><code>boost::move</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_utility.h"><code>bsl::exchange</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/option/enum.Option.html#method.take"><code>Option::take</code></a>; <a href="https://doc.rust-lang.org/std/mem/fn.replace.html"><code>mem::replace</code></a> for raw pointers</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L365"><code>ZuFwdLike</code></a><br>➕ optional target-type conversion</td>
  <td><a href="https://en.cppreference.com/cpp/utility/forward_like"><code>std::forward_like</code></a> (C++23)</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/move.html">Boost.Move</a>, <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">TypeTraits</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Utility.h"><code>folly::forward_like</code></a><br>➖ no separate target-type conversion overload</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h"><code>bslmf::Util::forward_like</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
</tbody>
</table>

## Compile-time constants

<table>
<thead>
<tr>
<th align="left" scope="col" width="340">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
<th align="left" scope="col" width="240">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
<th align="left" scope="col" width="240">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
<th align="left" scope="col" width="240">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
<th align="left" scope="col" width="240">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
<th align="left" scope="col" width="280">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuConstant</code></a><code>&lt;T, V&gt;</code><br/>Typed constant; reference access; e.g. <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh"><code>ZuString</code></a></td>
<td><a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::integral_constant</code></a><code>&lt;T, V&gt;</code><br/>Value access</td>
<td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp"><code>boost::integral_constant</code></a></td>
<td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::vtag_t&lt;V&gt;</code></a></td>
<td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::integral_constant</code></a><code>&lt;T, V&gt;</code><br/>Value access</td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const</code> items</a><br/><a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-constants">associated constants</a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a> (restricted types)</td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuInt</code></a></td>
<td><a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::integral_constant</code></a><code>&lt;int, I&gt;</code></td>
<td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_int</code></a><code>&lt;I&gt;</code></td>
<td>N/A</td>
<td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::integral_constant</code></a><code>&lt;int, I&gt;</code></td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const N: i32</code></a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuUnsigned</code></a></td>
<td><a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::integral_constant</code></a><code>&lt;unsigned, I&gt;</code></td>
<td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp"><code>boost::integral_constant</code></a><code>&lt;unsigned, I&gt;</code></td>
<td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::index_constant&lt;I&gt;</code></a></td>
<td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::integral_constant</code></a><code>&lt;unsigned, I&gt;</code></td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const N: u32</code></a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuBool</code></a></td>
<td><a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::bool_constant</code></a></td>
<td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_bool</code></a></td>
<td>N/A</td>
<td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::integral_constant</code></a><code>&lt;bool, B&gt;</code></td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const B: bool</code></a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuTrue</code></a></td>
<td><a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::true_type</code></a></td>
<td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp"><code>boost::true_type</code></a></td>
<td>N/A</td>
<td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::true_type</code></a></td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const V: bool = true</code></a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuFalse</code></a></td>
<td><a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::false_type</code></a></td>
<td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp"><code>boost::false_type</code></a></td>
<td>N/A</td>
<td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::false_type</code></a></td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const V: bool = false</code></a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuAlwaysTrue</code></a></td>
<td>Partial: <a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::true_type</code></a></td>
<td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_true</code></a></td>
<td>N/A</td>
<td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::true_type</code></a></td>
<td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-constants">associated constants</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuAlwaysFalse</code></a></td>
<td>Partial: <a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::false_type</code></a></td>
<td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_false</code></a></td>
<td>N/A</td>
<td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::false_type</code></a></td>
<td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-constants">associated constants</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh"><code>ZuInt128</code></a></td>
<td>Partial: <a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::integral_constant</code></a></td>
<td>Partial: constant wrapper over a supported 128-bit type</td>
<td>N/A</td>
<td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::integral_constant</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_bit.h"><code>bit</code></a></td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const N: i128</code></a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a></td>
</tr>
<tr valign="top">
<td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh"><code>ZuUInt128</code></a></td>
<td>Partial: <a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::integral_constant</code></a></td>
<td>Partial: constant wrapper over a supported 128-bit type</td>
<td>N/A</td>
<td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::integral_constant</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_bit.h"><code>bit</code></a></td>
<td><a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const N: u128</code></a><br/><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a></td>
</tr>
</tbody>
</table>

## Abbreviated type transformations and normalizations

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuDeref</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/remove_reference"><code>std::remove_reference_t</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/remove_reference.hpp"><code>boost::remove_reference_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_reference</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/type-aliases.html">type aliases</a>, <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuStrip</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/remove_cv"><code>std::remove_cv_t</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/remove_cv.hpp"><code>boost::remove_cv_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_cv</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuDecay</code></a><br>➖ does not decay arrays/functions to pointers</td>
  <td><a href="https://en.cppreference.com/cpp/types/remove_cvref"><code>std::remove_cvref_t</code></a><br>➕ matches the actual operation; <a href="https://en.cppreference.com/cpp/types/decay"><code>std::decay_t</code></a> has different semantics</td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/remove_cv_ref.hpp"><code>boost::remove_cv_ref_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_cvref</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/type-aliases.html">type aliases</a>, <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuAsIs</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/type_identity"><code>std::type_identity_t</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/type_identity.hpp"><code>boost::type_identity_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::type_identity</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/type-aliases.html">type alias</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuMkConst</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/add_cv"><code>std::add_const_t</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_const.hpp"><code>boost::add_const_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::add_const</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuMkVolatile</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/add_cv"><code>std::add_volatile_t</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_volatile.hpp"><code>boost::add_volatile_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::add_volatile</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/std/ptr/fn.read_volatile.html"><code>ptr::read_volatile</code></a>, <a href="https://doc.rust-lang.org/std/ptr/fn.write_volatile.html"><code>ptr::write_volatile</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuMkRRef</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/add_reference"><code>std::add_rvalue_reference_t</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_rvalue_reference.hpp"><code>boost::add_rvalue_reference_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::add_rvalue_reference</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuMkLRef</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/add_reference"><code>std::add_lvalue_reference_t</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_lvalue_reference.hpp"><code>boost::add_lvalue_reference_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::add_lvalue_reference</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuMkCRef</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/add_reference"><code>std::add_lvalue_reference_t</code></a>, <a href="https://en.cppreference.com/cpp/types/add_cv"><code>std::add_const_t</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_lvalue_reference.hpp"><code>boost::add_lvalue_reference_t</code></a>, <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_const.hpp"><code>boost::add_const_t</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::add_lvalue_reference_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::add_const_t</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuRDecay</code></a><br>➕ ADL-customizable recursive decay of composites</td>
  <td>None: <a href="https://en.cppreference.com/cpp/types/remove_cvref"><code>std::remove_cvref_t</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html">Boost.Mp11</a>, <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_cvref</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuNorm.hh"><code>ZuNorm</code></a><br>➕ customizable normalization of representation-equivalent character types</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/remove_cvref"><code>std::remove_cvref_t</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_cvref</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuEquiv.hh"><code>ZuEquiv</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_same"><code>std::is_same</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp"><code>boost::is_same</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_same</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>remove_cvref</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAlt.hh"><code>ZuAlt</code></a><br>➕ char/wchar_t alternate-type mapping</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/std/primitive.char.html"><code>char</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuUnder</code></a><br>➕ customizable for wrappers as well as enums</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/underlying_type"><code>std::underlying_type_t</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/underlying_type.hpp"><code>boost::underlying_type</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::underlying_type</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a>, <a href="https://doc.rust-lang.org/reference/type-layout.html#primitive-representations"><code>repr</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L747"><code>ZuUnderlying</code></a><br>➕ unwraps participating wrapper types as well as enums</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/to_underlying"><code>std::to_underlying</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/scoped_enum.hpp"><code>boost::underlying_cast</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Utility.h"><code>folly::to_underlying</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_utility.h"><code>bsl::to_underlying</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/expressions/operator-expr.html#type-cast-expressions"><code>as</code> casts</a>, <a href="https://doc.rust-lang.org/std/convert/trait.From.html"><code>From</code></a></td>
</tr>
</tbody>
</table>

## Qualifier and reference constraints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsConst</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/is_const"><code>std::is_const</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_const.hpp"><code>boost::is_const</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_const</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsVolatile</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/is_volatile"><code>std::is_volatile</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_volatile.hpp"><code>boost::is_volatile</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_volatile</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/std/ptr/fn.read_volatile.html"><code>volatile reads</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuConst</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a><code>&lt;</code><a href="https://en.cppreference.com/cpp/types/is_const"><code>std::is_const_v</code></a><code>&lt;U&gt;, R&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp"><code>boost::enable_if</code></a><code>&lt;</code><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_const.hpp"><code>boost::is_const</code></a><code>&lt;U&gt;, R&gt;::type</code></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_const_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuMutable</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a><code>&lt;!</code><a href="https://en.cppreference.com/cpp/types/is_const"><code>std::is_const_v</code></a><code>&lt;U&gt;, R&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp"><code>boost::disable_if</code></a><code>&lt;</code><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_const.hpp"><code>boost::is_const</code></a><code>&lt;U&gt;, R&gt;::type</code></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_const_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuVolatile</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a><code>&lt;</code><a href="https://en.cppreference.com/cpp/types/is_volatile"><code>std::is_volatile_v</code></a><code>&lt;U&gt;, R&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp"><code>boost::enable_if</code></a><code>&lt;</code><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_volatile.hpp"><code>boost::is_volatile</code></a><code>&lt;U&gt;, R&gt;::type</code></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_volatile_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/std/ptr/fn.read_volatile.html"><code>volatile reads</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNonVolatile</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a><code>&lt;!</code><a href="https://en.cppreference.com/cpp/types/is_volatile"><code>std::is_volatile_v</code></a><code>&lt;U&gt;, R&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp"><code>boost::disable_if</code></a><code>&lt;</code><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_volatile.hpp"><code>boost::is_volatile</code></a><code>&lt;U&gt;, R&gt;::type</code></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_volatile_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/std/ptr/fn.read_volatile.html"><code>volatile reads</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsLRef</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/is_lvalue_reference"><code>std::is_lvalue_reference</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_lvalue_reference.hpp"><code>boost::is_lvalue_reference</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_lvalue_reference</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsCRef</code></a></td>
  <td>Composition: lvalue-reference test and const-referent test</td>
  <td>Composition of <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::conjunction</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_lvalue_reference</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_const</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_reference_t</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsRRef</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/is_rvalue_reference"><code>std::is_rvalue_reference</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_rvalue_reference.hpp"><code>boost::is_rvalue_reference</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_rvalue_reference</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuLRef</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a><code>&lt;</code><a href="https://en.cppreference.com/cpp/types/is_lvalue_reference"><code>std::is_lvalue_reference_v</code></a><code>&lt;U&gt;, R&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a> + <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_lvalue_reference.hpp"><code>boost::is_lvalue_reference</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_lvalue_reference_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNotLRef</code></a></td>
  <td>Negated <a href="https://en.cppreference.com/cpp/types/is_lvalue_reference"><code>std::is_lvalue_reference</code></a> constraint</td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a> + <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_lvalue_reference.hpp"><code>boost::is_lvalue_reference</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_lvalue_reference_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuCRef</code></a></td>
  <td>Constraint on const lvalue references</td>
  <td>Composition of <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a> and <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">TypeTraits</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_lvalue_reference_v</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_const_v</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_reference_t</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNotCRef</code></a></td>
  <td>Negated const-lvalue-reference constraint</td>
  <td>Composition of <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a> and <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">TypeTraits</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_lvalue_reference_v</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_const_v</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::remove_reference_t</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuRRef</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a><code>&lt;</code><a href="https://en.cppreference.com/cpp/types/is_rvalue_reference"><code>std::is_rvalue_reference_v</code></a><code>&lt;U&gt;, R&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a> + <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_rvalue_reference.hpp"><code>boost::is_rvalue_reference</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_rvalue_reference_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNotRRef</code></a></td>
  <td>Negated <a href="https://en.cppreference.com/cpp/types/is_rvalue_reference"><code>std::is_rvalue_reference</code></a> constraint</td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a> + <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_rvalue_reference.hpp"><code>boost::is_rvalue_reference</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_rvalue_reference_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">references and raw pointers</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
</tbody>
</table>

## Type identity and inheritance constraints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsSame</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/is_same"><code>std::is_same</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp"><code>boost::is_same</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_same</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/any/struct.TypeId.html"><code>TypeId</code></a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuSame</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a><code>&lt;</code><a href="https://en.cppreference.com/cpp/types/is_same"><code>std::is_same_v</code></a><code>&lt;A, B&gt;, R&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a> + <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp"><code>boost::is_same</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_same_v</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a>, <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated-type equality</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNotSame</code></a></td>
  <td>Negated <a href="https://en.cppreference.com/cpp/types/is_same"><code>std::is_same</code></a> constraint</td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a> + <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp"><code>boost::is_same</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_same_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIs</code></a><br>➕ decays types and accepts identity or accessible pointer base conversion</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/concepts/same_as"><code>std::same_as</code></a>, <a href="https://en.cppreference.com/cpp/concepts/derived_from"><code>std::derived_from</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a>, <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">EnableIf</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>remove_cvref</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/items/traits.html#supertraits">supertraits</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsNot</code></a></td>
  <td>Negation of the identity/base-conversion constraint</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a>, <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">DisableIf</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/items/traits.html#supertraits">supertraits</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsBase</code></a><code>&lt;</code><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>From</code></a><code>, </code><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>To</code></a><code>&gt;</code> — derived-to-base argument order; Z identity/access rules</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_base_of"><code>std::is_base_of</code></a>, <a href="https://en.cppreference.com/cpp/concepts/derived_from"><code>std::derived_from</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_base_of.hpp"><code>boost::is_base_of</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_base_of</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>is_convertible</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/items/traits.html#supertraits">supertraits</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuBase</code></a></td>
  <td>Constraint on strict accessible base conversion</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a>, <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">TypeTraits</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/items/traits.html#supertraits">supertraits</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNotBase</code></a></td>
  <td>Negated strict accessible base-conversion constraint</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a>, <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">TypeTraits</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/items/traits.html#supertraits">supertraits</a></td>
</tr>
</tbody>
</table>

## Conversion, construction and exception traits

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsConvertible</code></a> — reference-normalized probe semantics</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_convertible"><code>std::is_convertible</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp"><code>boost::is_convertible</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_convertible</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/convert/trait.From.html"><code>From</code></a>, <a href="https://doc.rust-lang.org/std/convert/trait.TryFrom.html"><code>TryFrom</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuConvertible</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a>, <a href="https://en.cppreference.com/cpp/types/is_convertible"><code>std::is_convertible_v</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a>, <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp"><code>boost::is_convertible</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_convertible_v</code></a></td>
  <td><a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a> on <a href="https://doc.rust-lang.org/std/convert/trait.Into.html"><code>Into</code></a> / <a href="https://doc.rust-lang.org/std/convert/trait.TryInto.html"><code>TryInto</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNotConvertible</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_convertible"><code>std::is_convertible</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a>, <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp"><code>boost::is_convertible</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_convertible_v</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIsConstructible</code></a><br>➖ source/destination API and cast probing differ</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_constructible"><code>std::is_constructible</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_constructible.hpp"><code>boost::is_constructible</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_constructible</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/default/trait.Default.html"><code>Default</code></a>, <a href="https://doc.rust-lang.org/std/convert/trait.From.html"><code>From</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuConstructible</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a>, <a href="https://en.cppreference.com/cpp/types/is_constructible"><code>std::is_constructible_v</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a>, <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_constructible.hpp"><code>boost::is_constructible</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_constructible_v</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/default/trait.Default.html"><code>Default</code></a>, <a href="https://doc.rust-lang.org/std/convert/trait.From.html"><code>From</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNotConstructible</code></a></td>
  <td>Partial: negated constructibility constraint</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a>, <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_constructible.hpp"><code>boost::is_constructible</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_constructible_v</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/default/trait.Default.html"><code>Default</code></a>, <a href="https://doc.rust-lang.org/std/convert/trait.From.html"><code>From</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNXConstruct</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_default_constructible"><code>std::is_nothrow_default_constructible</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/has_nothrow_constructor.hpp"><code>boost::has_nothrow_constructor</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_nothrow_constructible</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/panic.html">panics</a>, <a href="https://doc.rust-lang.org/std/result/enum.Result.html"><code>Result</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNXMove</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_move_constructible"><code>std::is_nothrow_move_constructible</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_nothrow_move_constructible.hpp"><code>boost::is_nothrow_move_constructible</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_nothrow_move_constructible</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/panic.html">panics</a>, <a href="https://doc.rust-lang.org/std/result/enum.Result.html"><code>Result</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNXCopy</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_copy_constructible"><code>std::is_nothrow_copy_constructible</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/has_nothrow_copy.hpp"><code>boost::has_nothrow_copy</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_nothrow_copy_constructible</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/panic.html">panics</a>, <a href="https://doc.rust-lang.org/std/result/enum.Result.html"><code>Result</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNXDestroy</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_destructible"><code>std::is_nothrow_destructible</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/has_nothrow_destructor.hpp"><code>boost::has_nothrow_destructor</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_nothrow_destructible</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/panic.html">panics</a>, <a href="https://doc.rust-lang.org/std/result/enum.Result.html"><code>Result</code></a></td>
</tr>
</tbody>
</table>

## Conditional types and compile-time queries

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIf</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/conditional"><code>std::conditional_t</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_if_c</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::conditional_t</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::conditional</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a>, <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuIfT</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/enable_if"><code>std::enable_if_t</code></a></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp"><code>boost::enable_if_c</code></a><code>&lt;...&gt;::type</code></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::enable_if</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a>, <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuDeclVal</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/declval"><code>std::declval</code></a></td>
  <td><a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/declval.hpp"><code>boost::declval</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Utility.h"><code>FOLLY_DECLVAL</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h"><code>bslmf::Util::declval</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/items/associated-items.html">associated items</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L753"><code>ZuConstEval</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/is_constant_evaluated"><code>std::is_constant_evaluated</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_constant_evaluated</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/const_eval.html">constant evaluation</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuSize</code></a><br>➕ zero logical size for void/empty types; ordinary sizeof otherwise</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_empty"><code>std::is_empty</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::is_empty</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/mem/fn.size_of.html"><code>mem::size_of</code></a>; zero-sized types have size zero, including <code>()</code></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLargest.hh"><code>ZuLargest</code></a><br>➕ selects a type from a pack or <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh">ZuTypeList</a>; first wins ties; handles void</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html">Boost.Mp11</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/mem/fn.size_of.html"><code>size_of</code></a>, <a href="https://doc.rust-lang.org/std/mem/fn.align_of.html"><code>align_of</code></a>, <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMostAligned.hh"><code>ZuMostAligned</code></a><br>➕ selects the most-aligned type from a pack or <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh">ZuTypeList</a>; first wins ties</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html">Boost.Mp11</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/mem/fn.size_of.html"><code>size_of</code></a>, <a href="https://doc.rust-lang.org/std/mem/fn.align_of.html"><code>align_of</code></a>, <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTraits.hh"><code>ZuTraits</code></a><br>➕ common ADL vocabulary for strings, containers and numerics</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/header/type_traits"><code>&lt;type_traits&gt;</code></a>, <a href="https://en.cppreference.com/cpp/ranges"><code>std::ranges</code></a>, <a href="https://en.cppreference.com/cpp/types/numeric_limits"><code>std::numeric_limits</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a>, <a href="https://github.com/boostorg/range/blob/master/include/boost/range.hpp">Range</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::IsRelocatable / detection traits</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntccfg/ntccfg_traits.h"><code>traits</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h"><code>bdlat_TypeCategory</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a>, <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
</tbody>
</table>

## Compiler hints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L138"><code>ZuLikely</code></a><br>➕ expression-level branch hint, usable inside compound conditions</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp"><code>BOOST_LIKELY</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Likely.h"><code>FOLLY_LIKELY</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_performancehint.h"><code>BSLS_PERFORMANCEHINT_PREDICT_LIKELY</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/hint/fn.cold_path.html"><code>hint::cold_path</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L139"><code>ZuUnlikely</code></a><br>➕ expression-level branch hint, usable inside compound conditions</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp"><code>BOOST_UNLIKELY</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Likely.h"><code>FOLLY_UNLIKELY</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_performancehint.h"><code>BSLS_PERFORMANCEHINT_PREDICT_UNLIKELY</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/hint/fn.cold_path.html"><code>hint::cold_path</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuInline</code></a><br>➕ requests always_inline on supported compilers</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp"><code>BOOST_FORCEINLINE</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/CPortability.h"><code>FOLLY_ALWAYS_INLINE</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/reference/attributes/codegen.html#the-inline-attribute"><code>#[inline(always)]</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuNoInline</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp"><code>BOOST_NOINLINE</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/CPortability.h"><code>FOLLY_NOINLINE</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/reference/attributes/codegen.html#the-inline-attribute"><code>#[inline(never)]</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L145"><code>ZuUnreachable</code></a><br>➕ debug build aborts at the failed assumption</td>
  <td><a href="https://en.cppreference.com/cpp/utility/unreachable"><code>std::unreachable</code></a> (C++23)</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp"><code>BOOST_UNREACHABLE_RETURN</code></a> (partial return-site idiom)</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/lang/Assume.h"><code>FOLLY_ASSUME_UNREACHABLE</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/macro.unreachable.html"><code>unreachable!</code></a> (panics); <a href="https://doc.rust-lang.org/std/hint/fn.unreachable_unchecked.html"><code>unreachable_unchecked</code></a> (unsafe optimizer assumption)</td>
</tr>
</tbody>
</table>

## Assertions, declarations and preprocessor utilities

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAssert.hh#L28"><code>ZuAssert</code></a><br>➕ deferred <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh">ZuPP</a> expansion and optional expression-text diagnostics</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/static_assert.hpp"><code>BOOST_STATIC_ASSERT</code></a>, <a href="https://github.com/boostorg/config/blob/master/include/boost/static_assert.hpp"><code>BOOST_STATIC_ASSERT_MSG</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_assert.h"><code>BSLMF_ASSERT</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.assert.html"><code>assert!</code></a> in a <a href="https://doc.rust-lang.org/reference/expressions/block-expr.html#const-blocks"><code>const</code> block</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAssert.hh#L32"><code>ZuCAssert</code></a><br>➕ shorter spelling for a nonzero compile-time assertion<br>➖ older switch/case idiom; no semantic advantage over static_assert</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/static_assert.hpp"><code>BOOST_STATIC_ASSERT</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_assert.h"><code>BSLMF_ASSERT</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.assert.html"><code>assert!</code></a> in a <a href="https://doc.rust-lang.org/reference/expressions/block-expr.html#const-blocks"><code>const</code> block</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuOpBool</code></a><br>➕ emits a truth conversion from the existing operator! without repeating its logic<br>➖ pointer conversion permits more conversions than explicit bool</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/explicit_operator_bool.hpp"><code>BOOST_EXPLICIT_OPERATOR_BOOL</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_unspecifiedbool.h"><code>bsls::UnspecifiedBool</code></a></td>
  <td>None: <a href="https://doc.rust-lang.org/reference/expressions/if-expr.html"><code>if</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDerive.hh#L61"><code>ZuDerive</code></a><br>➕ short distinct type ID reduces complex template names in symbols<br>➕ generates constrained forwarding construction and derived-returning assignment</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/serialization/blob/master/include/boost/serialization/strong_typedef.hpp">Boost.StrongTypedef</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/reference/types/struct.html">newtype struct</a>; implement conversions and forwarding explicitly or with derives</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh"><code>ZuPP</code></a> macro family<br>➕ deferred recursive expansion, tuple manipulation and argument mapping</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/preprocessor/doc/index.html">Boost.Preprocessor</a><br>➕ broader documented macro algorithms</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Preprocessor.h"><code>FOLLY_PP_*</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_macrorepeat.h"><code>BSLS_MACROREPEAT</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh">ZuPP</a></td>
  <td><a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a> / <a href="https://doc.rust-lang.org/reference/procedural-macros.html">procedural macros</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuReserved.hh#L117"><code>ZuIfReserved</code></a><br>➕ branches on C++23 keyword tokens</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/preprocessor/doc/index.html">Boost.Preprocessor</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/conditional-compilation.html"><code>cfg</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFnName.hh"><code>ZuFnName</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/source_location/function_name"><code>std::source_location::function_name</code></a><code>()</code></td>
  <td><a href="https://github.com/boostorg/assert/blob/master/include/boost/current_function.hpp"><code>BOOST_CURRENT_FUNCTION</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/macro.module_path.html"><code>module_path!</code></a>, <a href="https://doc.rust-lang.org/std/panic/struct.Location.html"><code>Location</code></a></td>
</tr>
</tbody>
</table>

## Object storage and lifetime operations

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L183"><code>ZuLaunder</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/launder"><code>std::launder</code></a></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/launder.hpp"><code>boost::core::launder</code></a></td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/std/mem/union.MaybeUninit.html"><code>MaybeUninit</code></a>, <a href="https://doc.rust-lang.org/std/ptr/index.html"><code>raw-pointer APIs</code></a> (unsafe storage APIs)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L758"><code>ZuAddr</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/memory/addressof"><code>std::addressof</code></a></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/addressof.hpp"><code>boost::addressof</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h"><code>bsl::addressof</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/ptr/fn.from_ref.html"><code>ptr::from_ref</code></a> / <a href="https://doc.rust-lang.org/std/ptr/fn.from_mut.html"><code>ptr::from_mut</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L776"><code>ZuNew</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/memory/construct_at"><code>std::construct_at</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/container.html">Boost.Container</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_constructionutil.h"><code>bslma::ConstructionUtil::construct</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/ptr/fn.write.html"><code>ptr::write</code></a> / <a href="https://doc.rust-lang.org/std/mem/union.MaybeUninit.html#method.write"><code>MaybeUninit::write</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L783"><code>ZuCast</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/numeric/bit_cast"><code>std::bit_cast</code></a></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp"><code>boost::core::bit_cast</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/mem/fn.transmute.html"><code>mem::transmute</code></a> (unsafe bit reinterpretation); <a href="https://docs.rs/bytemuck/latest/bytemuck/fn.cast.html"><code>bytemuck::cast</code></a> with bit-pattern trait bounds</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuPun</code></a><br>➕ reusable union with forwarding input construction and suppressed member destruction<br>➖ caller manages active-member lifetime and aliasing</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/numeric/bit_cast"><code>std::bit_cast</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp"><code>boost::core::bit_cast</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/reference/items/unions.html"><code>union</code></a> with <a href="https://doc.rust-lang.org/std/mem/struct.ManuallyDrop.html"><code>ManuallyDrop</code></a>; unsafe access and explicit lifetime management</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L642"><code>ZuAlloca</code></a><br>➕ concise aligned stack allocation with runtime size<br>➖ no stack-capacity fallback</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/alloca/latest/alloca/fn.with_alloca.html"><code>alloca::with_alloca</code></a> (closure-scoped bytes; custom alignment)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L127"><code>ZuClear</code></a><br>➕ OS-backed clearing resistant to dead-store elimination</td>
  <td>None: <a href="https://en.cppreference.com/cpp/string/byte/memset"><code>std::memset</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/zeroize/latest/zeroize/"><code>zeroize</code></a>; explicit zeroing resistant to dead-store elimination</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuCanOverlap</code></a><br>➕ shorter framework spelling of the standard attribute<br>➖ naming alias only; identical attribute semantics</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp"><code>BOOST_ATTRIBUTE_NO_UNIQUE_ADDRESS</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/type-layout.html">zero-sized field layout</a></td>
</tr>
</tbody>
</table>

## Array element storage and access

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh"><code>ZuElem</code></a><br>➕ aligned structural union with no initial active element and explicit lifetime control<br>➕ short underlying type ID limits symbol/debug-name expansion</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/construct_at"><code>std::construct_at</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp">Boost.Optional</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_objectbuffer.h"><code>bsls::ObjectBuffer</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/mem/union.MaybeUninit.html"><code>MaybeUninit&lt;T&gt;</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh"><code>ZuIsElem</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh"><code>ZuElemType</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a>, <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh"><code>ZuElemVal</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/mem/union.MaybeUninit.html#method.assume_init_ref"><code>MaybeUninit::assume_init_ref</code></a> / <a href="https://doc.rust-lang.org/std/mem/union.MaybeUninit.html#method.assume_init_mut"><code>assume_init_mut</code></a> (unsafe)</td>
</tr>
</tbody>
</table>

## Primitive numeric types and traits

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh"><code>int128_t</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh"><code>uint128_t</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuInt</code></a><code>.hh</code>)<br>➕ native compiler integer operations on supported targets<br>➖ compiler-extension types</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp"><code>boost::multiprecision::int128_t</code></a>, <a href="https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp"><code>uint128_t</code></a><br>➕ portable fixed-width backends</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::int128_t / uint128_t</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/primitive.i128.html"><code>i128</code></a> / <a href="https://doc.rust-lang.org/std/primitive.u128.html"><code>u128</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFP_.hh"><code>ZuFP</code></a><br>➕ IEEE representation and precision helpers</td>
  <td><a href="https://en.cppreference.com/cpp/types/numeric_limits"><code>std::numeric_limits</code></a>, <a href="https://en.cppreference.com/cpp/header/cmath"><code>&lt;cmath&gt;</code></a></td>
  <td><a href="https://github.com/boostorg/math/blob/master/include/boost/math/special_functions.hpp">Boost.Math</a> floating-point utilities</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_limits.h"><code>bsl::numeric_limits</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_float.h"><code>bdlb::Float</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.f32.html"><code>f32</code></a> / <a href="https://doc.rust-lang.org/std/primitive.f64.html"><code>f64</code></a> constants and bit conversions</td>
</tr>
</tbody>
</table>

## Checked arithmetic and floating-point intrinsics

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L179"><code>Zu_add</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L242"><code>ZuIntrin::add</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/safe_numerics/doc/html/index.html">Boost.SafeNumerics</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/CheckedMath.h"><code>folly::checked_add</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.overflowing_add"><code>overflowing_add</code></a> / <a href="https://doc.rust-lang.org/std/primitive.u64.html#method.checked_add"><code>checked_add</code></a> on integer primitives</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L180"><code>Zu_sub</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L244"><code>ZuIntrin::sub</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/safe_numerics/doc/html/index.html">Boost.SafeNumerics</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.overflowing_sub"><code>overflowing_sub</code></a> / <a href="https://doc.rust-lang.org/std/primitive.u64.html#method.checked_sub"><code>checked_sub</code></a> on integer primitives</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L181"><code>Zu_mul</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L246"><code>ZuIntrin::mul</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/safe_numerics/doc/html/index.html">Boost.SafeNumerics</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/CheckedMath.h"><code>folly::checked_mul</code></a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.overflowing_mul"><code>overflowing_mul</code></a> / <a href="https://doc.rust-lang.org/std/primitive.u64.html#method.checked_mul"><code>checked_mul</code></a> on integer primitives</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh"><code>Zu_nan*</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L250"><code>ZuIntrin::nan</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/numeric_limits/quiet_NaN"><code>std::numeric_limits&lt;T&gt;::quiet_NaN</code></a>, <a href="https://en.cppreference.com/cpp/numeric/math/nan"><code>std::nan</code></a></td>
  <td><a href="https://github.com/boostorg/math/blob/master/include/boost/math/special_functions.hpp">Boost.Math</a> floating-point utilities</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/primitive.f64.html#associatedconstant.NAN"><code>f64::NAN</code></a> / <a href="https://doc.rust-lang.org/std/primitive.f32.html#associatedconstant.NAN"><code>f32::NAN</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L197"><code>Zu_isnan</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L258"><code>ZuIntrin::isnan</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/numeric/math/isnan"><code>std::isnan</code></a></td>
  <td><a href="https://github.com/boostorg/math/blob/master/include/boost/math/special_functions/fpclassify.hpp"><code>boost::math::isnan</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_float.h"><code>bdlb::Float::isNan</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.f64.html#method.is_nan"><code>f64::is_nan</code></a> / <a href="https://doc.rust-lang.org/std/primitive.f32.html#method.is_nan"><code>f32::is_nan</code></a></td>
</tr>
</tbody>
</table>

## Byte order and byte swapping

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh"><code>ZuByteSwap</code></a><br>➕ swapped-storage wrapper</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/numeric/byteswap"><code>std::byteswap</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html">Boost.Endian</a> arithmetic/buffer types</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/Bits.h"><code>folly::Endian::swap</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorderutil.h"><code>bsls::ByteOrderUtil::swapBytes</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/primitive.u64.html#method.swap_bytes"><code>swap_bytes</code></a>, <a href="https://docs.rs/zerocopy/latest/zerocopy/byteorder/index.html"><code>zerocopy::byteorder</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh"><code>ZuBigEndian</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/endian"><code>std::endian</code></a>, <a href="https://en.cppreference.com/cpp/numeric/byteswap"><code>std::byteswap</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html">Boost.Endian</a> big-endian types</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/Bits.h"><code>folly::Endian::big</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bigendian.h"><code>bdlb::BigEndianInt16</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_int64.h"><code>Int64</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.to_be_bytes"><code>to_be_bytes</code></a> / <a href="https://doc.rust-lang.org/std/primitive.u64.html#method.from_be_bytes"><code>from_be_bytes</code></a>; <a href="https://docs.rs/zerocopy/latest/zerocopy/byteorder/index.html"><code>big-endian storage</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh"><code>ZuLittleEndian</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/endian"><code>std::endian</code></a>, <a href="https://en.cppreference.com/cpp/numeric/byteswap"><code>std::byteswap</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html">Boost.Endian</a> little-endian types</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/Bits.h"><code>folly::Endian::little</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorder.h"><code>BSLS_BYTEORDER_LE_*</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.to_le_bytes"><code>to_le_bytes</code></a> / <a href="https://doc.rust-lang.org/std/primitive.u64.html#method.from_le_bytes"><code>from_le_bytes</code></a>; <a href="https://docs.rs/zerocopy/latest/zerocopy/byteorder/index.html"><code>little-endian storage</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh"><code>Zu_bswap*</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh"><code>ZuIntrin</code></a><code>.hh</code>)</td>
  <td><a href="https://en.cppreference.com/cpp/numeric/byteswap"><code>std::byteswap</code></a> (C++23)</td>
  <td><a href="https://github.com/boostorg/endian/blob/master/include/boost/endian/conversion.hpp"><code>boost::endian::endian_reverse</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/lang/Bits.h"><code>folly::Endian::swap</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorderutil.h"><code>bsls::ByteOrderUtil::swapBytes</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.swap_bytes"><code>integer::swap_bytes</code></a></td>
</tr>
</tbody>
</table>

## I/O status and demangling

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIOResult.hh"><code>Zu::IOResult</code></a><br>➕ small normalized I/O status vocabulary</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/error/error_code"><code>std::error_code</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html">Boost.System</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/io/type.Result.html"><code>io::Result</code></a>, <a href="https://doc.rust-lang.org/std/io/enum.ErrorKind.html"><code>io::ErrorKind</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDemangle.hh"><code>ZuDemangle</code></a></td>
  <td>None: <a href="https://en.cppreference.com/cpp/types/type_info/name"><code>typeid(T).name()</code></a></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/demangle.hpp"><code>boost::core::demangle</code></a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_typeindex.html">Boost.TypeIndex</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Demangle.h"><code>folly::demangle</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_nameof.h"><code>bsls::NameOf</code></a></td>
  <td><a href="https://docs.rs/cpp_demangle/latest/cpp_demangle/"><code>cpp_demangle</code></a> for C++; <a href="https://docs.rs/rustc-demangle/latest/rustc_demangle/"><code>rustc-demangle</code></a> for Rust symbols</td>
</tr>
</tbody>
</table>

## C interfaces

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/zu_bitmap.h"><code>zu_bitmap.h</code></a> C API</td>
  <td>None: <a href="https://en.cppreference.com/cpp/utility/bitset"><code>std::bitset</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/bitvec/latest/bitvec/"><code>bitvec</code></a>, <a href="https://doc.rust-lang.org/reference/items/external-blocks.html">C FFI</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/zu_decimal.h"><code>zu_decimal.h</code></a> C API</td>
  <td>N/A</td>
  <td>None: <a href="https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp">Boost.Multiprecision</a></td>
  <td>Partial: <a href="https://docs.rs/rust_decimal/latest/rust_decimal/"><code>rust_decimal</code></a>, <a href="https://doc.rust-lang.org/reference/items/external-blocks.html">C FFI</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/zu_time.h"><code>zu_time.h</code></a> C API</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/header/ctime"><code>&lt;time.h&gt;</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/time/index.html"><code>std::time</code></a>, <a href="https://doc.rust-lang.org/reference/items/external-blocks.html">C FFI</a></td>
</tr>
</tbody>
</table>

## Callable construction and invocation

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L652"><code>ZuDefaultAxor</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/functional/identity"><code>std::identity</code></a></td>
  <td><a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/functional/id.hpp"><code>boost::hana::id</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/convert/fn.identity.html"><code>convert::identity</code></a> / an identity closure</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuLambda</code></a><br>➕ recursive callable wrapper with cv/ref-sensitive SFINAE overloads<br>➕ documented workaround for GCC explicit-object-parameter issue 113563</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/hof/blob/master/include/boost/hof/fix.hpp"><code>boost::hof::fix</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/functions.html">functions</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDeduce.hh"><code>ZuDeduce</code></a><br>➕ extracts object, return type and argument typelist</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/result_of"><code>std::invoke_result_t</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html">Boost.CallableTraits</a><br>➕ broader callable-type transformations</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/ops/trait.FnOnce.html"><code>FnOnce::Output</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInvoke.hh#L35"><code>ZuInvoke</code></a><br>➕ accepts member, bound and unbound function shapes with optional context</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/functional/invoke"><code>std::invoke</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/compat/blob/master/include/boost/compat/invoke.hpp"><code>boost::compat::invoke</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h"><code>folly::invoke</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h"><code>bsl::invoke</code></a></td>
  <td><a href="https://doc.rust-lang.org/reference/expressions/call-expr.html">function calls</a> / <a href="https://doc.rust-lang.org/reference/expressions/method-call-expr.html">method calls</a>; <a href="https://doc.rust-lang.org/reference/types/closure.html">closure types and call traits</a> for generic invocation</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuInvokeFnT</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html">Boost.CallableTraits</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/reference/types/function-pointer.html">function pointers</a>; noncapturing closures coerce to <code>fn</code> pointers</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh#L175"><code>ZuInvokeFn</code></a><br>➕ concise named function-pointer conversion with signature deduction from lambda traits<br>➖ conversion semantics are already supplied by captureless lambdas</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/reference/types/function-pointer.html">function pointers</a>; noncapturing closures coerce to <code>fn</code> pointers</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh#L201"><code>ZuInvokeLambda</code></a><br>➕ type-only invocation with an explicit statelessness check and argument-list traits<br>➖ core construction/call already covers captureless lambdas<br>➖ placeholder-this implementation relies on supported compiler behavior</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/hof/blob/master/include/boost/hof/static.hpp"><code>boost::hof::static_</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/function-pointer.html">noncapturing closure coercion</a></td>
</tr>
</tbody>
</table>

## Lambda traits and constraints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuLambdaTraits</code></a><br>➕ generic-lambda invocation and capture-oriented traits</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_invocable"><code>std::is_invocable</code></a>, <a href="https://en.cppreference.com/cpp/types/result_of"><code>std::invoke_result</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html">Boost.CallableTraits</a>, <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">TypeTraits</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h"><code>folly::invoke_traits</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_memberfunctionpointertraits.h"><code>bslmf::MemberFunctionPointerTraits</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/ops/trait.FnOnce.html"><code>FnOnce::Output</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuArgList</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/callable_traits/blob/master/include/boost/callable_traits/args.hpp"><code>boost::callable_traits::args_t</code></a> (partial for generic lambdas)</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_memberfunctionpointertraits.h"><code>bslmf::MemberFunctionPointerTraits</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/ops/trait.FnOnce.html"><code>FnOnce::Output</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuLambdaReturn</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/result_of"><code>std::invoke_result_t</code></a> with supplied argument types</td>
  <td><a href="https://github.com/boostorg/callable_traits/blob/master/include/boost/callable_traits/return_type.hpp"><code>boost::callable_traits::return_type_t</code></a> (partial for generic lambdas)</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h"><code>folly::invoke_result_t</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::invoke_result</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/ops/trait.FnOnce.html"><code>FnOnce::Output</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuIsMutableLambda</code></a></td>
  <td>Partial: test invocability through a const callable reference</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a>, <a href="https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html">CallableTraits</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/ops/trait.FnMut.html"><code>FnMut</code></a>, <a href="https://doc.rust-lang.org/std/ops/trait.Fn.html"><code>Fn</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuIsVoidRetLambda</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/types/is_void"><code>std::is_void</code></a><code>&lt;</code><a href="https://en.cppreference.com/cpp/types/result_of"><code>std::invoke_result_t</code></a><code>&lt;...&gt;&gt;</code></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html">Boost.TypeTraits</a>, <a href="https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html">CallableTraits</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h"><code>folly::invoke_result_t</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>bsl::invoke_result</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>is_void</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/ops/trait.FnOnce.html"><code>FnOnce</code></a> bound with <code>Output = ()</code></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuIsStatelessLambda</code></a><br>➕ tests function-pointer conversion for the selected signature</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_convertible"><code>std::is_convertible</code></a>, <a href="https://en.cppreference.com/cpp/types/is_empty"><code>std::is_empty</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp"><code>boost::is_convertible</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/function-pointer.html">coercion of noncapturing closures to fn pointers</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuMutableLambda</code></a></td>
  <td>Constraint on const-callability test</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/ops/trait.FnMut.html"><code>FnMut</code></a>, <a href="https://doc.rust-lang.org/std/ops/trait.Fn.html"><code>Fn</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuNotMutableLambda</code></a></td>
  <td>Negated const-callability constraint</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/ops/trait.FnMut.html"><code>FnMut</code></a>, <a href="https://doc.rust-lang.org/std/ops/trait.Fn.html"><code>Fn</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuVoidRetLambda</code></a></td>
  <td>Constraint on void invocation result</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/ops/trait.FnOnce.html"><code>FnOnce</code></a> bound with <code>Output = ()</code></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuNotVoidRetLambda</code></a></td>
  <td>Constraint on non-void invocation result</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/std/ops/trait.FnOnce.html"><code>FnOnce::Output</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuStatelessLambda</code></a></td>
  <td>Constraint on function-pointer convertibility</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.EnableIf</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/function-pointer.html">coercion of noncapturing closures to fn pointers</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh"><code>ZuNotStatelessLambda</code></a></td>
  <td>Negated function-pointer-conversion constraint</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp">Boost.DisableIf</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/function-pointer.html">coercion of noncapturing closures to fn pointers</a></td>
</tr>
</tbody>
</table>

## Compile-time switch and unroll

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSwitch.hh"><code>ZuSwitch</code></a><br>➕ generates dispatch cases from a count or sequence, passing a compile-time index<br>➕ supports result collection and a caller-supplied default value</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_with_index</code></a></td>
  <td><a href="https://doc.rust-lang.org/reference/expressions/match-expr.html"><code>match</code></a>; <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a> can generate a complete dispatch expression, without a variadic const-index callback API</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnroll.hh"><code>ZuUnroll</code></a><br>➕ generates ordered calls from a count, sequence or type list<br>➕ shared API supports result collection and seeded reduction</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_for_each</code></a>, <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/for_each.hpp"><code>boost::hana::for_each</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/array/fn.from_fn.html"><code>array::from_fn</code></a>, <a href="https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.fold"><code>Iterator::fold</code></a>, <a href="https://docs.rs/seq-macro/latest/seq_macro/"><code>seq-macro</code></a></td>
</tr>
</tbody>
</table>

## Constant sequences and dispatch

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuSeq</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/integer_sequence"><code>std::integer_sequence</code></a><code>&lt;unsigned, ...&gt;</code></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_list_c</code></a><code>&lt;unsigned, ...&gt;</code></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::vtag_t&lt;I...&gt;</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integersequence.h"><code>bslmf::IntegerSequence</code></a><code>&lt;unsigned, ...&gt;</code></td>
  <td><a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a> and arrays; <a href="https://docs.rs/typenum/latest/typenum/"><code>typenum</code></a> for type-level numeric representations</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuMkSeq</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/integer_sequence"><code>std::make_integer_sequence</code></a><code>&lt;unsigned, N&gt;</code></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_iota_c</code></a> with value-type adapter</td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_makeintegersequence.h"><code>bslmf::MakeIntegerSequence</code></a><code>&lt;unsigned, N&gt;</code></td>
  <td><a href="https://doc.rust-lang.org/std/array/fn.from_fn.html"><code>array::from_fn</code></a>; <a href="https://docs.rs/seq-macro/latest/seq_macro/"><code>seq-macro</code></a> for generated index repetitions</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuMin</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/algorithm/min"><code>std::min</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_min</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/ConstexprMath.h"><code>folly::constexpr_min</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh">ZuSeq</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>bsl::min</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/cmp/fn.min.html"><code>cmp::min</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuMax</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/algorithm/max"><code>std::max</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_max</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/ConstexprMath.h"><code>folly::constexpr_max</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh">ZuSeq</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>bsl::max</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/cmp/fn.max.html"><code>cmp::max</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuSeqCall</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/apply"><code>std::apply</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/unpack.hpp"><code>boost::hana::unpack</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/functional/ApplyTuple.h"><code>folly::apply</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h"><code>bsl::apply</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/array/fn.from_fn.html"><code>array::from_fn</code></a>, <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a></td>
</tr>
</tbody>
</table>

## Constant-sequence conversions

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L61"><code>ZuSeqBitmap</code></a><br>➕ extracts sequence indices and combines their bits into a constexpr uint64_t mask</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html">Boost.Mp11</a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/const_eval.html">const evaluation</a>, <a href="https://docs.rs/typenum/latest/typenum/"><code>typenum</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuBitmapSeq</code></a><br>➕ generates an index-sequence type from the set bits of a constant mask</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html">Boost.Mp11</a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/const_eval.html">const evaluation</a>, <a href="https://docs.rs/typenum/latest/typenum/"><code>typenum</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuSeqTL</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_from_sequence</code></a></td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a>, <a href="https://docs.rs/typenum/latest/typenum/"><code>typenum</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuTLSeq</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html">Boost.Mp11</a></td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a>, <a href="https://docs.rs/typenum/latest/typenum/"><code>typenum</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L107"><code>ZuSeqString</code></a><br>➕ extracts sequence values and builds a structural <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh">ZuString</a> with a trailing terminator</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/array"><code>std::array</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/string.hpp"><code>boost::hana::string</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/constant-items.html">constant strings/arrays</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuStringSeq</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/unpack.hpp"><code>boost::hana::unpack</code></a>, <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/constant-items.html">constant strings/arrays</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L128"><code>ZuSeqArray</code></a><br>➕ extracts sequence values into a structural array of the selected element type</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/array"><code>std::array</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/unpack.hpp"><code>boost::hana::unpack</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/array/fn.from_fn.html"><code>array::from_fn</code></a>, <a href="https://doc.rust-lang.org/reference/expressions/array-expr.html">array literals</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh"><code>ZuArraySeq</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Boost.Hana</a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/generics.html#const-generics">const generics</a>, <a href="https://docs.rs/typenum/latest/typenum/"><code>typenum</code></a></td>
</tr>
</tbody>
</table>

## Type-list construction, access and slicing

Sources: [ZuTL.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [ZuSeq.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh),
[ZuString.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh).
Boost counterparts: [Mp11 reference](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html).
Composed operations below require a small adapter where the list/value representation differs.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuTypeList</code></a><br>➕ type-only list with nested operations</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/tuple"><code>std::tuple</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_list</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::tag_t&lt;T...&gt;</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh">ZuTypeList</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_typelist.h"><code>bslmf::TypeList</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_types.h"><code>types</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h"><code>bsl::tuple</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/tuple.html">tuples</a>, <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeRepeat</code></a><br>➕ generates an N-element type list from one type, including an empty result</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_repeat_c</code></a> on a singleton list</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/array.html"><code>[T; N]</code></a>, <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuType</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/tuple/tuple_element"><code>std::tuple_element_t</code></a> on a tuple</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_at_c</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::type_pack_element_t / type_list_element_t</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h"><code>bsl::tuple_element_t</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/expressions/tuple-expr.html#tuple-indexing-expressions">tuple indexing</a>, <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeIndex</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_find</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeIn</code></a><br>➕ membership test accepts a packed list or raw type arguments and handles empty input</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_same"><code>std::is_same</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_contains</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::is_one_of_v</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeHead</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_take_c</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a> (custom prefix selection)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeTail</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_drop_c</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a> (custom suffix selection)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeSlice</code></a><br>➕ stride/offset selection</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_transform</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeApply</code></a><br>➕ applies an existing packed type list or raw type arguments to a target template</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_rename</code></a> / <a href="https://github.com/boostorg/mp11/blob/master/include/boost/mp11/list.hpp"><code>mp_apply</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a>, <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-types">associated types</a></td>
</tr>
</tbody>
</table>

## Type-list algorithms

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeRev</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_reverse</code></a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a> reversal (value-level heterogeneous list)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeMap</code></a><br>➕ transforms a packed type list or raw type arguments and rebuilds the resulting list</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_transform</code></a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a> heterogeneous mapping; result types inferred through traits</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeGrep</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_copy_if</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeReduce</code></a><br>➕ recursive pairwise type reduction with empty/singleton cases and packed-list input</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_fold</code></a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a> folds; value-level heterogeneous reduction</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeMerge</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_append</code></a>, <a href="https://github.com/boostorg/mp11/blob/master/include/boost/mp11/algorithm.hpp"><code>mp_sort</code></a></td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a>, <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>HList</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeSort</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_sort</code></a> with comparator adapter</td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a>, <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>HList</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeConcat</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/tuple/tuple_cat"><code>std::tuple_cat</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_append</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Traits.h"><code>folly::type_list_concat_t</code></a></td>
  <td><a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>frunk::hlist</code></a> concatenation</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTypeUnique</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_unique</code></a></td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/reference/macros-by-example.html"><code>macro_rules!</code></a>, <a href="https://docs.rs/frunk/latest/frunk/hlist/index.html"><code>HList</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTLConverts</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_convertible"><code>std::is_convertible</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_transform</code></a>, <a href="https://github.com/boostorg/mp11/blob/master/include/boost/mp11/algorithm.hpp"><code>mp_all</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/frunk/latest/frunk/labelled/index.html"><code>frunk::labelled</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh"><code>ZuTLConstructs</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/is_constructible"><code>std::is_constructible</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html"><code>boost::mp11::mp_transform</code></a>, <a href="https://github.com/boostorg/mp11/blob/master/include/boost/mp11/algorithm.hpp"><code>mp_all</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a>, <a href="https://doc.rust-lang.org/std/convert/trait.From.html"><code>From</code></a></td>
</tr>
</tbody>
</table>

## Compile-time strings

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh"><code>ZuString</code></a><br>➕ structural string for non-type template arguments</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/array"><code>std::array</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/string.hpp"><code>boost::hana::string</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/FixedString.h"><code>folly::FixedString</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/constant-items.html"><code>const &amp;str</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh"><code>ZuStringT</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/integral_constant"><code>std::integral_constant</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/string.hpp"><code>boost::hana::string</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/FixedString.h"><code>folly::FixedString</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh">ZuStringT</a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/items/associated-items.html#associated-constants">associated string constants</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh"><code>ZuStringTL</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html">Boost.Mp11</a>, <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/procedural-macros.html">procedural macros</a></td>
</tr>
</tbody>
</table>

## Ownership and scope cleanup

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuObject.hh"><code>ZuObject</code></a><br>➕ embedded non-atomic count<br>➖ participating base class required</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/enable_shared_from_this"><code>std::enable_shared_from_this</code></a></td>
  <td><a href="https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp"><code>boost::intrusive_ref_counter</code></a><code>&lt;T, </code><a href="https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp"><code>boost::thread_unsafe_counter</code></a><code>&gt;</code></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/rc/struct.Rc.html"><code>Rc</code></a> (non-intrusive)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuObjectTraits.hh"><code>ZuObjectTraits</code></a><br>➕ detects Z intrusive-object participation</td>
  <td>N/A</td>
  <td>Partial: detection of intrusive pointer hooks</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/reference/trait-bounds.html">trait bounds</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuRef.hh"><code>ZuRef</code></a><br>➕ intrusive ownership without separate control block<br>➖ non-atomic; no weak observers</td>
  <td><a href="https://en.cppreference.com/cpp/memory/shared_ptr"><code>std::shared_ptr</code></a><br>➕ arbitrary pointees and weak observers</td>
  <td><a href="https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ptr.hpp"><code>boost::intrusive_ptr</code></a> with non-atomic hooks</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/rc/struct.Rc.html"><code>Rc</code></a> (non-intrusive)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPolymorph.hh"><code>ZuPolymorph</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/shared_ptr"><code>std::shared_ptr</code></a></td>
  <td>Partial: polymorphic base with Boost intrusive hooks</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/trait-object.html"><code>dyn Trait</code></a>, <a href="https://doc.rust-lang.org/std/boxed/struct.Box.html"><code>Box</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPtr.hh"><code>ZuPtr</code></a><br>➖ no configurable deleter</td>
  <td><a href="https://en.cppreference.com/cpp/memory/unique_ptr"><code>std::unique_ptr</code></a><br>➕ custom deleters and array specialization</td>
  <td><a href="https://github.com/boostorg/move/blob/master/include/boost/move/unique_ptr.hpp"><code>boost::movelib::unique_ptr</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Memory.h"><code>folly::static_function_deleter</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_managedptr.h"><code>bslma::ManagedPtr</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/boxed/struct.Box.html"><code>Box&lt;T&gt;</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuGuard</code></a><br>➕ cancellable lambda cleanup</td>
  <td>None: <a href="https://en.cppreference.com/cpp/experimental/scope_exit"><code>std::experimental::scope_exit</code></a></td>
  <td><a href="https://github.com/boostorg/scope/blob/master/include/boost/scope/scope_exit.hpp"><code>boost::scope::scope_exit</code></a><br>➕ related success/failure guards</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/ScopeGuard.h"><code>folly::ScopeGuard / makeGuard</code></a><br>➕ also supplies success/failure scope guards</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_scopeexit.h"><code>bdlb::ScopeExit</code></a><code> / </code><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_scopeexit.h"><code>ScopeExitUtil</code></a></td>
  <td><a href="https://docs.rs/scopeguard/latest/scopeguard/"><code>scopeguard</code></a>; dismissible scope cleanup</td>
</tr>
</tbody>
</table>

## Comparison, hashing and null sentinels

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh"><code>ZuCmp</code></a><br>➕ comparison, null sentinel and numeric bounds in one customization</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/compare/compare_three_way"><code>std::compare_three_way</code></a>, <a href="https://en.cppreference.com/cpp/types/numeric_limits"><code>std::numeric_limits</code></a>, <a href="https://en.cppreference.com/cpp/utility/optional"><code>std::optional</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/utility/operators.htm">Boost.Operators</a>, <a href="https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp">Optional</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/cmp/trait.Ord.html"><code>Ord</code></a>, <a href="https://doc.rust-lang.org/std/option/enum.Option.html"><code>Option</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh"><code>ZuCmp0</code></a><br>➕ zero-sentinel comparison policy</td>
  <td>Partial: comparison functions + zero test</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/utility/operators.htm">Boost.Operators</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/option/enum.Option.html"><code>Option</code></a>, <a href="https://doc.rust-lang.org/std/num/index.html"><code>NonZero integer types</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh"><code>ZuCmp_1</code></a><br>➕ negative-value null policy</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/option/enum.Option.html"><code>Option</code></a>, <a href="https://doc.rust-lang.org/std/num/index.html"><code>NonZero integer types</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh"><code>ZuCmpN</code></a><br>➕ compile-time chosen null sentinel</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/option/enum.Option.html"><code>Option</code></a>, <a href="https://doc.rust-lang.org/std/num/index.html"><code>NonZero integer types</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSemVer.hh"><code>ZuCompare</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/compare/compare_three_way"><code>std::compare_three_way</code></a></td>
  <td>Partial: Boost comparison utilities with integer-result adapter</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_compare.h"><code>bsl::compare_three_way</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/cmp/trait.Ord.html#tymethod.cmp"><code>Ord::cmp</code></a> / <a href="https://doc.rust-lang.org/std/cmp/trait.PartialOrd.html#tymethod.partial_cmp"><code>PartialOrd::partial_cmp</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnion.hh"><code>ZuEquals</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/functional/equal_to"><code>std::equal_to</code></a></td>
  <td><a href="https://github.com/boostorg/functional/blob/master/include/boost/functional.hpp"><code>boost::equal_to</code></a> (Functional)</td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h"><code>bsl::equal_to</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/cmp/trait.PartialEq.html"><code>PartialEq</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHash.hh"><code>ZuNull</code></a><br>➕ tests a type's configured sentinel</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/optional/operator_bool"><code>std::optional::has_value</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp"><code>boost::optional</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/option/enum.Option.html#method.is_none"><code>Option::is_none</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh#L747"><code>ZuNullRef</code></a><br>➕ reusable const-reference sentinel accessor honoring the type-specific comparator</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/option/enum.Option.html"><code>Option&lt;&amp;T&gt;</code></a>, <a href="https://doc.rust-lang.org/std/default/trait.Default.html"><code>Default</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuICmp.hh"><code>ZuICmp</code></a></td>
  <td>Partial: character folding + comparison algorithms</td>
  <td><a href="https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/predicate.hpp"><code>boost::algorithm::iequals</code></a> / <a href="https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/compare.hpp"><code>ilexicographical_compare</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Range.h"><code>folly::AsciiCaseInsensitive</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh">ZuCmp</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_string.h"><code>bdlb::String::areEqualCaseless</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/primitive.str.html#method.eq_ignore_ascii_case"><code>str::eq_ignore_ascii_case</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHash.hh"><code>ZuHash</code></a><br>➕ consistent text hashing across supported representations</td>
  <td><a href="https://en.cppreference.com/cpp/utility/hash"><code>std::hash</code></a></td>
  <td><a href="https://github.com/boostorg/container_hash/blob/master/include/boost/container_hash/hash.hpp"><code>boost::hash</code></a> (ContainerHash)</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/hash/Hash.h"><code>folly::hasher / folly::hash</code></a><br>➕ multiple hash families and combination helpers</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslh/bslh_hash.h"><code>bslh::Hash</code></a><code> / hashAppend</code>; <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_transparentstringhash.h"><code>bdlb::TransparentStringHash</code></a><br>➕ extensible hashing and heterogeneous string lookup</td>
  <td><a href="https://doc.rust-lang.org/std/hash/trait.Hash.html"><code>Hash</code></a> / <a href="https://doc.rust-lang.org/std/hash/trait.Hasher.html"><code>Hasher</code></a></td>
</tr>
</tbody>
</table>

## Bitmaps, bitfields and bit streams

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitmap.hh"><code>ZuBitmap</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/bitset"><code>std::bitset</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp"><code>boost::dynamic_bitset</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_bitset.h"><code>bsl::bitset</code></a><code>&lt;N&gt;</code></td>
  <td><a href="https://docs.rs/bitvec/latest/bitvec/"><code>bitvec</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitfield.hh"><code>ZuBitfield</code></a><br>➕ runtime-indexed packed fields with explicit word/mask operations and assignment proxies</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html">Boost.Endian</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/bitvec/latest/bitvec/field/trait.BitField.html"><code>bitvec::field::BitField</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitStream.hh"><code>ZuBitStream</code></a><br>➕ successive non-byte-aligned symbols</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp">Boost.DynamicBitset</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/bitstream-io/latest/bitstream_io/"><code>bitstream-io</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh"><code>Zu_clz*</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh"><code>ZuIntrin</code></a><code>.hh</code>)</td>
  <td><a href="https://en.cppreference.com/cpp/numeric/countl_zero"><code>std::countl_zero</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp">Boost.Core</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/Bits.h"><code>folly::findLastSet</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bitutil.h"><code>bdlb::BitUtil::numLeadingUnsetBits</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.leading_zeros"><code>integer::leading_zeros</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh"><code>Zu_ctz*</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh"><code>ZuIntrin</code></a><code>.hh</code>)</td>
  <td><a href="https://en.cppreference.com/cpp/numeric/countr_zero"><code>std::countr_zero</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp">Boost.Core</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/Bits.h"><code>folly::findFirstSet</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bitutil.h"><code>bdlb::BitUtil::numTrailingUnsetBits</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.trailing_zeros"><code>integer::trailing_zeros</code></a></td>
</tr>
</tbody>
</table>

## Boxed scalars and formatting adapters

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh"><code>ZuBox</code></a><br>➕ scalar null policy with integrated scanning/formatting</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/optional"><code>std::optional</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp">Boost.Optional</a>, <a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Charconv</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Optional.h"><code>folly::Optional</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h"><code>bdlb::NullableValue</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/option/enum.Option.html"><code>Option&lt;T&gt;</code></a>, <a href="https://doc.rust-lang.org/std/fmt/trait.Display.html"><code>Display</code></a>, <a href="https://doc.rust-lang.org/std/str/trait.FromStr.html"><code>FromStr</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh#L730"><code>ZuBoxed</code></a><br>➕ scalar-reference formatting adapter</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/format/format"><code>std::format</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/format/doc/format.html">Boost.Format</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h"><code>bdlb::NullableValue</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/fmt/struct.Arguments.html"><code>fmt::Arguments</code></a>, <a href="https://doc.rust-lang.org/std/fmt/trait.Display.html"><code>Display</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh"><code>ZuNBox</code></a><br>➕ boxed scalar with null-string formatting policy</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/optional"><code>std::optional</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp">Boost.Optional</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Optional.h"><code>folly::Optional</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h"><code>bdlb::NullableValue</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/option/enum.Option.html"><code>Option&lt;T&gt;</code></a>, <a href="https://doc.rust-lang.org/std/fmt/trait.Display.html"><code>Display</code></a>, <a href="https://doc.rust-lang.org/std/str/trait.FromStr.html"><code>FromStr</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh#L757"><code>ZuNBoxed</code></a></td>
  <td>Partial: scalar-reference adapter with custom null formatting</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/format/doc/format.html">Boost.Format</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h"><code>bdlb::NullableValue</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/fmt/struct.Arguments.html"><code>fmt::Arguments</code></a>, <a href="https://doc.rust-lang.org/std/fmt/trait.Display.html"><code>Display</code></a></td>
</tr>
</tbody>
</table>

## Arrays, spans and string interoperability

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zu/src/), [module guide](../zu/README.md).
Relevant counterparts: [standard containers](https://eel.is/c++draft/containers),
[Boost.SmartPtr](https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html),
[Boost.Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh"><code>ZuSpan</code></a><br>➕ integrated compare/hash, slicing and text searching</td>
  <td><a href="https://en.cppreference.com/cpp/container/span"><code>std::span</code></a><br>➕ standard ranges interoperability and static extent</td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/span.hpp"><code>boost::span</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Range.h"><code>folly::Range</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_span.h"><code>bsl::span</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.slice.html"><code>&amp;[T] / &amp;mut [T]</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh"><code>ZuCSpan</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/string/basic_string_view"><code>std::string_view</code></a></td>
  <td><a href="https://github.com/boostorg/utility/blob/master/include/boost/utility/string_view.hpp"><code>boost::string_view</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Range.h"><code>folly::StringPiece</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string_view.h"><code>bsl::string_view</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.str.html"><code>&amp;str</code></a> (validated UTF-8); <a href="https://doc.rust-lang.org/std/primitive.slice.html"><code>&amp;[u8]</code></a> for arbitrary bytes</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh"><code>ZuWSpan</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/string/basic_string_view"><code>std::wstring_view</code></a></td>
  <td><a href="https://github.com/boostorg/utility/blob/master/include/boost/utility/string_view.hpp"><code>boost::wstring_view</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Range.h"><code>folly::Range&lt;const wchar_t*&gt;</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string_view.h"><code>bsl::wstring_view</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/primitive.slice.html"><code>&amp;[u16] / &amp;[u32]</code></a>, <a href="https://doc.rust-lang.org/std/ffi/struct.OsStr.html"><code>OsStr</code></a> (code units / OS strings)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh"><code>ZuBSpan</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/container/span"><code>std::span</code></a><code>&lt;const uint8_t&gt;</code> / <a href="https://en.cppreference.com/cpp/container/span"><code>std::span</code></a><code>&lt;const </code><a href="https://en.cppreference.com/cpp/types/byte"><code>std::byte</code></a><code>&gt;</code></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/span.hpp"><code>boost::span</code></a><code>&lt;const unsigned char&gt;</code></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Range.h"><code>folly::ByteRange</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_span.h"><code>bsl::span</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.slice.html"><code>&amp;[u8]</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh"><code>ZuArray</code></a><br>➕ inline capacity with logical length; structural representation</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/array"><code>std::array</code></a>, <a href="https://en.cppreference.com/cpp/container/inplace_vector"><code>std::inplace_vector</code></a></td>
  <td><a href="https://github.com/boostorg/container/blob/master/include/boost/container/static_vector.hpp"><code>boost::container::static_vector</code></a><br>➖ different structural/NTTP contract</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/small_vector.h"><code>folly::small_vector</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_array.h"><code>bsl::array</code></a></td>
  <td><a href="https://docs.rs/arrayvec/latest/arrayvec/struct.ArrayVec.html"><code>ArrayVec</code></a>; inline fixed capacity with logical length</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh"><code>ZuCArray</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/array"><code>std::array</code></a></td>
  <td><a href="https://github.com/boostorg/static_string/blob/master/include/boost/static_string.hpp"><code>boost::static_string</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/FixedString.h"><code>folly::FixedString</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_array.h"><code>bsl::array</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h"><code>bsl::basic_string</code></a></td>
  <td>Partial: <a href="https://docs.rs/arrayvec/latest/arrayvec/struct.ArrayString.html"><code>ArrayString</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh"><code>ZuWArray</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/array"><code>std::array</code></a></td>
  <td><a href="https://github.com/boostorg/static_string/blob/master/include/boost/static_string/static_string.hpp"><code>boost::static_strings::basic_static_string</code></a><code>&lt;N, wchar_t&gt;</code></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/FixedString.h"><code>folly::BasicFixedString&lt;wchar_t, N&gt;</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_array.h"><code>bsl::array</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h"><code>wchar_t</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h"><code>bsl::basic_string</code></a></td>
  <td>Partial: <a href="https://docs.rs/arrayvec/latest/arrayvec/struct.ArrayVec.html"><code>ArrayVec&lt;u16, N&gt;</code></a> (code units)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMArray.hh"><code>ZuMArray</code></a><br>➕ writable transformed-element proxies</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/ranges/transform_view"><code>std::views::transform</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/iterator/doc/index.html">Boost.Iterator</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_transformiterator.h"><code>bdlb::TransformIterator</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.map"><code>Iterator::map</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuVArray.hh"><code>ZuVArray</code></a><br>➕ type-erased array interface across compiled boundaries</td>
  <td>None: <a href="https://en.cppreference.com/cpp/container/span"><code>std::span</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/range/blob/master/include/boost/range.hpp">Boost.Range</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlar/bdlar_arrayref.h"><code>bdlar::ArrayRef</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/trait-object.html">trait objects</a> (no stable Rust ABI)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStdString.hh"><code>ZuStdString</code></a> integration</td>
  <td><a href="https://en.cppreference.com/cpp/string/basic_string"><code>std::basic_string</code></a> used directly</td>
  <td>Partial: Boost string/range interoperability</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h"><code>bsl::basic_string</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/string/struct.String.html"><code>String</code></a> / <a href="https://doc.rust-lang.org/std/borrow/enum.Cow.html"><code>Cow&lt;str&gt;</code></a> / <a href="https://doc.rust-lang.org/std/convert/trait.AsRef.html"><code>AsRef&lt;str&gt;</code></a></td>
</tr>
</tbody>
</table>

## Tuples, unions and empty values

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTuple.hh"><code>ZuTuple</code></a><br>➕ size-ordered flat storage and positional mutation<br>➖ incomplete <a href="https://en.cppreference.com/cpp/utility/tuple"><code>std::tuple</code></a> algorithm interoperability</td>
  <td><a href="https://en.cppreference.com/cpp/utility/tuple"><code>std::tuple</code></a>, <a href="https://en.cppreference.com/cpp/utility/pair"><code>std::pair</code></a></td>
  <td><a href="https://github.com/boostorg/tuple/blob/master/include/boost/tuple/tuple.hpp"><code>boost::tuple</code></a>, <a href="https://github.com/boostorg/hana/blob/master/include/boost/hana/tuple.hpp"><code>boost::hana::tuple</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h"><code>bsl::tuple</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/types/tuple.html">tuples</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnion.hh"><code>ZuUnion</code></a><br>➕ void alternatives and explicit nonthrowing dispatch<br>➖ callers must respect active-alternative access contract</td>
  <td><a href="https://en.cppreference.com/cpp/utility/variant"><code>std::variant</code></a><br>➕ checked <a href="https://en.cppreference.com/cpp/utility/variant/get"><code>get</code></a>/<a href="https://en.cppreference.com/cpp/utility/variant/get_if"><code>get_if</code></a> and standard visitation</td>
  <td><a href="https://github.com/boostorg/variant2/blob/master/include/boost/variant2/variant.hpp"><code>boost::variant2::variant</code></a><br>➕ never-valueless value semantics</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Expected.h"><code>folly::Expected&lt;T, E&gt;</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_variant.h"><code>bsl::variant</code></a><br>➕ allocator-aware alternatives and checked access</td>
  <td><a href="https://doc.rust-lang.org/reference/items/enumerations.html"><code>enum</code></a> + <a href="https://doc.rust-lang.org/reference/expressions/match-expr.html"><code>match</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh"><code>ZuVoid</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/variant/monostate"><code>std::monostate</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/variant/reference.html"><code>boost::blank</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_variant.h"><code>bsl::monostate</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlf/bdlf_noop.h"><code>bdlf::NoOp</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.unit.html"><code>()</code></a></td>
</tr>
</tbody>
</table>

## Array algorithms, iterators, sorting and searching

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArrayFn.hh"><code>ZuArrayFn</code></a><br>➕ trait-selected bulk initialization, movement and comparison</td>
  <td><a href="https://en.cppreference.com/cpp/header/algorithm"><code>&lt;algorithm&gt;</code></a>, uninitialized-memory algorithms</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/container.html">Boost.Container</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/move.html">Move</a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/primitive.slice.html"><code>slice methods</code></a> / <a href="https://doc.rust-lang.org/std/ptr/index.html"><code>ptr</code></a> bulk operations</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStringFn.hh"><code>ZuStringFn</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/string/char_traits"><code>std::char_traits</code></a>, C string functions</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/string_algo/reference.html">Boost.StringAlgo</a></td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/primitive.str.html"><code>str</code></a> / <a href="https://doc.rust-lang.org/std/primitive.slice.html"><code>[u8]</code></a> operations</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIter.hh"><code>ZuIter</code></a><br>➕ reusable random-access iterator boilerplate</td>
  <td>Partial: iterator concepts and custom iterator implementation</td>
  <td><a href="https://github.com/boostorg/iterator/blob/master/include/boost/iterator/iterator_facade.hpp"><code>boost::iterator_facade</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/iter/trait.Iterator.html"><code>Iterator</code></a>, <a href="https://doc.rust-lang.org/std/iter/trait.DoubleEndedIterator.html"><code>DoubleEndedIterator</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSort.hh#L139"><code>ZuSort</code></a><br>➕ three-way comparison and Z bulk-element operations<br>➖ custom quicksort lacks <a href="https://en.cppreference.com/cpp/algorithm/sort"><code>std::sort</code></a>'s mandated worst-case bound</td>
  <td><a href="https://en.cppreference.com/cpp/algorithm/sort"><code>std::sort</code></a></td>
  <td><a href="https://github.com/boostorg/sort/blob/master/include/boost/sort/sort.hpp">Boost.Sort</a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>bsl::sort</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.slice.html#method.sort_unstable_by"><code>slice::sort_unstable_by</code></a> / <a href="https://doc.rust-lang.org/std/primitive.slice.html#method.sort_by"><code>sort_by</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L23"><code>ZuSearch</code></a><br>➕ packs found flag and insertion position into one result</td>
  <td><a href="https://en.cppreference.com/cpp/algorithm/lower_bound"><code>std::lower_bound</code></a> plus equality test</td>
  <td><a href="https://github.com/boostorg/range/blob/master/include/boost/range.hpp">Boost.Range</a> <a href="https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp"><code>lower_bound</code></a> plus equality test</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>bsl::lower_bound</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>binary_search</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.slice.html#method.binary_search"><code>slice::binary_search</code></a> returns <code>Result&lt;found, insertion&gt;</code></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L12"><code>ZuSearchFound</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/algorithm/lower_bound"><code>std::lower_bound</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/range/blob/master/include/boost/range.hpp">Boost.Range</a>, <a href="https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp"><code>lower_bound</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>bsl::lower_bound</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>binary_search</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/result/enum.Result.html#method.is_ok"><code>Result::is_ok</code></a> on the binary-search result</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L13"><code>ZuSearchPos</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/algorithm/lower_bound"><code>std::lower_bound</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/range/blob/master/include/boost/range.hpp">Boost.Range</a>, <a href="https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp"><code>lower_bound</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>bsl::lower_bound</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h"><code>binary_search</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/result/enum.Result.html"><code>Result&lt;usize, usize&gt;</code></a>; extract either index with <code>match</code></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L66"><code>ZuInterSearch</code></a><br>➕ interpolation search for suitably distributed keys<br>➖ behavior depends on key distribution</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/algorithm/lower_bound"><code>std::lower_bound</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/range/blob/master/include/boost/range.hpp">Boost.Range</a>, <a href="https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp"><code>lower_bound</code></a></td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/std/primitive.slice.html#method.binary_search_by"><code>slice::binary_search_by</code></a></td>
</tr>
</tbody>
</table>

## Text tokenization

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTokenizer.hh"><code>ZuTokenizer::Delimited</code></a><br>➕ borrowed tokens without allocation</td>
  <td><a href="https://en.cppreference.com/cpp/ranges/split_view"><code>std::views::split</code></a></td>
  <td><a href="https://github.com/boostorg/tokenizer/blob/master/include/boost/tokenizer.hpp"><code>boost::tokenizer</code></a>, <a href="https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/find_iterator.hpp"><code>boost::split_iterator</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/String.h"><code>folly::splitStep / splitTo</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_tokenizer.h"><code>bdlb::Tokenizer</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.str.html#method.split"><code>str::split</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTokenizer.hh"><code>ZuTokenizer::WhiteSpace</code></a><br>➕ simple ASCII whitespace scanning</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/ranges/split_view"><code>std::views::split</code></a></td>
  <td><a href="https://github.com/boostorg/tokenizer/blob/master/include/boost/tokenizer.hpp"><code>boost::tokenizer</code></a> with character separator</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/String.h"><code>folly::splitTo</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_tokenizer.h"><code>bdlb::Tokenizer</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.str.html#method.split_ascii_whitespace"><code>str::split_ascii_whitespace</code></a></td>
</tr>
</tbody>
</table>

## Numeric input

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh"><code>Zu_nscan</code></a> / numeric input (<a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh"><code>Zu_aton</code></a><code>.hh</code>)<br>➕ numeric scanning with Z-format policies</td>
  <td><a href="https://en.cppreference.com/cpp/utility/from_chars"><code>std::from_chars</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp"><code>from_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::to / tryTo</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h"><code>bdlb::NumericParseUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/str/trait.FromStr.html"><code>FromStr</code></a> / <a href="https://doc.rust-lang.org/std/primitive.str.html#method.parse"><code>str::parse</code></a>; <a href="https://docs.rs/lexical-core/latest/lexical_core/"><code>lexical-core</code></a> for caller-selected numeric parsing policies</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L332"><code>Zu_atou</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/from_chars"><code>std::from_chars</code></a> for unsigned integers</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp"><code>from_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::tryTo&lt;unsigned&gt;</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h"><code>bdlb::NumericParseUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.u64.html#method.from_str_radix"><code>u64::from_str_radix</code></a> / <a href="https://doc.rust-lang.org/std/primitive.str.html#method.parse"><code>str::parse</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L336"><code>Zu_atoi</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/from_chars"><code>std::from_chars</code></a> for signed integers</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp"><code>from_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::tryTo&lt;signed integer&gt;</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h"><code>bdlb::NumericParseUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.i64.html#method.from_str_radix"><code>i64::from_str_radix</code></a> / <a href="https://doc.rust-lang.org/std/primitive.str.html#method.parse"><code>str::parse</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L340"><code>Zu_atof</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/from_chars"><code>std::from_chars</code></a> for floating point</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp"><code>from_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::tryTo&lt;floating point&gt;</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h"><code>bdlb::NumericParseUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.str.html#method.parse"><code>str::parse::&lt;f64&gt;</code></a>; <a href="https://docs.rs/lexical-core/latest/lexical_core/"><code>lexical-core</code></a> for prefix parsing/configuration</td>
</tr>
</tbody>
</table>

## Numeric output and buffer sizing

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh"><code>Zu_nprint</code></a> / numeric output (<a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh"><code>Zu_ntoa</code></a><code>.hh</code>)<br>➕ compile-time numeric-format policies</td>
  <td><a href="https://en.cppreference.com/cpp/utility/to_chars"><code>std::to_chars</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp"><code>to_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::toAppend</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h"><code>bsl::to_chars</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.write.html"><code>write!</code></a> / <a href="https://doc.rust-lang.org/std/fmt/index.html"><code>fmt</code></a>; <a href="https://docs.rs/lexical-core/latest/lexical_core/"><code>lexical-core</code></a> for numeric output to byte buffers</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L915"><code>Zu_utoa</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/to_chars"><code>std::to_chars</code></a> for unsigned integers</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp"><code>to_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::toAppend</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h"><code>bsl::to_chars</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.write.html"><code>write!</code></a>; <a href="https://docs.rs/itoa/latest/itoa/struct.Buffer.html"><code>itoa::Buffer</code></a> for integer formatting</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L922"><code>Zu_itoa</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/to_chars"><code>std::to_chars</code></a> for signed integers</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp"><code>to_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::toAppend</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h"><code>bsl::to_chars</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.write.html"><code>write!</code></a>; <a href="https://docs.rs/itoa/latest/itoa/struct.Buffer.html"><code>itoa::Buffer</code></a> for integer formatting</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L929"><code>Zu_ftoa</code></a><br>➖ no automatic scientific-notation fallback</td>
  <td><a href="https://en.cppreference.com/cpp/utility/to_chars"><code>std::to_chars</code></a> for floating point<br>➕ general, fixed, scientific and hexadecimal modes</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a> <a href="https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp"><code>to_chars</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::toAppend</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h"><code>bsl::to_chars</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.write.html"><code>write!</code></a>; <a href="https://docs.rs/ryu/latest/ryu/struct.Buffer.html"><code>ryu::Buffer</code></a> for float formatting</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L911"><code>Zu_ulen</code></a><br>➕ constexpr unsigned-output buffer sizing</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/numeric_limits"><code>std::numeric_limits</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/integer/blob/master/include/boost/integer.hpp">Boost.Integer</a>, <a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Charconv</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h"><code>bsl::formatted_size</code></a></td>
  <td>Partial: <a href="https://docs.rs/lexical-core/latest/lexical_core/"><code>lexical-core</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L918"><code>Zu_ilen</code></a><br>➕ constexpr signed-output buffer sizing</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/types/numeric_limits"><code>std::numeric_limits</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/integer/blob/master/include/boost/integer.hpp">Boost.Integer</a>, <a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Charconv</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h"><code>bsl::formatted_size</code></a></td>
  <td>Partial: <a href="https://docs.rs/lexical-core/latest/lexical_core/"><code>lexical-core</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L925"><code>Zu_flen</code></a><br>➕ format-aware floating-output buffer sizing</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html">Boost.Charconv</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h"><code>bsl::formatted_size</code></a></td>
  <td>Partial: <a href="https://docs.rs/lexical-core/latest/lexical_core/"><code>lexical-core</code></a></td>
</tr>
</tbody>
</table>

## Formatting, streams and joining

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFmt.hh"><code>ZuFmt</code></a><br>➕ formatting policies encoded in types</td>
  <td><a href="https://en.cppreference.com/cpp/utility/format/format"><code>std::format</code></a>, stream manipulators</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/format/doc/format.html">Boost.Format</a> / <a href="https://github.com/boostorg/convert/blob/master/include/boost/convert.hpp">Convert</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h"><code>bsl::format</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/fmt/index.html"><code>std::fmt</code></a> formatting traits and format specifiers</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPrint.hh"><code>ZuPrint</code></a><br>➕ ADL print customization shared by Z streams</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/format/formatter"><code>std::formatter</code></a>, <a href="https://en.cppreference.com/cpp/io/basic_ostream/operator_ltlt"><code>operator&lt;&lt;</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/format/doc/format.html">Boost.Format</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::toAppend customization</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_printer.h"><code>bslim::Printer</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/fmt/trait.Display.html"><code>Display</code></a> / <a href="https://doc.rust-lang.org/std/fmt/trait.Debug.html"><code>Debug</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStream.hh"><code>ZuStream</code></a><br>➕ direct output into caller-supplied bounded text storage</td>
  <td><a href="https://en.cppreference.com/cpp/io/basic_ospanstream"><code>std::ospanstream</code></a> (C++23)</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html">Boost.Iostreams</a> array sink</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/Cursor.h"><code>folly::io::Appender</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlsb/bdlsb_fixedmemoutput.h"><code>bdlsb::FixedMemOutput</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/fmt/trait.Write.html"><code>fmt::Write</code></a>, <a href="https://doc.rust-lang.org/std/io/trait.Write.html"><code>io::Write</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuVStream.hh"><code>ZuVStream</code></a><br>➕ type-erased stream interface with function pointers</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/io/basic_ostream"><code>std::ostream</code></a>, <a href="https://en.cppreference.com/cpp/io/basic_streambuf"><code>std::streambuf</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html">Boost.Iostreams</a>, <a href="https://github.com/boostorg/iostreams/blob/master/include/boost/iostreams/stream.hpp"><code>stream</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_ostream.h"><code>bsl::ostream</code></a></td>
  <td><a href="https://doc.rust-lang.org/reference/types/trait-object.html"><code>dyn</code></a> <a href="https://doc.rust-lang.org/std/fmt/trait.Write.html"><code>fmt::Write</code></a> / <a href="https://doc.rust-lang.org/std/io/trait.Write.html"><code>io::Write</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuJoin.hh"><code>ZuJoin</code></a><br>➕ streams elements directly with a delimiter</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/ranges/join_with_view"><code>std::views::join_with</code></a></td>
  <td><a href="https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/join.hpp"><code>boost::algorithm::join</code></a><br>➖ normally builds a result string</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/String.h"><code>folly::join</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/primitive.slice.html#method.join"><code>slice::join</code></a>, <a href="https://docs.rs/itertools/latest/itertools/trait.Itertools.html#method.format"><code>Itertools::format</code></a></td>
</tr>
</tbody>
</table>

## Time, identifiers and versions

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTime.hh"><code>ZuTime</code></a><br>➕ compact seconds/nanoseconds with null sentinel<br>➖ same type represents instants and intervals</td>
  <td><a href="https://en.cppreference.com/cpp/chrono/duration"><code>std::chrono::duration</code></a>, <a href="https://en.cppreference.com/cpp/chrono/time_point"><code>std::chrono::time_point</code></a><br>➕ distinct clock and duration types</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/chrono.html">Boost.Chrono</a> / <a href="https://www.boost.org/doc/libs/latest/doc/html/date_time.html">DateTime</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/chrono/Conv.h"><code>folly::to&lt;chrono duration&gt; / timespec conversions</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_timeinterval.h"><code>bsls::TimeInterval</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h"><code>bdlb::NullableValue</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/time/struct.Duration.html"><code>Duration</code></a> / <a href="https://doc.rust-lang.org/std/time/struct.SystemTime.html"><code>SystemTime</code></a>; distinct interval/instant types</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDateTime.hh"><code>ZuDateTime</code></a><br>➕ wide Julian-calendar date range</td>
  <td><a href="https://en.cppreference.com/cpp/chrono"><code>std::chrono</code></a> calendar types and formatting (C++20)</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/date_time.html">Boost.DateTime</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlt/bdlt_datetime.h"><code>bdlt::Datetime</code></a></td>
  <td>Partial: <a href="https://docs.rs/time/latest/time/struct.OffsetDateTime.html"><code>time::OffsetDateTime</code></a>, <a href="https://docs.rs/chrono/latest/chrono/struct.DateTime.html"><code>chrono::DateTime</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuID.hh"><code>ZuID</code></a><br>➕ by-value bounded name with <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh">ZuArray</a> length, comparison, hashing and printing<br>➖ fixed framework capacity</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/string/basic_string"><code>std::string</code></a></td>
  <td><a href="https://github.com/boostorg/static_string/blob/master/include/boost/static_string.hpp"><code>boost::static_string</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/FixedString.h"><code>folly::FixedString</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/arrayvec/latest/arrayvec/struct.ArrayString.html"><code>ArrayString</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSemVer.hh"><code>ZuSemVer</code></a><br>➕ packed compact version value<br>➖ minor ≤ 99, patch ≤ 999; no prerelease/build metadata</td>
  <td>N/A</td>
  <td>None: <a href="https://github.com/boostorg/config/blob/master/include/boost/version.hpp"><code>BOOST_VERSION</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/semver/latest/semver/struct.Version.html"><code>semver::Version</code></a></td>
</tr>
</tbody>
</table>

## Fixed-point and decimal arithmetic

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFixed.hh"><code>ZuFixed</code></a><br>➕ mantissa plus runtime decimal scale, null/reset sentinels and scan/format conversions<br>➖ constrained precision/range</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp">Boost.Multiprecision</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h"><code>bdldfp::Decimal64</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h"><code>Decimal128</code></a></td>
  <td>Partial: <a href="https://docs.rs/rust_decimal/latest/rust_decimal/struct.Decimal.html"><code>rust_decimal::Decimal</code></a> (different precision)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimal.hh"><code>ZuDecimal</code></a><br>➕ decimal arithmetic with 18 integer and 18 fractional digits<br>➖ fixed precision/range</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_dec_float.hpp"><code>boost::multiprecision::cpp_dec_float</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h"><code>bdldfp::Decimal128</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimal.hh">ZuDecimal</a></td>
  <td>Partial: <a href="https://docs.rs/rust_decimal/latest/rust_decimal/struct.Decimal.html"><code>rust_decimal::Decimal</code></a> (different precision)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimalFn.hh"><code>ZuDecimalFn</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp">Boost.Multiprecision</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimalutil.h"><code>bdldfp::DecimalUtil</code></a></td>
  <td><a href="https://docs.rs/rust_decimal/latest/rust_decimal/"><code>rust_decimal</code></a> arithmetic, rounding, parsing and formatting</td>
</tr>
</tbody>
</table>

## Unicode conversion and measurement

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh"><code>ZuUTF</code></a><br>➕ conversion into caller-owned storage</td>
  <td>Partial: locale/codecvt facilities<br>➖ legacy Unicode conversion APIs are deprecated in C++23</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html">Boost.Locale</a> / Nowide</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Unicode.h"><code>folly::utf8ToCodePoint / codePointToUtf8</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h"><code>bdlde::CharConvertUtf16</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.char.html"><code>char</code></a> encoding APIs; <a href="https://docs.rs/encoding_rs/latest/encoding_rs/"><code>encoding_rs</code></a> for buffer-oriented encoding conversion</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh"><code>ZuUTF8</code></a><br>➕ code-point decoding/encoding and input/output length operations for caller buffers</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/locale/codecvt_utf8"><code>std::codecvt_utf8</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html">Boost.Locale</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Unicode.h"><code>folly::utf8ToCodePoint / codePointToUtf8</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_utf8util.h"><code>bdlde::Utf8Util</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/str/fn.from_utf8.html"><code>str::from_utf8</code></a> / <a href="https://doc.rust-lang.org/std/primitive.char.html#method.encode_utf8"><code>char::encode_utf8</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh"><code>ZuUTF16</code></a><br>➕ surrogate-aware code-point decoding/encoding with caller-buffer length checks</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/locale/codecvt_utf16"><code>std::codecvt_utf16</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html">Boost.Locale</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Unicode.h"><code>folly::unicode_code_point_from_utf16_surrogate_pair</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h"><code>bdlde::CharConvertUtf16</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/char/fn.decode_utf16.html"><code>char::decode_utf16</code></a> / <a href="https://doc.rust-lang.org/std/primitive.char.html#method.encode_utf16"><code>char::encode_utf16</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh"><code>ZuUTF32</code></a><br>➕ bounded code-point input/output and Unicode display-width lookup</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html">Boost.Locale</a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf32.h"><code>bdlde::CharConvertUtf32</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/char/fn.from_u32.html"><code>char::from_u32</code></a> / <a href="https://docs.rs/unicode-width/latest/unicode_width/"><code>unicode-width</code></a> for display width</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh"><code>ZuUTFSpan</code></a><br>➕ packs input length, output length and display width</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html">Boost.Locale</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/primitive.str.html#method.len"><code>str::len</code></a>, <a href="https://doc.rust-lang.org/std/primitive.str.html#method.chars"><code>str::chars</code></a>, <a href="https://docs.rs/unicode-width/latest/unicode_width/"><code>unicode-width</code></a></td>
</tr>
</tbody>
</table>

## Binary-text and percent encoding

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHex.hh"><code>ZuHex</code></a><br>➕ caller-buffer output and in-place decoding</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/hex.hpp"><code>boost::algorithm::hex</code></a> / <a href="https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/hex.hpp"><code>unhex</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/String.h"><code>folly::hexlify / unhexlify</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_hexencoder.h"><code>bdlde::HexEncoder</code></a><code> / </code><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_hexdecoder.h"><code>HexDecoder</code></a><br>➕ incremental conversion state</td>
  <td>Partial: <a href="https://docs.rs/hex/latest/hex/"><code>hex</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase32.hh"><code>ZuBase32</code></a><br>➕ caller-buffer codec</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Boost.Archive</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/data-encoding/latest/data_encoding/"><code>data-encoding</code></a> (Base32 with slice-oriented codecs)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase64.hh"><code>ZuBase64</code></a><br>➕ caller-buffer codec</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Boost.Archive</a>, <a href="https://github.com/boostorg/serialization/blob/master/include/boost/archive/iterators/base64_from_binary.hpp"><code>base64_from_binary</code></a>, <a href="https://github.com/boostorg/serialization/blob/master/include/boost/archive/iterators/binary_from_base64.hpp"><code>binary_from_base64</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/base64.h"><code>folly::base64Encode / base64Decode</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoder.h"><code>bdlde::Base64Encoder</code></a><code> / </code><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64decoder.h"><code>Base64Decoder</code></a><br>➕ incremental codec with configurable options</td>
  <td><a href="https://docs.rs/base64/latest/base64/"><code>base64</code></a> (<code>Engine</code> slice-oriented encoding/decoding)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase64URL.hh"><code>ZuBase64URL</code></a><br>➕ URL-safe alphabet</td>
  <td>N/A</td>
  <td>Partial: Base64 iterator composition with alphabet adaptation</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/base64.h"><code>folly::base64URLEncode / base64URLDecode</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoderoptions.h"><code>bdlde::Base64EncoderOptions::urlSafe</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64decoderoptions.h"><code>Base64DecoderOptions</code></a></td>
  <td><a href="https://docs.rs/base64/latest/base64/engine/general_purpose/index.html"><code>base64 URL-safe engines</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPercent.hh"><code>ZuPercent</code></a><br>➕ policy-driven percent encoding, plus/space handling and in-place decoding</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/url/blob/master/include/boost/url.hpp">Boost.URL</a> encoding/decoding<br>➕ <a href="https://github.com/boostorg/url/blob/master/include/boost/url.hpp">URL</a> component and grammar vocabulary</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/String.h"><code>folly::uriEscape / uriUnescape</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/percent-encoding/latest/percent_encoding/"><code>percent-encoding</code></a>, <a href="https://docs.rs/form_urlencoded/latest/form_urlencoded/"><code>form_urlencoded</code></a></td>
</tr>
</tbody>
</table>

## Testing

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTest.hh#L255"><code>ZuTest</code></a><br>➕ TAP output and static nested test counts</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/test/blob/master/include/boost/test/unit_test.hpp">Boost.Test</a><br>➕ broader test framework and reporting options</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h"><code>bslim::TestUtil</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h"><code>BSLIM_TESTUTIL_*</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/attributes/testing.html"><code>#[test]</code> / test harness</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTestUtil.hh"><code>ZuTestUtil</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/test/blob/master/include/boost/test/unit_test.hpp">Boost.Test</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h"><code>bslim::TestUtil</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h"><code>BSLIM_TESTUTIL_*</code></a></td>
  <td><a href="https://doc.rust-lang.org/reference/attributes/testing.html">test harness</a>; <a href="https://docs.rs/proptest/latest/proptest/"><code>proptest</code></a> / <a href="https://docs.rs/rstest/latest/rstest/"><code>rstest</code></a> for additional test utilities</td>
</tr>
</tbody>
</table>

## Structure reflection and facets

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh#L460"><code>ZuStruct</code></a><br>➕ field/accessor metadata, facets and composite keys in C++23<br>➕ supports members, methods and lambda-derived fields</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a> / <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a>.Struct<br>➕ <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Describe</a> also describes bases and member functions</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_sequencefunctions.h"><code>bdlat_SequenceFunctions</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h"><code>bdlat</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntccfg/ntccfg_traits.h"><code>traits</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/procedural-macros.html">derive macros</a>, <a href="https://docs.rs/frunk/latest/frunk/labelled/index.html"><code>LabelledGeneric</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh#L768"><code>ZuStructFacet</code></a><br>➕ alternate reflected field views of one type</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_sequencefunctions.h"><code>bdlat_SequenceFunctions</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h"><code>bdlat</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntccfg/ntccfg_traits.h"><code>traits</code></a></td>
  <td>Partial: <a href="https://serde.rs/attributes.html"><code>Serde attributes</code></a></td>
</tr>
</tbody>
</table>

## ZuMatcher — compile-time multi-string matching

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMatcher.hh#L447"><code>ZuMatcher</code></a><br>➕ compile-time Aho–Corasick for fixed multiple keys<br>➖ large instantiations increase compile cost</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/algorithm/search"><code>std::search</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string.hpp">Boost.Algorithm</a>, <a href="https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp">Boost.Spirit</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h"><code>bsl::boyer_moore_horspool_searcher</code></a></td>
  <td>Partial: <a href="https://docs.rs/aho-corasick/latest/aho_corasick/"><code>aho-corasick</code></a> (runtime construction)</td>
</tr>
</tbody>
</table>

# Zm - Multithreading foundation

## Platform and cache alignment

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zm/src/), [module guide](../zm/README.md).
Standard counterparts: [concurrency library](https://eel.is/c++draft/thread).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPlatform.hh"><code>ZmPlatform</code></a> primitives<br>➕ OS IDs, aligned allocation and runtime platform helpers</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/thread/thread/id"><code>std::thread::id</code></a>, <a href="https://en.cppreference.com/cpp/memory/new/operator_new"><code>operator new</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/thread.html">Boost.Thread</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/align.html">Align</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_platform.h"><code>BSLS_PLATFORM_*</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_platform.h"><code>bsls::Platform</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/alloc/index.html"><code>std::alloc</code></a>, <a href="https://doc.rust-lang.org/std/process/fn.id.html"><code>process::id</code></a>, <a href="https://docs.rs/rustix/latest/rustix/"><code>rustix</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPlatform.hh"><code>ZmCacheLineSize</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPlatform.hh"><code>ZmCacheAlign</code></a><br>➕ shared target-specific cache-separation policy and matching alignment annotation</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size"><code>std::hardware_destructive_interference_size</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/align.html">Boost.Align</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/Align.h"><code>folly::hardware_destructive_interference_size</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_platform.h"><code>bslmt::Platform::e_CACHE_LINE_SIZE</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/reference/type-layout.html#the-alignment-modifiers"><code>#[repr(align(N))]</code></a>, <a href="https://docs.rs/crossbeam-utils/latest/crossbeam_utils/struct.CachePadded.html"><code>CachePadded</code></a></td>
</tr>
</tbody>
</table>

## Atomic operations and locks

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAtomic.hh"><code>ZmAtomic</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/atomic/atomic"><code>std::atomic</code></a>, atomic fences</td>
  <td><a href="https://github.com/boostorg/atomic/blob/master/include/boost/atomic.hpp">Boost.Atomic</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/synchronization/RelaxedAtomic.h"><code>folly::relaxed_atomic</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_atomic.h"><code>bsls::AtomicInt</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_atomic.h"><code>AtomicInt64</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_atomic.h"><code>AtomicPointer</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/sync/atomic/index.html"><code>std::sync::atomic</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNoLock.hh"><code>ZmNoLock</code></a><br>➕ compile-time no-op locking policy</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/interprocess.html">Boost.Interprocess</a>, <a href="https://github.com/boostorg/interprocess/blob/master/include/boost/interprocess/sync/null_mutex.hpp"><code>null_mutex</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/reference/types/pointer.html">exclusive <code>&amp;mut T</code> access</a>, <a href="https://docs.rs/lock_api/latest/lock_api/"><code>lock_api</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLockTraits.hh"><code>ZmLockTraits</code></a></td>
  <td>Partial: Lockable concepts/requirements</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/thread.html">Boost.Thread</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/lock_api/latest/lock_api/"><code>lock_api</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSpinLock.hh"><code>ZmSpinLock</code></a><br>➕ wraps Concurrency Kit FAS spinlock</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/atomic/atomic_flag"><code>std::atomic_flag</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/atomic/blob/master/include/boost/atomic.hpp">Boost.Atomic</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/synchronization/MicroSpinLock.h"><code>folly::MicroSpinLock</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_spinlock.h"><code>bsls::SpinLock</code></a></td>
  <td>Partial: <a href="https://docs.rs/spin/latest/spin/type.Mutex.html"><code>spin::Mutex</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPLock.hh"><code>ZmPLock</code></a><br>➕ platform-tuned primitive mutex<br>➖ cannot be used with <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCondition.hh"><code>ZmCondition</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/thread/mutex"><code>std::mutex</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/mutex.hpp"><code>boost::mutex</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/synchronization/DistributedMutex.h"><code>folly::DistributedMutex</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_qlock.h"><code>bslmt::QLock</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/sync/struct.Mutex.html"><code>Mutex</code></a>; <a href="https://docs.rs/parking_lot/latest/parking_lot/type.Mutex.html"><code>parking_lot::Mutex</code></a> for an alternative implementation</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLock.hh"><code>ZmLock</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/thread/recursive_mutex"><code>std::recursive_mutex</code></a></td>
  <td><a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/recursive_mutex.hpp"><code>boost::recursive_mutex</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_mutex.h"><code>bslmt::Mutex</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/sync/struct.Mutex.html"><code>Mutex</code></a>; <a href="https://docs.rs/parking_lot/latest/parking_lot/type.Mutex.html"><code>parking_lot::Mutex</code></a> for an alternative implementation</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRWLock.hh"><code>ZmRWLock</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/thread/shared_mutex"><code>std::shared_mutex</code></a></td>
  <td><a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/shared_mutex.hpp"><code>boost::shared_mutex</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/SharedMutex.h"><code>folly::SharedMutex / SharedMutexReadPriority</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_readerwritermutex.h"><code>bslmt::ReaderWriterMutex</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/sync/struct.RwLock.html"><code>RwLock</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmGuard.hh"><code>ZmGuard</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNoLock.hh"><code>ZmReadGuard</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/thread/unique_lock"><code>std::unique_lock</code></a>, <a href="https://en.cppreference.com/cpp/thread/shared_lock"><code>std::shared_lock</code></a></td>
  <td><a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/lock_types.hpp"><code>boost::unique_lock</code></a>, <a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/lock_types.hpp"><code>boost::shared_lock</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Synchronized.h"><code>folly::Synchronized locking proxies</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_lockguard.h"><code>bslmt::LockGuard</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_readlockguard.h"><code>ReadLockGuard</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/sync/struct.MutexGuard.html"><code>MutexGuard</code></a> / <a href="https://doc.rust-lang.org/std/sync/struct.RwLockReadGuard.html"><code>RwLockReadGuard</code></a></td>
</tr>
</tbody>
</table>

## Conditions, semaphores and blocking adapters

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCondition.hh"><code>ZmCondition</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/thread/condition_variable_any"><code>std::condition_variable_any</code></a></td>
  <td><a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/condition_variable.hpp"><code>boost::condition_variable_any</code></a></td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_condition.h"><code>bslmt::Condition</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/sync/struct.Condvar.html"><code>Condvar</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSemaphore.hh"><code>ZmSemaphore</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/thread/counting_semaphore"><code>std::counting_semaphore</code></a> (C++20)</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/interprocess.html">Boost.Interprocess</a> <a href="https://github.com/boostorg/interprocess/blob/master/include/boost/interprocess/sync/interprocess_semaphore.hpp"><code>interprocess_semaphore</code></a> (partial sharing model)</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/synchronization/LifoSem.h"><code>folly::LifoSem / NativeSemaphore</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_semaphore.h"><code>bslmt::Semaphore</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/sync/struct.Semaphore.html"><code>tokio::sync::Semaphore</code></a>, <a href="https://docs.rs/parking_lot/latest/parking_lot/"><code>parking_lot</code></a> (async semaphore)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBlock.hh"><code>ZmBlock</code></a><br>➕ continuation-to-blocking bridge reusing a thread-local semaphore</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/thread/promise"><code>std::promise</code></a>, <a href="https://en.cppreference.com/cpp/thread/future"><code>std::future</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/thread.html">Boost.Thread</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/use_future.hpp"><code>use_future</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/synchronization/Baton.h"><code>folly::Baton</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_latch.h"><code>bslmt::Latch</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/sync/mpsc/fn.sync_channel.html"><code>sync_channel</code></a>, <a href="https://docs.rs/futures/latest/futures/executor/fn.block_on.html"><code>futures::executor::block_on</code></a></td>
</tr>
</tbody>
</table>

## Random number generation

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRandom.hh"><code>ZmRandom</code></a><br>➕ framework Mersenne Twister wrapper<br>➖ not cryptographic</td>
  <td><a href="https://en.cppreference.com/cpp/numeric/random/mersenne_twister_engine"><code>std::mt19937</code></a><br>➕ standard distribution family</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_random.html">Boost.Random</a> Mersenne Twister and distributions</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Random.h"><code>folly::Random / ThreadLocalPRNG</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_random.h"><code>bsl::mt19937</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_random.h"><code>random_device</code></a></td>
  <td><a href="https://docs.rs/rand/latest/rand/"><code>rand</code></a>; <a href="https://docs.rs/rand_mt/latest/rand_mt/"><code>rand_mt</code></a> if the Mersenne Twister algorithm is required</td>
</tr>
</tbody>
</table>

## Clocks, timing statistics and retry policies

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTime.hh"><code>ZmTime</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/chrono"><code>std::chrono</code></a> clocks</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/chrono.html">Boost.Chrono</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Chrono.h"><code>folly::chrono::coarse_steady_clock / coarse_system_clock</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_systemtime.h"><code>bsls::SystemTime</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/time/struct.Instant.html"><code>Instant</code></a> / <a href="https://doc.rust-lang.org/std/time/struct.SystemTime.html"><code>SystemTime</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTimeInterval.hh"><code>ZmTimeInterval</code></a><br>➕ locked min/max/total/mean/count aggregation</td>
  <td>Partial: chrono values + custom aggregation</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/accumulators.html">Boost.Accumulators</a> with timing values</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/stats/Histogram.h"><code>folly::Histogram</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_collector.h"><code>balm::Collector</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/time/struct.Duration.html"><code>Duration</code></a>, <a href="https://doc.rust-lang.org/std/sync/struct.Mutex.html"><code>Mutex</code></a>, <a href="https://docs.rs/hdrhistogram/latest/hdrhistogram/"><code>hdrhistogram</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBackoff.hh"><code>ZmBackoff</code></a><br>➕ configurable exponential backoff with jitter</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/header/random"><code>&lt;random&gt;</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_random.html">Boost.Random</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/chrono.html">Chrono</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/futures/Retrying.h"><code>folly::futures::retryingPolicyCappedJitteredExponentialBackoff</code></a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/backoff/latest/backoff/"><code>backoff</code></a>; exponential retry policy with randomization</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTimeout.hh"><code>ZmTimeout</code></a><br>➕ timeout state with exponential backoff</td>
  <td>Partial: chrono deadlines + custom retry policy</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/HHWheelTimer.h"><code>folly::HHWheelTimer</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_eventscheduler.h"><code>bdlmt::EventScheduler</code></a></td>
  <td>Partial: <a href="https://docs.rs/backoff/latest/backoff/"><code>backoff</code></a>, <a href="https://doc.rust-lang.org/std/time/struct.Instant.html"><code>Instant</code></a></td>
</tr>
</tbody>
</table>

## Stack and scratch allocation

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmStackAvail.hh#L20"><code>ZmStackAvail</code></a><br>➕ queries remaining stack for scratch policies</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/stacker/latest/stacker/fn.remaining_stack.html"><code>stacker::remaining_stack</code></a> (returns <code>Option&lt;usize&gt;</code>); lower level: <a href="https://docs.rs/psm/latest/psm/fn.stack_pointer.html"><code>psm::stack_pointer</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAlloc.hh#L79"><code>ZmAlloc</code></a><br>➕ checks remaining stack space and selects aligned stack storage or RAII heap fallback<br>➖ macro evaluates size more than once</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/container.html">Boost.Container</a>, <a href="https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp"><code>small_vector</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/stacker/latest/stacker/fn.remaining_stack.html"><code>stacker::remaining_stack</code></a> + <a href="https://docs.rs/alloca/latest/alloca/fn.with_alloca.html"><code>alloca::with_alloca</code></a>; custom aligned heap fallback</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLocal.hh#L60"><code>ZmLocal</code></a><br>➕ selects stack/heap storage from available stack space and runs construction/destruction</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/unique_ptr"><code>std::unique_ptr</code></a></td>
  <td>Partial: scoped/unique pointer with explicit storage selection</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h"><code>bdlma::LocalSequentialAllocator</code></a></td>
  <td>Partial: <a href="https://docs.rs/stacker/latest/stacker/fn.remaining_stack.html"><code>stacker::remaining_stack</code></a> + <a href="https://docs.rs/alloca/latest/alloca/fn.with_alloca.html"><code>alloca::with_alloca</code></a>; custom heap fallback and typed cleanup</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScratch.hh#L593"><code>ZmScratch</code></a><br>➕ runtime-sized scratch array with stack-space checks, heap fallback and element cleanup</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/vector"><code>std::vector</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp"><code>boost::container::small_vector</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/small_vector.h"><code>folly::small_vector</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h"><code>bdlma::LocalSequentialAllocator</code></a></td>
  <td>Partial: <a href="https://docs.rs/stacker/latest/stacker/fn.remaining_stack.html"><code>stacker::remaining_stack</code></a> + <a href="https://docs.rs/alloca/latest/alloca/fn.with_alloca.html"><code>alloca::with_alloca</code></a>; custom heap fallback and array wrapper</td>
</tr>
</tbody>
</table>

The Rust scratch entries describe a composition: runtime-sized, closure-scoped
stack storage with a custom heap fallback, alignment and element-lifetime policy.
[`psm`](https://docs.rs/psm/latest/psm/) supplies stack inspection/switching primitives;
it does not allocate scratch buffers. `smallvec` instead uses fixed inline capacity.

## Heaps and allocator integration

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zm/src/), [module guide](../zm/README.md).
Relevant counterparts: [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html),
[Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh"><code>ZmHeap</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh"><code>ZmNHeap</code></a><br>➕ named fixed-block arenas, partitions and telemetry<br>➕ CPU/NUMA-aware reserves<br>➖ reserves can fall back to ordinary allocation</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/allocator"><code>std::allocator</code></a>, <a href="https://en.cppreference.com/cpp/memory/synchronized_pool_resource"><code>std::pmr::synchronized_pool_resource</code></a></td>
  <td><a href="https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp">Boost.Pool</a><br>➕ reusable pool allocator vocabulary<br>➖ no equivalent integrated naming/NUMA/telemetry layer</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/memory/Arena.h"><code>folly::Arena / ThreadCachedArena</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentpool.h"><code>bdlma::ConcurrentPool</code></a></td>
  <td>Partial: <a href="https://docs.rs/slab/latest/slab/"><code>slab</code></a>, <a href="https://docs.rs/bumpalo/latest/bumpalo/"><code>bumpalo</code></a> (custom NUMA/tuning)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmVHeap.hh"><code>ZmVHeap</code></a><br>➕ named variable-size recycling classes and instrumentation</td>
  <td><a href="https://en.cppreference.com/cpp/memory/memory_resource"><code>std::pmr</code></a> pool resources</td>
  <td><a href="https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp">Boost.Pool</a> / <a href="https://www.boost.org/doc/libs/latest/doc/html/container.html">Container</a> PMR</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/memory/Arena.h"><code>folly::Arena</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentmultipool.h"><code>bdlma::ConcurrentMultipool</code></a></td>
  <td>Partial: <a href="https://docs.rs/bumpalo/latest/bumpalo/"><code>bumpalo</code></a>, <a href="https://docs.rs/tikv-jemallocator/latest/tikv_jemallocator/"><code>tikv-jemallocator</code></a> (different recycling policy)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAllocator.hh"><code>ZmAllocator</code></a><br>➕ connects Z heaps to standard allocator-aware containers</td>
  <td><a href="https://en.cppreference.com/cpp/memory/allocator"><code>std::allocator</code></a>, <a href="https://en.cppreference.com/cpp/memory/polymorphic_allocator"><code>std::pmr::polymorphic_allocator</code></a></td>
  <td><a href="https://github.com/boostorg/pool/blob/master/include/boost/pool/pool_alloc.hpp"><code>boost::pool_allocator</code></a> / <a href="https://www.boost.org/doc/libs/latest/doc/html/container.html">Container</a> PMR allocator</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/memory/ReentrantAllocator.h"><code>folly::reentrant_allocator</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_allocator.h"><code>bslma::Allocator</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h"><code>bsl::allocator</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/alloc/trait.GlobalAlloc.html"><code>GlobalAlloc</code></a>; <a href="https://docs.rs/allocator-api2/latest/allocator_api2/"><code>allocator-api2</code></a> for allocator-parameterized collections on stable Rust</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh"><code>ZmHeapMgr</code></a><br>➕ global named arena configuration and statistics</td>
  <td>N/A</td>
  <td>Partial: pool management with application registry</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/tikv-jemalloc-ctl/latest/tikv_jemalloc_ctl/"><code>tikv-jemalloc-ctl</code></a></td>
</tr>
</tbody>
</table>

## Scratch-buffer codecs

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh"><code>ZmCodec</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh"><code>ZmBase64</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh"><code>ZmBase32</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh"><code>ZmHex</code></a>)<br>➕ scratch-buffer codecs delivered through callbacks</td>
  <td>N/A</td>
  <td>Partial: Boost encoding iterators + scratch storage</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoder.h"><code>bdlde::Base64Encoder</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_hexencoder.h"><code>HexEncoder</code></a></td>
  <td>Partial: <a href="https://docs.rs/base64/latest/base64/"><code>base64</code></a>, <a href="https://docs.rs/data-encoding/latest/data_encoding/"><code>data-encoding</code></a>, <a href="https://docs.rs/hex/latest/hex/"><code>hex</code></a></td>
</tr>
</tbody>
</table>

## Intrusive ownership and callbacks

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmObject.hh"><code>ZmObject</code></a><br>➕ embedded atomic reference count</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/enable_shared_from_this"><code>std::enable_shared_from_this</code></a></td>
  <td><a href="https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp"><code>boost::intrusive_ref_counter</code></a><code>&lt;T, </code><a href="https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp"><code>boost::thread_safe_counter</code></a><code>&gt;</code></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/sync/struct.Arc.html"><code>Arc</code></a> (non-intrusive)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRef.hh"><code>ZmRef</code></a><br>➕ intrusive shared ownership<br>➖ no weak observers</td>
  <td><a href="https://en.cppreference.com/cpp/memory/shared_ptr"><code>std::shared_ptr</code></a><br>➕ arbitrary pointees and <a href="https://en.cppreference.com/cpp/memory/weak_ptr"><code>std::weak_ptr</code></a></td>
  <td><a href="https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ptr.hpp"><code>boost::intrusive_ptr</code></a> with atomic hooks</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/sync/struct.Arc.html"><code>Arc</code></a> (non-intrusive)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolymorph.hh"><code>ZmPolymorph</code></a></td>
  <td>Partial: shared ownership of a virtual-destructor base</td>
  <td>Partial: intrusive ownership of a virtual-destructor base</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/reference/types/trait-object.html"><code>dyn Trait</code></a> with <a href="https://doc.rust-lang.org/std/sync/struct.Arc.html"><code>Arc</code></a> for shared ownership</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmContext.hh"><code>ZmContext</code></a><br>➕ compact raw-or-owning erased context<br>➖ pointer-tag representation assumes supported architectures</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/variant"><code>std::variant</code></a>, <a href="https://en.cppreference.com/cpp/memory/shared_ptr"><code>std::shared_ptr</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/variant2/blob/master/include/boost/variant2/variant.hpp"><code>boost::variant2::variant</code></a>, <a href="https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ptr.hpp"><code>boost::intrusive_ptr</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/DiscriminatedPtr.h"><code>folly::DiscriminatedPtr</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/any/trait.Any.html"><code>Any</code></a>, <a href="https://doc.rust-lang.org/reference/types/trait-object.html">trait objects</a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmFn_.hh"><code>ZmFn</code></a><br>➕ optimized function/context representation<br>➖ larger captures can allocate</td>
  <td><a href="https://en.cppreference.com/cpp/utility/functional/function"><code>std::function</code></a>, <a href="https://en.cppreference.com/cpp/utility/functional/move_only_function"><code>std::move_only_function</code></a> (C++23)</td>
  <td><a href="https://github.com/boostorg/function/blob/master/include/boost/function.hpp"><code>boost::function</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Function.h"><code>folly::Function</code></a><br>➕ move-only captures and cv-qualified signatures<br>➖ move-only wrapper rather than <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmFn_.hh">ZmFn</a>'s copyable delegate</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h"><code>bsl::function</code></a><br>➕ allocator-aware type erasure</td>
  <td><a href="https://doc.rust-lang.org/reference/types/closure.html">closure types and call traits</a> / <a href="https://doc.rust-lang.org/reference/types/function-pointer.html">fn pointers</a>; <a href="https://doc.rust-lang.org/std/boxed/struct.Box.html"><code>Box&lt;dyn FnOnce()&gt;</code></a> for owned type erasure</td>
</tr>
</tbody>
</table>

## Thread-local state and cleanup

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSpecific.hh"><code>ZmSpecific</code></a><br>➕ explicit scope, cleanup levels and enumeration of all thread instances<br>➕ consolidates per-type TLS across Windows modules</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/tss.hpp"><code>boost::thread_specific_ptr</code></a><br>➖ no equivalent global instance-enumeration contract</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/ThreadLocal.h"><code>folly::ThreadLocal / ThreadLocalPtr</code></a><br>➕ supports cross-thread instance access<br>➖ different teardown/order and Windows module contracts</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadutil.h"><code>bslmt::ThreadUtil</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/macro.thread_local.html"><code>thread_local!</code></a>, <a href="https://docs.rs/thread_local/latest/thread_local/"><code>thread_local</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSingleton.hh"><code>ZmSingleton</code></a><br>➕ configurable lazy construction, optional final() hook and cleanup-level registration</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/thread/call_once"><code>std::call_once</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Boost.Serialization</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Singleton.h"><code>folly::Singleton / SingletonVault</code></a><br>➕ managed singleton lifecycle and dependency handling</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/sync/struct.LazyLock.html"><code>LazyLock</code></a>, <a href="https://doc.rust-lang.org/std/sync/struct.OnceLock.html"><code>OnceLock</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCleanup.hh"><code>ZmCleanup</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmGlobal.hh"><code>ZmGlobal</code></a><br>➕ ordered cleanup levels and a shared registry for singleton/TLS teardown</td>
  <td>N/A</td>
  <td>Partial: Boost singleton/TLS primitives with custom coordination</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Singleton.h"><code>folly::SingletonVault</code></a></td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/std/ops/trait.Drop.html"><code>Drop</code></a></td>
</tr>
</tbody>
</table>

## Assertions and diagnostic utilities

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAssert.hh#L38"><code>ZmAssert</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAssert.hh#L35"><code>ZmAssert_</code></a><br>➕ debug, always-on and release-fallback forms</td>
  <td><a href="https://en.cppreference.com/cpp/error/assert"><code>assert</code></a>, custom always-on check</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/assert/doc/html/assert.html">Boost.Assert</a><br>➕ configurable assertion handlers</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/SafeAssert.h"><code>FOLLY_SAFE_CHECK / FOLLY_SAFE_DCHECK</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h"><code>BSLS_ASSERT</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h"><code>BSLS_ASSERT_SAFE</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.debug_assert.html"><code>debug_assert!</code></a> / <a href="https://doc.rust-lang.org/std/macro.assert.html"><code>assert!</code></a>; fallback behavior is explicit application code</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmObjectDebug.hh"><code>ZmObjectDebug</code></a><br>➕ intrusive object lifetime diagnostics</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html">Boost.SmartPtr</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/sync/struct.Arc.html#method.strong_count"><code>Arc::strong_count</code></a>, <a href="https://docs.rs/tracing/latest/tracing/"><code>tracing</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmDemangle.hh"><code>ZmDemangle</code></a><br>➕ binutils demangling for difficult compiler names<br>➖ additional binutils dependency</td>
  <td>None: <a href="https://en.cppreference.com/cpp/types/type_info/name"><code>type_info::name()</code></a></td>
  <td><a href="https://github.com/boostorg/core/blob/master/include/boost/core/demangle.hpp"><code>boost::core::demangle</code></a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_typeindex.html">Boost.TypeIndex</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Demangle.h"><code>folly::demangle</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/cpp_demangle/latest/cpp_demangle/"><code>cpp_demangle</code></a>, <a href="https://docs.rs/rustc-demangle/latest/rustc_demangle/"><code>rustc-demangle</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBackTrace_.hh"><code>ZmBackTrace</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/utility/basic_stacktrace"><code>std::stacktrace</code></a> (C++23)</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html">Boost.Stacktrace</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/Symbolizer.h"><code>folly::symbolizer stack traces</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktraceutil.h"><code>balst::StackTraceUtil</code></a><code> / </code><a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktrace.h"><code>StackTrace</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/backtrace/struct.Backtrace.html"><code>Backtrace</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBackTracer.hh"><code>ZmBackTracer</code></a><br>➕ keeps a bounded ring of captured traces</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/basic_stacktrace"><code>std::stacktrace</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html">Boost.Stacktrace</a>, <a href="https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp">CircularBuffer</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/Symbolizer.h"><code>folly::symbolizer stack traces</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktrace.h"><code>balst::StackTrace</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/backtrace/struct.Backtrace.html"><code>Backtrace</code></a>, <a href="https://doc.rust-lang.org/std/collections/struct.VecDeque.html"><code>VecDeque</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTrap.hh"><code>ZmTrap</code></a><br>➕ last-ditch diagnostics and signal trapping</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/program/signal"><code>std::signal</code></a>, <a href="https://en.cppreference.com/cpp/error/set_terminate"><code>std::set_terminate</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html">Boost.Stacktrace</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/SignalHandler.h"><code>folly symbolizer fatal-signal handlers</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/panic/fn.set_hook.html"><code>panic::set_hook</code></a>, <a href="https://docs.rs/signal-hook/latest/signal_hook/"><code>signal-hook</code></a></td>
</tr>
</tbody>
</table>

## Containers, indices and caches

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNode.hh"><code>ZmNode</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNodeFn.hh"><code>ZmNodeFn</code></a><br>➕ explicit intrusive node ownership policies</td>
  <td>Partial: container node handles<br>➖ no general intrusive-node framework</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/intrusive.html">Boost.Intrusive</a> hooks and value traits</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/container/IntrusiveList.h"><code>folly::IntrusiveListHook</code></a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/intrusive-collections/latest/intrusive_collections/trait.Adapter.html"><code>intrusive_collections::Adapter</code></a> / <a href="https://docs.rs/intrusive-collections/latest/intrusive_collections/trait.PointerOps.html"><code>PointerOps</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmList.hh"><code>ZmList</code></a><br>➕ intrusive ownership, heap and locking policies</td>
  <td><a href="https://en.cppreference.com/cpp/container/list"><code>std::list</code></a><br>➕ standard iterators and algorithms</td>
  <td><a href="https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/list.hpp"><code>boost::intrusive::list</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/container/IntrusiveList.h"><code>folly::IntrusiveList / CountedIntrusiveList</code></a><br>➖ different locking/allocation/ownership policies</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_bidirectionallink.h"><code>bslalg::BidirectionalLink</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_bidirectionallinklistutil.h"><code>BidirectionalLinkListUtil</code></a></td>
  <td><a href="https://docs.rs/intrusive-collections/latest/intrusive_collections/linked_list/struct.LinkedList.html"><code>intrusive_collections::LinkedList</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmStack.hh"><code>ZmStack</code></a><br>➕ deletion inside stack for queued cancellation<br>➖ requires sentinel-null value semantics</td>
  <td><a href="https://en.cppreference.com/cpp/container/stack"><code>std::stack</code></a> / <a href="https://en.cppreference.com/cpp/container/vector"><code>std::vector</code></a> composition</td>
  <td><a href="https://github.com/boostorg/container/blob/master/include/boost/container/vector.hpp"><code>boost::container::vector</code></a> with stack operations</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/AtomicLinkedList.h"><code>folly::AtomicLinkedList</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_deque.h"><code>bdlcc::Deque</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/vec/struct.Vec.html"><code>Vec</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmQueue.hh"><code>ZmQueue</code></a><br>➕ FIFO/LIFO operations without shifting remaining items<br>➖ sentinel-null value contract</td>
  <td><a href="https://en.cppreference.com/cpp/container/deque"><code>std::deque</code></a>, <a href="https://en.cppreference.com/cpp/container/queue"><code>std::queue</code></a></td>
  <td><a href="https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp">Boost.CircularBuffer</a> / <a href="https://www.boost.org/doc/libs/latest/doc/html/container.html">Container</a> deque</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/MPMCQueue.h"><code>folly::MPMCQueue</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_deque.h"><code>bdlcc::Deque</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/collections/struct.VecDeque.html"><code>VecDeque</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRBTree.hh"><code>ZmRBTree</code></a><br>➕ intrusive nodes and configurable locking/ownership</td>
  <td><a href="https://en.cppreference.com/cpp/container/map"><code>std::map</code></a>, <a href="https://en.cppreference.com/cpp/container/set"><code>std::set</code></a>, multi variants</td>
  <td><a href="https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/rbtree.hpp"><code>boost::intrusive::rbtree</code></a> / set</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/ConcurrentSkipList.h"><code>folly::ConcurrentSkipList</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_rbtreenode.h"><code>bslalg::RbTreeNode</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_rbtreeutil.h"><code>RbTreeUtil</code></a></td>
  <td><a href="https://docs.rs/intrusive-collections/latest/intrusive_collections/rbtree/struct.RBTree.html"><code>intrusive_collections::RBTree</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLHash.hh"><code>ZmLHash</code></a><br>➕ by-value linear probing with Z policies</td>
  <td><a href="https://en.cppreference.com/cpp/container/unordered_map"><code>std::unordered_map</code></a> / <a href="https://en.cppreference.com/cpp/container/unordered_set"><code>std::unordered_set</code></a><br>➖ standard node-oriented contracts differ</td>
  <td><a href="https://github.com/boostorg/unordered/blob/master/include/boost/unordered/unordered_flat_map.hpp"><code>boost::unordered_flat_map</code></a> / <a href="https://github.com/boostorg/unordered/blob/master/include/boost/unordered/unordered_flat_set.hpp"><code>unordered_flat_set</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/container/F14Map.h"><code>folly::F14ValueMap / F14FastMap</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashmap.h"><code>bdlc::FlatHashMap</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashset.h"><code>FlatHashSet</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/collections/struct.HashMap.html"><code>HashMap</code></a>, <a href="https://docs.rs/hashbrown/latest/hashbrown/"><code>hashbrown</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHash.hh"><code>ZmHash</code></a><br>➕ intrusive chaining, lock striping and named tuning</td>
  <td><a href="https://en.cppreference.com/cpp/container/unordered_map"><code>std::unordered_map</code></a> / <a href="https://en.cppreference.com/cpp/container/unordered_set"><code>std::unordered_set</code></a><br>➖ caller supplies synchronization</td>
  <td><a href="https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/unordered_set.hpp"><code>boost::intrusive::unordered_set</code></a>; partial: <a href="https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html">Boost.Unordered</a> concurrent containers</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/concurrency/ConcurrentHashMap.h"><code>folly::ConcurrentHashMap</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_stripedunorderedmap.h"><code>bdlcc::StripedUnorderedMap</code></a></td>
  <td>Partial: <a href="https://docs.rs/dashmap/latest/dashmap/"><code>dashmap</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHashMgr.hh"><code>ZmHashMgr</code></a><br>➕ named sizing, striping and telemetry</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html">Boost.Unordered</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>None: <a href="https://doc.rust-lang.org/std/collections/struct.HashMap.html"><code>HashMap</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolyHash.hh"><code>ZmPolyHash</code></a><br>➕ reflected multiple intrusive hash indices in one allocation</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html">Boost.MultiIndex</a><br>➕ also supports ordered, sequenced and other index kinds</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/multi_index_map/latest/multi_index_map/"><code>multi_index_map</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCacheStats.hh"><code>ZmCacheStats</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/accumulators.html">Boost.Accumulators</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/moka/latest/moka/sync/struct.Cache.html"><code>moka::Cache</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCache.hh"><code>ZmCache</code></a><br>➕ integrated hash lookup and LRU eviction</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/list"><code>std::list</code></a>, <a href="https://en.cppreference.com/cpp/container/unordered_map"><code>std::unordered_map</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html">Boost.MultiIndex</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h"><code>folly::EvictingCacheMap</code></a><br>➖ different locking/node/heap policy</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h"><code>bdlcc::Cache</code></a></td>
  <td><a href="https://docs.rs/lru/latest/lru/struct.LruCache.html"><code>LruCache</code></a>; hash lookup with LRU eviction, non-intrusive</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolyCache.hh"><code>ZmPolyCache</code></a><br>➕ reflected multiple keys with LRU eviction</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html">Boost.MultiIndex</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h"><code>folly::EvictingCacheMap</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h"><code>bdlcc::Cache</code></a></td>
  <td>Partial: <a href="https://docs.rs/multi_index_map/latest/multi_index_map/"><code>multi_index_map</code></a>, <a href="https://docs.rs/lru/latest/lru/"><code>lru</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh"><code>ZmPQueue</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh"><code>ZmPQRx</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh"><code>ZmPQTx</code></a><br>➕ deterministic skip-list sequence reassembly; gaps and overlaps</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/map"><code>std::map</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/icl/blob/master/include/boost/icl/interval_set.hpp">Boost.ICL</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/ConcurrentSkipList.h"><code>folly::ConcurrentSkipList</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_skiplist.h"><code>bdlcc::SkipList</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/collections/struct.BTreeMap.html"><code>BTreeMap</code></a></td>
</tr>
</tbody>
</table>

## ZmRing — inter-thread communication

[`ZmRing`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh) carries records between threads; [`ZmRingFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingFn.hh) adds in-ring callable
dispatch. Folly provides related fixed-record ring facilities: its FIFO
`ProducerConsumerQueue` and cursor-based overwrite-history `LockFreeRingBuffer`.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingMirror.hh"><code>ZmRingMirror</code></a><br>➕ double-mapped storage simplifies wraparound records</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/interprocess.html">Boost.Interprocess</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/slice-deque/latest/slice_deque/"><code>slice-deque</code></a> mirrored-memory deque; platform support and record model differ</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh"><code>ZmRing</code></a><br>➕ fixed/variable records, producer policies and broadcast readers</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/lockfree.html">Boost.Lockfree</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/ProducerConsumerQueue.h"><code>folly::ProducerConsumerQueue</code></a>, <a href="https://github.com/facebook/folly/blob/main/folly/concurrency/container/LockFreeRingBuffer.h"><code>folly::LockFreeRingBuffer</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_singleproducersingleconsumerboundedqueue.h"><code>bdlcc::SingleProducerSingleConsumerBoundedQueue</code></a></td>
  <td>Partial: <a href="https://docs.rs/crossbeam-channel/latest/crossbeam_channel/"><code>crossbeam-channel</code></a>, <a href="https://docs.rs/ringbuf/latest/ringbuf/"><code>ringbuf</code></a>, <a href="https://docs.rs/tokio/latest/tokio/sync/broadcast/index.html"><code>broadcast</code></a> (different record/broadcast models)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingFn.hh"><code>ZmRingFn</code></a><br>➕ captured callable payloads stored in variable-record rings</td>
  <td>Partial: callable wrapper queued in a container</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/function.html">Boost.Function</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/lockfree.html">Lockfree</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Function.h"><code>folly::Function with a Folly queue</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_boundedqueue.h"><code>bdlcc::BoundedQueue</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h"><code>bsl::function</code></a></td>
  <td>Partial: <a href="https://docs.rs/crossbeam-channel/latest/crossbeam_channel/"><code>crossbeam-channel</code></a>, <a href="https://doc.rust-lang.org/std/boxed/struct.Box.html"><code>Box&lt;dyn FnOnce() + Send&gt;</code></a> (boxed captures)</td>
</tr>
</tbody>
</table>

## Topology and threads

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBitmap.hh"><code>ZmBitmap</code></a><br>➕ hwloc CPU/resource bitmap operations</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/vector"><code>std::vector</code></a></td>
  <td><a href="https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp"><code>boost::dynamic_bitset</code></a><br>➖ no topology integration</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_bitarray.h"><code>bdlc::BitArray</code></a></td>
  <td><a href="https://docs.rs/hwlocality/latest/hwlocality/"><code>hwlocality</code></a> CPU/node bitmap sets</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTopology.hh"><code>ZmTopology</code></a><br>➕ hwloc CPU/NUMA discovery and binding</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/thread/thread/hardware_concurrency"><code>std::thread::hardware_concurrency</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/concurrency/CacheLocality.h"><code>folly::CacheLocality / AccessSpreader</code></a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/hwlocality/latest/hwlocality/"><code>hwlocality</code></a> topology, CPU binding and NUMA facilities</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmThread.hh"><code>ZmThread</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmThread.hh"><code>ZmThreadParams</code></a><br>➕ named affinity, stack, priority and partition configuration</td>
  <td><a href="https://en.cppreference.com/cpp/thread/thread"><code>std::thread</code></a>, <a href="https://en.cppreference.com/cpp/thread/jthread"><code>std::jthread</code></a><br>➕ <a href="https://en.cppreference.com/cpp/thread/jthread"><code>jthread</code></a> joins automatically and supplies stop tokens<br>➖ native tuning needs platform code</td>
  <td><a href="https://github.com/boostorg/thread/blob/master/include/boost/thread/thread.hpp"><code>boost::thread</code></a> / thread attributes</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/executors/thread_factory/NamedThreadFactory.h"><code>folly::NamedThreadFactory / InitThreadFactory</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadutil.h"><code>bslmt::ThreadUtil</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadattributes.h"><code>ThreadAttributes</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/thread/struct.Builder.html"><code>thread::Builder</code></a>, <a href="https://docs.rs/core_affinity/latest/core_affinity/"><code>core_affinity</code></a>, <a href="https://docs.rs/thread-priority/latest/thread_priority/"><code>thread-priority</code></a></td>
</tr>
</tbody>
</table>

## Scheduling and engine lifecycle

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScheduler.hh"><code>ZmScheduler</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScheduler.hh"><code>ZmSchedParams</code></a><br>➕ named destinations, dedicated threads, timers and ring dispatch<br>➕ affinity/partition/queue configuration</td>
  <td>None: <a href="https://en.cppreference.com/cpp/execution"><code>std::execution</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a> <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/io_context.hpp"><code>io_context</code></a>, thread pool, timers and strands<br>➕ executor/completion-token ecosystem<br>➖ matching Z destination/affinity policies needs composition</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.h"><code>folly::IOThreadPoolExecutor / CPUThreadPoolExecutor</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_multiqueuethreadpool.h"><code>bdlmt::MultiQueueThreadPool</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_eventscheduler.h"><code>EventScheduler</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/runtime/struct.Builder.html"><code>Tokio runtime builder</code></a>, <a href="https://docs.rs/rayon/latest/rayon/struct.ThreadPoolBuilder.html"><code>Rayon ThreadPoolBuilder</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZmEngine.hh"><code>ZmEngine</code></a><br>➕ reusable asynchronous start/stop lifecycle and pending operations</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/statechart/doc/reference.html">Boost.Statechart</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_threadpool.h"><code>bdlmt::ThreadPool</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_pool.h"><code>pool</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio-util/latest/tokio_util/task/struct.TaskTracker.html"><code>TaskTracker</code></a>, <a href="https://docs.rs/tokio-util/latest/tokio_util/sync/struct.CancellationToken.html"><code>CancellationToken</code></a></td>
</tr>
</tbody>
</table>

## Runtime telemetry

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcHeap.hh"><code>ZtcHeap</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcHash.hh"><code>ZtcHash</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcQueue.hh"><code>ZtcQueue</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcThread.hh"><code>ZtcThread</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcTypes.hh"><code>ZtcTypes</code></a> (in <code>zm</code>)<br>➕ reflected runtime telemetry vocabulary</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a>, <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Serialization</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_metricsmanager.h"><code>balm::MetricsManager</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlm/bdlm_metricsregistry.h"><code>bdlm::MetricsRegistry</code></a></td>
  <td>Partial: <a href="https://docs.rs/metrics/latest/metrics/"><code>metrics</code></a>, <a href="https://docs.rs/tracing/latest/tracing/"><code>tracing</code></a></td>
</tr>
</tbody>
</table>

# Zt - Framework types

## Platform and environment

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zt/src/), [module guide](../zt/README.md).
Container comparisons use [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtPlatform.hh"><code>ZtPlatform</code></a> environment helpers</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/program/getenv"><code>std::getenv</code></a></td>
  <td><a href="https://github.com/boostorg/process/blob/master/include/boost/process.hpp">Boost.Process</a> environment facilities</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_cstdlib.h"><code>bsl::getenv</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_osutil.h"><code>bdls::OsUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/env/index.html"><code>std::env</code></a></td>
</tr>
</tbody>
</table>

## Boolean and enum vocabulary

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtScanBool.hh#L22"><code>ZtScanBool</code></a><br>➕ accepts case-insensitive yes/no, y/n, true/false and 1/0</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/io/manip/boolalpha"><code>std::boolalpha</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/convert/blob/master/include/boost/convert.hpp">Boost.Convert</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Conv.h"><code>folly::to&lt;bool&gt; / tryTo&lt;bool&gt;</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/primitive.str.html#method.parse"><code>str::parse::&lt;bool&gt;</code></a>, <a href="https://doc.rust-lang.org/std/primitive.str.html#method.eq_ignore_ascii_case"><code>eq_ignore_ascii_case</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBytesFmt.hh"><code>ZtBytesFmt</code></a><br>➕ shared binary-text encoding vocabulary</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/hex/latest/hex/"><code>hex</code></a>, <a href="https://docs.rs/base64/latest/base64/"><code>base64</code></a>, <a href="https://docs.rs/data-encoding/latest/data_encoding/"><code>data-encoding</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtcRAGMap.hh"><code>ZtcRAGMap</code></a> (in <code>zt</code>)<br>➕ common telemetry vocabulary with generated name lookup, conversion and iteration</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/strum/latest/strum/"><code>strum</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtEnum.hh#L106"><code>ZtEnum</code></a> macro family<br>➕ integer enum vocabulary with generated name/ordinal matching and iteration</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a> enum descriptors<br>➕ metadata can support other generic algorithms</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/strum/latest/strum/"><code>strum</code></a> (<code>EnumString</code>, <code>Display</code>, <code>EnumIter</code>)</td>
</tr>
</tbody>
</table>

## Text formatting

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtQuote.hh"><code>ZtQuote</code></a><br>➕ quoting and binary-text formatting adapters</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/io/manip/quoted"><code>std::quoted</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/io/doc/html/io.html">Boost.IO</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_literalutil.h"><code>bdlb::LiteralUtil::createQuotedEscapedCString</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/fmt/trait.Debug.html"><code>Debug</code></a>, <a href="https://docs.rs/serde_json/latest/serde_json/"><code>serde_json</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtHexDump.hh"><code>ZtHexDump</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Boost.Log</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json/serialize.hpp"><code>dump</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/String.h"><code>folly::hexDump</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_print.h"><code>bdlb::Print::hexDump</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/fmt/trait.LowerHex.html"><code>LowerHex</code></a>, <a href="https://docs.rs/pretty-hex/latest/pretty_hex/"><code>pretty-hex</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtFmt.hh"><code>ZtFmt</code></a><br>➕ reflected-field printing/scanning policies</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/format/format"><code>std::format</code></a>, <a href="https://en.cppreference.com/cpp/utility/from_chars"><code>std::from_chars</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/format/doc/format.html">Boost.Format</a>, <a href="https://github.com/boostorg/convert/blob/master/include/boost/convert.hpp">Convert</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h"><code>bsl::format</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/fmt/trait.Display.html"><code>Display</code></a>, <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a></td>
</tr>
</tbody>
</table>

## Character case and encoding conversion

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtCase.hh"><code>ZtCase</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/header/cctype"><code>&lt;cctype&gt;</code></a>, <a href="https://en.cppreference.com/cpp/header/cwctype"><code>&lt;cwctype&gt;</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/string_algo/reference.html">Boost.StringAlgo</a> / <a href="https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html">Locale</a> case conversion</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/String.h"><code>folly::toLowerAscii / toUpperAscii</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_string.h"><code>bdlb::String::toLower</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/primitive.str.html#method.to_lowercase"><code>str::to_lowercase</code></a> / <a href="https://doc.rust-lang.org/std/primitive.str.html#method.to_uppercase"><code>to_uppercase</code></a>; ASCII in-place methods also available</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtIconv.hh"><code>ZtIconv</code></a><br>➕ iconv conversions into adapted output buffers</td>
  <td>Partial: legacy locale conversion facilities</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html">Boost.Locale</a> encoding conversion</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h"><code>bdlde::CharConvertUtf16</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf32.h"><code>CharConvertUtf32</code></a></td>
  <td><a href="https://docs.rs/encoding_rs/latest/encoding_rs/"><code>encoding_rs</code></a> (buffer-oriented encoding conversion); <a href="https://docs.rs/iconv/latest/iconv/"><code>iconv</code></a> for native iconv bindings</td>
</tr>
</tbody>
</table>

## Owning arrays, strings and scratch buffers

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtArray.hh"><code>ZtArray</code></a><br>➕ owning or shadowing storage and direct capacity control<br>➖ ownership mode and initialization require care</td>
  <td><a href="https://en.cppreference.com/cpp/container/vector"><code>std::vector</code></a><br>➕ uniform owning semantics and standard iterators</td>
  <td><a href="https://github.com/boostorg/container/blob/master/include/boost/container/vector.hpp"><code>boost::container::vector</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/container/FBVector.h"><code>folly::fbvector</code></a><br>➖ owning storage; no Z shadowing mode</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_vector.h"><code>bsl::vector</code></a><br>➕ allocator propagation<br>➖ owning container; no non-owning shadow mode</td>
  <td><a href="https://doc.rust-lang.org/std/vec/struct.Vec.html"><code>Vec&lt;T&gt;</code></a> / <a href="https://doc.rust-lang.org/std/borrow/enum.Cow.html"><code>Cow&lt;[T]&gt;</code></a>; owned and borrowed modes are explicit</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh"><code>ZtString</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh"><code>ZtWString</code></a><br>➕ inline storage, named heaps and stream-style appending</td>
  <td><a href="https://en.cppreference.com/cpp/string/basic_string"><code>std::string</code></a>, <a href="https://en.cppreference.com/cpp/string/basic_string"><code>std::wstring</code></a><br>➕ standard string/range integration</td>
  <td><a href="https://github.com/boostorg/container/blob/master/include/boost/container/string.hpp"><code>boost::container::basic_string</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/FBString.h"><code>folly::fbstring</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h"><code>bsl::basic_string</code></a><br>➕ allocator-aware strings<br>➖ owning container; no non-owning shadow mode</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/string/struct.String.html"><code>String</code></a>, <a href="https://docs.rs/compact_str/latest/compact_str/"><code>compact_str</code></a>, <a href="https://docs.rs/widestring/latest/widestring/"><code>widestring</code></a> (different heap policy)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBuiltin.hh"><code>ZtBuiltin</code></a><br>➕ adds an initial inline buffer to array storage</td>
  <td>None: <a href="https://en.cppreference.com/cpp/container/inplace_vector"><code>std::inplace_vector</code></a></td>
  <td><a href="https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp"><code>boost::container::small_vector</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/small_vector.h"><code>folly::small_vector</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localbufferedobject.h"><code>bdlma::LocalBufferedObject</code></a></td>
  <td><a href="https://docs.rs/smallvec/latest/smallvec/struct.SmallVec.html"><code>SmallVec</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtScratch.hh#L83"><code>ZtScratch</code></a><br>➕ <a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtArray.hh">ZtArray</a>/<a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh">ZtString</a> operations over runtime stack backing with heap fallback and cleanup</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/vector"><code>std::vector</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp"><code>boost::container::small_vector</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/small_vector.h"><code>folly::small_vector</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h"><code>bdlma::LocalSequentialAllocator</code></a></td>
  <td>Partial: <a href="https://docs.rs/stacker/latest/stacker/fn.remaining_stack.html"><code>stacker::remaining_stack</code></a> + <a href="https://docs.rs/alloca/latest/alloca/fn.with_alloca.html"><code>alloca::with_alloca</code></a>; custom heap fallback and array/string wrapper</td>
</tr>
</tbody>
</table>

## Bitmaps and sliding windows

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBitmap.hh"><code>ZtBitmap</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/vector"><code>std::vector</code></a></td>
  <td><a href="https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp"><code>boost::dynamic_bitset</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_bitarray.h"><code>bdlc::BitArray</code></a></td>
  <td><a href="https://docs.rs/bitvec/latest/bitvec/vec/struct.BitVec.html"><code>BitVec</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtWindow.hh"><code>ZtWindow</code></a><br>➕ indexed sliding window with assignment-driven extension</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/deque"><code>std::deque</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp">Boost.CircularBuffer</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/collections/struct.VecDeque.html"><code>VecDeque</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBitWindow.hh"><code>ZtBitWindow</code></a><br>➕ sliding packed values in selected bit widths</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp">Boost.DynamicBitset</a>, <a href="https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp">CircularBuffer</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/bitvec/latest/bitvec/"><code>bitvec</code></a></td>
</tr>
</tbody>
</table>

## Timezone conversion

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtTimeZone.hh#L37"><code>Zt::tzOffset</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtTimeZone.hh"><code>ZtTimeZone</code></a><code>.hh</code>)<br>➖ explicit zones temporarily change <a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtTimeZone.hh"><code>TZ</code></a> under a global lock</td>
  <td><a href="https://en.cppreference.com/cpp/chrono/time_zone"><code>std::chrono::time_zone</code></a>, <a href="https://en.cppreference.com/cpp/chrono/zoned_time"><code>zoned_time</code></a> (C++20)<br>➕ independent zone objects and timezone database</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/date_time.html">Boost.DateTime</a> timezone facilities</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bal/baltzo/baltzo_timezoneutil.h"><code>baltzo::TimeZoneUtil</code></a><br>➕ explicit zone-info loading/cache and DST handling</td>
  <td><a href="https://docs.rs/chrono-tz/latest/chrono_tz/"><code>chrono-tz</code></a> / <a href="https://docs.rs/jiff/latest/jiff/"><code>jiff</code></a> timezone lookup without changing process <code>TZ</code></td>
</tr>
</tbody>
</table>

## Demangling

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtDemangle.hh"><code>ZtDemangle</code></a><br>➕ rewrites Z structural-string/template names for readability</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp">Boost.Core</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Demangle.h"><code>folly::demangle</code></a></td>
  <td>Partial: <a href="https://docs.rs/cpp_demangle/latest/cpp_demangle/"><code>cpp_demangle</code></a>, <a href="https://docs.rs/rustc-demangle/latest/rustc_demangle/"><code>rustc-demangle</code></a></td>
</tr>
</tbody>
</table>

## Regular expressions

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtRegex.hh"><code>ZtRegex</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zt/src/ZtRegex.hh#L290"><code>ZtREGEX</code></a><br>➕ PCRE syntax, captures and substitutions<br>➖ external PCRE dependency</td>
  <td><a href="https://en.cppreference.com/cpp/regex/basic_regex"><code>std::regex</code></a><br>➖ different syntax/feature contract</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/regex/doc/html/boost_regex/ref.html">Boost.Regex</a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlpcre/bdlpcre_regex.h"><code>bdlpcre::RegEx</code></a><br>➕ compiled PCRE expression wrapper</td>
  <td><a href="https://docs.rs/regex/latest/regex/"><code>regex</code></a>; <a href="https://docs.rs/pcre2/latest/pcre2/"><code>pcre2</code></a> for PCRE2 syntax/features</td>
</tr>
</tbody>
</table>

# Ze - Framework error handling

## Native errors

Sources: [ZePlatform.hh](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh), [module guide](../ze/README.md).
[Ze](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) constructs diagnostics; the asynchronous sink runtime is [`ZiLog`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh) below.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeOK</code></a></td>
  <td>Default-constructed success <a href="https://en.cppreference.com/cpp/error/error_code"><code>std::error_code</code></a></td>
  <td>Default-constructed success <a href="https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html">Boost.System</a> <a href="https://github.com/boostorg/system/blob/master/include/boost/system/error_code.hpp"><code>error_code</code></a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/result/enum.Result.html"><code>Result::Ok(())</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeError</code></a><br>➕ native OS-error wrapper and message formatting</td>
  <td><a href="https://en.cppreference.com/cpp/error/error_code"><code>std::error_code</code></a><br>➕ extensible error categories and conditions</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html">Boost.System</a> <a href="https://github.com/boostorg/system/blob/master/include/boost/system/error_code.hpp"><code>error_code</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Exception.h"><code>folly::makeSystemErrorExplicit</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>bsl::error_code</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>system_error</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/io/struct.Error.html"><code>io::Error</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeLastError</code></a><br>➕ captures native last-error state at the call site</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/error/errno"><code>errno</code></a>, <a href="https://en.cppreference.com/cpp/error/error_code"><code>std::error_code</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html">Boost.System</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Exception.h"><code>folly::makeSystemError</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>bsl::error_code</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>system_error</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/io/struct.Error.html#method.last_os_error"><code>io::Error::last_os_error</code></a>; <a href="https://docs.rs/windows-sys/latest/windows_sys/Win32/Networking/WinSock/index.html"><code>WinSock APIs</code></a> for explicit Windows socket last-error handling</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeLastSockError</code></a><br>➕ distinguishes Windows socket errors from ordinary OS errors</td>
  <td>None: <a href="https://en.cppreference.com/cpp/error/error_code"><code>std::error_code</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>bsl::error_code</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>system_error</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/io/struct.Error.html#method.last_os_error"><code>io::Error::last_os_error</code></a>; <a href="https://docs.rs/windows-sys/latest/windows_sys/Win32/Networking/WinSock/index.html"><code>WinSock APIs</code></a> for explicit Windows socket last-error handling</td>
</tr>
</tbody>
</table>

## Diagnostic metadata and events

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>Ze::Debug</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>Info</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>Warning</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZeLib.hh"><code>Error</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>Fatal</code></a><br>➕ shared severity vocabulary with generated name lookup and event/log integration</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Boost.Log</a> <a href="https://github.com/boostorg/log/blob/master/include/boost/log/trivial.hpp"><code>trivial::severity_level</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/logging/LogLevel.h"><code>folly::LogLevel</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_severity.h"><code>ball::Severity</code></a></td>
  <td>Partial: <a href="https://docs.rs/log/latest/log/enum.Level.html"><code>log::Level</code></a>, <a href="https://docs.rs/tracing/latest/tracing/struct.Level.html"><code>tracing::Level</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeEventInfo</code></a><br>➕ timestamp, source, severity and component identity</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/source_location"><code>std::source_location</code></a></td>
  <td><a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Boost.Log</a> attributes / record metadata</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/logging/LogMessage.h"><code>folly::LogMessage metadata</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_recordattributes.h"><code>ball::RecordAttributes</code></a></td>
  <td><a href="https://docs.rs/tracing/latest/tracing/struct.Metadata.html"><code>tracing::Metadata</code></a>; timestamps are supplied by the subscriber</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeEvent</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeAnyEvent</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeMsgFn</code></a><br>➕ structured events with deferred or type-erased formatting</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Boost.Log</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/logging/LogMessage.h"><code>folly::LogMessage</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_record.h"><code>ball::Record</code></a></td>
  <td><a href="https://docs.rs/tracing/latest/tracing/struct.Event.html"><code>tracing::Event</code></a> / <a href="https://docs.rs/log/latest/log/struct.Record.html"><code>log::Record</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L375"><code>ZeEVENT</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L364"><code>ZeMkEvent</code></a><br>➕ call-site metadata or explicit metadata construction</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/source_location/current"><code>std::source_location::current</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Boost.Log</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/logging/xlog.h"><code>XLOG / XLOGF</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh">Ze</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_log.h"><code>BALL_LOG_*</code></a></td>
  <td><a href="https://docs.rs/tracing/latest/tracing/macro.event.html"><code>tracing::event!</code></a> / <a href="https://doc.rust-lang.org/std/panic/struct.Location.html"><code>Location</code></a></td>
</tr>
</tbody>
</table>

## Exceptions

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh"><code>ZeException</code></a><br>➕ same diagnostic vocabulary as logging events<br>➖ not the <a href="https://en.cppreference.com/cpp/error/exception/what"><code>std::exception::what</code></a><code>()</code> interface</td>
  <td><a href="https://en.cppreference.com/cpp/error/exception"><code>std::exception</code></a>, <a href="https://en.cppreference.com/cpp/error/system_error"><code>std::system_error</code></a><br>➕ standard catch and message interface</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/exception/doc/boost-exception.html">Boost.Exception</a><br>➕ extensible attached diagnostic information</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/ExceptionWrapper.h"><code>folly::exception_wrapper</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh">Ze</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>bsl::system_error</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/error/trait.Error.html"><code>Error</code></a>, <a href="https://docs.rs/thiserror/latest/thiserror/"><code>thiserror</code></a>, <a href="https://doc.rust-lang.org/std/result/enum.Result.html"><code>Result</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L391"><code>ZeEXCEPT</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L380"><code>ZeMkException</code></a></td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/source_location"><code>std::source_location</code></a></td>
  <td>Partial: <a href="https://github.com/boostorg/throw_exception/blob/master/include/boost/throw_exception.hpp"><code>BOOST_THROW_EXCEPTION</code></a>, <a href="https://github.com/boostorg/exception/blob/master/include/boost/exception/enable_error_info.hpp"><code>boost::enable_error_info</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L391"><code>ZeEXCEPT</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Exception.h"><code>folly::makeSystemErrorExplicit</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h"><code>bsl::system_error</code></a></td>
  <td>Partial: <a href="https://docs.rs/thiserror/latest/thiserror/"><code>thiserror</code></a>, <a href="https://docs.rs/anyhow/latest/anyhow/"><code>anyhow</code></a></td>
</tr>
</tbody>
</table>

# Zf - Structured metadata and format adapters

## Configuration trees and typed loading

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zf/src/), [module guide](../zf/README.md).
Closest reflection counterparts: [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html),
[Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html).
JSON counterpart: [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html).
Format parsers assembled with Spirit are **partial** equivalents, not supplied codecs.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfTree.hh"><code>ZfTree</code></a><br>➕ owned typed configuration nodes and diagnostic paths</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/utility/variant"><code>std::variant</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/property_tree.html">Boost.PropertyTree</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/json/dynamic.h"><code>folly::dynamic</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdld/bdld_datum.h"><code>bdld::Datum</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdld/bdld_manageddatum.h"><code>ManagedDatum</code></a></td>
  <td>Partial: <a href="https://docs.rs/config/latest/config/"><code>config</code></a>, <a href="https://docs.rs/serde_json/latest/serde_json/enum.Value.html"><code>serde_json::Value</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfTreeLoad.hh"><code>ZfTreeLoad</code></a><br>➕ shared construction/load/update and validation policies</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/json/DynamicParser.h"><code>folly::DynamicParser / DynamicConverter</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_symbolicconverter.h"><code>bdlat_SymbolicConverter</code></a></td>
  <td>Partial: <a href="https://docs.rs/serde/latest/serde/de/index.html"><code>Serde deserialization</code></a>, <a href="https://docs.rs/config/latest/config/"><code>config</code></a></td>
</tr>
</tbody>
</table>

## Structure reflection and serialization metadata

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfStruct.hh#L3438"><code>ZfStruct</code></a><br>➕ typed/runtime field metadata, facets and serialization properties in C++23<br>➕ constructor/load/update/key policies shared across formats<br>➖ substantial template/macro machinery</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Boost.Hana</a>, <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Describe</a>, <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Serialization</a>, <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h"><code>bdlat</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_encoder.h"><code>baljsn</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balxml/balxml_encoder.h"><code>balxml</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balber/balber_berencoder.h"><code>balber</code></a></td>
  <td>Partial: <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a>, <a href="https://docs.rs/schemars/latest/schemars/"><code>schemars</code></a></td>
</tr>
</tbody>
</table>

## Text and web formats

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE / NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCSV.hh"><code>ZfCSV</code></a><br>➕ streaming rows with reflected header/column binding</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/tokenizer/doc/tokenizer.htm">Boost.Tokenizer</a>, <a href="https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp">Spirit</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/csv/latest/csv/"><code>csv</code></a> with Serde record/header binding</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfURI.hh"><code>ZfURI</code></a><br>➕ typed path/query/form binding and direct path loading</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/url/blob/master/include/boost/url.hpp">Boost.URL</a><br>➕ URI parsing, components and normalization<br>➖ no matching reflected field/facet loader</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Uri.h"><code>folly::Uri</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h"><code>ntsa::Uri</code></a></td>
  <td><a href="https://docs.rs/serde_urlencoded/latest/serde_urlencoded/"><code>serde_urlencoded</code></a> for query/form binding; <a href="https://docs.rs/axum/latest/axum/extract/index.html"><code>axum extractors</code></a> for typed paths</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfJSON.hh"><code>ZfJSON</code></a><br>➕ reflected load/save, fixed-field matching and in-place decoding<br>➖ borrowed text requires mutable input and retained backing</td>
  <td>N/A</td>
  <td><a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">Boost.JSON</a><br>➕ general <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a> values, streaming and type conversion<br>➖ different metadata/update contract</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/json/json.h"><code>folly::parseJson / toJson + dynamic conversion</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_encoder.h"><code>baljsn::Encoder</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_decoder.h"><code>Decoder</code></a></td>
  <td><a href="https://docs.rs/serde_json/latest/serde_json/"><code>serde_json</code></a>; <a href="https://docs.rs/simd-json/latest/simd_json/"><code>simd-json</code></a> for in-place borrowed decoding</td>
</tr>
</tbody>
</table>

## Configuration formats

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCf.hh"><code>ZfCf</code></a><br>➕ native configuration syntax, substitutions and typed updates</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/property_tree.html">PropertyTree</a></td>
  <td>Partial: <a href="https://docs.rs/config/latest/config/"><code>config</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfTOML.hh"><code>ZfTOML</code></a><br>➕ owned typed tree, field binding, formatting and parser limits</td>
  <td>N/A</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/property_tree.html">Boost.PropertyTree</a></td>
  <td><a href="https://docs.rs/toml/latest/toml/"><code>toml</code></a> / <a href="https://docs.rs/toml_edit/latest/toml_edit/"><code>toml_edit</code></a> with Serde</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfYAML.hh"><code>ZfYAML</code></a><br>➕ owned typed tree, field binding and bounded parsing<br>➖ restricted single-document subset; rejects tags and cyclic/shared graphs</td>
  <td>N/A</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/property_tree.html">Boost.PropertyTree</a></td>
  <td>Partial: <a href="https://docs.rs/serde-saphyr/latest/serde_saphyr/"><code>serde-saphyr</code></a>, <a href="https://docs.rs/serde_yaml/latest/serde_yaml/"><code>serde_yaml</code></a> (serde_yaml unmaintained)</td>
</tr>
</tbody>
</table>

## Command-line binding

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCLI.hh"><code>ZfCLI</code></a><br>➕ metadata-bound options/positionals, command text and argv output</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a><br>➕ established option descriptions, parsing and notification<br>➖ reflected facets and reverse serialization need custom work</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/cli/NestedCommandLineApp.h"><code>folly::NestedCommandLineApp</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balcl/balcl_commandline.h"><code>balcl::CommandLine</code></a></td>
  <td>Partial: <a href="https://docs.rs/clap/latest/clap/"><code>clap</code></a></td>
</tr>
</tbody>
</table>

## Binary wire formats

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCBOR.hh"><code>ZfCBOR</code></a><br>➕ reflected binary object/array mapping</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">Boost.JSON</a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/ciborium/latest/ciborium/"><code>ciborium</code></a> with Serde</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfASN1.hh"><code>ZfASN1</code></a><br>➕ schema properties bind C++ fields to tags and wire encodings</td>
  <td>N/A</td>
  <td>None: <a href="https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp">Spirit</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balber/balber_berencoder.h"><code>balber::BerEncoder</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balber/balber_berdecoder.h"><code>BerDecoder</code></a></td>
  <td><a href="https://docs.rs/rasn/latest/rasn/"><code>rasn</code></a> ASN.1 types/derive codecs</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfDBUS.hh"><code>ZfDBUS</code></a><br>➕ reflected signatures, alignment and message-body serialization</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/zvariant/latest/zvariant/"><code>zvariant</code></a> D-Bus signatures and serialization derives</td>
</tr>
</tbody>
</table>

# Zi - Framework I/O

## Native paths and scatter/gather descriptors

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zi/src/), [module guide](../zi/README.md).
Networking counterparts are in [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html).
The STL has no C++23 socket/event-loop API.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE / NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh"><code>ZiPlatform</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiDir.hh"><code>Zi::Path</code></a> and native I/O types</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/filesystem/path"><code>std::filesystem::path</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html">Boost.Filesystem</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_pathutil.h"><code>bdls::PathUtil</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h"><code>FilesystemUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/path/index.html"><code>std::path</code></a> / <a href="https://doc.rust-lang.org/std/os/index.html"><code>std::os</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh"><code>ZiVec</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh#L104"><code>ZiVec_ptr</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh#L105"><code>ZiVec_len</code></a><br>➕ portable native scatter/gather descriptors</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/span"><code>std::span</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a> mutable/const buffer sequences</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h"><code>folly::IOBuf iovec conversion</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_buffer.h"><code>ntsa::ConstBuffer</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_buffer.h"><code>MutableBuffer</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/io/struct.IoSlice.html"><code>IoSlice</code></a> / <a href="https://doc.rust-lang.org/std/io/struct.IoSliceMut.html"><code>IoSliceMut</code></a></td>
</tr>
</tbody>
</table>

## Process and module utilities

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiProgram.hh"><code>ZiProgram</code></a><br>➕ current executable/process information</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_dll/reference.html">Boost.DLL</a>, <a href="https://github.com/boostorg/process/blob/master/include/boost/process.hpp">Process</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_processutil.h"><code>bdls::ProcessUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/env/fn.current_exe.html"><code>env::current_exe</code></a> / <a href="https://doc.rust-lang.org/std/process/fn.id.html"><code>process::id</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiModule.hh"><code>ZiModule</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_dll/reference.html">Boost.DLL</a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/libloading/latest/libloading/"><code>libloading</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPIDFile.hh"><code>ZiPIDFile</code></a><br>➕ PID file ownership and cleanup</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/interprocess.html">Boost.Interprocess</a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/daemonize/latest/daemonize/"><code>daemonize</code></a> PID-file support; separate RAII ownership/locking policy may be needed</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiDaemon.hh"><code>ZiDaemon</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/process/blob/master/include/boost/process.hpp">Boost.Process</a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/daemonize/latest/daemonize/"><code>daemonize</code></a> (Unix)</td>
</tr>
</tbody>
</table>

## Directories and path completion

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiDir.hh"><code>ZiDir</code></a></td>
  <td><a href="https://en.cppreference.com/cpp/filesystem/directory_iterator"><code>std::filesystem::directory_iterator</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html">Boost.Filesystem</a> directory iterator</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h"><code>bdls::FilesystemUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/fs/fn.read_dir.html"><code>fs::read_dir</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiGlob.hh"><code>ZiGlob</code></a><br>➕ directory/path completion for interactive CLIs<br>➖ not a full glob-expression engine</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/filesystem"><code>std::filesystem</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html">Boost.Filesystem</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/string_algo/reference.html">StringAlgo</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h"><code>bdls::FilesystemUtil</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/fs/fn.read_dir.html"><code>fs::read_dir</code></a> for prefix completion; <a href="https://docs.rs/glob/latest/glob/"><code>glob</code></a> / <a href="https://docs.rs/rustyline/latest/rustyline/"><code>rustyline</code></a> for patterns/interactive completion</td>
</tr>
</tbody>
</table>

## File operations and structured output

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiFile.hh"><code>ZiFile</code></a><br>➕ direct native unbuffered file operations</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/io/basic_fstream"><code>std::fstream</code></a>, <a href="https://en.cppreference.com/cpp/filesystem"><code>std::filesystem</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html">Boost.Filesystem</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html">Iostreams</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/File.h"><code>folly::File + FileUtil operations</code></a><br>➕ RAII descriptor ownership and file locking</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h"><code>bdls::FilesystemUtil</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_fdstreambuf.h"><code>FdStreamBuf</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/fs/struct.File.html"><code>File</code></a> / <a href="https://doc.rust-lang.org/std/fs/struct.OpenOptions.html"><code>OpenOptions</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiFileTxStream.hh"><code>ZiFileTxStream</code></a><br>➕ file-backed buffered Z transmit stream</td>
  <td><a href="https://en.cppreference.com/cpp/io/basic_ofstream"><code>std::ofstream</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html">Boost.Iostreams</a> file sink / stream</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/FileUtil.h"><code>folly::writeFull</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_fdstreambuf.h"><code>bdls::FdStreamBuf</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/io/struct.BufWriter.html"><code>BufWriter&lt;File&gt;</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiCSV.hh"><code>ZiCSV</code></a><br>➕ file/stream integration for reflected CSV records</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html">Boost.Iostreams</a>, <a href="https://www.boost.org/doc/libs/latest/libs/tokenizer/doc/tokenizer.htm">Tokenizer</a>, <a href="https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp">Spirit</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/csv/latest/csv/struct.Reader.html"><code>csv::Reader</code></a> / <a href="https://docs.rs/csv/latest/csv/struct.Writer.html"><code>Writer</code></a> with Serde</td>
</tr>
</tbody>
</table>

## Addresses and name resolution

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIP.hh"><code>ZiIP</code></a></td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a> <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/address.hpp"><code>ip::address</code></a> / endpoint types</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/SocketAddress.h"><code>folly::SocketAddress / IPAddress</code></a></td>
  <td><a href="https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_ipaddress.h"><code>ntsa::IpAddress</code></a><code> / </code><a href="https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_endpoint.h"><code>Endpoint</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/net/enum.IpAddr.html"><code>IpAddr</code></a> / <a href="https://doc.rust-lang.org/std/net/enum.SocketAddr.html"><code>SocketAddr</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiResolver.hh"><code>ZiResolver</code></a><br>➕ c-ares-backed asynchronous DNS with controlled runtime lifecycle</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a> <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/tcp.hpp"><code>ip::tcp::resolver</code></a> / <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/udp.hpp"><code>ip::udp::resolver</code></a><br>➖ DNS configuration/lifecycle surface differs</td>
  <td>N/A</td>
  <td><a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_resolver.h"><code>ntci::Resolver</code></a><br>➕ asynchronous name/service resolution and cache configuration</td>
  <td><a href="https://doc.rust-lang.org/std/net/trait.ToSocketAddrs.html"><code>ToSocketAddrs</code></a> (blocking); <a href="https://docs.rs/hickory-resolver/latest/hickory_resolver/"><code>hickory-resolver</code></a> for async DNS</td>
</tr>
</tbody>
</table>

## I/O buffers, allocation and contexts

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE / NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOBuf.hh"><code>ZiIOBuf</code></a><br>➕ reference-owned packet backing and allocator integration</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/container/span"><code>std::span</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h"><code>folly::IOBuf</code></a><br>➕ chainable buffers with shared storage, headroom and tailroom</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h"><code>bdlbb::Blob</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h"><code>BlobBuffer</code></a></td>
  <td><a href="https://docs.rs/bytes/latest/bytes/struct.Bytes.html"><code>Bytes</code></a> / <a href="https://docs.rs/bytes/latest/bytes/struct.BytesMut.html"><code>BytesMut</code></a>; pool and packet ownership policy differ</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOBuf.hh"><code>Zi::IOBufAllocator</code></a>, builtin/plugin allocators and allocation roles<br>➕ explicit App/Rx/Tx storage provenance and transport integration</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/memory_resource"><code>std::pmr::memory_resource</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/memory/IOBufArenaFactory.h"><code>folly::memory::makeIOBufArenaFactory</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_pooledblobbufferfactory.h"><code>bdlbb::PooledBlobBufferFactory</code></a></td>
  <td>Partial: <a href="https://docs.rs/bytes/latest/bytes/"><code>bytes</code></a>, <a href="https://docs.rs/object-pool/latest/object_pool/"><code>object-pool</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh"><code>ZiRxContext</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh"><code>ZiTxContext</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh"><code>ZiIOContext</code></a><code>.hh</code>)<br>➕ buffer, progress and continuation state for transport I/O</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncTransport.h"><code>folly::AsyncTransport ReadCallback / WriteCallback</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_receivecontext.h"><code>ntca::ReceiveContext</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_sendcontext.h"><code>SendContext</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/task/struct.Context.html"><code>task::Context</code></a>, <a href="https://docs.rs/tokio/latest/tokio/io/struct.ReadBuf.html"><code>ReadBuf</code></a></td>
</tr>
</tbody>
</table>

## Receive and transmit streams

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRxStream.hh"><code>ZiRxStream</code></a><br>➕ consumable view over queued receive buffers</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/io/basic_istream"><code>std::istream</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://github.com/boostorg/beast/blob/master/include/boost/beast/core/multi_buffer.hpp"><code>multi_buffer</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/Cursor.h"><code>folly::io::Cursor + IOBufQueue</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h"><code>bdlbb::InBlobStreamBuf</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h"><code>OutBlobStreamBuf</code></a></td>
  <td><a href="https://docs.rs/bytes/latest/bytes/buf/trait.Buf.html"><code>bytes::Buf</code></a> / <a href="https://docs.rs/tokio/latest/tokio/io/trait.AsyncBufRead.html"><code>AsyncBufRead</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiTxStream.hh"><code>ZiTxStream</code></a><br>➕ streamed output into packet/frame-sized buffer sequences</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/io/basic_ostream"><code>std::ostream</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/Cursor.h"><code>folly::io::QueueAppender + IOBufQueue</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h"><code>bdlbb::InBlobStreamBuf</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h"><code>OutBlobStreamBuf</code></a></td>
  <td>Partial: <a href="https://docs.rs/bytes/latest/bytes/buf/trait.BufMut.html"><code>bytes::BufMut</code></a>, <a href="https://docs.rs/tokio/latest/tokio/io/trait.AsyncWrite.html"><code>AsyncWrite</code></a></td>
</tr>
</tbody>
</table>

## Message framing and transmission

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRx.hh"><code>ZiRx</code></a><br>➕ CRTP header/body framing and fragmented-message accumulation</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/read.hpp"><code>async_read</code></a>, <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/read_until.hpp"><code>async_read_until</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_send.h"><code>send</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_receive.h"><code>receive</code></a></td>
  <td><a href="https://docs.rs/tokio-util/latest/tokio_util/codec/trait.Decoder.html"><code>Decoder</code></a> / <a href="https://docs.rs/tokio-util/latest/tokio_util/codec/struct.FramedRead.html"><code>FramedRead</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiTx.hh"><code>ZiTx</code></a><br>➕ CRTP sender and queued completion handling</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/write.hpp"><code>async_write</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_send.h"><code>send</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_receive.h"><code>receive</code></a></td>
  <td><a href="https://docs.rs/tokio-util/latest/tokio_util/codec/struct.FramedWrite.html"><code>FramedWrite</code></a> / <a href="https://docs.rs/futures-sink/latest/futures_sink/trait.Sink.html"><code>Sink</code></a></td>
</tr>
</tbody>
</table>

## Event loops and connection multiplexing

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiEventLoop.hh"><code>ZiEventLoop</code></a><br>➕ external socket/handle readiness integration</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a> descriptors / handles and async waits</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/io/async/EventBase.h"><code>folly::EventBase / EventHandler</code></a></td>
  <td><a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_reactor.h"><code>ntci::Reactor</code></a><code> / </code><a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_proactor.h"><code>Proactor</code></a><br>➕ readiness and completion driver interfaces</td>
  <td><a href="https://docs.rs/mio/latest/mio/struct.Poll.html"><code>mio::Poll</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh"><code>ZiConnection</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh"><code>ZiCxnInfo</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh"><code>ZiCxnOptions</code></a><br>➕ transport callbacks, connection state and explicit I/O allocation roles</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h"><code>folly::AsyncSocket</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocketsession.h"><code>StreamSocketSession</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/net/struct.TcpStream.html"><code>TcpStream</code></a>, <a href="https://docs.rs/socket2/latest/socket2/struct.Socket.html"><code>socket2::Socket</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh"><code>ZiMxPlugin</code></a><br>➕ transport-plugin lifecycle integrated with the multiplexer</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/mio/latest/mio/event/trait.Source.html"><code>mio::event::Source</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh"><code>ZiMultiplex</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh"><code>ZiMxParams</code></a><br>➕ shard-directed I/O with scheduler and allocator integration</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a> sockets, <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/io_context.hpp"><code>io_context</code></a>, executors and strands<br>➕ completion tokens and coroutine integration<br>➖ Z shard/buffer ownership contracts need application composition</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.h"><code>folly::IOThreadPoolExecutor + EventBase</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_interface.h"><code>ntci::Interface</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/runtime/index.html"><code>Tokio runtime</code></a>, <a href="https://docs.rs/mio/latest/mio/"><code>mio</code></a></td>
</tr>
</tbody>
</table>

## ZiRing — inter-process communication using shared memory

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRing.hh"><code>ZiRing</code></a><br>➕ named cross-process variable-record broadcast ring<br>➖ processes must agree on layout and payload protocol</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/interprocess.html">Boost.Interprocess</a></td>
  <td>Partial: <a href="https://docs.rs/shared_memory/latest/shared_memory/"><code>shared_memory</code></a>, <a href="https://docs.rs/raw_sync/latest/raw_sync/"><code>raw_sync</code></a></td>
</tr>
</tbody>
</table>

## Logging and assertions

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiAssert.hh#L40"><code>ZiAssert</code></a><br>➕ release fallback with logging/backtrace</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/error/assert"><code>assert</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/assert/doc/html/assert.html">Boost.Assert</a>, <a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Log</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html">Stacktrace</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/lang/SafeAssert.h"><code>FOLLY_SAFE_CHECK / FOLLY_SAFE_DCHECK</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h"><code>BSLS_ASSERT</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktraceutil.h"><code>balst</code></a></td>
  <td><a href="https://doc.rust-lang.org/std/macro.debug_assert.html"><code>debug_assert!</code></a> / <a href="https://doc.rust-lang.org/std/macro.assert.html"><code>assert!</code></a> + <a href="https://docs.rs/tracing/latest/tracing/"><code>tracing</code></a>; release fallback is explicit</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh"><code>ZiLog</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh#L400"><code>ZiLOG</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh#L360"><code>ZiLogEvent</code></a><br>➕ asynchronous <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiModule.hh">Ze</a>-event fanout to multiple sinks</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/io/clog"><code>std::clog</code></a>, <a href="https://en.cppreference.com/cpp/io/basic_osyncstream"><code>std::osyncstream</code></a></td>
  <td><a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Boost.Log</a><br>➕ configurable attributes, filters and sinks</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/logging/xlog.h"><code>Folly logging: XLOG, handlers and AsyncFileWriter</code></a><br>➕ category hierarchy and configurable handler factories</td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_loggermanager.h"><code>ball::LoggerManager</code></a><code> / </code><a href="https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_asyncfileobserver.h"><code>AsyncFileObserver</code></a><br>➕ categories, rules, observers and asynchronous file output</td>
  <td><a href="https://docs.rs/tracing/latest/tracing/"><code>tracing</code></a> + <a href="https://docs.rs/tracing-subscriber/latest/tracing_subscriber/"><code>tracing-subscriber</code></a> / <a href="https://docs.rs/tracing-appender/latest/tracing_appender/"><code>tracing-appender</code></a> for asynchronous multi-sink logging</td>
</tr>
</tbody>
</table>

## Resource tuning

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiHeapTune.hh"><code>ZiHeapTune</code></a><br>➕ file-based named heap tuning and measurements</td>
  <td>N/A</td>
  <td>None: <a href="https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp">Boost.Pool</a></td>
  <td>Partial: <a href="https://docs.rs/tikv-jemalloc-ctl/latest/tikv_jemalloc_ctl/"><code>tikv-jemalloc-ctl</code></a>, <a href="https://docs.rs/config/latest/config/"><code>config</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiHashTune.hh"><code>ZiHashTune</code></a><br>➕ file-based hash configuration and measurements</td>
  <td>N/A</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html">Boost.Unordered</a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/collections/struct.HashMap.html"><code>HashMap</code></a>, <a href="https://docs.rs/config/latest/config/"><code>config</code></a>, <a href="https://docs.rs/metrics/latest/metrics/"><code>metrics</code></a></td>
</tr>
</tbody>
</table>

## I/O telemetry

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcMx.hh"><code>ZtcMx</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcLink.hh"><code>ZtcLink</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcPool.hh"><code>ZtcPool</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcHub.hh"><code>ZtcHub</code></a> (in <code>zi</code>)<br>➕ reflected I/O telemetry records</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a>, <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Serialization</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_metricsmanager.h"><code>balm::MetricsManager</code></a></td>
  <td><a href="https://docs.rs/metrics/latest/metrics/"><code>metrics</code></a> / <a href="https://docs.rs/tracing/latest/tracing/"><code>tracing</code></a>; application defines equivalent I/O telemetry records</td>
</tr>
</tbody>
</table>

# Zxdp - Linux AF_XDP packet I/O

## Registered packet storage

Sources: [module guide](../zxdp/README.md), [public headers](https://github.com/kaldron-labs/z/blob/main/zxdp/src/).
Linux-only; excluded from Windows builds.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/ZxdpAlloc.hh"><code>Zxdp::PoolParams</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/ZxdpAlloc.hh"><code>BufPool</code></a><br>➕ caller-owned intrusive pools over registered Rx/Tx arenas<br>➕ exhaustion returns null without replacement allocation<br>➕ allocation and kernel leases independently protect backing</td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp">Boost.Pool</a></td>
  <td>Partial: <a href="https://docs.rs/xsk-rs/latest/xsk_rs/umem/struct.Umem.html"><code>xsk_rs::Umem</code></a>, <a href="https://docs.rs/xdp/latest/xdp/"><code>xdp</code></a></td>
</tr>
</tbody>
</table>

## Raw packet I/O and service

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh"><code>Zxdp::Params</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh"><code>Instance</code></a><br>➕ direct AF_XDP packet submission and receive over one interface<br>➕ optional destination-mask steering with host-stack coexistence<br>➖ Linux AF_XDP and suitable kernel/driver support required</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a></td>
  <td>Partial: <a href="https://docs.rs/xsk-rs/latest/xsk_rs/socket/struct.Socket.html"><code>xsk_rs::Socket</code></a>, <a href="https://docs.rs/xdp/latest/xdp/"><code>xdp</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L62"><code>Zxdp::Instance::rx</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L65"><code>wake</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L66"><code>pollTx</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L70"><code>close</code></a><br>➕ configurable spin-before-epoll receive service and explicit wake<br>➕ bounded Tx retirement with retryable kernel-safe close</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Boost.Asio</a></td>
  <td>Partial: <a href="https://docs.rs/xsk-rs/latest/xsk_rs/"><code>xsk-rs</code></a></td>
</tr>
</tbody>
</table>

# Zutcp - User-mode TCP/IP stack

## User-mode TCP/IP stack

Sources: [module guide](../zutcp/README.md)
These comparisons describe the planned TCP goal-state; implementation is in
progress. Linux-only; excluded from Windows builds.

Here, **user-mode stack** means TCP/IP protocol processing that substitutes for
the kernel TCP/IP stack on selected traffic. It may still use OS packet I/O.
Socket wrappers, event loops and buffer pools alone do not provide that stack.
BDE supplies general utilities; the NTF socket APIs below use OS networking.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE/NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td>Planned user-mode stack:<br><a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh"><code>Zutcp::Stack</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh"><code>StackParams</code></a><br>➕ user-mode TCP/IP stack over <a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh">Zxdp</a>, integrated with <a href="https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh">ZiMultiplex</a> and ordinary <a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/Zutcp.hh">Zi</a> application interfaces<br>➕ protocol Rx/Tx work runs on the mx owners<br>➖ requires a configured interface and raw packet privileges</td>
  <td>No TCP/IP stack.</td>
  <td>No user-mode stack; <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/basics.html"><code>Boost.Asio</code></a> wraps OS sockets.</td>
  <td>No user-mode stack; <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h"><code>folly::AsyncSocket</code></a> wraps OS sockets.</td>
  <td>No user-mode stack; <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a> wraps OS sockets.</td>
  <td>User-mode TCP/IP: <a href="https://docs.rs/smoltcp/latest/smoltcp/"><code>smoltcp</code></a>; device integration required.</td>
</tr>
<tr valign="top">
  <td>Planned user-mode packet storage:<br><a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpAlloc.hh"><code>Zutcp::Allocator</code></a> and packet geometry<br>➕ <a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/Zutcp.hh">Zi</a> providers over caller-owned <a href="https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh">Zxdp</a> pools, with in-place wire construction<br>➕ separate control capacity and bounded ingress retention</td>
  <td>No TCP/IP stack.</td>
  <td>Buffer utilities only; <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/basics.html"><code>Boost.Asio</code></a> supplies no user-mode stack.</td>
  <td>Buffer utility only: <a href="https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h"><code>folly::IOBuf</code></a>; no TCP/IP stack.</td>
  <td>Buffer pool only: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h"><code>ntci::DataPool</code></a>; no TCP/IP stack.</td>
  <td>User-mode stack packet I/O: <a href="https://docs.rs/smoltcp/latest/smoltcp/phy/trait.Device.html"><code>smoltcp::phy::Device</code></a>; pool policy differs.</td>
</tr>
</tbody>
</table>

## TCP connection and recovery

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE/NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td>Planned user-mode TCP state:<br><a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpConnection.hh"><code>Zutcp::Connection</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpCliConnection.hh"><code>CliConnection</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpSrvConnection.hh"><code>SrvConnection</code></a><br>➕ one connection owns ordered bytes, reassembly, credit, retained Tx data and close<br>➕ bounded pending/live admission and closed-tuple history</td>
  <td>No TCP/IP stack.</td>
  <td>OS TCP connection API: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/basics.html"><code>Boost.Asio</code></a>; protocol state lives in the kernel.</td>
  <td>OS TCP connection API: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h"><code>folly::AsyncSocket</code></a>; protocol state lives in the kernel.</td>
  <td>OS TCP connection API: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a>; protocol state lives in the kernel.</td>
  <td>User-mode TCP state: <a href="https://docs.rs/smoltcp/latest/smoltcp/socket/tcp/struct.Socket.html"><code>smoltcp::socket::tcp::Socket</code></a>; admission/retention policy differs.</td>
</tr>
<tr valign="top">
  <td>Planned user-mode TCP recovery:<br><a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh">TCP ACK/window/recovery facilities</a><br>➕ cumulative ACKs, optional SACK/DSACK, scaling and timestamps/PAWS<br>➕ RACK-TLP, RTO/persist and ECN/NewReno on explicit owners</td>
  <td>No TCP/IP stack.</td>
  <td>Kernel-provided TCP; <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/basics.html"><code>Boost.Asio</code></a> does not implement recovery.</td>
  <td>Kernel-provided TCP; <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h"><code>folly::AsyncSocket</code></a> does not implement recovery.</td>
  <td>Kernel-provided TCP; <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a> does not implement recovery.</td>
  <td>User-mode TCP recovery: <a href="https://docs.rs/smoltcp/latest/smoltcp/socket/tcp/struct.Socket.html"><code>smoltcp::socket::tcp::Socket</code></a>; partial feature match.</td>
</tr>
<tr valign="top">
  <td>Planned user-mode ISN/Fast Open:<br><a href="https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh">TCP initial sequences and Fast Open</a><br>➕ secure keyed ISNs and bounded cookie state independent of TLS<br>➕ default-off, application-authorized early data with ordinary fallback</td>
  <td>No TCP/IP stack.</td>
  <td>Kernel TCP policy via OS-specific options; <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/basics.html"><code>Boost.Asio</code></a> supplies no user-mode implementation.</td>
  <td>Kernel TCP policy via OS socket options; <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h"><code>folly::AsyncSocket</code></a> supplies no user-mode implementation.</td>
  <td>Kernel TCP policy; <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a> supplies no user-mode implementation.</td>
  <td>User-mode TCP: <a href="https://docs.rs/smoltcp/latest/smoltcp/socket/tcp/struct.Socket.html"><code>smoltcp::socket::tcp::Socket</code></a>; no matching Fast Open contract.</td>
</tr>
</tbody>
</table>

# Zv - Framework server-side utilities

## Thread, stack and ring parameter binding

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zv/src/), [module guide](../zv/README.md).
PropertyTree and ProgramOptions comparisons refer to their configuration
storage/binding roles, not syntax-compatible parsers. Boost's
[library index](https://www.boost.org/doc/libs/latest/libs/libraries.htm)
lists those facilities and the other Boost components referenced above.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvThreadParams.hh#L35"><code>ZvThreadParams</code></a><br>➕ validated tree-to-thread settings with default preservation</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/thread.html">Thread</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadattributes.h"><code>bslmt::ThreadAttributes</code></a></td>
  <td>Partial: <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a>, <a href="https://doc.rust-lang.org/std/thread/struct.Builder.html"><code>thread::Builder</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvStackParams.hh#L25"><code>ZvStackParams</code></a><br>➕ binds stack sizing/configuration while retaining defaults</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a>, <a href="https://doc.rust-lang.org/std/thread/struct.Builder.html#method.stack_size"><code>Builder::stack_size</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvRingParams.hh#L35"><code>ZvRingParams</code></a><br>➕ binds configuration to shared-memory ring settings</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/interprocess.html">Interprocess</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a>, <a href="https://docs.rs/ringbuf/latest/ringbuf/"><code>ringbuf</code></a>, <a href="https://docs.rs/shared_memory/latest/shared_memory/"><code>shared_memory</code></a></td>
</tr>
</tbody>
</table>

## Connection and multiplexer parameter binding

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvMxParams.hh#L77"><code>ZvCxnOptions</code></a><br>➕ connection flags, TTL and multicast binding</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a>, <a href="https://docs.rs/socket2/latest/socket2/"><code>socket2</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvMxParams.hh#L112"><code>ZvMxParams</code></a><br>➕ binds scheduler threads, destinations and multiplexer settings</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a>, <a href="https://docs.rs/tokio/latest/tokio/runtime/struct.Builder.html"><code>runtime::Builder</code></a></td>
</tr>
</tbody>
</table>

## Configuration file loading and saving

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvCf.hh"><code>ZvCf</code></a><br>➕ native configuration files with includes, environment expansion and typed binding<br>➖ save/update/delete outputs are not transactional in-place file edits</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/program_options.html">Boost.ProgramOptions</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/property_tree.html">PropertyTree</a>, <a href="https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html">Filesystem</a></td>
  <td><a href="https://docs.rs/config/latest/config/"><code>config</code></a> layered files/environment with typed deserialization; syntax and save/update semantics differ</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvTOML.hh"><code>ZvTOML</code></a><br>➕ file I/O for reflected TOML and its owned tree</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://doc.rust-lang.org/std/fs/fn.read_to_string.html"><code>fs::read_to_string</code></a> / <a href="https://doc.rust-lang.org/std/fs/fn.write.html"><code>fs::write</code></a> + <a href="https://docs.rs/toml/latest/toml/"><code>toml</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zv/src/ZvYAML.hh"><code>ZvYAML</code></a><br>➕ file I/O for <a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh">Zf</a>'s reflected YAML subset</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/fs/fn.read_to_string.html"><code>fs::read_to_string</code></a>, <a href="https://doc.rust-lang.org/std/fs/fn.write.html"><code>fs::write</code></a>, <a href="https://docs.rs/serde-saphyr/latest/serde_saphyr/"><code>serde-saphyr</code></a></td>
</tr>
</tbody>
</table>

# Zpb - Protobuf implementation

## Protobuf wire format and typed messages

[Public API](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh) · [Module guide](../zpb/README.md)

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh"><code>Zpb</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh"><code>PB</code></a> facet<br>➕ protobuf wire mapping from <a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfStruct.hh">ZfStruct</a> metadata without canonical generated C++ headers<br>➖ unknown fields are skipped rather than retained</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Boost.Serialization</a></td>
  <td>Partial: <a href="https://docs.rs/prost/latest/prost/trait.Message.html"><code>prost::Message</code></a> (derive-based mapping)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh"><code>Zpb::Builder</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh"><code>Zpb::IOBuilder</code></a><br>➕ bounded output into caller storage or pooled I/O buffers</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Boost.Serialization</a>, <a href="https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html">Iostreams</a></td>
  <td>Partial: <a href="https://docs.rs/prost/latest/prost/trait.Message.html#method.encode"><code>Message::encode</code></a>, <a href="https://docs.rs/bytes/latest/bytes/buf/trait.BufMut.html"><code>BufMut</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh#L1824"><code>Zpb::verify</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh#L1834"><code>handler</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh#L1844"><code>load</code></a><br>➕ separate wire verification, borrowed access and typed construction<br>➖ borrowed results require retained backing</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/prost/latest/prost/trait.Message.html#method.decode"><code>prost::Message decoding</code></a> (decode validates; no borrowed-view API)</td>
</tr>
</tbody>
</table>

## Schema reflection and compiler

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/ZpbReflect.hh"><code>Zpb::Reflect</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/ZpbReflect.hh"><code>ZpbReflect</code></a><code>.hh</code>)<br>➕ protobuf schema/descriptor representation integrated with Z metadata</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a>, <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td><a href="https://docs.rs/prost-reflect/latest/prost_reflect/"><code>prost-reflect</code></a> descriptors and dynamic messages</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/zprotoc.cc"><code>zprotoc</code></a><br>➕ compiles supported protobuf declarations into Z metadata<br>➖ supported declaration profile is narrower than the complete protobuf ecosystem</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/prost-build/latest/prost_build/"><code>prost-build</code></a>, <a href="https://docs.rs/protobuf-codegen/latest/protobuf_codegen/"><code>protobuf-codegen</code></a></td>
</tr>
</tbody>
</table>

# Zfb - Flatbuffers

## FlatBuffers construction and access

[Public API](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh) · [Module guide](../zfb/README.md)

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh"><code>Zfb</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh"><code>FB</code></a> facet<br>➕ canonical FlatBuffers mapping from reflected C++ types without canonical generated headers</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Boost.Serialization</a></td>
  <td>Partial: <a href="https://docs.rs/flatbuffers/latest/flatbuffers/"><code>flatbuffers</code></a> (generated bindings)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh"><code>Zfb::Builder</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh"><code>Zfb::IOBuilder</code></a><br>➕ downward arena construction and direct pooled I/O output</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/container.html">Boost.Container</a></td>
  <td>Partial: <a href="https://docs.rs/flatbuffers/latest/flatbuffers/struct.FlatBufferBuilder.html"><code>FlatBufferBuilder</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L1667"><code>Zfb::view</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L2738"><code>handler</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L2799"><code>load</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L2730"><code>verify</code></a><br>➕ borrowed table/vector access, typed construction and structural verification<br>➖ views depend on wire-buffer lifetime</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/flatbuffers/latest/flatbuffers/fn.root.html"><code>flatbuffers::root</code></a> verifies and returns borrowed accessors; native-object unpacking is generated</td>
</tr>
</tbody>
</table>

## Schema reflection and compiler

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/ZfbReflect.hh"><code>Zfb::Reflect</code></a> (<a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/ZfbReflect.hh"><code>ZfbReflect</code></a><code>.hh</code>)<br>➕ emits and consumes BFBS/FBS schema metadata from reflected types</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a>, <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td><a href="https://docs.rs/flatbuffers-reflection/latest/flatbuffers_reflection/"><code>flatbuffers-reflection</code></a> BFBS schema reflection</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zfb/src/zflatc.cc"><code>zflatc</code></a><br>➕ FlatBuffers schema tooling for Z types</td>
  <td>N/A</td>
  <td><a href="https://flatbuffers.dev/languages/rust/"><code>flatc</code> Rust code generation</a>; schema compiler is external to Cargo</td>
</tr>
</tbody>
</table>

# Zdbus - D-Bus clients, servers and typed message routing

## Addresses, authentication and framing

[Public headers](https://github.com/kaldron-labs/z/blob/main/zdbus/src/) · [Module guide](../zdbus/README.md).
Used by the Linux [Ztls](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh) credential Vault; native message bodies use [`ZfDBUS`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfDBUS.hh).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusAddress.hh"><code>ZdbusAddress</code></a><br>➕ decodes D-Bus unix addresses into filesystem/abstract socket endpoints</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://docs.rs/zbus/latest/zbus/address/index.html"><code>zbus::address</code></a></td>
</tr>
<tr valign="top">
  <td>D-Bus EXTERNAL authentication (<a href="https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusAuth.hh"><code>ZdbusAuth</code></a><code>.hh</code>)<br>➕ incremental client authentication integrated with connection setup</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/zbus/latest/zbus/connection/struct.Builder.html"><code>zbus::connection::Builder</code></a></td>
</tr>
<tr valign="top">
  <td>D-Bus envelopes and pooled messages (<a href="https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusEnvelope.hh"><code>ZdbusEnvelope</code></a><code>.hh</code>, <a href="https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusMessage.hh"><code>ZdbusMessage</code></a><code>.hh</code>)<br>➕ bounded framing, header validation and direct pooled construction</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/zbus/latest/zbus/message/index.html"><code>zbus::message</code></a>, <a href="https://docs.rs/zvariant/latest/zvariant/"><code>zvariant</code></a></td>
</tr>
</tbody>
</table>

## Typed message adapters and runtime

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td>Request/return/error/signal adapters and catalogs<br>➕ routing headers and typed <a href="https://github.com/kaldron-labs/z/blob/main/zf/src/ZfDBUS.hh">ZfDBUS</a> bodies share application declarations</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a></td>
  <td><a href="https://docs.rs/zbus/latest/zbus/attr.interface.html"><code>zbus::interface</code></a> / <a href="https://docs.rs/zbus/latest/zbus/attr.proxy.html"><code>proxy</code></a> + <a href="https://docs.rs/zvariant/latest/zvariant/"><code>zvariant</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusClient.hh"><code>ZdbusClient</code></a><br>➕ asynchronous calls, serial correlation, named errors and signal subscriptions<br>➖ Linux-only module</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://docs.rs/zbus/latest/zbus/connection/struct.Connection.html"><code>zbus::Connection</code></a> / <a href="https://docs.rs/zbus/latest/zbus/proxy/struct.Proxy.html"><code>Proxy</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusServer.hh"><code>ZdbusServer</code></a><br>➕ service-side method routes and typed returns/errors over the shared bus client<br>➖ Linux-only module</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://docs.rs/zbus/latest/zbus/object_server/struct.ObjectServer.html"><code>zbus::ObjectServer</code></a> / <a href="https://docs.rs/zbus/latest/zbus/attr.interface.html"><code>interface</code></a></td>
</tr>
</tbody>
</table>

# Ztcp - Asynchronous TCP links

## Buffer policies and byte streams

[Public API](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh) · [Module guide](../ztcp/README.md)

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>Ztcp::RxBufAlloc</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>TxBufAlloc</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>RxStream</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>TxStream</code></a><br>➕ pooled, reference-owned input/output with explicit allocator roles<br>➖ applications supply message framing</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h"><code>folly::IOBuf / IOBufQueue</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h"><code>ntci::DataPool</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>StreamSocket</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/net/struct.TcpStream.html"><code>TcpStream</code></a>, <a href="https://docs.rs/bytes/latest/bytes/"><code>bytes</code></a>, <a href="https://docs.rs/tokio-util/latest/tokio_util/codec/index.html"><code>codec</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>Ztcp::HubParams</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>ClientParams</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>ServerParams</code></a><br>➕ named Rx/Tx destinations and allocator configuration shared by links</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/EventBase.h"><code>folly::EventBase + AsyncSocket options</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_streamsocketoptions.h"><code>ntca::StreamSocketOptions</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_listenersocketoptions.h"><code>ListenerSocketOptions</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/runtime/struct.Builder.html"><code>runtime::Builder</code></a>, <a href="https://docs.rs/socket2/latest/socket2/"><code>socket2</code></a></td>
</tr>
</tbody>
</table>

## TCP links and endpoints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>Ztcp::CliLink</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>SrvLink</code></a><br>➕ CRTP connection callbacks, queued writes and reconnect integration</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a> <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/tcp.hpp"><code>ip::tcp::socket</code></a> with composed connection/read/write operations</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h"><code>folly::AsyncSocket</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocketsession.h"><code>StreamSocketSession</code></a></td>
  <td><a href="https://docs.rs/tokio/latest/tokio/net/struct.TcpStream.html"><code>TcpStream</code></a> + <a href="https://docs.rs/tokio-util/latest/tokio_util/codec/struct.Framed.html"><code>Framed</code></a>; reconnect/callback policy is application-owned</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>Ztcp::Client</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh"><code>Server</code></a><br>➕ hub-managed link/listener lifecycle and restart after drain</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncServerSocket.h"><code>folly::AsyncSocket / AsyncServerSocket</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_listenersocket.h"><code>ntci::ListenerSocket</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>StreamSocket</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/net/struct.TcpListener.html"><code>TcpListener</code></a>, <a href="https://docs.rs/tokio/latest/tokio/net/struct.TcpStream.html"><code>TcpStream</code></a></td>
</tr>
</tbody>
</table>

# Ztls - TLS transport, cryptography and credential storage

## Cryptographic primitives

[Public headers](https://github.com/kaldron-labs/z/blob/main/ztls/src/) · [Module guide](../ztls/README.md).
TLS and crypto use zpicotls/OpenSSL; backend requirements are in
[dependencies](DEPENDENCIES.md). Hash/MAC comparisons use
[Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsRandom.hh"><code>Ztls::Random</code></a><br>➕ crypto-provider random bytes directly into a caller span</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/numeric/random/random_device"><code>std::random_device</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_random.html">Boost.Random</a>, <a href="https://github.com/boostorg/random/blob/master/include/boost/random/random_device.hpp"><code>random_device</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/Random.h"><code>folly::Random::secureRandom</code></a></td>
  <td><a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_randomdevice.h"><code>bdlb::RandomDevice::getRandomBytes</code></a></td>
  <td><a href="https://docs.rs/getrandom/latest/getrandom/"><code>getrandom</code></a> / <a href="https://docs.rs/rand_core/latest/rand_core/trait.CryptoRng.html"><code>CryptoRng</code></a> sources</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsSec.hh#L23"><code>Ztls::ctEqual</code></a><br>➕ security-oriented constant-time byte comparison</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/subtle/latest/subtle/trait.ConstantTimeEq.html"><code>ConstantTimeEq</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsSec.hh#L34"><code>Ztls::secretHash</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsSec.hh#L35"><code>secretVerify</code></a><br>➕ versioned salted secret-hash representation and verification</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html">Boost.Hash2</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/argon2/latest/argon2/"><code>argon2</code></a>, <a href="https://docs.rs/password-hash/latest/password_hash/"><code>password-hash</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsMD.hh"><code>Ztls::MD</code></a><br>➕ incremental SHA digests through the configured crypto backend</td>
  <td>None: <a href="https://en.cppreference.com/cpp/utility/hash"><code>std::hash</code></a></td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html">Boost.Hash2</a> SHA algorithms<br>➕ also offers other hash families</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLHash.h"><code>folly::ssl::OpenSSLHash::Digest</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_sha2.h"><code>bdlde::Sha256</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_sha2.h"><code>Sha512</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_sha1.h"><code>Sha1</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_md5.h"><code>Md5</code></a></td>
  <td><a href="https://docs.rs/sha2/latest/sha2/"><code>sha2</code></a> / <a href="https://docs.rs/digest/latest/digest/trait.Digest.html"><code>Digest</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsHMAC.hh"><code>Ztls::HMAC</code></a><br>➕ backend MAC state with span input/output and reset</td>
  <td>N/A</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html">Boost.Hash2</a> <a href="https://github.com/boostorg/hash2/blob/master/include/boost/hash2/hmac.hpp"><code>hmac</code></a></td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLHash.h"><code>folly::ssl::OpenSSLHash::Hmac</code></a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/hmac/latest/hmac/"><code>hmac</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsTOTP.hh"><code>Ztls::TOTP</code></a><br>➕ time-based code calculation and bounded-window verification</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html">Boost.Hash2</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/totp-rs/latest/totp_rs/"><code>totp-rs</code></a></td>
</tr>
</tbody>
</table>

## Keys, encapsulation and encrypted files

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsPK.hh"><code>Ztls::PK</code></a><br>➕ RSA/EC/Ed25519/X25519 key loading, generation and applicable signing/verification operations</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLKeyUtils.h"><code>folly::ssl::OpenSSLKeyUtils</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptionkey.h"><code>ntci::EncryptionKey</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptiondriver.h"><code>EncryptionDriver</code></a></td>
  <td><a href="https://docs.rs/openssl/latest/openssl/pkey/struct.PKey.html"><code>openssl::pkey::PKey</code></a>; <a href="https://docs.rs/rsa/latest/rsa/"><code>rsa</code></a> / <a href="https://docs.rs/p256/latest/p256/"><code>p256</code></a> / <a href="https://docs.rs/ed25519-dalek/latest/ed25519_dalek/"><code>ed25519-dalek</code></a> / <a href="https://docs.rs/x25519-dalek/latest/x25519_dalek/"><code>x25519-dalek</code></a> for algorithm-specific native crates</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsPK.hh"><code>Ztls::PK</code></a> ML-KEM-768 and ML-KEM-768–X25519 keys<br>➕ encapsulation/decapsulation and key serialization integrated with Z buffers</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/ml-kem/latest/ml_kem/"><code>ml-kem</code></a>, <a href="https://docs.rs/x-wing/latest/x_wing/"><code>x-wing</code></a> (check hybrid suite compatibility)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsCOSE.hh"><code>Ztls::COSE</code></a><br>➕ COSE key representations bound to native public/private key types</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/coset/latest/coset/"><code>coset</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsHPKE.hh"><code>Ztls::HPKE</code></a><br>➕ base-mode hybrid ML-KEM-768–X25519 encryption with caller buffers</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/hpke/latest/hpke/"><code>hpke</code></a>, <a href="https://docs.rs/hpke-rs/latest/hpke_rs/"><code>hpke-rs</code></a> (check hybrid suite compatibility)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsAge.hh"><code>ZtlsAge</code></a><br>➕ age-format file encryption/decryption and recipient/identity parsing</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/age/latest/age/"><code>age</code></a> age-format encryption/decryption</td>
</tr>
</tbody>
</table>

## Credential storage

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsVault.hh"><code>Ztls::Vault</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsVault.hh"><code>VaultConfig</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsVaultStore.hh"><code>VaultStore</code></a><br>➕ scoped credential names with ephemeral/file/keyring/module stores<br>➕ Linux Secret Service and Windows platform integration</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/keyring/latest/keyring/"><code>keyring</code></a>, <a href="https://docs.rs/secrecy/latest/secrecy/"><code>secrecy</code></a></td>
</tr>
</tbody>
</table>

## TLS byte streams and endpoints

TLS counterpart: [Boost.Asio SSL](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/ssl.html).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>Ztls::RxBufAlloc</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>TxBufAlloc</code></a> and TLS packet allocation<br>➕ separate plaintext/ciphertext ownership and serial record encryption into packet spans</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h"><code>folly::AsyncSSLSocket with IOBuf output</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryption.h"><code>ntci::Encryption</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h"><code>DataPool</code></a></td>
  <td>Partial: <a href="https://docs.rs/rustls/latest/rustls/"><code>rustls</code></a>, <a href="https://docs.rs/tokio-rustls/latest/tokio_rustls/"><code>tokio-rustls</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>Ztls::HubParams</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>ClientParams</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>ServerParams</code></a><br>➕ trust, keys, ALPN, allocator roles and Rx/Tx destinations in one setup contract</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/ssl/context.hpp"><code>ssl::context</code></a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/SSLContext.h"><code>folly::SSLContext + EventBase</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_encryptionclientoptions.h"><code>ntca::EncryptionClientOptions</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_encryptionserveroptions.h"><code>EncryptionServerOptions</code></a></td>
  <td><a href="https://docs.rs/rustls/latest/rustls/client/struct.ClientConfig.html"><code>ClientConfig</code></a> / <a href="https://docs.rs/rustls/latest/rustls/server/struct.ServerConfig.html"><code>ServerConfig</code></a>; transport allocation/sharding configuration is separate</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>Ztls::CliLink</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>SrvLink</code></a><br>➕ handshake/session callbacks integrated with pooled transport and shard ownership</td>
  <td><a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a> <a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/ssl/stream.hpp"><code>ssl::stream</code></a><code>&lt;</code><a href="https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/tcp.hpp"><code>ip::tcp::socket</code></a><code>&gt;</code><br>➕ standard <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a> asynchronous-stream composition</td>
  <td><a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h"><code>folly::AsyncSSLSocket</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>ntci::StreamSocket::upgrade</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio-rustls/latest/tokio_rustls/"><code>tokio-rustls</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>Ztls::Client</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh"><code>Server</code></a><br>➕ managed TLS link/listener startup, drain and restart</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h"><code>folly::AsyncSSLSocket / AsyncServerSocket</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptiondriver.h"><code>ntci::EncryptionDriver</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h"><code>StreamSocket</code></a>, <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_listenersocket.h"><code>ListenerSocket</code></a></td>
  <td><a href="https://docs.rs/tokio-rustls/latest/tokio_rustls/client/struct.TlsConnector.html"><code>TlsConnector</code></a> / <a href="https://docs.rs/tokio-rustls/latest/tokio_rustls/server/struct.TlsAcceptor.html"><code>TlsAcceptor</code></a>; listener restart/drain is application-owned</td>
</tr>
</tbody>
</table>

# Zquic - QUIC transport

## Packet, frame and crypto codecs

[Public headers](https://github.com/kaldron-labs/z/blob/main/zquic/src/) · [Module guide](../zquic/README.md).
QUIC v1 is the mainstream deployment baseline; quic-go still prefers it as the
[most commonly deployed version](https://quic-go.net/docs/quic/transport/).
V2 adds no application capabilities and does not deprecate v1
([RFC 9369](https://www.rfc-editor.org/rfc/rfc9369.html)).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh">QUIC packet/frame/transport-parameter codecs</a><br>➕ native wire parsing and output integrated with Z buffer ownership</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://docs.rs/quinn-proto/latest/quinn_proto/"><code>quinn-proto</code></a> / <a href="https://docs.rs/quiche/latest/quiche/"><code>quiche</code></a> low-level QUIC protocol facilities</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh"><code>Zquic::AddressToken</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh"><code>StatelessReset</code></a>, connection-ID/bootstrap facilities<br>➕ Retry/address-validation and connection routing machinery</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/quinn-proto/latest/quinn_proto/"><code>quinn-proto</code></a> token, reset and connection-ID handling; storage/bootstrap policies differ</td>
</tr>
<tr valign="top">
  <td>QUIC crypto (<a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicCrypto.hh"><code>ZquicCrypto</code></a><code>.hh</code>)<br>➕ zpicotls handshake state, packet protection and key retirement</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/quinn-proto/latest/quinn_proto/crypto/index.html"><code>quinn_proto::crypto</code></a></td>
</tr>
</tbody>
</table>

## Recovery, paths and stream ownership

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td>ACK/loss recovery, <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicRecovery.hh"><code>RttEstimator</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicRecovery.hh"><code>NewReno</code></a>, PTO and retransmission queues<br>➕ packet-number-space recovery with retained frame ownership</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://docs.rs/quinn-proto/latest/quinn_proto/congestion/index.html"><code>quinn_proto::congestion</code></a> / <a href="https://docs.rs/quiche/latest/quiche/"><code>quiche</code></a> recovery; exact algorithms and queue policy differ</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicPath.hh"><code>Zquic::Path</code></a> and link migration<br>➕ path validation, anti-amplification, PMTU/ECN and single-active-path migration</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/quinn-proto/latest/quinn_proto/struct.Connection.html"><code>quinn_proto::Connection</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh">QUIC buffer/sequence-queue policies</a><br>➕ separate packet, CRYPTO and stream retention/reassembly contracts</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/intrusive.html">Boost.Intrusive</a>, <a href="https://github.com/boostorg/icl/blob/master/include/boost/icl/interval_set.hpp">ICL</a></td>
  <td>Partial: <a href="https://docs.rs/quinn-proto/latest/quinn_proto/"><code>quinn-proto</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicStream.hh"><code>Zquic::CliStream</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicStream.hh"><code>SrvStream</code></a><br>➕ multiplexed streams with independent directional retirement, FIN/reset/stop and flow control</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/quinn/latest/quinn/struct.SendStream.html"><code>quinn::SendStream</code></a> / <a href="https://docs.rs/quinn/latest/quinn/struct.RecvStream.html"><code>RecvStream</code></a></td>
</tr>
</tbody>
</table>

## Transport endpoints and diagnostics

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh"><code>Zquic::HubParams</code></a><br>➕ unified TLS, ALPN, transport, migration and instrumentation configuration</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/quinn/latest/quinn/struct.TransportConfig.html"><code>TransportConfig</code></a> / <a href="https://docs.rs/quinn/latest/quinn/struct.ClientConfig.html"><code>ClientConfig</code></a> / <a href="https://docs.rs/quinn/latest/quinn/struct.ServerConfig.html"><code>ServerConfig</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicCliLink.hh"><code>Zquic::CliLink</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicSrvLink.hh"><code>SrvLink</code></a><br>➕ shared connection runtime, stream scheduling and migration callbacks</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/quinn/latest/quinn/struct.Connection.html"><code>quinn::Connection</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh"><code>Zquic::Client</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh"><code>Server</code></a><br>➕ QUIC connection bootstrap and UDP listener lifecycle over <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh">Zi</a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://docs.rs/quinn/latest/quinn/struct.Endpoint.html"><code>quinn::Endpoint</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicLog.hh"><code>ZquicLogger</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicLog.hh"><code>ZquicLogSink</code></a>, transport diagnostics<br>➕ qlog JSON-SEQ events with bounded queues and controlled drain/restart</td>
  <td>Partial: <a href="https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp">Boost.Log</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>Partial: <a href="https://docs.rs/qlog/latest/qlog/"><code>qlog</code></a>, <a href="https://docs.rs/quiche/latest/quiche/"><code>quiche</code></a></td>
</tr>
</tbody>
</table>

# Zhttp - HTTP/1.1, HTTP/2 and HTTP/3

## URLs, fields and protocol configuration

[Public headers](https://github.com/kaldron-labs/z/blob/main/zhttp/src/) · [Module guide](../zhttp/README.md).
[Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)
supplies HTTP/1 and WebSocket facilities; [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh) also owns HTTP/2 and HTTP/3.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpURL.hh"><code>Zhttp::URLView</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpURL.hh"><code>URL</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpURL.hh"><code>Target</code></a><br>➕ explicit borrowed/owning HTTP URL and request-target representations<br>➖ borrowed parsing normalizes mutable input</td>
  <td><a href="https://github.com/boostorg/url/blob/master/include/boost/url.hpp">Boost.URL</a> <a href="https://github.com/boostorg/url/blob/master/include/boost/url/url_view.hpp"><code>url_view</code></a> / <a href="https://github.com/boostorg/url/blob/master/include/boost/url/url.hpp"><code>url</code></a><br>➕ general URI component model</td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Uri.h"><code>folly::Uri</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h"><code>ntsa::Uri</code></a></td>
  <td>Partial: <a href="https://docs.rs/url/latest/url/struct.Url.html"><code>url::Url</code></a>, <a href="https://docs.rs/http/latest/http/uri/struct.Uri.html"><code>http::Uri</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpCore.hh#L58"><code>ZhttpHeaders</code></a>, field catalogs and matchers<br>➕ compile-time name/value dispatch and shared header semantics across wire versions</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/http/latest/http/header/index.html"><code>http::header</code></a>, <a href="https://docs.rs/headers/latest/headers/"><code>headers</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh"><code>Zhttp::Profiles</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh"><code>H1TCP</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh"><code>H1TLS</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh"><code>H2TLS</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh"><code>H3QUIC</code></a><br>➕ explicit compile-time selection of supported transport/protocol combinations</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/hyper/latest/hyper/"><code>hyper</code></a>, <a href="https://docs.rs/h3/latest/h3/"><code>h3</code></a>, <a href="https://docs.rs/quinn/latest/quinn/"><code>quinn</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpConfig.hh"><code>Zhttp::HubConfig</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpConfig.hh"><code>Config</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpConfig.hh"><code>ServerConfig</code></a> and transport configs<br>➕ shared connection, body, queue, protocol and allocator policy</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/reqwest/latest/reqwest/struct.ClientBuilder.html"><code>reqwest::ClientBuilder</code></a>, <a href="https://docs.rs/hyper-util/latest/hyper_util/"><code>hyper-util</code></a>, <a href="https://docs.rs/quinn/latest/quinn/"><code>quinn</code></a></td>
</tr>
</tbody>
</table>

## Discovery and event framing

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpDiscovery.hh"><code>Zhttp::DiscoveryRequest</code></a>, endpoint selection and <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpDiscovery.hh"><code>AltSvcCache</code></a><br>➕ HTTPS/SVCB and Alt-Svc routing integrated with client protocol selection</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/hickory-resolver/latest/hickory_resolver/"><code>hickory-resolver</code></a>, <a href="https://docs.rs/reqwest/latest/reqwest/"><code>reqwest</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpSSE.hh"><code>Zhttp::SSEDecoder</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpSSE.hh"><code>SSEEvent</code></a><br>➕ incremental server-sent event framing and bounded event storage</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td><a href="https://docs.rs/eventsource-stream/latest/eventsource_stream/"><code>eventsource-stream</code></a> incremental SSE parsing</td>
</tr>
</tbody>
</table>

## Messages, body streams and response routes

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh"><code>Zhttp::Parser</code></a>, request/response Builders and header catalogs<br>➕ one typed application interface for H1/H2/H3 parsing and production</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a> HTTP parsers, serializers and Body models<br>➖ HTTP/1 vocabulary rather than a shared H1/H2/H3 interface</td>
  <td><a href="https://docs.rs/http/latest/http/request/struct.Request.html"><code>Request</code></a> / <a href="https://docs.rs/http/latest/http/response/struct.Response.html"><code>Response</code></a> + <a href="https://docs.rs/hyper/latest/hyper/"><code>hyper</code></a> / <a href="https://docs.rs/h3/latest/h3/"><code>h3</code></a>; typed catalogs are separate</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpCore.hh"><code>Zhttp::BodyEmitter</code></a> and body policies<br>➕ bounded fixed/streaming production, cancellation and H2/H3 trailers<br>➖ application owns body decompression</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td><a href="https://docs.rs/http-body/latest/http_body/trait.Body.html"><code>http_body::Body</code></a> / <a href="https://docs.rs/http-body-util/latest/http_body_util/"><code>http-body-util</code></a>; byte budgets/cancellation are application policy</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpFields.hh"><code>Zhttp::Channel</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpServer.hh"><code>Route</code></a>, response queues<br>➕ owning deferred replies with H1 ordering and logical-stream cancellation</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>Partial: <a href="https://docs.rs/axum/latest/axum/response/trait.IntoResponse.html"><code>IntoResponse</code></a>, <a href="https://docs.rs/hyper/latest/hyper/service/index.html"><code>hyper services</code></a></td>
</tr>
</tbody>
</table>

## HTTP wire protocols and endpoints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td>HTTP/1.1 framing (<a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh"><code>Zhttp</code></a>)<br>➕ integrated request/response streaming and pooled transport</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Boost.Beast</a> HTTP/1</td>
  <td><a href="https://docs.rs/hyper/latest/hyper/client/conn/http1/index.html"><code>hyper HTTP/1</code></a> / <a href="https://docs.rs/httparse/latest/httparse/"><code>httparse</code></a> for standalone parsing</td>
</tr>
<tr valign="top">
  <td>HTTP/2 and HPACK (<a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh"><code>Zhttp</code></a>)<br>➕ multiplexed streams, flow control and header compression over TLS</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td><a href="https://docs.rs/h2/latest/h2/"><code>h2</code></a> / <a href="https://docs.rs/hyper/latest/hyper/client/conn/http2/index.html"><code>hyper HTTP/2</code></a></td>
</tr>
<tr valign="top">
  <td>HTTP/3 and QPACK (<a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh"><code>Zhttp</code></a>)<br>➕ HTTP messages, control streams and compression over <a href="https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicSrvLink.hh">Zquic</a><br>➖ nonzero blocked-stream buffering is outside the current QPACK profile</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>Partial: <a href="https://docs.rs/h3/latest/h3/"><code>h3</code></a>, <a href="https://docs.rs/h3-quinn/latest/h3_quinn/"><code>h3-quinn</code></a>, <a href="https://docs.rs/quiche/latest/quiche/"><code>quiche</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh"><code>Zhttp::Pool</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpClient.hh"><code>Client</code></a><br>➕ pooled destinations with independent link/request concurrency, retry and protocol fallback</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>Partial: <a href="https://docs.rs/reqwest/latest/reqwest/struct.Client.html"><code>reqwest::Client</code></a>, <a href="https://docs.rs/hyper-util/latest/hyper_util/client/legacy/struct.Client.html"><code>hyper_util::client::legacy::Client</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpServer.hh"><code>Zhttp::Server</code></a><br>➕ typed admission, listeners, sessions and graceful drain across selected HTTP profiles</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>Partial: <a href="https://docs.rs/axum/latest/axum/struct.Router.html"><code>axum::Router</code></a>, <a href="https://docs.rs/hyper/latest/hyper/"><code>hyper</code></a>, <a href="https://docs.rs/h3/latest/h3/"><code>h3</code></a></td>
</tr>
</tbody>
</table>

# Zws - Websockets

## URI, handshake and frame codecs

[Public headers](https://github.com/kaldron-labs/z/blob/main/zws/src/) · [Module guide](../zws/README.md).
WebSocket counterpart: [Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket.html).
[Zws](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsH1.hh) supplies the WebSocket dependency of [Zjrpc](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh) and [Zmcp](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh).

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">NTF&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsURI.hh"><code>Zws::URI</code></a><br>➕ ws/wss URI interpretation for native profile setup</td>
  <td>Partial: <a href="https://github.com/boostorg/url/blob/master/include/boost/url.hpp">Boost.URL</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/Uri.h"><code>folly::Uri</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h"><code>ntsa::Uri</code></a></td>
  <td><a href="https://docs.rs/http/latest/http/uri/struct.Uri.html"><code>http::Uri</code></a> / <a href="https://docs.rs/url/latest/url/struct.Url.html"><code>url::Url</code></a> with WebSocket client configuration</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zws/src/Zws.hh">Opening handshake, H1 Upgrade and H2/H3 Extended CONNECT</a><br>➕ binds the same WebSocket application to concrete HTTP profiles</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a> WebSocket handshake<br>➖ H2/H3 Extended CONNECT requires another HTTP stack</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/tungstenite/latest/tungstenite/handshake/index.html"><code>tungstenite::handshake</code></a> (H1; custom H2/H3 integration)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsCodec.hh"><code>Zws::Codec</code></a>, protocol frame/header utilities<br>➕ transport-independent masking, fragmentation and control-frame processing</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a> WebSocket frame/message operations</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/tungstenite/latest/tungstenite/protocol/index.html"><code>tungstenite::protocol</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsTx.hh"><code>Zws::TxLayer</code></a><br>➕ reference-buffer output and explicit fragment/message boundaries</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a> WebSocket write/partial-write operations</td>
  <td>N/A</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/tungstenite/latest/tungstenite/protocol/struct.WebSocket.html"><code>WebSocket</code></a> / <a href="https://docs.rs/tokio-tungstenite/latest/tokio_tungstenite/struct.WebSocketStream.html"><code>WebSocketStream</code></a>; frame ownership/fragmentation APIs differ</td>
</tr>
</tbody>
</table>

## WebSocket endpoints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsProtocol.hh"><code>Zws::Config</code></a> and link state<br>➕ bounded messages with handshake/close/heartbeat policy and per-link state</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>Partial: <a href="https://docs.rs/tungstenite/latest/tungstenite/protocol/struct.WebSocketConfig.html"><code>WebSocketConfig</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsHub.hh"><code>Zws::Client</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsHub.hh"><code>Server</code></a> and native links<br>➕ CRTP message callbacks and streaming output over H1/H2/H3 bindings</td>
  <td><a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a> <a href="https://github.com/boostorg/beast/blob/master/include/boost/beast/websocket/stream.hpp"><code>websocket::stream</code></a> + application accept/connect lifecycle</td>
  <td>Partial: <a href="https://docs.rs/tokio-tungstenite/latest/tokio_tungstenite/"><code>tokio-tungstenite</code></a> (H1; custom H2/H3 integration)</td>
</tr>
</tbody>
</table>

# Zrest - REST APIs

## Typed request/response contracts

[Public headers](https://github.com/kaldron-labs/z/blob/main/zrest/src/) · [Module guide](../zrest/README.md)

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh"><code>Zrest::Request</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh"><code>Response</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh"><code>Reply</code></a> and catalog macros<br>➕ compile-time methods, paths, body policies and allowed typed replies</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Describe</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/axum/latest/axum/"><code>axum</code></a>, <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a>, <a href="https://docs.rs/utoipa/latest/utoipa/"><code>utoipa</code></a>, <a href="https://docs.rs/aide/latest/aide/"><code>aide</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestInput.hh"><code>Zrest::Input</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestInput.hh"><code>BorrowedData</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh"><code>RequestContext</code></a><br>➕ bounded entity assembly with explicit owned/borrowed decoding policy</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">Boost.JSON</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h"><code>folly::IOBufQueue + dynamic JSON conversion</code></a></td>
  <td>Partial: <a href="https://docs.rs/axum/latest/axum/extract/index.html"><code>axum::extract</code></a>, <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a></td>
</tr>
<tr valign="top">
  <td>REST query/body signing hooks and <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh"><code>REST::Signature</code></a><br>➕ filters the signature field and exposes the serialized bytes to the signer<br>➖ algorithm, verification and wire-coverage policy are application-owned</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/reqwest/latest/reqwest/struct.Request.html"><code>reqwest::Request</code></a>, <a href="https://docs.rs/http/latest/http/"><code>http</code></a>, <a href="https://docs.rs/hmac/latest/hmac/"><code>hmac</code></a></td>
</tr>
</tbody>
</table>

## Adapters, completion and server-sent events

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestClient.hh"><code>MReqBuilder</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestClient.hh"><code>MResParser</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh"><code>MReqParser</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh"><code>MResBuilder</code></a><br>➕ shared catalog adapters for typed operations over <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh">Zhttp</a> profiles</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Describe</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/reqwest/latest/reqwest/"><code>reqwest</code></a>, <a href="https://docs.rs/axum/latest/axum/"><code>axum</code></a>, <a href="https://docs.rs/serde/latest/serde/"><code>serde</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh"><code>Zrest::Completion</code></a><br>➕ one-shot asynchronous typed completion with route invalidation</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/futures/Future.h"><code>folly::Promise / Future</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/future/trait.Future.html"><code>Future</code></a>, <a href="https://docs.rs/tokio/latest/tokio/sync/oneshot/index.html"><code>oneshot</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestSSE.hh"><code>Zrest::SSEProducer</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh"><code>SSEBuilder</code></a>, SSE reply metadata<br>➕ bounded producer queues and incremental typed SSE delivery<br>➖ no automatic replay or reconnection</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/axum/latest/axum/response/sse/index.html"><code>axum::response::sse</code></a> + bounded <a href="https://docs.rs/tokio/latest/tokio/sync/mpsc/index.html"><code>mpsc</code></a> queues</td>
</tr>
</tbody>
</table>

## REST endpoints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestClient.hh"><code>Zrest::Client</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh"><code>Server</code></a>, endpoint configs and limits<br>➕ typed calls/routes, admission limits and full REST/HTTP drain boundary</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a>, <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Describe</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/reqwest/latest/reqwest/"><code>reqwest</code></a>, <a href="https://docs.rs/axum/latest/axum/"><code>axum</code></a></td>
</tr>
</tbody>
</table>

# Zgrpc - GRPC

## Method, metadata and status vocabulary

[Public headers](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/) · [Module guide](../zgrpc/README.md).
[Zpb](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh) supplies native protobuf payloads; [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh) supplies H2/H3 transport.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh"><code>Zgrpc::Method</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh"><code>Message</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh"><code>Reply</code></a> and catalog macros<br>➕ typed method paths, RPC cardinality and response contracts</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a>, <a href="https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html">Hana</a></td>
  <td><a href="https://docs.rs/tonic/latest/tonic/"><code>tonic</code></a> typed service/method/message contracts; <a href="https://docs.rs/tonic-prost-build/latest/tonic_prost_build/"><code>tonic-prost-build</code></a> generates protobuf service code</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh"><code>Zgrpc::Metadata</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh"><code>Status</code></a>, timeout and binary-value codecs<br>➕ bounded initial/trailing metadata and gRPC status/deadline interpretation</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/tonic/latest/tonic/metadata/index.html"><code>tonic::metadata</code></a> / <a href="https://docs.rs/tonic/latest/tonic/struct.Status.html"><code>Status</code></a> / <a href="https://docs.rs/tonic/latest/tonic/struct.Request.html"><code>Request</code></a> timeout metadata</td>
</tr>
</tbody>
</table>

## Envelope, protobuf and HTTP adapters

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcFrame.hh"><code>Zgrpc::FrameRx</code></a> and pooled frame output<br>➕ incremental five-byte envelopes with byte budgets and retained payload backing</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>N/A</td>
  <td><a href="https://docs.rs/tonic/latest/tonic/codec/index.html"><code>tonic::codec</code></a> gRPC framing; byte budgets/backing ownership differ</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh"><code>Zgrpc::PBMessage</code></a> and typed payload adapters<br>➕ <a href="https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh">Zpb</a> verification before typed construction and explicit borrowed-data policy</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/tonic-prost/latest/tonic_prost/struct.ProstCodec.html"><code>tonic_prost::ProstCodec</code></a>, <a href="https://docs.rs/prost/latest/prost/"><code>prost</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcIO.hh"><code>Zgrpc::RxInput</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcIO.hh"><code>TxFrames</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcIO.hh"><code>TxBody</code></a><br>➕ bounded receive turns and retained frame queues for HTTP production</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h"><code>folly::IOBufQueue</code></a></td>
  <td>Partial: <a href="https://docs.rs/tonic/latest/tonic/struct.Streaming.html"><code>tonic::Streaming</code></a>, <a href="https://docs.rs/tonic/latest/tonic/codec/index.html"><code>codec</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh">Request/response HTTP and multi-method adapters</a><br>➕ method-specific metadata, streaming bodies and terminal trailers over H2/H3</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/tonic/latest/tonic/server/index.html"><code>tonic::server</code></a>, <a href="https://docs.rs/tonic/latest/tonic/client/index.html"><code>client</code></a> (HTTP/2)</td>
</tr>
</tbody>
</table>

## Call ownership and endpoints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcCall.hh"><code>Zgrpc::ClientCall</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcCall.hh"><code>ServerCall</code></a>, call tokens and directional state<br>➕ invalidatable Rx/Tx routes, cardinality and one-shot finish/cancel admission</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/tonic/latest/tonic/struct.Request.html"><code>Request</code></a>, <a href="https://docs.rs/tonic/latest/tonic/struct.Response.html"><code>Response</code></a>, <a href="https://docs.rs/tonic/latest/tonic/struct.Streaming.html"><code>Streaming</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcClient.hh"><code>Zgrpc::Client</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcServer.hh"><code>Server</code></a>, endpoint configs and limits<br>➕ unary/client-streaming/server-streaming/duplex RPCs, deadlines and bounded shutdown</td>
  <td>N/A</td>
  <td><a href="https://docs.rs/tonic/latest/tonic/transport/index.html"><code>tonic::transport</code></a> HTTP/2 client/server with four RPC cardinalities; shutdown/limits configured separately</td>
</tr>
</tbody>
</table>

# Zjrpc - JSON RPC

## JSON-RPC envelopes and declarations

[Public headers](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/) · [Module guide](../zjrpc/README.md).
JSON counterparts use [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html);
JSON parsing alone does not supply JSON-RPC dispatch or correlation.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh"><code>Zjrpc::Request</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh"><code>Response</code></a>, IDs, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh"><code>Envelope</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh"><code>Parsed</code></a> and wire views<br>➕ typed parameters/results and distinct missing/null IDs with retained JSON views<br>➖ numeric IDs use signed 64-bit integers; no fractional/exponent forms</td>
  <td>Partial: <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">Boost.JSON</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/json/json.h"><code>folly::dynamic / parseJson / toJson</code></a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/jsonrpsee-types/latest/jsonrpsee_types/"><code>jsonrpsee-types</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcIO.hh"><code>Zjrpc::Input</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcIO.hh"><code>Output</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcIO.hh"><code>BufOutput</code></a><br>➕ bounded pooled JSON assembly and serialization</td>
  <td>Partial: <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">Boost.JSON</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h"><code>folly::IOBufQueue + JSON parser/serializer</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_datumutil.h"><code>baljsn::DatumUtil</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h"><code>bdlbb::Blob</code></a></td>
  <td>Partial: <a href="https://docs.rs/serde_json/latest/serde_json/"><code>serde_json</code></a>, <a href="https://docs.rs/bytes/latest/bytes/"><code>bytes</code></a></td>
</tr>
</tbody>
</table>

## Correlation, completion and batches

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh"><code>Zjrpc::PendingCalls</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh"><code>Caller</code></a><br>➕ outbound ID correlation removed before typed terminal callbacks</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html">Boost.Unordered</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/jsonrpsee-core/latest/jsonrpsee_core/client/index.html"><code>jsonrpsee_core::client</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcInbound.hh"><code>Zjrpc::InboundCalls</code></a><br>➕ separate live requests and bounded recent-ID history<br>➖ history contains IDs, not replayable replies</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html">Boost.MultiIndex</a></td>
  <td>Partial: <a href="https://docs.rs/jsonrpsee-server/latest/jsonrpsee_server/"><code>jsonrpsee-server</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcCompletion.hh"><code>Zjrpc::Completion</code></a> and completion slots<br>➕ one-shot claims before reply posting and invalidation before cancellation callbacks</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td><a href="https://docs.rs/jsonrpsee-core/latest/jsonrpsee_core/server/struct.PendingSubscriptionSink.html"><code>PendingSubscriptionSink</code></a> / <a href="https://docs.rs/tokio/latest/tokio/sync/oneshot/index.html"><code>oneshot</code></a>; subscription acceptance is only a partial analogue to general one-shot RPC completion</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcDispatch.hh"><code>Zjrpc::Dispatcher</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh"><code>Batch</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh"><code>BatchReply</code></a><br>➕ shared typed dispatch, notifications and bounded batch aggregation</td>
  <td>Partial: <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">Boost.JSON</a></td>
  <td>Partial: <a href="https://docs.rs/jsonrpsee/latest/jsonrpsee/struct.RpcModule.html"><code>RpcModule</code></a>, <a href="https://docs.rs/jsonrpsee-core/latest/jsonrpsee_core/params/struct.BatchRequestBuilder.html"><code>BatchRequestBuilder</code></a></td>
</tr>
</tbody>
</table>

## Transport bindings and endpoints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcStdio.hh"><code>Zjrpc::StdioConfig</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcStdio.hh"><code>StdioFramer</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcStdio.hh"><code>IOEndpoint</code></a><br>➕ line framing and isolated blocking workers over owned per-run handle duplicates</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html">Iostreams</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/async/AsyncPipe.h"><code>folly::AsyncPipeReader / AsyncPipeWriter</code></a></td>
  <td>Partial: <a href="https://docs.rs/tokio-util/latest/tokio_util/codec/struct.LinesCodec.html"><code>LinesCodec</code></a>, <a href="https://docs.rs/serde_json/latest/serde_json/"><code>serde_json</code></a>, <a href="https://docs.rs/jsonrpsee-core/latest/jsonrpsee_core/"><code>jsonrpsee-core</code></a></td>
</tr>
<tr valign="top">
  <td>JSON-RPC HTTP/SSE and WebSocket bindings<br>➕ shared RPC work/correlation over <a href="https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh">Zhttp</a>/<a href="https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsH1.hh">Zws</a> channels</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/jsonrpsee-http-client/latest/jsonrpsee_http_client/"><code>jsonrpsee-http-client</code></a>, <a href="https://docs.rs/jsonrpsee-ws-client/latest/jsonrpsee_ws_client/"><code>jsonrpsee-ws-client</code></a>, <a href="https://docs.rs/jsonrpsee-server/latest/jsonrpsee_server/"><code>jsonrpsee-server</code></a> (custom SSE)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcClient.hh"><code>Zjrpc::IOClient</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcServer.hh"><code>IOServer</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcClient.hh"><code>HTTPClient</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcServer.hh"><code>HTTPServer</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcClient.hh"><code>WSClient</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcServer.hh"><code>WSServer</code></a><br>➕ typed calls/notifications/batches and coordinated restart after drain</td>
  <td>None: <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/jsonrpsee/latest/jsonrpsee/"><code>jsonrpsee</code></a> (HTTP/WebSocket)</td>
</tr>
</tbody>
</table>

# Zmcp - Model Context Protocol

## Typed tools and protocol vocabulary

[Public headers](https://github.com/kaldron-labs/z/blob/main/zmcp/src/) · [Module guide](../zmcp/README.md).
Uses [Zjrpc](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh), [Zws](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsH1.hh) and [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh); it has no dependency on [Zrest](https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh) despite shared metadata conventions.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh"><code>Zmcp::Request</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh"><code>Response</code></a>, tool catalogs and generated schemas<br>➕ reflected tools/list/tools/call with typed input/output schemas<br>➖ current scope is tool providers/callers; prompts/resources and other MCP subsystems are deferred</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td><a href="https://docs.rs/rmcp/latest/rmcp/"><code>rmcp</code></a> typed tool handlers + <a href="https://docs.rs/schemars/latest/schemars/"><code>schemars</code></a> input/output schemas</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh"><code>Zmcp::ToolAnnotations</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh"><code>MCP::Header</code></a><br>➕ compile-time tool semantics drive schema/header output and trusted retry policy</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/rmcp/latest/rmcp/model/struct.ToolAnnotations.html"><code>rmcp::model::ToolAnnotations</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh">MCP peer negotiation and catalog cache</a><br>➕ discovery/legacy handshake and explicit catalog invalidation</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/rmcp/latest/rmcp/service/index.html"><code>rmcp::service</code></a>, <a href="https://docs.rs/rmcp/latest/rmcp/model/index.html"><code>model</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh"><code>Zmcp::Context</code></a> and extended completion policy<br>➕ transport/session/stream context with cancellation, progress and logging<br>➖ context pointers are callback-scoped</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a></td>
  <td>Partial: <a href="https://docs.rs/rmcp/latest/rmcp/service/struct.RequestContext.html"><code>RequestContext</code></a>, <a href="https://docs.rs/rmcp/latest/rmcp/model/index.html"><code>model</code></a></td>
</tr>
</tbody>
</table>

## MCP endpoints

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh"><code>Zmcp::Batch</code></a> and shared JSON-RPC aggregation<br>➕ typed aggregate tool calls reuse the ordinary pending-call table</td>
  <td>Partial: <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">Boost.JSON</a></td>
  <td>Partial: <a href="https://docs.rs/rmcp/latest/rmcp/service/index.html"><code>rmcp service requests</code></a>, <a href="https://docs.rs/futures/latest/futures/future/fn.join_all.html"><code>join_all</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh"><code>Zmcp::IOClient</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpServer.hh"><code>IOServer</code></a><br>➕ newline-delimited MCP over inherited stdio with bounded work and lifecycle<br>➖ peer process creation/supervision is application-owned</td>
  <td>Partial: <a href="https://github.com/boostorg/process/blob/master/include/boost/process.hpp">Boost.Process</a></td>
  <td>Partial: <a href="https://docs.rs/rmcp/latest/rmcp/transport/index.html"><code>rmcp::transport</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh"><code>Zmcp::HTTPClient</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpServer.hh"><code>HTTPServer</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh"><code>WSClient</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpServer.hh"><code>WSServer</code></a><br>➕ shared typed tool handlers over Streamable HTTP and WebSockets with full drain<br>➖ authentication/authorization remain application-owned</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html">Beast</a>, <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://github.com/boostorg/json/blob/master/include/boost/json.hpp">JSON</a></td>
  <td>Partial: <a href="https://docs.rs/rmcp/latest/rmcp/transport/index.html"><code>rmcp::transport</code></a> (HTTP; custom WebSockets)</td>
</tr>
</tbody>
</table>

# Zdb - Typed, sharded, replicated database

## Schema, rows and indices

[Public headers](https://github.com/kaldron-labs/z/blob/main/zdb/src/) · [Module guide](../zdb/README.md).
[Zfb](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh) supplies wire/schema reflection and [Zv](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvLib.hh) supplies runtime configuration.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">STL&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbTable</code></a><code>&lt;T&gt;</code>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh#L88"><code>ZdbTableDerive</code></a> and reflected key/schema metadata<br>➕ typed primary/secondary indices, grouped queries and generated BFBS schema<br>➖ immutable application schema is selected before startup</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html">Boost.MultiIndex</a>, <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Describe</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/diesel/latest/diesel/"><code>diesel</code></a>, <a href="https://docs.rs/sea-orm/latest/sea_orm/"><code>sea-orm</code></a>, <a href="https://docs.rs/multi_index_map/latest/multi_index_map/"><code>multi_index_map</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbRow</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbRowRef</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbTblRef</code></a><br>➕ intrusive row/table lifetime and shard-owned mutation callbacks</td>
  <td>Partial: <a href="https://en.cppreference.com/cpp/memory/shared_ptr"><code>std::shared_ptr</code></a></td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/intrusive.html">Boost.Intrusive</a>, <a href="https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html">SmartPtr</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h"><code>bsl::shared_ptr</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/sync/struct.Arc.html"><code>Arc</code></a>, <a href="https://docs.rs/tokio/latest/tokio/sync/index.html"><code>Tokio synchronization</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh">Zdb</a> buffer and message formats (<a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbBuf</code></a><code>.hh</code>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbMsg.hh"><code>ZdbMsg</code></a><code>.hh</code>)<br>➕ reflected replication/store records share reference-owned I/O backing</td>
  <td>N/A</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Boost.Serialization</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h"><code>folly::IOBuf</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh">Zdb</a></td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/flatbuffers/latest/flatbuffers/"><code>flatbuffers</code></a>, <a href="https://docs.rs/bytes/latest/bytes/"><code>bytes</code></a></td>
</tr>
</tbody>
</table>

## Database operations and lifecycle

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh">Zdb</a> find/insert/update/delete and row caches<br>➕ typed asynchronous operations with write-through caching and index maintenance<br>➖ local commit acceptance is not synchronous persistence/replication confirmation</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html">Boost.MultiIndex</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h"><code>folly::EvictingCacheMap</code></a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h"><code>bdlcc::Cache</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h"><code>cache</code></a></td>
  <td>Partial: <a href="https://docs.rs/sea-orm/latest/sea_orm/"><code>sea-orm</code></a>, <a href="https://docs.rs/sqlx/latest/sqlx/"><code>sqlx</code></a>, <a href="https://docs.rs/moka/latest/moka/"><code>moka</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh">Zdb</a> select/count and queue barriers<br>➕ index-based backing-store queries ordered with writes</td>
  <td>Partial: database client + custom cache/store queue coordination</td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/sqlx/latest/sqlx/"><code>sqlx</code></a>, <a href="https://docs.rs/diesel/latest/diesel/"><code>diesel</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>Zdb</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbCf</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbHandler</code></a><br>➕ DB-wide shards, configured workers, activation callbacks and orderly drain</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html">Asio</a>, <a href="https://www.boost.org/doc/libs/latest/libs/statechart/doc/reference.html">Statechart</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/tokio/latest/tokio/"><code>tokio</code></a>, <a href="https://docs.rs/sqlx/latest/sqlx/"><code>sqlx</code></a></td>
</tr>
</tbody>
</table>

## Replication and recoverable workflows

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbHost</code></a> and replication/activation<br>➕ leader election, asynchronous chain replication and failover recovery<br>➖ asynchronous replication is not a synchronous consensus commit guarantee</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/openraft/latest/openraft/"><code>openraft</code></a> (Raft, not chain replication)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbSaga.hh"><code>ZdbSaga</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>ZdbSagaDB</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbSaga.hh#L701"><code>ZdbSagaPhase</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbSaga.hh#L707"><code>ZdbSagaRepeatPhase</code></a><br>➕ journaled workflow steps, recovery and application compensation<br>➖ arbitrary external effects are not automatically journaled or exactly-once</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/statechart/doc/reference.html">Boost.Statechart</a></td>
  <td>Partial: <a href="https://docs.rs/sqlx/latest/sqlx/"><code>sqlx</code></a> (custom saga journal)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZtcDB.hh"><code>Ztc::DB</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZtcDBHost.hh"><code>DBHost</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZtcDBTable.hh"><code>DBTable</code></a> telemetry<br>➕ reflected database, host and table statistics</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html">Boost.Describe</a>, <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Serialization</a></td>
  <td><a href="https://docs.rs/metrics/latest/metrics/"><code>metrics</code></a> / <a href="https://docs.rs/tracing/latest/tracing/"><code>tracing</code></a>; database/host/table schemas are application-defined</td>
</tr>
</tbody>
</table>

## Pluggable backing stores

[Store contract](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbStore.hh).
[Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) supplies in-memory indices; [Boost.MySQL](https://www.boost.org/doc/libs/latest/libs/mysql/doc/html/index.html)
is a database client, not an equivalent of [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh)'s database/store framework.

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Folly&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">BDE&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbStore.hh"><code>Zdb_::Store</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbStore.hh"><code>StoreTbl</code></a> and store-module loading<br>➕ asynchronous store contract with reflected schemas and query/write callbacks</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/doc/html/boost_dll/reference.html">Boost.DLL</a></td>
  <td>N/A</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/sea-orm/latest/sea_orm/struct.DatabaseConnection.html"><code>DatabaseConnection</code></a>, <a href="https://docs.rs/sqlx/latest/sqlx/struct.Any.html"><code>Any</code></a> (backend adapters; no dynamic store ABI)</td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbMemStore.hh"><code>ZdbMem::Store</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh"><code>libZdbMem</code></a><br>➕ in-memory implementation of the same store contract<br>➖ process loss loses locally stored data</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html">Boost.MultiIndex</a></td>
  <td>Partial: <a href="https://github.com/facebook/folly/blob/main/folly/container/F14Map.h"><code>folly::F14FastMap / ConcurrentHashMap</code></a>, <a href="https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh">Zdb</a></td>
  <td>Partial: <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashmap.h"><code>bdlc::FlatHashMap</code></a>, <a href="https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_stripedunorderedmap.h"><code>bdlcc::StripedUnorderedMap</code></a></td>
  <td>Partial: <a href="https://doc.rust-lang.org/std/collections/struct.BTreeMap.html"><code>BTreeMap</code></a>, <a href="https://docs.rs/redb/latest/redb/backends/struct.InMemoryBackend.html"><code>redb::backends::InMemoryBackend</code></a></td>
</tr>
</tbody>
</table>

# ZdbPQ - PostgreSQL backing store and native types

## PostgreSQL backing store

[Public API](https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh) · [Module guide](../zdb_pq/README.md)

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh"><code>ZdbPQ</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh"><code>libZdbPQ</code></a><br>➕ asynchronous libpq store with reflected prepared SQL and binary I/O<br>➖ requires PostgreSQL/libpq and configured server-side types</td>
  <td>None: <a href="https://www.boost.org/doc/libs/latest/libs/mysql/doc/html/mysql/ref.html">Boost.MySQL</a></td>
  <td>Partial: <a href="https://docs.rs/tokio-postgres/latest/tokio_postgres/"><code>tokio-postgres</code></a>, <a href="https://docs.rs/sqlx/latest/sqlx/postgres/index.html"><code>sqlx::postgres</code></a></td>
</tr>
<tr valign="top">
  <td>PostgreSQL <a href="https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh"><code>libz</code></a> type extension<br>➕ native database representations for Z decimal, time and bitmap values</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/pgrx/latest/pgrx/"><code>pgrx</code></a></td>
</tr>
</tbody>
</table>

# ZdbSL - SQLite backing store

## SQLite backing store and inspection

[Public headers](https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/) · [Module guide](../zdb_sqlite/README.md)

<table>
<thead>
<tr>
  <th scope="col" width="340" align="left">Z&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="240" align="left">Boost&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
  <th scope="col" width="280" align="left">Rust&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</th>
</tr>
</thead>
<tbody>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/ZdbSL.hh"><code>ZdbSL::Store</code></a> / <a href="https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/ZdbSL.hh"><code>libZdbSL</code></a><br>➕ reflected persistent store with WAL and one isolated worker per connection<br>➖ local filesystem and one writer connection per store</td>
  <td>N/A</td>
  <td>Partial: <a href="https://docs.rs/rusqlite/latest/rusqlite/"><code>rusqlite</code></a>, <a href="https://docs.rs/tokio-rusqlite/latest/tokio_rusqlite/"><code>tokio-rusqlite</code></a>, <a href="https://docs.rs/sqlx/latest/sqlx/sqlite/index.html"><code>sqlx::sqlite</code></a></td>
</tr>
<tr valign="top">
  <td><a href="https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/ZdbSL.hh"><code>ZdbSL</code></a> storage codecs and inspection extension<br>➕ canonical sortable Z scalar encodings and readable SQLite inspection helpers</td>
  <td>Partial: <a href="https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html">Boost.Endian</a>, <a href="https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html">Serialization</a></td>
  <td>Partial: <a href="https://docs.rs/rusqlite/latest/rusqlite/functions/index.html"><code>rusqlite scalar functions</code></a>, <a href="https://docs.rs/rusqlite/latest/rusqlite/vtab/index.html"><code>virtual tables</code></a></td>
</tr>
</tbody>
</table>
