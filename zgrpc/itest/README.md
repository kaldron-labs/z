# Zgrpc integration tests

Build from the repository root, then run `make -C zgrpc/itest test`.
`zgrpcpipetest` emits TAP using ZuTestUtil. It connects call tokens on separate
Rx/Tx scheduler threads to pooled protobuf encoding, budgeted fragmented receive,
verified borrowed decoding, initial metadata and normal receive completion.
It covers request cardinality for all four RPC kinds and an empty unary message.
Each nonempty message exceeds an ordinary HTTP/2 flow-control window; receive
processing yields after two 4096-byte fragments. Completion invalidates both
routes before scheduler teardown.

This is component integration. It does not open an HTTP connection, test response
streaming, or qualify H2/H3. Native client/server transport and lifecycle tests
remain required by `grpc.md`; Go interoperability belongs in `../interop`.

`zgrpcwiretest` joins the typed request/response adapters through public HTTP
coordinators over loopback TLS H2 and QUIC H3, with empty and 256 KiB payloads.
It checks borrowed decode lifetime after shutdown, Rx/Tx callback ownership,
request producer close once and rejection of a retained emitter after close.
All four transport groups pass. This does not replace the complete
endpoint/lifecycle matrix above.

`zgrpcclienttest` runs the complete client against the native HTTP server fixture.
Its client groups cover initialization/finalization without a running scheduler,
large/empty unary calls on H2/H3, immediate expiry, bounded admission, cancellation
and deadline expiry while Tx is held, late token rejection and shutdown drain.
The held-Tx cases require exactly one failure notification before releasing Tx
and require the admission slot to remain reserved until retirement. Two further
cases hold Rx past expiry, with response work ahead of the timer callback, and
require deadline failure without a metadata callback. The original 21 groups pass full Valgrind leak checking with zero errors and zero
definitely, indirectly or possibly lost bytes.

`zgrpcclienttest` also drives an expired request field provider on Tx while Rx
is held. It requires no operation output and leaves terminal selection queued
to Rx, independently of the timer callback. After Rx resumes, it requires one
DEADLINE_EXCEEDED notification and invalid call routes before teardown.

Client diagnostic arbitration covers deferred frame and metadata failure against
HTTP cancellation, failure and timeout, duplicate completion and absolute expiry. Native
peer cancellation on H2/H3 must report CANCELLED with the original HTTP native
domain/code. GOAWAY refusal retains its own domain through the resulting local
reset. The native-code cases distinguish refusal, cancellation, overload,
security and generic protocol failures, with local cancellation/deadline taking
precedence.

`zgrpcservertest` runs the complete server and client endpoints together. Its 41
groups include all four RPC kinds over H2/H3, messages larger than transport
windows, empty client/duplex streams, duplex responses before request half-close,
bounded client admission and cancellation/deadline drain. It also checks server
initialization/finalization without starting the endpoint (the shared multiplexer
is running, as required by the HTTP server). Eight further H2/H3 groups isolate
encoded count/byte refusal in both endpoint roles after a duplex warmup. They submit on Tx to exclude the pending-post
bound, require RESOURCE_EXHAUSTED at the failing endpoint, and check one terminal
notification and shutdown drain. Two early-reply groups finish duplex output
before request half-close and reject subsequent client operations. Three
controlled server diagnostic cases preserve a metadata failure against generic
transport completion and cancellation while retaining a primary production error. All 41 groups
pass functional validation. Two restart groups stop both endpoints, reject a
call while stopped, restart the same endpoint objects and complete a fresh RPC
on H2/H3. They exposed stale HTTP pool shutdown admission flags; startup now
reopens receive and transmit admission on their owning shards before completing.
See the module README for memory qualification,
toolchain and crypto configuration.

Two longer duplex groups transfer over 64 MiB per direction, exceeding the
configured 1 MiB input retention. After owner-shard barriers, four samples require
a constant live allocation count across the RPC heaps. Releasing endpoint work,
the retained observer and the last decoded message returns those heaps to their
starting count. The 34-group server matrix including these longer streams
passes Memcheck with zero errors and zero definitely, indirectly or possibly
lost bytes; the three newer diagnostic cases await that check.
A dual TLS/QUIC group requires an initial H2 RPC, H3 discovery
through its Alt-Svc response, and a second RPC over H3. Exactly two server
requests are delivered.

`zgrpcdeadlinetest` uses the complete server with a raw HTTP/gRPC client, so no
client RPC timer can hide the server deadline behavior. Its four H2/H3 groups
keep a duplex request open without DATA or half-close. Positive timeouts require
one Rx cancellation callback while server Tx is at a barrier, with the call
still registered until Tx cleanup. The peer must receive a valid HTTP response
carrying gRPC DEADLINE_EXCEEDED. Zero timeouts skip application metadata and open
callbacks while returning that status. Retained tokens reject later operations
after shutdown.

`zgrpcflowtest` withholds all response body consumption on one HTTP stream while
another RPC completes on the same native connection. H2 and H3 use a 4096-byte
stream window against a 256 KiB response, with enough connection credit for the
active RPC. It then cancels the stalled stream, completes a further RPC on that
connection, and checks receive/result notification counts, request retirement
and server drain. The raw HTTP client deliberately withholds credit; the server
uses the complete native gRPC endpoint.
Both groups pass normally and under Valgrind Memcheck with zero errors and no
definitely, indirectly or possibly lost bytes.

The unit suite separately covers 41 groups. Delivery qualification uses the
Clang debug build; the native integration matrix is run after the full build.

Two H2/H3 groups send runtime request metadata and runtime response metadata in
both initial headers and trailers. They require the matching application
sensitivity policies on Tx and verify the received values in the proper
sections. `ZhttpH2MessageTest` and `ZhttpQPackDynamicTest` separately verify
that those policies emit the never-index representation. TLS shutdown removes
every destroyed link from its telemetry registry and retains live links across
the drain snapshot, matching the TCP lifecycle pattern.

Two redirect cases configure retries and redirects, require the gRPC pool to
turn both off, and verify a single peer request. A final HTTP 307 without gRPC
status reports UNKNOWN with MissingStatus and retains the HTTP status.

Four GOAWAY cases use a test-owned native peer on H2 and H3. Two refuse the
first request with the respective inclusive or exclusive boundary zero. Two
keep an accepted request in flight, refuse the next request, finish the accepted
request and require a later request to use a replacement connection. Refusals
require one UNAVAILABLE completion carrying ReplayUnsafe, the native HTTP
version and nonzero request/attempt identifiers, with no retries or redirects.
The complete 31-group client binary passes functional validation and Valgrind
Memcheck with zero errors and zero definitely, indirectly or possibly lost
bytes.

A transport-transition case first learns an H3 alternative from a successful H2
response, then uses an H3 endpoint with an untrusted certificate. It requires
failure of the new RPC without another H2 request and passes functional validation.
The shared port probe uses the framework socket type and close operation after
multiplex startup, including Winsock initialization.
