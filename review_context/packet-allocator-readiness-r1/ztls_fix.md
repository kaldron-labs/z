# Ztls Tx plaintext and packet-buffer separation

Status: implementation plan. No production changes or runtime verification have been performed.

## Required behavior

Replace Ztls's in-place application encryption with separate plaintext and ciphertext storage, following Zquic's separation of stream buffers from output packet buffers.

| Role | Allocator type | Allocation | Geometry and use |
| --- | --- | --- | --- |
| Application plaintext | `Zi::IOBufType::TxTLS` | `allocBuf(false)`; ordinary builtin/heap allocation | Holds a complete plaintext TLS record, up to the supported TLS plaintext limit. Never submitted to TCP or the NIC. |
| Ciphertext output | `Zi::IOBufType::TxTCP` | `allocBuf(true)`; registered backing when supplied by the plugin | Holds one packet's worth of TLS wire bytes, with underlying protocol headroom supplied by the allocator. |

For the non-plugin multiplexer, the TxTCP allocator supplies ordinary transport buffers with `skip == 0`. Ztls requests registered-capable output but does not claim that kernel TCP buffers are UMEM or hardware-registered. The provider determines backing and usable geometry. Registered packet capacity is constrained through the existing `IOBufPlugin::max` path described below.

The allocator initializes each packet buffer's `skip`. Ztls writes ciphertext at `data()`, preserves `skip`, and sets `length` to the number of TLS wire bytes written. It neither computes nor writes Ethernet/IP/TCP headers. Its writable extent is the allocator-advertised usable capacity after `skip`; protocol tail reservations must already be excluded from that extent by the allocator contract.

### MTU-capped layered allocator

IOBufPlugin is now registered-only: it exposes max, alloc(uint32_t size) and free(uint8_t *ptr, uint32_t size). Zxdp/Zutcp implement no unregistered branch. The registered selector remains on IOBufAllocator::allocBuf/alloc/free. IOBufPluginAllocator handles false using the same policy as IOBufBuiltinAllocator: inline IOBufBuiltin storage when the request fits, or Grow/VHeap backing in IOBufStub otherwise. Its external unregistered alloc/free go directly to VHeap, never to a plugin. Thus ordinary TxTLS allocation and Rx framing do not consume UMEM or need plugin-provided heap services.

Use the current stopped-state factory mx->allocator<Type, Buf, Size, Align, HeapID, MaxSize, Grow>(count). Its plugin call is allocator(type, size, align, count); MaxSize is relevant only to ZmVHeap, Grow only to nonregistered heap-buffer growth, and HeapID to Zi heap allocation. Neither MaxSize nor Grow is a plugin concern or a registered-buffer sizing input. Zi's Size/MaxSize are normalized with BuiltinSize<Buf, Align>. Provider objects for heap-only usage reserve no registered backing region, and no TxTLS plaintext allocation invokes their alloc/free methods.

Zutcp layers its `Zi::IOBufPlugin` implementation on Zxdp's allocator implementation. Zxdp divides the shared UMEM arena into packet-sized backing slots for the configured MTU and sets its provider's `max` to the MTU available to its upper layer. Zutcp derives its provider maximum by subtracting the additional IP/TCP overhead. Ethernet framing is accounted for by Zxdp; do not subtract it again when deriving the TCP payload budget from the Linux IP MTU.

The current `ZiIOBuf.hh` `IOBufPluginAllocator::allocBuf` already implements the required cap: with `registered == true`, the object capacity is `min(Size, plugin->max)` and a requested initial `length` above that capacity returns null before backing allocation. With `registered == false`, that cap does not apply and allocation uses Zi's normal builtin/VHeap policy to satisfy the requested size without calling the plugin. Consequently an application can request record-sized TxTLS plaintext and use the separate TxTCP allocator for MTU-limited output without teaching Ztls about Ethernet, IP, TCP or AF_XDP geometry.

Ztls reserves successive TxTCP buffers using their actual runtime capacity and allocator-initialized `skip`, not the requested template size. Preserve the cap on the returned IOBuf; a larger application request never enlarges a registered slot beyond the packet limit. The generic adapter currently constructs the object with `skip == 0`; the layered adapter must establish any nonzero skip consistently with `size` and its advertised writable extent. Avoid subtracting the same lower-layer header allowance both from the payload view and again from its capacity.

Each registered output backing contains one contiguous packet, including space for its adjacent lower-layer headers. There is no registered record-sized buffer, per-record header table, buffer-to-many-packets descriptor mapping, chunk splitting on Tx, or kernel/NIC gather requirement. Preserve independent allocator counts, intrusive pending providers, the shared-registration arena and overlaid free lists. Kernel-required chunk alignment/padding remains private Zxdp geometry; it does not increase the provider's advertised `max`.

One TLS record remains one TLS record on the wire even when it occupies several TxTCP buffers. Treat its output as a serial sequence of chunks. Start record protection once, encrypt successive chunks directly into their destination packet buffers, and finalize authentication once. Packet boundaries do not introduce TLS headers, new TLS record sequence numbers, independent nonces, or per-packet tags.

## Allocation failure and backpressure

The author-selected rule is to reserve **all** output packet buffers before proceeding with a record send. If any allocation fails, release every packet buffer already reserved for that attempt and fail the send with backpressure. Do not wait for another frame while holding a partial reservation, suspend a partially encrypted record, spin, grow registered backing, or fall back to a heap ciphertext staging buffer.

Admission proceeds in this order:

1. On TLS Tx, validate the connection generation and plaintext bounds. Determine the complete TLS wire size without mutating record-protection state. Include the record header, any explicit IV, encrypted content-type/padding bytes and authentication tag. Include any required preceding KeyUpdate output in the same application-send reservation.
2. Acquire TxTCP buffers and retain them in a temporary intrusive chain. Use each returned buffer's actual writable extent; do not assume every pool or connection has identical capacity. Continue until the aggregate capacity covers the complete output. Apply a bounded maximum derived from supported record size and the minimum accepted packet capacity.
3. If an allocation fails, release the chain and report that the plaintext send was not admitted. No ciphertext has been published, TLS sequence/key state has not advanced, and no TCP stream bytes have been assigned to this attempt. The plaintext remains available to the existing producer retry/backpressure path.
4. Once all reservations succeed, protect the record through serial encryption calls into those buffers. Complete protection before publishing any part of that record. Set each packet's final `length`, leaving its allocator-set `skip` unchanged.
5. Submit the completed buffers in order through the existing Zi Tx machinery. A blocked socket, full transport queue, congestion window or descriptor ring retains the already admitted ciphertext; it does not cause the application to encrypt and append the record again.

The send result must distinguish admission failure from an accepted send that is still pending. Trace the existing `TxStream`/`send` return and ownership behavior and make the retry contract explicit at those entry points. A failed reservation must not consume the caller's retryable plaintext accidentally through a moved reference. Preserve an owning reference until the admission result is known; queued off-thread submission uses the existing asynchronous completion/backpressure mechanism rather than pretending its initial dispatch return reports later allocation success.

Validate configuration so the packet pool can accommodate at least one maximum supported record, including required control-prefix output. Outstanding records can still cause transient exhaustion; the same all-or-nothing rule applies. Retry is driven by existing producer/transport availability notifications, including final packet-buffer returns, rather than a retry timer. Do not modify a queue while its owner is on another shard.

After encryption begins, cryptographic errors fail the connection instead of rolling back or reusing partially advanced cipher state. A transport failure after a prefix was sent follows ordinary connection teardown. These failures are distinct from the retryable, pre-encryption allocation failure above.

### Partial transmission and retransmission

TCP may retransmit a leading portion of a record using the original TCP byte positions and identical retained ciphertext. TCP suppresses duplicate bytes before TLS delivery. Ztls must never retry an accepted record by appending its prefix again at new TCP byte positions: TLS would interpret that prefix as continuation of the incomplete record, corrupting framing/authentication. TCP owns retransmission; it retains the original TxTCP ciphertext buffers. It must not ask Ztls to re-encrypt a partially transmitted record.

Reserving and encrypting the whole record before publication prevents packet-pool exhaustion from creating that partial-record failure. It does not make TCP transmission atomic or eliminate normal partial socket sends, retransmissions or connection failure.

## Serial record encryption

The current `ptls_tx_send()` and `ptls_tx_sendv()` write to one contiguous `ptls_buffer_t`. The latter vectors its **input**, not its output. Repeated calls to either function would create separate TLS records and are not the requested chunk model.

Add a chunked record-output path in zpicotls. Its interface is a serial writable-span sink: obtain the next destination span, write within that span, commit its byte count, and advance to the next. It does not accept an AF_XDP descriptor vector or expose TCP/NIC concepts. Ztls implements this sink over its already reserved intrusive packet chain. Record sizing/admission must happen before record sequence or key-update state is consumed.

Keep TLS formatting and record-protection state inside zpicotls. The record writer emits the clear TLS header and explicit IV, if applicable, then encrypts plaintext in successive destination-sized chunks. It includes the TLS 1.3 inner content type/padding in the same protected record and writes the final tag across remaining spans as necessary. Header, IV and tag bytes may cross chunk boundaries; use a bounded cursor and fixed-size cryptographic scratch where required. Never collect a whole ciphertext record in temporary storage before copying it into packet buffers.

For each record, maintain one nonce/sequence, one AAD interpretation, a running cipher position and running authentication state. Advance the TLS record sequence once on successful record completion. Perform KeyUpdate between records, never between chunks; preserve the existing key-update threshold and ordering. Keep TLS 1.2 explicit-IV and authentication rules supported by current Ztls.

OpenSSL's existing AEAD init/update/final implementation provides the underlying serial model. Adapt it with checked output bounds and correct handling of any backend buffering. Do not assume an update always produces exactly its input length without establishing that for the selected algorithm.

Fusion's existing `aead_do_encrypt_init/update/final` functions are assertion stubs, and some optimized contexts expose no incremental functions. Implement genuine chunked encryption for the Fusion variants selected by Ztls, retaining counter/GHASH state and partial-block carry across calls. Do not invoke the stubs, restart one-shot encryption per chunk, change the negotiated cipher implicitly, or route Fusion sessions through OpenSSL as an undisclosed fallback. Fixed-size block/tag scratch is acceptable; record-sized plaintext or ciphertext gathering is not.

The chunk-output implementation must support actual allocator-provided destination addresses and lengths. Handle SIMD/block alignment inside the backend, using bounded block carry where necessary. Do not reset or advance allocator `skip` to obtain cipher alignment or consume lower-layer headroom. Validate any required base alignment through the allocator's existing alignment specification before admitting a record.

## Ztls integration

Keep the existing `txAllocator` configuration and `TxAllocator` type for TxTLS plaintext. Add a distinct `txPacketAllocator` configuration/accessor for TxTCP output. Both are explicit, immutable allocator selections entered in the multiplexer allocator registry while it is stopped. Registry membership is distinct from Registered packet backing; the TxTLS selection still allocates ordinary builtin/heap storage. Validate their types and common multiplexer. Forward their independent count requests through the existing allocator factory; plaintext requests must not reserve UMEM payload storage in a plugin.

Update `TxBufAlloc` and the application's Tx stream to describe plaintext capacity, rather than space for in-place TLS ciphertext. Remove TLS-output header/tag reservations from that plaintext path; preserve headroom genuinely needed by dependent application framing. Keep the TLS plaintext limit independent of the packet allocator's capacity. Plaintext allocations use `registered == false`, so Zi supplies normal builtin/heap storage and handles OOM through `ZmHeapFail`; no registered-only plugin method is called and packet-pool exhaustion cannot affect plaintext allocation.

Replace `send_`'s in-place `ptls_buffer_init_tx`/`ptls_tx_send` path with reservation followed by the serial record writer. Remove the assertion that TLS output starts at `data_()` and the finalization code that assigns `skip = 0`. A packet's initialized payload origin is stable through encryption, send and retransmission.

Use the existing intrusive Tx queue nodes for reserved/output buffers. Add one Tx-owned pending-record operation to carry plaintext admission, ordered output and any record-level completion association. Packet completions must not masquerade as separate application-record completions. Preserve the public `sent`/error semantics once per original submission, and preserve physical-connection/generation binding during replacement. Existing atomic IOBuf references govern backing lifetime; operation progress counters do not become an independent backing-lifetime mechanism.

Retain plaintext until encryption finishes, and longer only where the existing application completion contract requires it. TCP retains ciphertext for sending/recovery; it does not retain plaintext merely to regenerate encrypted packets. Account for plaintext submission and ciphertext transport bytes at their existing distinct telemetry boundaries. Count the complete record once at the record boundary and packet bytes once at the wire boundary, without counting retries as new records.

Cover every TLS output producer: application records, handshake output, alerts, close-notify and KeyUpdate. Where handshake processing currently owns mutable TLS state on Rx, preserve that ownership and pass completed packet references to Tx through the existing dispatch boundary. Use the same serial record writer and TxTCP allocation contract; formatting handshake plaintext in ordinary heap storage is permitted. Remove registered, record-sized handshake/control output allocations. If a handshake/control producer has already advanced non-rollbackable TLS state when reservation fails, use its existing handshake/session failure path; do not replay it speculatively on the live connection.

Rx framing/decryption continues to accept TLS records split across TCP packets. No Rx cryptographic redesign is required. Packet boundaries are invisible to peer TLS.

## Implementation sequence and file inventory

The zpicotls dependency is a separate checkout at `../zpicotls`. Implement and verify its changes first, then rebuild/install it through its documented dependency workflow before enabling the new Ztls path. This plan authorizes no production edits by itself; the present deliverable is this document.

1. **Implement serial output in the dependency.**
   - `../zpicotls/include/zpicotls.h`: define the serial span-sink, record-output sizing/admission boundary and chunk-capable protection contract; retain existing contiguous APIs for their existing callers.
   - `../zpicotls/lib/picotls.c`: share record framing, sequence/key-update rules and control-record construction with the chunk writer. Size application output without changing cipher state, then emit directly to reserved spans.
   - `../zpicotls/lib/openssl.c`: provide bounded serial encryption through the existing EVP state, including supported TLS 1.2/1.3 AEADs.
   - `../zpicotls/lib/fusion.c` and `../zpicotls/include/zpicotls/fusion.h`: implement the corresponding Fusion state/carry operations and required declarations for all Ztls-selected Fusion variants.
   - `../zpicotls/t/vector-tx.c`, `../zpicotls/t/openssl.c`, `../zpicotls/t/fusion.c`: compare chunked output against contiguous output and test backend state, boundary and failure behavior. Extend the existing test targets; no new target is required.
2. **Add allocator roles and migrate record transmission.**
   - `ztls/src/Ztls.hh`: split allocator configuration/validation using the current mx factory argument order, switch plaintext to Zi's existing unregistered builtin/VHeap path without plugin calls, add all-or-nothing packet reservation, serial output, ordered publication and record-level completion handling. Migrate handshake/control producers and preserve shard/generation teardown rules.
   - `ztls/src/ZtlsBackend.hh`: declare the Ztls-side adapter to the dependency's serial record-output API where backend-facing declarations belong. Keep packet/allocator ownership in Ztls, not the crypto dependency.
   - `configure.ac`: add a compile/link feature check for the selected chunk-output API; the existing `ptls_tx_sendv` check alone is insufficient.
   - `docs/DEPENDENCIES.md`: document the required rebuilt zpicotls capability.
3. **Update existing dependents in the same change.**
   - `ztls/example/ztlsclient.cc`, `ztls/example/ztlsserver.cc`: construct and register separate TxTLS and TxTCP allocators before start, with independent geometry/counts.
   - `ztls/test/ZtlsBufHookTest.cc`, `ztls/test/ZtlsAsyncTest.cc`: update allocator configuration and assertions that currently expect registered whole-record output or an unchanged plaintext/output origin.
   - `zhttp/src/ZhttpConfig.hh`, `zhttp/src/ZhttpNative.hh`, `zhttp/src/ZhttpH2Hub.hh`: carry the packet allocator through TLS configuration and native/H2 factory paths while preserving application plaintext framing. TCP and H3 continue using their respective allocator roles.
   - `zhttp/test/ZhttpH2HubTest.cc`, `zhttp/test/ZhttpTransportContractTest.cc`: update the affected TLS factory fixtures and assert both allocator roles. Reconcile other callers discovered by a repository-wide `txAllocator`/Ztls factory search before compiling; do not insert compatibility forwarding shims.
4. **Verify independently of Zutcp.**
   - Create `ztls/test/ZtlsTxPacketTest.cc`: isolated fake packet-provider/sink tests for reservation, headroom preservation, lifetime and completion behavior.
   - `ztls/test/Makefile.am`: register that TAP self-test.
   - Create `ztls/itest/ztlstxpackettest.cc`: working TLS client/server fixture over kernel TCP using deliberately small output buffers and nonzero-headroom fake-provider coverage where the fixture supports it.
   - `ztls/itest/Makefile.am`, `ztls/itest/README.md`: build/run and document the working-pair coverage.
   - `ztls/README.md`: document plaintext/packet allocator roles, counts, backpressure and completion semantics.
5. **Reconcile the Zutcp plan after this migration is adopted.**
   - `design_draft.md`: replace the Ztls whole-record registered ciphertext/header-table dependency with packet-sized registered TxTCP ciphertext for this path. Update the earlier exclusion of Ztls edits to identify this separately implemented prerequisite. Adopt MTU-sized registered buffers and layered IOBufPlugin::max limits throughout; remove the large registered-buffer/header-table/gather machinery superseded by this decision. No additional dependent requiring it has been selected.
   - `review_context/af-xdp-sg-verification.md`: record that the old TLS-specific multi-buffer requirement has been removed by this prerequisite, while retaining the verified driver limitations.
   - `implementation_readiness_report.md`, `goldfish_review_report.md`, `review_context/manifest.json`: record the changed prerequisite and affected claims without rewriting historical review snapshots as if they reviewed this migration.

## Verification and completion gates

Use the existing zpicotls test workflow, then configure Z through `z.config` and build dependencies from the top level with `make -j8`. Build each affected test directory before its `make test` target. Perform a clean rebuild when changing release/sanitizer configuration, per `docs/BUILD.md`.

- Compare concatenated ciphertext chunks byte-for-byte against contiguous encryption from identical initial state for TLS 1.2/1.3, AES-GCM and ChaCha20-Poly1305 where supported; verify decryption and authentication with the existing peer.
- Exercise output boundaries through the TLS header, explicit IV, cipher blocks, inner content type and tag. Include tiny final chunks, packet-sized records, a full 16 KiB plaintext record, consecutive records and KeyUpdate. Exercise selected Fusion acceleration variants and OpenSSL paths, including permitted unaligned destinations.
- Fail allocation at every reservation position. Verify release of all prior reservations, unchanged plaintext, no published ciphertext, unchanged record sequence/key state, once-only failure notification and a successful later retry that decrypts correctly.
- Verify `skip == 0` kernel output and nonzero allocator headroom with sentinel bytes; TLS must never overwrite the reserved prefix or provider tail reservation. Assert distinct plaintext/output backing and no full-record ciphertext allocation/copy.
- Apply partial socket sends and transport backpressure after admission. Verify retained output resumes in order without repeated encryption, duplicate application completion or duplicated TLS stream bytes. Simulate retransmission at unchanged TCP sequence positions in transport tests.
- Close, replace and restart connections with pending reservations/output. Verify old-generation work is rejected, every reference retires through the correct allocator, cipher state stays on its owner and teardown remains asynchronous.
- Run affected Ztls/Zhttp suites, sanitizer/leak checks and a kernel TCP working pair before making this a Zutcp prerequisite. Test oversized registered requests clamped to each layered max, oversize initial-length rejection, unchanged unregistered record capacity, and exact max/skip accounting. Later verify the packet geometry over veth and physical mlx5 without `XDP_USE_SG` for the single-buffer path; actual zero-copy selection remains a kernel/driver decision and must be reported, not inferred.

The migration is complete when application plaintext remains on Zi's normal unregistered builtin/heap path, all TLS wire output is produced directly into separately allocated packet buffers, allocation failure is atomic before protection/publication, and a peer receives the same TLS byte stream without requiring NIC scatter/gather for this path.
