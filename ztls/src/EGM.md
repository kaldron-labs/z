# Ztls transport, cryptography, and vault library

## Overview

TLS 1.3 links over ZiMultiplex, an embedded TLS engine with OpenSSL and Fusion, public-key cryptography, age file encryption, and platform-backed secret stores.

## Interface

- CRTP Link/CliLink/SrvLink and Hub/Client/Server manage the transport lifecycle, allocator-backed streams, receives, and queued sends. [Ztls.hh](./Ztls.hh)
- `Ztls::Cxn` owns the protocol state. The record buffer types directly reserve Zi buffers. [ZtlsCxn.hh](./ZtlsCxn.hh), [ZtlsBuf.hh](./ZtlsBuf.hh)
- [ZtlsPK.hh](./ZtlsPK.hh), [ZtlsMD.hh](./ZtlsMD.hh), [ZtlsKEM.hh](./ZtlsKEM.hh), [ZtlsHPKE.hh](./ZtlsHPKE.hh), and [ZtlsCOSE.hh](./ZtlsCOSE.hh): keys, hashes/HMAC, KEMs, fixed-suite base-mode HPKE, and CBOR key/signature conversion.
- [ZtlsAge.hh](./ZtlsAge.hh), [ZtlsSec.hh](./ZtlsSec.hh), and [ZtlsTOTP.hh](./ZtlsTOTP.hh): age recipients/identities and file encryption, secret hashing/comparison, and time-based codes.
- [ZtlsVault.hh](./ZtlsVault.hh): Vault init/open/close/load/save/final, Ephemeral/File/KeyRing/Module/Auto stores, and Direct/Indirect modes.
- [ztotp.cc](./ztotp.cc): installed utility accepting one BASE32 secret argument and printing a six-character code. [Makefile.am](./Makefile.am) exports libZtls and public headers; ZtlsVaultSS is private.

## Invariants

- Hub validation requires compatible multiplexer-owned allocators and distinct TLS Rx/Tx threads. Async TLS needs isolated threads and is unsupported on Windows. Configuration stays stable during use; final requires a stopped engine. [Ztls.hh](./Ztls.hh)
- Rx owns the handshake and receive state. The Rx→Tx→Rx handshake handoff assigns the aligned outbound members to Tx before `connected()`. Generation and connection checks reject stale sends and closes. Client links retain their connections. Accepted server connections retain their links. [ZtlsCxn.hh](./ZtlsCxn.hh), [Ztls.hh](./Ztls.hh)
- Application process returning zero pauses, negative disconnects, positive continues. Plaintext records are bounded to 16,384 bytes. Teardown drains admitted work through TLS Rx, I/O Rx, TLS Tx, I/O Tx, and TLS Rx. Keep hub/multiplexer alive through pending DNS, socket setup, and async jobs. [Ztls.hh](./Ztls.hh)
- Server ALPN follows configured server preference; configured negotiation without a match fails. Certificate/key configuration is validated; early data is disabled. [Ztls.hh](./Ztls.hh)
- Crypto hook spans are borrowed. Contexts and signature verifiers use virtual operations and deterministic destruction. Key parsing can modify encoded input and enforces file limits. KEM provider availability is checked at runtime. HPKE uses a fixed hybrid KEM/SHA-256/ChaCha20-Poly1305 suite in base mode. [ZtlsPK.cc](./ZtlsPK.cc), [OpenSSL backend](./engine/ZtlsEngineOpenSSL.cc), [ZtlsHPKE.cc](./ZtlsHPKE.cc)
- Age accepts 1–256 recipients; scrypt must be sole recipient and hybrid/classical classes cannot mix. Decryption bounds headers and verifies header MAC before payload. Existing file positions are used; partial writes are not rolled back. [ZtlsAge.cc](./ZtlsAge.cc)
- Vault final requires a closed store. Load callbacks borrow mutable data. Direct File is plaintext JSON; Auto may fall back to File after native failures. Indirect storage uses a backend passphrase and per-account age files. [ZtlsVault.cc](./ZtlsVault.cc)
- TLS input retains its original registered or builtin backing. The private, unregistered plaintext allocation is published after authentication. Packet reservations preserve the selected provider and physical payload cap. [ZtlsBuf.hh](./ZtlsBuf.hh)
- TOTP verification has no replay state. Calculation uses native-word dynamic truncation. [ZtlsTOTP.cc](./ZtlsTOTP.cc)

## Side Effects

- Transport reads certificate/CA material, resolves hosts, opens sockets/listeners, registers telemetry, queues output/retries, and logs errors. Tx stream destruction flushes; close notifications and key updates run on TLS Tx. [Ztls.hh](./Ztls.hh)
- Backend initialization loads OpenSSL configuration. Crypto operations allocate temporary buffers and use random/backend state; secret clearing follows individual operation paths. [OpenSSL backend](./engine/ZtlsEngineOpenSSL.cc), [ZtlsSec.cc](./ZtlsSec.cc)
- Vault file storage creates private directories and an exclusive PID file, then publishes through exclusive temporary files, file sync, and rename, without promising parent-directory sync. Module stores load a shared library. [ZtlsVault.cc](./ZtlsVault.cc)
- Linux KeyRing uses D-Bus Secret Service with a plain session, private scheduler, and unlock prompts. Windows uses generic credentials persisted to the local machine. [ZtlsVaultLinux.cc](./ZtlsVaultLinux.cc), [ZtlsVaultWindows.cc](./ZtlsVaultWindows.cc)
- ztotp prints a code to stdout or usage/decode errors to stderr. [ztotp.cc](./ztotp.cc)

## Verification

From a configured build with Z prerequisites available, build `libZtls.la`
and the Ztls test targets. Run `make -C ztls/test test` and
`make -C ztls/itest test`. The suites cover standalone cryptography, credentials,
ticket resumption, direct packet allocation, key updates, and async retirement.
[Test manifest](../test/Makefile.am), [integration manifest](../itest/Makefile.am)
