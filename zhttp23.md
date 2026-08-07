# Zhttp public-header reorganization

## Objective

Make the public entry points reflect the role being built:

- HTTP clients include `ZhttpClient.hh`.
- HTTP servers include `ZhttpServer.hh`.
- `ZhttpClient.hh` and `ZhttpServer.hh` both include `Zhttp.hh`.
- `Zhttp.hh` is the role-neutral public core facade and does not include either
  role facade or role-specific implementation headers.

This is an intentional source compatibility break. Do not preserve the current
behavior in which including `Zhttp.hh` also makes `Zhttp::Client` and related
client facilities visible.

## Target layering

| Layer | Public header | Contents |
| --- | --- | --- |
| Foundation | `ZhttpCore.hh` | Header-list macros, shared limits, wire-neutral types/body policy, and small parsing primitives required below the public facade |
| Core | `Zhttp.hh` | Common hub/link lifecycle and application `Parser`/`Builder` contracts, protocol aliases, and shared application-message orchestration required by both roles |
| Client | `ZhttpClient.hh` | The extended `Request` builder contract, extended response-parser contract, client policy/event types, request coordination, pools, and client transport adapters |
| Server | `ZhttpServer.hh` | The extended `Response` builder contract, workload contract, request metadata, server configuration/coordinator, and server transport adapters |

The dependency direction must remain one-way:

```text
                 Zhttp.hh
                 /      \
    ZhttpClient.hh      ZhttpServer.hh
```

Neither facade may include the other, directly or transitively. The complete
header graph must be a DAG. Include guards protect repeated inclusion; they
must never be used to make a dependency cycle compile. Every include belongs
in the header's include block before declarations. Tail inclusion is
prohibited.

A lower-level foundation header will be needed because the protocol headers
currently include `Zhttp.hh` to obtain defaults and common declarations while
`Zhttp.hh` includes those same protocol headers. The intended topological
shape is:

```text
ZhttpLib.hh / narrow Zu, Zm, Zt, Zi dependencies
                         |
                  ZhttpCore.hh
                  /     |     \
          fields/body  H1/H2/H3  transport primitives
                  \     |     /
                     Zhttp.hh
                    /        \
         client-only DAG    server-only DAG
                    \        /
          ZhttpClient.hh    ZhttpServer.hh
```

`ZhttpCore.hh` is the minimal common prerequisite, not a second umbrella. It
contains the header typelist macros, HTTP default limits, wire-neutral types,
and small parsing primitives which lower-level headers genuinely share. Body
queues and field semantics remain one layer above it in `ZhttpFields.hh` so
URL/configuration users do not inherit pooled I/O-buffer dependencies.
`Zhttp.hh` remains the public core entry point and the home of the shared
`Builder` and `Parser` contracts.

## Plan

### 1. Make the core headers a DAG

1. Move `ZhttpHeaders`, the default limits, wire-neutral types from
   `ZhttpTypes.hh`, and parsing primitives from `ZhttpUtil.hh` into
   `ZhttpCore.hh`. Move `ZhttpTypes.cc` to `ZhttpCore.cc`. Do not move body
   queues into this lowest layer; merge them into `ZhttpFields.hh` instead.
2. Replace upward includes of `Zhttp.hh` in the protocol headers with direct
   includes of `ZhttpCore.hh` and the narrow headers that provide every
   referenced type. These headers must not depend on their public aggregator.
3. Dissolve `ZhttpMessage.hh` by ownership: move `ClientMessage` to
   `ZhttpClient.hh`, move `ServerSession` to `ZhttpServer.hh`, and merge only
   the shared message traits, transmit orchestration, retained-body support,
   and lightweight mutable-span header patch adapter into `Zhttp.hh`. The
   adapter does not retain header values or validate application patches.
   Remove the tail include
   rather than preserving it during the merge.
4. Put all of `Zhttp.hh`'s own includes in its normal include block before its
   declarations. It may include lower-level core protocol/message headers
   because none of them may include `Zhttp.hh` in return.
5. Retain the common application/lifecycle documentation and the `Parser` and
   `Builder` contract sketches in `Zhttp.hh`. They are the common
   inversion-of-control boundary used by client and server message machinery.
6. Retain the low-level H1/H2/H3 parser and builder aliases in `Zhttp.hh`, plus
   only the headers needed to define those aliases and their shared types.
7. Remove all tail includes. Re-add a truly role-neutral dependency at its
   normal ordered include location only if core declarations require its
   complete definition.
8. Remove `Zhttp_CORE_ONLY`; it exists solely to mask a cycle. Any header that
   still needs the macro identifies an unresolved graph error.
9. Keep every core header independently includable. Replace reliance on the
   old umbrella's incidental include order with direct, narrow includes, and
   add `ZhttpCore.hh` to the installed-header list.
10. Delete `ZhttpConfig.hh` after distributing its declarations by ownership:
    - move HTTP versions, body policies/placeholders, and other wire-neutral
      message values to `ZhttpCore.hh`;
    - move transport profiles, `ConnectedInfo`, hub/TCP/TLS/H2/QUIC
      configuration, H2 policy, and transport completion values to
      `ZhttpTransport.hh`;
    - move client result/event/protocol policy and `ClientConfig` to
      `ZhttpClient.hh`;
    - move endpoint source and discovery limits to `ZhttpDiscovery.hh`;
    - move any purely server result value to `ZhttpServer.hh`.
    This removes the current mixture of client, server, message, and transport
    policy from a nominally shared configuration header.

### 2. Move the client application contracts to `ZhttpClient.hh`

1. Make `ZhttpClient.hh` include `Zhttp.hh` in its top include block, guarded
   by `Zhttp_HH` in the established project style. All other client
   prerequisites remain in that same top include block; none may be included
   after declarations.
2. Move the complete extended `Request` contract sketch and its lifecycle
   documentation from `Zhttp.hh` to `ZhttpClient.hh`. Keep it close to the
   `Client` API that consumes it.
3. Move the extended `ResParser` contract sketch with it. Although the base
   `Parser` contract is shared, binding a parsed response to a submitted
   request is client-specific.
4. Update the `Client`/`TxQ` comments so `Builder` and `Parser` link to the core
   contract in `Zhttp.hh`, while `Request` and `ResParser` are documented as
   local client contracts.
5. Keep `ClientMessage`, the reusable client response-stream/request adapter,
   in `ZhttpClient.hh`; neither its definition nor a forward declaration
   belongs in the shared core.
6. Consolidate client-only hubs, links, request pools, TLS pooling, and the
   client sides of H2/H3 into this facade. Keep one separate
   `ZhttpDiscovery.hh` client-routing facility containing both DNS/HTTPS
   discovery and Alt-Svc parsing/cache declarations; their `.cc`
   implementations may remain separate.

### 3. Move the server application contracts to `ZhttpServer.hh`

1. Keep `ZhttpServer.hh`'s direct include of `Zhttp.hh` in its top include
   block. All server prerequisites must likewise precede declarations.
2. Move the complete extended `Response` contract sketch and its synchronous
   and streaming lifecycle documentation from `Zhttp.hh` to
   `ZhttpServer.hh`.
3. Keep the `Workload` sketch beside `Response`, and update its comments to
   distinguish the shared request `Parser` contract in `Zhttp.hh` from the
   server-local `Response` contract.
4. Keep `ServerSession`, `ProtocolServer`, request-body accounting, and their
   declarations in `ZhttpServer.hh`; none belongs in the shared core.
5. Consolidate server-only listener/admission support, links, and the server
   sides of H2/H3 into this facade.

### 4. Dissolve mixed H2/H3 role headers without creating more headers

`ZhttpH2Hub.hh` and `ZhttpH3Hub.hh` currently contain both client and server
implementations; `ZhttpH2Hub.hh` also directly includes `ZhttpClientHub.hh`.
Simply moving the facade includes would therefore leave a server transitively
burdened by client code.

1. Retain only framing, connection state, events, stream primitives,
   HPACK integration, and other genuinely shared HTTP/2 implementation in
   `ZhttpH2Hub.hh`.
2. Move H2 client logical streams, link/hub specializations, pool state, and
   `ClientHub` behavior into `ZhttpClient.hh`; move H2 server logical streams,
   listener state, links, and `ProtocolServer` specializations into
   `ZhttpServer.hh`.
3. Move genuinely shared H3 connection/link mechanics from `ZhttpH3Hub.hh`
   into `ZhttpH3.hh`. Move its client half into `ZhttpClient.hh`, its server
   half into `ZhttpServer.hh`, then delete `ZhttpH3Hub.hh`.
4. Do not create `ZhttpH2Client.hh`, `ZhttpH2Server.hh`,
   `ZhttpH3Client.hh`, or `ZhttpH3Server.hh`. The role facades are already the
   correct ownership boundaries and every role consumer pays their template
   parse cost regardless.
5. Preserve static dispatch and template definitions; this is an ownership and
   include-boundary change, not a conversion to runtime polymorphism.
6. If a client/server specialization needs a type owned by the other side,
   move the genuinely shared type down into `ZhttpH2Hub.hh`, `ZhttpH3.hh`, or
   `ZhttpTransport.hh` instead of introducing a cross-role dependency.

### 5. Remove process lifecycle policy from the library

Use `zi/example/zimxclient.cc` and `zi/example/zimxserver.cc` as the reference
pattern. Their `main()` functions make the whole application lifecycle visible:

```text
parse application options
initialize and start logging
construct the multiplex/application objects
register the application's ZmTrap callback and call ZmTrap::trap()
start the multiplex
initiate connect/listen/application work
wait on the application-owned completion primitive
stop the multiplex/application
emit application diagnostics
stop logging
return the application result
```

Apply that pattern as follows:

1. Delete `ZhttpRuntime.hh` and `ZhttpRuntime.cc`, remove them from
   `zhttp/src/Makefile.am`, and remove `ZhttpRuntimeTest.cc` and its test build
   entry.
2. Remove `Runtime`, `DiagnosticFn`, and the `Runtime` data members from
   `Client` and `Server`. Remove their `wait()` and `diagnostic()` methods and
   all implicit signal-handler setup/teardown from `init()`/`final()`.
3. Remove the internal `m_runtime.stop()` wakeups. HTTP shutdown remains an
   explicit `Client::stop()` or `Server::stop()` operation; the library does
   not decide why an application should stop or which thread waits for it.
4. In `zhttp.cc`, follow `zimxclient.cc`: register an application callback with
   `ZmTrap::sigintFn(...)`, call `ZmTrap::trap()` explicitly in `main()`, start
   the multiplex/client, submit the workload, wait on an application-owned
   `ZmSemaphore`, then stop/finalize the client and multiplex and stop logging.
   The request completion path posts the semaphore when this application's
   completion policy is satisfied.
5. Keep `zhttp`'s timeout and periodic memory/QUIC diagnostics in a concise
   main-thread timed-wait loop. Those policies are specific to this command
   and must not move into libZhttp.
6. In `zhttpd.cc`, follow `zimxserver.cc`: configure `ZmTrap` visibly in
   `main()`, start the multiplex/server, wait on server-application state, then
   stop/finalize the server and multiplex, finalize the access log, stop the
   logger, and return the application result. Listener failure and the trap
   callback post the application-owned semaphore.
7. Delete the forwarding-only `zhttpdmain.cc`. Restructure
   `zhttp/util/Makefile.am` so `zhttpd.cc` is the program entry point and only
   genuinely reusable server/workload support remains in `libzhttputil`.
   Update `zhttpboundary.md` and its fixture accordingly.
8. `zhttpqirimpl.cc` must own its own signal/wait/start/stop policy instead of
   calling a reusable `Zhttpd::run()`. Share HTTP workload/configuration code
   where useful, but do not share `main` policy merely to remove a few lines.
9. Keep the completion primitive and trap callback in concise application
   state, analogous to the examples' `Global::post()`/`Global::wait()`.
   `zimxclient.cc` and `zimxserver.cc` share that mechanical code through the
   adjacent, local `global.hh`; they do not put it in an installed Zi header.
10. If the concrete `zhttp` and `zhttpd` implementations have enough identical
   mechanics, factor those mechanics into an adjacent lowercase
   `zhttp/util/runtime.hh`. It may own such things as the application semaphore
   and the small callback bridge required by `ZmTrap`. It must be listed only
   in `noinst_HEADERS`, must not be included by anything under `zhttp/src`, and
   must not become part of `libZhttp` or the installed-header DAG.
11. A local `runtime.hh` must not conceal application policy. Trap
   registration, timeout and diagnostic choices, component start order, the
   decision to wait, component stop/final order, logging teardown, and exit
   status remain visibly sequenced in each `main()`. Avoid forcing unlike
   client and server policies into callbacks merely to enlarge the shared
   helper.

Repetition of those policy decisions does not violate DRY: the client, server,
and QIR applications have different completion, timeout, diagnostic, logging,
threading, and teardown policies. Only identical local mechanics need to be
factored.

### 6. Migrate direct consumers

1. Change `zhttp/util/zhttp.cc` to include `ZhttpClient.hh`; it must not include
   `Zhttp.hh` separately because the client facade supplies core declarations.
2. Change `zhttp/util/zhttpd.cc` and `zhttp/util/zhttpd.hh` to include
   `ZhttpServer.hh`; remove their redundant direct `Zhttp.hh` includes.
3. Audit all repository consumers currently including `Zhttp.hh`:
   - pure parser/framing tests and low-level protocol implementation files
     remain on `Zhttp.hh`;
   - client programs/tests use `ZhttpClient.hh`;
   - server programs/tests use `ZhttpServer.hh`;
   - combined integration fixtures explicitly include both facades;
   - dependent modules such as `zrest` and `zws` include the facade(s) whose
     public types they actually expose or instantiate.
4. Remove redundant pairs such as `Zhttp.hh` plus `ZhttpServer.hh` after the
   facade becomes self-contained.
5. Do not add compatibility forwarders or make `Zhttp.hh` re-export a facade.

### 7. Prove the boundaries and build

Add or use minimal compile-only translation units for these assertions:

1. `#include <zlib/Zhttp.hh>` compiles and provides the core macros, common
   types, parser aliases, and builder aliases, but not `Zhttp::Client` or
   `Zhttp::Server`.
2. `#include <zlib/ZhttpClient.hh>` compiles standalone and can instantiate a
   representative request/client without pre-including `Zhttp.hh`.
3. `#include <zlib/ZhttpServer.hh>` compiles standalone and can instantiate a
   representative workload/server without pre-including `Zhttp.hh`.
4. Compiler include tracing or preprocessor dependency output confirms that a
   client-only translation unit does not include `ZhttpServer.hh`, and the
   reciprocal server check excludes `ZhttpClient.hh` and
   `ZhttpDiscovery.hh`.
5. Generate the installed-header include relation (for example from compiler
   dependency output) and run a topological-sort/cycle check. The check must
   fail on any strongly connected component larger than one.
6. Audit source text for includes after the first declaration/namespace and
   for `Zhttp_CORE_ONLY`; both searches must be empty across `zhttp/src`.

Then verify in dependency order:

1. Build `zhttp/src`, then `zhttp/test`, `zhttp/util`, `zhttp/itest`, and
   `zhttp/interop` with the configured build's normal parallel make command.
2. Run the zhttp unit tests and the relevant client/server lifecycle,
   multi-request, fallback, and interoperability tests.
3. Run a top-level `make -j8` so dependent modules such as `zrest` and `zws`
   are rebuilt against the new public include contract.
4. Run the full configured `make test` suite and record toolchain/platform
   details.

## Completion criteria

- Client applications need only `ZhttpClient.hh`; server applications need
  only `ZhttpServer.hh`.
- Both facades directly include `Zhttp.hh` and compile standalone.
- `Zhttp.hh` contains the common `Builder` and `Parser` contracts and exports
  no client/server facade, role adapter, or role-specific forward declaration
  as an incidental side effect.
- The `Request` contract is documented in `ZhttpClient.hh`; the `Response`
  contract is documented in `ZhttpServer.hh`.
- `ClientMessage` is owned by `ZhttpClient.hh`; `ServerSession`,
  `ProtocolServer`, and server request-body accounting are owned by
  `ZhttpServer.hh`.
- Client-only and server-only preprocessing graphs exclude the opposite role's
  facade and implementation, and the server graph also excludes the combined
  discovery/Alt-Svc client-routing facility.
- `ZhttpRuntime.hh`, `ZhttpRuntime.cc`, and their test are deleted; no signal,
  blocking-wait, diagnostic scheduling, logging, or process lifecycle policy
  remains in `Client`, `Server`, or another replacement HTTP-library helper.
- `zhttp` and `zhttpd` each show their complete signal-to-shutdown lifecycle
  concisely in their own `main()`. Any shared `zhttp/util/runtime.hh` is a
  lowercase, non-installed application utility containing mechanics rather
  than lifecycle policy.
- `Zhttp_CORE_ONLY` is gone, all installed headers are listed in the build
  metadata, and repository consumers build and test with explicit role
  includes.
- Exactly 16 zhttp library headers remain installed; deleted mechanical splits
  have no compatibility forwarding headers.
- The installed zhttp header graph topologically sorts as a DAG, every include
  is in the normal pre-declaration include block, and no tail inclusion is
  used.

## Header-by-header consolidation audit

Keep a header only when it is an independently usable facility with compiled
implementation, a major protocol/layer boundary, or a necessary low-level DAG
node used by multiple owners. File length alone is not a reason to split a
template implementation: a single consumer and inseparable ownership are
stronger signals to consolidate.

| Header | Disposition | Reason and destination |
| --- | --- | --- |
| `ZhttpLib.hh` | Keep | Standard module ABI/export root. |
| `ZhttpCore.hh` | Create | Minimal common root; absorbs header macros/defaults plus the genuinely wire-neutral parts of `ZhttpTypes.hh` and `ZhttpUtil.hh`; server request-body accounting does not belong here. |
| `Zhttp.hh` | Keep and absorb shared message mechanics | Public role-neutral facade containing the shared `Parser`/`Builder` contracts, aliases, message traits, transmit orchestration, retained-body support, and the minimal mutable-span header patch adapter from `ZhttpMessage.hh`; the application owns correct placeholder replacement, and no role adapters, role declarations, or tail includes remain. |
| `ZhttpConfig.hh` | Delete after distribution | It mixes client policy/results, discovery limits, message/body values, and transport configuration. Move each group to `ZhttpClient.hh`, `ZhttpDiscovery.hh`, `ZhttpCore.hh`, `ZhttpTransport.hh`, or `ZhttpServer.hh` according to ownership. |
| `ZhttpTypes.hh` | Delete after merge | Only 68 lines of foundational wire types; merge into `ZhttpCore.hh` and rename/merge `ZhttpTypes.cc` as `ZhttpCore.cc`. |
| `ZhttpUtil.hh` | Delete after merge | Small header parsing primitives used throughout core parsing; they belong in `ZhttpCore.hh`, not a separately installed utility grab-bag. |
| `ZhttpBody.hh` | Delete after merge | `BodyRx` and body queue helpers are shared message-parsing infrastructure; merge into `ZhttpFields.hh` so the lowest core and URL/configuration users do not inherit pooled I/O-buffer dependencies. |
| `ZhttpURL.hh` | Keep | Independently useful URL/request-target model with a substantial compiled parser and dedicated tests. |
| `ZhttpFields.hh` | Keep and absorb body queues | Shared H1/H2/H3 field/body receive semantics and dispatch layer; multiple protocol owners justify the boundary. |
| `ZhttpAltSvc.hh` | Delete after merge | Alt-Svc and DNS/HTTPS discovery are client-only routes from an origin to candidate endpoints and are always needed together by the concrete client; move the declarations into `ZhttpDiscovery.hh`, while `ZhttpAltSvc.cc` may remain a separate compiled implementation. |
| `ZhttpDiscovery.hh` | Keep and absorb Alt-Svc | Unified client-routing facility: DNS/HTTPS resolution, resolver seam, Alt-Svc parsing/cache, endpoint types, and discovery limits. Include only from the client facade. |
| `ZhttpCompression.hh` | Keep and absorb static tables | Shared HPACK/QPACK integer, string, and Huffman primitives are a real cross-protocol boundary. Merge `ZhttpStaticTable.hh` here. |
| `ZhttpStaticTable.hh` | Delete after merge | Only 78 lines and exactly two consumers, both of which already depend on `ZhttpCompression.hh`. |
| `ZhttpHPack.hh` | Keep | Large, independently tested HTTP/2 compression engine with compiled implementation. Folding it into H2 would conflate framing and compression and worsen rebuild scope. |
| `ZhttpQPack.hh` | Keep | Large, independently tested HTTP/3 compression engine with compiled implementation and dynamic-table state. |
| `ZhttpH1.hh` | Keep | Complete HTTP/1 parser/builder protocol boundary. |
| `ZhttpH1Stream.hh` | Delete after merge | Single production consumer and only 99 lines; merge H1 stream binding into `ZhttpTransport.hh`. |
| `ZhttpH2.hh` | Keep and absorb messages | Make this the complete H2 framing/message protocol header by merging `ZhttpH2Message.hh` and its tiny `.cc` into `ZhttpH2.hh`/`ZhttpH2.cc`. |
| `ZhttpH2Message.hh` | Delete after merge | One production include and inseparable from H2 protocol types. |
| `ZhttpH2Stream.hh` | Delete after merge | Queue admission and stream hash are used by the H2 hub; merge into `ZhttpH2Hub.hh`. |
| `ZhttpTLSHub.hh` | Delete after merge | Only 88 lines of H2 ALPN/config adaptation. Merge into `ZhttpH2Hub.hh` and move its compiled `version()` implementation to `ZhttpH2.cc`. |
| `ZhttpH2Hub.hh` | Keep and reduce | Retain the substantial shared H2 wire/hub machinery; move all client sections to `ZhttpClient.hh` and server sections to `ZhttpServer.hh`. |
| `ZhttpH3.hh` | Keep and absorb connection/common hub code | Make this the complete shared H3 protocol/connection layer by merging `ZhttpH3Cxn.hh` plus the genuinely common parts of `ZhttpH3Hub.hh`. |
| `ZhttpH3Cxn.hh` | Delete after merge | One protocol-specific connection helper with no independent owner beyond H3. |
| `ZhttpH3Hub.hh` | Delete after distribution | Move common mechanics to `ZhttpH3.hh`, client mechanics to `ZhttpClient.hh`, and server mechanics to `ZhttpServer.hh`. |
| `ZhttpTransport.hh` | Keep and broaden coherently | Make this the shared transport/link/stream layer; absorb `ZhttpStream.hh`, `ZhttpH1Stream.hh`, `ZhttpLink.hh`, and `ZhttpHubs.hh`. |
| `ZhttpStream.hh` | Delete after merge | Generic stream dispatch is used as transport/link machinery, not an independent public facility. |
| `ZhttpLink.hh` | Delete after merge | Client/server link templates are shared transport integration and have no direct production consumer other than the current client hub path. |
| `ZhttpHubs.hh` | Delete after merge | The lifecycle coordinator is shared transport orchestration; merge into `ZhttpTransport.hh` rather than retaining a 193-line installed header. |
| `ZhttpMessage.hh` | Delete after distribution | Move `ClientMessage` to `ZhttpClient.hh` and `ServerSession` to `ZhttpServer.hh`; merge only the machinery used by both roles into `Zhttp.hh`. |
| `ZhttpClientHub.hh` | Delete after merge | A 53-line client-only base; merge into `ZhttpClient.hh`. |
| `ZhttpClientPool.hh` | Delete after merge | Client-only request/link ownership used solely by `ZhttpClient.hh`; merge into the client facade. |
| `ZhttpTLSClientPool.hh` | Delete after merge | Client-only TLS/H1/H2 pool specialization; merge into the client facade with the H2 client hub code. |
| `ZhttpClient.hh` | Keep and absorb client implementation | Sole client facade and owner of `ClientMessage`, client hubs, pools, transport specializations, `Request`, and `ResParser`. Its consumers already incur these templates, so mechanical subheaders do not improve isolation. |
| `ZhttpServer.hh` | Keep and absorb server implementation | Sole server facade and owner of `ServerSession`, `ProtocolServer`, server request-body accounting, server hubs, links, H2/H3 specializations, `Response`, and workload contract. |
| `ZhttpRuntime.hh` | Delete outright | Process policy belongs in application `main()`; no replacement installed header. Delete its `.cc` and test as described above. |
| `ZhttpH2Client.hh` | Do not create | Previously proposed split; fold the H2 client implementation into `ZhttpClient.hh`. |
| `ZhttpH2Server.hh` | Do not create | Previously proposed split; fold the H2 server implementation into `ZhttpServer.hh`. |
| `ZhttpH3Client.hh` | Do not create | Previously proposed split; fold the H3 client implementation into `ZhttpClient.hh`. |
| `ZhttpH3Server.hh` | Do not create | Previously proposed split; fold the H3 server implementation into `ZhttpServer.hh`. |

Apply these direct include replacements throughout sources, tests, utilities,
interop programs, and dependent modules; do not leave forwarding headers:

| Removed include | Replacement owner |
| --- | --- |
| `ZhttpTypes.hh`, `ZhttpUtil.hh` | `ZhttpCore.hh` |
| `ZhttpConfig.hh` | `ZhttpCore.hh`, `ZhttpTransport.hh`, `ZhttpDiscovery.hh`, `ZhttpClient.hh`, or `ZhttpServer.hh` according to the declaration used |
| `ZhttpAltSvc.hh` | `ZhttpDiscovery.hh` |
| `ZhttpBody.hh` | `ZhttpFields.hh` |
| `ZhttpStaticTable.hh` | `ZhttpCompression.hh` |
| `ZhttpH1Stream.hh`, `ZhttpStream.hh`, `ZhttpLink.hh`, `ZhttpHubs.hh` | `ZhttpTransport.hh` |
| `ZhttpH2Message.hh` | `ZhttpH2.hh` |
| `ZhttpH2Stream.hh`, `ZhttpTLSHub.hh` | `ZhttpH2Hub.hh` |
| `ZhttpH3Cxn.hh` | `ZhttpH3.hh` |
| `ZhttpMessage.hh` | `Zhttp.hh` |
| `ZhttpClientHub.hh`, `ZhttpClientPool.hh`, `ZhttpTLSClientPool.hh` | `ZhttpClient.hh` |
| `ZhttpH3Hub.hh` | `ZhttpH3.hh`, `ZhttpClient.hh`, or `ZhttpServer.hh` according to ownership |
| `ZhttpRuntime.hh` | No library replacement; optional local `zhttp/util/runtime.hh` only |

Update compiled-source boundaries at the same time: replace `ZhttpTypes.cc`
with `ZhttpCore.cc`; distribute `ZhttpConfig.cc` enum definitions to
`ZhttpCore.cc`, a new `ZhttpTransport.cc`, `ZhttpDiscovery.cc`,
`ZhttpClient.cc`, and `ZhttpServer.cc` with their declarations; merge
`ZhttpH2Message.cc` and `ZhttpTLSHub.cc` into `ZhttpH2.cc`; and delete
`ZhttpRuntime.cc`. Update `zhttp/src/Makefile.am` in one change so no deleted
header or source remains installed or built.

This reduces the installed module from 35 current headers to 16 target headers,
including the new `ZhttpCore.hh`. It deletes 20 current headers, creates no
role-fragment headers, and preserves the few large files which correspond to
real protocol or ownership boundaries.

## Consolidated proposed header DAG

The following adjacency table is normative for direct dependencies between
the 16 surviving installed zhttp headers. Each header appears exactly once on
the left; the right side contains every zhttp header it directly includes.
C/C++ system headers and headers owned by lower Z modules (`Zu`, `Zm`, `Zt`,
`Zf`, `Zi`, `Ztcp`, `Ztls`, and `Zquic`) remain explicit top-of-file
prerequisites but are outside this module-local graph. An empty right side
denotes a root node. Every non-root header directly includes `ZhttpLib.hh` as
the component-library root, even when another dependency also includes it.
Every other edge names the owner of a declaration used directly by the source
header; no edge exists merely to obtain incidental transitive declarations.

```text
ZhttpClient.hh | ZhttpLib.hh Zhttp.hh ZhttpCore.hh ZhttpURL.hh ZhttpDiscovery.hh ZhttpH2Hub.hh ZhttpH3.hh ZhttpTransport.hh
ZhttpServer.hh | ZhttpLib.hh Zhttp.hh ZhttpCore.hh ZhttpURL.hh ZhttpH2Hub.hh ZhttpH3.hh ZhttpTransport.hh
Zhttp.hh | ZhttpLib.hh ZhttpCore.hh ZhttpURL.hh ZhttpH1.hh ZhttpH2.hh ZhttpH3.hh ZhttpTransport.hh
ZhttpH2Hub.hh | ZhttpLib.hh ZhttpCore.hh ZhttpTransport.hh ZhttpH2.hh ZhttpHPack.hh
ZhttpH3.hh | ZhttpLib.hh ZhttpCore.hh ZhttpFields.hh ZhttpQPack.hh ZhttpTransport.hh
ZhttpH2.hh | ZhttpLib.hh ZhttpCore.hh ZhttpFields.hh
ZhttpH1.hh | ZhttpLib.hh ZhttpCore.hh ZhttpFields.hh
ZhttpTransport.hh | ZhttpLib.hh ZhttpCore.hh
ZhttpDiscovery.hh | ZhttpLib.hh ZhttpCore.hh ZhttpURL.hh
ZhttpFields.hh | ZhttpLib.hh ZhttpCore.hh ZhttpURL.hh
ZhttpURL.hh | ZhttpLib.hh ZhttpCore.hh
ZhttpHPack.hh | ZhttpLib.hh ZhttpCompression.hh
ZhttpQPack.hh | ZhttpLib.hh ZhttpCompression.hh
ZhttpCompression.hh | ZhttpLib.hh
ZhttpCore.hh | ZhttpLib.hh
ZhttpLib.hh |
```
