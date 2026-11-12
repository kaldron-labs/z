# Z, STL, Boost, Folly, BDE and NTF equivalents

See the [container verb Rosetta stone](ROSETTA.md) for Z and STL insertion,
lookup, iteration and removal idioms.

## Contents

- [Zu](#zu)
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
- [Zm](#zm)
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
- [Zt](#zt)
  - [Platform and environment](#platform-and-environment)
  - [Boolean and enum vocabulary](#boolean-and-enum-vocabulary)
  - [Text formatting](#text-formatting)
  - [Character case and encoding conversion](#character-case-and-encoding-conversion)
  - [Owning arrays, strings and scratch buffers](#owning-arrays-strings-and-scratch-buffers)
  - [Bitmaps and sliding windows](#bitmaps-and-sliding-windows)
  - [Timezone conversion](#timezone-conversion)
  - [Demangling](#demangling)
  - [Regular expressions](#regular-expressions)
- [Ze](#ze)
  - [Native errors](#native-errors)
  - [Diagnostic metadata and events](#diagnostic-metadata-and-events)
  - [Exceptions](#exceptions)
- [Zf](#zf)
  - [Configuration trees and typed loading](#configuration-trees-and-typed-loading)
  - [Structure reflection and serialization metadata](#structure-reflection-and-serialization-metadata)
  - [Text and web formats](#text-and-web-formats)
  - [Configuration formats](#configuration-formats)
  - [Command-line binding](#command-line-binding)
  - [Binary wire formats](#binary-wire-formats)
- [Zi](#zi)
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
- [Zxdp](#zxdp)
  - [Registered packet storage](#registered-packet-storage)
  - [Raw packet I/O and service](#raw-packet-io-and-service)
- [Zutcp](#zutcp)
  - [User-mode TCP/IP stack](#user-mode-tcpip-stack)
  - [TCP connection and recovery](#tcp-connection-and-recovery)
- [Zv](#zv)
  - [Thread, stack and ring parameter binding](#thread-stack-and-ring-parameter-binding)
  - [Connection and multiplexer parameter binding](#connection-and-multiplexer-parameter-binding)
  - [Configuration file loading and saving](#configuration-file-loading-and-saving)
- [Zpb](#zpb)
  - [Protobuf wire format and typed messages](#protobuf-wire-format-and-typed-messages)
  - [Schema reflection and compiler](#schema-reflection-and-compiler)
- [Zfb](#zfb)
  - [FlatBuffers construction and access](#flatbuffers-construction-and-access)
  - [Schema reflection and compiler](#schema-reflection-and-compiler-1)
- [Zdbus](#zdbus)
  - [Addresses, authentication and framing](#addresses-authentication-and-framing)
  - [Typed message adapters and runtime](#typed-message-adapters-and-runtime)
- [Ztcp](#ztcp)
  - [Buffer policies and byte streams](#buffer-policies-and-byte-streams)
  - [TCP links and endpoints](#tcp-links-and-endpoints)
- [Ztls](#ztls)
  - [Cryptographic primitives](#cryptographic-primitives)
  - [Keys, encapsulation and encrypted files](#keys-encapsulation-and-encrypted-files)
  - [Credential storage](#credential-storage)
  - [TLS byte streams and endpoints](#tls-byte-streams-and-endpoints)
- [Zquic](#zquic)
  - [Packet, frame and crypto codecs](#packet-frame-and-crypto-codecs)
  - [Recovery, paths and stream ownership](#recovery-paths-and-stream-ownership)
  - [Transport endpoints and diagnostics](#transport-endpoints-and-diagnostics)
- [Zhttp](#zhttp)
  - [URLs, fields and protocol configuration](#urls-fields-and-protocol-configuration)
  - [Discovery and event framing](#discovery-and-event-framing)
  - [Messages, body streams and response routes](#messages-body-streams-and-response-routes)
  - [HTTP wire protocols and endpoints](#http-wire-protocols-and-endpoints)
- [Zws](#zws)
  - [URI, handshake and frame codecs](#uri-handshake-and-frame-codecs)
  - [WebSocket endpoints](#websocket-endpoints)
- [Zrest](#zrest)
  - [Typed request/response contracts](#typed-requestresponse-contracts)
  - [Adapters, completion and server-sent events](#adapters-completion-and-server-sent-events)
  - [REST endpoints](#rest-endpoints)
- [Zgrpc](#zgrpc)
  - [Method, metadata and status vocabulary](#method-metadata-and-status-vocabulary)
  - [Envelope, protobuf and HTTP adapters](#envelope-protobuf-and-http-adapters)
  - [Call ownership and endpoints](#call-ownership-and-endpoints)
- [Zjrpc](#zjrpc)
  - [JSON-RPC envelopes and declarations](#json-rpc-envelopes-and-declarations)
  - [Correlation, completion and batches](#correlation-completion-and-batches)
  - [Transport bindings and endpoints](#transport-bindings-and-endpoints)
- [Zmcp](#zmcp)
  - [Typed tools and protocol vocabulary](#typed-tools-and-protocol-vocabulary)
  - [MCP endpoints](#mcp-endpoints)
- [Zdb](#zdb)
  - [Schema, rows and indices](#schema-rows-and-indices)
  - [Database operations and lifecycle](#database-operations-and-lifecycle)
  - [Replication and recoverable workflows](#replication-and-recoverable-workflows)
  - [Pluggable backing stores](#pluggable-backing-stores)
- [ZdbPQ](#zdbpq)
  - [PostgreSQL backing store](#postgresql-backing-store)
- [ZdbSL](#zdbsl)
  - [SQLite backing store and inspection](#sqlite-backing-store-and-inspection)

# Zu

Framework underlying foundation.

## Abbreviated forwarding and move operations

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuFwd`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L334) | [`std::forward`](https://en.cppreference.com/cpp/utility/forward) | [`boost::forward`](https://github.com/boostorg/move/blob/master/include/boost/move/utility_core.hpp) ([Move](https://www.boost.org/doc/libs/latest/doc/html/move.html)) | N/A | [`bslmf::Util::forward`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h) |
| [`ZuMv`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L343) | [`std::move`](https://en.cppreference.com/cpp/utility/move) | [`boost::move`](https://github.com/boostorg/move/blob/master/include/boost/move/utility_core.hpp) | N/A | [`bslmf::MovableRefUtil::move`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_movableref.h) |
| [`ZuMvPtr`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ nulls a moved raw pointer as well as moving smart pointers | Partial: [`std::exchange`](https://en.cppreference.com/cpp/utility/exchange)`(p, nullptr)` for raw pointers, [`std::move`](https://en.cppreference.com/cpp/utility/move) for smart pointers | Partial: [`boost::exchange`](https://github.com/boostorg/core/blob/master/include/boost/core/exchange.hpp), [`boost::move`](https://github.com/boostorg/move/blob/master/include/boost/move/utility_core.hpp) | N/A | Partial: [`bsl::exchange`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_utility.h)`(p, nullptr)` |
| [`ZuFwdLike`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L365)<br>➕ optional target-type conversion | [`std::forward_like`](https://en.cppreference.com/cpp/utility/forward_like) (C++23) | Partial: [Boost.Move](https://www.boost.org/doc/libs/latest/doc/html/move.html) forwarding + [TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) qualifier propagation | [`folly::forward_like`](https://github.com/facebook/folly/blob/main/folly/Utility.h)<br>➖ no separate target-type conversion overload | Partial: [`bslmf::Util::forward_like`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h) |

## Compile-time constants

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuConstant`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)`<T, V>` — typed compile-time value, including non-integral and structural class values such as [`ZuString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh)<br>➕ conversion and call return const references | [`std::integral_constant`](https://en.cppreference.com/cpp/types/integral_constant)`<T, V>` — also accepts permitted non-integral template values<br>➖ conversion and call return by value | [`boost::integral_constant`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp) | Partial: [`folly::vtag_t<V>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ value-list tag rather than a typed reference-returning constant wrapper | [`bsl::integral_constant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<T, V>`<br>➖ conversion and call return by value rather than const reference |
| [`ZuInt`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::integral_constant`](https://en.cppreference.com/cpp/types/integral_constant)`<int, I>` | [`boost::mp11::mp_int`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html)`<I>` | N/A | [`bsl::integral_constant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<int, I>` |
| [`ZuUnsigned`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::integral_constant`](https://en.cppreference.com/cpp/types/integral_constant)`<unsigned, I>` | [`boost::integral_constant`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp)`<unsigned, I>` | Partial: [`folly::index_constant<I>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ size_t-valued rather than unsigned-valued | [`bsl::integral_constant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<unsigned, I>` |
| [`ZuBool`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::bool_constant`](https://en.cppreference.com/cpp/types/integral_constant) | [`boost::mp11::mp_bool`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A | [`bsl::integral_constant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<bool, B>` |
| [`ZuTrue`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::true_type`](https://en.cppreference.com/cpp/types/integral_constant) | [`boost::true_type`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp) | N/A | [`bsl::true_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuFalse`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::false_type`](https://en.cppreference.com/cpp/types/integral_constant) | [`boost::false_type`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/integral_constant.hpp) | N/A | [`bsl::false_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuAlwaysTrue`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: dependent [`std::true_type`](https://en.cppreference.com/cpp/types/integral_constant) wrapper | Partial: [`boost::mp11::mp_true`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) wrapper | N/A | Composition: [`bsl::true_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` via a dependent alias` |
| [`ZuAlwaysFalse`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: dependent [`std::false_type`](https://en.cppreference.com/cpp/types/integral_constant) wrapper | Partial: [`boost::mp11::mp_false`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) wrapper | N/A | Composition: [`bsl::false_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` via a dependent alias` |
| [`ZuInt128`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh) | Partial: [`std::integral_constant`](https://en.cppreference.com/cpp/types/integral_constant) over compiler 128-bit integer | Partial: constant wrapper over a supported 128-bit type | N/A | Partial: [`bsl::integral_constant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` with compiler 128-`[`bit`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_bit.h)` type` |
| [`ZuUInt128`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh) | Partial: [`std::integral_constant`](https://en.cppreference.com/cpp/types/integral_constant) over compiler unsigned 128-bit integer | Partial: constant wrapper over a supported 128-bit type | N/A | Partial: [`bsl::integral_constant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` with compiler 128-`[`bit`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_bit.h)` type` |

## Abbreviated type transformations and normalizations

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuDeref`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::remove_reference_t`](https://en.cppreference.com/cpp/types/remove_reference) | [`boost::remove_reference_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/remove_reference.hpp) | N/A | [`bsl::remove_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuStrip`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::remove_cv_t`](https://en.cppreference.com/cpp/types/remove_cv) | [`boost::remove_cv_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/remove_cv.hpp) | N/A | [`bsl::remove_cv`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuDecay`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➖ does not decay arrays/functions to pointers | [`std::remove_cvref_t`](https://en.cppreference.com/cpp/types/remove_cvref)<br>➕ matches the actual operation; [`std::decay_t`](https://en.cppreference.com/cpp/types/decay) has different semantics | [`boost::remove_cv_ref_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/remove_cv_ref.hpp) | N/A | [`bsl::remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuAsIs`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::type_identity_t`](https://en.cppreference.com/cpp/types/type_identity) | [`boost::type_identity_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/type_identity.hpp) | N/A | [`bsl::type_identity`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuMkConst`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::add_const_t`](https://en.cppreference.com/cpp/types/add_cv)<br>➕ handles reference inputs through trait rules | [`boost::add_const_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_const.hpp) | N/A | [`bsl::add_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuMkVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::add_volatile_t`](https://en.cppreference.com/cpp/types/add_cv) | [`boost::add_volatile_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_volatile.hpp) | N/A | [`bsl::add_volatile`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuMkRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::add_rvalue_reference_t`](https://en.cppreference.com/cpp/types/add_reference)<br>➕ also handles non-referenceable types | [`boost::add_rvalue_reference_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_rvalue_reference.hpp) | N/A | [`bsl::add_rvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuMkLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::add_lvalue_reference_t`](https://en.cppreference.com/cpp/types/add_reference) | [`boost::add_lvalue_reference_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_lvalue_reference.hpp) | N/A | [`bsl::add_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuMkCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Composition: [`std::add_lvalue_reference_t`](https://en.cppreference.com/cpp/types/add_reference)`<`[`std::add_const_t`](https://en.cppreference.com/cpp/types/add_cv)`<T>>` | Composition: [`boost::add_lvalue_reference_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_lvalue_reference.hpp)`<`[`boost::add_const_t`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/add_const.hpp)`<T>>` | N/A | Composition: [`bsl::add_lvalue_reference_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::add_const_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<T>>` |
| [`ZuRDecay`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ ADL-customizable recursive decay of composites | N/A; [`std::remove_cvref_t`](https://en.cppreference.com/cpp/types/remove_cvref) is shallow | Partial: recursive [Boost.Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) / [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) transformation | N/A | Partial: [`bsl::remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ no ADL-customizable recursive decay of composite types |
| [`ZuNorm`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuNorm.hh)<br>➕ customizable normalization of representation-equivalent character types | Partial: [`std::remove_cvref_t`](https://en.cppreference.com/cpp/types/remove_cvref)<br>➖ does not normalize character representations | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html); custom mapping | N/A | Partial: [`bsl::remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ no built-in normalization of representation-equivalent character types |
| [`ZuEquiv`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuEquiv.hh) | Composition: [`std::is_same`](https://en.cppreference.com/cpp/types/is_same) with a custom normalization | Composition: [`boost::is_same`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp) with a custom normalization | N/A | Partial: [`bsl::is_same`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`  +  `[`remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ distinct character types remain distinct without a normalization trait |
| [`ZuAlt`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAlt.hh)<br>➕ char/wchar_t alternate-type mapping | N/A | N/A | N/A | N/A |
| [`ZuUnder`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ customizable for wrappers as well as enums | Partial: [`std::underlying_type_t`](https://en.cppreference.com/cpp/types/underlying_type)<br>➖ enums only | Partial: [`boost::underlying_type`](https://github.com/boostorg/core/blob/master/include/boost/core/underlying_type.hpp)`<T>::type` | N/A | Partial: [`bsl::underlying_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ enums only; no ADL unwrapping of participating wrapper types |
| [`ZuUnderlying`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L747)<br>➕ unwraps participating wrapper types as well as enums | Partial: [`std::to_underlying`](https://en.cppreference.com/cpp/utility/to_underlying) (C++23)<br>➖ enums only | Partial: [`boost::underlying_cast`](https://github.com/boostorg/core/blob/master/include/boost/core/scoped_enum.hpp) | Partial: [`folly::to_underlying`](https://github.com/facebook/folly/blob/main/folly/Utility.h)<br>➖ enums only | Partial: [`bsl::to_underlying`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_utility.h)<br>➖ enums only; no ADL unwrapping of participating wrapper types |

## Qualifier and reference constraints

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuIsConst`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::is_const`](https://en.cppreference.com/cpp/types/is_const) | [`boost::is_const`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_const.hpp) | [`bsl::is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuIsVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::is_volatile`](https://en.cppreference.com/cpp/types/is_volatile) | [`boost::is_volatile`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_volatile.hpp) | [`bsl::is_volatile`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuConst`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<`[`std::is_const_v`](https://en.cppreference.com/cpp/types/is_const)`<U>, R>` | [`boost::enable_if`](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp)`<`[`boost::is_const`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_const.hpp)`<U>, R>::type` | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_const_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |
| [`ZuMutable`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<!`[`std::is_const_v`](https://en.cppreference.com/cpp/types/is_const)`<U>, R>` | [`boost::disable_if`](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp)`<`[`boost::is_const`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_const.hpp)`<U>, R>::type` | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!`[`bsl::is_const_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |
| [`ZuVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<`[`std::is_volatile_v`](https://en.cppreference.com/cpp/types/is_volatile)`<U>, R>` | [`boost::enable_if`](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp)`<`[`boost::is_volatile`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_volatile.hpp)`<U>, R>::type` | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_volatile_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |
| [`ZuNonVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<!`[`std::is_volatile_v`](https://en.cppreference.com/cpp/types/is_volatile)`<U>, R>` | [`boost::disable_if`](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp)`<`[`boost::is_volatile`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_volatile.hpp)`<U>, R>::type` | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!`[`bsl::is_volatile_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |
| [`ZuIsLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::is_lvalue_reference`](https://en.cppreference.com/cpp/types/is_lvalue_reference) | [`boost::is_lvalue_reference`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_lvalue_reference.hpp) | [`bsl::is_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuIsCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Composition: lvalue-reference test and const-referent test | Composition of [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) | Composition: [`bsl::conjunction`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, `[`bsl::is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::remove_reference_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>>>` |
| [`ZuIsRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::is_rvalue_reference`](https://en.cppreference.com/cpp/types/is_rvalue_reference) | [`boost::is_rvalue_reference`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_rvalue_reference.hpp) | [`bsl::is_rvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<`[`std::is_lvalue_reference_v`](https://en.cppreference.com/cpp/types/is_lvalue_reference)`<U>, R>` | [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_lvalue_reference`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_lvalue_reference.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_lvalue_reference_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |
| [`ZuNotLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Negated [`std::is_lvalue_reference`](https://en.cppreference.com/cpp/types/is_lvalue_reference) constraint | [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_lvalue_reference`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_lvalue_reference.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!`[`bsl::is_lvalue_reference_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |
| [`ZuCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Constraint on const lvalue references | Composition of [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) and [TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_lvalue_reference_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U> && `[`bsl::is_const_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::remove_reference_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>>, R>` |
| [`ZuNotCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Negated const-lvalue-reference constraint | Composition of [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) and [TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!(`[`bsl::is_lvalue_reference_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U> && `[`bsl::is_const_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::remove_reference_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>>), R>` |
| [`ZuRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<`[`std::is_rvalue_reference_v`](https://en.cppreference.com/cpp/types/is_rvalue_reference)`<U>, R>` | [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_rvalue_reference`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_rvalue_reference.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_rvalue_reference_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |
| [`ZuNotRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Negated [`std::is_rvalue_reference`](https://en.cppreference.com/cpp/types/is_rvalue_reference) constraint | [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_rvalue_reference`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_rvalue_reference.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!`[`bsl::is_rvalue_reference_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<U>, R>` |

## Type identity and inheritance constraints

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuIsSame`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::is_same`](https://en.cppreference.com/cpp/types/is_same) | [`boost::is_same`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp) | [`bsl::is_same`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuSame`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<`[`std::is_same_v`](https://en.cppreference.com/cpp/types/is_same)`<A, B>, R>` | [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_same`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_same_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<A, B>, R>` |
| [`ZuNotSame`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Negated [`std::is_same`](https://en.cppreference.com/cpp/types/is_same) constraint | [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_same`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_same.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!`[`bsl::is_same_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<A, B>, R>` |
| [`ZuIs`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ decays types and accepts identity or accessible pointer base conversion | Partial: [`std::same_as`](https://en.cppreference.com/cpp/concepts/same_as) / [`std::derived_from`](https://en.cppreference.com/cpp/concepts/derived_from) constraint | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) + [EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) | Composition: [`bsl::enable_if`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`  +  `[`remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` + identity/accessible pointer-conversion tests` |
| [`ZuIsNot`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Negation of the identity/base-conversion constraint | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) + [DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) | Composition: [`bsl::enable_if`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` + negated normalized identity/accessible pointer-conversion tests` |
| [`ZuIsBase`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)`<`[`From`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)`, `[`To`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)`>` — derived-to-base argument order; Z identity/access rules | Partial: [`std::is_base_of`](https://en.cppreference.com/cpp/types/is_base_of) / [`std::derived_from`](https://en.cppreference.com/cpp/concepts/derived_from) | Partial: [`boost::is_base_of`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_base_of.hpp) | Partial: [`bsl::is_base_of`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`  /  `[`is_convertible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuBase`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Constraint on strict accessible base conversion | Partial: [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) | Composition: [`bsl::enable_if`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` + normalized identity/pointer-conversion tests + original-type inequality` |
| [`ZuNotBase`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Negated strict accessible base-conversion constraint | Partial: [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) | Composition: [`bsl::enable_if`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)` + negation of the strict identity/pointer-conversion test` |

## Conversion, construction and exception traits

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuIsConvertible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) — reference-normalized probe semantics | Partial: [`std::is_convertible`](https://en.cppreference.com/cpp/types/is_convertible) | Partial: [`boost::is_convertible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp) | [`bsl::is_convertible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuConvertible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<`[`std::is_convertible_v`](https://en.cppreference.com/cpp/types/is_convertible)`<From, To>, R>` | Partial: [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_convertible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_convertible_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<From, To>, R>` |
| [`ZuNotConvertible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: negated [`std::is_convertible`](https://en.cppreference.com/cpp/types/is_convertible) constraint | Partial: [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_convertible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!`[`bsl::is_convertible_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<From, To>, R>` |
| [`ZuIsConstructible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➖ source/destination API and cast probing differ | Partial: [`std::is_constructible`](https://en.cppreference.com/cpp/types/is_constructible)<br>➕ variadic constructor-argument API | Partial: [`boost::is_constructible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_constructible.hpp) | [`bsl::is_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuConstructible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if)`<`[`std::is_constructible_v`](https://en.cppreference.com/cpp/types/is_constructible)`<To, From>, R>` | Partial: [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_constructible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_constructible.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<`[`bsl::is_constructible_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<To, From>, R>` |
| [`ZuNotConstructible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: negated constructibility constraint | Partial: [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + [`boost::is_constructible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_constructible.hpp) | Composition: [`bsl::enable_if_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<!`[`bsl::is_constructible_v`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`<To, From>, R>` |
| [`ZuNXConstruct`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::is_nothrow_default_constructible`](https://en.cppreference.com/cpp/types/is_default_constructible) | Partial: [`boost::has_nothrow_constructor`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/has_nothrow_constructor.hpp) | [`bsl::is_nothrow_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuNXMove`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::is_nothrow_move_constructible`](https://en.cppreference.com/cpp/types/is_move_constructible) | Partial: [`boost::is_nothrow_move_constructible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_nothrow_move_constructible.hpp) | [`bsl::is_nothrow_move_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuNXCopy`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::is_nothrow_copy_constructible`](https://en.cppreference.com/cpp/types/is_copy_constructible) | Partial: [`boost::has_nothrow_copy`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/has_nothrow_copy.hpp) | [`bsl::is_nothrow_copy_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuNXDestroy`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Partial: [`std::is_nothrow_destructible`](https://en.cppreference.com/cpp/types/is_destructible) | Partial: [`boost::has_nothrow_destructor`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/has_nothrow_destructor.hpp) | [`bsl::is_nothrow_destructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |

## Conditional types and compile-time queries

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuIf`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::conditional_t`](https://en.cppreference.com/cpp/types/conditional) | [`boost::mp11::mp_if_c`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | [`folly::conditional_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) | [`bsl::conditional`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuIfT`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::enable_if_t`](https://en.cppreference.com/cpp/types/enable_if) | [`boost::enable_if_c`](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp)`<...>::type` | N/A | [`bsl::enable_if`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuDeclVal`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::declval`](https://en.cppreference.com/cpp/utility/declval) | [`boost::declval`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/declval.hpp) | Partial: [`FOLLY_DECLVAL`](https://github.com/facebook/folly/blob/main/folly/Utility.h)<br>➕ avoids function-template instantiation<br>➖ value-category/completeness rules differ | [`bslmf::Util::declval`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h) |
| [`ZuConstEval`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L753) | [`std::is_constant_evaluated`](https://en.cppreference.com/cpp/types/is_constant_evaluated) | N/A; use the standard facility or compiler intrinsic | N/A | [`bsl::is_constant_evaluated`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuSize`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ zero logical size for void/empty types; ordinary sizeof otherwise | Partial: [`std::is_empty`](https://en.cppreference.com/cpp/types/is_empty) with custom logical-size calculation | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) + `sizeof` | N/A | Partial: [`bsl::is_empty`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuLargest`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLargest.hh)<br>➕ selects a type from a pack or [ZuTypeList](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh); first wins ties; handles void | N/A; size measurement alone does not select a type | Partial: [Boost.Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) reduction by `sizeof` | N/A | N/A |
| [`ZuMostAligned`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMostAligned.hh)<br>➕ selects the most-aligned type from a pack or [ZuTypeList](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh); first wins ties | N/A; alignment measurement alone does not select a type | Partial: [Boost.Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) reduction by `alignof` | N/A | N/A |
| [`ZuTraits`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTraits.hh)<br>➕ common ADL vocabulary for strings, containers and numerics | Partial: [`<type_traits>`](https://en.cppreference.com/cpp/header/type_traits), [`std::ranges`](https://en.cppreference.com/cpp/ranges) concepts, [`std::numeric_limits`](https://en.cppreference.com/cpp/types/numeric_limits) | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html), [Range](https://github.com/boostorg/range/blob/master/include/boost/range.hpp), NumericConversion | Partial: [`folly::IsRelocatable / detection traits`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ no unified Z-style string/container/numeric trait vocabulary | Partial: `BSL type `[`traits`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntccfg/ntccfg_traits.h)`  +  `[`bdlat_TypeCategory`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h) |

## Compiler hints

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuLikely`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L138)<br>➕ expression-level branch hint, usable inside compound conditions | N/A; C++23 likelihood attributes apply to statements/labels | [`BOOST_LIKELY`](https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp) | [`FOLLY_LIKELY`](https://github.com/facebook/folly/blob/main/folly/Likely.h) | [`BSLS_PERFORMANCEHINT_PREDICT_LIKELY`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_performancehint.h) |
| [`ZuUnlikely`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L139)<br>➕ expression-level branch hint, usable inside compound conditions | N/A; C++23 likelihood attributes apply to statements/labels | [`BOOST_UNLIKELY`](https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp) | [`FOLLY_UNLIKELY`](https://github.com/facebook/folly/blob/main/folly/Likely.h) | [`BSLS_PERFORMANCEHINT_PREDICT_UNLIKELY`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_performancehint.h) |
| [`ZuInline`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ requests always_inline on supported compilers | N/A; the inline keyword does not request forced optimizer inlining | [`BOOST_FORCEINLINE`](https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp) | [`FOLLY_ALWAYS_INLINE`](https://github.com/facebook/folly/blob/main/folly/CPortability.h) | N/A |
| [`ZuNoInline`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | N/A; compiler attributes | [`BOOST_NOINLINE`](https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp) | [`FOLLY_NOINLINE`](https://github.com/facebook/folly/blob/main/folly/CPortability.h) | N/A |
| [`ZuUnreachable`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L145)<br>➕ debug build aborts at the failed assumption | [`std::unreachable`](https://en.cppreference.com/cpp/utility/unreachable) (C++23) | [`BOOST_UNREACHABLE_RETURN`](https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp) (partial return-site idiom) | [`FOLLY_ASSUME_UNREACHABLE`](https://github.com/facebook/folly/blob/main/folly/lang/Assume.h) | N/A |

## Assertions, declarations and preprocessor utilities

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuAssert`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAssert.hh#L28)<br>➕ deferred [ZuPP](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh) expansion and optional expression-text diagnostics | N/A; wraps core static_assert with macro-expansion support | [`BOOST_STATIC_ASSERT`](https://github.com/boostorg/config/blob/master/include/boost/static_assert.hpp), [`BOOST_STATIC_ASSERT_MSG`](https://github.com/boostorg/config/blob/master/include/boost/static_assert.hpp) | N/A | Partial: [`BSLMF_ASSERT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_assert.h) |
| [`ZuCAssert`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAssert.hh#L32)<br>➕ shorter spelling for a nonzero compile-time assertion<br>➖ older switch/case idiom; no semantic advantage over static_assert | N/A; core static_assert supplies the assertion itself | [`BOOST_STATIC_ASSERT`](https://github.com/boostorg/config/blob/master/include/boost/static_assert.hpp) | N/A | Partial: [`BSLMF_ASSERT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_assert.h) |
| [`ZuOpBool`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ emits a truth conversion from the existing operator! without repeating its logic<br>➖ pointer conversion permits more conversions than explicit bool | N/A; a conversion operator still needs a user-provided body | Partial: [`BOOST_EXPLICIT_OPERATOR_BOOL`](https://github.com/boostorg/core/blob/master/include/boost/core/explicit_operator_bool.hpp) | N/A | Partial: [`bsls::UnspecifiedBool`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_unspecifiedbool.h) |
| [`ZuDerive`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDerive.hh#L61)<br>➕ short distinct type ID reduces complex template names in symbols<br>➕ generates constrained forwarding construction and derived-returning assignment | N/A; inheritance alone does not generate this forwarding interface | Partial: [Boost.StrongTypedef](https://github.com/boostorg/serialization/blob/master/include/boost/serialization/strong_typedef.hpp)<br>➖ different wrapper/conversion semantics | N/A | N/A |
| [`ZuPP`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh) macro family<br>➕ deferred recursive expansion, tuple manipulation and argument mapping | N/A; the preprocessor supplies primitives, not these macro algorithms | [Boost.Preprocessor](https://www.boost.org/doc/libs/latest/libs/preprocessor/doc/index.html)<br>➕ broader documented macro algorithms | Partial: [`FOLLY_PP_*`](https://github.com/facebook/folly/blob/main/folly/Preprocessor.h)<br>➖ different macro vocabulary | Partial: [`BSLS_MACROREPEAT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_macrorepeat.h)<br>➖ repetition macros do not provide [ZuPP](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh)-style deferred recursive expansion |
| [`ZuIfReserved`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuReserved.hh#L117)<br>➕ branches on C++23 keyword tokens | N/A | Partial: [Boost.Preprocessor](https://www.boost.org/doc/libs/latest/libs/preprocessor/doc/index.html) token dispatch; custom keyword table | N/A | N/A |
| [`ZuFnName`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFnName.hh) | [`std::source_location::function_name`](https://en.cppreference.com/cpp/utility/source_location/function_name)`()` | [`BOOST_CURRENT_FUNCTION`](https://github.com/boostorg/assert/blob/master/include/boost/current_function.hpp) | N/A | N/A |

## Object storage and lifetime operations

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuLaunder`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L183) | [`std::launder`](https://en.cppreference.com/cpp/utility/launder) | [`boost::core::launder`](https://github.com/boostorg/core/blob/master/include/boost/core/launder.hpp) | N/A |
| [`ZuAddr`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L758) | [`std::addressof`](https://en.cppreference.com/cpp/memory/addressof) | [`boost::addressof`](https://github.com/boostorg/core/blob/master/include/boost/core/addressof.hpp) | [`bsl::addressof`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h) |
| [`ZuNew`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L776) | [`std::construct_at`](https://en.cppreference.com/cpp/memory/construct_at) | Partial: placement `new`; [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) allocator construction | Partial: [`bslma::ConstructionUtil::construct`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_constructionutil.h)<br>➕ allocator-aware construction |
| [`ZuCast`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L783) | [`std::bit_cast`](https://en.cppreference.com/cpp/numeric/bit_cast) | [`boost::core::bit_cast`](https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp) | N/A |
| [`ZuPun`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ reusable union with forwarding input construction and suppressed member destruction<br>➖ caller manages active-member lifetime and aliasing | Partial: [`std::bit_cast`](https://en.cppreference.com/cpp/numeric/bit_cast)<br>➕ defined value conversion for eligible types<br>➖ does not provide shared storage or lifetime control | Partial: [`boost::core::bit_cast`](https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp) | N/A |
| [`ZuAlloca`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L642)<br>➕ concise aligned stack allocation with runtime size<br>➖ no stack-capacity fallback | N/A; runtime-sized stack arrays are not a core C++23 facility | N/A; platform `alloca` remains nonstandard | N/A |
| [`ZuClear`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L127)<br>➕ OS-backed clearing resistant to dead-store elimination | N/A in C++23; [`std::memset`](https://en.cppreference.com/cpp/string/byte/memset) does not guarantee secure erasure | N/A; ordinary fill algorithms do not guarantee secure erasure | N/A |
| [`ZuCanOverlap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ shorter framework spelling of the standard attribute<br>➖ naming alias only; identical attribute semantics | N/A; exact alias of core [[no_unique_address]] | [`BOOST_ATTRIBUTE_NO_UNIQUE_ADDRESS`](https://github.com/boostorg/config/blob/master/include/boost/config/detail/suffix.hpp) | N/A |

## Array element storage and access

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuElem`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh)<br>➕ aligned structural union with no initial active element and explicit lifetime control<br>➕ short underlying type ID limits symbol/debug-name expansion | Partial: [`std::construct_at`](https://en.cppreference.com/cpp/memory/construct_at)<br>➖ caller must supply suitable storage and lifetime management | Partial: [Boost.Optional](https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp) storage machinery; no matching public wrapper | Partial: [`bsls::ObjectBuffer`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_objectbuffer.h) |
| [`ZuIsElem`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh) | N/A; custom-wrapper type trait | N/A; custom-wrapper type trait | N/A |
| [`ZuElemType`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh) | N/A; custom-wrapper unwrapping trait | N/A; custom-wrapper unwrapping trait | N/A |
| [`ZuElemVal`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh) | N/A; conditional wrapper-value access | N/A; custom wrapper accessor | N/A |

## Primitive numeric types and traits

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`int128_t`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh), [`uint128_t`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh) ([`ZuInt`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)`.hh`)<br>➕ native compiler integer operations on supported targets<br>➖ compiler-extension types | N/A; no required standard 128-bit integer | [`boost::multiprecision::int128_t`](https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp), [`uint128_t`](https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp)<br>➕ portable fixed-width backends | [`folly::int128_t / uint128_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) | N/A |
| [`ZuFP`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFP_.hh)<br>➕ IEEE representation and precision helpers | [`std::numeric_limits`](https://en.cppreference.com/cpp/types/numeric_limits), [`<cmath>`](https://en.cppreference.com/cpp/header/cmath) | [Boost.Math](https://github.com/boostorg/math/blob/master/include/boost/math/special_functions.hpp) floating-point utilities | N/A | Partial: [`bsl::numeric_limits`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_limits.h)`  +  `[`bdlb::Float`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_float.h) |

## Checked arithmetic and floating-point intrinsics

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`Zu_add`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L179) / [`ZuIntrin::add`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L242) | N/A in C++23; checked-addition composition | Partial: [Boost.SafeNumerics](https://www.boost.org/doc/libs/latest/libs/safe_numerics/doc/html/index.html)<br>➕ broader checked-number policies | Partial: [`folly::checked_add`](https://github.com/facebook/folly/blob/main/folly/lang/CheckedMath.h)<br>➖ reports success rather than Z overflow-flag semantics | N/A |
| [`Zu_sub`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L180) / [`ZuIntrin::sub`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L244) | N/A in C++23; checked-subtraction composition | Partial: [Boost.SafeNumerics](https://www.boost.org/doc/libs/latest/libs/safe_numerics/doc/html/index.html) | N/A | N/A |
| [`Zu_mul`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L181) / [`ZuIntrin::mul`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L246) | N/A in C++23; checked-multiplication composition | Partial: [Boost.SafeNumerics](https://www.boost.org/doc/libs/latest/libs/safe_numerics/doc/html/index.html) | Partial: [`folly::checked_mul`](https://github.com/facebook/folly/blob/main/folly/lang/CheckedMath.h)<br>➖ reports success rather than Z overflow-flag semantics | N/A |
| [`Zu_nan*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh) / [`ZuIntrin::nan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L250) | [`std::numeric_limits<T>::quiet_NaN`](https://en.cppreference.com/cpp/types/numeric_limits/quiet_NaN), [`std::nan`](https://en.cppreference.com/cpp/numeric/math/nan) | [Boost.Math](https://github.com/boostorg/math/blob/master/include/boost/math/special_functions.hpp) floating-point utilities | N/A | N/A |
| [`Zu_isnan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L197) / [`ZuIntrin::isnan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L258) | [`std::isnan`](https://en.cppreference.com/cpp/numeric/math/isnan) | [`boost::math::isnan`](https://github.com/boostorg/math/blob/master/include/boost/math/special_functions/fpclassify.hpp) | N/A | [`bdlb::Float::isNan`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_float.h) |

## Byte order and byte swapping

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuByteSwap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh)<br>➕ swapped-storage wrapper | Partial: [`std::byteswap`](https://en.cppreference.com/cpp/numeric/byteswap) (C++23)<br>➖ operation rather than storage wrapper | [Boost.Endian](https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html) arithmetic/buffer types | Partial: [`folly::Endian::swap`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ conversion operation, not endian-storage wrapper | [`bsls::ByteOrderUtil::swapBytes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorderutil.h) |
| [`ZuBigEndian`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh) | Partial: [`std::endian`](https://en.cppreference.com/cpp/types/endian) + [`std::byteswap`](https://en.cppreference.com/cpp/numeric/byteswap) | [Boost.Endian](https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html) big-endian types | Partial: [`folly::Endian::big`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ conversion operation, not endian-storage wrapper | Partial: [`bdlb::BigEndianInt16`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bigendian.h)`  / Int32 /  `[`Int64`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_int64.h)<br>➖ integer storage wrappers stop at 64 bits; Z also supports 128-bit values |
| [`ZuLittleEndian`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh) | Partial: [`std::endian`](https://en.cppreference.com/cpp/types/endian) + [`std::byteswap`](https://en.cppreference.com/cpp/numeric/byteswap) | [Boost.Endian](https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html) little-endian types | Partial: [`folly::Endian::little`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ conversion operation, not endian-storage wrapper | Partial: [`BSLS_BYTEORDER_LE_*`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorder.h)` conversion macros`<br>➖ byte-order conversions only; no corresponding little-endian storage wrapper |
| [`Zu_bswap*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh) ([`ZuIntrin`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh)`.hh`) | [`std::byteswap`](https://en.cppreference.com/cpp/numeric/byteswap) (C++23) | [`boost::endian::endian_reverse`](https://github.com/boostorg/endian/blob/master/include/boost/endian/conversion.hpp) | [`folly::Endian::swap`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h) | [`bsls::ByteOrderUtil::swapBytes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorderutil.h) |

## I/O status and demangling

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`Zu::IOResult`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIOResult.hh)<br>➕ small normalized I/O status vocabulary | Partial: [`std::error_code`](https://en.cppreference.com/cpp/error/error_code), stream state flags | Partial: [Boost.System](https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html) / [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) error codes | N/A | N/A |
| [`ZuDemangle`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDemangle.hh) | N/A; [`typeid(T).name()`](https://en.cppreference.com/cpp/types/type_info/name) does not guarantee readable text | [`boost::core::demangle`](https://github.com/boostorg/core/blob/master/include/boost/core/demangle.hpp), [Boost.TypeIndex](https://www.boost.org/doc/libs/latest/doc/html/boost_typeindex.html) | [`folly::demangle`](https://github.com/facebook/folly/blob/main/folly/Demangle.h) | Partial: [`bsls::NameOf`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_nameof.h)` / nameOfType` |

## C interfaces

| Z facility | STL | Boost |
| --- | --- | --- |
| [`zu_bitmap.h`](https://github.com/kaldron-labs/z/blob/main/zu/src/zu_bitmap.h) C API | N/A; [`std::bitset`](https://en.cppreference.com/cpp/utility/bitset) is C++ only | N/A; Boost bitsets are C++ only |
| [`zu_decimal.h`](https://github.com/kaldron-labs/z/blob/main/zu/src/zu_decimal.h) C API | N/A | N/A; [Boost.Multiprecision](https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp) is C++ only |
| [`zu_time.h`](https://github.com/kaldron-labs/z/blob/main/zu/src/zu_time.h) C API | Partial: C [`<time.h>`](https://en.cppreference.com/cpp/header/ctime) | N/A; Boost time libraries are C++ only |

## Callable construction and invocation

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuDefaultAxor`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L652) | [`std::identity`](https://en.cppreference.com/cpp/utility/functional/identity) | [`boost::hana::id`](https://github.com/boostorg/hana/blob/master/include/boost/hana/functional/id.hpp) | N/A | N/A |
| [`ZuLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ recursive callable wrapper with cv/ref-sensitive SFINAE overloads<br>➕ documented workaround for GCC explicit-object-parameter issue 113563 | N/A; explicit-object lambda syntax supplies recursion, not this wrapper interface | [`boost::hof::fix`](https://github.com/boostorg/hof/blob/master/include/boost/hof/fix.hpp) | N/A | N/A |
| [`ZuDeduce`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDeduce.hh)<br>➕ extracts object, return type and argument typelist | Partial: [`std::invoke_result_t`](https://en.cppreference.com/cpp/types/result_of)<br>➖ requires supplied argument types; no full signature decomposition | [Boost.CallableTraits](https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html)<br>➕ broader callable-type transformations | N/A | N/A |
| [`ZuInvoke`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInvoke.hh#L35)<br>➕ accepts member, bound and unbound function shapes with optional context | Partial: [`std::invoke`](https://en.cppreference.com/cpp/utility/functional/invoke)<br>➖ does not discard an extra context argument automatically | Partial: [`boost::compat::invoke`](https://github.com/boostorg/compat/blob/master/include/boost/compat/invoke.hpp) | Partial: [`folly::invoke`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h)<br>➖ does not automatically discard an extra context argument | Partial: [`bsl::invoke`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)<br>➖ does not automatically discard an unused context argument |
| [`ZuInvokeFnT`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | N/A; construct a function-pointer type from a callable signature | Partial: [Boost.CallableTraits](https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html) function-type transformation | N/A | N/A |
| [`ZuInvokeFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh#L175)<br>➕ concise named function-pointer conversion with signature deduction from lambda traits<br>➖ conversion semantics are already supplied by captureless lambdas | N/A; core lambda conversion supplies the underlying operation | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html)-selected function-pointer conversion | N/A | N/A |
| [`ZuInvokeLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh#L201)<br>➕ type-only invocation with an explicit statelessness check and argument-list traits<br>➖ core construction/call already covers captureless lambdas<br>➖ placeholder-this implementation relies on supported compiler behavior | N/A; core captureless-lambda construction/call lacks the explicit statelessness check | Partial: [`boost::hof::static_`](https://github.com/boostorg/hof/blob/master/include/boost/hof/static.hpp) | N/A | N/A |

## Lambda traits and constraints

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuLambdaTraits`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh)<br>➕ generic-lambda invocation and capture-oriented traits | Partial: [`std::is_invocable`](https://en.cppreference.com/cpp/types/is_invocable), [`std::invoke_result`](https://en.cppreference.com/cpp/types/result_of), type traits | Partial: [Boost.CallableTraits](https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html), [TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) | Partial: [`folly::invoke_traits`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h)<br>➖ invocation traits rather than full lambda argument/capture decomposition | Partial: [`bslmf::MemberFunctionPointerTraits`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_memberfunctionpointertraits.h) |
| [`ZuArgList`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | N/A; signature decomposition requires custom traits | [`boost::callable_traits::args_t`](https://github.com/boostorg/callable_traits/blob/master/include/boost/callable_traits/args.hpp) (partial for generic lambdas) | N/A | Partial: [`bslmf::MemberFunctionPointerTraits`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_memberfunctionpointertraits.h) |
| [`ZuLambdaReturn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | [`std::invoke_result_t`](https://en.cppreference.com/cpp/types/result_of) with supplied argument types | [`boost::callable_traits::return_type_t`](https://github.com/boostorg/callable_traits/blob/master/include/boost/callable_traits/return_type.hpp) (partial for generic lambdas) | Partial: [`folly::invoke_result_t`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h) | [`bsl::invoke_result`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuIsMutableLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Partial: test invocability through a const callable reference | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) / [CallableTraits](https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html) composition | N/A | N/A |
| [`ZuIsVoidRetLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | [`std::is_void`](https://en.cppreference.com/cpp/types/is_void)`<`[`std::invoke_result_t`](https://en.cppreference.com/cpp/types/result_of)`<...>>` | Partial: [Boost.TypeTraits](https://www.boost.org/doc/libs/latest/libs/type_traits/doc/html/boost_typetraits/reference.html) + [CallableTraits](https://www.boost.org/doc/libs/latest/libs/callable_traits/doc/html/callable_traits/reference.html) | Partial: [`folly::invoke_result_t`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h)<br>➖ needs a separate void-type test | Partial: [`bsl::invoke_result`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`  +  `[`is_void`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| [`ZuIsStatelessLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh)<br>➕ tests function-pointer conversion for the selected signature | Partial: [`std::is_convertible`](https://en.cppreference.com/cpp/types/is_convertible) to the selected function pointer<br>➖ [`std::is_empty`](https://en.cppreference.com/cpp/types/is_empty) alone is not an equivalent test | Partial: [`boost::is_convertible`](https://github.com/boostorg/type_traits/blob/master/include/boost/type_traits/is_convertible.hpp) to the selected function pointer | N/A | N/A |
| [`ZuMutableLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Constraint on const-callability test | Partial: [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + callable traits | N/A | N/A |
| [`ZuNotMutableLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Negated const-callability constraint | Partial: [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + callable traits | N/A | N/A |
| [`ZuVoidRetLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Constraint on void invocation result | Partial: [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + result-type traits | N/A | N/A |
| [`ZuNotVoidRetLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Constraint on non-void invocation result | Partial: [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + result-type traits | N/A | N/A |
| [`ZuStatelessLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Constraint on function-pointer convertibility | Partial: [Boost.EnableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + conversion traits | N/A | N/A |
| [`ZuNotStatelessLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Negated function-pointer-conversion constraint | Partial: [Boost.DisableIf](https://github.com/boostorg/core/blob/master/include/boost/core/enable_if.hpp) + conversion traits | N/A | N/A |

## Compile-time switch and unroll

| Z facility | STL | Boost |
| --- | --- | --- |
| [`ZuSwitch`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSwitch.hh)<br>➕ generates dispatch cases from a count or sequence, passing a compile-time index<br>➕ supports result collection and a caller-supplied default value | N/A; a switch statement does not generate its own cases | [`boost::mp11::mp_with_index`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) |
| [`ZuUnroll`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnroll.hh)<br>➕ generates ordered calls from a count, sequence or type list<br>➕ shared API supports result collection and seeded reduction | N/A; pack expansion requires pack extraction/generation and call adaptation | [`boost::mp11::mp_for_each`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html), [`boost::hana::for_each`](https://github.com/boostorg/hana/blob/master/include/boost/hana/for_each.hpp) |

## Constant sequences and dispatch

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | [`std::integer_sequence`](https://en.cppreference.com/cpp/utility/integer_sequence)`<unsigned, ...>` | [`boost::mp11::mp_list_c`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html)`<unsigned, ...>` | Partial: [`folly::vtag_t<I...>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ value-list tag with different operations | [`bslmf::IntegerSequence`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integersequence.h)`<unsigned, ...>` |
| [`ZuMkSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | [`std::make_integer_sequence`](https://en.cppreference.com/cpp/utility/integer_sequence)`<unsigned, N>` | [`boost::mp11::mp_iota_c`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) with value-type adapter | N/A | [`bslmf::MakeIntegerSequence`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_makeintegersequence.h)`<unsigned, N>` |
| [`ZuMin`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Partial: constexpr [`std::min`](https://en.cppreference.com/cpp/algorithm/min) over constant values | [`boost::mp11::mp_min`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | Partial: [`folly::constexpr_min`](https://github.com/facebook/folly/blob/main/folly/ConstexprMath.h)<br>➖ value reduction rather than [ZuSeq](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) metafunction | Partial: [`bsl::min`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h) |
| [`ZuMax`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Partial: constexpr [`std::max`](https://en.cppreference.com/cpp/algorithm/max) over constant values | [`boost::mp11::mp_max`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | Partial: [`folly::constexpr_max`](https://github.com/facebook/folly/blob/main/folly/ConstexprMath.h)<br>➖ value reduction rather than [ZuSeq](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) metafunction | Partial: [`bsl::max`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h) |
| [`ZuSeqCall`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Partial: [`std::apply`](https://en.cppreference.com/cpp/utility/apply) with index/element adaptation | Partial: [`boost::hana::unpack`](https://github.com/boostorg/hana/blob/master/include/boost/hana/unpack.hpp) | Partial: [`folly::apply`](https://github.com/facebook/folly/blob/main/folly/functional/ApplyTuple.h)<br>➖ no Z accessor/index adaptation | Partial: [`bsl::apply`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h) |

## Constant-sequence conversions

| Z facility | STL | Boost |
| --- | --- | --- |
| [`ZuSeqBitmap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L61)<br>➕ extracts sequence indices and combines their bits into a constexpr uint64_t mask | N/A; a fold expression requires a separately unpacked index pack | Partial: [Boost.Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) fold |
| [`ZuBitmapSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh)<br>➕ generates an index-sequence type from the set bits of a constant mask | N/A; integer bit operations do not generate a sequence type | Partial: [Boost.Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) filtering of bit indices |
| [`ZuSeqTL`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | N/A; convert an integer sequence into constant types | [`boost::mp11::mp_from_sequence`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) |
| [`ZuTLSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | N/A; unpack constant types into an integer sequence | Partial: [Boost.Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) pack adapter |
| [`ZuSeqString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L107)<br>➕ extracts sequence values and builds a structural [ZuString](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh) with a trailing terminator | Partial: [`std::array`](https://en.cppreference.com/cpp/container/array)`<char, N>` with a custom sequence-to-string adapter | Partial: [`boost::hana::string`](https://github.com/boostorg/hana/blob/master/include/boost/hana/string.hpp) |
| [`ZuStringSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | N/A; string-to-integer-pack adapter | Partial: [`boost::hana::unpack`](https://github.com/boostorg/hana/blob/master/include/boost/hana/unpack.hpp) on a [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) string |
| [`ZuSeqArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L128)<br>➕ extracts sequence values into a structural array of the selected element type | Partial: [`std::array`](https://en.cppreference.com/cpp/container/array) with a custom sequence-conversion adapter | Partial: [`boost::hana::unpack`](https://github.com/boostorg/hana/blob/master/include/boost/hana/unpack.hpp) into an array |
| [`ZuArraySeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | N/A; structural-array-to-integer-pack adapter | Partial: [Boost.Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) sequence conversion |

## Type-list construction, access and slicing

Sources: [ZuTL.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [ZuSeq.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh),
[ZuString.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh).
Boost counterparts: [Mp11 reference](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html).
Composed operations below require a small adapter where the list/value representation differs.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuTypeList`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ type-only list with nested operations | Partial: [`std::tuple`](https://en.cppreference.com/cpp/utility/tuple) as a type carrier | [`boost::mp11::mp_list`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | Partial: [`folly::tag_t<T...>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ lacks [ZuTypeList](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) nested push/unshift interface | Partial: [`bslmf::TypeList`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_typelist.h)`  (up to 20  `[`types`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_types.h)`)`; [`bsl::tuple`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h)`<T...> (variadic type-pack representation)` |
| [`ZuTypeRepeat`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh)<br>➕ generates an N-element type list from one type, including an empty result | N/A; parameter-pack syntax does not synthesize repeated types by itself | [`boost::mp11::mp_repeat_c`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) on a singleton list | N/A | N/A |
| [`ZuType`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | [`std::tuple_element_t`](https://en.cppreference.com/cpp/utility/tuple/tuple_element) on a tuple | [`boost::mp11::mp_at_c`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | [`folly::type_pack_element_t / type_list_element_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) | Partial: [`bsl::tuple_element_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h) |
| [`ZuTypeIndex`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; compile-time type search | [`boost::mp11::mp_find`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A | N/A |
| [`ZuTypeIn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh)<br>➕ membership test accepts a packed list or raw type arguments and handles empty input | Composition: [`std::is_same`](https://en.cppreference.com/cpp/types/is_same) with custom list unpacking and reduction | [`boost::mp11::mp_contains`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | Partial: [`folly::is_one_of_v`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ packed-list input needs adaptation | N/A |
| [`ZuTypeHead`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; prefix index-sequence adapter | [`boost::mp11::mp_take_c`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A | N/A |
| [`ZuTypeTail`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; suffix index-sequence adapter | [`boost::mp11::mp_drop_c`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A | N/A |
| [`ZuTypeSlice`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh)<br>➕ stride/offset selection | N/A; index-sequence selection | Partial: [`boost::mp11::mp_transform`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) over selected indices | N/A | N/A |
| [`ZuTypeApply`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh)<br>➕ applies an existing packed type list or raw type arguments to a target template | N/A; pack expansion does not unpack a type-list object/type by itself | [`boost::mp11::mp_rename`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) / [`mp_apply`](https://github.com/boostorg/mp11/blob/master/include/boost/mp11/list.hpp) | N/A | N/A |

## Type-list algorithms

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| [`ZuTypeRev`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; reversed index-sequence adapter | [`boost::mp11::mp_reverse`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A |
| [`ZuTypeMap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh)<br>➕ transforms a packed type list or raw type arguments and rebuilds the resulting list | N/A; requires a custom list-unpacking and transformation adapter | [`boost::mp11::mp_transform`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A |
| [`ZuTypeGrep`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; compile-time type filtering | [`boost::mp11::mp_copy_if`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A |
| [`ZuTypeReduce`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh)<br>➕ recursive pairwise type reduction with empty/singleton cases and packed-list input | N/A; ordinary fold expressions do not directly reduce type metafunctions | Partial: [`boost::mp11::mp_fold`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A |
| [`ZuTypeMerge`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; compile-time merge | Partial: [`boost::mp11::mp_append`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) + [`mp_sort`](https://github.com/boostorg/mp11/blob/master/include/boost/mp11/algorithm.hpp) | N/A |
| [`ZuTypeSort`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; compile-time type sorting | [`boost::mp11::mp_sort`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) with comparator adapter | N/A |
| [`ZuTypeConcat`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Partial: result type of [`std::tuple_cat`](https://en.cppreference.com/cpp/utility/tuple/tuple_cat) | [`boost::mp11::mp_append`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | [`folly::type_list_concat_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) |
| [`ZuTypeUnique`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | N/A; compile-time type deduplication | [`boost::mp11::mp_unique`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) | N/A |
| [`ZuTLConverts`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Composition: zipped [`std::is_convertible`](https://en.cppreference.com/cpp/types/is_convertible) tests | Composition: [`boost::mp11::mp_transform`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) + [`mp_all`](https://github.com/boostorg/mp11/blob/master/include/boost/mp11/algorithm.hpp) | N/A |
| [`ZuTLConstructs`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Composition: zipped [`std::is_constructible`](https://en.cppreference.com/cpp/types/is_constructible) tests | Composition: [`boost::mp11::mp_transform`](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) + [`mp_all`](https://github.com/boostorg/mp11/blob/master/include/boost/mp11/algorithm.hpp) | N/A |

## Compile-time strings

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| [`ZuString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh)<br>➕ structural string for non-type template arguments | Partial: [`std::array`](https://en.cppreference.com/cpp/container/array)`<char, N>`<br>➖ no dedicated structural string class | Partial: [`boost::hana::string`](https://github.com/boostorg/hana/blob/master/include/boost/hana/string.hpp)<br>➖ different template representation | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h)<br>➖ different template-argument/structural-type contract |
| [`ZuStringT`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh) | Partial: [`std::integral_constant`](https://en.cppreference.com/cpp/types/integral_constant) of a structural string | Partial: [`boost::hana::string`](https://github.com/boostorg/hana/blob/master/include/boost/hana/string.hpp) as a type-level string | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h)<br>➖ value container rather than [ZuStringT](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh) type-level wrapper |
| [`ZuStringTL`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh) | N/A; type list of structural-string constants | Partial: [Boost.Mp11](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html) list of [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) strings | N/A |

## Ownership and scope cleanup

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuObject`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuObject.hh)<br>➕ embedded non-atomic count<br>➖ participating base class required | Partial: [`std::enable_shared_from_this`](https://en.cppreference.com/cpp/memory/enable_shared_from_this) + shared ownership<br>➕ supports weak observers | [`boost::intrusive_ref_counter`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp)`<T, `[`boost::thread_unsafe_counter`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp)`>` | N/A | N/A |
| [`ZuObjectTraits`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuObjectTraits.hh)<br>➕ detects Z intrusive-object participation | N/A; custom type traits | Partial: detection of intrusive pointer hooks | N/A | N/A |
| [`ZuRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuRef.hh)<br>➕ intrusive ownership without separate control block<br>➖ non-atomic; no weak observers | [`std::shared_ptr`](https://en.cppreference.com/cpp/memory/shared_ptr)<br>➕ arbitrary pointees and weak observers | [`boost::intrusive_ptr`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ptr.hpp) with non-atomic hooks | N/A | N/A |
| [`ZuPolymorph`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPolymorph.hh) | Partial: polymorphic base with [`std::shared_ptr`](https://en.cppreference.com/cpp/memory/shared_ptr) | Partial: polymorphic base with Boost intrusive hooks | N/A | N/A |
| [`ZuPtr`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPtr.hh)<br>➖ no configurable deleter | [`std::unique_ptr`](https://en.cppreference.com/cpp/memory/unique_ptr)<br>➕ custom deleters and array specialization | [`boost::movelib::unique_ptr`](https://github.com/boostorg/move/blob/master/include/boost/move/unique_ptr.hpp) | Partial: [`folly::static_function_deleter`](https://github.com/facebook/folly/blob/main/folly/Memory.h)<br>➖ a deleter policy used with unique_ptr, not an owning pointer by itself | Partial: [`bslma::ManagedPtr`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_managedptr.h)<br>➕ type-erased deleter and allocator-aware factories |
| [`ZuGuard`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh)<br>➕ cancellable lambda cleanup | N/A in C++23; [`std::experimental::scope_exit`](https://en.cppreference.com/cpp/experimental/scope_exit) (Library Fundamentals TS) | [`boost::scope::scope_exit`](https://github.com/boostorg/scope/blob/master/include/boost/scope/scope_exit.hpp)<br>➕ related success/failure guards | [`folly::ScopeGuard / makeGuard`](https://github.com/facebook/folly/blob/main/folly/ScopeGuard.h)<br>➕ also supplies success/failure scope guards | [`bdlb::ScopeExit`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_scopeexit.h)`  /  `[`ScopeExitUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_scopeexit.h) |

## Comparison, hashing and null sentinels

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuCmp`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh)<br>➕ comparison, null sentinel and numeric bounds in one customization | Partial: [`std::compare_three_way`](https://en.cppreference.com/cpp/utility/compare/compare_three_way), [`std::numeric_limits`](https://en.cppreference.com/cpp/types/numeric_limits), [`std::optional`](https://en.cppreference.com/cpp/utility/optional) | Partial: [Boost.Operators](https://www.boost.org/doc/libs/latest/libs/utility/operators.htm), NumericConversion, [Optional](https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp) | N/A | N/A |
| [`ZuCmp0`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh)<br>➕ zero-sentinel comparison policy | Partial: comparison functions + zero test | Partial: [Boost.Operators](https://www.boost.org/doc/libs/latest/libs/utility/operators.htm) + custom sentinel policy | N/A | N/A |
| [`ZuCmp_1`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh)<br>➕ negative-value null policy | N/A; custom negative-sentinel comparison policy | N/A; custom sentinel policy | N/A | N/A |
| [`ZuCmpN`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh)<br>➕ compile-time chosen null sentinel | N/A; custom sentinel comparison policy | N/A; custom sentinel policy | N/A | N/A |
| [`ZuCompare`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSemVer.hh) | Partial: [`std::compare_three_way`](https://en.cppreference.com/cpp/utility/compare/compare_three_way)<br>➖ returns an ordering category rather than an integer | Partial: Boost comparison utilities with integer-result adapter | N/A | Partial: [`bsl::compare_three_way`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_compare.h) |
| [`ZuEquals`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnion.hh) | [`std::equal_to`](https://en.cppreference.com/cpp/utility/functional/equal_to) | [`boost::equal_to`](https://github.com/boostorg/functional/blob/master/include/boost/functional.hpp) (Functional) | N/A | [`bsl::equal_to`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h) |
| [`ZuNull`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHash.hh)<br>➕ tests a type's configured sentinel | Partial: [`std::optional::has_value`](https://en.cppreference.com/cpp/utility/optional/operator_bool)<br>➕ does not consume a payload value as a sentinel | Partial: [`boost::optional`](https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp) engagement test | N/A | N/A |
| [`ZuNullRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh#L747)<br>➕ reusable const-reference sentinel accessor honoring the type-specific comparator | N/A; a static variable alone does not select the configured sentinel | N/A; application static sentinel | N/A | N/A |
| [`ZuICmp`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuICmp.hh) | Partial: character folding + comparison algorithms | [`boost::algorithm::iequals`](https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/predicate.hpp) / [`ilexicographical_compare`](https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/compare.hpp) | Partial: [`folly::AsciiCaseInsensitive`](https://github.com/facebook/folly/blob/main/folly/Range.h)<br>➖ comparator used with range search; not the full [ZuCmp](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh) interface | Partial: [`bdlb::String::areEqualCaseless`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_string.h)` / lowerCaseCmp` |
| [`ZuHash`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHash.hh)<br>➕ consistent text hashing across supported representations | [`std::hash`](https://en.cppreference.com/cpp/utility/hash) | [`boost::hash`](https://github.com/boostorg/container_hash/blob/master/include/boost/container_hash/hash.hpp) (ContainerHash) | [`folly::hasher / folly::hash`](https://github.com/facebook/folly/blob/main/folly/hash/Hash.h)<br>➕ multiple hash families and combination helpers | [`bslh::Hash`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslh/bslh_hash.h)` / hashAppend`; [`bdlb::TransparentStringHash`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_transparentstringhash.h)<br>➕ extensible hashing and heterogeneous string lookup |

## Bitmaps, bitfields and bit streams

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuBitmap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitmap.hh) | [`std::bitset`](https://en.cppreference.com/cpp/utility/bitset) | Partial: [`boost::dynamic_bitset`](https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp)<br>➕ runtime size<br>➖ dynamic allocation | N/A | [`bsl::bitset`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_bitset.h)`<N>` |
| [`ZuBitfield`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitfield.hh)<br>➕ runtime-indexed packed fields with explicit word/mask operations and assignment proxies | N/A; native named bitfields do not supply indexed packed-array access | Partial: [Boost.Endian](https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html) buffers with masks | N/A | N/A |
| [`ZuBitStream`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitStream.hh)<br>➕ successive non-byte-aligned symbols | N/A; custom bit packing | Partial: [Boost.DynamicBitset](https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp) with custom cursor | N/A | N/A |
| [`Zu_clz*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh) ([`ZuIntrin`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh)`.hh`) | [`std::countl_zero`](https://en.cppreference.com/cpp/numeric/countl_zero) | Partial: [Boost.Core](https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp) bit utilities | Partial: [`folly::findLastSet`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ convert its one-based result to a leading-zero count | [`bdlb::BitUtil::numLeadingUnsetBits`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bitutil.h) |
| [`Zu_ctz*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh) ([`ZuIntrin`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh)`.hh`) | [`std::countr_zero`](https://en.cppreference.com/cpp/numeric/countr_zero) | Partial: [Boost.Core](https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp) bit utilities | Partial: [`folly::findFirstSet`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ one-based position; zero-input convention differs | [`bdlb::BitUtil::numTrailingUnsetBits`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bitutil.h) |

## Boxed scalars and formatting adapters

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuBox`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh)<br>➕ scalar null policy with integrated scanning/formatting | Partial: scalar + [`std::optional`](https://en.cppreference.com/cpp/utility/optional) + character conversion | Partial: [Boost.Optional](https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp) + [Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) / LexicalCast | Partial: [`folly::Optional`](https://github.com/facebook/folly/blob/main/folly/Optional.h)<br>➖ separate engagement state; no integrated numeric formatting policy | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h) |
| [`ZuBoxed`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh#L730)<br>➕ scalar-reference formatting adapter | Partial: [`std::format`](https://en.cppreference.com/cpp/utility/format/format) argument / stream insertion | Partial: [Boost.Format](https://www.boost.org/doc/libs/latest/libs/format/doc/format.html) argument | N/A | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h) |
| [`ZuNBox`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh)<br>➕ boxed scalar with null-string formatting policy | Partial: [`std::optional`](https://en.cppreference.com/cpp/utility/optional) + custom formatter | Partial: [Boost.Optional](https://github.com/boostorg/optional/blob/master/include/boost/optional.hpp) + custom formatter | Partial: [`folly::Optional`](https://github.com/facebook/folly/blob/main/folly/Optional.h)<br>➖ separate engagement state; no integrated numeric formatting policy | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h) |
| [`ZuNBoxed`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh#L757) | Partial: scalar-reference adapter with custom null formatting | Partial: [Boost.Format](https://www.boost.org/doc/libs/latest/libs/format/doc/format.html) argument adapter | N/A | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h) |

## Arrays, spans and string interoperability

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zu/src/), [module guide](../zu/README.md).
Relevant counterparts: [standard containers](https://eel.is/c++draft/containers),
[Boost.SmartPtr](https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html),
[Boost.Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh)<br>➕ integrated compare/hash, slicing and text searching | [`std::span`](https://en.cppreference.com/cpp/container/span)<br>➕ standard ranges interoperability and static extent | [`boost::span`](https://github.com/boostorg/core/blob/master/include/boost/core/span.hpp) | [`folly::Range`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::span`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_span.h) |
| [`ZuCSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh) | [`std::string_view`](https://en.cppreference.com/cpp/string/basic_string_view) | [`boost::string_view`](https://github.com/boostorg/utility/blob/master/include/boost/utility/string_view.hpp) | [`folly::StringPiece`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::string_view`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string_view.h) |
| [`ZuWSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh) | [`std::wstring_view`](https://en.cppreference.com/cpp/string/basic_string_view) | [`boost::wstring_view`](https://github.com/boostorg/utility/blob/master/include/boost/utility/string_view.hpp) | [`folly::Range<const wchar_t*>`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::wstring_view`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string_view.h) |
| [`ZuBSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh) | [`std::span`](https://en.cppreference.com/cpp/container/span)`<const uint8_t>` / [`std::span`](https://en.cppreference.com/cpp/container/span)`<const `[`std::byte`](https://en.cppreference.com/cpp/types/byte)`>` | [`boost::span`](https://github.com/boostorg/core/blob/master/include/boost/core/span.hpp)`<const unsigned char>` | [`folly::ByteRange`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::span`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_span.h) |
| [`ZuArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh)<br>➕ inline capacity with logical length; structural representation | Partial: [`std::array`](https://en.cppreference.com/cpp/container/array)<br>➖ no logical length; [`std::inplace_vector`](https://en.cppreference.com/cpp/container/inplace_vector) is C++26 | [`boost::container::static_vector`](https://github.com/boostorg/container/blob/master/include/boost/container/static_vector.hpp)<br>➖ different structural/NTTP contract | Partial: [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h)<br>➖ different capacity/growth and structural-type contract | Partial: [`bsl::array`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_array.h)<br>➖ fixed size; no separate logical length within the inline capacity |
| [`ZuCArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh) | Partial: [`std::array`](https://en.cppreference.com/cpp/container/array)`<char, N>` with length | [`boost::static_string`](https://github.com/boostorg/static_string/blob/master/include/boost/static_string.hpp) | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h) | Partial: [`bsl::array`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_array.h)`<char, N>` + logical length; [`bsl::basic_string`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h)`<char>` |
| [`ZuWArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh) | Partial: [`std::array`](https://en.cppreference.com/cpp/container/array)`<wchar_t, N>` with length | [`boost::static_strings::basic_static_string`](https://github.com/boostorg/static_string/blob/master/include/boost/static_string/static_string.hpp)`<N, wchar_t>` | Partial: [`folly::BasicFixedString<wchar_t, N>`](https://github.com/facebook/folly/blob/main/folly/FixedString.h) | Partial: [`bsl::array`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_array.h)`<`[`wchar_t`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)`, N>` + logical length; [`bsl::basic_string`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h)`<wchar_t>` |
| [`ZuMArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMArray.hh)<br>➕ writable transformed-element proxies | Partial: [`std::views::transform`](https://en.cppreference.com/cpp/ranges/transform_view)<br>➖ inverse assignment needs custom proxy support | Partial: [Boost.Iterator](https://www.boost.org/doc/libs/latest/libs/iterator/doc/index.html) transform iterator | N/A | Partial: [`bdlb::TransformIterator`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_transformiterator.h) |
| [`ZuVArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuVArray.hh)<br>➕ type-erased array interface across compiled boundaries | N/A; [`std::span`](https://en.cppreference.com/cpp/container/span) only covers contiguous homogeneous storage | Partial: [Boost.Range](https://github.com/boostorg/range/blob/master/include/boost/range.hpp) any-range | N/A | Partial: [`bdlar::ArrayRef`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlar/bdlar_arrayref.h) |
| [`ZuStdString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStdString.hh) integration | [`std::basic_string`](https://en.cppreference.com/cpp/string/basic_string) used directly | Partial: Boost string/range interoperability | N/A | Partial: [`bsl::basic_string`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h) |

## Tuples, unions and empty values

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuTuple`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTuple.hh)<br>➕ size-ordered flat storage and positional mutation<br>➖ incomplete [`std::tuple`](https://en.cppreference.com/cpp/utility/tuple) algorithm interoperability | [`std::tuple`](https://en.cppreference.com/cpp/utility/tuple), [`std::pair`](https://en.cppreference.com/cpp/utility/pair) | [`boost::tuple`](https://github.com/boostorg/tuple/blob/master/include/boost/tuple/tuple.hpp), [`boost::hana::tuple`](https://github.com/boostorg/hana/blob/master/include/boost/hana/tuple.hpp) | N/A | Partial: [`bsl::tuple`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h) |
| [`ZuUnion`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnion.hh)<br>➕ void alternatives and explicit nonthrowing dispatch<br>➖ callers must respect active-alternative access contract | [`std::variant`](https://en.cppreference.com/cpp/utility/variant)<br>➕ checked [`get`](https://en.cppreference.com/cpp/utility/variant/get)/[`get_if`](https://en.cppreference.com/cpp/utility/variant/get_if) and standard visitation | [`boost::variant2::variant`](https://github.com/boostorg/variant2/blob/master/include/boost/variant2/variant.hpp)<br>➕ never-valueless value semantics | Partial: [`folly::Expected<T, E>`](https://github.com/facebook/folly/blob/main/folly/Expected.h)<br>➖ value/error pair, not an arbitrary multi-alternative union | [`bsl::variant`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_variant.h)<br>➕ allocator-aware alternatives and checked access |
| [`ZuVoid`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | [`std::monostate`](https://en.cppreference.com/cpp/utility/variant/monostate) | [`boost::blank`](https://www.boost.org/doc/libs/latest/doc/html/variant/reference.html) | N/A | Partial: [`bsl::monostate`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_variant.h) / [`bdlf::NoOp`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlf/bdlf_noop.h) |

## Array algorithms, iterators, sorting and searching

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuArrayFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArrayFn.hh)<br>➕ trait-selected bulk initialization, movement and comparison | [`<algorithm>`](https://en.cppreference.com/cpp/header/algorithm), uninitialized-memory algorithms | Partial: [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) / [Move](https://www.boost.org/doc/libs/latest/doc/html/move.html) algorithms | N/A |
| [`ZuStringFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStringFn.hh) | [`std::char_traits`](https://en.cppreference.com/cpp/string/char_traits), C string functions | Partial: [Boost.StringAlgo](https://www.boost.org/doc/libs/latest/doc/html/string_algo/reference.html) | N/A |
| [`ZuIter`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIter.hh)<br>➕ reusable random-access iterator boilerplate | Partial: iterator concepts and custom iterator implementation | [`boost::iterator_facade`](https://github.com/boostorg/iterator/blob/master/include/boost/iterator/iterator_facade.hpp) | N/A |
| [`ZuSort`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSort.hh#L139)<br>➕ three-way comparison and Z bulk-element operations<br>➖ custom quicksort lacks [`std::sort`](https://en.cppreference.com/cpp/algorithm/sort)'s mandated worst-case bound | [`std::sort`](https://en.cppreference.com/cpp/algorithm/sort) | [Boost.Sort](https://github.com/boostorg/sort/blob/master/include/boost/sort/sort.hpp) | [`bsl::sort`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h) |
| [`ZuSearch`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L23)<br>➕ packs found flag and insertion position into one result | [`std::lower_bound`](https://en.cppreference.com/cpp/algorithm/lower_bound) plus equality test | [Boost.Range](https://github.com/boostorg/range/blob/master/include/boost/range.hpp) [`lower_bound`](https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp) plus equality test | Partial: [`bsl::lower_bound`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)`  /  `[`binary_search`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h) |
| [`ZuSearchFound`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L12) | Partial: equality test after [`std::lower_bound`](https://en.cppreference.com/cpp/algorithm/lower_bound) | Partial: equality test after [Boost.Range](https://github.com/boostorg/range/blob/master/include/boost/range.hpp) [`lower_bound`](https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp) | Partial: [`bsl::lower_bound`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)`  /  `[`binary_search`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h) |
| [`ZuSearchPos`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L13) | Partial: iterator distance from [`std::lower_bound`](https://en.cppreference.com/cpp/algorithm/lower_bound) | Partial: iterator distance from [Boost.Range](https://github.com/boostorg/range/blob/master/include/boost/range.hpp) [`lower_bound`](https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp) | Partial: [`bsl::lower_bound`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)`  /  `[`binary_search`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h) |
| [`ZuInterSearch`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L66)<br>➕ interpolation search for suitably distributed keys<br>➖ behavior depends on key distribution | Partial: [`std::lower_bound`](https://en.cppreference.com/cpp/algorithm/lower_bound)<br>➕ logarithmic comparisons independent of distribution | Partial: [Boost.Range](https://github.com/boostorg/range/blob/master/include/boost/range.hpp) [`lower_bound`](https://github.com/boostorg/range/blob/master/include/boost/range/algorithm/lower_bound.hpp) | N/A |

## Text tokenization

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuTokenizer::Delimited`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTokenizer.hh)<br>➕ borrowed tokens without allocation | [`std::views::split`](https://en.cppreference.com/cpp/ranges/split_view) | [`boost::tokenizer`](https://github.com/boostorg/tokenizer/blob/master/include/boost/tokenizer.hpp), [`boost::split_iterator`](https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/find_iterator.hpp) | [`folly::splitStep / splitTo`](https://github.com/facebook/folly/blob/main/folly/String.h) | Partial: [`bdlb::Tokenizer`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_tokenizer.h) |
| [`ZuTokenizer::WhiteSpace`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTokenizer.hh)<br>➕ simple ASCII whitespace scanning | Partial: [`std::views::split`](https://en.cppreference.com/cpp/ranges/split_view) / custom delimiter predicate | [`boost::tokenizer`](https://github.com/boostorg/tokenizer/blob/master/include/boost/tokenizer.hpp) with character separator | Partial: [`folly::splitTo`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ whitespace-token policy needs adaptation | Partial: [`bdlb::Tokenizer`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_tokenizer.h) |

## Numeric input

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`Zu_nscan`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh) / numeric input ([`Zu_aton`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh)`.hh`)<br>➕ numeric scanning with Z-format policies | [`std::from_chars`](https://en.cppreference.com/cpp/utility/from_chars) | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`from_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp) | Partial: [`folly::to / tryTo`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h) |
| [`Zu_atou`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L332) | [`std::from_chars`](https://en.cppreference.com/cpp/utility/from_chars) for unsigned integers | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`from_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp) | Partial: [`folly::tryTo<unsigned>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h) |
| [`Zu_atoi`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L336) | [`std::from_chars`](https://en.cppreference.com/cpp/utility/from_chars) for signed integers | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`from_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp) | Partial: [`folly::tryTo<signed integer>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h) |
| [`Zu_atof`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L340) | [`std::from_chars`](https://en.cppreference.com/cpp/utility/from_chars) for floating point | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`from_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/from_chars.hpp) | Partial: [`folly::tryTo<floating point>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h) |

## Numeric output and buffer sizing

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`Zu_nprint`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh) / numeric output ([`Zu_ntoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh)`.hh`)<br>➕ compile-time numeric-format policies | [`std::to_chars`](https://en.cppreference.com/cpp/utility/to_chars) | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`to_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp) | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h) |
| [`Zu_utoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L915) | [`std::to_chars`](https://en.cppreference.com/cpp/utility/to_chars) for unsigned integers | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`to_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp) | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h) |
| [`Zu_itoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L922) | [`std::to_chars`](https://en.cppreference.com/cpp/utility/to_chars) for signed integers | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`to_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp) | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h) |
| [`Zu_ftoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L929)<br>➖ no automatic scientific-notation fallback | [`std::to_chars`](https://en.cppreference.com/cpp/utility/to_chars) for floating point<br>➕ general, fixed, scientific and hexadecimal modes | [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) [`to_chars`](https://github.com/boostorg/charconv/blob/master/include/boost/charconv/to_chars.hpp) | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h) |
| [`Zu_ulen`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L911)<br>➕ constexpr unsigned-output buffer sizing | Partial: [`std::numeric_limits`](https://en.cppreference.com/cpp/types/numeric_limits) and digit-count calculation | Partial: [Boost.Integer](https://github.com/boostorg/integer/blob/master/include/boost/integer.hpp) / [Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) sizing composition | N/A | Partial: [`bsl::formatted_size`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h) |
| [`Zu_ilen`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L918)<br>➕ constexpr signed-output buffer sizing | Partial: [`std::numeric_limits`](https://en.cppreference.com/cpp/types/numeric_limits) and digit-count calculation | Partial: [Boost.Integer](https://github.com/boostorg/integer/blob/master/include/boost/integer.hpp) / [Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) sizing composition | N/A | Partial: [`bsl::formatted_size`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h) |
| [`Zu_flen`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L925)<br>➕ format-aware floating-output buffer sizing | N/A; size according to selected conversion format | Partial: [Boost.Charconv](https://www.boost.org/doc/libs/latest/libs/charconv/doc/html/charconv.html) with caller-selected capacity | N/A | Partial: [`bsl::formatted_size`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h) |

## Formatting, streams and joining

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuFmt`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFmt.hh)<br>➕ formatting policies encoded in types | [`std::format`](https://en.cppreference.com/cpp/utility/format/format), stream manipulators | [Boost.Format](https://www.boost.org/doc/libs/latest/libs/format/doc/format.html) / [Convert](https://github.com/boostorg/convert/blob/master/include/boost/convert.hpp) | N/A | Partial: [`bsl::format`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h) |
| [`ZuPrint`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPrint.hh)<br>➕ ADL print customization shared by Z streams | Partial: [`std::formatter`](https://en.cppreference.com/cpp/utility/format/formatter), [`operator<<`](https://en.cppreference.com/cpp/io/basic_ostream/operator_ltlt) | Partial: [Boost.Format](https://www.boost.org/doc/libs/latest/libs/format/doc/format.html) streamable types | Partial: [`folly::toAppend customization`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-conversion customization rather than Z stream ADL | Partial: [`bslim::Printer`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_printer.h) |
| [`ZuStream`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStream.hh)<br>➕ direct output into caller-supplied bounded text storage | [`std::ospanstream`](https://en.cppreference.com/cpp/io/basic_ospanstream) (C++23) | [Boost.Iostreams](https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html) array sink | Partial: [`folly::io::Appender`](https://github.com/facebook/folly/blob/main/folly/io/Cursor.h)<br>➖ IOBuf-chain output, not a bounded character-span stream | Partial: [`bdlsb::FixedMemOutput`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlsb/bdlsb_fixedmemoutput.h) |
| [`ZuVStream`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuVStream.hh)<br>➕ type-erased stream interface with function pointers | Partial: [`std::ostream`](https://en.cppreference.com/cpp/io/basic_ostream) / [`std::streambuf`](https://en.cppreference.com/cpp/io/basic_streambuf) | Partial: [Boost.Iostreams](https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html) [`stream`](https://github.com/boostorg/iostreams/blob/master/include/boost/iostreams/stream.hpp) / filtering stream | N/A | Partial: [`bsl::ostream`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_ostream.h) |
| [`ZuJoin`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuJoin.hh)<br>➕ streams elements directly with a delimiter | Partial: [`std::views::join_with`](https://en.cppreference.com/cpp/ranges/join_with_view) (C++23)<br>➖ range joining is not a formatting adapter | [`boost::algorithm::join`](https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string/join.hpp)<br>➖ normally builds a result string | Partial: [`folly::join`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ normally appends to an owning string rather than an arbitrary Z stream | N/A |

## Time, identifiers and versions

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuTime`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTime.hh)<br>➕ compact seconds/nanoseconds with null sentinel<br>➖ same type represents instants and intervals | [`std::chrono::duration`](https://en.cppreference.com/cpp/chrono/duration), [`std::chrono::time_point`](https://en.cppreference.com/cpp/chrono/time_point)<br>➕ distinct clock and duration types | [Boost.Chrono](https://www.boost.org/doc/libs/latest/doc/html/chrono.html) / [DateTime](https://www.boost.org/doc/libs/latest/doc/html/date_time.html) | Partial: [`folly::to<chrono duration> / timespec conversions`](https://github.com/facebook/folly/blob/main/folly/chrono/Conv.h)<br>➖ no unified Z instant/interval/sentinel type | Partial: [`bsls::TimeInterval`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_timeinterval.h) / [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h) |
| [`ZuDateTime`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDateTime.hh)<br>➕ wide Julian-calendar date range | [`std::chrono`](https://en.cppreference.com/cpp/chrono) calendar types and formatting (C++20) | [Boost.DateTime](https://www.boost.org/doc/libs/latest/doc/html/date_time.html) | N/A | Partial: [`bdlt::Datetime`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlt/bdlt_datetime.h)<br>➕ broad calendar/date utilities |
| [`ZuID`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuID.hh)<br>➕ by-value bounded name with [ZuArray](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh) length, comparison, hashing and printing<br>➖ fixed framework capacity | Partial: [`std::string`](https://en.cppreference.com/cpp/string/basic_string)<br>➖ different storage/capacity contract | [`boost::static_string`](https://github.com/boostorg/static_string/blob/master/include/boost/static_string.hpp) | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h) | N/A |
| [`ZuSemVer`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSemVer.hh)<br>➕ packed compact version value<br>➖ minor ≤ 99, patch ≤ 999; no prerelease/build metadata | N/A | N/A; [`BOOST_VERSION`](https://github.com/boostorg/config/blob/master/include/boost/version.hpp) is a library version macro, not a SemVer value type | N/A | N/A |

## Fixed-point and decimal arithmetic

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuFixed`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFixed.hh)<br>➕ mantissa plus runtime decimal scale, null/reset sentinels and scan/format conversions<br>➖ constrained precision/range | N/A; integer arithmetic alone supplies none of these decimal-value policies | Partial: [Boost.Multiprecision](https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp) scaled integer composition | Partial: [`bdldfp::Decimal64`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h)`  /  `[`Decimal128`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h)<br>➕ IEEE decimal floating-point arithmetic |
| [`ZuDecimal`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimal.hh)<br>➕ decimal arithmetic with 18 integer and 18 fractional digits<br>➖ fixed precision/range | N/A in C++23 | Partial: [`boost::multiprecision::cpp_dec_float`](https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_dec_float.hpp)<br>➕ configurable precision<br>➖ decimal floating point has different rounding/storage semantics | Partial: [`bdldfp::Decimal128`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h)<br>➕ IEEE decimal arithmetic and rounding<br>➖ 34 significant decimal digits; [ZuDecimal](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimal.hh) can retain 18 integer plus 18 fractional digits |
| [`ZuDecimalFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimalFn.hh) | N/A; decimal representation helpers | Partial: [Boost.Multiprecision](https://github.com/boostorg/multiprecision/blob/master/include/boost/multiprecision/cpp_int.hpp) arithmetic and conversions | Partial: [`bdldfp::DecimalUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimalutil.h) |

## Unicode conversion and measurement

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuUTF`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh)<br>➕ conversion into caller-owned storage | Partial: locale/codecvt facilities<br>➖ legacy Unicode conversion APIs are deprecated in C++23 | [Boost.Locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html) / Nowide | Partial: [`folly::utf8ToCodePoint / codePointToUtf8`](https://github.com/facebook/folly/blob/main/folly/Unicode.h)<br>➖ lower-level conversion subset rather than the full UTF-8/16/32 adapter | Partial: [`bdlde::CharConvertUtf16`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h) |
| [`ZuUTF8`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh)<br>➕ code-point decoding/encoding and input/output length operations for caller buffers | Partial: legacy [`std::codecvt_utf8`](https://en.cppreference.com/cpp/locale/codecvt_utf8)<br>➖ different stateful facet interface; deprecated in C++23 | Partial: [Boost.Locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html) UTF conversion primitives | [`folly::utf8ToCodePoint / codePointToUtf8`](https://github.com/facebook/folly/blob/main/folly/Unicode.h) | Partial: [`bdlde::Utf8Util`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_utf8util.h) |
| [`ZuUTF16`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh)<br>➕ surrogate-aware code-point decoding/encoding with caller-buffer length checks | Partial: legacy [`std::codecvt_utf16`](https://en.cppreference.com/cpp/locale/codecvt_utf16)<br>➖ different stateful facet interface; deprecated in C++23 | Partial: [Boost.Locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html) UTF conversion primitives | Partial: [`folly::unicode_code_point_from_utf16_surrogate_pair`](https://github.com/facebook/folly/blob/main/folly/Unicode.h)<br>➖ code-point helper, not complete buffer codec | Partial: [`bdlde::CharConvertUtf16`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h) |
| [`ZuUTF32`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh)<br>➕ bounded code-point input/output and Unicode display-width lookup | N/A; char32_t stores a code unit but supplies no width lookup or buffer operations | Partial: [Boost.Locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html) UTF conversion primitives | N/A | [`bdlde::CharConvertUtf32`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf32.h) |
| [`ZuUTFSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh)<br>➕ packs input length, output length and display width | N/A; custom encoding measurement record | Partial: [Boost.Locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html) conversion + custom measurement record | N/A | N/A |

## Binary-text and percent encoding

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZuHex`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHex.hh)<br>➕ caller-buffer output and in-place decoding | N/A; custom digit conversion | [`boost::algorithm::hex`](https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/hex.hpp) / [`unhex`](https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/hex.hpp) | Partial: [`folly::hexlify / unhexlify`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ different destination/policy interfaces | [`bdlde::HexEncoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_hexencoder.h)`  /  `[`HexDecoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_hexdecoder.h)<br>➕ incremental conversion state |
| [`ZuBase32`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase32.hh)<br>➕ caller-buffer codec | N/A | Partial: [Boost.Archive](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html) iterator composition | N/A | N/A |
| [`ZuBase64`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase64.hh)<br>➕ caller-buffer codec | N/A | Partial: [Boost.Archive](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html) [`base64_from_binary`](https://github.com/boostorg/serialization/blob/master/include/boost/archive/iterators/base64_from_binary.hpp) / [`binary_from_base64`](https://github.com/boostorg/serialization/blob/master/include/boost/archive/iterators/binary_from_base64.hpp) | [`folly::base64Encode / base64Decode`](https://github.com/facebook/folly/blob/main/folly/base64.h) | [`bdlde::Base64Encoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoder.h)`  /  `[`Base64Decoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64decoder.h)<br>➕ incremental codec with configurable options |
| [`ZuBase64URL`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase64URL.hh)<br>➕ URL-safe alphabet | N/A | Partial: Base64 iterator composition with alphabet adaptation | [`folly::base64URLEncode / base64URLDecode`](https://github.com/facebook/folly/blob/main/folly/base64.h) | Partial: [`bdlde::Base64EncoderOptions::urlSafe`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoderoptions.h)`  +  `[`Base64DecoderOptions`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64decoderoptions.h) |
| [`ZuPercent`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPercent.hh)<br>➕ policy-driven percent encoding, plus/space handling and in-place decoding | N/A | [Boost.URL](https://github.com/boostorg/url/blob/master/include/boost/url.hpp) encoding/decoding<br>➕ [URL](https://github.com/boostorg/url/blob/master/include/boost/url.hpp) component and grammar vocabulary | Partial: [`folly::uriEscape / uriUnescape`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ different destination/policy interfaces | N/A |

## Testing

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuTest`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTest.hh#L255)<br>➕ TAP output and static nested test counts | N/A | [Boost.Test](https://github.com/boostorg/test/blob/master/include/boost/test/unit_test.hpp)<br>➕ broader test framework and reporting options | Partial: [`bslim::TestUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h)`  /  `[`BSLIM_TESTUTIL_*`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h)<br>➖ assertion helpers do not include a TAP subtest runner |
| [`ZuTestUtil`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTestUtil.hh) | N/A | Partial: [Boost.Test](https://github.com/boostorg/test/blob/master/include/boost/test/unit_test.hpp) helpers and execution utilities | Partial: [`bslim::TestUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h)`  /  `[`BSLIM_TESTUTIL_*`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h)<br>➖ assertion helpers do not include a TAP subtest runner |

## Structure reflection and facets

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuStruct`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh#L460)<br>➕ field/accessor metadata, facets and composite keys in C++23<br>➕ supports members, methods and lambda-derived fields | N/A in C++23; core C++23 has no structure reflection | [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) / [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html).Struct<br>➕ [Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) also describes bases and member functions | Partial: [`bdlat_SequenceFunctions`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_sequencefunctions.h)`  /  `[`bdlat`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h)`  type  `[`traits`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntccfg/ntccfg_traits.h) |
| [`ZuStructFacet`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh#L768)<br>➕ alternate reflected field views of one type | N/A in C++23 | Partial: separate [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html)/Fusion adaptations or metadata types | Partial: [`bdlat_SequenceFunctions`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_sequencefunctions.h)`  /  `[`bdlat`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h)`  type  `[`traits`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntccfg/ntccfg_traits.h) |

## ZuMatcher — compile-time multi-string matching

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZuMatcher`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMatcher.hh#L447)<br>➕ compile-time Aho–Corasick for fixed multiple keys<br>➖ large instantiations increase compile cost | Partial: [`std::search`](https://en.cppreference.com/cpp/algorithm/search), Boyer–Moore searchers<br>➖ single-pattern facilities | Partial: [Boost.Algorithm](https://github.com/boostorg/algorithm/blob/master/include/boost/algorithm/string.hpp) searching / [Boost.Spirit](https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp) symbols | Partial: [`bsl::boyer_moore_horspool_searcher`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)<br>➖ single-pattern search rather than a compiled multi-key automaton |

# Zm

Multithreading foundation.

## Platform and cache alignment

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zm/src/), [module guide](../zm/README.md).
Standard counterparts: [concurrency library](https://eel.is/c++draft/thread).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmPlatform`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPlatform.hh) primitives<br>➕ OS IDs, aligned allocation and runtime platform helpers | Partial: [`std::thread::id`](https://en.cppreference.com/cpp/thread/thread/id), aligned [`operator new`](https://en.cppreference.com/cpp/memory/new/operator_new) | Partial: [Boost.Thread](https://www.boost.org/doc/libs/latest/doc/html/thread.html), [Align](https://www.boost.org/doc/libs/latest/doc/html/align.html), WinAPI | N/A | Partial: [`BSLS_PLATFORM_*`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_platform.h)`  /  `[`bsls::Platform`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_platform.h) |
| [`ZmCacheLineSize`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPlatform.hh) / [`ZmCacheAlign`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPlatform.hh)<br>➕ shared target-specific cache-separation policy and matching alignment annotation | Partial: [`std::hardware_destructive_interference_size`](https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size)<br>➖ does not supply Z's selected 128-byte ARM64 / 64-byte other-target policy | Partial: [Boost.Align](https://www.boost.org/doc/libs/latest/doc/html/align.html) / alignment macros | Partial: [`folly::hardware_destructive_interference_size`](https://github.com/facebook/folly/blob/main/folly/lang/Align.h)<br>➖ uses Folly's own separation policy | Partial: [`bslmt::Platform::e_CACHE_LINE_SIZE`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_platform.h) |

## Atomic operations and locks

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmAtomic`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAtomic.hh) | [`std::atomic`](https://en.cppreference.com/cpp/atomic/atomic), atomic fences | [Boost.Atomic](https://github.com/boostorg/atomic/blob/master/include/boost/atomic.hpp) | Partial: [`folly::relaxed_atomic`](https://github.com/facebook/folly/blob/main/folly/synchronization/RelaxedAtomic.h)<br>➖ relaxed default ordering; not the same complete operation contract | Partial: [`bsls::AtomicInt`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_atomic.h)`  /  `[`AtomicInt64`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_atomic.h)`  /  `[`AtomicPointer`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_atomic.h) |
| [`ZmNoLock`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNoLock.hh)<br>➕ compile-time no-op locking policy | N/A; custom null mutex | Partial: [Boost.Interprocess](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html) [`null_mutex`](https://github.com/boostorg/interprocess/blob/master/include/boost/interprocess/sync/null_mutex.hpp) | N/A | N/A |
| [`ZmLockTraits`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLockTraits.hh) | Partial: Lockable concepts/requirements | Partial: [Boost.Thread](https://www.boost.org/doc/libs/latest/doc/html/thread.html) lockable traits | N/A | N/A |
| [`ZmSpinLock`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSpinLock.hh)<br>➕ wraps Concurrency Kit FAS spinlock | Partial: [`std::atomic_flag`](https://en.cppreference.com/cpp/atomic/atomic_flag) spin loop | Partial: [Boost.Atomic](https://github.com/boostorg/atomic/blob/master/include/boost/atomic.hpp) spinlock composition | [`folly::MicroSpinLock`](https://github.com/facebook/folly/blob/main/folly/synchronization/MicroSpinLock.h) | [`bsls::SpinLock`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_spinlock.h) |
| [`ZmPLock`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPLock.hh)<br>➕ platform-tuned primitive mutex<br>➖ cannot be used with [`ZmCondition`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCondition.hh) | Partial: [`std::mutex`](https://en.cppreference.com/cpp/thread/mutex) | Partial: [`boost::mutex`](https://github.com/boostorg/thread/blob/master/include/boost/thread/mutex.hpp) | Partial: [`folly::DistributedMutex`](https://github.com/facebook/folly/blob/main/folly/synchronization/DistributedMutex.h)<br>➖ different contention/locking implementation | Partial: [`bslmt::QLock`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_qlock.h) |
| [`ZmLock`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLock.hh) | [`std::recursive_mutex`](https://en.cppreference.com/cpp/thread/recursive_mutex) | [`boost::recursive_mutex`](https://github.com/boostorg/thread/blob/master/include/boost/thread/recursive_mutex.hpp) | N/A | [`bslmt::Mutex`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_mutex.h) |
| [`ZmRWLock`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRWLock.hh) | [`std::shared_mutex`](https://en.cppreference.com/cpp/thread/shared_mutex) | [`boost::shared_mutex`](https://github.com/boostorg/thread/blob/master/include/boost/thread/shared_mutex.hpp) | [`folly::SharedMutex / SharedMutexReadPriority`](https://github.com/facebook/folly/blob/main/folly/SharedMutex.h) | [`bslmt::ReaderWriterMutex`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_readerwritermutex.h) |
| [`ZmGuard`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmGuard.hh) / [`ZmReadGuard`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNoLock.hh) | [`std::unique_lock`](https://en.cppreference.com/cpp/thread/unique_lock), [`std::shared_lock`](https://en.cppreference.com/cpp/thread/shared_lock) | [`boost::unique_lock`](https://github.com/boostorg/thread/blob/master/include/boost/thread/lock_types.hpp), [`boost::shared_lock`](https://github.com/boostorg/thread/blob/master/include/boost/thread/lock_types.hpp) | Partial: [`folly::Synchronized locking proxies`](https://github.com/facebook/folly/blob/main/folly/Synchronized.h)<br>➕ ties protected data to its lock<br>➖ different guard/data ownership interface | Partial: [`bslmt::LockGuard`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_lockguard.h)`  +  `[`ReadLockGuard`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_readlockguard.h) |

## Conditions, semaphores and blocking adapters

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmCondition`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCondition.hh) | [`std::condition_variable_any`](https://en.cppreference.com/cpp/thread/condition_variable_any) | [`boost::condition_variable_any`](https://github.com/boostorg/thread/blob/master/include/boost/thread/condition_variable.hpp) | N/A | [`bslmt::Condition`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_condition.h) |
| [`ZmSemaphore`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSemaphore.hh) | [`std::counting_semaphore`](https://en.cppreference.com/cpp/thread/counting_semaphore) (C++20) | [Boost.Interprocess](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html) [`interprocess_semaphore`](https://github.com/boostorg/interprocess/blob/master/include/boost/interprocess/sync/interprocess_semaphore.hpp) (partial sharing model) | Partial: [`folly::LifoSem / NativeSemaphore`](https://github.com/facebook/folly/blob/main/folly/synchronization/LifoSem.h) | [`bslmt::Semaphore`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_semaphore.h) |
| [`ZmBlock`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBlock.hh)<br>➕ continuation-to-blocking bridge reusing a thread-local semaphore | Partial: [`std::promise`](https://en.cppreference.com/cpp/thread/promise) / [`std::future`](https://en.cppreference.com/cpp/thread/future)<br>➖ shared-state machinery differs | Partial: [Boost.Thread](https://www.boost.org/doc/libs/latest/doc/html/thread.html) futures / [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`use_future`](https://github.com/boostorg/asio/blob/master/include/boost/asio/use_future.hpp) | Partial: [`folly::Baton`](https://github.com/facebook/folly/blob/main/folly/synchronization/Baton.h)<br>➖ one-shot handoff primitive; async-to-blocking wrapper is additional work | Partial: [`bslmt::Latch`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_latch.h) |

## Random number generation

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmRandom`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRandom.hh)<br>➕ framework Mersenne Twister wrapper<br>➖ not cryptographic | [`std::mt19937`](https://en.cppreference.com/cpp/numeric/random/mersenne_twister_engine)<br>➕ standard distribution family | [Boost.Random](https://www.boost.org/doc/libs/latest/doc/html/boost_random.html) Mersenne Twister and distributions | Partial: [`folly::Random / ThreadLocalPRNG`](https://github.com/facebook/folly/blob/main/folly/Random.h)<br>➖ current default generator is not Mersenne Twister | Partial: [`bsl::mt19937`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_random.h)`  /  `[`random_device`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_random.h) |

## Clocks, timing statistics and retry policies

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmTime`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTime.hh) | [`std::chrono`](https://en.cppreference.com/cpp/chrono) clocks | [Boost.Chrono](https://www.boost.org/doc/libs/latest/doc/html/chrono.html) | Partial: [`folly::chrono::coarse_steady_clock / coarse_system_clock`](https://github.com/facebook/folly/blob/main/folly/Chrono.h) | [`bsls::SystemTime`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_systemtime.h) |
| [`ZmTimeInterval`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTimeInterval.hh)<br>➕ locked min/max/total/mean/count aggregation | Partial: chrono values + custom aggregation | [Boost.Accumulators](https://www.boost.org/doc/libs/latest/doc/html/accumulators.html) with timing values | Partial: [`folly::Histogram`](https://github.com/facebook/folly/blob/main/folly/stats/Histogram.h)<br>➕ distribution buckets in addition to aggregate statistics<br>➖ different time-value/locking contract | Partial: [`balm::Collector`](https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_collector.h) |
| [`ZmBackoff`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBackoff.hh)<br>➕ configurable exponential backoff with jitter | Partial: chrono + [`<random>`](https://en.cppreference.com/cpp/header/random) composition | Partial: [Boost.Random](https://www.boost.org/doc/libs/latest/doc/html/boost_random.html) + [Chrono](https://www.boost.org/doc/libs/latest/doc/html/chrono.html) composition | Partial: [`folly::futures::retryingPolicyCappedJitteredExponentialBackoff`](https://github.com/facebook/folly/blob/main/folly/futures/Retrying.h)<br>➖ future retry policy rather than standalone backoff state | N/A |
| [`ZmTimeout`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTimeout.hh)<br>➕ timeout state with exponential backoff | Partial: chrono deadlines + custom retry policy | Partial: [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) timer + retry policy | Partial: [`folly::HHWheelTimer`](https://github.com/facebook/folly/blob/main/folly/io/async/HHWheelTimer.h)<br>➖ timer scheduling does not include Z exponential-backoff state | Partial: [`bdlmt::EventScheduler`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_eventscheduler.h)<br>➖ no built-in exponential-backoff retry state or final-attempt callback |

## Stack and scratch allocation

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmStackAvail`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmStackAvail.hh#L20)<br>➕ queries remaining stack for scratch policies | N/A | N/A | N/A | N/A |
| [`ZmAlloc`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAlloc.hh#L79)<br>➕ checks remaining stack space and selects aligned stack storage or RAII heap fallback<br>➖ macro evaluates size more than once | N/A; automatic storage does not provide runtime stack-capacity checks or heap fallback | Partial: [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) [`small_vector`](https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp)<br>➖ fixed inline capacity rather than remaining-stack check | N/A | N/A |
| [`ZmLocal`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLocal.hh#L60)<br>➕ selects stack/heap storage from available stack space and runs construction/destruction | Partial: [`std::unique_ptr`](https://en.cppreference.com/cpp/memory/unique_ptr)<br>➖ does not automatically select stack storage | Partial: scoped/unique pointer with explicit storage selection | N/A | Partial: [`bdlma::LocalSequentialAllocator`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h)<br>➖ local buffer capacity is fixed; no runtime available-stack check |
| [`ZmScratch`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScratch.hh#L593)<br>➕ runtime-sized scratch array with stack-space checks, heap fallback and element cleanup | Partial: [`std::vector`](https://en.cppreference.com/cpp/container/vector)<br>➖ no automatic stack-backed allocation path | Partial: [`boost::container::small_vector`](https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp) | Partial: [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h)<br>➖ fixed inline capacity rather than remaining-stack-space selection | Partial: [`bdlma::LocalSequentialAllocator`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h)<br>➖ local buffer capacity is fixed; no runtime available-stack check |

## Heaps and allocator integration

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zm/src/), [module guide](../zm/README.md).
Relevant counterparts: [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html),
[Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh) / [`ZmNHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh)<br>➕ named fixed-block arenas, partitions and telemetry<br>➕ CPU/NUMA-aware reserves<br>➖ reserves can fall back to ordinary allocation | Partial: [`std::allocator`](https://en.cppreference.com/cpp/memory/allocator), [`std::pmr::synchronized_pool_resource`](https://en.cppreference.com/cpp/memory/synchronized_pool_resource)<br>➕ standard allocator/resource interfaces<br>➖ no named tuning/telemetry layer | [Boost.Pool](https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp)<br>➕ reusable pool allocator vocabulary<br>➖ no equivalent integrated naming/NUMA/telemetry layer | Partial: [`folly::Arena / ThreadCachedArena`](https://github.com/facebook/folly/blob/main/folly/memory/Arena.h)<br>➖ bulk arena lifetime rather than named individually recycled block heaps | Partial: [`bdlma::ConcurrentPool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentpool.h)<br>➕ thread-safe configurable block pooling<br>➖ no named heap registry with shard and tuning-file integration |
| [`ZmVHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmVHeap.hh)<br>➕ named variable-size recycling classes and instrumentation | [`std::pmr`](https://en.cppreference.com/cpp/memory/memory_resource) pool resources | [Boost.Pool](https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp) / [Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) PMR | Partial: [`folly::Arena`](https://github.com/facebook/folly/blob/main/folly/memory/Arena.h)<br>➖ bulk allocation/release model rather than per-block recycling classes | Partial: [`bdlma::ConcurrentMultipool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentmultipool.h) |
| [`ZmAllocator`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAllocator.hh)<br>➕ connects Z heaps to standard allocator-aware containers | [`std::allocator`](https://en.cppreference.com/cpp/memory/allocator), [`std::pmr::polymorphic_allocator`](https://en.cppreference.com/cpp/memory/polymorphic_allocator) | [`boost::pool_allocator`](https://github.com/boostorg/pool/blob/master/include/boost/pool/pool_alloc.hpp) / [Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) PMR allocator | Partial: [`folly::reentrant_allocator`](https://github.com/facebook/folly/blob/main/folly/memory/ReentrantAllocator.h)<br>➖ different allocator policy; no Z named-heap integration | Partial: [`bslma::Allocator`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_allocator.h)`  /  `[`bsl::allocator`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h)<br>➕ pervasive allocator propagation in BSL containers |
| [`ZmHeapMgr`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHeap.hh)<br>➕ global named arena configuration and statistics | N/A; custom PMR resource registry | Partial: pool management with application registry | N/A | N/A |

## Scratch-buffer codecs

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZmCodec`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh) ([`ZmBase64`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh), [`ZmBase32`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh), [`ZmHex`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCodec.hh))<br>➕ scratch-buffer codecs delivered through callbacks | N/A | Partial: Boost encoding iterators + scratch storage | Partial: [`bdlde::Base64Encoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoder.h)`  /  `[`HexEncoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_hexencoder.h)<br>➖ no Base32 codec in bdlde |

## Intrusive ownership and callbacks

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmObject`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmObject.hh)<br>➕ embedded atomic reference count | Partial: [`std::enable_shared_from_this`](https://en.cppreference.com/cpp/memory/enable_shared_from_this) | [`boost::intrusive_ref_counter`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp)`<T, `[`boost::thread_safe_counter`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ref_counter.hpp)`>` | N/A | N/A |
| [`ZmRef`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRef.hh)<br>➕ intrusive shared ownership<br>➖ no weak observers | [`std::shared_ptr`](https://en.cppreference.com/cpp/memory/shared_ptr)<br>➕ arbitrary pointees and [`std::weak_ptr`](https://en.cppreference.com/cpp/memory/weak_ptr) | [`boost::intrusive_ptr`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ptr.hpp) with atomic hooks | N/A | N/A |
| [`ZmPolymorph`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolymorph.hh) | Partial: shared ownership of a virtual-destructor base | Partial: intrusive ownership of a virtual-destructor base | N/A | N/A |
| [`ZmContext`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmContext.hh)<br>➕ compact raw-or-owning erased context<br>➖ pointer-tag representation assumes supported architectures | Partial: [`std::variant`](https://en.cppreference.com/cpp/utility/variant)`<void*, `[`std::shared_ptr`](https://en.cppreference.com/cpp/memory/shared_ptr)`<Base>>` | Partial: [`boost::variant2::variant`](https://github.com/boostorg/variant2/blob/master/include/boost/variant2/variant.hpp) + [`boost::intrusive_ptr`](https://github.com/boostorg/smart_ptr/blob/master/include/boost/smart_ptr/intrusive_ptr.hpp) | Partial: [`folly::DiscriminatedPtr`](https://github.com/facebook/folly/blob/main/folly/DiscriminatedPtr.h)<br>➖ tagged pointers do not retain pointees | N/A |
| [`ZmFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmFn_.hh)<br>➕ optimized function/context representation<br>➖ larger captures can allocate | [`std::function`](https://en.cppreference.com/cpp/utility/functional/function), [`std::move_only_function`](https://en.cppreference.com/cpp/utility/functional/move_only_function) (C++23) | [`boost::function`](https://github.com/boostorg/function/blob/master/include/boost/function.hpp) | [`folly::Function`](https://github.com/facebook/folly/blob/main/folly/Function.h)<br>➕ move-only captures and cv-qualified signatures<br>➖ move-only wrapper rather than [ZmFn](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmFn_.hh)'s copyable delegate | [`bsl::function`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)<br>➕ allocator-aware type erasure |

## Thread-local state and cleanup

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmSpecific`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSpecific.hh)<br>➕ explicit scope, cleanup levels and enumeration of all thread instances<br>➕ consolidates per-type TLS across Windows modules | N/A; thread_local does not provide this registry and cleanup policy | [`boost::thread_specific_ptr`](https://github.com/boostorg/thread/blob/master/include/boost/thread/tss.hpp)<br>➖ no equivalent global instance-enumeration contract | [`folly::ThreadLocal / ThreadLocalPtr`](https://github.com/facebook/folly/blob/main/folly/ThreadLocal.h)<br>➕ supports cross-thread instance access<br>➖ different teardown/order and Windows module contracts | Partial: [`bslmt::ThreadUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadutil.h)` TLS keys`<br>➖ no built-in cross-thread instance enumeration or cleanup-priority registry |
| [`ZmSingleton`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmSingleton.hh)<br>➕ configurable lazy construction, optional final() hook and cleanup-level registration | Partial: [`std::call_once`](https://en.cppreference.com/cpp/thread/call_once)<br>➖ no singleton ownership or cleanup sequencing | Partial: [Boost.Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html) singleton | [`folly::Singleton / SingletonVault`](https://github.com/facebook/folly/blob/main/folly/Singleton.h)<br>➕ managed singleton lifecycle and dependency handling | N/A |
| [`ZmCleanup`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCleanup.hh) / [`ZmGlobal`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmGlobal.hh)<br>➕ ordered cleanup levels and a shared registry for singleton/TLS teardown | N/A; static destruction order alone does not implement this registry/policy | Partial: Boost singleton/TLS primitives with custom coordination | Partial: [`folly::SingletonVault`](https://github.com/facebook/folly/blob/main/folly/Singleton.h)<br>➖ does not implement Z's shared singleton/TLS cleanup-level registry | N/A |

## Assertions and diagnostic utilities

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmAssert`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAssert.hh#L38) / [`ZmAssert_`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmAssert.hh#L35)<br>➕ debug, always-on and release-fallback forms | [`assert`](https://en.cppreference.com/cpp/error/assert), custom always-on check | [Boost.Assert](https://www.boost.org/doc/libs/latest/libs/assert/doc/html/assert.html)<br>➕ configurable assertion handlers | Partial: [`FOLLY_SAFE_CHECK / FOLLY_SAFE_DCHECK`](https://github.com/facebook/folly/blob/main/folly/lang/SafeAssert.h)<br>➖ no matching release-fallback expression form | Partial: [`BSLS_ASSERT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h)`  /  `[`BSLS_ASSERT_SAFE`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h)<br>➕ configurable assertion handling and build levels |
| [`ZmObjectDebug`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmObjectDebug.hh)<br>➕ intrusive object lifetime diagnostics | N/A | Partial: [Boost.SmartPtr](https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html) debugging hooks | N/A | N/A |
| [`ZmDemangle`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmDemangle.hh)<br>➕ binutils demangling for difficult compiler names<br>➖ additional binutils dependency | N/A; [`type_info::name()`](https://en.cppreference.com/cpp/types/type_info/name) is implementation-defined | [`boost::core::demangle`](https://github.com/boostorg/core/blob/master/include/boost/core/demangle.hpp), [Boost.TypeIndex](https://www.boost.org/doc/libs/latest/doc/html/boost_typeindex.html) | Partial: [`folly::demangle`](https://github.com/facebook/folly/blob/main/folly/Demangle.h)<br>➖ different demangler/backend contract | N/A |
| [`ZmBackTrace`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBackTrace_.hh) | [`std::stacktrace`](https://en.cppreference.com/cpp/utility/basic_stacktrace) (C++23) | [Boost.Stacktrace](https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html) | [`folly::symbolizer stack traces`](https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/Symbolizer.h) | [`balst::StackTraceUtil`](https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktraceutil.h)`  /  `[`StackTrace`](https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktrace.h) |
| [`ZmBackTracer`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBackTracer.hh)<br>➕ keeps a bounded ring of captured traces | Partial: [`std::stacktrace`](https://en.cppreference.com/cpp/utility/basic_stacktrace) + bounded storage | Partial: [Boost.Stacktrace](https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html) + [CircularBuffer](https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp) | Partial: [`folly::symbolizer stack traces`](https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/Symbolizer.h)<br>➖ bounded trace-history ring is application work | Partial: [`balst::StackTrace`](https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktrace.h)<br>➖ captures individual traces; no built-in bounded trace-history ring |
| [`ZmTrap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTrap.hh)<br>➕ last-ditch diagnostics and signal trapping | Partial: [`std::signal`](https://en.cppreference.com/cpp/utility/program/signal), [`std::set_terminate`](https://en.cppreference.com/cpp/error/set_terminate) | Partial: [Boost.Stacktrace](https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html) signal-handler integration | Partial: [`folly symbolizer fatal-signal handlers`](https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/SignalHandler.h) | N/A |

## Containers, indices and caches

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmNode`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNode.hh) / [`ZmNodeFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmNodeFn.hh)<br>➕ explicit intrusive node ownership policies | Partial: container node handles<br>➖ no general intrusive-node framework | [Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html) hooks and value traits | Partial: [`folly::IntrusiveListHook`](https://github.com/facebook/folly/blob/main/folly/container/IntrusiveList.h)<br>➖ list linkage rather than Z's generic node ownership policy | N/A |
| [`ZmList`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmList.hh)<br>➕ intrusive ownership, heap and locking policies | [`std::list`](https://en.cppreference.com/cpp/container/list)<br>➕ standard iterators and algorithms | [`boost::intrusive::list`](https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/list.hpp) | [`folly::IntrusiveList / CountedIntrusiveList`](https://github.com/facebook/folly/blob/main/folly/container/IntrusiveList.h)<br>➖ different locking/allocation/ownership policies | Partial: [`bslalg::BidirectionalLink`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_bidirectionallink.h)`  /  `[`BidirectionalLinkListUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_bidirectionallinklistutil.h) |
| [`ZmStack`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmStack.hh)<br>➕ deletion inside stack for queued cancellation<br>➖ requires sentinel-null value semantics | [`std::stack`](https://en.cppreference.com/cpp/container/stack) / [`std::vector`](https://en.cppreference.com/cpp/container/vector) composition | [`boost::container::vector`](https://github.com/boostorg/container/blob/master/include/boost/container/vector.hpp) with stack operations | Partial: [`folly::AtomicLinkedList`](https://github.com/facebook/folly/blob/main/folly/AtomicLinkedList.h)<br>➖ bulk-sweep atomic stack rather than Z's indexed/removable stack | Partial: [`bdlcc::Deque`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_deque.h) |
| [`ZmQueue`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmQueue.hh)<br>➕ FIFO/LIFO operations without shifting remaining items<br>➖ sentinel-null value contract | [`std::deque`](https://en.cppreference.com/cpp/container/deque), [`std::queue`](https://en.cppreference.com/cpp/container/queue) | [Boost.CircularBuffer](https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp) / [Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) deque | Partial: [`folly::MPMCQueue`](https://github.com/facebook/folly/blob/main/folly/MPMCQueue.h)<br>➕ bounded concurrent producer/consumer queue<br>➖ different sizing and FIFO/LIFO contract | Partial: [`bdlcc::Deque`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_deque.h) |
| [`ZmRBTree`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRBTree.hh)<br>➕ intrusive nodes and configurable locking/ownership | [`std::map`](https://en.cppreference.com/cpp/container/map), [`std::set`](https://en.cppreference.com/cpp/container/set), multi variants | [`boost::intrusive::rbtree`](https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/rbtree.hpp) / set | Partial: [`folly::ConcurrentSkipList`](https://github.com/facebook/folly/blob/main/folly/ConcurrentSkipList.h)<br>➖ ordered skip-list set rather than intrusive red/black tree | Partial: [`bslalg::RbTreeNode`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_rbtreenode.h)`  /  `[`RbTreeUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslalg/bslalg_rbtreeutil.h) |
| [`ZmLHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmLHash.hh)<br>➕ by-value linear probing with Z policies | [`std::unordered_map`](https://en.cppreference.com/cpp/container/unordered_map) / [`std::unordered_set`](https://en.cppreference.com/cpp/container/unordered_set)<br>➖ standard node-oriented contracts differ | [`boost::unordered_flat_map`](https://github.com/boostorg/unordered/blob/master/include/boost/unordered/unordered_flat_map.hpp) / [`unordered_flat_set`](https://github.com/boostorg/unordered/blob/master/include/boost/unordered/unordered_flat_set.hpp) | Partial: [`folly::F14ValueMap / F14FastMap`](https://github.com/facebook/folly/blob/main/folly/container/F14Map.h)<br>➕ optimized value/node/vector storage choices<br>➖ different probing, locking and ownership policies | Partial: [`bdlc::FlatHashMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashmap.h)`  /  `[`FlatHashSet`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashset.h)<br>➕ allocator-aware flat storage |
| [`ZmHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHash.hh)<br>➕ intrusive chaining, lock striping and named tuning | [`std::unordered_map`](https://en.cppreference.com/cpp/container/unordered_map) / [`std::unordered_set`](https://en.cppreference.com/cpp/container/unordered_set)<br>➖ caller supplies synchronization | [`boost::intrusive::unordered_set`](https://github.com/boostorg/intrusive/blob/master/include/boost/intrusive/unordered_set.hpp); partial: [Boost.Unordered](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html) concurrent containers | Partial: [`folly::ConcurrentHashMap`](https://github.com/facebook/folly/blob/main/folly/concurrency/ConcurrentHashMap.h)<br>➕ concurrent lookup/update and reclamation<br>➖ not Z intrusive nodes or named hash/heap tuning | Partial: [`bdlcc::StripedUnorderedMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_stripedunorderedmap.h)<br>➕ concurrent operations with striped locking<br>➖ no named hash/heap tuning registry |
| [`ZmHashMgr`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmHashMgr.hh)<br>➕ named sizing, striping and telemetry | N/A; per-container load-factor/rehash controls only | Partial: [Boost.Unordered](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html) controls with custom registry | N/A | N/A |
| [`ZmPolyHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolyHash.hh)<br>➕ reflected multiple intrusive hash indices in one allocation | N/A; multiple containers with manual index maintenance | [Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html)<br>➕ also supports ordered, sequenced and other index kinds | N/A | N/A |
| [`ZmCacheStats`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCacheStats.hh) | N/A; application counters | Partial: [Boost.Accumulators](https://www.boost.org/doc/libs/latest/doc/html/accumulators.html) or custom counters | N/A | N/A |
| [`ZmCache`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmCache.hh)<br>➕ integrated hash lookup and LRU eviction | Partial: [`std::list`](https://en.cppreference.com/cpp/container/list) + [`std::unordered_map`](https://en.cppreference.com/cpp/container/unordered_map) | Partial: [Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) hashed + sequenced indices | [`folly::EvictingCacheMap`](https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h)<br>➖ different locking/node/heap policy | Partial: [`bdlcc::Cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h)<br>➕ LRU/FIFO eviction and allocator support |
| [`ZmPolyCache`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPolyCache.hh)<br>➕ reflected multiple keys with LRU eviction | N/A; custom multi-index cache | Partial: [Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) + eviction policy | Partial: [`folly::EvictingCacheMap`](https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h)<br>➖ no reflected multiple-key indices | Partial: [`bdlcc::Cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h)<br>➖ no built-in reflected multiple-key indices |
| [`ZmPQueue`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh), [`ZmPQRx`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh), [`ZmPQTx`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmPQueue.hh)<br>➕ deterministic skip-list sequence reassembly; gaps and overlaps | Partial: [`std::map`](https://en.cppreference.com/cpp/container/map) plus interval/reassembly logic | Partial: [Boost.ICL](https://github.com/boostorg/icl/blob/master/include/boost/icl/interval_set.hpp) + ordered index | Partial: [`folly::ConcurrentSkipList`](https://github.com/facebook/folly/blob/main/folly/ConcurrentSkipList.h) + application sequence/reassembly logic | Partial: [`bdlcc::SkipList`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_skiplist.h) + application sequence/reassembly logic |

## ZmRing — inter-thread communication

[`ZmRing`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh) carries records between threads; [`ZmRingFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingFn.hh) adds in-ring callable
dispatch. Folly provides related fixed-record ring facilities: its FIFO
`ProducerConsumerQueue` and cursor-based overwrite-history `LockFreeRingBuffer`.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmRingMirror`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingMirror.hh)<br>➕ double-mapped storage simplifies wraparound records | N/A | Partial: [Boost.Interprocess](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html) mapping primitives<br>➖ mirror arrangement is application work | N/A | N/A |
| [`ZmRing`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRing.hh)<br>➕ fixed/variable records, producer policies and broadcast readers | N/A; custom bounded queue | Partial: [Boost.Lockfree](https://www.boost.org/doc/libs/latest/doc/html/lockfree.html) queues<br>➖ different record/broadcast contracts | Partial: [`folly::ProducerConsumerQueue`](https://github.com/facebook/folly/blob/main/folly/ProducerConsumerQueue.h) (SPSC FIFO ring); [`folly::LockFreeRingBuffer`](https://github.com/facebook/folly/blob/main/folly/concurrency/container/LockFreeRingBuffer.h) (concurrent cursor-based overwrite ring) | Partial: [`bdlcc::SingleProducerSingleConsumerBoundedQueue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_singleproducersingleconsumerboundedqueue.h) (typed SPSC communication) |
| [`ZmRingFn`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmRingFn.hh)<br>➕ captured callable payloads stored in variable-record rings | Partial: callable wrapper queued in a container | Partial: [Boost.Function](https://www.boost.org/doc/libs/latest/doc/html/function.html) + [Lockfree](https://www.boost.org/doc/libs/latest/doc/html/lockfree.html) queue composition | Partial: [`folly::Function with a Folly queue`](https://github.com/facebook/folly/blob/main/folly/Function.h) (callable queueing) | Partial: [`bdlcc::BoundedQueue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_boundedqueue.h)`<`[`bsl::function`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)`<...>>` (callable queueing) |

## Topology and threads

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmBitmap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmBitmap.hh)<br>➕ hwloc CPU/resource bitmap operations | Partial: [`std::vector`](https://en.cppreference.com/cpp/container/vector)`<bool>`<br>➖ no CPU-set semantics | [`boost::dynamic_bitset`](https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp)<br>➖ no topology integration | N/A | Partial: [`bdlc::BitArray`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_bitarray.h)<br>➖ no hwloc CPU-set/topology integration |
| [`ZmTopology`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmTopology.hh)<br>➕ hwloc CPU/NUMA discovery and binding | Partial: [`std::thread::hardware_concurrency`](https://en.cppreference.com/cpp/thread/thread/hardware_concurrency)<br>➖ only a concurrency hint | N/A; platform/hwloc integration required | Partial: [`folly::CacheLocality / AccessSpreader`](https://github.com/facebook/folly/blob/main/folly/concurrency/CacheLocality.h)<br>➖ not the full hwloc topology/binding surface | N/A |
| [`ZmThread`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmThread.hh) / [`ZmThreadParams`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmThread.hh)<br>➕ named affinity, stack, priority and partition configuration | [`std::thread`](https://en.cppreference.com/cpp/thread/thread), [`std::jthread`](https://en.cppreference.com/cpp/thread/jthread)<br>➕ [`jthread`](https://en.cppreference.com/cpp/thread/jthread) joins automatically and supplies stop tokens<br>➖ native tuning needs platform code | [`boost::thread`](https://github.com/boostorg/thread/blob/master/include/boost/thread/thread.hpp) / thread attributes | Partial: [`folly::NamedThreadFactory / InitThreadFactory`](https://github.com/facebook/folly/blob/main/folly/executors/thread_factory/NamedThreadFactory.h)<br>➖ full stack/priority/partition setup needs composition | Partial: [`bslmt::ThreadUtil`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadutil.h)`  +  `[`ThreadAttributes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadattributes.h)<br>➖ no built-in hwloc partition/affinity policy |

## Scheduling and engine lifecycle

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZmScheduler`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScheduler.hh) / [`ZmSchedParams`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmScheduler.hh)<br>➕ named destinations, dedicated threads, timers and ring dispatch<br>➕ affinity/partition/queue configuration | N/A in C++23; C++26 [`std::execution`](https://en.cppreference.com/cpp/execution) is a partial asynchronous-execution counterpart | [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`io_context`](https://github.com/boostorg/asio/blob/master/include/boost/asio/io_context.hpp), thread pool, timers and strands<br>➕ executor/completion-token ecosystem<br>➖ matching Z destination/affinity policies needs composition | Partial: [`folly::IOThreadPoolExecutor / CPUThreadPoolExecutor`](https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.h)<br>➕ EventBase/future/coroutine executor ecosystem<br>➖ matching named destinations, timers and affinity needs composition | Partial: [`bdlmt::MultiQueueThreadPool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_multiqueuethreadpool.h)`  +  `[`EventScheduler`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_eventscheduler.h)<br>➕ serialized job queues and recurring/one-shot scheduling<br>➖ serialized queues are not bound to fixed worker threads |
| [`ZmEngine`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZmEngine.hh)<br>➕ reusable asynchronous start/stop lifecycle and pending operations | N/A | Partial: [Boost.Statechart](https://www.boost.org/doc/libs/latest/libs/statechart/doc/reference.html) / MSM plus [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) | N/A | Partial: [`bdlmt::ThreadPool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_threadpool.h)` (worker-`[`pool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_pool.h)` lifecycle only)` |

## Runtime telemetry

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZtcHeap`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcHeap.hh), [`ZtcHash`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcHash.hh), [`ZtcQueue`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcQueue.hh), [`ZtcThread`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcThread.hh), [`ZtcTypes`](https://github.com/kaldron-labs/z/blob/main/zm/src/ZtcTypes.hh) (in `zm`)<br>➕ reflected runtime telemetry vocabulary | N/A | Partial: application-defined [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) / [Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html) records | Partial: [`balm::MetricsManager`](https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_metricsmanager.h)`  /  `[`bdlm::MetricsRegistry`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlm/bdlm_metricsregistry.h) |

# Zt

Framework types.

## Platform and environment

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zt/src/), [module guide](../zt/README.md).
Container comparisons use [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html).

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZtPlatform`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtPlatform.hh) environment helpers | Partial: [`std::getenv`](https://en.cppreference.com/cpp/utility/program/getenv)<br>➖ no standard environment setter | [Boost.Process](https://github.com/boostorg/process/blob/master/include/boost/process.hpp) environment facilities | Partial: [`bsl::getenv`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_cstdlib.h)`  +  `[`bdls::OsUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_osutil.h) |

## Boolean and enum vocabulary

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| [`ZtScanBool`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtScanBool.hh#L22)<br>➕ accepts case-insensitive yes/no, y/n, true/false and 1/0 | Partial: [`std::boolalpha`](https://en.cppreference.com/cpp/io/manip/boolalpha) input | Partial: [Boost.Convert](https://github.com/boostorg/convert/blob/master/include/boost/convert.hpp) / LexicalCast | Partial: [`folly::to<bool> / tryTo<bool>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ accepted boolean spellings/error behavior differ |
| [`ZtBytesFmt`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBytesFmt.hh)<br>➕ shared binary-text encoding vocabulary | N/A | N/A; codec-specific options | N/A |
| [`ZtcRAGMap`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtcRAGMap.hh) (in `zt`)<br>➕ common telemetry vocabulary with generated name lookup, conversion and iteration | N/A; an enum declaration alone does not generate the name map | Partial: enum + [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) | N/A |
| [`ZtEnum`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtEnum.hh#L106) macro family<br>➕ integer enum vocabulary with generated name/ordinal matching and iteration | N/A; native enums have no generated runtime name mapping in C++23 | [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) enum descriptors<br>➕ metadata can support other generic algorithms | N/A |

## Text formatting

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZtQuote`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtQuote.hh)<br>➕ quoting and binary-text formatting adapters | Partial: [`std::quoted`](https://en.cppreference.com/cpp/io/manip/quoted) | Partial: [Boost.IO](https://www.boost.org/doc/libs/latest/libs/io/doc/html/io.html) quoted manipulators | N/A | Partial: [`bdlb::LiteralUtil::createQuotedEscapedCString`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_literalutil.h) |
| [`ZtHexDump`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtHexDump.hh) | N/A; composed formatting loop | Partial: [Boost.Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) [`dump`](https://github.com/boostorg/json/blob/master/include/boost/json/serialize.hpp) manipulator | [`folly::hexDump`](https://github.com/facebook/folly/blob/main/folly/String.h) | [`bdlb::Print::hexDump`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_print.h) |
| [`ZtFmt`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtFmt.hh)<br>➕ reflected-field printing/scanning policies | Partial: [`std::format`](https://en.cppreference.com/cpp/utility/format/format), [`std::from_chars`](https://en.cppreference.com/cpp/utility/from_chars) | Partial: [Boost.Format](https://www.boost.org/doc/libs/latest/libs/format/doc/format.html) / [Convert](https://github.com/boostorg/convert/blob/master/include/boost/convert.hpp) | N/A | Partial: [`bsl::format`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_format.h) |

## Character case and encoding conversion

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZtCase`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtCase.hh) | Partial: [`<cctype>`](https://en.cppreference.com/cpp/header/cctype), [`<cwctype>`](https://en.cppreference.com/cpp/header/cwctype), locale facets | [Boost.StringAlgo](https://www.boost.org/doc/libs/latest/doc/html/string_algo/reference.html) / [Locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html) case conversion | Partial: [`folly::toLowerAscii / toUpperAscii`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ ASCII transformations rather than general locale/encoding handling | Partial: [`bdlb::String::toLower`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_string.h)` / toUpper`<br>➖ case folding only; no snake_case/camelCase conversion |
| [`ZtIconv`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtIconv.hh)<br>➕ iconv conversions into adapted output buffers | Partial: legacy locale conversion facilities | [Boost.Locale](https://www.boost.org/doc/libs/latest/libs/locale/doc/html/index.html) encoding conversion | N/A | Partial: [`bdlde::CharConvertUtf16`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h)`  /  `[`CharConvertUtf32`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf32.h)<br>➖ UTF conversion only; no arbitrary iconv encoding pairs |

## Owning arrays, strings and scratch buffers

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZtArray`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtArray.hh)<br>➕ owning or shadowing storage and direct capacity control<br>➖ ownership mode and initialization require care | [`std::vector`](https://en.cppreference.com/cpp/container/vector)<br>➕ uniform owning semantics and standard iterators | [`boost::container::vector`](https://github.com/boostorg/container/blob/master/include/boost/container/vector.hpp) | [`folly::fbvector`](https://github.com/facebook/folly/blob/main/folly/container/FBVector.h)<br>➖ owning storage; no Z shadowing mode | [`bsl::vector`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_vector.h)<br>➕ allocator propagation<br>➖ owning container; no non-owning shadow mode |
| [`ZtString`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh) / [`ZtWString`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh)<br>➕ inline storage, named heaps and stream-style appending | [`std::string`](https://en.cppreference.com/cpp/string/basic_string), [`std::wstring`](https://en.cppreference.com/cpp/string/basic_string)<br>➕ standard string/range integration | [`boost::container::basic_string`](https://github.com/boostorg/container/blob/master/include/boost/container/string.hpp) | Partial: [`folly::fbstring`](https://github.com/facebook/folly/blob/main/folly/FBString.h)<br>➖ different allocator/shadowing and wide-string interface | [`bsl::basic_string`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_string.h)<br>➕ allocator-aware strings<br>➖ owning container; no non-owning shadow mode |
| [`ZtBuiltin`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBuiltin.hh)<br>➕ adds an initial inline buffer to array storage | N/A; [`std::inplace_vector`](https://en.cppreference.com/cpp/container/inplace_vector) (C++26) cannot spill to heap | [`boost::container::small_vector`](https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp) | [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h) | Partial: [`bdlma::LocalBufferedObject`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localbufferedobject.h)` with a BSL container` |
| [`ZtScratch`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtScratch.hh#L83)<br>➕ [ZtArray](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtArray.hh)/[ZtString](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtString.hh) operations over runtime stack backing with heap fallback and cleanup | Partial: [`std::vector`](https://en.cppreference.com/cpp/container/vector)<br>➖ no automatic stack-backed allocation path | Partial: [`boost::container::small_vector`](https://github.com/boostorg/container/blob/master/include/boost/container/small_vector.hpp)<br>➖ inline capacity is compile-time fixed | Partial: [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h)<br>➖ fixed inline size rather than stack-availability-based selection | Partial: [`bdlma::LocalSequentialAllocator`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h)` + BSL containers`<br>➖ local buffer capacity is fixed; no runtime available-stack check |

## Bitmaps and sliding windows

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZtBitmap`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBitmap.hh) | Partial: [`std::vector`](https://en.cppreference.com/cpp/container/vector)`<bool>` | [`boost::dynamic_bitset`](https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp) | [`bdlc::BitArray`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_bitarray.h) |
| [`ZtWindow`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtWindow.hh)<br>➕ indexed sliding window with assignment-driven extension | Partial: [`std::deque`](https://en.cppreference.com/cpp/container/deque) with sequence-index bookkeeping | Partial: [Boost.CircularBuffer](https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp) | N/A |
| [`ZtBitWindow`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtBitWindow.hh)<br>➕ sliding packed values in selected bit widths | N/A; custom packed circular storage | Partial: [Boost.DynamicBitset](https://github.com/boostorg/dynamic_bitset/blob/master/include/boost/dynamic_bitset.hpp) + [CircularBuffer](https://github.com/boostorg/circular_buffer/blob/master/include/boost/circular_buffer.hpp) concepts | N/A |

## Timezone conversion

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`Zt::tzOffset`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtTimeZone.hh#L37) ([`ZtTimeZone`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtTimeZone.hh)`.hh`)<br>➖ explicit zones temporarily change [`TZ`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtTimeZone.hh) under a global lock | [`std::chrono::time_zone`](https://en.cppreference.com/cpp/chrono/time_zone), [`zoned_time`](https://en.cppreference.com/cpp/chrono/zoned_time) (C++20)<br>➕ independent zone objects and timezone database | [Boost.DateTime](https://www.boost.org/doc/libs/latest/doc/html/date_time.html) timezone facilities | [`baltzo::TimeZoneUtil`](https://github.com/bloomberg/bde/blob/main/groups/bal/baltzo/baltzo_timezoneutil.h)<br>➕ explicit zone-info loading/cache and DST handling |

## Demangling

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| [`ZtDemangle`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtDemangle.hh)<br>➕ rewrites Z structural-string/template names for readability | N/A | Partial: [Boost.Core](https://github.com/boostorg/core/blob/master/include/boost/core/bit.hpp) demangle + custom postprocessing | Partial: [`folly::demangle`](https://github.com/facebook/folly/blob/main/folly/Demangle.h)<br>➖ no Z-specific template-name rewriting |

## Regular expressions

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZtRegex`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtRegex.hh) / [`ZtREGEX`](https://github.com/kaldron-labs/z/blob/main/zt/src/ZtRegex.hh#L290)<br>➕ PCRE syntax, captures and substitutions<br>➖ external PCRE dependency | [`std::regex`](https://en.cppreference.com/cpp/regex/basic_regex)<br>➖ different syntax/feature contract | [Boost.Regex](https://www.boost.org/doc/libs/latest/libs/regex/doc/html/boost_regex/ref.html) | [`bdlpcre::RegEx`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlpcre/bdlpcre_regex.h)<br>➕ compiled PCRE expression wrapper |

# Ze

Framework error handling.

## Native errors

Sources: [ZePlatform.hh](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh), [module guide](../ze/README.md).
[Ze](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) constructs diagnostics; the asynchronous sink runtime is [`ZiLog`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh) below.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZeOK`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) | Default-constructed success [`std::error_code`](https://en.cppreference.com/cpp/error/error_code) | Default-constructed success [Boost.System](https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html) [`error_code`](https://github.com/boostorg/system/blob/master/include/boost/system/error_code.hpp) | N/A | N/A |
| [`ZeError`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh)<br>➕ native OS-error wrapper and message formatting | [`std::error_code`](https://en.cppreference.com/cpp/error/error_code)<br>➕ extensible error categories and conditions | [Boost.System](https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html) [`error_code`](https://github.com/boostorg/system/blob/master/include/boost/system/error_code.hpp) | Partial: [`folly::makeSystemErrorExplicit`](https://github.com/facebook/folly/blob/main/folly/Exception.h)<br>➖ creates system_error rather than Z's numeric wrapper | Partial: [`bsl::error_code`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h)`  /  `[`system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h) |
| [`ZeLastError`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh)<br>➕ captures native last-error state at the call site | Partial: capture [`errno`](https://en.cppreference.com/cpp/error/errno) / native error into [`std::error_code`](https://en.cppreference.com/cpp/error/error_code) | Partial: native error capture + [Boost.System](https://www.boost.org/doc/libs/latest/libs/system/doc/html/system.html) | Partial: [`folly::makeSystemError`](https://github.com/facebook/folly/blob/main/folly/Exception.h)<br>➖ errno-based helper rather than Z's separate Windows OS/socket capture | Partial: [`bsl::error_code`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h)`  /  `[`system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h) |
| [`ZeLastSockError`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh)<br>➕ distinguishes Windows socket errors from ordinary OS errors | N/A; native call + [`std::error_code`](https://en.cppreference.com/cpp/error/error_code) | Partial: [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) socket operation error reporting | N/A | Partial: [`bsl::error_code`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h)`  /  `[`system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h) |

## Diagnostic metadata and events

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`Ze::Debug`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) / [`Info`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) / [`Warning`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) / [`Error`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZeLib.hh) / [`Fatal`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh)<br>➕ shared severity vocabulary with generated name lookup and event/log integration | N/A; a plain enum alone does not provide the generated mapping or integration | [Boost.Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) [`trivial::severity_level`](https://github.com/boostorg/log/blob/master/include/boost/log/trivial.hpp) | [`folly::LogLevel`](https://github.com/facebook/folly/blob/main/folly/logging/LogLevel.h) | [`ball::Severity`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_severity.h) |
| [`ZeEventInfo`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh)<br>➕ timestamp, source, severity and component identity | Partial: [`std::source_location`](https://en.cppreference.com/cpp/utility/source_location) + custom event record | [Boost.Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) attributes / record metadata | Partial: [`folly::LogMessage metadata`](https://github.com/facebook/folly/blob/main/folly/logging/LogMessage.h) | [`ball::RecordAttributes`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_recordattributes.h) |
| [`ZeEvent`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) / [`ZeAnyEvent`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) / [`ZeMsgFn`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh)<br>➕ structured events with deferred or type-erased formatting | N/A; custom event type and callable | Partial: [Boost.Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) record and message attributes | Partial: [`folly::LogMessage`](https://github.com/facebook/folly/blob/main/folly/logging/LogMessage.h)<br>➖ different message ownership/formatting contract | Partial: [`ball::Record`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_record.h) |
| [`ZeEVENT`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L375) / [`ZeMkEvent`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L364)<br>➕ call-site metadata or explicit metadata construction | Partial: [`std::source_location::current`](https://en.cppreference.com/cpp/utility/source_location/current)`()` + custom factory | Partial: [Boost.Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) source macros and attributes | Partial: [`XLOG / XLOGF`](https://github.com/facebook/folly/blob/main/folly/logging/xlog.h)<br>➖ constructs and emits a log record, not a standalone [Ze](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) event | Partial: [`BALL_LOG_*`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_log.h) |

## Exceptions

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZeException`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh)<br>➕ same diagnostic vocabulary as logging events<br>➖ not the [`std::exception::what`](https://en.cppreference.com/cpp/error/exception/what)`()` interface | [`std::exception`](https://en.cppreference.com/cpp/error/exception), [`std::system_error`](https://en.cppreference.com/cpp/error/system_error)<br>➕ standard catch and message interface | [Boost.Exception](https://www.boost.org/doc/libs/latest/libs/exception/doc/boost-exception.html)<br>➕ extensible attached diagnostic information | Partial: [`folly::exception_wrapper`](https://github.com/facebook/folly/blob/main/folly/ExceptionWrapper.h)<br>➖ type-erased existing exception, not a structured [Ze](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh) diagnostic event | Partial: [`bsl::system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h) |
| [`ZeEXCEPT`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L391) / [`ZeMkException`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L380) | Partial: construct exception with [`std::source_location`](https://en.cppreference.com/cpp/utility/source_location) metadata | Partial: [`BOOST_THROW_EXCEPTION`](https://github.com/boostorg/throw_exception/blob/master/include/boost/throw_exception.hpp) / [`boost::enable_error_info`](https://github.com/boostorg/exception/blob/master/include/boost/exception/enable_error_info.hpp)<br>➖ throw macro throws; [`ZeEXCEPT`](https://github.com/kaldron-labs/z/blob/main/ze/src/ZePlatform.hh#L391) only constructs | Partial: [`folly::makeSystemErrorExplicit`](https://github.com/facebook/folly/blob/main/folly/Exception.h)<br>➖ system-error construction, not arbitrary severity/component event construction | Partial: [`bsl::system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_system_error.h) |

# Zf

Structured metadata and format adapters.

## Configuration trees and typed loading

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zf/src/), [module guide](../zf/README.md).
Closest reflection counterparts: [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html),
[Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html).
JSON counterpart: [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html).
Format parsers assembled with Spirit are **partial** equivalents, not supplied codecs.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZfTree`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfTree.hh)<br>➕ owned typed configuration nodes and diagnostic paths | Partial: recursive [`std::variant`](https://en.cppreference.com/cpp/utility/variant) / containers | Partial: [Boost.PropertyTree](https://www.boost.org/doc/libs/latest/doc/html/property_tree.html)<br>➖ scalar typing and structural contracts differ | Partial: [`folly::dynamic`](https://github.com/facebook/folly/blob/main/folly/json/dynamic.h)<br>➖ JSON value model, not the shared Z configuration tree contract | Partial: [`bdld::Datum`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdld/bdld_datum.h)`  /  `[`ManagedDatum`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdld/bdld_manageddatum.h) |
| [`ZfTreeLoad`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfTreeLoad.hh)<br>➕ shared construction/load/update and validation policies | N/A | Partial: [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) + custom tree conversion | Partial: [`folly::DynamicParser / DynamicConverter`](https://github.com/facebook/folly/blob/main/folly/json/DynamicParser.h)<br>➖ typed conversion requires application bindings; no Z field/update policies | Partial: [`bdlat_SymbolicConverter`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_symbolicconverter.h) |

## Structure reflection and serialization metadata

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZfStruct`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfStruct.hh#L3438)<br>➕ typed/runtime field metadata, facets and serialization properties in C++23<br>➕ constructor/load/update/key policies shared across formats<br>➖ substantial template/macro machinery | N/A in C++23; core C++23 has no matching reflection/serialization framework | Partial: [Boost.Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) / [Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) + [Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html)<br>➕ [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html) supplies a broader heterogeneous metaprogramming library<br>➖ matching Z's format/facet/update policies requires additional code | Partial: [`bdlat`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_typecategory.h)`  adaptation protocols +  `[`baljsn`](https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_encoder.h)`/`[`balxml`](https://github.com/bloomberg/bde/blob/main/groups/bal/balxml/balxml_encoder.h)`/`[`balber`](https://github.com/bloomberg/bde/blob/main/groups/bal/balber/balber_berencoder.h)` codecs`<br>➕ shared model for sequence/choice/enum/array/nullable types |

## Text and web formats

| Z facility | STL | Boost | Folly | BDE / NTF |
| --- | --- | --- | --- | --- |
| [`ZfCSV`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCSV.hh)<br>➕ streaming rows with reflected header/column binding | N/A | Partial: [Boost.Tokenizer](https://www.boost.org/doc/libs/latest/libs/tokenizer/doc/tokenizer.htm) or [Spirit](https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp)<br>➖ row construction, quoting and field binding require composition | N/A | N/A |
| [`ZfURI`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfURI.hh)<br>➕ typed path/query/form binding and direct path loading | N/A | [Boost.URL](https://github.com/boostorg/url/blob/master/include/boost/url.hpp)<br>➕ URI parsing, components and normalization<br>➖ no matching reflected field/facet loader | Partial: [`folly::Uri`](https://github.com/facebook/folly/blob/main/folly/Uri.h)<br>➖ URI component model rather than reflected path/query/form binding | Partial: [`ntsa::Uri`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h) |
| [`ZfJSON`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfJSON.hh)<br>➕ reflected load/save, fixed-field matching and in-place decoding<br>➖ borrowed text requires mutable input and retained backing | N/A | [Boost.JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)<br>➕ general [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) values, streaming and type conversion<br>➖ different metadata/update contract | Partial: [`folly::parseJson / toJson + dynamic conversion`](https://github.com/facebook/folly/blob/main/folly/json/json.h)<br>➖ no Z reflected facets, constructor/update policy or fixed-field automaton | Partial: [`baljsn::Encoder`](https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_encoder.h)`  /  `[`Decoder`](https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_decoder.h)<br>➕ codecs over bdlat-adapted types |

## Configuration formats

| Z facility | STL | Boost |
| --- | --- | --- |
| [`ZfCf`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCf.hh)<br>➕ native configuration syntax, substitutions and typed updates | N/A | Partial: [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html) / [PropertyTree](https://www.boost.org/doc/libs/latest/doc/html/property_tree.html)<br>➖ different syntax and binding model |
| [`ZfTOML`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfTOML.hh)<br>➕ owned typed tree, field binding, formatting and parser limits | N/A | N/A; [Boost.PropertyTree](https://www.boost.org/doc/libs/latest/doc/html/property_tree.html) has no built-in TOML parser |
| [`ZfYAML`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfYAML.hh)<br>➕ owned typed tree, field binding and bounded parsing<br>➖ restricted single-document subset; rejects tags and cyclic/shared graphs | N/A | N/A; [Boost.PropertyTree](https://www.boost.org/doc/libs/latest/doc/html/property_tree.html) has no built-in YAML parser |

## Command-line binding

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZfCLI`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCLI.hh)<br>➕ metadata-bound options/positionals, command text and argv output | N/A | [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html)<br>➕ established option descriptions, parsing and notification<br>➖ reflected facets and reverse serialization need custom work | Partial: [`folly::NestedCommandLineApp`](https://github.com/facebook/folly/blob/main/folly/cli/NestedCommandLineApp.h)<br>➕ nested command structure<br>➖ no matching reflected argv/command reverse serialization | Partial: [`balcl::CommandLine`](https://github.com/bloomberg/bde/blob/main/groups/bal/balcl/balcl_commandline.h)<br>➕ typed option specifications and validation<br>➖ no argv/command-line serialization from the bound object |

## Binary wire formats

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZfCBOR`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfCBOR.hh)<br>➕ reflected binary object/array mapping | N/A | Partial: [Boost.JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) CBOR example<br>➖ example-level codec, not matching reflected API | N/A |
| [`ZfASN1`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfASN1.hh)<br>➕ schema properties bind C++ fields to tags and wire encodings | N/A | N/A; [Spirit](https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp)-based ASN.1 implementation would be custom work | Partial: [`balber::BerEncoder`](https://github.com/bloomberg/bde/blob/main/groups/bal/balber/balber_berencoder.h)`  /  `[`BerDecoder`](https://github.com/bloomberg/bde/blob/main/groups/bal/balber/balber_berdecoder.h) |
| [`ZfDBUS`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfDBUS.hh)<br>➕ reflected signatures, alignment and message-body serialization | N/A | N/A; no dedicated Boost D-Bus structural codec | N/A |

# Zi

Framework I/O.

## Native paths and scatter/gather descriptors

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zi/src/), [module guide](../zi/README.md).
Networking counterparts are in [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html).
The STL has no C++23 socket/event-loop API.

| Z facility | STL | Boost | Folly | BDE / NTF |
| --- | --- | --- | --- | --- |
| [`ZiPlatform`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh) / [`Zi::Path`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiDir.hh) and native I/O types | Partial: [`std::filesystem::path`](https://en.cppreference.com/cpp/filesystem/path)<br>➖ no standard socket/native-handle abstraction | Partial: [Boost.Filesystem](https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html), [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html), WinAPI | N/A | Partial: [`bdls::PathUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_pathutil.h)`  /  `[`FilesystemUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h) |
| [`ZiVec`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh) / [`ZiVec_ptr`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh#L104) / [`ZiVec_len`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPlatform.hh#L105)<br>➕ portable native scatter/gather descriptors | Partial: [`std::span`](https://en.cppreference.com/cpp/container/span)<br>➖ no native scatter/gather syscall adapter | [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) mutable/const buffer sequences | Partial: [`folly::IOBuf iovec conversion`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➖ chained-buffer adapter rather than portable native descriptor aliases | Partial: [`ntsa::ConstBuffer`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_buffer.h)`  /  `[`MutableBuffer`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_buffer.h) |

## Process and module utilities

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZiProgram`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiProgram.hh)<br>➕ current executable/process information | N/A | Partial: [Boost.DLL](https://www.boost.org/doc/libs/latest/doc/html/boost_dll/reference.html) program location + [Process](https://github.com/boostorg/process/blob/master/include/boost/process.hpp) | Partial: [`bdls::ProcessUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_processutil.h) |
| [`ZiModule`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiModule.hh) | N/A; OS loader calls | [Boost.DLL](https://www.boost.org/doc/libs/latest/doc/html/boost_dll/reference.html) | N/A |
| [`ZiPIDFile`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiPIDFile.hh)<br>➕ PID file ownership and cleanup | N/A | Partial: [Boost.Interprocess](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html) file lock + custom PID file | N/A |
| [`ZiDaemon`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiDaemon.hh) | N/A | Partial: [Boost.Process](https://github.com/boostorg/process/blob/master/include/boost/process.hpp) plus platform daemon setup | N/A |

## Directories and path completion

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZiDir`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiDir.hh) | [`std::filesystem::directory_iterator`](https://en.cppreference.com/cpp/filesystem/directory_iterator) | [Boost.Filesystem](https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html) directory iterator | Partial: [`bdls::FilesystemUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h)` path matching/visitation` |
| [`ZiGlob`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiGlob.hh)<br>➕ directory/path completion for interactive CLIs<br>➖ not a full glob-expression engine | Partial: [`std::filesystem`](https://en.cppreference.com/cpp/filesystem) iteration + prefix matching | Partial: [Boost.Filesystem](https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html) + [StringAlgo](https://www.boost.org/doc/libs/latest/doc/html/string_algo/reference.html) | Partial: [`bdls::FilesystemUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h)` path matching/visitation` |

## File operations and structured output

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZiFile`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiFile.hh)<br>➕ direct native unbuffered file operations | Partial: [`std::fstream`](https://en.cppreference.com/cpp/io/basic_fstream), [`std::filesystem`](https://en.cppreference.com/cpp/filesystem)<br>➖ stream buffering and native-operation APIs differ | Partial: [Boost.Filesystem](https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html) / [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) file APIs / [Iostreams](https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html) file descriptors | [`folly::File + FileUtil operations`](https://github.com/facebook/folly/blob/main/folly/File.h)<br>➕ RAII descriptor ownership and file locking | Partial: [`bdls::FilesystemUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h)`  +  `[`FdStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_fdstreambuf.h)<br>➕ allocator-aware stream buffering and filesystem utilities |
| [`ZiFileTxStream`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiFileTxStream.hh)<br>➕ file-backed buffered Z transmit stream | [`std::ofstream`](https://en.cppreference.com/cpp/io/basic_ofstream) | [Boost.Iostreams](https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html) file sink / stream | Partial: [`folly::writeFull`](https://github.com/facebook/folly/blob/main/folly/FileUtil.h)<br>➖ full-write helper, not a buffered Z transmit stream | Partial: [`bdls::FdStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_fdstreambuf.h) |
| [`ZiCSV`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiCSV.hh)<br>➕ file/stream integration for reflected CSV records | N/A; fstreams + custom CSV binding | Partial: [Boost.Iostreams](https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html) + [Tokenizer](https://www.boost.org/doc/libs/latest/libs/tokenizer/doc/tokenizer.htm)/[Spirit](https://github.com/boostorg/spirit/blob/master/include/boost/spirit.hpp) | N/A | N/A |

## Addresses and name resolution

| Z facility | STL | Boost | Folly | NTF |
| --- | --- | --- | --- | --- |
| [`ZiIP`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIP.hh) | N/A | [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`ip::address`](https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/address.hpp) / endpoint types | [`folly::SocketAddress / IPAddress`](https://github.com/facebook/folly/blob/main/folly/SocketAddress.h) | [`ntsa::IpAddress`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_ipaddress.h)`  /  `[`Endpoint`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_endpoint.h) |
| [`ZiResolver`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiResolver.hh)<br>➕ c-ares-backed asynchronous DNS with controlled runtime lifecycle | N/A | [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`ip::tcp::resolver`](https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/tcp.hpp) / [`ip::udp::resolver`](https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/udp.hpp)<br>➖ DNS configuration/lifecycle surface differs | N/A | [`ntci::Resolver`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_resolver.h)<br>➕ asynchronous name/service resolution and cache configuration |

## I/O buffers, allocation and contexts

| Z facility | STL | Boost | Folly | BDE / NTF |
| --- | --- | --- | --- | --- |
| [`ZiIOBuf`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOBuf.hh)<br>➕ reference-owned packet backing and allocator integration | Partial: [`std::span`](https://en.cppreference.com/cpp/container/span) + owning storage<br>➖ span alone carries no ownership | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) buffers + application-owned storage<br>➖ buffer descriptors alone carry no ownership | [`folly::IOBuf`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➕ chainable buffers with shared storage, headroom and tailroom | Partial: [`bdlbb::Blob`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h)`  /  `[`BlobBuffer`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h)<br>➕ shared segmented backing<br>➖ no integrated headroom/tailroom geometry for layered packet I/O |
| [`Zi::IOBufAllocator`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOBuf.hh), builtin/plugin allocators and allocation roles<br>➕ explicit App/Rx/Tx storage provenance and transport integration | Partial: [`std::pmr::memory_resource`](https://en.cppreference.com/cpp/memory/memory_resource)<br>➖ no packet-backing/provenance contract | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) associated allocators + custom packet pool | Partial: [`folly::memory::makeIOBufArenaFactory`](https://github.com/facebook/folly/blob/main/folly/memory/IOBufArenaFactory.h)<br>➖ no Z App/Rx/Tx role/provenance contract | Partial: [`bdlbb::PooledBlobBufferFactory`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_pooledblobbufferfactory.h)<br>➖ no per-layer headroom/tailroom provisioning contract |
| [`ZiRxContext`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh) / [`ZiTxContext`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh) ([`ZiIOContext`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh)`.hh`)<br>➕ buffer, progress and continuation state for transport I/O | N/A | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) composed-operation state / completion handlers | Partial: [`folly::AsyncTransport ReadCallback / WriteCallback`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncTransport.h)<br>➖ different callback state and lifetime interface | Partial: [`ntca::ReceiveContext`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_receivecontext.h)`  /  `[`SendContext`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_sendcontext.h) |

## Receive and transmit streams

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZiRxStream`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRxStream.hh)<br>➕ consumable view over queued receive buffers | Partial: [`std::istream`](https://en.cppreference.com/cpp/io/basic_istream) over a custom stream buffer | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) DynamicBuffer / [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) [`multi_buffer`](https://github.com/boostorg/beast/blob/master/include/boost/beast/core/multi_buffer.hpp) | Partial: [`folly::io::Cursor + IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/Cursor.h) | Partial: [`bdlbb::InBlobStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h)`  /  `[`OutBlobStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h)<br>➖ no built-in protocol-frame recognition or extraction |
| [`ZiTxStream`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiTxStream.hh)<br>➕ streamed output into packet/frame-sized buffer sequences | Partial: [`std::ostream`](https://en.cppreference.com/cpp/io/basic_ostream) over custom segmented storage | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) buffer sequences / [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) buffers | Partial: [`folly::io::QueueAppender + IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/Cursor.h)<br>➖ frame-size/flush policy needs adaptation | Partial: [`bdlbb::InBlobStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h)`  /  `[`OutBlobStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h)<br>➖ no frame-admission or reserved-header/trailer policy |

## Message framing and transmission

| Z facility | STL | Boost | NTF |
| --- | --- | --- | --- |
| [`ZiRx`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRx.hh)<br>➕ CRTP header/body framing and fragmented-message accumulation | N/A | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`async_read`](https://github.com/boostorg/asio/blob/master/include/boost/asio/read.hpp) / [`async_read_until`](https://github.com/boostorg/asio/blob/master/include/boost/asio/read_until.hpp) + framing state | Partial: [`ntci::StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)` `[`send`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_send.h)`/`[`receive`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_receive.h)` queues`<br>➕ asynchronous operations with flow-control watermarks |
| [`ZiTx`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiTx.hh)<br>➕ CRTP sender and queued completion handling | N/A | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`async_write`](https://github.com/boostorg/asio/blob/master/include/boost/asio/write.hpp) + queued write state | Partial: [`ntci::StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)` `[`send`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_send.h)`/`[`receive`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntcq/ntcq_receive.h)` queues`<br>➕ asynchronous operations with flow-control watermarks |

## Event loops and connection multiplexing

| Z facility | STL | Boost | Folly | NTF |
| --- | --- | --- | --- | --- |
| [`ZiEventLoop`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiEventLoop.hh)<br>➕ external socket/handle readiness integration | N/A | [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) descriptors / handles and async waits | [`folly::EventBase / EventHandler`](https://github.com/facebook/folly/blob/main/folly/io/async/EventBase.h) | [`ntci::Reactor`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_reactor.h)`  /  `[`Proactor`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_proactor.h)<br>➕ readiness and completion driver interfaces |
| [`ZiConnection`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiIOContext.hh) / [`ZiCxnInfo`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh) / [`ZiCxnOptions`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh)<br>➕ transport callbacks, connection state and explicit I/O allocation roles | N/A | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) socket, endpoint and socket options | Partial: [`folly::AsyncSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h)<br>➖ different connection callbacks and allocation contract | Partial: [`ntci::StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)`  /  `[`StreamSocketSession`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocketsession.h) |
| [`ZiMxPlugin`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh)<br>➕ transport-plugin lifecycle integrated with the multiplexer | N/A | Partial: custom [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) service/executor/stream implementation | N/A | N/A |
| [`ZiMultiplex`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh) / [`ZiMxParams`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh)<br>➕ shard-directed I/O with scheduler and allocator integration | N/A | [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) sockets, [`io_context`](https://github.com/boostorg/asio/blob/master/include/boost/asio/io_context.hpp), executors and strands<br>➕ completion tokens and coroutine integration<br>➖ Z shard/buffer ownership contracts need application composition | Partial: [`folly::IOThreadPoolExecutor + EventBase`](https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.h)<br>➖ matching Z Rx/Tx destinations and buffer ownership needs composition | Partial: [`ntci::Interface`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_interface.h)<br>➕ socket/timer factories, thread management and executor/strand integration |

## ZiRing — inter-process communication using shared memory

| Z facility | STL | Boost |
| --- | --- | --- |
| [`ZiRing`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiRing.hh)<br>➕ named cross-process variable-record broadcast ring<br>➖ processes must agree on layout and payload protocol | N/A | Partial: [Boost.Interprocess](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html) shared memory / message queue<br>➖ message queue consumption is not broadcast |

## Logging and assertions

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZiAssert`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiAssert.hh#L40)<br>➕ release fallback with logging/backtrace | Partial: [`assert`](https://en.cppreference.com/cpp/error/assert) + explicit release handling | Partial: [Boost.Assert](https://www.boost.org/doc/libs/latest/libs/assert/doc/html/assert.html) + [Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) + [Stacktrace](https://www.boost.org/doc/libs/latest/doc/html/stacktrace.html) | Partial: [`FOLLY_SAFE_CHECK / FOLLY_SAFE_DCHECK`](https://github.com/facebook/folly/blob/main/folly/lang/SafeAssert.h)<br>➖ no matching release-fallback logging contract | Partial: [`BSLS_ASSERT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h)`  +  `[`balst`](https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktraceutil.h)` stack traces` |
| [`ZiLog`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh) / [`ZiLOG`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh#L400) / [`ZiLogEvent`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiLog.hh#L360)<br>➕ asynchronous [Ze](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiModule.hh)-event fanout to multiple sinks | Partial: [`std::clog`](https://en.cppreference.com/cpp/io/clog), [`std::osyncstream`](https://en.cppreference.com/cpp/io/basic_osyncstream)<br>➖ no severity, sink routing or async log runtime | [Boost.Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp)<br>➕ configurable attributes, filters and sinks | [`Folly logging: XLOG, handlers and AsyncFileWriter`](https://github.com/facebook/folly/blob/main/folly/logging/xlog.h)<br>➕ category hierarchy and configurable handler factories | [`ball::LoggerManager`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_loggermanager.h)`  /  `[`AsyncFileObserver`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_asyncfileobserver.h)<br>➕ categories, rules, observers and asynchronous file output |

## Resource tuning

| Z facility | STL | Boost |
| --- | --- | --- |
| [`ZiHeapTune`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiHeapTune.hh)<br>➕ file-based named heap tuning and measurements | N/A | N/A; custom configuration around [Boost.Pool](https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp) |
| [`ZiHashTune`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiHashTune.hh)<br>➕ file-based hash configuration and measurements | N/A; per-container controls only | N/A; custom configuration around [Boost.Unordered](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html) |

## I/O telemetry

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZtcMx`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcMx.hh), [`ZtcLink`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcLink.hh), [`ZtcPool`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcPool.hh), [`ZtcHub`](https://github.com/kaldron-labs/z/blob/main/zi/src/ZtcHub.hh) (in `zi`)<br>➕ reflected I/O telemetry records | N/A | Partial: application-defined [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) / [Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html) records | Partial: [`balm::MetricsManager`](https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_metricsmanager.h) |

# Zxdp

Linux AF_XDP packet I/O.

## Registered packet storage

Sources: [module guide](../zxdp/README.md), [public headers](https://github.com/kaldron-labs/z/blob/main/zxdp/src/).
Linux-only; excluded from Windows builds.

| Z facility | STL | Boost |
| --- | --- | --- |
| [`Zxdp::PoolParams`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/ZxdpAlloc.hh), [`BufPool`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/ZxdpAlloc.hh)<br>➕ caller-owned intrusive pools over registered Rx/Tx arenas<br>➕ exhaustion returns null without replacement allocation<br>➕ allocation and kernel leases independently protect backing | N/A | Partial: [Boost.Pool](https://github.com/boostorg/pool/blob/master/include/boost/pool/pool.hpp)<br>➖ no registered UMEM or kernel submission lifetime |

## Raw packet I/O and service

| Z facility | STL | Boost |
| --- | --- | --- |
| [`Zxdp::Params`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh), [`Instance`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh)<br>➕ direct AF_XDP packet submission and receive over one interface<br>➕ optional destination-mask steering with host-stack coexistence<br>➖ Linux AF_XDP and suitable kernel/driver support required | N/A | Partial: [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) generic raw sockets<br>➖ kernel socket path rather than AF_XDP rings and registered backing |
| [`Zxdp::Instance::rx`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L62), [`wake`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L65), [`pollTx`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L66), [`close`](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh#L70)<br>➕ configurable spin-before-epoll receive service and explicit wake<br>➕ bounded Tx retirement with retryable kernel-safe close | N/A | Partial: [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) event/cancellation machinery<br>➖ no matching raw-ring ownership, lease retirement or close contract |

# Zutcp

User-mode TCP/IP stack.

## User-mode TCP/IP stack

Sources: [module guide](../zutcp/README.md), [execution plan](../zutcp_plan.md).
These comparisons describe the planned TCP goal-state; implementation is in
progress. Linux-only; excluded from Windows builds.

| Z facility | STL | Boost | Folly | NTF |
| --- | --- | --- | --- | --- |
| [`Zutcp::Stack`](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh), [`StackParams`](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh)<br>➕ user-mode TCP/IP stack over [Zxdp](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh), integrated with [ZiMultiplex](https://github.com/kaldron-labs/z/blob/main/zi/src/ZiMultiplex.hh) and ordinary [Zi](https://github.com/kaldron-labs/z/blob/main/zutcp/src/Zutcp.hh) application interfaces<br>➕ protocol Rx/Tx work runs on the mx owners<br>➖ requires a configured interface and raw packet privileges | N/A | Partial: [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) TCP sockets/executors<br>➖ kernel TCP rather than a user-mode TCP implementation | Partial: [`folly::AsyncSocket / EventBase`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h)<br>➖ kernel TCP rather than an AF_XDP transport backend | Partial: [`ntci::Interface`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_interface.h)`  +  `[`StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)<br>➖ no AF_XDP/user-mode TCP backend |
| [`Zutcp::Allocator`](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpAlloc.hh) and packet geometry<br>➕ [Zi](https://github.com/kaldron-labs/z/blob/main/zutcp/src/Zutcp.hh) providers over caller-owned [Zxdp](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh) pools, with in-place wire construction<br>➕ separate control capacity and bounded ingress retention | N/A | Partial: [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) buffer sequences + custom allocation<br>➖ no registered-packet provider or retention/control reserve contract | Partial: [`folly::IOBuf`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➖ buffer ownership only; no [Zxdp](https://github.com/kaldron-labs/z/blob/main/zxdp/src/Zxdp.hh) registration or protocol admission policy | Partial: [`ntci::DataPool`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h)<br>➖ no registered AF_XDP packet backing |

## TCP connection and recovery

| Z facility | STL | Boost | NTF |
| --- | --- | --- | --- |
| [`Zutcp::Connection`](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpConnection.hh), [`CliConnection`](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpCliConnection.hh), [`SrvConnection`](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpSrvConnection.hh)<br>➕ one connection owns ordered bytes, reassembly, credit, retained Tx data and close<br>➕ bounded pending/live admission and closed-tuple history | N/A | Partial: [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) TCP sockets<br>➖ corresponding TCP state and recovery live in the OS | Partial: [`ntci::StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h) |
| [TCP ACK/window/recovery facilities](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh)<br>➕ cumulative ACKs, optional SACK/DSACK, scaling and timestamps/PAWS<br>➕ RACK-TLP, RTO/persist and ECN/NewReno on explicit owners | N/A | N/A; [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) delegates these mechanisms to kernel TCP | N/A |
| [TCP initial sequences and Fast Open](https://github.com/kaldron-labs/z/blob/main/zutcp/src/ZutcpMx.hh)<br>➕ secure keyed ISNs and bounded cookie state independent of TLS<br>➕ default-off, application-authorized early data with ordinary fallback | N/A | Partial: platform-specific socket options around kernel TCP<br>➖ no portable user-mode ISN/cookie or early-delivery policy | N/A |

# Zv

Framework server-side utilities.

## Thread, stack and ring parameter binding

Sources: [public headers](https://github.com/kaldron-labs/z/blob/main/zv/src/), [module guide](../zv/README.md).
PropertyTree and ProgramOptions comparisons refer to their configuration
storage/binding roles, not syntax-compatible parsers. Boost's
[library index](https://www.boost.org/doc/libs/latest/libs/libraries.htm)
lists those facilities and the other Boost components referenced above.

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| [`ZvThreadParams`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvThreadParams.hh#L35)<br>➕ validated tree-to-thread settings with default preservation | N/A | Partial: [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html) + [Thread](https://www.boost.org/doc/libs/latest/doc/html/thread.html) attributes | Partial: [`bslmt::ThreadAttributes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadattributes.h) |
| [`ZvStackParams`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvStackParams.hh#L25)<br>➕ binds stack sizing/configuration while retaining defaults | N/A | Partial: [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html) + container configuration | N/A |
| [`ZvRingParams`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvRingParams.hh#L35)<br>➕ binds configuration to shared-memory ring settings | N/A | Partial: [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html) + [Interprocess](https://www.boost.org/doc/libs/latest/doc/html/interprocess.html) setup | N/A |

## Connection and multiplexer parameter binding

| Z facility | STL | Boost |
| --- | --- | --- |
| [`ZvCxnOptions`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvMxParams.hh#L77)<br>➕ connection flags, TTL and multicast binding | N/A | Partial: [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html) + [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) socket options |
| [`ZvMxParams`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvMxParams.hh#L112)<br>➕ binds scheduler threads, destinations and multiplexer settings | N/A | Partial: [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html) + custom [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) runtime settings |

## Configuration file loading and saving

| Z facility | STL | Boost |
| --- | --- | --- |
| [`ZvCf`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvCf.hh)<br>➕ native configuration files with includes, environment expansion and typed binding<br>➖ save/update/delete outputs are not transactional in-place file edits | N/A; fstreams + custom parser/binder | Partial: [Boost.ProgramOptions](https://www.boost.org/doc/libs/latest/doc/html/program_options.html) / [PropertyTree](https://www.boost.org/doc/libs/latest/doc/html/property_tree.html) + [Filesystem](https://www.boost.org/doc/libs/latest/libs/filesystem/doc/reference.html)<br>➖ different syntax; includes/expansion require custom policy |
| [`ZvTOML`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvTOML.hh)<br>➕ file I/O for reflected TOML and its owned tree | N/A | N/A; no built-in Boost TOML loader |
| [`ZvYAML`](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvYAML.hh)<br>➕ file I/O for [Zf](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh)'s reflected YAML subset | N/A | N/A; no built-in Boost YAML loader |

# Zpb

Protobuf implementation.

## Protobuf wire format and typed messages

[Public API](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh) · [Module guide](../zpb/README.md)

| Z facility | Boost |
| --- | --- |
| [`Zpb`](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh) / [`PB`](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh) facet<br>➕ protobuf wire mapping from [ZfStruct](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfStruct.hh) metadata without canonical generated C++ headers<br>➖ unknown fields are skipped rather than retained | Partial: [Boost.Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html)<br>➖ its archives are not protobuf wire format |
| [`Zpb::Builder`](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh), [`Zpb::IOBuilder`](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh)<br>➕ bounded output into caller storage or pooled I/O buffers | Partial: [Boost.Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html) archive over an [Iostreams](https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html) device<br>➖ custom protobuf encoding still required |
| [`Zpb::verify`](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh#L1824), [`handler`](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh#L1834), [`load`](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh#L1844)<br>➕ separate wire verification, borrowed access and typed construction<br>➖ borrowed results require retained backing | N/A; no dedicated Boost protobuf verifier/view API |

## Schema reflection and compiler

| Z facility | Boost |
| --- | --- |
| [`Zpb::Reflect`](https://github.com/kaldron-labs/z/blob/main/zpb/src/ZpbReflect.hh) ([`ZpbReflect`](https://github.com/kaldron-labs/z/blob/main/zpb/src/ZpbReflect.hh)`.hh`)<br>➕ protobuf schema/descriptor representation integrated with Z metadata | Partial: [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) / [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html)<br>➖ no protobuf descriptor model |
| [`zprotoc`](https://github.com/kaldron-labs/z/blob/main/zpb/src/zprotoc.cc)<br>➕ compiles supported protobuf declarations into Z metadata<br>➖ supported declaration profile is narrower than the complete protobuf ecosystem | N/A |

# Zfb

Flatbuffers.

## FlatBuffers construction and access

[Public API](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh) · [Module guide](../zfb/README.md)

| Z facility | Boost |
| --- | --- |
| [`Zfb`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh) / [`FB`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh) facet<br>➕ canonical FlatBuffers mapping from reflected C++ types without canonical generated headers | Partial: [Boost.Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html)<br>➖ archive formats do not supply FlatBuffers layout |
| [`Zfb::Builder`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh), [`Zfb::IOBuilder`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh)<br>➕ downward arena construction and direct pooled I/O output | Partial: [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html) storage + custom builder |
| [`Zfb::view`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L1667), [`handler`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L2738), [`load`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L2799), [`verify`](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh#L2730)<br>➕ borrowed table/vector access, typed construction and structural verification<br>➖ views depend on wire-buffer lifetime | N/A; no dedicated Boost FlatBuffers access/verifier API |

## Schema reflection and compiler

| Z facility | Boost |
| --- | --- |
| [`Zfb::Reflect`](https://github.com/kaldron-labs/z/blob/main/zfb/src/ZfbReflect.hh) ([`ZfbReflect`](https://github.com/kaldron-labs/z/blob/main/zfb/src/ZfbReflect.hh)`.hh`)<br>➕ emits and consumes BFBS/FBS schema metadata from reflected types | Partial: [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) / [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html)<br>➖ no FlatBuffers schema emission |
| [`zflatc`](https://github.com/kaldron-labs/z/blob/main/zfb/src/zflatc.cc)<br>➕ FlatBuffers schema tooling for Z types | N/A |

# Zdbus

D-Bus clients, servers and typed message routing.

## Addresses, authentication and framing

[Public headers](https://github.com/kaldron-labs/z/blob/main/zdbus/src/) · [Module guide](../zdbus/README.md).
Used by the Linux [Ztls](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh) credential Vault; native message bodies use [`ZfDBUS`](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfDBUS.hh).

| Z facility | Boost |
| --- | --- |
| [`ZdbusAddress`](https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusAddress.hh)<br>➕ decodes D-Bus unix addresses into filesystem/abstract socket endpoints | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) local stream endpoints<br>➖ D-Bus address parsing is additional work |
| D-Bus EXTERNAL authentication ([`ZdbusAuth`](https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusAuth.hh)`.hh`)<br>➕ incremental client authentication integrated with connection setup | N/A |
| D-Bus envelopes and pooled messages ([`ZdbusEnvelope`](https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusEnvelope.hh)`.hh`, [`ZdbusMessage`](https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusMessage.hh)`.hh`)<br>➕ bounded framing, header validation and direct pooled construction | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) buffers + custom D-Bus codec |

## Typed message adapters and runtime

| Z facility | Boost |
| --- | --- |
| Request/return/error/signal adapters and catalogs<br>➕ routing headers and typed [ZfDBUS](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfDBUS.hh) bodies share application declarations | Partial: [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) + custom dispatch |
| [`ZdbusClient`](https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusClient.hh)<br>➕ asynchronous calls, serial correlation, named errors and signal subscriptions<br>➖ Linux-only module | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) local sockets, timers and pending-call map<br>➖ no complete Boost D-Bus client |
| [`ZdbusServer`](https://github.com/kaldron-labs/z/blob/main/zdbus/src/ZdbusServer.hh)<br>➕ service-side method routes and typed returns/errors over the shared bus client<br>➖ Linux-only module | N/A; service routing would be application code over [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) |

# Ztcp

Asynchronous TCP links.

## Buffer policies and byte streams

[Public API](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh) · [Module guide](../ztcp/README.md)

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| [`Ztcp::RxBufAlloc`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh), [`TxBufAlloc`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh), [`RxStream`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh), [`TxStream`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh)<br>➕ pooled, reference-owned input/output with explicit allocator roles<br>➖ applications supply message framing | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) buffer sequences and DynamicBuffer<br>➖ matching pool ownership and framing require composition | Partial: [`folly::IOBuf / IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h)<br>➖ no matching transport allocator-role or Z stream interface | Partial: [`ntci::DataPool`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h)`  +  `[`StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h) |
| [`Ztcp::HubParams`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh), [`ClientParams`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh), [`ServerParams`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh)<br>➕ named Rx/Tx destinations and allocator configuration shared by links | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) executors, socket options and application configuration | Partial: [`folly::EventBase + AsyncSocket options`](https://github.com/facebook/folly/blob/main/folly/io/async/EventBase.h)<br>➖ hub/allocator configuration requires application composition | Partial: [`ntca::StreamSocketOptions`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_streamsocketoptions.h)`  /  `[`ListenerSocketOptions`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_listenersocketoptions.h) |

## TCP links and endpoints

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| [`Ztcp::CliLink`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh), [`SrvLink`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh)<br>➕ CRTP connection callbacks, queued writes and reconnect integration | [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`ip::tcp::socket`](https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/tcp.hpp) with composed connection/read/write operations | Partial: [`folly::AsyncSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h)<br>➖ reconnect/hub behavior remains application policy | Partial: [`ntci::StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)`  /  `[`StreamSocketSession`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocketsession.h) |
| [`Ztcp::Client`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh), [`Server`](https://github.com/kaldron-labs/z/blob/main/ztcp/src/Ztcp.hh)<br>➕ hub-managed link/listener lifecycle and restart after drain | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) resolver, sockets and acceptor<br>➖ hub lifecycle and reconnect policy remain application work | Partial: [`folly::AsyncSocket / AsyncServerSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncServerSocket.h)<br>➖ endpoint startup/drain/restart orchestration is additional work | Partial: [`ntci::ListenerSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_listenersocket.h)`  +  `[`StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h) |

# Ztls

TLS transport, cryptography and credential storage.

## Cryptographic primitives

[Public headers](https://github.com/kaldron-labs/z/blob/main/ztls/src/) · [Module guide](../ztls/README.md).
TLS and crypto use zpicotls/OpenSSL; backend requirements are in
[dependencies](DEPENDENCIES.md). Hash/MAC comparisons use
[Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`Ztls::Random`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsRandom.hh)<br>➕ crypto-provider random bytes directly into a caller span | Partial: [`std::random_device`](https://en.cppreference.com/cpp/numeric/random/random_device)<br>➖ implementation may use a pseudorandom engine; no universal cryptographic guarantee | Partial: [Boost.Random](https://www.boost.org/doc/libs/latest/doc/html/boost_random.html) [`random_device`](https://github.com/boostorg/random/blob/master/include/boost/random/random_device.hpp)<br>➖ different device/word-generation interface | [`folly::Random::secureRandom`](https://github.com/facebook/folly/blob/main/folly/Random.h) | [`bdlb::RandomDevice::getRandomBytes`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_randomdevice.h) |
| [`Ztls::ctEqual`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsSec.hh#L23)<br>➕ security-oriented constant-time byte comparison | N/A; ordinary equality algorithms provide no constant-time guarantee | N/A; ordinary range equality provides no constant-time guarantee | N/A | N/A |
| [`Ztls::secretHash`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsSec.hh#L34), [`secretVerify`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsSec.hh#L35)<br>➕ versioned salted secret-hash representation and verification | N/A | Partial: [Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html) + custom salt/format/verification policy | N/A | N/A |
| [`Ztls::MD`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsMD.hh)<br>➕ incremental SHA digests through the configured crypto backend | N/A; [`std::hash`](https://en.cppreference.com/cpp/utility/hash) is not a cryptographic digest API | [Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html) SHA algorithms<br>➕ also offers other hash families | [`folly::ssl::OpenSSLHash::Digest`](https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLHash.h) | Partial: [`bdlde::Sha256`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_sha2.h)`  /  `[`Sha512`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_sha2.h)`  /  `[`Sha1`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_sha1.h)`  /  `[`Md5`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_md5.h) |
| [`Ztls::HMAC`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsHMAC.hh)<br>➕ backend MAC state with span input/output and reset | N/A | [Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html) [`hmac`](https://github.com/boostorg/hash2/blob/master/include/boost/hash2/hmac.hpp) | [`folly::ssl::OpenSSLHash::Hmac`](https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLHash.h) | N/A |
| [`Ztls::TOTP`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsTOTP.hh)<br>➕ time-based code calculation and bounded-window verification | N/A | Partial: [Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html) HMAC + custom TOTP construction | N/A | N/A |

## Keys, encapsulation and encrypted files

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| [`Ztls::PK`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsPK.hh)<br>➕ RSA/EC/Ed25519/X25519 key loading, generation and applicable signing/verification operations | N/A; [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) SSL's native handle is not a public-key wrapper library | Partial: [`folly::ssl::OpenSSLKeyUtils`](https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLKeyUtils.h)<br>➖ private-key parsing helpers, not the full typed key/signature API | Partial: [`ntci::EncryptionKey`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptionkey.h)`  +  `[`EncryptionDriver`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptiondriver.h) |
| [`Ztls::PK`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsPK.hh) ML-KEM-768 and ML-KEM-768–X25519 keys<br>➕ encapsulation/decapsulation and key serialization integrated with Z buffers | N/A | N/A | N/A |
| [`Ztls::COSE`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsCOSE.hh)<br>➕ COSE key representations bound to native public/private key types | N/A | N/A | N/A |
| [`Ztls::HPKE`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsHPKE.hh)<br>➕ base-mode hybrid ML-KEM-768–X25519 encryption with caller buffers | N/A | N/A | N/A |
| [`ZtlsAge`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsAge.hh)<br>➕ age-format file encryption/decryption and recipient/identity parsing | N/A; file streams alone supply no age encryption | N/A | N/A |

## Credential storage

| Z facility | Boost |
| --- | --- |
| [`Ztls::Vault`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsVault.hh), [`VaultConfig`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsVault.hh), [`VaultStore`](https://github.com/kaldron-labs/z/blob/main/ztls/src/ZtlsVaultStore.hh)<br>➕ scoped credential names with ephemeral/file/keyring/module stores<br>➕ Linux Secret Service and Windows platform integration | N/A; no matching Boost credential vault |

## TLS byte streams and endpoints

TLS counterpart: [Boost.Asio SSL](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/ssl.html).

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| [`Ztls::RxBufAlloc`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh), [`TxBufAlloc`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh) and TLS packet allocation<br>➕ separate plaintext/ciphertext ownership and serial record encryption into packet spans | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) SSL buffers and associated allocators<br>➖ different packet-backing integration | Partial: [`folly::AsyncSSLSocket with IOBuf output`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h)<br>➖ no Z plaintext/ciphertext packet-allocation contract | Partial: [`ntci::Encryption`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryption.h)`  +  `[`DataPool`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h) |
| [`Ztls::HubParams`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh), [`ClientParams`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh), [`ServerParams`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh)<br>➕ trust, keys, ALPN, allocator roles and Rx/Tx destinations in one setup contract | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`ssl::context`](https://github.com/boostorg/asio/blob/master/include/boost/asio/ssl/context.hpp) + executors and application parameters | Partial: [`folly::SSLContext + EventBase`](https://github.com/facebook/folly/blob/main/folly/io/async/SSLContext.h)<br>➖ different allocator and endpoint configuration model | Partial: [`ntca::EncryptionClientOptions`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_encryptionclientoptions.h)`  /  `[`EncryptionServerOptions`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_encryptionserveroptions.h) |
| [`Ztls::CliLink`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh), [`SrvLink`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh)<br>➕ handshake/session callbacks integrated with pooled transport and shard ownership | [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) [`ssl::stream`](https://github.com/boostorg/asio/blob/master/include/boost/asio/ssl/stream.hpp)`<`[`ip::tcp::socket`](https://github.com/boostorg/asio/blob/master/include/boost/asio/ip/tcp.hpp)`>`<br>➕ standard [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) asynchronous-stream composition | [`folly::AsyncSSLSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h) | Partial: [`ntci::StreamSocket::upgrade`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)` / downgrade`<br>➕ TLS upgrade/downgrade of an existing stream socket |
| [`Ztls::Client`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh), [`Server`](https://github.com/kaldron-labs/z/blob/main/ztls/src/Ztls.hh)<br>➕ managed TLS link/listener startup, drain and restart | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) SSL streams, acceptors and application lifecycle | Partial: [`folly::AsyncSSLSocket / AsyncServerSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h)<br>➖ listener acceptance and lifecycle require composition | Partial: [`ntci::EncryptionDriver`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptiondriver.h)`  +  `[`StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)`  /  `[`ListenerSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_listenersocket.h) |

# Zquic

QUIC transport.

## Packet, frame and crypto codecs

[Public headers](https://github.com/kaldron-labs/z/blob/main/zquic/src/) · [Module guide](../zquic/README.md).
QUIC v1 is the mainstream deployment baseline; quic-go still prefers it as the
[most commonly deployed version](https://quic-go.net/docs/quic/transport/).
V2 adds no application capabilities and does not deprecate v1
([RFC 9369](https://www.rfc-editor.org/rfc/rfc9369.html)).

| Z facility | Boost |
| --- | --- |
| [QUIC packet/frame/transport-parameter codecs](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh)<br>➕ native wire parsing and output integrated with Z buffer ownership | N/A; [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) UDP does not implement QUIC wire formats |
| [`Zquic::AddressToken`](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh), [`StatelessReset`](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh), connection-ID/bootstrap facilities<br>➕ Retry/address-validation and connection routing machinery | N/A |
| QUIC crypto ([`ZquicCrypto`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicCrypto.hh)`.hh`)<br>➕ zpicotls handshake state, packet protection and key retirement | N/A; [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) SSL streams are TLS byte streams, not QUIC packet protection |

## Recovery, paths and stream ownership

| Z facility | Boost |
| --- | --- |
| ACK/loss recovery, [`RttEstimator`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicRecovery.hh), [`NewReno`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicRecovery.hh), PTO and retransmission queues<br>➕ packet-number-space recovery with retained frame ownership | N/A; [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) timers provide scheduling primitives only |
| [`Zquic::Path`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicPath.hh) and link migration<br>➕ path validation, anti-amplification, PMTU/ECN and single-active-path migration | N/A |
| [QUIC buffer/sequence-queue policies](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh)<br>➕ separate packet, CRYPTO and stream retention/reassembly contracts | Partial: [Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html) / [ICL](https://github.com/boostorg/icl/blob/master/include/boost/icl/interval_set.hpp) containers<br>➖ custom QUIC ownership/recovery logic required |
| [`Zquic::CliStream`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicStream.hh), [`SrvStream`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicStream.hh)<br>➕ multiplexed streams with independent directional retirement, FIN/reset/stop and flow control | N/A; no complete Boost QUIC stream facility |

## Transport endpoints and diagnostics

| Z facility | Boost |
| --- | --- |
| [`Zquic::HubParams`](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh)<br>➕ unified TLS, ALPN, transport, migration and instrumentation configuration | N/A |
| [`Zquic::CliLink`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicCliLink.hh), [`SrvLink`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicSrvLink.hh)<br>➕ shared connection runtime, stream scheduling and migration callbacks | N/A |
| [`Zquic::Client`](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh), [`Server`](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh)<br>➕ QUIC connection bootstrap and UDP listener lifecycle over [Zi](https://github.com/kaldron-labs/z/blob/main/zquic/src/Zquic.hh) | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) UDP sockets and timers<br>➖ no QUIC transport implementation |
| [`ZquicLogger`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicLog.hh), [`ZquicLogSink`](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicLog.hh), transport diagnostics<br>➕ qlog JSON-SEQ events with bounded queues and controlled drain/restart | Partial: [Boost.Log](https://github.com/boostorg/log/blob/master/include/boost/log/core.hpp) + [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)<br>➖ QUIC/qlog event model and instrumentation are custom work |

# Zhttp

HTTP/1.1, HTTP/2 and HTTP/3.

## URLs, fields and protocol configuration

[Public headers](https://github.com/kaldron-labs/z/blob/main/zhttp/src/) · [Module guide](../zhttp/README.md).
[Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)
supplies HTTP/1 and WebSocket facilities; [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh) also owns HTTP/2 and HTTP/3.

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| [`Zhttp::URLView`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpURL.hh), [`URL`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpURL.hh), [`Target`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpURL.hh)<br>➕ explicit borrowed/owning HTTP URL and request-target representations<br>➖ borrowed parsing normalizes mutable input | [Boost.URL](https://github.com/boostorg/url/blob/master/include/boost/url.hpp) [`url_view`](https://github.com/boostorg/url/blob/master/include/boost/url/url_view.hpp) / [`url`](https://github.com/boostorg/url/blob/master/include/boost/url/url.hpp)<br>➕ general URI component model | Partial: [`folly::Uri`](https://github.com/facebook/folly/blob/main/folly/Uri.h)<br>➖ no matching mutable borrowed HTTP-target view | Partial: [`ntsa::Uri`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h)<br>➖ no borrowed mutable HTTP request-target view |
| [`ZhttpHeaders`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpCore.hh#L58), field catalogs and matchers<br>➕ compile-time name/value dispatch and shared header semantics across wire versions | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) HTTP fields<br>➖ matching typed catalogs require application machinery | N/A | N/A |
| [`Zhttp::Profiles`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh), [`H1TCP`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh), [`H1TLS`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh), [`H2TLS`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh), [`H3QUIC`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh)<br>➕ explicit compile-time selection of supported transport/protocol combinations | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) stream types with [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) SSL<br>➖ no corresponding H2/H3 profiles | N/A | N/A |
| [`Zhttp::HubConfig`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpConfig.hh), [`Config`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpConfig.hh), [`ServerConfig`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpConfig.hh) and transport configs<br>➕ shared connection, body, queue, protocol and allocator policy | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html)/[Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) stream options + application configuration | N/A | N/A |

## Discovery and event framing

| Z facility | Boost |
| --- | --- |
| [`Zhttp::DiscoveryRequest`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpDiscovery.hh), endpoint selection and [`AltSvcCache`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpDiscovery.hh)<br>➕ HTTPS/SVCB and Alt-Svc routing integrated with client protocol selection | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) resolver + custom discovery/cache logic |
| [`Zhttp::SSEDecoder`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpSSE.hh), [`SSEEvent`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpSSE.hh)<br>➕ incremental server-sent event framing and bounded event storage | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) streaming HTTP bodies + custom SSE parser |

## Messages, body streams and response routes

| Z facility | Boost |
| --- | --- |
| [`Zhttp::Parser`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh), request/response Builders and header catalogs<br>➕ one typed application interface for H1/H2/H3 parsing and production | [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) HTTP parsers, serializers and Body models<br>➖ HTTP/1 vocabulary rather than a shared H1/H2/H3 interface |
| [`Zhttp::BodyEmitter`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpCore.hh) and body policies<br>➕ bounded fixed/streaming production, cancellation and H2/H3 trailers<br>➖ application owns body decompression | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) serializers and custom Body implementations |
| [`Zhttp::Channel`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpFields.hh), [`Route`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpServer.hh), response queues<br>➕ owning deferred replies with H1 ordering and logical-stream cancellation | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) stream plus application response-ordering/lifetime state |

## HTTP wire protocols and endpoints

| Z facility | Boost |
| --- | --- |
| HTTP/1.1 framing ([`Zhttp`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh))<br>➕ integrated request/response streaming and pooled transport | [Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) HTTP/1 |
| HTTP/2 and HPACK ([`Zhttp`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh))<br>➕ multiplexed streams, flow control and header compression over TLS | N/A; [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) does not provide HTTP/2 |
| HTTP/3 and QPACK ([`Zhttp`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh))<br>➕ HTTP messages, control streams and compression over [Zquic](https://github.com/kaldron-labs/z/blob/main/zquic/src/ZquicSrvLink.hh)<br>➖ nonzero blocked-stream buffering is outside the current QPACK profile | N/A; [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) does not provide HTTP/3 |
| [`Zhttp::Pool`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpProfiles.hh), [`Client`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpClient.hh)<br>➕ pooled destinations with independent link/request concurrency, retry and protocol fallback | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) client examples + custom pooling/request coordinator |
| [`Zhttp::Server`](https://github.com/kaldron-labs/z/blob/main/zhttp/src/ZhttpServer.hh)<br>➕ typed admission, listeners, sessions and graceful drain across selected HTTP profiles | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) server examples + custom runtime<br>➖ no native H2/H3 endpoints |

# Zws

Websockets.

## URI, handshake and frame codecs

[Public headers](https://github.com/kaldron-labs/z/blob/main/zws/src/) · [Module guide](../zws/README.md).
WebSocket counterpart: [Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket.html).
[Zws](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsH1.hh) supplies the WebSocket dependency of [Zjrpc](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh) and [Zmcp](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh).

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| [`Zws::URI`](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsURI.hh)<br>➕ ws/wss URI interpretation for native profile setup | Partial: [Boost.URL](https://github.com/boostorg/url/blob/master/include/boost/url.hpp) URI parsing + WebSocket-specific checks | Partial: [`folly::Uri`](https://github.com/facebook/folly/blob/main/folly/Uri.h)<br>➖ WebSocket scheme/profile validation is additional work | Partial: [`ntsa::Uri`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h) |
| [Opening handshake, H1 Upgrade and H2/H3 Extended CONNECT](https://github.com/kaldron-labs/z/blob/main/zws/src/Zws.hh)<br>➕ binds the same WebSocket application to concrete HTTP profiles | [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) WebSocket handshake<br>➖ H2/H3 Extended CONNECT requires another HTTP stack | N/A | N/A |
| [`Zws::Codec`](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsCodec.hh), protocol frame/header utilities<br>➕ transport-independent masking, fragmentation and control-frame processing | [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) WebSocket frame/message operations | N/A | N/A |
| [`Zws::TxLayer`](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsTx.hh)<br>➕ reference-buffer output and explicit fragment/message boundaries | [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) WebSocket write/partial-write operations | N/A | N/A |

## WebSocket endpoints

| Z facility | Boost |
| --- | --- |
| [`Zws::Config`](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsProtocol.hh) and link state<br>➕ bounded messages with handshake/close/heartbeat policy and per-link state | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) WebSocket options + application state |
| [`Zws::Client`](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsHub.hh), [`Server`](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsHub.hh) and native links<br>➕ CRTP message callbacks and streaming output over H1/H2/H3 bindings | [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) [`websocket::stream`](https://github.com/boostorg/beast/blob/master/include/boost/beast/websocket/stream.hpp) + application accept/connect lifecycle |

# Zrest

REST APIs.

## Typed request/response contracts

[Public headers](https://github.com/kaldron-labs/z/blob/main/zrest/src/) · [Module guide](../zrest/README.md)

| Z facility | Boost | Folly |
| --- | --- | --- |
| [`Zrest::Request`](https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh), [`Response`](https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh), [`Reply`](https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh) and catalog macros<br>➕ compile-time methods, paths, body policies and allowed typed replies | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) + [Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html)/[JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)<br>➖ typed REST contracts and routing require application code | N/A |
| [`Zrest::Input`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestInput.hh), [`BorrowedData`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestInput.hh), [`RequestContext`](https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh)<br>➕ bounded entity assembly with explicit owned/borrowed decoding policy | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) Body storage + [Boost.JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) conversion | Partial: [`folly::IOBufQueue + dynamic JSON conversion`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h)<br>➖ no REST request-context or reflected ownership/binding policy |
| REST query/body signing hooks and [`REST::Signature`](https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh)<br>➕ filters the signature field and exposes the serialized bytes to the signer<br>➖ algorithm, verification and wire-coverage policy are application-owned | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html)/[JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) serialization + application signing logic | N/A |

## Adapters, completion and server-sent events

| Z facility | Boost | Folly |
| --- | --- | --- |
| [`MReqBuilder`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestClient.hh), [`MResParser`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestClient.hh), [`MReqParser`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh), [`MResBuilder`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh)<br>➕ shared catalog adapters for typed operations over [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh) profiles | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) HTTP parser/serializer + [Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html)/[JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) binding | N/A |
| [`Zrest::Completion`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh)<br>➕ one-shot asynchronous typed completion with route invalidation | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) completion handlers + application token/lifetime state | Partial: [`folly::Promise / Future`](https://github.com/facebook/folly/blob/main/folly/futures/Future.h)<br>➖ no REST route invalidation or allowed-reply catalog |
| [`Zrest::SSEProducer`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestSSE.hh), [`SSEBuilder`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh), SSE reply metadata<br>➕ bounded producer queues and incremental typed SSE delivery<br>➖ no automatic replay or reconnection | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) streaming bodies + custom SSE/lifecycle state | N/A |

## REST endpoints

| Z facility | Boost |
| --- | --- |
| [`Zrest::Client`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestClient.hh), [`Server`](https://github.com/kaldron-labs/z/blob/main/zrest/src/ZrestServer.hh), endpoint configs and limits<br>➕ typed calls/routes, admission limits and full REST/HTTP drain boundary | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) + [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)/[Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) + [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html)<br>➖ no complete matching Boost REST endpoint framework |

# Zgrpc

GRPC.

## Method, metadata and status vocabulary

[Public headers](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/) · [Module guide](../zgrpc/README.md).
[Zpb](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh) supplies native protobuf payloads; [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh) supplies H2/H3 transport.

| Z facility | Boost |
| --- | --- |
| [`Zgrpc::Method`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh), [`Message`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh), [`Reply`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh) and catalog macros<br>➕ typed method paths, RPC cardinality and response contracts | Partial: [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) / [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html)<br>➖ no gRPC method/catalog runtime |
| [`Zgrpc::Metadata`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh), [`Status`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh), timeout and binary-value codecs<br>➕ bounded initial/trailing metadata and gRPC status/deadline interpretation | N/A; generic HTTP fields do not implement these gRPC rules |

## Envelope, protobuf and HTTP adapters

| Z facility | Boost | Folly |
| --- | --- | --- |
| [`Zgrpc::FrameRx`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcFrame.hh) and pooled frame output<br>➕ incremental five-byte envelopes with byte budgets and retained payload backing | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) buffers + custom envelope codec | N/A |
| [`Zgrpc::PBMessage`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh) and typed payload adapters<br>➕ [Zpb](https://github.com/kaldron-labs/z/blob/main/zpb/src/Zpb.hh) verification before typed construction and explicit borrowed-data policy | N/A; no native Boost protobuf/gRPC payload binding | N/A |
| [`Zgrpc::RxInput`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcIO.hh), [`TxFrames`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcIO.hh), [`TxBody`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcIO.hh)<br>➕ bounded receive turns and retained frame queues for HTTP production | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) queues/composed operations + custom stream state | Partial: [`folly::IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h)<br>➖ no protobuf verification, gRPC envelope or bounded RPC turn policy |
| [Request/response HTTP and multi-method adapters](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/Zgrpc.hh)<br>➕ method-specific metadata, streaming bodies and terminal trailers over H2/H3 | N/A; [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) lacks H2/H3 and gRPC adapters | N/A |

## Call ownership and endpoints

| Z facility | Boost |
| --- | --- |
| [`Zgrpc::ClientCall`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcCall.hh), [`ServerCall`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcCall.hh), call tokens and directional state<br>➕ invalidatable Rx/Tx routes, cardinality and one-shot finish/cancel admission | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) cancellation and completion primitives<br>➖ gRPC call semantics remain custom work |
| [`Zgrpc::Client`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcClient.hh), [`Server`](https://github.com/kaldron-labs/z/blob/main/zgrpc/src/ZgrpcServer.hh), endpoint configs and limits<br>➕ unary/client-streaming/server-streaming/duplex RPCs, deadlines and bounded shutdown | N/A; no complete Boost gRPC endpoint library |

# Zjrpc

JSON RPC.

## JSON-RPC envelopes and declarations

[Public headers](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/) · [Module guide](../zjrpc/README.md).
JSON counterparts use [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html);
JSON parsing alone does not supply JSON-RPC dispatch or correlation.

| Z facility | Boost | Folly | BDE |
| --- | --- | --- | --- |
| [`Zjrpc::Request`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh), [`Response`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh), IDs, [`Envelope`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh), [`Parsed`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh) and wire views<br>➕ typed parameters/results and distinct missing/null IDs with retained JSON views<br>➖ numeric IDs use signed 64-bit integers; no fractional/exponent forms | Partial: [Boost.JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) values/conversion + custom [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)-RPC schema | Partial: [`folly::dynamic / parseJson / toJson`](https://github.com/facebook/folly/blob/main/folly/json/json.h)<br>➖ no JSON-RPC envelope declarations or typed RPC dispatch | N/A |
| [`Zjrpc::Input`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcIO.hh), [`Output`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcIO.hh), [`BufOutput`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcIO.hh)<br>➕ bounded pooled JSON assembly and serialization | Partial: [Boost.JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) parser/serializer with custom pooled storage | Partial: [`folly::IOBufQueue + JSON parser/serializer`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h) | Partial: [`baljsn::DatumUtil`](https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_datumutil.h)`  +  `[`bdlbb::Blob`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h) |

## Correlation, completion and batches

| Z facility | Boost |
| --- | --- |
| [`Zjrpc::PendingCalls`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh), [`Caller`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh)<br>➕ outbound ID correlation removed before typed terminal callbacks | Partial: [Boost.Unordered](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered.html) + [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) completion state |
| [`Zjrpc::InboundCalls`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcInbound.hh)<br>➕ separate live requests and bounded recent-ID history<br>➖ history contains IDs, not replayable replies | Partial: [Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) + custom admission/history policy |
| [`Zjrpc::Completion`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcCompletion.hh) and completion slots<br>➕ one-shot claims before reply posting and invalidation before cancellation callbacks | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) completion tokens + custom one-shot ownership |
| [`Zjrpc::Dispatcher`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcDispatch.hh), [`Batch`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh), [`BatchReply`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcPending.hh)<br>➕ shared typed dispatch, notifications and bounded batch aggregation | Partial: [Boost.JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) + custom RPC dispatcher |

## Transport bindings and endpoints

| Z facility | Boost | Folly |
| --- | --- | --- |
| [`Zjrpc::StdioConfig`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcStdio.hh), [`StdioFramer`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcStdio.hh), [`IOEndpoint`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcStdio.hh)<br>➕ line framing and isolated blocking workers over owned per-run handle duplicates | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) descriptors/handles or [Iostreams](https://www.boost.org/doc/libs/latest/libs/iostreams/doc/index.html) + custom line/RPC runtime | Partial: [`folly::AsyncPipeReader / AsyncPipeWriter`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncPipe.h)<br>➖ no newline JSON-RPC framing or complete stdio endpoint lifecycle |
| JSON-RPC HTTP/SSE and WebSocket bindings<br>➕ shared RPC work/correlation over [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh)/[Zws](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsH1.hh) channels | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html) HTTP/WebSocket + [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)<br>➖ RPC/SSE work and lifecycle remain application code | N/A |
| [`Zjrpc::IOClient`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcClient.hh), [`IOServer`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcServer.hh), [`HTTPClient`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcClient.hh), [`HTTPServer`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcServer.hh), [`WSClient`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcClient.hh), [`WSServer`](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/ZjrpcServer.hh)<br>➕ typed calls/notifications/batches and coordinated restart after drain | N/A; no complete matching Boost [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)-RPC endpoint library | N/A |

# Zmcp

Model Context Protocol.

## Typed tools and protocol vocabulary

[Public headers](https://github.com/kaldron-labs/z/blob/main/zmcp/src/) · [Module guide](../zmcp/README.md).
Uses [Zjrpc](https://github.com/kaldron-labs/z/blob/main/zjrpc/src/Zjrpc.hh), [Zws](https://github.com/kaldron-labs/z/blob/main/zws/src/ZwsH1.hh) and [Zhttp](https://github.com/kaldron-labs/z/blob/main/zhttp/src/Zhttp.hh); it has no dependency on [Zrest](https://github.com/kaldron-labs/z/blob/main/zrest/src/Zrest.hh) despite shared metadata conventions.

| Z facility | Boost |
| --- | --- |
| [`Zmcp::Request`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh), [`Response`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh), tool catalogs and generated schemas<br>➕ reflected tools/list/tools/call with typed input/output schemas<br>➖ current scope is tool providers/callers; prompts/resources and other MCP subsystems are deferred | Partial: [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) + [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)<br>➖ MCP schema/catalog policy is additional work |
| [`Zmcp::ToolAnnotations`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh), [`MCP::Header`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh)<br>➕ compile-time tool semantics drive schema/header output and trusted retry policy | N/A |
| [MCP peer negotiation and catalog cache](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh)<br>➕ discovery/legacy handshake and explicit catalog invalidation | N/A |
| [`Zmcp::Context`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/Zmcp.hh) and extended completion policy<br>➕ transport/session/stream context with cancellation, progress and logging<br>➖ context pointers are callback-scoped | Partial: [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) handlers/cancellation + custom MCP policy |

## MCP endpoints

| Z facility | Boost |
| --- | --- |
| [`Zmcp::Batch`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh) and shared JSON-RPC aggregation<br>➕ typed aggregate tool calls reuse the ordinary pending-call table | Partial: [Boost.JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp) + custom MCP/RPC aggregation |
| [`Zmcp::IOClient`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh), [`IOServer`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpServer.hh)<br>➕ newline-delimited MCP over inherited stdio with bounded work and lifecycle<br>➖ peer process creation/supervision is application-owned | Partial: [Boost.Process](https://github.com/boostorg/process/blob/master/include/boost/process.hpp) + custom MCP protocol/runtime |
| [`Zmcp::HTTPClient`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh), [`HTTPServer`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpServer.hh), [`WSClient`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpClient.hh), [`WSServer`](https://github.com/kaldron-labs/z/blob/main/zmcp/src/ZmcpServer.hh)<br>➕ shared typed tool handlers over Streamable HTTP and WebSockets with full drain<br>➖ authentication/authorization remain application-owned | Partial: [Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref.html)/[Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html) + [JSON](https://github.com/boostorg/json/blob/master/include/boost/json.hpp)<br>➖ no complete Boost MCP endpoint library |

# Zdb

Typed, sharded, replicated database.

## Schema, rows and indices

[Public headers](https://github.com/kaldron-labs/z/blob/main/zdb/src/) · [Module guide](../zdb/README.md).
[Zfb](https://github.com/kaldron-labs/z/blob/main/zfb/src/Zfb.hh) supplies wire/schema reflection and [Zv](https://github.com/kaldron-labs/z/blob/main/zv/src/ZvLib.hh) supplies runtime configuration.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| [`ZdbTable`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh)`<T>`, [`ZdbTableDerive`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh#L88) and reflected key/schema metadata<br>➕ typed primary/secondary indices, grouped queries and generated BFBS schema<br>➖ immutable application schema is selected before startup | N/A | Partial: [Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) + [Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html)<br>➖ no persistence/schema-store integration | N/A | N/A |
| [`ZdbRow`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh), [`ZdbRowRef`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh), [`ZdbTblRef`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh)<br>➕ intrusive row/table lifetime and shard-owned mutation callbacks | Partial: [`std::shared_ptr`](https://en.cppreference.com/cpp/memory/shared_ptr)<br>➖ no database row state or shard ownership contract | Partial: [Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html) / [SmartPtr](https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html) | N/A | Partial: [`bsl::shared_ptr`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h)<br>➕ allocator-aware shared ownership |
| [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh) buffer and message formats ([`ZdbBuf`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh)`.hh`, [`ZdbMsg`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbMsg.hh)`.hh`)<br>➕ reflected replication/store records share reference-owned I/O backing | N/A | Partial: [Boost.Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html)<br>➖ different wire layout and ownership model | Partial: [`folly::IOBuf`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➖ backing ownership only; no [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh) wire/schema codec | N/A |

## Database operations and lifecycle

| Z facility | Boost | Folly | BDE |
| --- | --- | --- | --- |
| [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh) find/insert/update/delete and row caches<br>➕ typed asynchronous operations with write-through caching and index maintenance<br>➖ local commit acceptance is not synchronous persistence/replication confirmation | Partial: [Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) cache + custom store/transaction runtime | Partial: [`folly::EvictingCacheMap`](https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h)<br>➖ cache only; no typed database writes, persistence or index coordination | Partial: [`bdlcc::Cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h)` (row-`[`cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h)` building block only)` |
| [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh) select/count and queue barriers<br>➕ index-based backing-store queries ordered with writes | Partial: database client + custom cache/store queue coordination | N/A | N/A |
| [`Zdb`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh), [`ZdbCf`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh), [`ZdbHandler`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh)<br>➕ DB-wide shards, configured workers, activation callbacks and orderly drain | N/A; [Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference.html)/[Statechart](https://www.boost.org/doc/libs/latest/libs/statechart/doc/reference.html) primitives do not form a database runtime | N/A | N/A |

## Replication and recoverable workflows

| Z facility | Boost |
| --- | --- |
| [`ZdbHost`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh) and replication/activation<br>➕ leader election, asynchronous chain replication and failover recovery<br>➖ asynchronous replication is not a synchronous consensus commit guarantee | N/A |
| [`ZdbSaga`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbSaga.hh), [`ZdbSagaDB`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh), [`ZdbSagaPhase`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbSaga.hh#L701), [`ZdbSagaRepeatPhase`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbSaga.hh#L707)<br>➕ journaled workflow steps, recovery and application compensation<br>➖ arbitrary external effects are not automatically journaled or exactly-once | Partial: [Boost.Statechart](https://www.boost.org/doc/libs/latest/libs/statechart/doc/reference.html) / MSM<br>➖ no durable saga journal, database mutation protocol or recovery |
| [`Ztc::DB`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZtcDB.hh), [`DBHost`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZtcDBHost.hh), [`DBTable`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZtcDBTable.hh) telemetry<br>➕ reflected database, host and table statistics | Partial: application records + [Boost.Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html) / [Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html) |

## Pluggable backing stores

[Store contract](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbStore.hh).
[Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html) supplies in-memory indices; [Boost.MySQL](https://www.boost.org/doc/libs/latest/libs/mysql/doc/html/index.html)
is a database client, not an equivalent of [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh)'s database/store framework.

| Z facility | Boost | Folly | BDE |
| --- | --- | --- | --- |
| [`Zdb_::Store`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbStore.hh), [`StoreTbl`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbStore.hh) and store-module loading<br>➕ asynchronous store contract with reflected schemas and query/write callbacks | Partial: [Boost.DLL](https://www.boost.org/doc/libs/latest/doc/html/boost_dll/reference.html) + custom database adapter<br>➖ no matching Boost backing-store interface | N/A | N/A |
| [`ZdbMem::Store`](https://github.com/kaldron-labs/z/blob/main/zdb/src/ZdbMemStore.hh) / [`libZdbMem`](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh)<br>➕ in-memory implementation of the same store contract<br>➖ process loss loses locally stored data | Partial: [Boost.MultiIndex](https://www.boost.org/doc/libs/latest/libs/multi_index/doc/reference/index.html)<br>➖ persistence/replication/store contracts require custom code | Partial: [`folly::F14FastMap / ConcurrentHashMap`](https://github.com/facebook/folly/blob/main/folly/container/F14Map.h)<br>➖ in-memory indices only; no [Zdb](https://github.com/kaldron-labs/z/blob/main/zdb/src/Zdb.hh) store/query contract | Partial: [`bdlc::FlatHashMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashmap.h)`  /  `[`bdlcc::StripedUnorderedMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_stripedunorderedmap.h)` (index building blocks only)` |

# ZdbPQ

PostgreSQL backing store and native types.

## PostgreSQL backing store

[Public API](https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh) · [Module guide](../zdb_pq/README.md)

| Z facility | Boost |
| --- | --- |
| [`ZdbPQ`](https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh) / [`libZdbPQ`](https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh)<br>➕ asynchronous libpq store with reflected prepared SQL and binary I/O<br>➖ requires PostgreSQL/libpq and configured server-side types | N/A; [Boost.MySQL](https://www.boost.org/doc/libs/latest/libs/mysql/doc/html/mysql/ref.html) speaks a different database protocol |
| PostgreSQL [`libz`](https://github.com/kaldron-labs/z/blob/main/zdb_pq/src/ZdbPQ.hh) type extension<br>➕ native database representations for Z decimal, time and bitmap values | N/A |

# ZdbSL

SQLite backing store.

## SQLite backing store and inspection

[Public headers](https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/) · [Module guide](../zdb_sqlite/README.md)

| Z facility | Boost |
| --- | --- |
| [`ZdbSL::Store`](https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/ZdbSL.hh) / [`libZdbSL`](https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/ZdbSL.hh)<br>➕ reflected persistent store with WAL and one isolated worker per connection<br>➖ local filesystem and one writer connection per store | N/A; no matching Boost SQLite backing-store implementation |
| [`ZdbSL`](https://github.com/kaldron-labs/z/blob/main/zdb_sqlite/src/ZdbSL.hh) storage codecs and inspection extension<br>➕ canonical sortable Z scalar encodings and readable SQLite inspection helpers | Partial: [Boost.Endian](https://www.boost.org/doc/libs/latest/libs/endian/doc/html/endian.html) / [Serialization](https://www.boost.org/doc/libs/latest/libs/serialization/doc/index.html)<br>➖ Z logical encodings and SQLite extension require custom work |
