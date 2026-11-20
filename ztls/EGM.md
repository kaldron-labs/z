# Ztls

## Overview

TLS transport over the framework multiplexer, with cryptographic primitives,
age encryption, and secret storage. Ztls embeds its TLS 1.3 engine and uses
OpenSSL libcrypto.

The engine includes OpenSSL and Fusion backends and supplies the raw handshake
API used by Zquic. See the [engine notes](./src/engine/README.md).

## Subdirectories & Boundaries

- [Library](./src/EGM.md): transport, keys, crypto adapters, vault stores, and the installed `ztotp` utility.
- [Examples](./example/EGM.md): TLS client/server programs with explicit I/O and TLS scheduler threads.

Verification boundaries are documented separately in the
[unit test guide](./test/README.md),
[integration guide](./itest/README.md), and
[interoperability guide](./interop/README.md).

## Interface

- [Makefile.am](./Makefile.am) traverses `src`, `test`, `itest`, `example`, and
  `interop`. [src/Makefile.am](./src/Makefile.am) exports `libZtls.la`, public
  headers, and `ztotp`; `libZtlsFusion.la` is an internal build library.
- [Ztls.hh](./src/Ztls.hh): CRTP `Link`, `CliLink`, `SrvLink`, `Hub`, `Client`,
  and `Server` provide socket transport, scheduling, and application callbacks.
  [ZtlsCxn.hh](./src/ZtlsCxn.hh) holds `Ztls::Cxn` protocol state;
  [ZtlsBuf.hh](./src/ZtlsBuf.hh) supplies record and packet storage adapters.
- [ZtlsEngine.hh](./src/ZtlsEngine.hh): `Ztls_::Context`, `rawHandshake`,
  `RawHandshakeResult`, and `rawReadEpoch` expose the shared engine. The raw
  caller supplies ordered CRYPTO input at one epoch and owns output storage;
  five offsets delimit Initial, 0-RTT, Handshake, and Application ranges.
  [ZtlsCredentials.hh](./src/ZtlsCredentials.hh) exposes certificate stores,
  signing/verification hooks, and session-ticket keys.
- Standalone facilities include `Ztls::PK` keys and operations,
  [ChaCha20Poly1305](./src/ZtlsAEAD.hh),
  [HPKE::sealBase/openBase](./src/ZtlsHPKE.hh),
  [ZtlsAge](./src/ZtlsAge.hh), and
  [secretHash/secretVerify](./src/ZtlsSec.hh). See the library contract for the
  complete primitive and encoding index.
- [ZtlsVault.hh](./src/ZtlsVault.hh): `Vault` exposes
  `init/open/load/save/close/final`; configuration selects
  Ephemeral/File/KeyRing/Module/Auto storage and Direct/Indirect mode.

## Invariants

- `Hub::validate_` and `Hub::init_` require compatible allocators bound to the
  same multiplexer and distinct TLS Rx/Tx threads. Automatic socket allocator
  provisioning requires a stopped multiplexer; `Hub::final` requires a stopped
  engine. Pending DNS, socket setup, and async jobs must retire while their
  hub and multiplexer remain alive. [Ztls.hh](./src/Ztls.hh)
- Rx owns handshake and receive state; `handoffTx` transfers outbound state to
  Tx before the application sees `connected()`. Queued output is fenced by the
  originating connection and generation. [Ztls.hh](./src/Ztls.hh),
  [ZtlsCxn.hh](./src/ZtlsCxn.hh)
- TLS input keeps its original backing. `PlaintextSink` keeps distinct RxTLS
  plaintext private until authentication succeeds. TxTLS plaintext and TxTCP
  ciphertext use separate allocations; `RecordSink` reserves application
  record packets before protection/publication. Packet boundaries do not create
  separate TLS records. [ZtlsBuf.hh](./src/ZtlsBuf.hh)
- `Ztls_::Context` borrows typed hooks and provider catalogs; their owners must
  outlive connections using them. Raw-handshake consumers own packet protection
  and epoch retirement. [ZtlsEngine.hh](./src/ZtlsEngine.hh),
  [engine notes](./src/engine/README.md)
- `Vault::final` requires a closed store. Load callbacks borrow mutable data
  only for the callback's duration. Direct File storage is plaintext JSON;
  Auto can fall back to File. Indirect storage uses a backend passphrase and
  age-encrypted account files. [ZtlsVault.cc](./src/ZtlsVault.cc)

## Side Effects

- Transport resolves endpoints, opens sockets/listeners, schedules owner work,
  logs errors, and loads certificates and keys. Stream destruction can flush
  pending output. [Ztls.hh](./src/Ztls.hh)
- `Ztls::cryptoInit` loads OpenSSL configuration. Primitive operations allocate
  native contexts and use backend randomness; `ZtlsAge::encrypt/decrypt` write
  or read caller-supplied files. [ZtlsLib.cc](./src/ZtlsLib.cc),
  [ZtlsAge.hh](./src/ZtlsAge.hh)
- Vault operations create/read local files, may load a shared-library store,
  and may access native credential services and unlock prompts. Linux KeyRing
  uses D-Bus Secret Service; Windows uses the credential API.
  [ZtlsVault.cc](./src/ZtlsVault.cc),
  [ZtlsVaultLinux.cc](./src/ZtlsVaultLinux.cc),
  [ZtlsVaultWindows.cc](./src/ZtlsVaultWindows.cc)
- `ztotp` writes a code to stdout or errors to stderr. The example client
  overwrites `index.hdr` and `index.html`; both examples open network sockets.
  [ztotp.cc](./src/ztotp.cc), [example contract](./example/EGM.md)

## Verification

From a configured repository root, build dependencies and test binaries with
`make -j8`, then run `make -C ztls/test test` (or `make -C ztls test`, whose
test recipe delegates to the unit suite).

Integration verification is separate: `make -C ztls/itest test` runs the Linux
isolated Secret Service and kernel TCP packet tests. The bus fixture requires
`zdbus/util/zdbusbus`; build it with `make -C zdbus/util -j8` if needed.
`make -C ztls/interop test` runs the Linux GNOME Keyring interoperability test,
which skips when its external daemons are unavailable. `ztlsageinterop` is a
separate optional build target. See the respective
[unit](./test/Makefile.am), [integration](./itest/Makefile.am), and
[interop](./interop/Makefile.am) manifests.

Build and test commands were not run for this documentation-only update.
