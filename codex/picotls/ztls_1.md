# Ztls.hh Review

Reviewed [`ztls/src/Ztls.hh`](/home/count0/src/z/ztls/src/Ztls.hh) against [`ztls/src/Ztls.hh.orig`](/home/count0/src/z/ztls/src/Ztls.hh.orig). The rewrite is materially smaller (`1217` lines vs `1525`) and clearly tries to remove bespoke queueing/copy paths in favor of framework helpers, but it is not yet a safe drop-in replacement.

**Findings**
1. Critical: the rewritten header does not currently compile.
The basic syntax check
```sh
clang++ -std=gnu++2b -fsyntax-only -ferror-limit=0 \
  -D__x86_64__ -DCK_USE_CC_BUILTINS=1 -DZ_VMAJOR=10 -DZ_VMINOR=0 -DZ_VPATCH=0 \
  -I. -Izu/src -Izm/src -Izt/src -Ize/src -Izi/src -Iztls/src \
  -include ztls/src/Ztls.hh -xc++ /dev/null
```
produced `42` errors. The refactor is incomplete in several places:
[`ztls/src/Ztls.hh:81`](/home/count0/src/z/ztls/src/Ztls.hh#L81) and [`ztls/src/Ztls.hh:84`](/home/count0/src/z/ztls/src/Ztls.hh#L84) use `IOBuf` instead of `ZiIOBuf`.
[`ztls/src/Ztls.hh:97`](/home/count0/src/z/ztls/src/Ztls.hh#L97) to [`ztls/src/Ztls.hh:101`](/home/count0/src/z/ztls/src/Ztls.hh#L101) make `parseHdr` a free function, but it still references `RxBufAlloc::MaxSize`, which only existed in template scope before.
[`ztls/src/Ztls.hh:110`](/home/count0/src/z/ztls/src/Ztls.hh#L110) and [`ztls/src/Ztls.hh:111`](/home/count0/src/z/ztls/src/Ztls.hh#L111) use `ZiRx` / `ZiTx` without including their headers, and the old public aliases `IOQueue`, `RxStream`, and `IOBufAlloc` were removed instead of being replaced.
[`ztls/src/Ztls.hh:259`](/home/count0/src/z/ztls/src/Ztls.hh#L259) and [`ztls/src/Ztls.hh:260`](/home/count0/src/z/ztls/src/Ztls.hh#L260) use the unknown type `ZIOBuf`.
[`ztls/src/Ztls.hh:151`](/home/count0/src/z/ztls/src/Ztls.hh#L151) to [`ztls/src/Ztls.hh:218`](/home/count0/src/z/ztls/src/Ztls.hh#L218) contain multiple unfinished control-flow edits: `m_handshook` is used but never declared, `buf` is referenced from lambdas without being captured, and `n` is used without a declaration.
[`ztls/src/Ztls.hh:914`](/home/count0/src/z/ztls/src/Ztls.hh#L914) to [`ztls/src/Ztls.hh:917`](/home/count0/src/z/ztls/src/Ztls.hh#L917) convert ALPN data to `ptls_iovec_t` using `ZuCSpan::data()` directly, but `ptls_iovec_t::base` is a `uint8_t *`, not a `const char *`.
[`ztls/src/Ztls.hh:1043`](/home/count0/src/z/ztls/src/Ztls.hh#L1043) to [`ztls/src/Ztls.hh:1047`](/home/count0/src/z/ztls/src/Ztls.hh#L1047) and [`ztls/src/Ztls.hh:1196`](/home/count0/src/z/ztls/src/Ztls.hh#L1196) to [`ztls/src/Ztls.hh:1199`](/home/count0/src/z/ztls/src/Ztls.hh#L1199) pass `ZuCSpan` to C-string APIs without conversion.

2. High: the client path now recreates `ptls_t` after `connect_()` has already set the server name, so the first handshake no longer uses the configured SNI state.
In the old code, `connect_()` created `ptls_t`, set SNI, and `connected__()` only populated handshake properties before starting the handshake: [`ztls/src/Ztls.hh.orig:914`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L914) to [`ztls/src/Ztls.hh.orig:978`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L978).
In the new code, `connect_()` still does `reset_tls_(); ptls_set_server_name(...)` at [`ztls/src/Ztls.hh:610`](/home/count0/src/z/ztls/src/Ztls.hh#L610) to [`ztls/src/Ztls.hh:631`](/home/count0/src/z/ztls/src/Ztls.hh#L631), but `connected_()` immediately calls `reset_tls_()` again at [`ztls/src/Ztls.hh:654`](/home/count0/src/z/ztls/src/Ztls.hh#L654) to [`ztls/src/Ztls.hh:655`](/home/count0/src/z/ztls/src/Ztls.hh#L655) before sending the ClientHello.
That discards the `ptls_t` instance on which SNI was configured. Even if the compile errors are fixed, this is a functional regression relative to the original implementation.

3. High: the rewrite removed the original `ptls` reallocation and oversized-payload fallbacks, so correctness now depends on stricter hidden assumptions.
The original code explicitly handled `ptls` growing or relocating output buffers in both the handshake path and the data path:
[`ztls/src/Ztls.hh.orig:506`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L506) to [`ztls/src/Ztls.hh.orig:529`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L529) copied handshake output when `sendbuf.base != base`.
[`ztls/src/Ztls.hh.orig:575`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L575) to [`ztls/src/Ztls.hh.orig:623`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L623) did the same for `ptls_send()`.
[`ztls/src/Ztls.hh.orig:457`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L457) to [`ztls/src/Ztls.hh.orig:480`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L480) also re-framed oversized plaintext through `txStream_()`.
The new code removes all of those branches and immediately forwards the original buffer via `Tx::send`:
[`ztls/src/Ztls.hh:267`](/home/count0/src/z/ztls/src/Ztls.hh#L267) to [`ztls/src/Ztls.hh:273`](/home/count0/src/z/ztls/src/Ztls.hh#L273),
[`ztls/src/Ztls.hh:391`](/home/count0/src/z/ztls/src/Ztls.hh#L391) to [`ztls/src/Ztls.hh:450`](/home/count0/src/z/ztls/src/Ztls.hh#L450),
[`ztls/src/Ztls.hh:461`](/home/count0/src/z/ztls/src/Ztls.hh#L461) to [`ztls/src/Ztls.hh:466`](/home/count0/src/z/ztls/src/Ztls.hh#L466).
That is a meaningful behavior change, not just a cleanup. The original code had warning-and-copy fallbacks because those cases were possible. The rewrite currently assumes they are impossible, but does not prove that.

4. High: the public `Ztls` API is no longer source-compatible with the rest of the tree.
The original header exported `IOQueue`, `RxStream`, and `IOBufAlloc` in the `Ztls` namespace: [`ztls/src/Ztls.hh.orig:86`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L86) to [`ztls/src/Ztls.hh.orig:96`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L96). The rewrite removed those aliases, but downstream code still uses them:
[`zws/src/Zws.hh:24`](/home/count0/src/z/zws/src/Zws.hh#L24),
[`zhttp/test/zhttpclient.cc:64`](/home/count0/src/z/zhttp/test/zhttpclient.cc#L64),
[`ztls/example/ZtlsClient.cc:79`](/home/count0/src/z/ztls/example/ZtlsClient.cc#L79),
[`ztls/example/ZtlsServer.cc:51`](/home/count0/src/z/ztls/example/ZtlsServer.cc#L51),
[`ztls/test/ZtlsBufHookTest.cc:142`](/home/count0/src/z/ztls/test/ZtlsBufHookTest.cc#L142).
The init API also changed from `const char **alpn` to `ZuSpan<ZuCSpan>` at [`ztls/src/Ztls.hh:1005`](/home/count0/src/z/ztls/src/Ztls.hh#L1005) to [`ztls/src/Ztls.hh:1007`](/home/count0/src/z/ztls/src/Ztls.hh#L1007) and [`ztls/src/Ztls.hh:1113`](/home/count0/src/z/ztls/src/Ztls.hh#L1113) to [`ztls/src/Ztls.hh:1116`](/home/count0/src/z/ztls/src/Ztls.hh#L1116), but existing call sites still pass null-terminated ALPN arrays:
[`zws/src/Zws.hh:239`](/home/count0/src/z/zws/src/Zws.hh#L239) to [`zws/src/Zws.hh:241`](/home/count0/src/z/zws/src/Zws.hh#L241),
[`zhttp/test/zhttpclient.cc:115`](/home/count0/src/z/zhttp/test/zhttpclient.cc#L115) and [`zhttp/test/zhttpclient.cc:133`](/home/count0/src/z/zhttp/test/zhttpclient.cc#L133),
[`ztls/example/ZtlsClient.cc:203`](/home/count0/src/z/ztls/example/ZtlsClient.cc#L203) and [`ztls/example/ZtlsClient.cc:221`](/home/count0/src/z/ztls/example/ZtlsClient.cc#L221),
[`ztls/example/ZtlsServer.cc:130`](/home/count0/src/z/ztls/example/ZtlsServer.cc#L130) and [`ztls/example/ZtlsServer.cc:161`](/home/count0/src/z/ztls/example/ZtlsServer.cc#L161).

5. Medium: receive and disconnect control flow no longer preserves the original “drain buffered data before teardown” behavior.
The original disconnect path explicitly drained queued ciphertext after handshake completion before resetting state: [`ztls/src/Ztls.hh.orig:170`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L170) to [`ztls/src/Ztls.hh.orig:175`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L175).
The original `handshake_done_()` also immediately re-entered `recv()` so pending post-handshake records/plaintext were drained before returning: [`ztls/src/Ztls.hh.orig:756`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L756) to [`ztls/src/Ztls.hh.orig:787`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L787).
The rewrite simplifies that away. `disconnected_()` now resets immediately at [`ztls/src/Ztls.hh:343`](/home/count0/src/z/ztls/src/Ztls.hh#L343) to [`ztls/src/Ztls.hh:346`](/home/count0/src/z/ztls/src/Ztls.hh#L346), and `handshook()` just invokes the application callback and returns at [`ztls/src/Ztls.hh:220`](/home/count0/src/z/ztls/src/Ztls.hh#L220) to [`ztls/src/Ztls.hh:255`](/home/count0/src/z/ztls/src/Ztls.hh#L255).
That is simpler, but it is not behaviorally equivalent. Late-arriving or already-queued records are more likely to be dropped.

6. Medium: the refactor only partially migrated to `ZiRx` / `ZiTx`, so the new control flow is less coherent than it first appears.
Architecturally, the intent is clear: replace manual TCP/TLS queue management with framework components. But the migration is only half-done:
Old receive path: manual `io.init` + `rx()` + `m_rxInQueue` + `m_rxPlainQueue` + `process_plaintext_()` in [`ztls/src/Ztls.hh.orig:139`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L139) to [`ztls/src/Ztls.hh.orig:360`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L360).
New receive path: intended `ZiRx::recv<parseHdr, Body>` + `handshake_0` / `rcvd_0` + single `m_rxStream` in [`ztls/src/Ztls.hh:151`](/home/count0/src/z/ztls/src/Ztls.hh#L151) to [`ztls/src/Ztls.hh:313`](/home/count0/src/z/ztls/src/Ztls.hh#L313).
Old transmit path: custom `txOutQueue`, `txOut()`, `flush_sendbuf_()`, `ptls_buf_send_()` in [`ztls/src/Ztls.hh.orig:442`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L442) to [`ztls/src/Ztls.hh.orig:717`](/home/count0/src/z/ztls/src/Ztls.hh.orig#L717).
New transmit path: `ZiTx::send` plus smaller helpers in [`ztls/src/Ztls.hh:350`](/home/count0/src/z/ztls/src/Ztls.hh#L350) to [`ztls/src/Ztls.hh:466`](/home/count0/src/z/ztls/src/Ztls.hh#L466).
However, the rewrite still carries leftover state from the removed design at [`ztls/src/Ztls.hh:534`](/home/count0/src/z/ztls/src/Ztls.hh#L534) to [`ztls/src/Ztls.hh:538`](/home/count0/src/z/ztls/src/Ztls.hh#L538), and it introduces a new cross-thread phase flag (`m_handshook`) in the dispatch logic at [`ztls/src/Ztls.hh:155`](/home/count0/src/z/ztls/src/Ztls.hh#L155) to [`ztls/src/Ztls.hh:169`](/home/count0/src/z/ztls/src/Ztls.hh#L169) and [`ztls/src/Ztls.hh:181`](/home/count0/src/z/ztls/src/Ztls.hh#L181) to [`ztls/src/Ztls.hh:190`](/home/count0/src/z/ztls/src/Ztls.hh#L190). Even if that member is added, it will need explicit synchronization rules to avoid becoming a new data race between I/O and TLS threads.

**Implementation Changes**
Control flow:
The original file handled TCP framing itself in `connected_()` / `rx()`, pushed whole TLS records into `m_rxInQueue`, then let the TLS thread drive handshake and plaintext delivery from there. The rewrite tries to move the TCP framing step into `ZiRx`, with `connected_0()` arming a `recv<parseHdr, ...>` pipeline and dispatching each complete TLS record to either `handshake_0()` or `rcvd_0()`.
Client handshake startup changed from “lazy” to “eager”. In the original code, the client started the handshake from `handshake()` when first entered on the TLS thread; in the rewrite, `CliLink::connected_()` constructs the ClientHello and sends it immediately via `handshake__(nullptr, nullptr)`.
Plaintext delivery is also flatter. The old design used `m_rxPlainQueue` plus `process_plaintext_()` so `process()` could consume incrementally while buffers remained queued. The new design pushes decrypted buffers straight into `m_rxStream` and loops on `impl()->process(m_rxStream)` directly.

Data structures:
The old design exposed and used `IOQueue`, `RxStream`, and `IOBufAlloc` as `Ztls` vocabulary types. The rewrite removes those aliases and adds heap-tagged vocabulary types `Ticket` and `ALPN` instead: [`ztls/src/Ztls.hh:37`](/home/count0/src/z/ztls/src/Ztls.hh#L37) to [`ztls/src/Ztls.hh:40`](/home/count0/src/z/ztls/src/Ztls.hh#L40).
State was reduced aggressively. The original `Link` tracked `m_txOutQueue`, `m_rxInQueue`, `m_rxPlainQueue`, `m_max_plaintext`, `m_tx_realloc_warned`, `m_rx_realloc_warned`, and `m_handshakeStarted`. The rewrite collapses that to `ZiTx` queueing plus a single `m_rxStream`, and renames `m_tx_headroom` / `m_tx_tailroom` to `m_headroom` / `m_tailroom`.
That direction makes sense for simplification, but it also removes explicit buffering and fallback state that the original code used to preserve correctness under non-ideal `ptls` behavior.

Function naming:
The old internal names were mostly semantic: `connected_2`, `connected__`, `recv_`, `handshake_done_`, `process_plaintext_`, `txStream_send_`, `ptls_buf_send_`.
The rewrite switches to phase-numbered names around thread hops: `connected_0`, `connected_1`, `connected_`, `handshake_0`, `handshake_`, `handshake__`, `handshook`, `rcvd_0`, `rcvd_`.
That does make the intended thread boundaries more explicit, but it also makes the call graph harder to read because the names only make sense when every stage is already understood.

Framework integration:
The biggest architectural change is the new dependency on `ZiRx` and `ZiTx`. The original file only used `ZiRxStream` and `ZiTxStream`; the rewrite tries to reuse the full generic framing and send-queue machinery instead of maintaining a local copy in `Ztls`.
The callback setup in `Client::init()` and `Server::init()` is also cleaner. The rewrite switches from inline aggregate assignment of `ctx->save_ticket` / `ctx->on_client_hello` to named static callback objects and pointer assignment.
The include/assertion side was also adjusted: `ZeAssert.hh` was replaced by `ZiAssert.hh`, presumably to align the header with the `Zi*` framework layer it now depends on.

**Overall Assessment**
The intended direction is sound: reduce duplicated `ptls` wrapper code, reuse `ZiRx` / `ZiTx`, cut down queue/copy machinery, and tighten the public data vocabulary. Relative to the original file, though, the rewrite is still at an intermediate state rather than a finished simplification.
Before this can be treated as a valid replacement for [`ztls/src/Ztls.hh.orig`](/home/count0/src/z/ztls/src/Ztls.hh.orig), it needs at least:
the compile errors fixed,
the client SNI regression removed,
public aliases or downstream call sites reconciled,
and an explicit decision on whether the old `ptls` reallocation / buffered-data guarantees are intentionally being dropped or still need to be preserved.
