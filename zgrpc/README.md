# Zgrpc

Native gRPC over Zhttp H2/H3, using Zpb for protobuf payloads. The full
implementation and acceptance requirements are in [grpc.md](../grpc.md).
Both endpoints are implemented. Clang debug unit, native transport and
independent-peer matrices pass. The remaining lifecycle and replay checks are
tracked below.

Implemented foundations:

- Shared-library build, header installation rules and pkg-config metadata.
- Method defaults, kinds, status codes, positive endpoint limits, exact method
  path catalogs and merged header catalogs.
- Incremental five-byte envelopes, bounded single-message feed operations,
  sticky framing failures, retained contiguous slices and pooled assembly.
- Direct pooled protobuf output with network-order envelopes and preserved
  transport headroom/tailroom.
- Typed message values with optional retained backing, verification before
  construction and explicit borrowed-data policy.
- Bounded metadata arenas, selected catalog IDs, repeated dynamic fields and
  comma-split binary values; separate initial/trailing storage and protocol
  control validation with terminal status published only after receive end.
- Timeout conversion, status parsing/HTTP fallback, percent-encoded status
  messages and padded/unpadded binary metadata value codecs.
- Structural initial/trailing field providers that render binary values into
  final HTTP field storage and apply sensitive-name policy to fixed and runtime
  fields without deduplication.
- Typed request/response HTTP builders and parsers, retained body output,
  status trailers/trailers-only responses, raw payload modes and a pre-envelope
  payload signing hook.
- Owned receive queues with bounded decode turns, incremental pooled assembly,
  callback ownership across parser reset and completion ordered after input.
- Catalog-wide builders/parsers with active-method metadata defaults and exact
  method dispatch.
- Client and server endpoints with bounded admission, serialized sends, deferred
  results/replies, cancellation, deadlines, call registries and event-driven
  shutdown drain. Server dispatch selects typed request or streaming handlers;
  unknown methods receive UNIMPLEMENTED without application dispatch.
- Client/server call handles with independent invalidatable Rx/Tx routes,
  bounded off-shard send posts and one-shot end/finish/complete/cancel admission;
  separate directional cardinality state.

`FrameRx::feed` consumes at most one message and its supplied byte budget. The
adapter must bound calls per scheduler turn, retain unconsumed input and post a
continuation. A non-null input backing promises immutable storage until the
returned frame is released; mutable transport queues must not supply that
promise unless their extraction contract guarantees it. Without backing, input
is copied into one consumer-owned pooled buffer. Typed adapters must verify
payloads with Zpb before constructing objects; raw framing alone is opaque.

Build from the repository root with `make -j8`. Run `make -C zgrpc test` after
the build. The module includes native `itest` and explicit Go `interop` targets;
see [itest](itest/README.md) and [interop](interop/README.md) for their scope and
commands. Canonical Go dependencies are not part of the default native build.
`ZgrpcFrameTest` exercises all split points, byte-wise fragments,
coalesced/empty messages, truncation, bounds, budgets, buffer lifetime and pooled
protobuf output. Independent wire bytes cover constructor positions distinct
from field IDs, nested borrowed values, packed repeated fields, borrowed map
values, unknown fields and last-oneof-wins behavior. Rich messages also encode
and decode through the pooled frame path and pass Valgrind. `ZgrpcMetaTest` exercises timeout rounding/saturation, status
codes/fallback and metadata value codecs. `ZgrpcCatalogTest` checks exact
matching, merged header values, invalid metadata declarations and descriptor
cases. `ZgrpcCallTest` exercises cardinality, role constraints, concurrent
admission while Tx is held, finish ordering, counter release on discarded work,
and queued operations after destination owners are invalidated and deleted.
It also checks endpoint shutdown in bounded batches, retirement between batches
repeated stop callbacks, stop during successful/failed startup and restart
after a cancelled startup.
`ZgrpcHTTPTest` exercises retained production, terminal fields, sensitive defaults,
raw sends, exact signing bytes, queued end/status ownership, encoded count/byte
refusal, deferred receive lifetime, bounded admission,
compression/length/truncation failures and terminal notification once.

Remaining delivery gates include the broader lifecycle and replay matrix. The
H2/H3 GOAWAY refusal, accepted-stream drain and replacement
cases, together with the H2-to-H3 trust-failure transition, pass in the 31-group
client integration fixture. That binary also passes Valgrind Memcheck with zero
errors and zero definitely, indirectly or possibly lost bytes. Delivery
qualification uses Clang debug; Valgrind runs at the end unless memory
corruption needs investigation.

Foundation verification: Linux x86_64, Ubuntu Clang 21.1.8, GNU C++2b debug
(`-march=x86-64-v2 -g -DZDEBUG`). The five unit binaries pass 41 test groups;
the six native integration binaries pass 87 groups. The client lifecycle,
flow-control and server-deadline fixtures pass Valgrind 3.26.0 with zero errors
and zero definitely, indirectly or possibly lost bytes. Memcheck runs set
`OPENSSL_ia32cap=0` to select software crypto, as do the independent peer runners.
`ZgrpcHTTPTest` also passes full leak checking with zero errors and no definite
or indirect leaks. The 34-group server matrix also passes Memcheck with zero
errors and zero definitely, indirectly or possibly lost bytes, including both
64 MiB duplex transfers. The newer diagnostic, restart and runtime metadata cases and the full memory
matrix remain to be qualified under Memcheck.
The independent Go fixture passes its TLS loopback self-tests and the Go race
detector on Go 1.26.8 using grpc-go 1.84.0. The native client also passes the
canonical Go TLS peer matrix for all four RPC kinds, empty messages/streams,
binary metadata, trailers-only errors, cancellation, deadlines and concurrency;
that run is Valgrind-clean. Both native-client/Go-server and Go-client/native-server
directions pass the canonical H2 matrix and full Valgrind leak checking. A second
independent Go peer uses Connect 1.21.1 in gRPC mode over quic-go 0.64.0 HTTP/3;
both native endpoint roles pass its H3 matrix and full Valgrind leak checking
with zero errors and no definite/indirect leaks. Both Go peers pass the race
detector.

Live adapter qualification in `zgrpcwiretest` covers
unary echo with empty and 256 KiB payloads through public HTTP client/server
coordinators. Retained client production, pooled-frame adoption and queued-request
retirement repairs in the HTTP precursor pass these tests. `zgrpcclienttest`
adds endpoint admission, immediate expiry, cancellation and deadline expiry while
Tx is held, late token rejection and shutdown drain on both H2 and H3. It also
holds Rx past the absolute expiry and rejects late metadata/message dispatch. The
complete server passes 41 integration groups, including all four RPC kinds on
H2/H3, empty client/duplex streams, duplex responses before request half-close
and initialization/finalization without starting the endpoint. Encoded Tx queues
independently bound frame count and bytes, retain writer-allocated buffers,
and hand off at most `workBatch` messages per turn. Queued end/status waits for
that handoff; queue refusal aborts the stream and preserves the original local
failure. Native H2/H3 tests isolate these limits from pending send posts and
cover early duplex completion before request half-close.
`zgrpcdeadlinetest` adds server-only deadlines while Tx is held and zero-timeout
rejection before metadata/open callbacks. Rx event boundaries recheck the
absolute expiry before dispatch and terminal selection. Both endpoints calculate
the expiry from the current `Zm::now()` time plus the interval and arm
`ZmScheduler::add` with that absolute time. The scheduler adds no thread or
periodic polling for this facility. Full transport
and lifecycle qualification remains required. The normal
full-tree build and Go H2/H3 plus grpc-dotnet H3 peer tests pass. Broader delivery
gates in `grpc.md` remain separate from this focused qualification.

Native HTTP reset diagnostics remain in `Failure::http`. The client maps reset
codes using the [H2 transport table](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#errors)
and [H3 transport table](https://github.com/grpc/proposal/blob/master/G2-http3-protocol.md),
while preserving original local metadata/framing failures and deadline priority.
Server expiry returns gRPC DEADLINE_EXCEEDED through the normal response path;
zero-timeout requests skip application callbacks while still returning status.

`zgrpcflowtest` holds a response stream at its receive-credit limit while an RPC
on the same connection completes, on H2 and H3. Cancelling that stream permits
a further RPC on the connection. This exposed and fixed QUIC reset final-size
accounting: queued application bytes are excluded from the transmitted extent,
and reset releases stream-owned output and recovery ranges. Client field
construction also rejects an elapsed deadline on Tx and posts terminal selection
to Rx; a held-Rx regression verifies that boundary independently of the timer.

Two duplex cases transfer over 64 MiB per direction through 1 MiB of finite
input retention. Four samples keep the RPC heaps' live allocation count constant;
after teardown and release of the retained message and observer, it returns to
the starting count. A dual TLS/QUIC case learns Alt-Svc on its first H2 RPC and
uses H3 for the next RPC, requiring exactly two server deliveries. The receive
catalog declares Alt-Svc so the existing HTTP client policy can update discovery.

Server applications implement `request(Method *, Context &, Message<Request>,
ZmRef<ServerCall<Method>>)` for unary and server streaming, or return an intrusive
stream observer from `open(Method *, Context &, ZmRef<ServerCall<Method>>)` for
client streaming and duplex. Stream observers receive `message`, `peerEnd` and
optional `cancelled(Code)` on Rx. Deferred replies use the retained call token.
An optional `headers(Method *, Context &)` returns owned initial `Metadata` on
Rx; it is transferred to Tx before response production. Context views remain
callback-scoped. Optional transport/listener callbacks are forwarded to the
application.

Endpoint start/stop coordination runs on Rx. Stop closes admission immediately
and retains startup and posted stop entries through their owner acknowledgements.
A startup overtaken by stop reports false and is drained before stop completes;
a fresh start is accepted after shutdown completes. Idle stop/finalization needs
no scheduler work when the endpoint was never started.
After start, the application completes `stop()` before `final()`. Finalization
asserts stopped state and does not initiate shutdown.
