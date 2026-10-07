# JSON-RPC wire layer

`Zjrpc.hh` provides compile-time declarations, owned IDs, envelope codecs and
borrowed JSON payload views. Keep the parsed tree alive while using those views.
A request sets `Borrowed = 1` when deferred handlers or replies retain spans
or raw JSON into its decoded input. Such work keeps the original buffer/tree;
asynchronous reply output pins that work until serialization finishes. Owned
parameter/result types use the default `Borrowed = 0` and release parsed input
after dispatch. Application-owned external views must still outlive their use.
Numeric IDs use signed 64-bit decimal integers; fractional and exponent forms
are unsupported. Missing IDs and explicit JSON null are distinct.

Payload dispatch follows `Zrest`: a wire discriminator selects a compile-time
specialization. Inbound method matching selects the declared request and its
typed parameter decoder; outbound ID correlation selects the pending call's
typed result decoder. Ordinary output uses templated request/result views.
Envelope `ZfJSON::Union<>` fields hold only optional borrowed JSON nodes, with
no application-type alternatives; they do not infer types from JSON shape.
Arbitrary error data and explicit raw forwarding retain generic JSON traversal.
`ReplyData<Req>` alternatives come from the compile-time response declarations.

`ZjrpcPending.hh` owns outbound correlation. Entries are removed before
completion callbacks. Applications can supply a compile-time result decoder.
`ZjrpcCompletion.hh` supplies typed completion tokens and one direct slot per
ordinary work item. MCP extends the shared token with progress/logging policy.
Completion and MCP progress/logging calls from other threads retain only the
token and action payload while posting to the captured scheduler/thread. Work
back-pointers are read on that owner thread, after checking invalidation. Keep
the scheduler alive until application token calls have returned; after peer
closure, retained tokens reject further calls without accessing their work.
`ZjrpcInbound.hh` owns a peer's live lookup and recent-ID LRU. Live entries
never evict. History stores only IDs, with no reply replay. The server chooses
history capacity for all its peers; `ZmHashParams` rounds it up to a power of
two, minimum eight (default 1024).

`ZjrpcDispatch.hh` shares typed inbound dispatch and work lifetime across
bindings. Inbound batches admit member work under the existing budget,
dispatch bounded turns, and collect one response array in completion order.
Notifications contribute no response; an empty batch produces an error.
`Caller` in `ZjrpcPending.hh` shares outbound calls and notifications.
`Batch<Catalog>` builds a batch from typed requests with caller-chosen IDs and
notifications. Pass it to `callBatch(batch, aggregate)`; the aggregate's
`process(BatchReply)` runs once after the complete response array is decoded
and its pending members are removed. `BatchReply` owns that response's original
buffer/tree and can be retained. Member errors remain in the response array;
transport failure, missing replies or ID collisions fail the aggregate once.
`notifyBatch(batch)` sends an all-notification batch without pending calls.
The same pending hash owns ordinary calls and batch members; there is no batch
wire ID or second correlation index. HTTP keeps one actual member ID solely
for route failure/header association. Batch HTTP headers come from application
metadata; per-method header values belong to ordinary requests. The IO
client and server support calls in both directions with separate inbound and
outbound ID spaces.

`ZjrpcStdio.hh` provides pooled buffers, line framing and blocking I/O workers.
Its internal `IOLink` is stored in place in each IO endpoint and destroyed only
after its workers have drained.
`ZjrpcIO.hh` supplies bounded message assembly and output into pooled I/O buffers.
`ZjrpcHTTP.hh` provides POST request and fixed
response writing, response parsing and SSE queues/framing. MCP extends these
with its routing, session headers and control response policy.

`WSClient<Impl, Catalog, Profile>` and `WSServer<Impl, Catalog, Profile>` use
native `Zws` links. Supply the profile configuration and `WSConfig` to `init`;
`WSConfig::ws` configures framing limits while `limits` bounds RPC work and
retained complete messages. The client starts, then `connect(uri, ...)` forwards
the native link's constructor arguments, such as the optional subprotocol.
The server's `connected(link, info)` callback publishes the established native
link. Retain a `ZmRef` to use it outside that callback; release retained native
links after `stop()` and before `final()`, while their hub still exists.
Server `call`, `notify`,
`callBatch` and `notifyBatch` take that established link as their first argument;
handlers receive the native link before their completion token. RPC state is
constructed once before publication, drained on disconnect and destroyed with
the link. Caller entry points dispatch to the Tx owner; no second link API or
peer registry is needed. `Zws` owns handshake, framing, fragmentation and control
handling across H1 TCP/TLS and H2/H3 extended CONNECT profiles.

Unit tests run through `make -C zjrpc/test test` after building the library and
test targets in the current configuration. `ZjrpcWireTest` covers shared
stdio/SSE framing, fragmented WebSocket input with interleaved control callbacks,
HTTP body bounds, partial input, output failure and application headers.
Rebuild and run tests only at the implementation
milestones in `plan.md`; retain incremental artifacts and the current flags.
`ZjrpcDispatchTest` covers batch completion, duplicates and teardown;
`make -C zjrpc/itest test` exercises duplex stdio calls and notifications over
pipes, including deferred completion from outside the owner shard, and HTTP
fixed JSON/SSE replies with declared request/response headers. HTTP integration
uses loopback port 21100 (reserved for this module, separate from MCP's 21000
range).
`jrpcwstest` exercises generic WebSocket bindings over those four profiles,
including duplex calls, deferred completion, batches, notifications and pending
call teardown. Its loopback ports are 21110–21113. New integration sources are
verified at the final implementation milestone, together with MCP migration.

`HTTPServer` and `HTTPClient` take an explicit endpoint in `ServerConfig` and
`ClientConfig`. Compose link settings with `config.http([](auto &http) { ...; })`.
HTTP replies carry status 200; response types declare headers, never statuses.
The handler receives an `HTTPContext` containing the parsed declared headers.
A server application can provide `peer(headers)` returning a `ZuRef<HTTPPeer>`
created with `HTTPPeer::create(server)` for cross-stream duplicate detection.
The application owns that peer's identity/lifetime and closes it on the owner
shard in bounded turns. Without this hook, requests are sessionless and TCP
connection reuse supplies no peer identity. Disconnecting a reply route does
not close the peer or cancel live work.

`example/zjrpcd` and `example/zjrpc` use the same reflected `add` declarations
over HTTP POST at `/rpc` (default), stdio (`--stdio`) or H1 WebSockets (`--ws`).
`--stream` selects the method whose HTTP response uses SSE. The supervisor owns
stdio process plumbing; results and diagnostics go to stderr, preserving stdout
for wire messages. For example, run the server and client in separate terminals:

```sh
./zjrpc/example/zjrpcd --ws --port=8080
./zjrpc/example/zjrpc --ws --port=8080 --lhs=20 --rhs=22
```

Omit `--ws` on both commands for HTTP. These sample programs use loopback
TCP; the endpoint templates and integration sources also cover the secure and
extended CONNECT profiles. Build the examples at the final implementation
milestone, after their libraries, using the existing configuration and artifacts.
