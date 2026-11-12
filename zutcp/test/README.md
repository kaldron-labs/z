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

The other programs cover the surviving connection, codec, recovery and runtime
facilities during phase-1 adaptation. They do not establish the stripped native
working pair or TCP interoperability. See `../../zutcp_plan.md` for the current
scope.

`ZutcpAPITest::testPeerParamOwnership` delays the Rx-to-Tx handoff and checks
that connection credit changes only when Tx installs its bounded snapshot. The
decoded caller storage is overwritten before delivery.

`ZutcpHandshakeTest` tests the plaintext Initial codec and bootstrap transcript:
parameter-bearing request, ACK-bearing response, and final ACK without parameters.
It uses the actual CID bootstrap, frame codec and sent-packet accounting, with a
fixed wire vector, truncated extents and packet-number width coverage. The
Initial header places payload length directly after its CIDs, with no token
length or payload. Parameters and frames borrow the original input; output frames
use their declared final extent. Deleted crypto/token/handshake-confirmation frame
codes are checked as unknown; connection admission rejects them.

The live client/server setup also uses this plaintext codec. `ZutcpRuntimeTest`
checks emitted Initial parameters, repeats setup after deliberately dropped
responses, exchanges retained bytes in both directions, and exercises connection
ownership, multiple clients and idle shutdown. Certificate provisioning and
TLS/protection/early-data/Retry-specific cases have been removed. The remaining
transport path uses kernel UDP sockets until the native endpoint is wired; these
cases do not establish the native AF_XDP working-pair gate.

Connection-only setup credit replaces per-stream byte/count negotiation.
`ZutcpCodecTest::testTransportParamCoverage` checks an independent exact parameter
vector containing the connection byte limit and no stream limits. Parameter qlog
coverage requires obsolete stream negotiation fields to be absent. Reset-token
transport parameters are removed; the former parameter code follows ordinary
unknown-parameter handling. Bootstrap tests retain CID identity checks.

The transport owner API is `Connection`, `CliConnection` and `SrvConnection`,
with matching headers. Byte state, reassembly, producer queues and retained Tx
ranges belong directly to Connection. No separate stream owner or registry is
required by these fixtures.

`ZutcpConnectionTest` exercises the physically merged connection directly, without
stream factories, IDs or child objects. It covers reordered packet slices,
partial byte delivery and retirement, duplicate suppression, producer queue
pressure, credit-limited packetization retaining original backing, FIN and partial
ACK retirement. This focused test does not replace the remaining recovery,
lifecycle and native endpoint gates. The full suite and native endpoint gates are recorded in the plan.

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
must leave queue and assembly state untouched. Crypto and protected-packet
cases are removed; surviving path cases remain
scheduled for phase-1 deletion. This program does not establish the stripped
native working pair or TCP interoperability.

CID rotation, reset-token/path-challenge value tests and migration-only fixtures
are removed. Router tests retain variable CID length matching, active route
lifecycle and bounded tombstone FIFO coverage. Byte/control assembly tests use
MAX_DATA, RESET and STOP_SENDING; send refusal and congestion retain queued
controls. The runtime pair also injects a fresh data packet with the established
CID from a foreign tuple and checks that it cannot admit bytes; the original pair
then exchanges its exact application bytes. Endpoint queue coverage uses explicit
disconnect/drain/reopen and preserves capacity, ownership and generation checks.
