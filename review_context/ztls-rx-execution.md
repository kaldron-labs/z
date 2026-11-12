# Ztls incremental receive execution

Execution of [ztls_fix2.md](../ztls_fix2.md), including its explicit
builtin/stub buffer contract. Verification is in progress; this document does
not yet claim that the completion gates have passed. Historical review
snapshots retain their original scope.

## Implementation

`ptls_receive_stream` keeps bounded header, IV, tag and serial AEAD state in
the existing TLS object. It consumes received spans directly into one private
destination supplied by `ptls_plaintext_sink_t`. OpenSSL and Fusion implement
checked serial decryption. Authenticated records share the existing record
dispatcher, handshake/transcript handling and key transitions with contiguous
receivers. Output generated during receive uses the existing serial Tx sink.

Ztls transfers the original RxTCP reference to its Rx owner and pauses receive
admission while that chunk is pending. It releases consumed input independently
of the incomplete record destination. Scheduler turns process at most 32 record
boundaries. Resumption and publication check the physical connection and TLS
generation, including the first receive admission and the interval between
physical disassociation and final owner drain. Teardown drains queued mux Rx
admissions before the existing TLS Tx, mux Tx and final TLS Rx barriers.

`Backend::PlaintextSink` owns the provisional RxTLS allocation and clears it on
discard. Completion transfers its existing reference after the receive API has
returned. RxTLS defaults to `PTLS_RECEIVE_RECORD_CAPACITY`: 16 KiB application
capacity plus the dependency's 256-byte supported record allowance. Actual
per-record reservations exclude header, explicit IV and tag. Initialization
rejects insufficient custom builtin geometry. RxTCP defaults to local
1460-byte payload geometry; custom allocator capacity and headroom survive
unchanged.

Neither Ztls nor Ztcp contains a plugin-presence check. Allocators and receive
contexts encapsulate backing provision, including a stub without backing before
reception. Tx packet reservation uses allocator geometry and the physical
connection's payload cap through the same sink for every backend.

## Evidence location

Current logs and machine-readable results are under
`/home/ubuntu/rebuild-evidence/ztls-fix2`. All builds use the existing Clang debug
configuration and one shared eight-job make jobserver. Zi and Ztcp are frozen;
`frozen-source-verification.json` verifies all 509 recorded lower-module source
hashes. `lower-phases.json` records passing Phase 1, Phase 2 and Zdbus build/test
commands.

| Gate | Current evidence |
| --- | --- |
| Checked serial crypto and contiguous compatibility | `backend-verification.json`; final fail-fast dependency build/install and native vector plan 18 in `dependency-build.log` / `vector-rx.log`; installed core hash matches the build in `dependency-final-hashes.json`. |
| Async, tickets, accepted/rejected early data, authenticated controls | `stream-async-openssl.log`, including the incremental async/resumption subtests in `t/picotls.c`. |
| No ciphertext accumulation, fragmentation, padding, corruption, bounds, key transitions | `t/vector-tx.c` and final `vector-rx.log`; final software Valgrind plan 18 passes with zero errors/lost bytes. |
| Ztls defaults, sink geometry, authentication gating and original input lifetime | `ZtlsRxPacketTest`, `ZtlsRxStubTest`; `ztls-final-barrier-tests.log`: 14 binaries / 79 tests pass. |
| Working pairs, async close/restart, application frames across records, Tx regressions | `ztls-final-barrier-itests.log`: three binaries / 13 tests pass. |
| Memory and owner lifetime | Final Rx kernel/unit, stub and isolated BufHook Valgrind logs: zero errors and no definitely, indirectly or possibly lost blocks. Software OpenSSL mode is used for Valgrind; native suites also pass. |
| Duplicate TCP delivery | `netns-tx-regression.json`, `tls-duplicate-proof.json` and pcap: current Tx integration pair passes with 5,578 repeated original sequence ranges and no capture drops. |
| Zquic consumers | `zquic-test-test.log`: 21 files / 197 tests; `zquic-itest-test.log`: working pair passes. |
| Local HTTP consumers/interoperability | `modules-zhttp.json`: full build, 27 unit files / 278 tests, eight integration files / 91 tests, and seven offline H2 interoperability checks pass. |

## Verification repairs

Rejected early-data authentication must clear per-record OpenSSL state while
retaining the installed traffic key for the next legitimate handshake record.
Terminal receive errors abort that state and clear private plaintext. New
resumption tests exposed and now cover this distinction.

The BufHook fixture previously retained a raw server link pointer after the
server could retire. It now snapshots server telemetry on its own owner at
disconnect; native and isolated Valgrind runs preserve the telemetry checks.
Concurrent debug copies of that fixture shared its residue root and interfered
with certificate files, so final memory verification runs it in isolation.

The later Zutcp application gate requires a Zutcp implementation, which is not
present in this checkout. Builtin/stub fixtures and kernel TCP verification do
not establish physical NIC operation.
