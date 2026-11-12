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

The application selects its buffer allocator policies; the multiplexer creates
and owns the allocator instances. `Ztls::RxBufAlloc<>` and `Ztls::TxBufAlloc<>`
name policies, whose `Buf` members name queue-node headers. Supply explicit
RxTLS, TxTLS plaintext and TxTCP packet allocators through `rxAllocator(rx)`,
`txAllocator(tx)` and `txPacketAllocator(packets)`. TxTLS and RxTLS always use
ordinary builtin/heap storage. RxTLS reserves `Ztls_::RxRecordCapacity`, enough for
a full 16 KiB application record plus the supported TLS 1.3 inner content type
and padding. Custom RxTLS geometry must supply that capacity before traffic;
initialization rejects insufficient storage. TxTLS holds a complete application
record (up to 16 KiB) and has no TLS header/tag reservation. TxTCP supplies distinct ciphertext
backing, registered when provided by the multiplexer plugin. Its fixed provider
geometry reserves lower-layer headroom and tailroom; each connection passes its
MPS as the packet payload cap. Create all selections through the stopped
multiplexer before hub initialization. TLS validates their roles and common mx.
After provider activation and negotiation, the link reserves and releases the
maximum record plus KeyUpdate budget using its physical MPS before publishing
`connected()`. A pool unable to hold that budget terminates the session.

All TLS wire producers, including handshake, alerts and KeyUpdate, encrypt
serially into packet spans. A record may span many packets without adding TLS
headers, sequence numbers or tags at packet boundaries. Detached application
output reserves every packet, including a pending KeyUpdate prefix, before
protection or publication. Exhaustion releases the reservation and disconnects
the originating generation without consuming application record cipher state.
A failed handshake/control record is unpublished and terminates that session;
earlier complete records of its flight may have been sent.

For hubs initialized after the multiplexer starts, also supply a prepared
`RxTCP` allocator with `socketRxAllocator()`. Create it through the same
stopped multiplexer and reuse it across dynamic hubs. Cold hub initialization
creates a missing socket allocator through the multiplexer factory. Its builtin
capacity follows local receive geometry, independent of the peer MSS or outbound
MPS; smaller input chunks remain valid.

Create additional TxTLS allocators through `mx.allocator<Zi::IOBufType::TxTLS, ZiTxBuf, Size>()`, then
supply them to `hub.allocator(allocator)`. The hub checks the TxTLS type and
existing multiplexer binding. Each outgoing
plaintext buffer retains the allocator that created it until encryption completes. Application output is not restricted to the primary factory.

```cpp
auto rx = App::Link::rxAllocator(&mx);
auto tx = App::Link::txAllocator(&mx);
auto packets = App::Link::txPacketAllocator(&mx, 1024);
auto socketRx = mx.allocator<Zi::IOBufType::RxTCP, ZiRxQueue::Node, Ztls_::RxPacketCapacity>();
auto params = Ztls::ClientParams{&mx, "3", "4"}.hub([rx, tx, packets, socketRx](auto &p) {
  p.rxAllocator(rx).txAllocator(tx).txPacketAllocator(packets)
    .socketRxAllocator(socketRx);
});
bool initialized = app.init(ZuMv(params));

auto output = mx.allocator<Zi::IOBufType::TxTLS, ZiTxBuf, 4096>();
bool registered = initialized && app.allocator(output);
if (registered && !mx.start()) return false;
// Once connected, append application plaintext.
if (registered) {
  auto buf = output->allocBuf();
  buf->append(payload);
  link.send(ZuMv(buf));
}
```

Construct the multiplexer before TLS hub initialization, which uses its
scheduler for TLS setup and any configured async event loop. Create and
register all allocators while the multiplexer is stopped, then start it before
publishing links or listeners. Allocators stay dedicated to their multiplexer,
which must outlive retained buffers. Allocator selection and first sends do not
register policies implicitly. Additional Tx allocators need not share the primary allocator's
capacity; a stream grows plaintext storage through the normal heap path as needed.

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


## Receive buffers and authentication

The transport callback transfers the original RxTCP buffer and its readable
range to the TLS Rx owner. Kernel buffers use packet-sized builtin backing;
plugins may attach registered backing to an initially backingless context stub.
Builtin and stub-backed reception use the same Ztls processing path without
branching on the multiplexer plugin. The callback preserves headroom and actual allocator capacity, rearms the receive
context using the existing notification contract, and never grows input to the
TLS record length.

zpicotls incrementally parses each header, explicit IV, ciphertext and tag. It
retains bounded parser/AEAD carry between chunks and decrypts directly from the
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

The legacy zpicotls buffer hook remains available to contiguous callers. It
preserves origin ownership and base offsets when growing an origin-backed
buffer; it is not the transport's incremental receive destination mechanism.
