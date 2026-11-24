# Embedded TLS 1.3 engine

The TLS 1.3 engine and its backends are built directly into `libZtls` from
the sources and headers in `ztls/src`. Retained source files carry their
upstream copyright and license notices. The portions of
Fusion identified in its source header also carry the Apache 2.0 terms.
[Apache 2.0 license](LICENSE.Apache-2.0)

| Component | Sources and headers |
| --- | --- |
| TLS protocol and engine API | `ZtlsTLS13.cc`, `ZtlsEngine.hh` |
| OpenSSL backend | `ZtlsEngineOpenSSL.cc`, `ZtlsEngineOpenSSL.hh` |
| Fusion backend | `ZtlsFusion.cc`, `ZtlsFusion.hh` |
| HPKE | `ZtlsEngineHPKE.cc` |
| Certificate compression | `ZtlsCertCompression.cc`, `ZtlsCertCompression.hh` |
| Connection state, packet storage and credentials | `ZtlsCxn.hh`, `ZtlsBuf.hh`, `ZtlsCredentials.hh` / `.cc` |

Backend implementations live in `ztls/src/engine`; installed headers live in
`ztls/src`. No external TLS engine checkout, headers, or libraries are required.

Transport clients include `<zlib/Ztls.hh>` and link `libZtls`. The
engine's backend descriptors and implementation are part of that library.
Fusion compilation uses per-translation-unit ISA flags, and backend selection
checks CPU capabilities. Brotli is used when available for TLS certificate
compression. OpenSSL libcrypto remains a native dependency.

The engine exposes native C++ types in `Ztls_`. `CipherAlgo`, `AeadAlgo`,
`HashAlgo` and `KeyExchangeAlgo` create complete derived contexts on concrete,
named heaps. Contexts use virtual operations and destructors. Algorithm catalogs
hold borrowed pointers or references to complete provider objects; they never
slice a polymorphic descriptor. Borrowed byte ranges use `Bytes` (`ZuSpan<uint8_t>`).

`Context` borrows its random source, clock and other typed hooks. Their owners
must outlive all connections that use them. Credential holders own native keys
and certificate stores. Certificate verification transfers a `VerifySignature`
object to `Ztls::Cxn`; verification, failure, reset and teardown retire it exactly
once. Hash finalization leaves ownership with the caller; HMAC rejects snapshot
mode with `Ztls_ErrorNotAvailable` and does not support cloning.

Fusion GCM storage inherits its context interface. Each width owns its GHASH
array, implements growth and setup, and clears its table before release. Expanded
AES keys and serial state are cleared on destruction. SIMD load/store casts do
not recover enclosing objects from a presumed prefix layout.

OpenSSL async/RSA entry points and ticket-key callback parameters retain their
native C signatures. They are adapters at the native boundary; no engine object
stores a C dispatch table. Existing framework continuations retain their async
scheduling and retirement contracts. The `ZtlsVaultStore` module factory retains
its C linkage and ABI.

Connection and transmission state live in `Ztls::Cxn`. The TLS wire format,
ticket encoding and TLS 1.3 legacy version fields retain their protocol meaning.

The application link owns scheduling, dispatch, and socket lifecycle.
The protocol operates on its connection fields and on buffers directly
owned or reserved through Zi allocators. Use the Ztls transport interface
for socket-based TLS and the standalone crypto headers for primitive
operations.

Zquic drives the same handshake with `rawHandshake` and uses
`rawReadEpoch` to query the expected input level. Zquic owns its
connection on Rx, supplies ordered CRYPTO bytes, and retains the output
storage. The driver writes complete handshake messages directly into the
supplied span and returns the five boundaries for the four epoch slots.
The 0-RTT slot is empty. A terminal output failure leaves the failed
flight unpublished and the caller retires the handshake.
On a stateless HelloRetryRequest, the raw driver returns the Initial
HelloRetryRequest range with `Ztls_ErrorStatelessRetry`; the caller sends it
and recreates the connection for ClientHello2 using the same cookie key.
Ticket resumption is available after stateful or stateless HelloRetryRequest.
The retry transcript is retained while the ticket replaces the empty early
secret before validating the second ClientHello binder. Early data is rejected
after either retry.

The raw driver reports traffic secrets through the handshake callback.
Zquic owns packet protection, epoch retirement, and packet key updates.
The driver rejects TLS KeyUpdate on the QUIC path and carries session
tickets in the Application epoch.
