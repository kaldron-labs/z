# Zhttp additions for gRPC

Status: focused implementation plan against the current working tree. Existing
streaming is the foundation; additions below are proposed, not implemented.
[grpc.md](grpc.md) owns RPC framing, metadata policy, cardinality, deadlines,
protobuf integration and application dispatch.

## Existing support and scope

Zhttp already streams bodies over H2 and H3 without hidden full-body buffering.
It has incremental receive callbacks, section-tagged receive fields, pooled
output buffers, flow control, cancellation and asynchronous endpoint teardown.
Server producers can retain `BodyEmitter`, return `WriteOutcome::Stream`, and
produce further turns on Tx. Their close/invalidation discipline already
handles queued emitter copies. See [zhttp/README.md](zhttp/README.md).

Client request writers also transmit streaming bodies, but their application
production is single-turn: the writer must return `End`. That is sufficient
for a unary or server-streaming request. It does not support a client producing
messages over successive application turns.

The principal missing wire capability is outbound trailers: ordinary builders
currently emit only initial fields. Extend existing production and completion
paths where required. Internal factoring should follow the implementation;
sharing the entire client/server producer state machine is not a prerequisite.
Apply [GUIDELINES.md](GUIDELINES.md), including ownership, allocation and teardown
rules, and propagate necessary API changes directly through affected dependents.

H2 and H3 remain baseline transports as specified by grpc.md. Use the existing
public CRTP endpoints and transport profiles; keep protocol implementation
headers private. A gRPC call is an ordinary POST. Existing CONNECT frame-mode
operations do not substitute for ordinary request/response production.

## Required deltas

| Capability | Work | Needed for |
| --- | --- | --- |
| Response trailers | Add final ordinary fields to the existing response producer, ordered after accepted DATA | Every successful RPC, including unary |
| Trailers-only response | Verify the existing no-body response path can terminate with all required initial fields | Immediate RPC errors |
| Receive completion | Verify section-tagged fields and existing completion callbacks provide the necessary ordering; add only missing notification | Metadata and terminal status |
| Multi-turn client production | Extend the client producer using the server producer's established lifetime discipline | Client-streaming and duplex |
| Early final response | Verify response preservation and unfinished request disposal; repair demonstrated gaps | Early rejection and completion |
| Replay policy | Close any automatic reissue path that bypasses the RPC no-replay policy | grpc.md's initial call policy |

### Response trailers

Add a trailing-field operation to the existing ordinary response producer.
Use the current typed catalog and dynamic field-provider machinery. Its exact
signature should fit `BodyEmitter`; a new general stream abstraction is not
required. gRPC requests need ordinary request end, not request trailers, so
symmetric trailer production on both roles is not a delivery requirement.

Accepted trailing fields close producer admission. Emit them after the entire
accepted DATA prefix, including data stalled on stream flow control. Later body
or finish submissions fail. Render borrowed provider values into owned queued
storage before returning; encode the section when eligible so shared compression
state follows wire order. Reuse existing queues and invalidation machinery.

On H2, emit trailing HEADERS through HPACK with END_STREAM and correct
HEADERS/CONTINUATION framing; preceding DATA must remain open. On H3, emit a
trailing HEADERS section through QPACK, then FIN after the section. Preserve the
existing compression profile and cleanup. A trailers-only response terminates
its initial field section through the existing no-body path, without a second
section or manufactured DATA.

Zhttp handles HTTP fields and termination. Zgrpc supplies `grpc-status` and
interprets trailers-only responses. The wire contracts are the
[gRPC H2 protocol](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md)
and [H3 mapping](https://github.com/grpc/proposal/blob/master/G2-http3-protocol.md).

### Receive fields and completion

`Parser::header(FieldSection, ...)` already distinguishes informational,
final-initial and trailing fields. Start by documenting and testing its ordering
with `bodyInfo`, body callbacks, normal completion, failure and parser reset.
Zgrpc must retain selected metadata before borrowed field storage expires and
publish terminal status only after normal response completion establishes that
it is usable. An individual field callback does not establish HTTP acceptance.

If those callbacks cannot identify an accepted section boundary needed by the
adapter, add a section-completion hook through the installed parser bindings,
with a harmless base default. Do not require a new parallel terminal-event API
or both section-end and peer-end callbacks without a demonstrated need.
H3 FIN may arrive after HEADERS; tests must cover that ordering. Reuse existing
completion/result reporting for reset, connection failure and unprocessed
attempts, extending only information needed for RPC error mapping.

### Multi-turn client production

After unary and server-streaming support, extend ordinary client request
production to retain an emitter across turns. Reuse `BodyPolicy::Stream`,
`WriteOutcome::Stream` and `End`, pooled output and existing queue admission.
A zero-byte open turn must not end the request; `End` half-closes the request
while response reception continues. Keep the existing single-turn path usable.

Apply the server producer's close/invalidation discipline on the client:
invalidate retained emitters, reject late turns, close the builder once on Tx,
and drain queued work before releasing owners. Never destroy a producer while
its writer is active. Factor common mechanics only as needed to avoid duplicated
lifecycle logic; no wholesale endpoint rewrite is prescribed.

### Early replies, cancellation and retirement

Exercise the existing response routing, cancellation and drain paths before
changing them. The server must be able to respond while request DATA is arriving.
A completed final response must survive stopping an unfinished request, including
queued client producer actions. Stop application delivery of discarded request
bytes and preserve flow-control accounting. Use existing exchange cancellation
for deadline or whole-call aborts.

Repair only missing early-completion behavior. On H2, preserve the complete
response before any RST_STREAM(NO_ERROR) used to stop the request, as described
in [RFC 9113 section 8.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-8.1).
On H3, stop the request receive direction without resetting the response send
direction; see [RFC 9114 section 4.1](https://www.rfc-editor.org/rfc/rfc9114.html#section-4.1).
Add an ordinary-message stop operation only if existing routing cannot express
this behavior. Keep ownership on the appropriate Rx/Tx shard and retain existing
asynchronous teardown. Do not introduce a comprehensive event record or general
directional-operation family solely for gRPC.

## Targeted checks alongside the adapter

These checks retain grpc.md's performance and policy requirements. They do not
gate starting Zgrpc on a general Zhttp redesign.

- **Long-lived input:** exercise incremental consumption beyond the default
  cumulative body ceiling, with an appropriate configured allowance and bounded
  queued storage. Use existing settings where sufficient. If total-byte and retained-byte enforcement prevent this,
  separate those checks in the owning paths; justify any configuration change
  from that failure. Keep per-protobuf-message bounds in Zgrpc.
- **Receive ownership:** use existing `Input`/`ZiRxStream` extraction and pooled
  buffers. Zgrpc retains contiguous payload storage or incrementally assembles
  fragmented messages. Deferred decoding owns its bytes and call state before
  HTTP completion resets the parser. Add a transport ownership primitive only
  if existing operations cannot provide that ownership.
- **Pooled output:** validate the existing `Writer << ZmRef<ZiIOBuf>` path with
  detached Zpb buffers, nonzero `skip`, and DATA splitting. Preserve the project
  requirement to avoid whole-payload staging copies. Fix only demonstrated
  adoption gaps; new allocation APIs and shared Zi changes are conditional.
- **Sensitive fields:** preserve existing never-index defaults. Expose a minimal
  field/provider sensitivity binding if custom RPC metadata cannot otherwise
  use the existing encoders safely. A new catalog subsystem is not prescribed.
- **No replay:** use existing retry/redirect configuration, then test automatic
  unprocessed-attempt and transport-transition paths as well. If a path can
  reissue an RPC despite that configuration, add a request-level replay gate.
  grpc.md disables transparent replay, including ordinary HTTP reissue.
- **Transport failures:** verify existing reset, GOAWAY and failure results are
  sufficient for RPC status mapping. Preserve native details where required;
  extend the existing result contract only for missing facts.

Zhttp continues to own transport flow control and queue bounds. Zgrpc owns its
message assembly, call queues and extracted application data. No new receive
credit API, aggregate application retention accounting, parser pause/resume
protocol or scheduler admission subsystem is required.

## Delivery and verification

1. **Unary transport delta:** add response trailers on H2 and H3. Verify
   trailers-only responses and existing receive completion, making only missing
   callback changes. Test DATA/trailer order under flow-control stalls and
   producer invalidation. Build the unary Zgrpc adapter on this surface.
2. **Server streaming:** reuse the existing multi-turn server producer. Exercise
   early replies, cancellation and shutdown with the adapter; repair any missing
   routing or drain behavior. Perform targeted ownership, buffer and policy
   checks as the adapter reaches those paths.
3. **Client streaming and duplex:** extend client production across turns and
   verify request half-close, concurrent response reception, early final response
   and late producer rejection. Test long-lived bounded-memory operation.

Use focused TAP tests for changed contracts in `zhttp/test` and endpoint coverage
in `zhttp/itest`. Exercise installed public headers on both transports. Cover
fragmented fields and DATA, delayed H3 FIN, stalled DATA before trailers, queue
refusal, reset and stop with outstanding producers. Use deterministic scheduler
barriers. Run affected existing HTTP and dependent regression tests; additional
independent HTTP qualification infrastructure is not a prerequisite to starting
the RPC adapter. Zgrpc's independent gRPC peer tests establish interoperability
in both roles on H2 and H3, as required by grpc.md.

For implementation changes, build with top-level `make -j4`, build each affected
module's `all` target before `make test`, and use `libtool exec` for instrumented
source-tree binaries. Record tested toolchains and untested platforms. This
plan makes no implementation or test-result claim. Keep grpc.md's question IDs;
resolve individual entries only when the corresponding implementation and tests
establish the answer.
