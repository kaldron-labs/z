# Ztls Tx plaintext and packet-buffer separation

Status: implementation contracts finalized, confirmatory readiness review pending. This document specifies future production changes and verification.

## Required behavior

Replace Ztls's in-place application encryption with separate plaintext and ciphertext storage, following Zquic's separation of stream buffers from output packet buffers.

| Role | Allocator type | Allocation | Geometry and use |
| --- | --- | --- | --- |
| Application plaintext | `Zi::IOBufType::TxTLS` | `allocBuf(owner, length)`; ordinary builtin/heap allocation | Holds a complete plaintext TLS record, up to the supported TLS plaintext limit. Never submitted to TCP or the NIC. |
| Ciphertext output | `Zi::IOBufType::TxTCP` | `allocBuf(owner, length, cxn->info().mps)`; registered backing when supplied by the plugin | Holds one packet's worth of TLS wire bytes, with underlying protocol headroom supplied by the allocator. |

For kernel TCP, the TxTCP allocator supplies builtin/VHeap buffers with zero lower-layer headroom and tailroom. The factory selects plugin storage for TxTCP when a plugin is installed. TxTLS and RxTLS always select builtin/VHeap storage.

### Fixed provider geometry

Use the stopped-state factory `mx->allocator<Type, Buf, Size, Align, HeapID, VHMaxSize, Grow>(count)`. The mux invokes `plugin->allocator(type, size, align, count)` only for RxUDP, TxUDP, RxTCP and TxTCP. VHMaxSize and Grow govern heap policy. Validate allocator roles and common mux association through public `type` and `mx` fields.

The registered provider publishes fixed `size`, `headRoom` and `tailRoom` before Zi constructs the adapter. Zi retains the provider by reference and uses `alloc()` and `free(ptr)` for its slots. The Tx adapter sets `skip = headRoom` and rejects a requested initial length greater than `capacity()`, which subtracts both framing reservations from the overall size.

Reserve each ciphertext packet through `txPacketAllocator()->allocBuf(owner, 0, cxn->info().mps)`. Snapshot mps from the originating physical connection before reserving the record and use that value for all its packet allocations. The third mps parameter defaults to UINT32_MAX for callers that omit a payload cap. TxTLS plaintext and App/RxTLS framing use the default because they hold complete records.

ZiCxnInfo::mps is the connection maximum payload size. TCP uses the effective send MSS and UDP uses its IP MTU minus IP/UDP overhead. Zutcp/Zxdp must set it before publishing connected/accepted callbacks that permit sends. Kernel Zi already reads TCP MSS during executedConnect and obtains the connected UDP payload limit when creating its connection information. A zero value means unknown, including unconnected UDP. Establishment must supply a positive value before packet output. A zero mps at reservation time fails the send and disconnects that generation, avoiding a zero-capacity reservation loop.

The third argument now limits payload size. The registered adapter computes ps = min(allocator.capacity(), mps), checks initial length against ps, and advertises overall size ps + headRoom + tailRoom. It initializes skip to headRoom. Pass cxn->info().mps directly and compute the TLS chunk from the returned payload extent. For example, mps 1460 with headRoom 54 and tailRoom zero permits 1460 ciphertext bytes in an overall 1514-byte view when provider capacity allows it. Provider backing and pool count remain fixed.

The builtin adapter now returns the capped ps as its inline object's advertised size and requests VHeap storage using the grown size_. Its zero-headroom geometry therefore follows the same payload-cap contract. Verify caps below builtin Size, rejection above mps, growth above builtin Size and final heap release in the allocator regression tests.

This supplies the connection-to-packet allocation boundary required by IR-4. Shared provider geometry remains fixed while individual connections pass different mps values. Verify smaller-MSS connections sharing one allocator, TCP and UDP establishment, provider capacity below mps, kernel zero-headroom allocations, and the zero/unknown state before callbacks.

Use the current receive split as the baseline. Socket contexts use RxTCP, ZiRx frames ciphertext through App storage and Ztls decrypts into RxTLS plaintext buffers. A network Rx stub has null backing and rejects nonzero initial length. App framing accepts `allocBuf(owner, nextLen)` for coalesced input.

One TLS record remains one TLS record on the wire even when it occupies several TxTCP buffers. Treat its output as a serial sequence of chunks. Start record protection once, encrypt successive chunks directly into their destination packet buffers, and finalize authentication once. Packet boundaries do not introduce TLS headers, new TLS record sequence numbers, independent nonces, or per-packet tags.

## Packet reservation and transmit failure

Reserve every output packet before protecting a TLS record. Determine the wire size, including any KeyUpdate prefix, on the link Tx thread before advancing cipher state. Acquire the full reservation in an intrusive chain. If any allocation fails, release the chain and disconnect the originating connection generation. The failed reservation consumes no TLS sequence state and publishes no bytes. The shared link and multiplexer Tx thread must remain free to process sends and release buffers.

After reservation succeeds, encrypt the record into those buffers, set their lengths and submit them in order through ZiTx. Validate that the packet pool can hold one maximum supported record including its control prefix. Socket readiness, congestion and descriptor availability govern progress for accepted output.

Z's slow-receiver backpressure policy allows the Tx queue to grow to its configured limit. When the queue rejects further output at that limit, disconnect the originating connection. Resume service through the existing reconnect and recovery path.

ZiTx limits queued buffers through `Impl::maxQueueLen()`, which defaults to 1024. A full queue rejects the incoming buffer through `sent(buf, false)`. Apply the following failure contract:

- Callers writing through `ZiTxStream::operator<<` check `failed()` after the final flush and disconnect on failure.
- Implementers of `sendBuf_` that submit through ZiTx implement `sent(buf, ok)` and disconnect on `ok == false`. Off-thread dispatch success precedes the later queue decision.
- Route deferred reservation failure through the generation-bound Tx error and disconnect path. Bind packet failure to its originating physical connection and generation. Schedule teardown on the appropriate owner and allow the queue callback to finish before cleanup.
- If queue rejection or transport failure occurs after any ciphertext was submitted, stop publishing that record's remaining packets, release unpublished reservations and tear down the originating connection. Release canceled record state through the existing generation teardown. Packet completion follows the ZiTx contract specified below.

The disconnect policy supersedes IR-1's proposed retry and capacity-notification contract.

### Partial transmission and retransmission

TCP may retransmit a leading portion of a record using the original TCP byte positions and identical retained ciphertext. TCP suppresses duplicate bytes before TLS delivery. Ztls must never retry an accepted record by appending its prefix again at new TCP byte positions: TLS would interpret that prefix as continuation of the incomplete record, corrupting framing/authentication. TCP owns retransmission; it retains the original TxTCP ciphertext buffers. It must not ask Ztls to re-encrypt a partially transmitted record.

Reserving and encrypting the whole record before publication prevents packet-pool exhaustion from creating that partial-record failure. It does not make TCP transmission atomic or eliminate normal partial socket sends, retransmissions or connection failure.

## Serial record encryption

Add the following public dependency interface in zpicotls.h. Function names and callback signatures below are the selected contract. Existing contiguous entry points use the same record emitter with their contiguous sink adapter.

```c
#define PTLS_HAVE_RECORD_CHUNKS 1

typedef struct st_ptls_record_sink_t {
    void *ctx;
    int (*reserve)(void *ctx, size_t wire_size);
    int (*next)(void *ctx, void **base, size_t *capacity);
    int (*commit)(void *ctx, size_t length);
    int (*finish)(void *ctx);
    void (*discard)(void *ctx);
} ptls_record_sink_t;

int ptls_tx_send_chunks(ptls_tx_t *tx, ptls_record_sink_t *sink,
                        const void *input, size_t length);
int ptls_tx_key_chunks(ptls_tx_t *tx, ptls_record_sink_t *sink,
                       int request_update);
int ptls_tx_alert_chunks(ptls_tx_t *tx, ptls_record_sink_t *sink,
                         uint8_t level, uint8_t description);
int ptls_handshake_chunks(ptls_t *tls, ptls_record_sink_t *sink,
                          const void *input, size_t *length,
                          ptls_handshake_properties_t *properties);
int ptls_recv_chunks(ptls_t *tls, ptls_buffer_t *plaintext,
                     ptls_record_sink_t *sink,
                     const void *input, size_t *length);
```

All callbacks are synchronous on the calling TLS owner. They return zero on success. Reservation exhaustion returns PTLS_ERROR_NO_MEMORY. Invalid lengths, span bounds or sink sequencing return PTLS_ERROR_LIBRARY. Backend encryption failures return PTLS_ERROR_LIBRARY through the high-level operation. These failures cause the connection disconnect policy. A C++ sink callback must catch and translate exceptions before crossing the C boundary.

For detached application output, compute exact wire_size for the current record plus any required KeyUpdate record before mutating outbound cipher state. Use the existing negotiated overhead, TLS 1.2 IV length, TLS 1.3 inner type and current key-update decision. Length is bounded by PTLS_MAX_PLAINTEXT_RECORD_SIZE. Empty input retains the existing contiguous entry point's behavior, including any pending control emission. Call reserve once for that operation's total wire output. The reserve callback acquires every required TxTCP packet with the same originating connection and mps snapshot, in an intrusive chain. On failure it releases the partial chain. Call discard before returning the reservation error. No record sequence, explicit-IV counter or key transition has occurred at this point.

After reserve succeeds, the owning TLS object is unchanged until this same synchronous operation emits its output. This establishes sizing validity without a token or a separately scheduled sizing pass. Apply a pending KeyUpdate using the old key and advance to the new key before the following application record, following the existing contiguous ordering. Each record advances its own TLS sequence once. The entire operation remains unpublished until its final tag and finish.

next exposes the unused part of the current reserved packet, starting at data()+length and ending before allocator tailRoom. Its returned capacity is positive and excludes committed bytes. commit consumes at most the most recently exposed span and increments the packet length. next returns the remaining span or advances to the next reserved node. The writer may split a clear header, explicit IV, encrypted byte range or tag over these spans. finish requires committed bytes to equal the reserved wire_size and queues the complete packet chain in order through ZiTx. An error during emission invokes discard, releasing all unpublished packet references, and makes the high-level operation terminal for that session. Cipher state is not rolled back after encryption starts.

The handshake and receive-with-control entry points reuse the existing TLS state machine. Route all record output generated by that state machine, including alerts, tickets and post-handshake messages, through the same emitter. Each generated output record has its own reserve/write/finish transaction on Rx. A handshake flight may publish preceding complete records before a later record fails. The resulting error tears down the session. Pass encrypted output packets to mx Tx with the existing generation and physical-connection guards. Detached application protection stays Tx-owned. Receive plaintext remains in its existing ptls_buffer_t and RxTLS backing. No handshake transcript or key state is read by the detached Tx owner.

### Backend state across calls

Add optional checked serial callbacks to ptls_aead_context_t named do_enc_begin, do_enc_chunk and do_enc_end. Use these signatures in ptls_aead_context_t:

```c
int (*do_enc_begin)(ptls_aead_context_t *ctx, uint64_t seq,
                    const void *aad, size_t aadlen);
int (*do_enc_chunk)(ptls_aead_context_t *ctx, void *output,
                    const void *input, size_t length, size_t *written);
int (*do_enc_end)(ptls_aead_context_t *ctx, void *output,
                  size_t capacity, size_t *written);
```

 For the AEADs selected by Ztls, chunk writes exactly length bytes and end writes the tag. The record writer supplies fixed PTLS_MAX_DIGEST_SIZE tag scratch when the remaining packet span cannot hold the tag and copies those bounded tag bytes across packet spans. Selected cipher setup must install all three callbacks and configuration rejects an unavailable selected backend before a connection can send.

OpenSSL begin initializes the nonce and AAD through the existing EVP path. chunk performs checked EVP_EncryptUpdate calls limited to the current output span. end checks EVP_EncryptFinal_ex and obtains the tag. Translate EVP failure to PTLS_ERROR_LIBRARY in release builds. Preserve one AEAD operation across chunk calls, including TLS 1.3's protected content type and TLS 1.2's explicit-IV rules.

Fusion uses one serial state structure embedded in aesgcm_context for both existing temporal and non-temporal variants. Fields are J0, the current 128-bit counter, one 16-byte keystream block with a consumed-byte index, a 128-bit GHASH accumulator, one 16-byte authentication carry block with a used-byte count, and uint64_t AAD/text byte counts. begin derives J0 from the existing static-IV/record-sequence logic, initializes the counter with inc32(J0), hashes AAD and pads its final authentication block once. chunk reuses the existing expanded AES keys and AES-NI/VAES and carryless-multiply helpers. Generate counter blocks continuously, XOR into the supplied destination, and feed produced ciphertext into GHASH in order. Preserve unused keystream bytes and uncompleted GHASH blocks across destination boundaries. Chunk boundaries never pad GHASH or restart the counter. end pads the final ciphertext GHASH block, incorporates the 64-bit big-endian AAD/text bit-length block and XORs the accumulator with AES(J0) to emit the tag. Clear serial state on disposal and after terminal failure. For full aligned blocks retain the selected backend's existing accelerated kernels. Use the fixed block state to handle partial or unaligned packet spans. Record-sized scratch allocation and plaintext/ciphertext flattening are excluded from this path.

Test every split position around cipher blocks, record framing and tag bytes against contiguous output from identical initial state. Include the selected Fusion variants, OpenSSL AES-GCM and ChaCha20-Poly1305, both TLS versions currently supported, key updates and release-build backend failures.

## Ztls integration

Keep the existing `txAllocator` configuration and `TxAllocator` type for TxTLS plaintext. Add a distinct `txPacketAllocator` configuration/accessor for TxTCP output. Both are explicit, immutable allocator selections entered in the multiplexer allocator registry while it is stopped. Registry membership is distinct from Registered packet backing; the TxTLS selection still allocates ordinary builtin/heap storage. Validate their types and common multiplexer. Pass the packet pool count through the TxTCP factory. TxTLS uses the builtin allocator on either backend.

Update `TxBufAlloc` and the application's Tx stream to describe plaintext capacity, rather than space for in-place TLS ciphertext. Remove TLS-output header/tag reservations from that plaintext path; preserve headroom genuinely needed by dependent application framing. Keep the TLS plaintext limit independent of the packet allocator's capacity. The TxTLS role selects ordinary builtin/heap storage and uses `ZmHeapFail` for heap exhaustion.

Replace `send_`'s in-place `ptls_buffer_init_tx`/`ptls_tx_send` path with reservation followed by the serial record writer. Remove the assertion that TLS output starts at `data_()` and the finalization code that assigns `skip = 0`. A packet's initialized payload origin is stable through encryption, send and retransmission.

Use ZiTx's per-buffer completion contract for ciphertext packets. Each output packet receives one local send-completion callback when ZiTx processes it. Successful completion retires that packet's local send hold. TCP retains any independent recovery references. Increment the record-protection count once after the serial writer succeeds and account for wire bytes per packet. Release the source plaintext after protection finishes. Completion has no record-level aggregation object.

Define Ztls::TxPacket outside the Link template, deriving from ZiTxBuf and adding a raw originating Cxn pointer and uint64_t generation. The pointer has the common ZiConnection type. Construct packet metadata through the existing IOBufStub/IOBufBuiltin factory using this base type. Provider backing lifetime remains governed by the existing IOBuf reference count. The physical connection and link must drain their queued setups and ZiTx queue before destruction under docs/IO.md. Stamp origin/generation before publishing a packet and preserve them through send completion. Do not inspect those pointers during a late provider free.

Implement sent(packet, ok) on the mux Tx owner. For false, capture a control-path ZmRef to that packet's originating physical connection and post its close to mx Tx after the current queue callback. Post the generation-tagged failure to link Rx for its existing disconnect/reconnect lifecycle. The current successor connection is affected only if its generation matches. This failure is unconditional even if a diagnostic callback chooses to continue. For true, retain existing optional diagnostics and release the callback reference. Derived sent overrides must forward to the Link implementation to preserve failure handling.

On transport failure, ZiTx reports the failed head and cleans the remaining queued nodes. Tail cleanup only releases buffers and their origin metadata. It creates no missing parent completion because the interface is packet-based. On a full queue, the rejected packet receives sent(false). Close prevents later queued setups from starting output. Canceled setups and unpublished reserved packets release through their ordinary references. Tests cover multiple records in the queue, rejection after a record prefix, head failure with a populated tail, delayed setups after close and a replacement connection that survives an old-generation error.

Cover every TLS output producer: application records, handshake output, alerts, close-notify and KeyUpdate. Where handshake processing currently owns mutable TLS state on Rx, preserve that ownership and pass completed packet references to Tx through the existing dispatch boundary. Use the same serial record writer and TxTCP allocation contract; formatting handshake plaintext in ordinary heap storage is permitted. Remove registered, record-sized handshake/control output allocations. If a handshake/control producer has already advanced non-rollbackable TLS state when reservation fails, use its existing handshake/session failure path; do not replay it speculatively on the live connection.

Rx framing/decryption continues to accept TLS records split across TCP packets. No Rx cryptographic redesign is required. Packet boundaries are invisible to peer TLS.

## Implementation sequence and file inventory

The zpicotls dependency is a separate checkout at `../zpicotls`. Implement and verify its changes first, then rebuild/install it through its documented dependency workflow before enabling the new Ztls path. This plan authorizes no production edits by itself; the present deliverable is this document.

1. **Implement serial output in the dependency.**
   - `../zpicotls/include/zpicotls.h`: define the serial span-sink, record-output sizing/admission boundary and chunk-capable protection contract; retain existing contiguous APIs for their existing callers.
   - `../zpicotls/lib/picotls.c`: share record framing, sequence/key-update rules and control-record construction with the chunk writer. Size application output without changing cipher state, then emit directly to reserved spans.
   - `../zpicotls/lib/openssl.c`: provide bounded serial encryption through the existing EVP state, including supported TLS 1.2/1.3 AEADs.
   - `../zpicotls/lib/fusion.c` and `../zpicotls/include/zpicotls/fusion.h`: implement the corresponding Fusion state/carry operations and required declarations for all Ztls-selected Fusion variants.
   - `../zpicotls/CMakeLists.txt`: compile the existing vector-Tx test target against the new feature macro and register any added cases in the existing test executable.
   - `../zpicotls/t/vector-tx.c`, `../zpicotls/t/openssl.c`, `../zpicotls/t/fusion.c`: compare chunked output against contiguous output and test backend state, boundary and failure behavior. Extend the existing test targets; no new target is required.
2. **Atomically migrate allocator roles, record transmission and all dependent callers.**
   - `ztls/src/Ztls.hh`: add the TxPacket metadata base, serial sink callbacks and generation-bound packet failure handling described above, then split allocator configuration/validation using the current mx factory argument order, pass originating connection mps as allocBuf's third argument for every ciphertext packet, switch plaintext to Zi's existing unregistered builtin/VHeap path without plugin calls, add all-or-nothing packet reservation, serial output, ordered publication and record-level completion handling. Migrate handshake/control producers and preserve shard/generation teardown rules.
   - `zi/src/ZiIOBuf.hh`: consume the corrected builtin and plugin payload-cap implementations and retain their allocation/free bounds during integration.
   - `zi/test/ZiIOBufTest.cc`: cover payload mps caps, initial-length rejection, preserved skip, headroom/tailroom added outside the cap, builtin capped sizes, grown backing bounds and shared provider independence.
   - `ztls/src/ZtlsBackend.hh`: declare the Ztls-side adapter to the dependency's serial record-output API where backend-facing declarations belong. Keep packet/allocator ownership in Ztls, not the crypto dependency.
   - `configure.ac`: add a compile/link feature check for the selected chunk-output API; check PTLS_HAVE_RECORD_CHUNKS and link ptls_tx_send_chunks, ptls_handshake_chunks and ptls_recv_chunks.
   - `docs/DEPENDENCIES.md`: document the required rebuilt zpicotls capability.
   Complete the following caller migration in the same buildable phase as the preceding production API changes.
   - `ztls/example/ztlsclient.cc`, `ztls/example/ztlsserver.cc`: construct and register separate TxTLS and TxTCP allocators before start, with independent geometry/counts.
   - `ztls/test/ZtlsBufHookTest.cc`, `ztls/test/ZtlsAsyncTest.cc`: update allocator configuration and assertions that currently expect registered whole-record output or an unchanged plaintext/output origin.
   - `zhttp/src/ZhttpConfig.hh`, `zhttp/src/ZhttpNative.hh`, `zhttp/src/ZhttpH2Hub.hh`: carry the packet allocator through TLS configuration and native/H2 factory paths while preserving application plaintext framing. TCP and H3 continue using their respective allocator roles.
   - `zhttp/test/ZhttpH2HubTest.cc`, `zhttp/test/ZhttpTransportContractTest.cc`: update the affected TLS factory fixtures and assert both allocator roles. The remaining direct callers are enumerated below.
   - `zhttp/itest/zhttplifecycletest.cc`: supply packet allocators to both TLS endpoints and update allocator-role assertions.
   - `zhttp/interop/zhttp3interoptest.cc`: supply packet allocators for the direct TLS client and server fixtures used by HTTP interoperability.
   - `zhttp/src/ZhttpClientImpl.hh`, `zhttp/src/ZhttpServerImpl.hh`: verify their TLS template/configuration plumbing with the added packet allocator and update affected forwarding declarations in this phase.
   - `ztls/README.md`: document packet callback semantics, allocator roles, stream failure checking and disconnect/recovery behavior.

   The phase checkpoint builds all configured dependencies with top-level make -j8 and runs the affected Ztls and Zhttp test targets. Phase 1 keeps existing contiguous callers buildable. Phase 2 includes the complete API/caller migration as one change. No commit between those changes may leave a mandatory allocator selection missing.

3. **Verify independently of Zutcp.**
   - Create `ztls/test/ZtlsTxPacketTest.cc`: isolated fake packet-provider/sink tests for reservation, headroom preservation, lifetime and completion behavior.
   - `ztls/test/Makefile.am`: register that TAP self-test.
   - Create `ztls/itest/ztlstxpackettest.cc`: working TLS client/server fixture over kernel TCP using deliberately small output buffers and nonzero-headroom fake-provider coverage where the fixture supports it.
   - `ztls/itest/Makefile.am`, `ztls/itest/README.md`: build/run and document the working-pair coverage.
4. **Reconcile the Zutcp prerequisite and run its phase-2 gate.**
   - `design_draft.md`: replace the Ztls whole-record registered ciphertext/header-table dependency with packet-sized registered TxTCP ciphertext for this path. Update the earlier exclusion of Ztls edits to identify this separately implemented prerequisite. Adopt MTU-sized registered buffers and fixed provider size and framing reservations throughout; remove the large registered-buffer/header-table/gather machinery superseded by this decision. No additional dependent requiring it has been selected.
   - `review_context/af-xdp-sg-verification.md`: record that the old TLS-specific multi-buffer requirement has been removed by this prerequisite, while retaining the verified driver limitations.
   - `implementation_readiness_report.md`, `goldfish_review_report.md`, `review_context/manifest.json`: record the changed prerequisite and affected claims without rewriting historical review snapshots as if they reviewed this migration.

## Verification and completion gates

Use the existing zpicotls test workflow, then configure Z through `z.config` and build dependencies from the top level with `make -j8`. Build each affected test directory before its `make test` target. Perform a clean rebuild when changing release/sanitizer configuration, per `docs/BUILD.md`.

- Compare concatenated ciphertext chunks byte-for-byte against contiguous encryption from identical initial state for TLS 1.2/1.3, AES-GCM and ChaCha20-Poly1305 where supported; verify decryption and authentication with the existing peer.
- Exercise output boundaries through the TLS header, explicit IV, cipher blocks, inner content type and tag. Include tiny final chunks, packet-sized records, a full 16 KiB plaintext record, consecutive records and KeyUpdate. Exercise selected Fusion acceleration variants and OpenSSL paths, including permitted unaligned destinations.
- Fail allocation at every reservation position. Verify release of all prior reservations, unchanged plaintext, no published ciphertext, unchanged record sequence/key state, and disconnect of the originating generation.
- Verify `skip == 0` kernel output and nonzero allocator headroom with sentinel bytes; TLS must never overwrite the reserved prefix or provider tail reservation. Assert distinct plaintext/output backing and no full-record ciphertext allocation/copy.
- Fill ZiTx to its queue limit and verify rejection reaches sent(false), disconnects the originating connection and releases the rejected buffer. Exercise final-flush failure and verify the writer checks failed() and disconnects.
- Apply partial socket sends and transport backpressure after admission. Verify retained output resumes in order without repeated encryption, duplicate application completion or duplicated TLS stream bytes. Simulate retransmission at unchanged TCP sequence positions in transport tests.
- Close, replace and restart connections with pending reservations/output. Verify old-generation work is rejected, every reference retires through the correct allocator, cipher state stays on its owner and teardown remains asynchronous.
- Run affected Ztls/Zhttp suites, sanitizer/leak checks and a kernel TCP working pair before making this a Zutcp prerequisite. Test per-connection mps passed to every packet allocation, MTU-bounded provider geometry, rejection of oversized initial lengths, independent TxTLS record capacity, and exact size/headRoom/tailRoom accounting. Later verify the packet geometry over veth and physical mlx5 without `XDP_USE_SG` for the single-buffer path; actual zero-copy selection remains a kernel/driver decision and must be reported, not inferred.

The migration is complete when application plaintext remains on Zi's normal unregistered builtin/heap path, all TLS wire output is produced directly into separately allocated packet buffers, allocation failure is atomic before protection/publication, and a peer receives the same TLS byte stream without requiring NIC scatter/gather for this path.
