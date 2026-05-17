## Summary
The goal is to integrate the local picotls buffer alloc/free hooks so Ztls can grow buffers in-place via ZiIOBuf, remove the fixed handshake buffer size, and treat hook-driven growth as zero-copy. The plan installs global hooks once during Ztls init, tags every Ztls ptls_buffer_t with its owning ZiIOBuf (including receive buffers), extends ZiIOBuf::ensure() to preserve data with non-zero skip, and updates send/receive paths to treat origin-owned growth as zero-copy; any alignment or ensure failures are treated as fatal disconnects (no silent copy). All picotls API behavior is verified against the local fork under `../picotls/` (not upstream docs) before finalizing call semantics.

Comparable TLS stacks use global allocator hooks and/or explicit buffer preallocation to avoid allocations during data processing: OpenSSL documents SSL_alloc_buffers as a way to avoid allocations during data processing and references using custom allocators via CRYPTO_set_mem_functions, mbedTLS exposes global `mbedtls_calloc`/`mbedtls_free` hooks and allows replacing them via `mbedtls_platform_set_calloc_free` or the memory buffer allocator, and wolfSSL lets applications install custom allocators via `wolfSSL_SetAllocators` and routes allocations through its own wrappers. citeturn8view0turn8view1turn12view0 These patterns reinforce the plan to install picotls hooks early and to avoid mid-flight allocations by ensuring buffer capacity up front.

## Architecture Documentation
- New or changed components
  - New `ztls/src/ZtlsPico.cc` (and small header if needed) implementing `ptls_buffer_alloc` / `ptls_buffer_free` hooks plus a one-time install helper.
  - Enhanced `Zi::IOBuf::ensure()` to handle non-zero `skip` while preserving active data.
- New or changed processes or threads
  - None (hooks execute in existing TLS thread contexts).
- New or changed interfaces
  - Ztls init path installs global picotls buffer hooks exactly once.
  - ptls_buffer_t instances are tagged with `origin = ZiIOBuf*` (including Rx buffers).
- New or changed data flows
  - TLS handshake/send/receive buffers remain within ZiIOBuf on growth, avoiding copy fallbacks.
- New or changed event-driven or timer processing
  - None.
- New or changed network programming
  - TLS send/receive logic now treats origin-owned reallocation as zero-copy and disconnects on hook failure.
- New or changed data stores
  - None.

## Detailed Design and Implementation Plan

### Phase 1: Scrutinize local picotls hooks + add ZtlsPico module
- Focus: validate local fork contract and introduce hook installation + allocator behavior.
- Scrutinize local fork APIs (no upstream references):
  - `ptls_buffer_alloc` is only for growth and must copy `buf->off` bytes, clear old memory via `ptls_clear_memory`, free old base when `buf->is_allocated`, and update `buf->base/capacity/is_allocated/align_bits` before returning the new base. (`../picotls/include/picotls.h`, `../picotls/lib/picotls.c`)
  - `ptls_buffer_free` is only called for `is_allocated` buffers; smallbuf passed to `ptls_buffer_init` is never freed by picotls, and `buf->origin` distinguishes application buffers from internal buffers. (`../picotls/include/picotls.h`)
  - `ptls_buffer_init` always sets `origin = NULL`, so Ztls must set `origin` immediately after init. (`../picotls/include/picotls.h`)
  - `ptls_buffer_reserve_aligned` may trigger growth when capacity is insufficient or alignment requirements are stricter than the current base alignment. (`../picotls/lib/picotls.c`)
- Add new module files:
  - `ztls/src/ZtlsPico.cc` (hook implementations + one-time installer)
  - `ztls/src/ZtlsPico.hh` only if a public declaration is needed (otherwise keep static and forward-declare in `ZtlsOpenSSL.cc`)
- Hook installation:
  - Call installer from `Ztls::Backend::init()` (invoked by `Ztls::lib_init()`), once per process, before any TLS sessions.
  - Mirror existing `static bool done` pattern for one-time init to match codebase conventions.
- Hook behavior design (aligns with local picotls contract):
  - **Internal buffers (`origin == NULL`)**
    - Allocate with `Zi::VHeap::valloc`, copy `buf->off`, clear old memory, free old base via `Zi::VHeap::vfree` if `buf->is_allocated`, and update `buf` fields exactly as picotls expects.
  - **Application buffers (`origin != NULL`)**
    - Treat `origin` as `ZiIOBuf*` and grow via `ZiIOBuf::ensure(new_capacity)`.
    - Temporarily adjust `skip/length` so that the preserved region is exactly `[buf->base, buf->base + buf->off)` (per feedback: only written prefix must be preserved), then restore original `skip/length` after growth.
    - Update `buf->base` (to `data() - skip`), `buf->capacity` (use `zbuf->size`), `buf->is_allocated = 1`, and `buf->align_bits = align_bits`.
  - **Alignment policy**
    - `align_bits <= log2(ZiIOBuf_Align)` is required for origin-owned growth; any higher value is treated as an invariant violation that returns NULL (causing TLS error -> disconnect). The defensive internal-allocation path remains available for internal buffers only.
  - **Free policy**
    - `ptls_buffer_free` for internal buffers uses `Zm::VHeap::vfree` (after `ptls_clear_memory`).
    - For origin buffers, do not free underlying memory; just reset `buf` fields as needed, letting ZiIOBuf lifetime manage memory.
- Complexity/feasibility: low-to-medium. Correctness hinges on faithfully mirroring the local fork’s buffer semantics and alignment rules.

### Phase 2: ZiIOBuf::ensure() growth semantics for non-zero skip
- Focus: allow safe growth when `skip != 0` while preserving active data used by TLS in-place buffers.
- Modify `Zi::IOBuf::ensure()` to:
  - Remove the `ZmAssert(!skip)` guard and allow non-zero skip.
  - Preserve the active region `[skip, skip + length)` across reallocation (memcpy/memmove as needed).
  - Keep `skip`/`length` unchanged after growth.
  - Retain existing alignment guarantees based on `ZiIOBuf_Align` and `Zm::VHeap::valloc`.
- Add a small helper (local to `ZtlsPico.cc`) to temporarily adjust `skip/length` for the preserve-prefix requirement before calling `ensure()`.
- Complexity/feasibility: medium. Requires careful pointer arithmetic and invariants to avoid data loss with headroom.

### Phase 3: Ztls zero-copy path updates + origin tagging
- Focus: tag buffers, remove HandshakeBufSize, and treat origin-owned growth as zero-copy.
- Origin tagging + capacity usage:
  - After each `ptls_buffer_init`, set `buf.origin = zbuf.ptr()` and pass `zbuf->size` as capacity (including receive-side buffers) so picotls sees full available capacity.
  - Apply to handshake send buffers, alert send buffers, record send buffers, and receive buffers.
- Remove fixed handshake sizing:
  - Delete `HandshakeBufSize` and the pre-ensure logic in `handshake_send_()`.
  - Rely on hooks to grow `ZiIOBuf` when handshake output exceeds initial size; this is treated as zero-copy if origin is preserved.
- Zero-copy resilience in send/receive:
  - Update `flush_sendbuf_()` and `send_record_()` to treat `sendbuf.base` changes as zero-copy if `sendbuf.origin` matches the owning `ZiIOBuf` and invariants hold.
  - Only copy and dispose when `sendbuf.origin == NULL` (internal allocation) or when hooks fail (return NULL).
  - On receive, tag `plain.origin = buf.ptr()` and treat reallocation as zero-copy when origin matches; enqueue the same `ZiIOBuf` with `length = plain.off` instead of copying.
- Capacity guarantees for in-place send:
  - For buffers produced by `alloc_txbuf`, ensure `size >= headroom + len + tailroom` before `ptls_send` (reuse existing headroom calculations based on `ptls_get_record_overhead`).
  - For externally supplied buffers, check tailroom/headroom and either `ensure()` or treat as fatal (disconnect) rather than copying.
- Diagnostics:
  - Log warnings only on actual copy fallbacks or hook failures; eliminate warnings for origin-preserving growth.
- Complexity/feasibility: medium-high. Changes are correctness-sensitive; requires careful origin/base invariants and error propagation.

### Phase 4: Diagnostics & Tests
- Add new tests and extend existing ones to validate growth and ownership behavior:
  - New `ztls/test/ZtlsBufHookTest.cc`: use a small `IOBufAlloc<BufSize>` to force growth in handshake and record send paths; assert origin-preserving growth stays zero-copy and no copy warnings fire.
  - Extend `ZtlsClient`/`ZtlsServer` to send large records and validate that `ptls_receive` growth keeps the same ZiIOBuf (origin preserved) and no copy fallback occurs.
  - Add an explicit alignment-mismatch case (e.g., temporarily force `align_bits` > 6 in test harness) and assert the connection disconnects with a clear error path, without leaks.
  - Optional: extend `zi/test/ZiIOBufTest.cc` to cover `ensure()` with non-zero skip and preservation of `[skip, skip + length)`.
- Update `ztls/test/Makefile.am` to include the new test binary and note any new test entry points.

## Code References to Impacted Code
- `ztls/src/ZtlsOpenSSL.cc:289` - `Backend::init()` hook installation point.
- `ztls/src/ZtlsLib.cc:17` - `lib_init()` entry point that calls `Backend::init()`.
- `ztls/src/Ztls.hh:36` - `HandshakeBufSize` constant to remove.
- `ztls/src/Ztls.hh:341` - receive-side `ptls_buffer_init` currently uses `buf->length`.
- `ztls/src/Ztls.hh:468` - `handshake_send_()` pre-ensure and buffer init.
- `ztls/src/Ztls.hh:481` - `send_alert_()` buffer init.
- `ztls/src/Ztls.hh:492` - `flush_sendbuf_()` base-change fallback logic.
- `ztls/src/Ztls.hh:532` - `send_record_()` capacity and zero-copy logic.
- `zi/src/ZiIOBuf.hh:192` - `ensure()` currently asserts `skip == 0`.
- `zi/src/ZiIOBuf.hh:48` - `ZiIOBuf_Align` alignment policy.
- `ztls/src/Makefile.am` - add `ZtlsPico.cc` to `libZtls_la_SOURCES`.
- `ztls/test/Makefile.am` - add new test binary.
- `../picotls/include/picotls.h` - local fork buffer hook contracts and `ptls_buffer_t::origin`.
- `../picotls/lib/picotls.c` - default buffer_alloc/free semantics and growth behavior.

## Detailed Test Plan
- `ZtlsBufHookTest` (new)
  - Configure a small `IOBufAlloc<BufSize>` (e.g., 128–256 bytes) to force growth.
  - Handshake growth: client/server handshake with small buffers; assert `ptls_buffer_alloc` grows in-place and `sendbuf.origin` remains the same.
  - Record send growth: send payloads that exceed initial buffer; verify no copy fallback and `txOut` uses the same ZiIOBuf.
  - Alignment failure: inject an alignment override to force `align_bits > 6`, confirm TLS disconnects and no leak (verify `ptls_buffer_dispose` only runs for internal buffers).
- `ZtlsClient` / `ZtlsServer` (extend)
  - Send large records to force `ptls_receive` growth; assert origin-preserved zero-copy receive path and absence of reallocation warnings.
- `ZiIOBufTest` (optional)
  - Set `skip` and `length` to non-zero values, call `ensure(new_capacity)`, and verify the preserved region `[skip, skip + length)` matches the original contents and `skip/length` are unchanged.

## Options and Open Questions
- Resolved decisions
  - Hook placement: use a new `ztls/src/ZtlsPico.cc` module for picotls hook logic.
  - Receive-side origin tagging: tag Rx buffers so ptls_receive growth can remain zero-copy and buffers can be enqueued without copying.
  - Preserve-region semantics: only the written prefix `[buf->base, buf->base + buf->off)` must be preserved during growth.
  - Failure policy: alignment or ensure failures are treated as fatal disconnects; no silent copy fallback for origin-owned buffers.
- Open questions
  - None; all prior open questions are resolved above.
