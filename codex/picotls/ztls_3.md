The target should be the first rewrite’s architecture, not the original API: Ztls should be a
thin picotls policy layer over ZiRx, ZiTx, and ZiTxStream, with no restored compatibility
unless it is also the clean API.

1. Define the simplified API boundary.

Remove the compatibility surface that was restored in the second rewrite:

- Drop Ztls::IOQueue, Ztls::RxCursor, and Ztls::IOBufAlloc from ztls/src/Ztls.hh:101.
- Keep only a necessary receive stream type, or make users name the Zi type directly.
- Drop the const char **alpn overloads at ztls/src/Ztls.hh:1128 and ztls/src/Ztls.hh:1284; keep
  the span API.
- Update zws, zhttp, examples, and tests to use the new API instead of preserving old source
  compatibility.

2. Collapse receive dispatch.

Current receive flow still has compatibility-like phase plumbing:

- Cxn::handshake() / Cxn::rcvd() are unused shims at ztls/src/Ztls.hh:84.
- m_handshook is a cross-thread phase flag at ztls/src/Ztls.hh:655.

Replace this with one record callback:

- ZiRx frames TLS records.
- Every record is queued to the TLS thread with the current Cxn identity captured from
  ZiIOContext::cxn.
- On the TLS thread, compare against m_cxn; discard stale records.
- Then decide locally: handshake if !ptls_handshake_is_complete(m_tls), otherwise decrypt
  payload.

That removes recvPayload, handshake_0, rcvd_0, and m_handshook.

3. Fix transmit sizing simply.

The remaining documented correctness issue is the mismatch between txStream() sizing and
send_()’s fixed m_max_plaintext guard at ztls/src/Ztls.hh:477.

Use one invariant:

- Application plaintext record capacity is always TxRecordCapacity - TxMaxOverhead.
- txStream() and txStream_() reserve worst-case tailroom: TxMaxOverhead - m_headroom.
- Remove mutable m_max_plaintext; use the constexpr limit.
- Keep send(const uint8_t *, unsigned) and send_(const uint8_t *, unsigned) as splitters.
- Treat oversized prebuilt buffers as caller errors, not something to recursively re-stream.

This keeps the first rewrite’s zero-copy direction without restoring complex fallback paths.

4. Keep strict picotls buffer invariants.

Do not bring back original copy fallbacks for picotls reallocating buffers. The current hard
checks in flushTxBuf_() and rcvd_() are the right simplification. Make them explicit with
comments/tests: Ztls requires picotls buffer hooks to grow the originating ZiIOBuf, not
allocate detached storage.

5. Clean dead state and lifecycle.

Remove obsolete manual receive fields at ztls/src/Ztls.hh:632. Reset per-connection state
deliberately on new client/server connections, including stale disconnect flags where
appropriate, while preserving the SNI fix: client connect_() must create TLS and set server
name before connected_() sends ClientHello.

6. Make ALPN ownership explicit.

The span ALPN API currently stores pointers into caller-provided spans at ztls/src/
Ztls.hh:1016. Either document static-lifetime requirements, or preferably copy ALPN protocol
bytes into engine-owned storage and make m_alpn point into that storage.

7. Verification gates.

After each patch:

- Syntax-check Ztls.hh, ZtlsClient.cc, ZtlsServer.cc, ZtlsBufHookTest.cc, and affected zws /
  zhttp users.
- Run make -C ztls/test ZtlsBufHookTest.
- Run timeout 10s ./ZtlsBufHookTest from ztls/test.
- Add/strengthen a test for large payload splitting so txStream() can never recurse through the
  oversized-buffer path.

I verified the current tree still syntax-checks, ZtlsBufHookTest rebuilds, and a bounded
runtime run completed successfully. The next patch should start with phases 1 and 2 together:
remove restored API compatibility and collapse record dispatch, then fix transmit sizing in the
same series.

