# Combined Zquic Tx and ZiRx execution

Both production repairs were installed before dependent tests began. Execution
uses the existing Clang debug configuration (`CXX=clang++`, `-g -DZDEBUG`) and
reuses incremental builds. All concurrent make invocations reserve an implicit
slot from the same eight-token GNU make FIFO jobserver; recursive makes inherit
that jobserver. No sanitizer, GCC, cross-platform or release reconfiguration is
part of this run.

Detailed logs and command drivers are retained under
`/home/ubuntu/zquic-zirx-execution`. The combined repairs and supported dependent gates passed. The subsequent
PTO/Handshake follow-up and its current-image verification are recorded in
[zquic-pto-repair.md](zquic-pto-repair.md).

## Implementation and evidence map

| Requirement | Implementation / verification |
| --- | --- |
| Intrinsic Tx-only node and allocator factory | `ZquicBuf.hh`: `TxPkt : ZiTxBuf`; `ZquicBufferTest` verifies distinct Rx/plaintext roles, factory policy and actual packet protection with builtin and registered backing, 17-byte headroom and 11-byte tailroom. |
| Common queue, bound and priority | `EndpointCxn` inherits `ZiTx`; owner admission rejects total count 4096 before submission accounting. Public list iterator inserts priority immediately after active head. `ZquicSockTest` covers held head, repeated priorities, per-head metadata and capacity/refcounts. |
| Physical ownership and teardown | Posted submissions capture raw physical identity and generation, without retaining the old connection, and reject stale submissions on Tx. Disconnect prevents admission; physical notification posts Tx context retirement before queue cleanup. Synthetic fixture and real endpoint integration cover backing holds, replacement queues, close/rebind/restart. |
| Recovery and protocol separation | Existing protector rejection paths discard direct rejected records; posted local drops retain records/plaintext for existing PTO/loss. Retry requests priority. Socket completion only records local output; no producer wake, ACK or recovery retirement is added. Full QUIC runtime and QIR gates verify working protocol pairs. |
| Scheduler servicing | `zquictxqueuetest` forces native EAGAIN, fills all 4096 slots, then profiles the unchanged common send loop using real syscalls. A scheduled task and second endpoint make progress each round. No per-packet continuation remains. |
| Receive/context separation | `ZiRx::recv` / `recvSync` use connection network allocator for every transport slot. A single closure-local reference retains App accumulation; `recvMem` / `recvMemSync` preserve the memory-only interface. |
| Parser/lifecycle contracts | `ZiRxTest` connection matrix covers builtin/stub, async/sync, TCP/UDP, exact original backing, every split, coalescing/partial tail, larger-than-packet messages, unknown/max/malformed headers, partial consumption, drop/error, complete/disconnect/rearm, allocation rejection and off-owner release. |
| Dependent allocator roles and forwarding | Repository search finds only Zdb and three Zi examples as connection framing users; each already separates network and App allocators. Zdb normal and five-byte fragmented three-host replication verifies App accumulation, schema/table/shard dispatch, original RxFrame forwarding and retained backing through stop. |

## Backend UDP contract characterization

The migrated fixture injects a positive short UDP result and observes the same
packet at a nonzero offset. A separate native-writer characterization in `zquictxqueuetest` injects a
1-of-4-byte sendmsg result, then observes the unmodified common writer retry
`pointer+1` with length 3. The real receiver gets that 3-byte suffix while
local diagnostics count one completed 4-byte datagram.
`zquic-itest-short-evidence.log` records an injected backend contract violation.
UDP backends must report an entire datagram or an error; a positive short result
is a backend defect. Native UDP already provides that all-or-error contract.
No ZiTx repair is needed for a backend violating it. See `zquic-zitx-contracts.md` and its source fingerprints. Native EAGAIN
preserves the protected packet and metadata until readiness retry.

The current source has synchronous native Tx and no saved TxContext registry.
Backend-retained backing is held by the backend through physical retirement;
application queue completion alone cannot establish kernel retirement.

## Final review corrections

The nonblocking rebind fixture observed a stale posted send drop and verified no
packet entered the replacement queue. Repeated public disconnect calls also
reproduced three physical close requests where two were expected (one old
connection after rebind and one current connection). Reusing the existing close
flag as a test-and-set now submits physical close once; both caller callbacks
and the endpoint-down notification still complete once. Advisory queue telemetry
also returns zero immediately for a sampled zero count, preventing an underflow
when count and context are sampled across Tx progress. See the independent
`zquic-zirx-review.md` report and its reproduction/final logs.

The TCP example truncation was a bug in `zi/example/zimxserver.cc`, not in
ZiTxStream. The example disconnected after each output buffer; its completion
handler now waits for the whole response. Tracing verifies 32/32 responses,
including six bodies exceeding 32768 bytes. See `zirx-execution.md`.

## Scheduler profile

Four full queues complete 16384 native datagrams. The Tx-migration measured sample
records `maxTurnUS=14815`, `maxThreadCPUUS=14814` and
`maxScheduledDelayUS=14835`; all 16384 datagrams reached the primary receiver in
that sample. Another endpoint and a scheduled task progressed after each drain.
Earlier samples observed a 22668 us wall / CPU turn and a 15526 us wall /
15525 us CPU turn, and under
concurrent compilation a 39601 us wall turn and
39606 us scheduled-task delay, with permitted kernel UDP receive drops. The
profile proves bounded progress and quantifies the servicing change; it does
not claim the previous per-packet scheduling latency. Host perf events are denied
(`perf_event_paranoid=4`); the test uses monotonic wall and thread CPU clocks,
with `/usr/bin/time` resource evidence. No production scheduler change was added.

## ACK credit servicing regression

The first complete external run passed 22 supported core cases (the cached
MsQuic image does not implement HTTP/3 in either role) but failed both server
rebinding cases against ngtcp2. A matched pre-task image, verified to contain
the old private pump and identical protocol/crypto/Zi headers, passed all six
ngtcp2 loss/rebinding cells. Qlog and decrypted pcaps established that missing
STREAM ranges were retained and correctly identified as lost, but retransmission
waited more than ten seconds behind fresh data. The eventual datagram targeted
an obsolete NAT tuple.

ACK completion synchronously flushed ordinary producers before its deferred
retransmission work, and did not resume earlier pending losses when a later ACK
opened credit. The narrow correction uses the existing bounded retransmission
service before normal output when credit opens. It changes no retained-record,
loss detection, congestion calculation, encryption or heartbeat behavior.
Two owner-scheduled regressions fail before and pass after the correction,
including the genuinely deferred no-credit attempt. See
`zquic-qir-rebind-gap.md` for source and packet evidence. Final consumer and QIR verification of this correction passed; the later
PTO/Handshake follow-up also passes its supported current-image gates.

## Verification status

Both repairs were in place before dependent verification began, using REBUILD.md.
Zi owner/integration/example, Ztcp/Ztls unit, Ztls incremental integration,
Zdb unit/normal and fragmented replication, PostgreSQL and SQLite persisted
replication, Zfix, Zdbus, Zdf, Zrl and Zgtk gates passed. The ZiRx report retains
those logs and counts; their production dependencies remain unchanged.

The current PTO/Handshake follow-up passes QUIC 21 files / 205 tests, endpoint
integration four cases, HTTP 27 files / 278 unit tests, eight files / 91 integration
tests, and local interoperability. All affected downstream modules through Zdash
were rebuilt and passed. Final QIR has 28 supported successes and two evidenced
cached-peer unsupported HTTP/3 cells; one Docker-fixture interruption and its
single unchanged-image replacement are preserved. See the follow-up report for
source/image fingerprints, exact module counts, memory-check provider limits,
Initial-key ownership scope and cleanup evidence. Pre-confirmation NAT recovery
with an external client remains outside any guarantee made by these results.
