# Zhttp client, server, and protocol library

## Overview

Typed HTTP clients and servers across HTTP/1 over TCP/TLS, HTTP/2 over TLS, and HTTP/3 over QUIC, with discovery, pooling, header compression, streaming responses, and SSE.

## Interface

- [Zhttp.hh](./Zhttp.hh), [ZhttpCore.hh](./ZhttpCore.hh), [ZhttpClient.hh](./ZhttpClient.hh), and [ZhttpServer.hh](./ZhttpServer.hh): Parser/Builder, Writer/Channel, request pools/results, server routes and response binding.
- [ZhttpProfiles.hh](./ZhttpProfiles.hh), [ZhttpConfig.hh](./ZhttpConfig.hh), and [ZhttpTransport.hh](./ZhttpTransport.hh): compile-time profile selection, explicit configuration overlays and native hub factories.
- [ZhttpDiscovery.hh](./ZhttpDiscovery.hh), [ZhttpURL.hh](./ZhttpURL.hh), and [ZhttpSSE.hh](./ZhttpSSE.hh): owning discovery endpoints, borrowed/owning URL forms, Alt-Svc cache, SSE decoder/formatter.
- [Makefile.am](./Makefile.am) builds libZhttp and installs the facade/core/config/profile/transport/discovery/SSE headers. Protocol, native, codec and implementation headers are internal.

## Invariants

- Lifecycle is init→start→stop(done)→final. Control owns initialization/finalization, Rx owns parsing and input, Tx owns builders/producers/output. Stop completion follows required owner drains; dependent multiplexers, allocators, callbacks and parser storage must survive them. [Zhttp.hh](./Zhttp.hh), [ZhttpClientImpl.hh](./ZhttpClientImpl.hh), [ZhttpServerImpl.hh](./ZhttpServerImpl.hh)
- Parser/field/body/channel callback views are borrowed and synchronous. Parsers are reused serially and reset after completion. Writers flush on destruction; reservation views expire on commit/output/flush. Embedded records retain the actual typed outer node rather than an independent allocation. [ZhttpCore.hh](./ZhttpCore.hh), [ZhttpFields.hh](./ZhttpFields.hh)
- Each logical client request has its own intrusive queue node and application key. Replayability is sampled at first binding; attempts preserve logical identity but get new attempt IDs. Retry/redirect callbacks must reproduce the request. One logical deadline survives retries/redirects; completion and later queue retirement are distinct. [ZhttpClient.hh](./ZhttpClient.hh), [ZhttpClientImpl.hh](./ZhttpClientImpl.hh)
- Redirects require replayability, remaining budget, idempotency and same-origin Location. Ordinary retries require transient connect failure before response status; H3 fallback drains old routing before restarting eligible requests. Cancellation/completion use slot/generation identity checks. [ZhttpClientImpl.hh](./ZhttpClientImpl.hh)
- Fixed bodies are produced before headers commit under cumulative retention limits. Streaming producers run sequentially on Tx; retained emitters become inert before producer close. Headers/data may already be visible when later production fails; commit is not universally transactional. [ZhttpWire.hh](./ZhttpWire.hh), [ZhttpServerImpl.hh](./ZhttpServerImpl.hh)
- Server routes retain channel and sequence; H1 responses wait for sequence order. Connection/request/global retained-byte admission are separate from response queue counts. Stop disables admission, drains stream tasks and native hubs, then verifies retained responses and requests are gone. [ZhttpServerImpl.hh](./ZhttpServerImpl.hh)
- H1 parsing has protocol-specific permissive branches: repeated Content-Length overwrites prior values, chunked takes precedence, and an empty chunk-size line completes input. These differ from H2/H3 shared header semantics. Header normalization can mutate input, and decoder failures can leave earlier callback effects. [ZhttpH1.hh](./ZhttpH1.hh), [ZhttpFieldImpl.hh](./ZhttpFieldImpl.hh), [ZhttpH3.hh](./ZhttpH3.hh)
- H2 preserves connection-wide HPACK state even for discarded streams, restores receive credit after WINDOW_UPDATE admission, and completes admitted CONTINUATION chains before retirement. H3 retains critical streams/QPACK bindings and explicitly retires consumed QUIC bytes. Stream removal and producer cancellation drain before native handles disappear. [ZhttpH2Hub.hh](./ZhttpH2Hub.hh), [ZhttpH3.hh](./ZhttpH3.hh), [ZhttpNative.hh](./ZhttpNative.hh)
- HPACK/QPACK tables are owner-serialized; borrowed seed/view storage must remain stable. Authorization, cookie and set-cookie are always never-indexed. Parsing/encoding may partially mutate state before failure. [ZhttpHPack.hh](./ZhttpHPack.hh), [ZhttpHPack.cc](./ZhttpHPack.cc), [ZhttpQPack.hh](./ZhttpQPack.hh), [ZhttpQPack.cc](./ZhttpQPack.cc)
- Discovery bounds aliases, records, hints and endpoints; retained requests and generations protect asynchronous resolver callbacks. Alt-Svc views borrow cache storage and expiry is lazy. Mutable URL parsing can normalize host input before later failure; origin identity uses textual host spelling and effective port. [ZhttpDiscovery.cc](./ZhttpDiscovery.cc), [ZhttpAltSvc.cc](./ZhttpAltSvc.cc), [ZhttpURL.cc](./ZhttpURL.cc)
- SSE limits must be configured; event bodies own buffers while IDs are callback-borrowed. The decoder resets ID/retry per event boundary and drops an unfinished event on close. [ZhttpSSE.hh](./ZhttpSSE.hh)

## Side Effects

- Backends open TCP/TLS/QUIC endpoints, use resolver requests, load transport credentials, allocate/retain buffers and compression tables, schedule timers/owner work, and register telemetry. Hub startup rolls back earlier successful hubs; the failing hub owns its own cleanup. [ZhttpTCP.cc](./ZhttpTCP.cc), [ZhttpTLS.cc](./ZhttpTLS.cc), [ZhttpQUIC.cc](./ZhttpQUIC.cc), [ZhttpTransport.cc](./ZhttpTransport.cc)
- Native TCP keeps physical connection references through posted continuations and copies incompatible buffer backing when required. TLS close checks captured physical identity so it cannot close a replacement connection. [ZhttpNative.hh](./ZhttpNative.hh)
- Body/parser/producer callbacks mutate application state and may run before later message failure. Response completion reports submission/failure, not peer receipt. Application code remains responsible for body decompression. [Zhttp.hh](./Zhttp.hh), [ZhttpServerImpl.hh](./ZhttpServerImpl.hh)

## Verification

From a configured repository root, run `make -j8 && make -C zhttp test`. [The module target](../Makefile.am) runs unit, integration and interoperability targets. Commands were not run for this documentation change.
