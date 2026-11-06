# zquic source audit inventory

Status: phase 2 inventory and selected repairs after coordinator notification that phase 1 source integration completed. This supports the coordinator's integrated audit; it does not independently certify phase 2 completion. No configure, regeneration, compilation, compiler-driven analysis, tests, reproductions, or image builds were run.

Scope: all repository-owned `.cc`/`.hh` files in `zquic/src`, `test`, `itest`, and `example`. `util`, `bench`, and `interop` contain no C++ files in the inspected snapshot. The new RTT regression is included when present. Each file was inventoried and its declarations/test registration and audit-sensitive expressions inspected through source searches. Selected owner/dependency bodies were read directly. This is a focused source audit, not an exhaustive proof of every line's correctness; recovery integration remains assigned to the coordinator.

## Demonstrated prerequisite incompatibilities

| Requirement / invariant | Owner and source evidence | Disposition |
| --- | --- | --- |
| Referenced APIs must exist | `Zi::RxRange` uses in `ZquicCrypto.hh` (sent/TxRange), `ZquicCrypto.cc`, `Zquic.hh`, `ZquicCliLink.hh`, `ZquicSrvLink.hh`, `ZquicStream.hh` and tests; no declaration in current `zi/src` | Existing I/O migration prerequisite. Preserve unrelated work; require reconciliation before phase 3 build. Not a newly observed compiler result. |
| Backend selector must exist | `Zquic_.hh` endpoint open and `itest/zquictxqueuetest.cc` refer to `ZiMxBackend::URing`; current `ZiMultiplex.hh` has no selector | Existing migration prerequisite; defer unrelated API design. |
| Tx completion API must match | `Zquic_.hh` send callback uses `io.flags & ZiTxContext::TxDataDone`; `ZiIOContext.hh` Tx type exposes `state`, `complete()`, and no such flags/constant | Existing migration prerequisite; do not silently alter endpoint completion semantics. |
| Virtual overrides must have base members | `ZquicPQueue.hh` `StreamRxData::releaseView_()` and `memory()` declared override; current `ZiIOBuf` supplies neither; its `memory()` body also uses absent `storageMemory_()` and `packet->memory()` | Existing queue/buffer migration prerequisite. Runtime packet-slice ownership must be checked when reconciling, rather than merely deleting override markers. |

## Additional findings and category dispositions

| Category | Evidence / invariant | Disposition |
| --- | --- | --- |
| Exposed layout | `ZquicDiag.hh`: `Diag` counters follow methods; `LinkDiag` rx/tx follow constructors. `ZquicOpenSSL.hh`: `OpenSSLCipherCtx::ctx` follows methods | Repaired in phase 2: exposed fields moved before methods in Diag, LinkDiag, OpenSSLCipherCtx. Member order, initializers, and behavior preserved. |
| Allocation and storage | `ZquicBuf.hh` role aliases use existing Zi allocators with named heaps; `ZquicPQueue.hh` nodes retain packet slices and use framework queues; example links inherit concrete `ZmNHeap` | No demonstrated extra allocation repair from these inspected owners. Buffer allocator migration remains a prerequisite above. |
| Intrusion / lifetime | Stream queue declaration and retained packet field inspected; `StreamRxData` retains original packet while view is queued | Preserve retained backing; source mismatch prevents claiming complete queue correctness. |
| Copies | `ZquicCrypto.cc` decrypt template uses scratch only for `PreserveCiphertext`; ordinary branch decrypts directly in packet. HKDF, header masks and wire serialization copies are bounded protocol operations | No speculative copy removal. `ZquicStream.hh` queueRxSlices creates views, not payload copies. |
| Control / liveness | `ZquicInteropTest.hh::waitUntil` and `ZquicRuntimeTest.cc::waitUntil` poll with sleeps; unused `ZquicAPITest.cc::waitUntil` removed in phase 2 | Demonstrated existing audit-policy violations. Runtime/external-process polling remains an unresolved demonstrated liveness defect and blocks phase 3 under the plan. An event-driven repair spans fixture notifications. Timed semaphore waits are event waits and are not treated as sleep polling. |
| Framework / logging | Example `ZiLOG` error lambdas capture error by value. Qlog writer operates on its dedicated thread; event schema conversions live in logger source | No demonstrated new logging repair; integrated recovery captures still need coordinator review. |
| Casting | `ZquicStream.hh::queueRxSlices_` casts a char-equivalent payload pointer to uint8_t; syscall casts in `ZquicSock.cc` reflect platform interfaces | Payload cast is an audit simplification candidate, not a reproduced failure. Avoid unrelated churn. |
| Diagnostic atomics | `ZquicDiag.hh` rx datagrams/bytes and tx packets/bytes are `ZmAtomic` while other counters use DiagCounter | Requirement for stable cross-thread observation has not been disproved. Unproven concern: trace consumers before any removal. |
| Cleanup | `OpenSSLCipherCtx` releases EVP context in destructor; logger singleton allocation is exempt from ordinary heap tagging; examples use owning app shutdown | No demonstrated new cleanup repair from inspected bodies. |
| Bounds / protocol loops | Packet number/varint loops have protocol widths; frame/transport parsing advances through received input; scheduler packet budget bounds payload selection | No demonstrated need for a new timer, lock, GC scan, or container. Integrated recovery cursor bounds remain coordinator scope. |

## Per-file inspection index

The evidence column records actual source declarations/registration lines selected during this pass. All files also participated in searches for allocation, intrusive containers, copies, casts, logging, polling, atomics, member access, and iteration. A clean search is not a correctness proof.

| File | Inspection evidence |
| --- | --- |
| `zquic/example/zquicclient.cc` | 29: `struct App : public Zquic::Client<App> {`; 64: `friend inline ZuStringT<"Zquic.Example.CliLink"> ZmNHeapID(Link *);` |
| `zquic/example/zquicserver.cc` | 19: `struct AppLink;`; 70: `friend inline ZuStringT<"Zquic.Example.SrvLink"> ZmNHeapID(AppLink *);` |
| `zquic/itest/zquictxqueuetest.cc` | 13: `struct Endpoint : public Zquic_::Endpoint_<Endpoint> {`; 105: `ZuTestCall(testDrain);` |
| `zquic/src/Zquic.cc` | Include/implementation body and audit-expression searches (440 lines) |
| `zquic/src/Zquic.hh` | 81: `struct InitialInfo {`; 1376: `template <typename L>` |
| `zquic/src/ZquicBuf.hh` | 26: `struct BufNode : public ZiIOBuf {`; 78: `struct TxRange {` |
| `zquic/src/ZquicCliLink.hh` | 15: `template <`; 584: `template <typename L>` |
| `zquic/src/ZquicCrypto.cc` | 166: `struct stat st;`; 799: `template <bool PreserveCiphertext>` |
| `zquic/src/ZquicCrypto.hh` | 27: `struct PKey;`; 304: `class ZquicAPI Crypto {` |
| `zquic/src/ZquicDiag.cc` | Include/implementation body and audit-expression searches (83 lines) |
| `zquic/src/ZquicDiag.hh` | 24: `struct DiagCounter {`; 268: `struct LinkDiag {` |
| `zquic/src/ZquicFrame.cc` | Include/implementation body and audit-expression searches (491 lines) |
| `zquic/src/ZquicFrame.hh` | 18: `struct AckRange {`; 73: `struct ZquicAPI FrameCodec {` |
| `zquic/src/ZquicLib.cc` | Include/implementation body and audit-expression searches (29 lines) |
| `zquic/src/ZquicLib.hh` | Include/implementation body and audit-expression searches (42 lines) |
| `zquic/src/ZquicLink.cc` | Include/implementation body and audit-expression searches (13 lines) |
| `zquic/src/ZquicLink.hh` | 26: `struct LinkCloseState {`; 8612: `template <typename EmitTLS, typename HandleControl>` |
| `zquic/src/ZquicLog.cc` | 65: `template <`; 2087: `template <template <typename> class Filter, typename S>` |
| `zquic/src/ZquicLog.hh` | 36: `struct Diag {`; 1186: `template <typename L>` |
| `zquic/src/ZquicOpenSSL.hh` | 19: `struct OpenSSLCipherCtx {` |
| `zquic/src/ZquicPQueue.hh` | 21: `struct CryptoRxData : public ZiIOBuf {`; 562: `template <typename Spans, typename Alloc, typename Enqueue>` |
| `zquic/src/ZquicPacket.cc` | Include/implementation body and audit-expression searches (437 lines) |
| `zquic/src/ZquicPacket.hh` | 35: `class PktWriter {`; 247: `class PktBuild {` |
| `zquic/src/ZquicPath.hh` | 17: `struct PathHint {`; 44: `class Path {` |
| `zquic/src/ZquicRecovery.hh` | 60: `class AckTracker {`; 1958: `class PTOBackoff {` |
| `zquic/src/ZquicSched.hh` | 26: `struct PktBudget {`; 431: `class TxScheduler {` |
| `zquic/src/ZquicSock.cc` | Include/implementation body and audit-expression searches (243 lines) |
| `zquic/src/ZquicSock.hh` | 17: `struct Datagram {`; 109: `struct ZquicAPI Sock {` |
| `zquic/src/ZquicSrvLink.hh` | 15: `template <`; 53: `template <typename L>` |
| `zquic/src/ZquicStream.hh` | 15: `class StreamData : public ZmPolymorph {`; 1037: `template <typename Stream_>` |
| `zquic/src/ZquicStreamUtil.hh` | 15: `struct StreamID {`; 227: `class StreamTxState {` |
| `zquic/src/ZquicTransport.cc` | Include/implementation body and audit-expression searches (267 lines) |
| `zquic/src/ZquicTransport.hh` | 17: `struct ZquicAPI TransportParams {` |
| `zquic/src/ZquicTypes.cc` | Include/implementation body and audit-expression searches (45 lines) |
| `zquic/src/ZquicTypes.hh` | 25: `template <typename T, unsigned N>`; 394: `struct EcnMark {` |
| `zquic/src/Zquic_.hh` | 17: `template <typename Link_>`; 717: `template <typename ImplRef_>` |
| `zquic/test/ZquicAPITest.cc` | 85: `struct TestLink;`; 2198: `ZuTestCall(testInitValidate);` |
| `zquic/test/ZquicBufferTest.cc` | 195: `ZuTestCall(testPacketAllocRoles);`; 197: `ZuTestCall(testPktBuildMetadata);` |
| `zquic/test/ZquicCIDTest.cc` | 18: `struct TestRouteLink { };`; 353: `ZuTestCall(testServerInitialBootstrap);` |
| `zquic/test/ZquicCodecTest.cc` | 162: `struct Case {`; 941: `ZuTestCall(testAddressTokenCodec);` |
| `zquic/test/ZquicCongestionTest.cc` | 269: `ZuTestCall(testSentPktTracker);`; 273: `ZuTestCall(testPTOBackoff);` |
| `zquic/test/ZquicCryptoTest.cc` | 299: `ZuTestCall(testCryptoPosture);`; 305: `ZuTestCall(testCryptoOwnedRanges);` |
| `zquic/test/ZquicFlowTest.cc` | 14: `struct FlowApp : public Zquic::Hub<FlowApp> {`; 343: `ZuTestCall(testPacing);` |
| `zquic/test/ZquicHandshakeTest.cc` | 60: `struct TempDir {`; 480: `ZuTestCall(testZeroRTTPktDrop);` |
| `zquic/test/ZquicInteropTest.hh` | 56: `template <typename L>`; 317: `struct CaddyProcess {` |
| `zquic/test/ZquicLogTest.cc` | 1942: `ZuTestCall(testQLogRestartDrain);`; 1954: `ZuTestCall(testQLogTypedCloseEvents);` |
| `zquic/test/ZquicLoopTest.cc` | 23: `struct App : public Zquic::Hub<App> {`; 590: `ZuTestCall(testDatagramPayloadHeadroom, Zm::CacheLineSize);` |
| `zquic/test/ZquicPMTUDTest.cc` | 214: `ZuTestCall(testPMTUD);`; 215: `ZuTestCall(testPathECN);` |
| `zquic/test/ZquicPQueueTest.cc` | 520: `ZuTestCall(testCryptoRxClipping);`; 535: `ZuTestCall(testTxUnackdRandomized);` |
| `zquic/test/ZquicPacketProtectionTest.cc` | 38: `template <unsigned N>`; 492: `ZuTestCall(testTrafficSecretProt);` |
| `zquic/test/ZquicRecoveryTest.cc` | 959: `ZuTestCall(testRecovery);`; 973: `ZuTestCall(testLosslessFrameUpdates);` |
| `zquic/test/ZquicRttLossTest.cc` | 94: `ZuTestCall(testFallingRTT);`; 95: `ZuTestCall(testOtherRTT);` |
| `zquic/test/ZquicRuntimeTest.cc` | 36: `template <typename Link>`; 1882: `ZuTestCall(testIdleTimeoutQLog);` |
| `zquic/test/ZquicSockTest.cc` | 17: `struct TestEndpoint : public Zquic_::Endpoint_<TestEndpoint> {`; 165: `ZuTestCall(testEndpointRebind);` |
| `zquic/test/ZquicStreamStateTest.cc` | 54: `ZuTestCall(testStreamUtilities);` |
| `zquic/test/ZquicStreamTest.cc` | 40: `struct App : public Zquic::Hub<App> {`; 3841: `ZuTestCall(testPeerKeyUpdateState);` |
| `zquic/test/ZquicTestPorts.hh` | Include/implementation body and audit-expression searches (20 lines) |
| `zquic/test/ZquicTimerTest.cc` | 31: `struct TimerApp : public Zquic::Hub<TimerApp> {`; 246: `ZuTestCall(testTimerDisconnect);` |
| `zquic/test/ZquicVersionTest.cc` | 211: `ZuTestCall(testVersionNegotiation);`; 214: `ZuTestCall(testRetryParamValidate);` |

## Polling caller trace

- `ZquicAPITest.cc::waitUntil` had no local call sites (the only source match was its declaration). Removed the unused template in phase 2.
- `ZquicInteropTest.hh::waitUntil` is imported by `zhttp/test/ZhttpTestUtil.hh:27`; preserve it until dependent fixtures are migrated.
- `ZquicRuntimeTest.cc::waitUntil` has roughly forty uses spanning server listening, accepted links, readiness, spare CIDs, promotion, stream delivery and closure. Replacing it requires notifications at each relevant owning-thread event; posting a generic semaphore without these producer notifications would deadlock or merely disguise polling.
- `ZquicInteropTest.hh::CaddyProcess::stop` polls `waitpid(..., WNOHANG)` with sleeps, and `waitCaddyReady` polls curl. These require external-process readiness/termination handling rather than a protocol recovery change.

These are demonstrated existing policy/liveness issues, not conjectured recovery failures. If left unresolved, record them in the phase 2 exit record instead of claiming all audit flags repaired.


## Phase 2 source-repair handoff

Changed only `ZquicDiag.hh`, `ZquicOpenSSL.hh`, and `ZquicAPITest.cc`: moved exposed members ahead of behavior without changing their relative declaration order or initialization, and removed an uncalled polling helper. Reviewed the resulting source diff. No build or test was run.

Unresolved demonstrated language-policy issue: `ZquicRuntimeTest.cc:30` opens an anonymous namespace, contrary to `docs/CPP.md`. It also contains default reference captures and constexpr span declarations. Removing only the namespace braces does not establish the required internal linkage of its numerous helpers; retain as an explicit policy issue requiring a separate scoped source repair, rather than claiming this audit passed.

The prerequisite API incompatibilities and remaining fixture polling listed above are unresolved. They must appear in the coordinator's exit record; phase 3 cannot be declared ready on this evidence.
