# Ztls / picotls Buffer Handling

This document is the maintainer reference for how `ztls` supplies buffers to
picotls and how picotls is allowed to use them. The rules below are invariants:
code that cannot satisfy them should fail with a diagnostic or `ZiAssert`, not
try to repair the buffer after the fact.

The implementation relies on a local picotls buffer hook installed by
`Ztls::Pico::install()`. That hook lets picotls grow ztls-owned `ZiIOBuf`
instances without copying through picotls' internal heap, while preserving
picotls' `ptls_buffer_t` contract.

## Data Model

- `ZiIOBuf::data_()` is the raw allocation base. It is independent of
  `buf->skip`.
- `ZiIOBuf::data()` is the active data pointer. The relationship is always
  `buf->data() == buf->data_() + buf->skip`.
- The TLS record base is the first byte of a serialized TLS record, including
  the 5-byte TLS record header.
- `m_headroom` is the byte distance from the TLS record base to application
  plaintext:
  - TLS 1.3: `m_headroom == 5`.
  - TLS 1.2: `m_headroom == 5 + explicit record IV bytes`.
- `ptls_buffer_t::base` is the first byte of the output region picotls writes
  into.
- `ptls_buffer_t::off` is the number of valid output bytes starting at
  `pbuf.base`.
- `ptls_buffer_t::capacity` is the writable byte count starting at `pbuf.base`.
- `ptls_buffer_t::origin` is ztls' ownership marker. For every ztls-owned
  output buffer, `pbuf.origin == buf.ptr()`.

## Shared Invariants

- Origin-backed output is zero-copy and remains owned by the `ZiIOBuf`.
  Ztls must not call `ptls_buffer_dispose()` for bytes owned by `ZiIOBuf`.
- For every ztls-owned `ptls_buffer_t`, `pbuf.base` must point inside the same
  `ZiIOBuf` allocation as `pbuf.origin`.
- Bounds are relative to the actual `pbuf.base`:
  `pbuf.off <= buf->size - (pbuf.base - buf->data_())`.
- A ztls-owned `ptls_buffer_t` returning as an internal picotls allocation, or
  returning a base outside its `ZiIOBuf`, is a contract violation.
- The origin allocation hook must preserve `pbuf.base - buf->data_()` across
  `ZiIOBuf::ensure()`.
- If `pbuf.off` is non-zero during origin-backed growth, the logical valid
  range `[pbuf.base, pbuf.base + pbuf.off)` must be preserved after relocation.
- The hook must update `pbuf.capacity` relative to the preserved base offset.
- The hook must not reset `pbuf.base` to `buf->data()`. `buf->data()` is
  path-dependent: application Tx uses it as the plaintext pointer, while the
  TLS record base remains `buf->data_()`.
- When picotls requests `align_bits`, the buffer base passed to picotls for
  that operation must satisfy the requested alignment.
- `ZiIOBuf_Align` must be sufficient for the fusion AEADs selected by ztls.

## AEAD Constraints

Ztls application Rx decrypts in-place: the ciphertext record and plaintext
output are different regions within the same `ZiIOBuf`. A selected AEAD must
support exact in-place decrypt for that layout.

The `ptls_non_temporal_*` fusion AEADs are not valid for this invariant because
their decrypt path can write plaintext before it has finished reading the
ciphertext and authentication tag. Ztls therefore uses normal fusion AEADs when
fusion is enabled, and excludes the non-temporal variants.

## Handshake Rx

- Before `ptls_handshake_is_complete(m_tls)`, incoming records are routed to
  `handshake_()`, not `rcvd_()`.
- The incoming buffer is a complete TLS record from the transport framing path.
  `buf->data()` points at the TLS record base and `buf->skip == 0`.
- `ptls_handshake(m_tls, &pbuf, input, inlen, &m_props)` receives the incoming
  record base as `input`.
- The incoming record is read-only for ztls. It is not converted into
  application plaintext.
- The `pbuf` supplied to `ptls_handshake()` is a handshake Tx buffer.
  Handshake Rx does not use `rxBuf()` and does not publish to `m_rxStream`.
- `*inlen` is picotls' consumed-input count for the handshake call. It is not a
  plaintext length and must not be used to adjust application-visible data.

## Handshake Tx

- Handshake Tx uses `txBuf()`.
- `txBuf()` allocates a fresh `TxBufAlloc`, grows it to `TxRecordCapacity`, and
  starts with `buf->skip == 0`.
- The handshake record base is `buf->data_()`. Since `skip == 0`, this is also
  `buf->data()`.
- Ztls initializes picotls with
  `ptls_buffer_init_tx(&pbuf, buf->data_(), TxRecordCapacity)`, then sets
  `pbuf.origin = buf.ptr()` and `pbuf.align_bits`.
- `ptls_handshake()` may write serialized handshake records into `pbuf`.
- After the call, Tx finalization must assert origin ownership, assert
  `pbuf.base == buf->data_()`, assert bounds, then send with `buf->skip = 0`
  and `buf->length = pbuf.off`.

## Application Rx

- After `ptls_handshake_is_complete(m_tls)`, application records are routed to
  `rcvd_()` and use `rxBuf()`.
- The incoming network buffer has `buf->data()` pointing at the TLS record base,
  with `buf->skip == 0` in the normal framed Rx path.
- Ztls passes the TLS record base as the input pointer to
  `ptls_receive(m_tls, &pbuf, base, &inlen)`.
- Ztls initializes the receive output buffer with
  `ptls_buffer_init_rx(&pbuf, base + m_headroom, buf->size - m_headroom)`.
- The receive output base is the plaintext location inside the same `ZiIOBuf`,
  after the record header and any explicit TLS 1.2 record IV.
- Before `ptls_receive()` is called, `rxBuf()` must ensure the backing
  `ZiIOBuf` has at least `buf->length + m_headroom` bytes.
- Origin-backed Rx growth inside `ptls_receive()` is invalid. Picotls parses
  input pointers before reserving output; relocating the `ZiIOBuf` after that
  would stale the parsed ciphertext pointer.
- On successful application receive, `pbuf.origin == buf.ptr()` and
  `pbuf.base == base + m_headroom`.
- Plaintext is valid in `[pbuf.base, pbuf.base + pbuf.off)`.
- Application plaintext is published by setting `buf->skip = m_headroom`,
  setting `buf->length = pbuf.off`, and pushing the buffer to `m_rxStream`.
- `ptls_receive()` must consume the entire supplied TLS record buffer. A partial
  `inlen` result is a protocol or framing failure for this path.

## Application Tx

- Application Tx starts with plaintext staged at `buf->data()`.
- Application Tx requires `buf->skip == m_headroom`.
- The application Tx TLS record base is `buf->data_()`, which is also
  `buf->data() - buf->skip`.
- Application Tx must pre-size the `ZiIOBuf` to `TxRecordCapacity` before
  plaintext is given to picotls.
- `txStream()` bounds each plaintext fragment so
  `plaintext length + m_headroom + (TxMaxOverhead - m_headroom)` fits within
  `TxRecordCapacity`.
- `finishHandshake_()` rejects negotiated record overhead above `TxMaxOverhead`.
- Growth of an application Tx origin buffer during `ptls_send()` is invalid.
  It means the headroom/tailroom sizing invariant was broken; the hook should
  not repair the buffer.
- Ztls initializes picotls with
  `ptls_buffer_init_tx(&pbuf, buf->data_(), TxRecordCapacity)`, sets
  `pbuf.origin = buf.ptr()` and `pbuf.align_bits`, then calls
  `ptls_send(m_tls, &pbuf, buf->data(), buf->length)`.
- Picotls writes the TLS record header at `pbuf.base` and encrypts the supplied
  plaintext into the record body.
- For TLS 1.3, the encrypted body begins immediately after the 5-byte record
  header. TLS 1.3 also adds the inner content type before the AEAD tag, which is
  part of the tailroom budget.
- Fusion AEAD alignment is satisfied by the TLS record base passed to picotls.
  The plaintext pointer at `buf->data_() + m_headroom` does not need a separate
  alignment invariant.
- After Tx finalization, `pbuf.base == buf->data_()` is invariant. The code
  asserts that `pbuf.base - buf->data_() == 0`, asserts bounds, then publishes
  the serialized TLS record with `buf->skip = 0` and `buf->length = pbuf.off`.
- Tx sends exactly the serialized TLS record bytes in
  `[pbuf.base, pbuf.base + pbuf.off)`.

## Post-Handshake Control Rx

- After the handshake is complete, TLS 1.3 control messages such as KeyUpdate
  arrive through the same framed network path as application records.
- Control messages are processed by `ptls_receive()`.
- The input-side invariants are the same as application Rx: `buf->data()` is
  the TLS record base and `buf->skip == 0`.
- The output-side invariants are also the same as application Rx:
  `pbuf.origin == buf.ptr()` and `pbuf.base == base + m_headroom`.
- The no-growth-before-`ptls_receive()` invariant is also the same as
  application Rx: the `ZiIOBuf` must be sized to cover the wire-record length
  plus `m_headroom` before picotls parses and processes the record.
- A pure post-handshake control message does not publish application plaintext.
  Successful processing with `pbuf.off == 0` must not push the buffer to
  `m_rxStream`.
- If a received KeyUpdate requires a reciprocal update, that requirement must be
  represented in ztls state before the next application Tx record is emitted.
- `ptls_receive()` must consume the entire control record. A partial `inlen`
  result is a protocol or framing failure for this path.

## Post-Handshake Control Tx

- Local or reciprocal re-keying Tx uses the no-plaintext control-message shape.
- `ptls_update_key(m_tls, request_update)` updates picotls state.
- `ptls_send(m_tls, &pbuf, nullptr, 0)` serializes the KeyUpdate record.
- Post-handshake control Tx uses `txBuf()`, not an application Tx buffer.
- The record base is `buf->data_()`, `buf->skip == 0`, and
  `pbuf.origin == buf.ptr()`.
- If control Tx is triggered from `send_()`, the control record is finalized and
  sent using its own buffer before the application record is serialized.
- The application buffer keeps the application Tx invariants unchanged.
- Finalization is the same as handshake Tx: assert origin ownership, assert
  `pbuf.base == buf->data_()`, assert bounds, set `buf->skip = 0`, set
  `buf->length = pbuf.off`, and send exactly
  `[pbuf.base, pbuf.base + pbuf.off)`.
- After a re-key control record is serialized, `m_tx_seq_est` must be reset to
  match the new Tx traffic keys.

## Alerts

- Alerts use the same no-plaintext Tx buffer shape as handshake/control Tx.
- The alert Tx buffer comes from `txBuf()`.
- Alert finalization must preserve the same origin, base, bounds, `skip`, and
  `length` invariants as handshake Tx.

## Buffer Hook Contract

The picotls hook receives all buffer-growth requests. The origin-backed path is
used when `pbuf.origin` is non-null and points at a ztls `ZiIOBuf`.

For origin-backed growth:

- Reject requested alignment that exceeds `ZiIOBuf_Align`.
- Compute the base offset as `pbuf.base - buf->data_()` before calling
  `ZiIOBuf::ensure()`.
- Reject negative offsets or offset/capacity overflow.
- Ensure `offset + capacity` bytes in the `ZiIOBuf`.
- Re-read `buf->data_()` after `ensure()`, because the backing allocation may
  have moved.
- Restore `pbuf.base = buf->data_() + offset`.
- Set `pbuf.capacity = buf->size - offset`.
- Leave ownership with the `ZiIOBuf`.

For non-origin buffers, picotls owns the allocation. The hook may allocate
through `Zi::VHeap`, copy existing bytes, clear old bytes, and free old internal
storage as required by the picotls buffer contract.

## Maintainer Checklist

- Do not conflate `buf->data_()` and `buf->data()`.
- Do not let application Tx growth occur inside `ptls_send()`.
- Do not let application/control Rx growth occur inside `ptls_receive()`.
- Do not dispose origin-owned picotls buffers as internal picotls allocations.
- Keep handshake/control Tx separate from application Tx.
- Keep post-handshake control records out of `m_rxStream` unless picotls
  produces application plaintext.
- Keep selected AEADs compatible with exact in-place decrypt.
- Keep tests covering jumbo Rx/Tx, TLS 1.3 handshakes, TLS 1.2 record overhead,
  explicit-IV and no-explicit-IV ciphers, tailroom requirements, and forced
  post-handshake re-keying.
