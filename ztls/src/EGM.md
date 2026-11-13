# Ztls transport, cryptography, and vault library

## Overview

TLS links over ZiMultiplex, picotls/OpenSSL adapters, public-key cryptography, age file encryption, and platform-backed secret stores.

## Interface

- [Ztls.hh](./Ztls.hh): CRTP Link/CliLink/SrvLink and Hub/Client/Server provide transport lifecycle, allocator-backed streams, receive processing, and queued sends.
- [ZtlsPK.hh](./ZtlsPK.hh), [ZtlsMD.hh](./ZtlsMD.hh), [ZtlsKEM.hh](./ZtlsKEM.hh), [ZtlsHPKE.hh](./ZtlsHPKE.hh), and [ZtlsCOSE.hh](./ZtlsCOSE.hh): keys, hashes/HMAC, KEMs, fixed-suite base-mode HPKE, and CBOR key/signature conversion.
- [ZtlsAge.hh](./ZtlsAge.hh), [ZtlsSec.hh](./ZtlsSec.hh), and [ZtlsTOTP.hh](./ZtlsTOTP.hh): age recipients/identities and file encryption, secret hashing/comparison, and time-based codes.
- [ZtlsVault.hh](./ZtlsVault.hh): Vault init/open/close/load/save/final, Ephemeral/File/KeyRing/Module/Auto stores, and Direct/Indirect modes.
- [ztotp.cc](./ztotp.cc): installed utility accepting one BASE32 secret argument and printing a six-character code. [Makefile.am](./Makefile.am) exports libZtls and public headers; ZtlsVaultSS is private.

## Invariants

- Hub validation requires compatible multiplexer-owned allocators and distinct TLS Rx/Tx threads. Async TLS needs isolated threads and is unsupported on Windows. Configuration stays stable during use; final requires a stopped engine. [Ztls.hh](./Ztls.hh)
- Receive TLS state belongs to TLS Rx; detached encryption belongs to TLS Tx. Handshake completion installs keys through Rx→Tx→Rx before connected. Generation and physical-connection checks reject stale send/close continuations. Client links retain connections; accepted server connections retain links. [Ztls.hh](./Ztls.hh)
- Application process returning zero pauses, negative disconnects, positive continues. Plaintext records are bounded to 16,384 bytes. Teardown drains admitted work through TLS Rx, I/O Rx, TLS Tx, I/O Tx, and TLS Rx. Keep hub/multiplexer alive through pending DNS, socket setup, and async jobs. [Ztls.hh](./Ztls.hh)
- Server ALPN follows configured server preference; configured negotiation without a match fails. Certificate/key configuration is validated; early data is disabled. [Ztls.hh](./Ztls.hh)
- Crypto callback spans are borrowed. Key parsing can modify encoded input and enforces file limits. KEM provider availability is checked at runtime. HPKE uses a fixed hybrid KEM/SHA-256/ChaCha20-Poly1305 suite in base mode. [ZtlsPK.cc](./ZtlsPK.cc), [ZtlsOpenSSL.cc](./ZtlsOpenSSL.cc), [ZtlsHPKE.cc](./ZtlsHPKE.cc)
- Age accepts 1–256 recipients; scrypt must be sole recipient and hybrid/classical classes cannot mix. Decryption bounds headers and verifies header MAC before payload. Existing file positions are used; partial writes are not rolled back. [ZtlsAge.cc](./ZtlsAge.cc)
- Vault final requires a closed store. Load callbacks borrow mutable data. Direct File is plaintext JSON; Auto may fall back to File after native failures. Indirect storage uses a backend passphrase and per-account age files. [ZtlsVault.cc](./ZtlsVault.cc)
- Callers serialize process-global picotls hook installation. TOTP verification has no replay state; calculation uses native-word dynamic truncation. [ZtlsPico.cc](./ZtlsPico.cc), [ZtlsTOTP.cc](./ZtlsTOTP.cc)

## Side Effects

- Transport reads certificate/CA material, resolves hosts, opens sockets/listeners, registers telemetry, queues output/retries, and logs errors. Tx stream destruction flushes; close notifications and key updates run on TLS Tx. [Ztls.hh](./Ztls.hh)
- Backend initialization loads OpenSSL configuration. Crypto operations allocate temporary buffers and use random/backend state; secret clearing follows individual operation paths. [ZtlsOpenSSL.cc](./ZtlsOpenSSL.cc), [ZtlsSec.cc](./ZtlsSec.cc)
- Vault file storage creates private directories and an exclusive PID file, then publishes through exclusive temporary files, file sync, and rename, without promising parent-directory sync. Module stores load a shared library. [ZtlsVault.cc](./ZtlsVault.cc)
- Linux KeyRing uses D-Bus Secret Service with a plain session, private scheduler, and unlock prompts. Windows uses generic credentials persisted to the local machine. [ZtlsVaultLinux.cc](./ZtlsVaultLinux.cc), [ZtlsVaultWindows.cc](./ZtlsVaultWindows.cc)
- ztotp prints a code to stdout or usage/decode errors to stderr. [ztotp.cc](./ztotp.cc)

## Verification

From a configured repository root, run `make -j8 && make -C ztls/test test`. [The test manifest](../test/Makefile.am) runs crypto, vault, buffer, async, secret, and packet tests through prove, adding Secret Service coverage on Linux. Commands were not run for this documentation change.
