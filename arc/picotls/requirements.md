## Summary
The goal is to use the new picotls buffer allocation/free hooks in the local fork to make Ztls zero-copy paths more resilient, remove the fixed `HandshakeBufSize`, and avoid copy fallbacks when picotls grows buffers. This requires wiring the hooks at Ztls init, tagging Ztls-managed buffers with their owning `ZiIOBuf`, and defining safe growth semantics when picotls asks for more capacity.

Key findings:
- The local picotls fork exposes `ptls_buffer_alloc` / `ptls_buffer_free` and a new `ptls_buffer_t::origin` field that can distinguish internal buffers from application-owned buffers.
- Ztls currently avoids picotls reallocation by pre-sizing handshake buffers and falls back to copy when `ptls_send` / `ptls_receive` reallocate.
- Other TLS stacks expose allocator hooks and expect them to be installed early in process initialization, which aligns with setting picotls hooks inside Ztls init.

## Product Requirements

### Buffer Hook Installation
- Description: Install Ztls-owned implementations of `ptls_buffer_alloc` and `ptls_buffer_free` during TLS library initialization so picotls uses Ztls/ Zi allocators consistently.
- Change (IS -> WILL BE): IS: picotls uses its default allocator hooks and Ztls does not override them. WILL BE: `Ztls::Backend::init()` (or `Ztls::lib_init()`) sets the global `ptls_buffer_alloc` / `ptls_buffer_free` pointers exactly once, before any TLS sessions are created.
- Connections: Enables the ZiIOBuf-aware allocation behavior in the next requirements and depends on early initialization ordering (see Options).

### Origin-Tagged ptls Buffers
- Description: Mark Ztls-managed `ptls_buffer_t` instances with their owning `ZiIOBuf` so the hooks can detect application buffers vs internal picotls buffers.
- Change (IS -> WILL BE): IS: `ptls_buffer_init` is called with raw pointers; `origin` remains NULL, and capacity often equals current length. WILL BE: Ztls sets `buf.origin = zbuf.ptr()` (or equivalent) after `ptls_buffer_init` for handshake send buffers, alert send buffers, and record send buffers, and uses capacity that reflects available buffer size rather than current payload length when safe.
- Connections: Required for the hook implementation to know when to grow `ZiIOBuf` vs allocate internal memory, and feeds the zero-copy behavior changes.

### ZiIOBuf-Aware ptls_buffer_alloc/free
- Description: Implement hook functions that mirror picotls default behavior while using Zi allocation APIs and `ZiIOBuf` for application buffers.
- Change (IS -> WILL BE): IS: picotls allocates with `malloc`/`free`, copies `buf->off` on growth, and uses no application-specific logic. WILL BE: 
  - For `origin == NULL`: allocate/free via Zi’s allocator (e.g., `Zi::VHeap::valloc` / `vfree`) while preserving the default semantics (alignment handling, copying `buf->off`, clearing old memory, setting `is_allocated` and `align_bits`).
  - For `origin != NULL`: grow the owning `ZiIOBuf` to `new_capacity`, update `buf->base` and `buf->capacity`, and ensure `buf->align_bits` reflects the request.
  - For `ptls_buffer_free`: route frees to `ZiIOBuf::free` for application buffers and Zi allocator free for internal buffers, preserving clearing behavior.
- Connections: Depends on a safe ZiIOBuf growth mechanism (next requirement) and is used by the zero-copy path adjustments.

### ZiIOBuf Growth Semantics (Headroom + In-Place Safety)
- Description: Provide a ZiIOBuf growth helper that can safely expand capacity for picotls while preserving already-written output bytes and any in-place plaintext region, even when `skip` is non-zero.
- Change (IS -> WILL BE): IS: `ZiIOBuf::ensure` asserts `skip == 0` and copies `length` only, which is insufficient for in-place TLS send buffers with headroom. WILL BE: add a short, module-conformant helper (either in `ZiIOBuf` or Ztls) that:
  - grows to a requested capacity,
  - preserves a specified prefix length (e.g., `buf->off`),
  - keeps `skip` / `length` consistent for in-place send buffers,
  - respects alignment requests or explicitly falls back when they cannot be met.
- Connections: Required by the hook implementation to safely service `ptls_buffer_alloc` for send buffers; directly affects zero-copy resiliency.

### Zero-Copy Resilience and Handshake Buffer Removal
- Description: Remove the fixed handshake buffer sizing and adjust Ztls logic to treat hook-driven growth as zero-copy, avoiding copy fallbacks when buffers stay within `ZiIOBuf`.
- Change (IS -> WILL BE): IS: `HandshakeBufSize` is enforced and Ztls treats any `ptls_buffer_t` base change as a copy fallback. WILL BE:
  - `HandshakeBufSize` constant and pre-ensure logic are removed.
  - `handshake_send_`, `send_alert_`, and `send_record_` use origin-aware checks to decide whether output stays in the same `ZiIOBuf`, and only copy when hooks cannot preserve ownership or safety.
  - `ptls_buffer_dispose` is only called when buffers have detached from `ZiIOBuf` or after explicit copy.
- Connections: Depends on requirements 1–4 and influences diagnostics/tests.

### Alignment Policy and Fallback Rules
- Description: Define an explicit alignment and fallback policy for when picotls requests alignments that exceed ZiIOBuf’s guarantees or when in-place safety cannot be preserved.
- Change (IS -> WILL BE): IS: alignment is enforced by preconditions and triggers copy fallback if reallocation occurs. WILL BE: a documented, tested policy that:
  - treats `align_bits <= log2(ZiIOBuf_Align)` as safe for ZiIOBuf growth,
  - falls back to internal allocation or copy if requested alignment exceeds ZiIOBuf constraints,
  - guarantees that failure paths remain correct (no leaks, no use-after-free).
- Connections: Drives hook behavior and zero-copy decisions; should be reflected in tests and logs.

### Diagnostics and Tests
- Description: Update logs and tests to validate the new hook-based zero-copy behavior and buffer growth handling.
- Change (IS -> WILL BE): IS: warnings fire when reallocation occurs and no targeted tests exist for buffer hooks. WILL BE:
  - warnings are emitted only when a real copy fallback occurs or when hook growth fails,
  - add or extend ztls tests to force buffer growth (handshake and record send/recv) and assert expected ownership/zero-copy behavior,
  - document any new test binaries under `ztls/test/`.
- Connections: Covers all prior requirements and provides regression protection.

## Options and Open Questions
- Should `ptls_buffer_free` for application buffers always call `ZiIOBuf::free`, or should buffers remain owned by the IOBuf for reuse and only be cleared? What is the preferred lifetime model for TLS buffers in Ztls?
  Answer: buffers should remain owned by the IOBuf, they will be freed when the last ZiIOBuf reference goes out of scope (it is intrusively reference counted)
- For in-place `ptls_send`, is it acceptable to reallocate the underlying buffer (potentially moving plaintext) or should we guarantee capacity ahead of time to avoid reallocation entirely?
  Answer: guarantee capacity ahead of time to avoid reallocation entirely
- Should `ptls_buffer_init` use `zbuf->size` as capacity in all Ztls call sites, or keep `length` for receive-side buffers to avoid accidental growth in in-place decrypt?
  Answer: use `zbuf->size` consistently
- Where should the ZiIOBuf growth helper live (Zi vs Ztls) to match layering and reuse expectations?
  Answer: enhance Zi::IOBuf::ensure() for non-zero skip, and re-use that consistently; there is no need for a new function
- If `align_bits` exceeds `ZiIOBuf_Align`, should we route to internal allocation (with copy) or dynamically allocate a higher-alignment buffer?
  Answer: picotls maximum `align_bits` is `6`; ZiIOBuf_Align is `(1<<6) == 64`; there is no possibility of a higher-alignment buffer being needed
