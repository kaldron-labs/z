# Z repository blueprint

## System Overview

Z is a C++ framework organized into foundation libraries, scheduling and I/O, protocol transports and codecs, storage, application services, and tools. Module manifests define enabled build traversal; each linked contract describes the inspected implementation and its ownership, lifecycle, and side effects.

## Interface

The directory contracts below are the architectural index. Source contracts identify installed libraries/headers, application-provided interfaces, and executable entry points. Examples and utilities have their own contracts where they contain production source.

## Subdirectories & System Boundaries

| Directory | Boundary |
| --- | --- |
| [mingw](./mingw/EGM.md) | Build MSYS2 MINGW64 DLLs and import libraries for BFD and SFrame using installed static archives and explicit export definitions. |
| [scripts](./scripts/EGM.md) | Build and run QUIC interoperability images, collect and report results, convert TOML, and maintain development inputs. |
| [zdash](./zdash/EGM.md) | GTK telemetry dashboard with OAuth authentication, WebSocket subscriptions, and an installed module-session ABI. |
| [zdb](./zdb/EGM.md) | Sharded database tables and caches, pluggable storage, host replication/recovery, persistent sagas, and database telemetry. |
| [zdb_pq](./zdb_pq/EGM.md) | PostgreSQL backing store for Zdb, native PostgreSQL types, and continuation-query benchmark. |
| [zdb_sqlite](./zdb_sqlite/EGM.md) | SQLite implementation of the Zdb storage interface, scalar inspection extension, and storage benchmark. |
| [zdbus](./zdbus/EGM.md) | Linux D-Bus integration with typed message adaptation and explicit scheduler ownership. |
| [zdf](./zdf/EGM.md) | Compressed typed dataframes, time series, historical/live readers, and statistics over Zdb storage. |
| [ze](./ze/EGM.md) | Native error wrappers and structured diagnostic event construction and formatting. |
| [zf](./zf/EGM.md) | Reflection-backed field metadata and codecs for JSON, CBOR, D-Bus, ASN.1, CLI, URI, CSV, Cf, TOML and YAML, with shared owned configuration trees. |
| [zfb](./zfb/EGM.md) | FlatBuffers wire support integrated with native framework metadata, explicit verification and borrowed access, canonical reflection/schema export, and native C++ schema generation. |
| [zfix](./zfix/EGM.md) | Schema-driven FIX encoding/dispatch and managed or externally managed TCP/TLS transport, with typed examples, load tools and benchmarks. |
| [zgrpc](./zgrpc/EGM.md) | Typed gRPC over the framework HTTP/protobuf stack, with bounded framing and metadata, invalidatable call handles, and asynchronous client/server teardown. |
| [zgtk](./zgtk/EGM.md) | GTK scheduling, value/callback adapters, and tree models. |
| [zhttp](./zhttp/EGM.md) | Typed HTTP/1, HTTP/2 and HTTP/3 clients/servers over TCP, TLS and QUIC, with discovery, pooling, streaming and command-line utilities. |
| [zi](./zi/EGM.md) | Platform files and processes, scheduled network I/O and buffers, stream framing, shared-memory rings, logging, DNS, and I/O telemetry. |
| [zjrpc](./zjrpc/EGM.md) | Typed JSON-RPC 2.0 codecs, bounded dispatch/correlation and stdio/HTTP/WebSocket endpoints, with arithmetic examples. |
| [zm](./zm/EGM.md) | Runtime foundation for allocation, ownership, containers, synchronization, threads, scheduling, diagnostics and telemetry. |
| [zmcp](./zmcp/EGM.md) | Typed MCP tool schemas and protocol lifecycle over JSON-RPC HTTP, WebSocket and stdio transports, with arithmetic client/server examples. |
| [zpb](./zpb/EGM.md) | Protobuf wire support integrated with native framework metadata, explicit verification and materialization, descriptor reflection, and native schema compilation. |
| [zquic](./zquic/EGM.md) | QUIC v1 transport over UDP with TLS, streams, loss recovery, flow/congestion control, migration and optional qlog. |
| [zrest](./zrest/EGM.md) | Typed REST catalogs and client/server lifecycle over Zhttp, with bounded body/SSE handling and native OAuth client/server examples. |
| [zrl](./zrl/EGM.md) | Terminal input/output, readline and CLI lifecycle, keymap-driven editing, filesystem completion, and in-memory history. |
| [zt](./zt/EGM.md) | Dynamic arrays and strings, bitmaps and windows, regex and text adapters, and timezone/platform utilities. |
| [ztc](./ztc/EGM.md) | Telemetry and control services connecting application publishers over local shared rings to authenticated WebSocket subscribers through an agent and hub. |
| [ztcp](./ztcp/EGM.md) | TCP transport and ping/pong examples built on the framework multiplexer. The library provides CRTP client/server links, allocator-backed streams, telemetry, and asynchronous connection teardown. |
| [ztls](./ztls/EGM.md) | TLS transport over the framework multiplexer, with cryptographic primitives, age encryption, and secret storage. OpenSSL and picotls back the library. |
| [zu](./zu/EGM.md) | Foundation library for low-level types, compile-time reflection, containers, numeric/text codecs, time values and test infrastructure. |
| [zum](./zum/EGM.md) | Zum groups the service-side authentication library, persistent authorization daemon, management CLI, and a native-client/resource-server example. It builds on Zhttp/Zrest, Ztls, Zdb, Vault and the framework scheduler; daemon telemetry uses Ztc. The existing [README.md](./zum/README.md) describes deployment and provisioning. |
| [zv](./zv/EGM.md) | Configuration file I/O and runtime parameter adaptation. |
| [zws](./zws/EGM.md) | WebSocket framing and HTTP transport adaptation, with command-line client/server programs and a loopback benchmark. |
| [zxdp](./zxdp/EGM.md) | Linux AF_XDP raw packet service and packet validation utilities. Application protocol adaptation and scheduling remain outside the library; its boundary covers arenas, packet allocation/return, kernel rings, receive/wake, and retirement. |

## Global Invariants

- Borrowed wire views require surviving backing storage; verification and materialization are distinct operations in the [protobuf](./zpb/src/EGM.md) and [FlatBuffers](./zfb/src/EGM.md) codecs.
- Thread ownership and shutdown ordering are explicit component contracts. Drain pending work before finalization as required by the [D-Bus](./zdbus/src/EGM.md), [gRPC](./zgrpc/src/EGM.md), and [REST](./zrest/src/EGM.md) endpoints; retained buffers and application objects must survive their callbacks.
- Storage completion semantics are backend-specific. Follow the documented transaction and recovery behavior of [SQLite](./zdb_sqlite/src/EGM.md) and [PostgreSQL](./zdb_pq/src/EGM.md), rather than inferring durability from a generic callback.
- Build dependencies are not automatically refreshed across sibling directories. Follow [build guidance](./docs/BUILD.md) when changing a lower layer and rebuilding dependents.

## Global Side Effects

Depending on the selected components, operations allocate storage, schedule work, invoke application callbacks, access files, open network endpoints, or mutate databases. The module contracts specify their concrete effects and teardown requirements.

Repository [scripts](./scripts/EGM.md) can build/clean, operate containers, write artifacts, and publish images. The [MinGW helper](./mingw/EGM.md) can install packages and libraries. Their execution effects are separate from this documentation bootstrap.

## System Verification

From a configured repository build root, run:

```sh
make -j8
make test
```

See [build guidance](./docs/BUILD.md) and [test setup](./docs/TESTING.md) for configuration and environment requirements. Module contracts provide narrower commands checked against their manifests. Builds, tests, network examples, and database workloads were not executed during this documentation-only bootstrap.

## Contract Scope

These EGM.md files document eligible production source directories and their parent boundaries. Test-only trees, generated/build outputs, hidden directories, and saved review/repair evidence are excluded. Zutcp is explicitly excluded at the user’s request while concurrent work is in progress. Existing README.md files are preserved. The contracts describe inspected source behavior; they do not replace the repository guidance or test suites.
