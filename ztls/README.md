# Ztls: TLS transport, cryptography and credential storage

Ztls supplies asynchronous TLS links, cryptographic primitives, key encoding,
HPKE, age-format encryption, TOTP and a credential Vault. `libZtls` includes
the TLS 1.3 engine and its OpenSSL and Fusion backends. Include
`<zlib/Ztls.hh>` for transport or individual facility headers and link
`libZtls`. OpenSSL libcrypto supplies native cryptographic operations.

The same engine drives QUIC through the raw handshake API in
`<zlib/ZtlsEngine.hh>`. The caller owns the connection and output storage.
The engine reports output ranges by encryption level and delivers traffic
secrets to the caller. See the [engine notes](src/engine/README.md) for the
raw output contract.

## TLS clients and servers

The CRTP transport API parallels [Ztcp](../ztcp/README.md): derive an application
from `Ztls::Client<App>` or `Ztls::Server<App>` and define `CliLink`/`SrvLink`
types. Configure the multiplexer, Rx/Tx destinations, certificates, keys, trust
and ALPN before initiating connections. `connected(Ztls::Connected)` reports
handshake completion and negotiated information; consume plaintext through the
receive stream and emit plaintext through `txStream(allocator)` for encryption/output.
Use `txStream_(allocator)` on the TLS Tx thread.

The hub accepts an RxTLS plaintext allocator through `rxAllocator(rx)`.
`Ztls::RxBufAlloc<>` names its ordinary builtin/heap policy. It must supply
`Ztls_::RxRecordCapacity`, enough for a full 16 KiB record plus inner content
type and padding; initialization rejects insufficient capacity.

Each application Tx stream receives its own plaintext allocator explicitly.
The same link can use several allocators, and adopted buffers retain their
original allocator until encryption completes. `Ztls::TxBufAlloc<>` names a
convenient plaintext policy; it is not a Link template parameter or hub property.
Plaintext records hold up to 16 KiB without a TLS header/tag reserve.

The hub owns `txPktAllocator`, obtained internally from the multiplexer.
Its `TxPktAlloc` policy defaults to TxTCP packet storage and can be specialized
by the application type for memory geometry. TLS receive preparation through
`Link::rxAllocator()` or `Ztls::rxAllocator<Policy>()` prepares that internal
registration while the multiplexer is stopped. Native plugins provide registered
packet backing; later hubs reuse an existing matching registration.
Packet capacity comes from the allocator, while each connection supplies its
MPS. Actual sends reserve spans using both bounds. Handshake, alerts and other
TLS control output require no application plaintext Tx allocator.

All TLS wire producers, including handshake, alerts and KeyUpdate, encrypt
serially into packet spans. A record may span many packets without adding TLS
headers, sequence numbers or tags at packet boundaries. Application output
reserves every packet, including a pending KeyUpdate prefix, before
protection or publication. Exhaustion releases the reservation and disconnects
the originating generation without consuming application record cipher state.
A failed handshake/control record is unpublished and terminates that session;
earlier complete records of its flight may have been sent.

For hubs initialized after explicit allocation or startup, prepare TLS Rx
storage through one of these helpers and retain a packet `Rx` allocator for
`rxPktAllocator()`. Obtain the handles before `alloc()` or `start()` and reuse
them across dynamic hubs. Initial hub setup can register its default receive
packet allocator itself. The hub passes that handle to each endpoint setup.

`ZiRx::recv<Body>()` allocates and admits each transport input chunk, then
transfers its original backing to TLS Rx with reception disarmed. Ztls's
`processRx_()` drives record processing and application delivery, requesting the
next chunk only after consuming the current input. Fragmented records decrypt
incrementally into private RxTLS storage without gathering ciphertext.

```cpp
auto rx = App::Link::rxAllocator(&mx);
auto rxPkt = mx.allocator<Zi::IOBufType::Rx, ZiRxQueue::Node, Ztls_::RxPacketCapacity>();
auto output = Ztls::allocator<Ztls::TxBufAlloc<>>(&mx);
auto other = mx.allocator<Zi::IOBufType::App, ZiIOBuf, 4096>();
auto params = Ztls::ClientParams{&mx, "3", "4"}.hub([rx, rxPkt](auto &p) {
  p.rxAllocator(rx).rxPktAllocator(rxPkt);
});
if (!app.init(ZuMv(params)) || !mx.start()) return false;
// Once connected, choose an allocator for each application writer.
{
  auto tx = link.txStream(output);
  tx << payload << Zi::flush();
  if (tx.failed()) link.disconnect();
}
{
  auto tx = link.txStream(other);
  tx << morePayload << Zi::flush();
  if (tx.failed()) link.disconnect();
}
```

Create all allocators while the multiplexer is stopped, then start it before
publishing links or listeners. Allocators must outlive their streams and retained
buffers. A formatting stream grows plaintext storage through its allocator as
needed; allocator selection and first sends never register policies implicitly.
Keep owners alive through pending I/O and drain links before stopping the multiplexer.

`sent(buf, ok)` reports each ciphertext packet through the existing ZiTx
completion contract. Forward overrides to the base hook. A dependent retaining
plaintext identity for completion may use `txPacketized(plaintext, lastPacket)`
on the TLS Tx owner before publication to track the final ciphertext packet.
There is no record completion aggregator. TCP retains ciphertext and owns
partial writes and retransmission at their original TCP positions; TLS never
re-encrypts or appends an accepted prefix for retry.

Check a `ZiTxStream` writer's `failed()` after its final flush and disconnect on
failure. ZiTx queues output up to `maxQueueLen()` (1024 by default); rejection
reports `sent(buf, false)` and closes the original physical connection. Deferred
protection/reservation failures fence the Tx generation and schedule teardown
on Rx. A configured `txErrorFn` observes these errors; disconnect/recovery is
mandatory for failed TLS output. Queued setups after close release their packet
references through the original allocator. Physical socket failures follow
ZiConnection's existing close path.

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
and drains native connections and registered signature completions before
`final()`. The bound address is retained,
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
| `ZtlsPKOps.hh` | Concrete `PK::Key` EVP key ownership and shared key operations |
| `ZtlsKDF.hh` | HKDF/SHA256 and scrypt |
| `ZtlsAEAD.hh` | Reusable ChaCha20-Poly1305 encryption and authentication |

Use the selected primitive's initialization, update and finalization operations
and check its result before consuming output. Buffer sizes and algorithm choices
are part of the API; avoid silently truncating key or digest material. The
[test directory](test/) contains concrete key, MAC and signature fixtures.
Standalone cryptography uses OpenSSL libcrypto directly.
`Ztls::cryptoInit()` reports initialization failure through its return value.
`Ztls::init()` reports it through `ZeException`. Both share the same
thread-safe native initialization.

MD/HMAC owners cannot be copied or moved. Digest finalization takes a snapshot;
HMAC finalization resets to the same key. Streaming failures throw `ZeException`
and invalidate the operation; recover with MD `reset()` or HMAC `start()`.
HKDF and AEAD report failure through `bool` and clear the intended output.
`PK::Key` embeds native EVP key ownership; imports replace a key only on success.
Use the concrete crypto operations for standalone work and the
`Ztls::Client` and `Ztls::Server` interfaces for transport.

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

The existing `Ztls::TOTP::calc` in `src/ZtlsTOTP.cc` interprets the four selected
HMAC bytes as a native-endian integer (`ZuPun`), rather than the standard's
big-endian truncation. Its results on little-endian targets are retained by the
direct-libcrypto migration. Correcting truncation is a separate behavioral
change; HMAC-SHA1 counter vectors establish primitive correctness, not TOTP
conformance.

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


## Receive buffers and authentication

The transport callback transfers the original Rx buffer and its readable
range to the TLS Rx owner. Kernel buffers use packet-sized builtin backing;
plugins may attach registered backing to an initially backingless context stub.
Builtin and stub-backed reception use the same Ztls processing path without
branching on the multiplexer plugin. The callback preserves headroom and actual allocator capacity, rearms the receive
context using the existing notification contract, and never grows input to the
TLS record length.

The engine parses each TLS 1.3 record incrementally. It retains bounded header,
tag, and AEAD carry between chunks and decrypts directly from the
input span into one distinct, stable RxTLS allocation. No whole ciphertext record
is gathered or copied into that allocation. The destination stays private until
authentication succeeds; failure clears/discards provisional plaintext and
terminates the originating connection. TLS 1.3 padding and inner content type
are interpreted only after authentication.

After the receive operation returns, authenticated application data transfers
its existing buffer reference into `ZiRxStream`. Records remain separate stream
buffers, while application message framing may span records. Empty application
records need no callback. Handshake and post-handshake records use the existing
TLS state machine and receive-key transitions; any resulting output uses the
serial Tx packet sink. Async work and cross-owner handoffs retain the originating
connection/generation, and teardown drains those owners before releasing state.

## Transmit buffers

TxTLS holds unregistered plaintext. The serial Tx sink reserves all packet
backing for an application record before encryption, then writes directly into
TxTCP spans, preserving allocator headroom and the connection payload cap.
Handshake, alerts and KeyUpdate use the same packet sink. Packet boundaries do
not restart protection or create additional TLS headers/tags. Retained packets
remain immutable through partial writes, retransmission and completion.

Rx owns handshake state and receive keys. During the handshake, Rx also uses
the outbound members. At handoff, Tx takes ownership of the same cache-line
aligned members in `Ztls::Cxn`. The connection generation and physical
destination are checked before queued sends, rekeys, and close alerts.
The receive owner retains the handshake transcript needed for tickets and
exported secrets. Pending signature digest and provider state remain on their
originating connection until completion or retirement.

See the [engine migration map](src/engine/README.md) for consumers moving their
low-level includes or build dependencies to this version.
