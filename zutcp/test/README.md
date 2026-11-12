# Zutcp unit tests

Reuse the configured Clang/debug/ASan tree. Build from the top level with
`make -j8`, then run:

```sh
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 make -C zutcp/test test
```

`ZutcpIPTest` covers the intermediate Ethernet/IP/UDP carriage helpers using
independent IPv4/IPv6 wire vectors, borrowed payload identity, checksum corruption
and computed-zero encoding, every payload length through MTU, truncated extents,
IPv4 options and common IPv6 extension/atomic-fragment headers. Fragmented input
is not a complete transport datagram and requires IP reassembly outside these
helpers. Header construction changes framing/padding only; it neither allocates
nor moves payload. Linux UDP peer coverage lives in `../itest/zutcpiptest.cc`.

The other programs still cover the mechanically renamed transport baseline
during phase-1 adaptation. They do not establish the stripped native working
pair or TCP interoperability. See `../../zutcp_plan.md` for the current scope.

`ZutcpAPITest::testPeerParamOwnership` delays the Rx-to-Tx handoff and checks
that new Tx streams retain the previous credit until Tx installs the snapshot.
The decoded caller storage is overwritten before delivery. Crypto no longer
provides a duplicate TLS parameter carrier; the copied runtime tests still
exercise TLS flights/readiness pending their removal.

`ZutcpHandshakeTest` now tests the plaintext Initial codec and bootstrap transcript:
parameter-bearing request, ACK-bearing response, and final ACK without parameters.
It uses the actual CID bootstrap, frame codec and sent-packet accounting. It also
checks a fixed wire vector, all truncated extents, packet-number widths and
coalesced-packet bounds. Parameters and frames borrow the original input; output
frames are constructed directly in their declared final extent. These tests
replace four copied TLS/CRYPTO scenarios with three intermediate-protocol cases.
They do not establish live handshake timers, callbacks or the native working pair.

The live client/server Initial path now uses the same plaintext codec, including
existing Retry tokens. `ZutcpRuntimeTest` inspects emitted server Initials without
decryption and requires a decoded parameter record with matching source CID.
The copied TLS flight still supplies frames and readiness, while Handshake and
application packets remain protected until their production conversion. Passing
these tests therefore does not prove the stripped native working-pair gate.

Connection-only setup credit replaces copied per-stream byte/count negotiation.
`ZutcpCodecTest::testTransportParamCoverage` checks an independent exact parameter
vector containing the connection byte limit and no stream limits. Parameter qlog
coverage requires obsolete stream negotiation fields to be absent. The delayed
owner handoff case now checks the connection credit snapshot. Copied stream
object/control tests remain transitional and do not establish consolidation.

The copied transport owner API is now `Connection`, `CliConnection` and
`SrvConnection`, with matching headers. Fixtures use those names directly.
The byte-state implementation has now moved into Connection. The old separate
stream implementation and registries are deleted; some copied fixtures still
reference their removed APIs and require migration before the full suite builds.

`ZutcpConnectionTest` exercises the physically merged connection directly, without
stream factories, IDs or child objects. It covers reordered packet slices,
partial byte delivery and retirement, duplicate suppression, producer queue
pressure, credit-limited packetization retaining original backing, FIN and partial
ACK retirement. This focused test does not replace the remaining recovery,
lifecycle and native endpoint gates. During consolidation, the copied fixtures
still require porting before the full unit suite can build.

`ZutcpFlowTest` now uses a real merged connection to check a single byte window,
retirement-driven credit replenishment, advisory blocking and stale peer-credit
updates. Removed per-stream/count wire codes decode as unknown and fail the
connection's frame-admission policy. `ZutcpCodecTest::testConnectionWire` checks
independent exact data/reset/stop vectors containing no stream ID and every
truncated extent of the data record. `ZutcpConnectionStateTest` replaces the old
stream-state utility program; stream ID and count-limit utilities are deleted.

`ZutcpAPITest` now constructs concrete client/server connections directly. Its
ownership cases delay peer-parameter delivery to Tx, verify repeated setup cannot
shrink extended credit or reset consumption, exercise readiness without owner
reference churn, and race STOP with reset across Rx/Tx. The old registry and
stream-count ownership cases are removed. `ZutcpLoopTest` delivers reordered
bytes directly to Connection, and `ZutcpTimerTest` no longer declares a child
stream type. Logging tests cover connection blocking transitions and require
stream identity/lifecycle fields to be absent from the corresponding records.

`ZutcpDataTest` replaces the old stream-runtime unit program. Its surviving byte
cases now use Connection directly, covering borrowed Rx slices and overlap,
retirement, original Tx buffers, queue pressure, partial ACK/loss/retransmission,
producer-credit callbacks, bounded flush continuation, connection control
replacement and RESET/STOP. Multiple data frames are built from separate queued
buffers on one connection. Independent subcases use separate connections.
The stream ID/count/registry/GC cases are removed, with no compatibility factory
or dummy ID. Prefix-only packetization is bounded by scratch capacity while its
payload remains a separately retained range; insufficient prefix/packet budget
must leave queue and assembly state untouched. Retained crypto/path cases still
exercise copied facilities scheduled for removal; this program does not establish
the stripped native working pair or TCP interoperability.
