# Embedded TLS 1.3 engine

These sources were moved from Z's zpicotls migration input. Retained source
files carry their upstream copyright and license notices. The portions of
Fusion identified in its source header also carry the Apache 2.0 terms.
[Apache 2.0 license](LICENSE.Apache-2.0)

| Former location | Ztls location |
| --- | --- |
| `lib/picotls.c` | `ztls/src/engine/tls13.cc` |
| `lib/openssl.c` | `ztls/src/engine/openssl.cc` |
| `lib/fusion.c` | `ztls/src/engine/fusion.cc` |
| `lib/hpke.c` | `ztls/src/engine/hpke.cc` |
| `lib/certificate_compression.c` | `ztls/src/engine/certificate_compression.cc` |
| `<zpicotls.h>` | `<zlib/ZtlsEngine.hh>` |
| `<zpicotls/openssl.h>` | `<zlib/ZtlsEngineOpenSSL.hh>` |
| `<zpicotls/fusion.h>` | `<zlib/ZtlsFusion.hh>` |
| `ZtlsPico.hh` / `ZtlsPico.cc` | Connection state in `ZtlsCxn.hh`, direct packet storage in `ZtlsBuf.hh`, and credential and ticket setup in `ZtlsCredentials.hh` / `.cc` |

Transport clients include `<zlib/Ztls.hh>` and link `libZtls`. The
engine's backend descriptors and implementation are part of that library.
Fusion compilation uses per-translation-unit ISA flags, and backend selection
checks CPU capabilities. Brotli is used when available for TLS certificate
compression. OpenSSL libcrypto remains a native dependency.

Ordinary retained `ptls_*` names are members of `Ztls_` without the prefix.
Retained `PTLS_*` macros use `Ztls_*`. Borrowed byte ranges use
`ZuSpan<uint8_t>` through the engine's `Bytes` vocabulary. Connection and transmission
state live in `Ztls::Cxn`. There is no detached Tx allocation or process-global
buffer hook. A consumer moving low-level code should re-evaluate its
allocation and thread ownership alongside renaming. The existing ticket
encoding and the TLS 1.3 legacy version fields are part of the protocol
behavior this migration preserves.

The application link owns scheduling, dispatch, and socket lifecycle.
The protocol operates on its connection fields and on buffers directly
owned or reserved through Zi allocators. Use the Ztls transport interface
for socket-based TLS and the standalone crypto headers for primitive
operations. Zquic still uses its existing zpicotls-based integration.
