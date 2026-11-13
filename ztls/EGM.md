# Ztls

## Overview

TLS transport over the framework multiplexer, with cryptographic primitives, age encryption, and secret storage. OpenSSL and picotls back the library.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): transport, keys, crypto adapters, vault stores, and the installed `ztotp` utility.
- [Examples](./example/EGM.md): TLS client/server programs with explicit I/O and TLS scheduler threads.

## Interface

[Makefile.am](./Makefile.am) builds source, unit tests, integration tests, examples, and interoperability tools. The production library exports `libZtls.la`; the child contracts describe its APIs.

## Invariants

Transport initialization requires compatible allocators and distinct TLS Rx/Tx threads; finalization requires a stopped engine. Pending asynchronous operations must retire while their owning hub and multiplexer remain alive. Vault users must close stores before finalization. See [Ztls.hh](./src/Ztls.hh) and [ZtlsVault.cc](./src/ZtlsVault.cc).

## Side Effects

Transport resolves endpoints, opens sockets, schedules work, and reads certificates. Vault operations may access local files, platform credential services, dynamically loaded stores, and native unlock prompts. The direct file store contains plaintext JSON; automatic store selection can fall back to that store. See the library contract.

## Verification

From a configured repository root, run `make -j8 && make -C ztls/test test`. The module's test target runs unit tests; integration and interoperability directories are separate. Commands were not run for this documentation change.
