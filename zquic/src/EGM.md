# Zquic transport library

## Overview

QUIC v1 transport with TLS handshake/protection, multiplexed streams, recovery and congestion control, path validation/migration, packet allocators, and optional qlog output.

## Interface

- [Zquic.hh](./Zquic.hh): HubParams, Hub, Client, Server, bootstrap/token/reset helpers and public allocator aliases. [ZquicCliLink.hh](./ZquicCliLink.hh) and [ZquicSrvLink.hh](./ZquicSrvLink.hh) adapt shared [Link](./ZquicLink.hh) to client/server endpoints.
- [ZquicStream.hh](./ZquicStream.hh), [ZquicStreamUtil.hh](./ZquicStreamUtil.hh): streams, retained Tx ranges, receive retirement and flow/stream credit. [ZquicBuf.hh](./ZquicBuf.hh) distinguishes retained plaintext/receive buffers from final UDP Tx nodes.
- [ZquicCrypto.hh](./ZquicCrypto.hh), [ZquicPacket.hh](./ZquicPacket.hh), [ZquicFrame.hh](./ZquicFrame.hh), and [ZquicTransport.hh](./ZquicTransport.hh): TLS/protection contexts, packet/frame codecs and transport parameters.
- [ZquicRecovery.hh](./ZquicRecovery.hh), [ZquicSched.hh](./ZquicSched.hh), [ZquicPath.hh](./ZquicPath.hh): ACK/loss/PTO recovery, NewReno, packetization and path/MTU/ECN state.
- [ZquicLog.hh](./ZquicLog.hh), [ZquicDiag.hh](./ZquicDiag.hh): qlog lifecycle and diagnostics. [Makefile.am](./Makefile.am) builds libZquic and installs public headers; the OpenSSL helper is private.

## Invariants

- Hub initialization requires a multiplexer, distinct valid QUIC Rx/Tx threads, and configured UDP maximum no larger than BufSize. Optional async TLS needs an isolated distinct thread and is unsupported on Windows. Final requires a stopped engine; multiplexer/allocator and callback owners must outlive admitted asynchronous work. [Zquic.hh](./Zquic.hh)
- Rx owns received streams, crypto, ACKs and CID admission; Tx owns recovery, packetization, congestion, controls, timers and migration. Cross-owner continuations carry copied metadata or retained references and use connection/runtime/attempt generations. Stream retirement traverses Tx→Rx→Tx with identity checks. [ZquicLink.hh](./ZquicLink.hh)
- STREAM receive slices retain decrypted packet backing. Tx bodies remain retained for ACK/recovery; packet protection gathers borrowed vectors into final output. Crypto reassembly may copy fragments. Short-header key trials preserve ciphertext in scratch until authentication succeeds. [ZquicStream.hh](./ZquicStream.hh), [ZquicCrypto.cc](./ZquicCrypto.cc)
- Queued source-buffer admission, flow credit, sent-packet retention and socket completion are separate. Retire only consumed Rx bytes; FIN becomes packetizable after queued ranges empty, and successful Tx completion waits for its ACK. Off-thread send success can mean scheduling acceptance. [ZquicStream.hh](./ZquicStream.hh), [ZquicCliLink.hh](./ZquicCliLink.hh)
- Frame parsing/dispatch is incremental: earlier valid frame effects can survive a later invalid frame, while packet ACK recording requires full consumer success. Codec views borrow input and failed writes may leave partial output. [ZquicLink.hh](./ZquicLink.hh), [ZquicFrame.cc](./ZquicFrame.cc)
- ACK/loss processing uses bounded packet batches and generation guards; reopened congestion credit services retained loss before ordinary producers. Some helper limits bound successful results rather than total visits. Lost packet history remains bounded for late ACK processing. [ZquicLink.hh](./ZquicLink.hh), [ZquicRecovery.hh](./ZquicRecovery.hh)
- Path admission separately checks MTU, congestion and anti-amplification. Migration requires matching attempt/CID state; current UDP readiness is distinct from establishment. Server handshake completion and client HANDSHAKE_DONE drive different confirmation paths. [ZquicPath.hh](./ZquicPath.hh), [ZquicCliLink.hh](./ZquicCliLink.hh), [ZquicSrvLink.hh](./ZquicSrvLink.hh)
- Client TLS installs certificate verification when a CA path is supplied. TLS callbacks/ticket bytes have borrowed lifetimes as described by Crypto initialization; secret erasure guarantees are operation-specific. [ZquicCrypto.cc](./ZquicCrypto.cc), [ZquicCrypto.hh](./ZquicCrypto.hh)
- ZquicLOG erases qlog arguments in release builds and checks enabled state before evaluating them in debug builds. Detailed diagnostic counters/summaries can be zero/empty in release; top-level atomic traffic counters remain. [ZquicLog.hh](./ZquicLog.hh), [ZquicDiag.hh](./ZquicDiag.hh), [ZquicDiag.cc](./ZquicDiag.cc)

- Each physical UDP connection owns a 4096-packet Tx queue including its active head. Full queues reject before admission; priority packets insert after the active head. Posted sends compare original physical identity and generation against the current connection. Endpoint destruction requires closed state and fully drained physical connections/unbind work. [Zquic_.hh](./Zquic_.hh)

- Packet helpers require caller-side extent/context checks; transport-parameter decoding resets then incrementally mutates its destination and repeated recognized fields overwrite earlier values. Discard failed decode results. Socket option capability plans do not guarantee every optional option was applied. [ZquicPacket.cc](./ZquicPacket.cc), [ZquicTransport.cc](./ZquicTransport.cc), [ZquicSock.cc](./ZquicSock.cc)

## Side Effects

- Client/server adapters open UDP endpoints, read TLS credentials/CA, generate tokens/CIDs, schedule timers and owner work, exchange protected packets, register telemetry and invoke application callbacks. [Zquic.hh](./Zquic.hh), [ZquicCliLink.hh](./ZquicCliLink.hh), [ZquicSrvLink.hh](./ZquicSrvLink.hh)
- Crypto may append plaintext traffic secrets and client random to the configured key-log file, or SSLKEYLOGFILE when the configured path is empty; I/O failure is silently ignored by this path. Initialization also uses backend TLS state and certificate stores. [ZquicCrypto.cc](./ZquicCrypto.cc)

- Enabled debug qlog starts a writer, ages/opens per-trace files and writes JSON-sequence records. Ring backpressure uses a heap fallback queue drained before worker exit; callers keep Trace alive through queued work. Events omit payload/key bytes but can include stateless reset tokens and connection IDs. Writer failure counters can count one failure more than once. [ZquicLog.hh](./ZquicLog.hh), [ZquicLog.cc](./ZquicLog.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zquic test`. [Unit tests](../test/Makefile.am) cover codecs, crypto, recovery, streams, paths, runtime/timers and logs; [integration tests](../itest/Makefile.am) exercise the Tx queue. Commands were not run for this documentation change.
