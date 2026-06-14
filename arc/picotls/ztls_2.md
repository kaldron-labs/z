# Ztls.hh Diff Review (Updated)

Reviewed the current [`ztls/src/Ztls.hh`](/home/count0/src/z/ztls/src/Ztls.hh) against [`ztls/src/Ztls.hh.orig`](/home/count0/src/z/ztls/src/Ztls.hh.orig).

The current rewrite is smaller than the original (`1434` lines vs `1525`), now parses cleanly, and has restored most of the source-compatibility that the first rewrite had dropped. The architectural direction is still the same: replace the hand-rolled TLS record framing and transmit queueing with `ZiRx` / `ZiTx`, collapse multiple internal queues into one stream abstraction, and use more framework-owned vocabulary types.

**Findings**
1. High: the new `txStream()` framing limits no longer match the fixed `m_max_plaintext` limit enforced by `send_()`, which creates a recursion bug for some payload sizes.
Current `handshook()` computes exact negotiated tailroom with `m_tailroom = m_rec_overhead - m_headroom` at [`ztls/src/Ztls.hh:253`](/home/count0/src/z/ztls/src/Ztls.hh#L253) to [`ztls/src/Ztls.hh:269`](/home/count0/src/z/ztls/src/Ztls.hh#L269).
Current `txStream()` and `txStream_()` then pass that exact tailroom to `Zi::txStream` at [`ztls/src/Ztls.hh:416`](/home/count0/src/z/ztls/src/Ztls.hh#L416) to [`ztls/src/Ztls.hh:447`](/home/count0/src/z/ztls/src/Ztls.hh#L447).
`Zi::TxStream` permits payload bytes up to `maxSize - headRoom - tailRoom` per buffer at [`zi/src/ZiTxStream.hh:57`](/home/count0/src/z/zi/src/ZiTxStream.hh#L57) to [`zi/src/ZiTxStream.hh:71`](/home/count0/src/z/zi/src/ZiTxStream.hh#L71).
But `send_()` still rejects any buffer longer than `m_max_plaintext`, which remains fixed at `TxRecordCapacity - 325`, and re-feeds oversized buffers back into `txStream_()` at [`ztls/src/Ztls.hh:400`](/home/count0/src/z/ztls/src/Ztls.hh#L400) to [`ztls/src/Ztls.hh:407`](/home/count0/src/z/ztls/src/Ztls.hh#L407) and [`ztls/src/Ztls.hh:475`](/home/count0/src/z/ztls/src/Ztls.hh#L475) to [`ztls/src/Ztls.hh:479`](/home/count0/src/z/ztls/src/Ztls.hh#L479).
The original code kept those two limits aligned by passing worst-case tailroom (`TxMaxOverhead - m_tx_headroom`) into `txStream`, so every stream-produced chunk was already `<= m_max_plaintext` at [`ztls/src/Ztls.hh.orig:399`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L399) to [`ztls/src/Ztls.hh.orig:425`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L425), [`ztls/src/Ztls.hh.orig:457`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L457) to [`ztls/src/Ztls.hh.orig:461`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L461), and [`ztls/src/Ztls.hh.orig:770`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L770) to [`ztls/src/Ztls.hh.orig:778`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L778).
As written now, if negotiated record overhead is below `325`, `txStream_()` can emit a chunk that `send_()` immediately routes back into another `txStream_()` with the same limits. That is a real logic loop, not just a throughput change.

2. Low: the old manual receive-framing state is still declared even though the new implementation no longer uses it.
The rewrite now frames records entirely through `ZiRx::recv<parseHdr<...>, ...>` at [`ztls/src/Ztls.hh:169`](/home/count0/src/z/ztls/src/Ztls.hh#L169) to [`ztls/src/Ztls.hh:198`](/home/count0/src/z/ztls/src/Ztls.hh#L198).
But the old I/O-thread fields `m_rxBuf`, `m_rx_need`, `m_rx_total`, and `m_rx_hdr_rcvd` are still present at [`ztls/src/Ztls.hh:629`](/home/count0/src/z/ztls/src/Ztls.hh#L629) to [`ztls/src/Ztls.hh:633`](/home/count0/src/z/ztls/src/Ztls.hh#L633), even though the old users were removed with the deleted `rx()` path from [`ztls/src/Ztls.hh.orig:178`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L178) to [`ztls/src/Ztls.hh.orig:230`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L230).
This is not a correctness issue by itself, but it makes the rewrite look less complete and obscures which state actually matters.

**Implementation Changes**
Control flow:
The original receive path manually read TLS records on the I/O thread with `io.init(...)`, `rx()`, and the `m_rx_need` / `m_rx_total` state machine, then queued complete ciphertext buffers into `m_rxInQueue` for the TLS thread at [`ztls/src/Ztls.hh.orig:139`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L139) to [`ztls/src/Ztls.hh.orig:237`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L237).
The rewrite moves record framing into `ZiRx`, with `connected_0()` arming `Rx::recv<parseHdr<RxBufAlloc>, &Impl::recvRecord>` and `recvRecord()` dispatching each complete TLS record either to `handshake_0()` or `recvPayload()` at [`ztls/src/Ztls.hh:169`](/home/count0/src/z/ztls/src/Ztls.hh#L169) to [`ztls/src/Ztls.hh:215`](/home/count0/src/z/ztls/src/Ztls.hh#L215).

The handshake state machine is flatter.
The original client drove handshake lazily from `handshake()`, with `m_handshakeStarted` guarding the first ClientHello send at [`ztls/src/Ztls.hh.orig:240`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L240) to [`ztls/src/Ztls.hh.orig:285`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L285).
The current client starts the ClientHello eagerly from `CliLink::connected_()` by calling `handshake__(nullptr, nullptr)` after setting handshake properties at [`ztls/src/Ztls.hh:751`](/home/count0/src/z/ztls/src/Ztls.hh#L751) to [`ztls/src/Ztls.hh:771`](/home/count0/src/z/ztls/src/Ztls.hh#L771).

The internal naming now reflects thread-hop phases rather than semantic stages.
Old names were `connected_`, `connected_2`, `connected__`, `recv_`, `handshake_done_`, `process_plaintext_`, `txStream_send_`, and `ptls_buf_send_`.
New names are `connected_0`, `connected_1`, `connected_`, `recvRecord`, `recvPayload`, `handshake_0`, `handshake_`, `handshake__`, `handshook`, `rcvd_0`, and `rcvd_` at [`ztls/src/Ztls.hh:169`](/home/count0/src/z/ztls/src/Ztls.hh#L169) to [`ztls/src/Ztls.hh:363`](/home/count0/src/z/ztls/src/Ztls.hh#L363).

Data structures and state:
The old design used three distinct queues for I/O and TLS coordination: `m_txOutQueue`, `m_rxInQueue`, and `m_rxPlainQueue` at [`ztls/src/Ztls.hh.orig:837`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L837) to [`ztls/src/Ztls.hh.orig:859`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L859).
The rewrite removes those queues and keeps only `m_rxStream` plus `ZiTx` / `ZiRx` framework machinery at [`ztls/src/Ztls.hh:648`](/home/count0/src/z/ztls/src/Ztls.hh#L648) to [`ztls/src/Ztls.hh:654`](/home/count0/src/z/ztls/src/Ztls.hh#L654).

The public `Ztls` vocabulary changed, but source compatibility has now largely been restored.
The current file adds heap-tagged `Ticket` and `ALPN` typedefs at [`ztls/src/Ztls.hh:41`](/home/count0/src/z/ztls/src/Ztls.hh#L41) to [`ztls/src/Ztls.hh:44`](/home/count0/src/z/ztls/src/Ztls.hh#L44).
It also restores `IOQueue`, `RxStream`, `RxCursor`, and `IOBufAlloc` at [`ztls/src/Ztls.hh:99`](/home/count0/src/z/ztls/src/Ztls.hh#L99) to [`ztls/src/Ztls.hh:110`](/home/count0/src/z/ztls/src/Ztls.hh#L110), so downstream code can still refer to the same namespace-level types the original exposed at [`ztls/src/Ztls.hh.orig:82`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L82) to [`ztls/src/Ztls.hh.orig:96`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L96).

State around handshake progress also changed.
The old code used `m_handshakeStarted` and queue emptiness to know whether it was still in handshake mode at [`ztls/src/Ztls.hh.orig:242`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L242) to [`ztls/src/Ztls.hh.orig:283`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L283).
The new code uses `m_handshook` as a contended atomic phase flag, checked both when dispatching records and when processing queued lambdas at [`ztls/src/Ztls.hh:185`](/home/count0/src/z/ztls/src/Ztls.hh#L185) to [`ztls/src/Ztls.hh:205`](/home/count0/src/z/ztls/src/Ztls.hh#L205) and declared at [`ztls/src/Ztls.hh:652`](/home/count0/src/z/ztls/src/Ztls.hh#L652) to [`ztls/src/Ztls.hh:654`](/home/count0/src/z/ztls/src/Ztls.hh#L654).

Framework integration:
The biggest architectural shift is that `Link` now inherits from `ZiRx<Impl, RxBufAlloc_>` and `ZiTx<Impl>` at [`ztls/src/Ztls.hh:124`](/home/count0/src/z/ztls/src/Ztls.hh#L124) to [`ztls/src/Ztls.hh:128`](/home/count0/src/z/ztls/src/Ztls.hh#L128), whereas the original `Link` only inherited `ZmPolymorph` at [`ztls/src/Ztls.hh.orig:101`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L101).
That is why the new header includes [`zi/src/ZiRx.hh`](/home/count0/src/z/zi/src/ZiRx.hh) and [`zi/src/ZiTx.hh`](/home/count0/src/z/zi/src/ZiTx.hh), and why the file switched from [`ZeAssert.hh`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L22) to [`ZiAssert.hh`](/home/count0/src/z/ztls/src/Ztls.hh#L22).

The transmit-side plumbing is correspondingly shorter.
The original transmit path relied on `TxStreamState`, `txStream_send`, `txStream_send_`, `txOut(ZmRef<ZiIOBuf>)`, and `txOut(const uint8_t *, unsigned)` at [`ztls/src/Ztls.hh.orig:399`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L399) to [`ztls/src/Ztls.hh.orig:717`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L717).
The rewrite collapses that into `Zi::txStream`, a single `send(ZmRef<ZiIOBuf>)` app-thread hop, and `Tx::send` from `flushTxBuf_()` at [`ztls/src/Ztls.hh:416`](/home/count0/src/z/ztls/src/Ztls.hh#L416) to [`ztls/src/Ztls.hh:560`](/home/count0/src/z/ztls/src/Ztls.hh#L560).

The receive-side plaintext model is also flatter.
The original decrypted into `m_rxPlainQueue` and then let `process_plaintext_()` drive `process()` until the queue drained at [`ztls/src/Ztls.hh.orig:314`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L314) to [`ztls/src/Ztls.hh.orig:360`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L360) and [`ztls/src/Ztls.hh.orig:788`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L788) to [`ztls/src/Ztls.hh.orig:797`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L797).
The rewrite decrypts directly into `m_rxStream` and calls `impl()->process(m_rxStream)` in-place at [`ztls/src/Ztls.hh:328`](/home/count0/src/z/ztls/src/Ztls.hh#L328) to [`ztls/src/Ztls.hh:350`](/home/count0/src/z/ztls/src/Ztls.hh#L350).

Buffering and invariants:
The original implementation treated `ptls` buffer relocation as a recoverable condition.
It had warning-and-copy fallback paths for `ptls_receive()`, `ptls_handshake()`, and `ptls_send()` at [`ztls/src/Ztls.hh.orig:326`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L326) to [`ztls/src/Ztls.hh.orig:339`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L339), [`ztls/src/Ztls.hh.orig:506`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L506) to [`ztls/src/Ztls.hh.orig:529`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L529), and [`ztls/src/Ztls.hh.orig:587`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L587) to [`ztls/src/Ztls.hh.orig:606`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L606).
The current file removes those copy fallbacks and now treats any `origin` / `base` escape as a hard invariant violation that logs `TLS TX buffer origin mismatch` or `TLS RX buffer origin mismatch` and disconnects at [`ztls/src/Ztls.hh:288`](/home/count0/src/z/ztls/src/Ztls.hh#L288) to [`ztls/src/Ztls.hh:304`](/home/count0/src/z/ztls/src/Ztls.hh#L304) and [`ztls/src/Ztls.hh:328`](/home/count0/src/z/ztls/src/Ztls.hh#L328) to [`ztls/src/Ztls.hh:352`](/home/count0/src/z/ztls/src/Ztls.hh#L352).
That is a deliberate strengthening of invariants, not just a cleanup.

API and initialization changes:
The init API is broader than the original now.
The current `Engine` adds `loadCA(ZuCSpan)` at [`ztls/src/Ztls.hh:938`](/home/count0/src/z/ztls/src/Ztls.hh#L938) to [`ztls/src/Ztls.hh:940`](/home/count0/src/z/ztls/src/Ztls.hh#L940), adds `init_alpn_(ZuSpan<ZuCSpan>)` while keeping `init_alpn_(const char **)` at [`ztls/src/Ztls.hh:1014`](/home/count0/src/z/ztls/src/Ztls.hh#L1014) to [`ztls/src/Ztls.hh:1036`](/home/count0/src/z/ztls/src/Ztls.hh#L1036), and provides both span-based and original `const char **` overloads for `Client::init` and `Server::init` at [`ztls/src/Ztls.hh:1123`](/home/count0/src/z/ztls/src/Ztls.hh#L1123) to [`ztls/src/Ztls.hh:1129`](/home/count0/src/z/ztls/src/Ztls.hh#L1129) and [`ztls/src/Ztls.hh:1278`](/home/count0/src/z/ztls/src/Ztls.hh#L1278) to [`ztls/src/Ztls.hh:1286`](/home/count0/src/z/ztls/src/Ztls.hh#L1286).
The original only exposed the `const char **` ALPN entry points at [`ztls/src/Ztls.hh.orig:1313`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L1313) and [`ztls/src/Ztls.hh.orig:1422`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L1422).

Client/server startup behavior also changed in a few specific places.
The client now preserves the `ptls_t` configured in `connect_()` and only resets handshake properties in `connected_()`, instead of recreating the TLS object there. That fixes the SNI-losing behavior from the first rewrite and leaves the connect path consistent with the original intent at [`ztls/src/Ztls.hh:707`](/home/count0/src/z/ztls/src/Ztls.hh#L707) to [`ztls/src/Ztls.hh:771`](/home/count0/src/z/ztls/src/Ztls.hh#L771).
The server side now creates fresh per-connection TLS state in `SrvLink::connected_()` at [`ztls/src/Ztls.hh:814`](/home/count0/src/z/ztls/src/Ztls.hh#L814) to [`ztls/src/Ztls.hh:816`](/home/count0/src/z/ztls/src/Ztls.hh#L816), instead of relying on `Link` construction-time `reset_tls_()` plus an unused `connected__()` hook in the original at [`ztls/src/Ztls.hh.orig:118`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L118) and [`ztls/src/Ztls.hh.orig:1017`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L1017) to [`ztls/src/Ztls.hh.orig:1018`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L1018).

**Overall Assessment**
The refreshed rewrite is much closer to a coherent replacement than the earlier broken version.
It now compiles, it restores the exported aliases and init overloads needed by existing users, and the central architectural change is consistent: push more of the transport mechanics into `ZiRx`, `ZiTx`, and `ZiTxStream`, leaving `Ztls` as a thinner `ptls` policy layer.

The main remaining correctness issue is the `txStream` sizing mismatch described above.
Once that is fixed, the rest of the diff reads mostly as an intentional redesign rather than an incomplete port.

**Verification**
Syntax-checked the current header and representative users with:
```sh
clang++ -w -std=gnu++2b -fsyntax-only -ferror-limit=5 \
  -D__x86_64__ -DCK_USE_CC_BUILTINS=1 -DZ_VMAJOR=10 -DZ_VMINOR=0 -DZ_VPATCH=0 \
  -I. -Izu/src -Izm/src -Izt/src -Ize/src -Izi/src -Iztls/src \
  -include ztls/src/Ztls.hh -xc++ /dev/null

clang++ -w -std=gnu++2b -fsyntax-only -ferror-limit=5 \
  -D__x86_64__ -DCK_USE_CC_BUILTINS=1 -DZ_VMAJOR=10 -DZ_VMINOR=0 -DZ_VPATCH=0 \
  -I. -Izu/src -Izm/src -Izt/src -Ize/src -Izi/src -Iztls/src \
  ztls/example/ZtlsClient.cc

clang++ -w -std=gnu++2b -fsyntax-only -ferror-limit=5 \
  -D__x86_64__ -DCK_USE_CC_BUILTINS=1 -DZ_VMAJOR=10 -DZ_VMINOR=0 -DZ_VPATCH=0 \
  -I. -Izu/src -Izm/src -Izt/src -Ize/src -Izi/src -Iztls/src \
  ztls/example/ZtlsServer.cc

clang++ -w -std=gnu++2b -fsyntax-only -ferror-limit=5 \
  -D__x86_64__ -DCK_USE_CC_BUILTINS=1 -DZ_VMAJOR=10 -DZ_VMINOR=0 -DZ_VPATCH=0 \
  -I. -Izu/src -Izm/src -Izt/src -Ize/src -Izi/src -Iztls/src \
  ztls/test/ZtlsBufHookTest.cc
```
All four syntax checks passed.
