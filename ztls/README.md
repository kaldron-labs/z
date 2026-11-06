# Ztls: TLS transport, cryptography and credential storage

Ztls supplies asynchronous TLS links, cryptographic primitives, key encoding,
HPKE, age-format encryption, TOTP and a credential Vault. It integrates
zpicotls/OpenSSL with Zi buffers and Rx/Tx scheduling. Include `<zlib/Ztls.hh>`
for transport or individual facility headers, and link `libZtls`. See
[DEPENDENCIES.md](../docs/DEPENDENCIES.md) for the required zpicotls backends and
OpenSSL capabilities.

## TLS clients and servers

The CRTP transport API parallels [Ztcp](../ztcp/README.md): derive an application
from `Ztls::Client<App>` or `Ztls::Server<App>` and define `CliLink`/`SrvLink`
types. Configure the multiplexer, Rx/Tx destinations, certificates, keys, trust
and ALPN before initiating connections. `connected(Ztls::Connected)` reports
handshake completion and negotiated information; consume plaintext through the
receive stream and emit plaintext through `txStream()` for encryption/output.

The application owns its buffer allocators. Supply the RxTLS allocator and a
primary TxTLS allocator in `HubParams` through `rxAllocator(rx)` and
`txAllocator(tx)`. Their concrete types match the link's `RxAllocator` and
`TxAllocator` declarations. The primary Tx allocator supplies handshake/control
output and the link's default Tx stream. Initialization registers both with the
underlying multiplexer; TLS does not manufacture producer policies.

Register additional application-owned TxTLS allocators with
`hub.allocator(allocator)`, mirroring `ZiMultiplex::allocator`. Registration
checks the TxTLS type and binds it to the hub's multiplexer. Each outgoing
buffer retains the allocator that created it, including when it is encrypted
in place or grows. Application output is not restricted to the primary factory.

```cpp
ZmRef<App::Link::RxAllocator> rx = new App::Link::RxAllocator{};
ZmRef<App::Link::TxAllocator> tx = new App::Link::TxAllocator{};
auto params = Ztls::ClientParams{&mx, "3", "4"}.hub([&rx, &tx](auto &p) {
  p.rxAllocator(rx.ptr()).txAllocator(tx.ptr());
});
bool initialized = app.init(ZuMv(params));

using OutputAllocator = Zi::TxTLSBufAllocator<ZiTxBuf, 4096>;
ZmRef<OutputAllocator> output = new OutputAllocator{};
bool registered = initialized && app.allocator(output.ptr());
// Once connected, reserve the link's TLS framing room before appending data.
if (registered) {
  auto buf = output->allocBuf();
  buf->skip = link.txHeadRoom<true>();
  buf->append(payload);
  link.send(ZuMv(buf));
}
```

Initialize and start the multiplexer before TLS hub initialization, which uses
its scheduler for TLS setup and any configured async event loop. Supply and
register allocators immediately on that setup path, before links or listeners
are published. Allocators stay dedicated to their multiplexer, and their owners
must outlive retained buffers. There are no ring-count arguments or first-send
registration. Additional Tx allocators need not share the primary allocator's
capacity, provided their buffers satisfy the link's TLS framing limits.

Configure `txErrorFn(ZiTxErrorFn{...})` before using the link. Queued protection
and validation failures notify on Rx. A `false` result requests close only when
the originating TLS generation is still current after the callback and has a
bound socket; stale failures remain diagnostic. Physical send failures notify
on I/O Tx and Zi closes the failed original socket independently of the result.
Invalid public buffer input is diagnostic only and can notify on the calling
thread. Dispatch application mutations to their owner shard instead of assuming
every transmit-error callback runs on Rx.

[ztlsclient.cc](example/ztlsclient.cc) is a complete client, including transmit
errors and session-resumption observations. The [example directory](example/)
and [integration guide](itest/README.md) demonstrate server/client lifecycles.
Use Zhttp for HTTP protocol handling rather than treating the raw TLS example
as a complete HTTP client.

Maintain Rx/Tx ownership, keep owners alive during pending handshake and I/O
work, and drain links before stopping the multiplexer. Session resumption and
early data are separate behaviors; inspect the configured policy and link
information rather than assuming a resumed connection admits early data.

Initialize a server, call `start()`, then `listen()`. `stop()` cancels listening
and drains native connections before `final()`. The bound address is retained,
including an OS-assigned port requested with zero. Late listener success during
stop closes the new socket; late failures/retries cannot reopen it. Start a
stopped hub again to reuse its configuration. After failed initialization, call
`final()` to release partially loaded credentials before correcting parameters
and reinitializing. Zfix's [integration suite](../zfix/itest/README.md) tests these
paths through TCP/TLS hubs, including explicit TLS trust/key failure cleanup.

## Hashes, MACs, randomness and secrets

| Header | Facility and usage |
| --- | --- |
| `ZtlsMD.hh` | `Ztls::MD<Algorithm>` incremental message digests |
| `ZtlsHMAC.hh` | `Ztls::HMAC<Algorithm>` keyed authentication |
| `ZtlsRandom.hh` | Random byte generation for cryptographic operations |
| `ZtlsSec.hh` | Constant-time byte comparison and secret-hash utilities |
| `ZtlsKEM.hh` | Key-encapsulation algorithm vocabulary and operations |
| `ZtlsCOSE.hh` | COSE-related key/signature handling |
| `ZtlsPK.hh` | Public/private key parsing and formatting |

Use the selected primitive's initialization, update and finalization operations
and check its result before consuming output. Buffer sizes and algorithm choices
are part of the API; avoid silently truncating key or digest material. The
[test directory](test/) contains concrete key, MAC and signature fixtures.
`ZtlsBackend` and `ZtlsPico` connect these facilities to the crypto/TLS backends;
most applications should use the public wrappers.

## HPKE, age and TOTP

`ZtlsHPKE.hh` exposes hybrid public-key encryption; its tests include
independent vectors and post-quantum cases. Preserve the sender/recipient
context and use the configured suite consistently on both sides.

`ZtlsAge.hh` supplies recipient/identity parsing, SSH key loading, and file
encryption/decryption. `ZtlsAge::encrypt` takes randomness, recipients, plaintext
and an output `ZiFile`; `decrypt` takes the file, identities and destination
storage. Check the result union: encryption returns success/error, while decryption
returns a byte count/error. See the
[test guide](test/README.md) for supported age/SSH cases and interoperability
with the independent age command-line tool.

`ZtlsTOTP.hh` supplies time-based one-time-password operations. Follow the
header and tests for secret, counter/time and digit parameters; applications
own enrollment, verification policy and replay/rate controls.

## Credential Vault

`Ztls::Vault` addresses secrets by `program` → `account` → `scope` → `name`.
The program defaults to `ZiLog::program()` (the executable's tail name), the
account to `<user>@localhost`, and scope to global. Choose a name describing
the credential's purpose, such as `"oauth"`.

`VaultConfig` selects identity and store policy. Stores include `Ephemeral`,
`File`, `KeyRing`, `Module` and `Auto`; variants include `Direct` and `Indirect`.
Call `init(config)` and `open()`, checking both results, then use
`save(scope, name, bytes)` and `load(scope, name, callback)`. Operations return
`VaultResult`, whose alternatives are success (`void`) and `ZeException`.
Call `close()` and `final()` when finished. The load callback receives secret
bytes with callback-scoped lifetime; consume
or deliberately copy them there rather than retaining a borrowed span.

Linux keyring integration uses Secret Service and Windows has its own platform
backend. The [Secret Service interoperability guide](interop/README.md)
explains isolated testing against GNOME Keyring without touching the desktop
keyring. [Vault tests](test/README.md) cover store variants and encrypted files.

## Examples and tests

Run `make -C ztls test` from a configured build tree. See
[test/README.md](test/README.md), [itest/README.md](itest/README.md) and
[interop/README.md](interop/README.md) for vectors, transport fixtures and
external peers. Environment and ports are centralized in [TESTING.md](../docs/TESTING.md).

Outgoing TLS links resolve hostnames asynchronously and consume the resolver's
terminal continuation before submitting a socket connect. Submitted DNS and TCP
operations complete normally; applications keep links, hubs and the multiplex
alive until their continuations and connection shutdown have drained. Stopping
prevents new setup work and closes a late successful socket through the ordinary
connection path. There is no protocol-imposed DNS/connect cancellation or native
setup deadline. Busy links reject overlapping connects before address/SNI mutation.
`connectRetired_()` starts protocol owner drain after pre-readiness failure;
`connectFailed()` reports native failures. Reconnect timers retain the owner and
ignore queued callbacks from earlier timer generations.


---

## Ztls / zpicotls Buffer Handling

This document is the maintainer reference for how `ztls` supplies buffers to
zpicotls and how zpicotls is allowed to use them. The rules below are invariants:
code that cannot satisfy them should fail with a diagnostic or `ZiAssert`, not
try to repair the buffer after the fact.

The implementation relies on a local zpicotls buffer hook installed by
`Ztls::Pico::install()`. That hook lets zpicotls grow ztls-owned `ZiIOBuf`
instances without copying through zpicotls' internal heap, while preserving
zpicotls' `ptls_buffer_t` contract.

### Data Model

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
- `ptls_buffer_t::base` is the first byte of the output region zpicotls writes
  into.
- `ptls_buffer_t::off` is the number of valid output bytes starting at
  `pbuf.base`.
- `ptls_buffer_t::capacity` is the writable byte count starting at `pbuf.base`.
- `ptls_buffer_t::origin` is ztls' ownership marker. For every ztls-owned
  output buffer, `pbuf.origin == buf.ptr()`.

### Shared Invariants

- Origin-backed output is zero-copy and remains owned by the `ZiIOBuf`.
  Ztls must not call `ptls_buffer_dispose()` for bytes owned by `ZiIOBuf`.
- For every ztls-owned `ptls_buffer_t`, `pbuf.base` must point inside the same
  `ZiIOBuf` allocation as `pbuf.origin`.
- Bounds are relative to the actual `pbuf.base`:
  `pbuf.off <= buf->size - (pbuf.base - buf->data_())`.
- A ztls-owned `ptls_buffer_t` returning as an internal zpicotls allocation, or
  returning a base outside its `ZiIOBuf`, is a contract violation.
- The origin allocation hook must preserve `pbuf.base - buf->data_()` across
  `ZiIOBuf::ensure()`.
- If `pbuf.off` is non-zero during origin-backed growth, the logical valid
  range `[pbuf.base, pbuf.base + pbuf.off)` must be preserved after relocation.
- The hook must update `pbuf.capacity` relative to the preserved base offset.
- The hook must not reset `pbuf.base` to `buf->data()`. `buf->data()` is
  path-dependent: application Tx uses it as the plaintext pointer, while the
  TLS record base remains `buf->data_()`.
- When zpicotls requests `align_bits`, the buffer base passed to zpicotls for
  that operation must satisfy the requested alignment.
- `ZiIOBuf_Align` must be sufficient for the fusion AEADs selected by ztls.

### AEAD Constraints

Ztls application Rx decrypts in-place: the ciphertext record and plaintext
output are different regions within the same `ZiIOBuf`. A selected AEAD must
support exact in-place decrypt for that layout.

The `ptls_non_temporal_*` fusion AEADs are not valid for this invariant because
their decrypt path can write plaintext before it has finished reading the
ciphertext and authentication tag. Ztls therefore uses normal fusion AEADs when
fusion is enabled, and excludes the non-temporal variants.

### Handshake Rx

- Before `ptls_handshake_is_complete(m_tls)`, incoming records are routed to
  `handshake_()`, not `rcvd_()`.
- `ZiRx` supplies each complete record as an owning `ZmRef<ZiIOBuf>`.
  Its active `data()` and `length` identify the record passed to the handshake.
- `ptls_handshake(m_tls, &pbuf, input, inlen, &m_props)` receives the incoming
  record base as `input`.
- The incoming record is read-only for ztls. It is not converted into
  application plaintext.
- The `pbuf` supplied to `ptls_handshake()` is a handshake Tx buffer.
  Handshake Rx does not use `rxBuf()` and does not publish to `m_rxStream`.
- `*inlen` is zpicotls' consumed-input count for the handshake call. It is not a
  plaintext length and must not be used to adjust application-visible data.

### Allocator lifetime

The application chooses and owns the Rx and primary Tx policies. The hub retains
those supplied allocators, and the multiplexer retains all registered policies.
Additional Tx policies are registered directly, without a per-type or per-thread
policy registry. Links use the primary Tx policy for their own control output;
application buffers retain their own registered allocator. Stop/finalization and
retained-buffer release must complete before allocator owners are destroyed.

### Handshake Tx

- Handshake Tx uses `txBuf()`.
- `rxTxBuf_()` and `txBuf_()` allocate through the supplied primary Tx TLS
  allocator on their respective producers. Buffers grow to
  `TxRecordCapacity` when necessary; each starts with `buf->skip == 0`.
- The handshake record base is `buf->data_()`. Since `skip == 0`, this is also
  `buf->data()`.
- Ztls initializes zpicotls with
  `ptls_buffer_init_tx(&pbuf, buf->data_(), TxRecordCapacity)`, then sets
  `pbuf.origin = buf.ptr()` and `pbuf.align_bits`.
- `ptls_handshake()` may write serialized handshake records into `pbuf`.
- After the call, Tx finalization must assert origin ownership, assert
  `pbuf.base == buf->data_()`, assert bounds, then send with `buf->skip = 0`
  and `buf->length = pbuf.off`.

### Application Rx

`ZiRx` frames a complete contiguous TLS record before handing its buffer to the
TLS Rx owner. Trailing records are preserved by the existing receive helper.
After handshake completion, `rcvd_()` passes the record to `ptls_receive()` and
writes plaintext into the same buffer after the negotiated record headroom.
The full record must be consumed. The buffer then advances past the headroom,
sets its length to the plaintext size and enters the application `RxStream`.
No retained-range or slice metadata is involved.

### Application Tx

Application output reserves `txHeadRoom()` and `txTailRoom()` within
`TxRecordCapacity`. `txStream()` hands completed buffers to the TLS Tx owner;
`txStream_()` performs the same work directly on that owner. Encryption uses
`ptls_tx_send()` in the buffer that will be submitted to `ZiTx`, retaining the
existing origin, base and capacity checks. `ZiTxContext::offset` tracks partial
socket sends without modifying the buffer's published bytes. Handshake and
control output use their existing separate producer paths.

### Post-Handshake Control Rx

- After the handshake is complete, TLS 1.3 control messages such as KeyUpdate
  arrive through the same framed network path as application records.
- Control messages are processed by `ptls_receive()`.
- The input-side invariants are the same as application Rx: `buf->data()` is
  the TLS record base and `buf->skip == 0`.
- The output-side invariants are also the same as application Rx:
  `pbuf.origin == buf.ptr()` and `pbuf.base == base + m_headroom`.
- The no-growth-before-`ptls_receive()` invariant is also the same as
  application Rx: the `ZiIOBuf` must be sized to cover the wire-record length
  plus `m_headroom` before zpicotls parses and processes the record.
- A pure post-handshake control message does not publish application plaintext.
  Successful processing with `pbuf.off == 0` must not push the buffer to
  `m_rxStream`.
- If a received KeyUpdate requires a reciprocal update, that requirement must be
  represented in ztls state before the next application Tx record is emitted.
- `ptls_receive()` must consume the entire control record. A partial `inlen`
  result is a protocol or framing failure for this path.

### Post-Handshake Control Tx

- Local or reciprocal re-keying Tx uses the no-plaintext control-message shape.
- `ptls_update_key(m_tls, request_update)` updates zpicotls state.
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

### Alerts

- Alerts use the same no-plaintext Tx buffer shape as handshake/control Tx.
- The alert Tx buffer comes from `txBuf()`.
- Alert finalization must preserve the same origin, base, bounds, `skip`, and
  `length` invariants as handshake Tx.
- A generated fatal handshake alert must finish sending before socket shutdown.
  The existing I/O producer closes after that actual buffer completes; an
  already completed DATA send may shut down while the backend retains any
  outstanding kernel notification holds.

### Buffer Hook Contract

The zpicotls hook receives all buffer-growth requests. The origin-backed path is
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

For non-origin buffers, zpicotls owns the allocation. The hook may allocate
through `Zi::VHeap`, copy existing bytes, clear old bytes, and free old internal
storage as required by the zpicotls buffer contract.

### Maintainer Checklist

- Do not conflate `buf->data_()` and `buf->data()`.
- Do not let application Tx growth occur inside `ptls_send()`.
- Do not let application/control Rx growth occur inside `ptls_receive()`.
- Do not dispose origin-owned zpicotls buffers as internal zpicotls allocations.
- Keep handshake/control Tx separate from application Tx.
- Keep post-handshake control records out of `m_rxStream` unless zpicotls
  produces application plaintext.
- Keep selected AEADs compatible with exact in-place decrypt.
- Keep tests covering jumbo Rx/Tx, TLS 1.3 handshakes, TLS 1.2 record overhead,
  explicit-IV and no-explicit-IV ciphers, tailroom requirements, and forced
  post-handshake re-keying.
