# Zquic Heap Allocation and Buffer Copy Review

Scope: `zquic/src` plus the `ztls/src/ZtlsPico.*` support added for the
Zquic packet path.  Test-only allocations and copies are excluded except where
they exercise production helpers.

Status: implementation complete through Phase 10 of `plan.new.md`.

## Implementation Status

| Area | Implemented state |
| --- | --- |
| Packet buffers | Endpoint receive and transmit buffers use distinct `Zquic.Packet.Rx` and `Zquic.Packet.Tx` aliases and diagnostics. |
| Allocation diagnostics | `Zquic::BufDiag` now exposes role counters for packet Rx/Tx buffers, stream Rx slices/copy fallback, stream Tx buffers, queue nodes, packet-protection context initialization, packet-to-stream fallback copies, and forbidden copies. |
| Queue heaps | Zquic queues use named role heaps, including stream Rx, stream Tx, CRYPTO Rx, recovery ACK/sent/retransmit queues, and endpoint connection objects. |
| Picotls contexts | Traffic AEAD and header-protection contexts are cached per direction/epoch and allocated at `algo->context_size` from the single named `Ztls.Pico.Ctx` `ZmVHeap`. |
| Packet protection | 1-RTT/handshake traffic protection uses cached contexts. Tx vector protection uses `ptls_aead_encrypt_v_s()` and supplementary HP sample encryption. Rx decrypt remains in-place. |
| Packet builder | Tx send paths build headers in packet buffers, serialize small prefixes to scratch, and pass retained plaintext spans to vector packet protection. |
| Stream Tx | A stream Tx queue node is now the retained `ZiIOBuf` object, so flushed stream data does not allocate a separate buffer plus queue node. |
| Stream Rx | Normal STREAM receive queues retained packet-backed `ZiIOBuf` slices. The packet buffer stays alive until the app-visible slice is released. |
| CRYPTO Rx | In-order CRYPTO delivery exposes a direct span from the incoming frame. Fragmented CRYPTO uses the existing named queue/copy fallback and bulk delivery rather than byte-at-a-time growth. |
| TLS output | Zquic TLS output uses named `CryptoTxBufAlloc` buffers with `ptls_buffer_t.origin` set to the destination `ZiIOBuf`; silent picotls growth is rejected in the per-message Zquic helper. |
| Stream table | Stream objects are allocated as intrusive hash nodes under `Zquic.Stream.ObjectHash`, removing the separate stream-object plus hash-node allocation pattern. |
| Endpoint connection | `Endpoint::Cxn_` uses the named `Zquic.Endpoint.Cxn` heap. Existing one-pointer callbacks remain inline. |

## Remaining Allocations

The final scan command was:

```sh
rg -n "memcpy|memmove|new |ptls_aead_new_direct|ptls_aead_free|ptls_cipher_new|ptls_cipher_free|ZtArrayHeapID<\"ZtArray\"|Zi::VHeap|ZiIOBuf_HeapID|ZmHash<ZmRef<Stream" zquic/src ztls/src
```

Remaining `new` sites in `zquic/src` are intentional and classified:

| Category | Current reason |
| --- | --- |
| Packet Rx/Tx buffers | Async endpoint ownership requires retained datagram buffers; these use distinct packet role heaps and counters. |
| CRYPTO Rx fallback | Fragmented/out-of-order CRYPTO still needs retained bytes before contiguous TLS delivery; this path uses named CRYPTO buffer and queue heaps. |
| TLS output buffers | Per-message TLS output uses named crypto Tx buffers and picotls origin-backed storage. |
| Stream Tx nodes | The node is the retained stream plaintext buffer; this is one allocation per flushed stream buffer. |
| Stream Rx nodes | The node is the app-visible retained slice or fallback copy buffer; normal receive uses packet-backed slices. |
| Stream objects | Stream/hash allocation is consolidated in the intrusive hash node. |
| Recovery metadata | ACK, sent-packet, and retransmit queues allocate metadata nodes only; no stream payload bytes are copied there. |
| Endpoint connection | `Cxn_` has endpoint/multiplexer lifetime and a distinct heap. |

`ztls/src/ZtlsPico.cc` intentionally contains variable-sized context allocations
from `Ztls.Pico.Ctx`.  Those are named `ZmVHeap` blocks sized from
`algo->context_size`, not per-cipher buckets or per-packet malloc/free calls.  The generic
origin-less `Zi::VHeap` buffer fallback remains available for non-Zquic picotls
buffer users, but the Zquic TLS output path uses origin-backed `ZiIOBuf`
storage and tests assert no internal allocation in the covered common path.

## Remaining Copies

Remaining copies are accepted in these categories:

| Category | Current reason |
| --- | --- |
| HKDF/key/secret scratch | Fixed-size crypto scratch construction and secret ownership copies. |
| Initial packet protection | Initial and retry paths still use the existing backend helpers. The plan explicitly deprioritized OpenSSL/internal setup tuning; traffic protection no longer constructs picotls contexts per packet. |
| Full-frame scalar writers | `writeCrypto()` and `writeStream()` still provide contiguous frame-writing APIs for tests and scalar callers. Runtime Tx uses prefix writers plus vector plaintext spans. |
| Packet/header serialization | Connection IDs, tokens, retry tags, transport parameters, path challenge data, and similar wire fields must be serialized into packet/output buffers. |
| STREAM Rx payloads | STREAM frame delivery requires the owning packet buffer and queues packet-backed slices; the frame-only copy fallback was removed. |
| CRYPTO fragmentation fallback | Fragmented CRYPTO can still copy into named retained buffers; in-order delivery uses a direct span and queued delivery is bulk-appended. |
| Picotls buffer fallback | Origin-less picotls buffer growth copies old data into `Zi::VHeap` storage; this is outside the steady-state Zquic packet/TLS output path. |

No per-STREAM-payload Tx copy into packet plaintext scratch remains in the
runtime send paths.  Small ACK, frame-prefix, and padding bytes remain scratch
serialization, not payload buffer copying.

## Acceptance Checklist

- Traffic `protect*()` / `unprotect*()` code in `zquic/src` no longer calls
  `ptls_aead_new_direct`, `ptls_aead_free`, `ptls_cipher_new`, or
  `ptls_cipher_free`.
- Traffic key updates refresh cached AEAD, ECB HP, and supplementary CTR HP
  contexts; secret discard and key update clear cached contexts.
- Packet-builder runtime paths use vector protection for Initial, Handshake,
  and short-header packet sends.
- Normal STREAM Rx payload delivery does not increment packet-to-stream copy
  counters.
- Stream Tx and Rx queue data each use one retained node object per live range,
  not a buffer object plus a second queue-node allocation.
- Remaining hot allocations use named role heap IDs and diagnostics.
- No Zquic-owned persistent `ZtArray` defaults to the generic `"ZtArray"` heap.
- Stream lookup/open/accept behavior is preserved with the intrusive stream
  hash.

## Validation

Phase validation already exercised:

- `ZquicAPITest`
- `ZquicBufferTest`
- `ZquicEndpointTest`
- `ZquicPacketProtectionTest`
- `ZquicHandshakeTest`
- `ZquicStreamTest`
- `ZquicRuntimeTest`
- `ZquicH3InteropTest`
- `ZquicFlowTest`
- `ZquicLoopTest`
- `ZquicPQueueTest`
- `ZquicCongestionTest`
- `ZquicSockTest`

The preferred final gate remains:

```sh
make -C zquic/test test
```
