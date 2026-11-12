# Ztls packet-output execution

Execution of [ztls_fix.md](../ztls_fix.md), using the Clang debug configuration,
reused builds and one shared eight-job make jobserver. Historical design-review
snapshots retain their original scope.

## Implemented behavior

Application plaintext uses ordinary builtin/VHeap TxTLS storage. All TLS wire
output is encrypted directly into separately reserved TxTCP packets, including
handshake, alerts, close-notify and KeyUpdate. Every packet allocation receives
the originating physical connection's mps snapshot. Provider headroom, tailroom
and fixed pool geometry remain intact.

Detached application output reserves the entire record and any KeyUpdate prefix
before changing cipher state. Failed reservation releases every acquired packet
and publishes nothing. Handshake/control failures terminate the session under
the per-record rule. Completion remains per packet; successful application
protection is reported once before publication. Generation-bound failures close
the originating connection without closing a successor.

The dependency exposes the selected sink/chunk API and serial OpenSSL/Fusion
AEAD callbacks. Contiguous callers share the record emitter and retain one-shot
backend support. Chunk entry points reject unsupported TLS 1.2 and TLS 1.3
providers before reservation or handshake-state mutation.

## Requirement evidence

All paths below the evidence directory refer to
`/home/ubuntu/rebuild-evidence/ztls-fix`.

| Requirement | Evidence |
| --- | --- |
| Serial encryption, boundaries, contiguous equivalence, key updates, unsupported providers | `crypto-accepted-evidence.json`: full dependency baseline PASS (7 files/57 tests), followed by final affected suites PASS (5 files/45 tests). Only `lib/picotls.c` and `t/vector-tx.c` changed after the full baseline; Fusion's exhaustive implementation/suite is unchanged. |
| Dependency memory checks | Final vector-Tx Valgrind: zero errors and zero definitely, indirectly or possibly lost bytes. The optional interrupted broad OpenSSL Valgrind run is documented separately and is not claimed as a complete clean gate. |
| Packet geometry, sentinel framing reservations, fixed/shared pools, failure cleanup | `ZtlsTxPacketTest` in `ztls-accepted-final-unit.log`. |
| Full 16 KiB working pair, KeyUpdate, all 114 detached reservation failures, initial handshake failure, maximum-record pool probe, final-flush failure | `ztls-accepted-final-integration.log`. Assertions cover unchanged plaintext, no failed-record protection/publication, terminal teardown and balanced packet references. |
| Partial writes/EAGAIN, queue rejection after a prefix, retained ciphertext | Same kernel working-pair suite; writes are constrained to seven bytes and injected EAGAIN. |
| Duplicate ciphertext at original TCP byte positions | `tls-duplicate-proof.json` and `tls-duplicate-tcp.pcap`: netem duplicate 100%, 6,510 repeated sequence ranges; exact peer plaintext and once-only protection checks pass. This is a kernel TCP duplication experiment. |
| Final working-pair memory check | `ztls-packet-current-valgrind.log`: zero errors and zero lost bytes, with `OPENSSL_ia32cap=0`. |
| Complete consumer build and tests | `all-final-build.log` and `accepted-final-gates.json`: full top-level build, separate Zdash build, 17 affected unit suites, eight integration suites and HTTP interoperability pass. Zws has no separate itest directory; its networking fixtures run in its unit suite. Earlier unaffected REBUILD phase results remain in `lower-gates.json`, `web-gates*.json`, `downstream-data-status.json` and phase result files. |
| Cross-stack QUIC interoperability | Final image `sha256:226785203a6013e2e610ff803817eef760be97bfaab800257eadda780edd9299`; `qir-accepted/report.md`: 22 passed, two msquic HTTP/3 cases unsupported, zero failures. ngtcp2, quiche, quic-go and msquic were tested in both roles. |
| Frozen lower libraries | `frozen-source-final.json`: all 509 recorded files match their frozen hashes, including Zi. Existing worktree edits were preserved. |

## Defects resolved during verification

TLS dispatch now reports ownership transfer without rereading connection state
after dispatch: a peer can consume the last packet and close before its producer
resumes. Packet completion remains authoritative. Ten repeated single-CPU FIX
TLS session runs pass (`zfix-stop-accepted.json`). The FIX shutdown fixture also
verifies the expected failed-output/terminal-event pair when a queued Logon loses
the intentional stop-before-bind race; readiness, business-traffic, physical
close and final owner-drain assertions remain intact.

HTTP H1 pool transmit errors now select the actual transport, including TLS-only
pools. FIX frame completion binds to the final ciphertext packet before
publication. FIX protocol-close callers avoid repeating native close after their
physical obligation has drained. The underlying frozen Ztcp idempotency defect
is recorded for its author in
[ztls-frozen-dependency-defects.md](ztls-frozen-dependency-defects.md).

## Driver boundary

This establishes the independently testable Ztls prerequisite. No Zutcp or Zxdp
module is present to execute the subsequent driver phase-2 gate. Zutcp/veth and
physical mlx5 verification remain later implementation gates. These kernel/fake
provider results do not establish NIC zero-copy selection or physical-NIC
operation, and no such claim is made.
