# Zutcp unit tests

`ZutcpRecoveryTest::cumulativeByteHistory` and `byteHistoryBatchAndFin` cover the
Phase 3 cumulative byte-history operation: partial and duplicate ACKs, stable
covering coordinates, repeated sends of identical bytes, bounded byte-ordered
retirement, final-position handling and exclusion of bootstrap/empty records.
The byte index uses a non-owning hook embedded in the existing sent-packet node.
This is preparatory recovery work; the live ACK wire/RTO conversion remains open
in `../zutcp_plan.md`. The application pair remains a regression check until that
conversion is connected and its end-to-end Phase 3 cases pass.

`ZutcpDataTest::testMxTxQueue` checks the Zi Tx-context contract against the
existing connection queue: a full queue preserves the next context, dequeue
credit resumes it, callbacks retain control of the send offset, and zero-byte
completion adds no progress counts. The context replacement includes same-owner
reentry. The live registered-buffer counterpart is `../itest/zutcpmxtest`.

`ZutcpRxAccountingTest` covers distinct physical slot charges, independent shared
and per-account bounds, retired account lifetime, concurrent off-owner returns
and repeated slot reuse. It tests the Zutcp-only accounting primitive; connection
admission and gap/control pressure are covered separately below.

`ZutcpRxAdmissionTest` drives connection resource refusal/retry without byte-state
commitment, duplicate demand, gap service using heap replacement, service under
application backpressure bounded by byte credit, and a full Short packet whose
later control processes while refused DATA withholds its ACK. Its resource gate
is a test seam; `../itest/zutcpstacktest` verifies native shared/per-connection
limits over original registered ingress.

`ZutcpBufferTest::testRxSliceReturn` verifies that receive aliases hold their
original packet only until destruction or heap replacement. It covers clipped
views, off-thread final replacement, repeated heap growth, immediate backing reuse
and exactly one provider return. `testPacketAllocRoles` checks the same transition
with ordinary heap-backed source packets.

Live client/server coverage now runs through Stack/Zxdp in `../itest/zutcppairtest`.
The former kernel-loopback runtime and completed-socket-queue cases were retired
and archived with the native-endpoint checkpoint. Native multi-connection, idle,
pressure and stale owner-work equivalents remain open; current green tests do not
extend the completed Phase 1 regression gate or establish TCP interoperability.

`ZutcpBufferTest::testAllocatorDirection` verifies Hub initialization and retained
byte adaptation make zero mx allocator requests. An external application-side
factory request calls the plugin provider hook once. `ZutcpTestAlloc.hh` contains
only test/application factory exercises; production Zutcp owns its internal pools
and adapters, and makes no request to `ZiMultiplex::allocator`.

Reuse the configured Clang/debug/ASan tree. Build from the top level with
`make -j8`, then run:

```sh
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 make -C zutcp/test test
```

`ZutcpIPTest` covers direct Ethernet/IP framing using independent IPv4/IPv6
wire vectors, borrowed payload identity, IPv4 header checksum corruption, every
payload length through MTU, truncated extents, IPv4 options and common IPv6
extension/atomic-fragment headers. Fragmented input requires IP reassembly outside
these helpers. Generic IP framing writes headers/padding only. TCP cases cover
base-header fields, IPv4/IPv6 pseudo-header/header/payload checksums, borrowed TCP
options/payload, corruption, truncation, failed-write atomicity and every payload
length through family-specific MTU. The independent raw-socket peer lives in
`../itest/zutcpiptest.cc`. The native pair supplies the completed Phase 1 regression gate; later protocol
and integration acceptance remains in the plan.

The other programs cover the surviving connection, codec, recovery and runtime
facilities during phase-1 adaptation. They do not establish the full native
phase-1 gate or TCP interoperability. See `../../zutcp_plan.md` for the current
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

The server setup cases use the real SrvConnection/codec/recovery with delayed
owner queues. Parameters and response submission do not establish; a matched
response ACK publishes to Rx only after the full bounded ACK operation. Cases
cover unsent/duplicate ACKs, early Short input and staged reply resumption, both
reset timings, multi-batch publication and late response evidence beyond the PN
sample bound. Native end-to-end coverage remains in `../itest/zutcppairtest`.

The live client/server setup uses this plaintext codec through Zxdp. Current
runtime acceptance lives in `../itest/zutcppairtest`. The archived loopback
runtime covered additional multi-client, idle-qlog and ownership regressions;
those cases still need native equivalents. Certificate provisioning and
TLS/protection/early-data/Retry-specific cases are removed.

Connection-only setup credit replaces per-stream byte/count negotiation.
`ZutcpCodecTest::testTransportParamCoverage` checks an independent exact parameter
vector containing the connection byte limit and no stream limits. Parameter qlog
coverage requires obsolete stream negotiation fields to be absent. Reset-token
transport parameters are removed; the former parameter code follows ordinary
unknown-parameter handling. Bootstrap tests retain CID identity checks.

The transport owner API is `Connection`, `CliCxn` and `SrvCxn`,
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

Tx retained ranges now reference shared queued-node storage, independently of its
mutable queue position and pending Zi descriptor. The data tests check original
descriptor coordinates through partial consumption, retained byte identity after
queue reset, and the same storage identity through loss/retransmission splitting.
Production writers now allocate registered backing through Stack and first
submission reuses it. Logical unit apps explicitly use local builtin storage.
ZutcpBufferTest verifies Hub does not request an mx allocator and refuses to
supply native producer storage through a non-native plugin. Packetization and
recovery fixtures still cover heap-backed application input and fragmentation.
Native same-slot production and whole-buffer credit are exercised by
zutcpstacktest. The copied recovery and packetization path is retained in Phase 1.
Covering retransmission, cap-resegmentation and CQ waiter extensions are removed;
the retained unit and native pair regressions are the Phase 1 exit checks.

Control reserve defaults and invalid zero counts/credits are checked in
ZutcpBufferTest without requesting an mx allocator. Native backing separation,
reserved admission, CQ-only credit release, held-control resize refusal, stopped
reconfiguration and large-ACK scheduling are covered by zutcpstacktest.

Phase 3 cumulative byte ACKs are exercised by ZutcpCodecTest and ZutcpDataTest.
The codec checks the intermediate byte frontier envelope, truncation and legacy
bootstrap ACK separation. Connection tests reject ACKs beyond accepted sends,
preserve DATA on packet-only ACKs, and retire partial prefixes and the remaining
suffix through cumulative ACKs. RTO and covering retransmission acceptance is
recorded in the final Phase 3 checkpoint linked from zutcp_plan.md.

DATA recovery now uses a separate cumulative-progress RTO. ZutcpRttLossTest
checks its injected-time boundaries, adaptive floor/granularity, backoff cap,
partial progress, disarm and reset. ZutcpRecoveryTest checks bounded oldest-byte
selection across repeated transmissions and exclusion from control packet loss.
The timeout congestion check preserves ssthresh on repeated loss of the same
covering range. ZutcpTimerTest exercises the actual connection RTO separately
from control PTO and includes it in cancellation and disconnect draining.
ZutcpBufferTest verifies Hub defaults, bounds and configuration forwarding.
Connection timestamps and timer deadlines use the inherited Zm::now() clock
and existing mx add() API. ZutcpTimerTest injects time to check early callbacks,
replacement deadlines, Advance ordering, rearming and cancellation. No scheduler
extension or clock-domain conversion is required by this port.
The final Phase 3 acceptance run passes all 190 unit cases and 43 privileged
packet cases, including controlled Karn timing and live covering recovery.

Registered covering retransmission is tested separately from the inherited
unregistered clipping fixtures. Its simulated registered allocator checks that
resubmission uses the exact retained payload address without invoking the packet
allocator, busy NIC ownership prevents every header write, partial byte ACKs keep
the full original packet, and the final ACK returns backing once despite retained
history. DATA-plus-FIN also retains backing after payload-only ACK and resends the
original packet when only FIN remains. Real kernel/CQ behavior is exercised by
the privileged provider/native/mapped integration fixtures.

The controlled connection RTT case seeds a 100 ms measurement, acknowledges an
accepted retransmission at an ambiguous interval and requires the estimator to
remain unchanged. Fresh 20 ms DATA then resumes sampling. A later 80 ms sample
with an encoded 10 ms peer ACK delay verifies timestamp-free DATA timing does not
subtract that delay. The later sample is deliberately above the minimum RTT so
the estimator's existing minimum guard cannot hide incorrect subtraction.
Fixture transport time stays ahead of the real scheduler clock; no host clock is
changed and teardown cancels its future timers.

The Phase 4 receive-side window-probe case exhausts a small byte window,
consumes application input, drops its reopening update and repeats the sender's
old blocked-edge probe. It checks the unchanged closed edge, recovery of the
current opening edge, duplicate response coalescing and exactly-once credit
retirement. The Tx-owned persist test advances connection time through the one-second floor,
exponential backoff and 60-second cap, including a refused send. It submits real
Short packets through the shared builder with empty recovery refs and checks
that ACKs create no congestion flight, reduction or DATA/control recovery. Window
opening resumes queued DATA immediately; reset cancels the schedule. A configured
three-second RTO selects the longer initial interval. The single-packet API also
arms persist when it exhausts credit; acknowledging the transmitted prefix
disarms DATA RTO without canceling persist for the queued suffix. Deferred Rx-to-Tx window
work is rejected after a connection generation reset, while current work succeeds.
ZutcpTimerTest separately exercises actual scheduler firing and cancel
past a later sentinel. Native zero-window fault scenarios and advertised-window
conversion remain Phase 4 acceptance work.
