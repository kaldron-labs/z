# Ztls: TLS transport, cryptography and credential storage

Ztls supplies asynchronous TLS links, cryptographic primitives, key encoding,
HPKE, age-format encryption, TOTP and a credential Vault. It integrates
zpicotls/OpenSSL with Zi buffers and Rx/Tx scheduling. Include `<zlib/Ztls.hh>`
for transport or individual facility headers, and link `libZtls`. See
[DEPENDENCIES.md](../DEPENDENCIES.md) for the required zpicotls backends and
OpenSSL capabilities.

## TLS clients and servers

The CRTP transport API parallels [Ztcp](../ztcp/README.md): derive an application
from `Ztls::Client<App>` or `Ztls::Server<App>` and define `CliLink`/`SrvLink`
types. Configure the multiplexer, Rx/Tx destinations, certificates, keys, trust
and ALPN before initiating connections. `connected(Ztls::Connected)` reports
handshake completion and negotiated information; consume plaintext through the
receive stream and emit plaintext through `txStream()` for encryption/output.

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
external peers. Environment and ports are centralized in [TESTING.md](../TESTING.md).

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
