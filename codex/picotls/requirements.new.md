## Summary
The goal is to use the new picotls buffer allocation/free hooks in the local fork to make Ztls zero-copy paths more resilient, remove the fixed `HandshakeBufSize`, and avoid copy fallbacks when picotls grows buffers. This requires wiring the hooks at Ztls init, tagging Ztls-managed buffers with their owning `ZiIOBuf`, and defining safe growth semantics when picotls asks for more capacity.

IMPORTANT: DO NOT use canonical online references for picotls; this is using a local fork

Key findings:
- The local picotls fork exposes `ptls_buffer_alloc` / `ptls_buffer_free` and a new `ptls_buffer_t::origin` field that can distinguish internal buffers from application-owned buffers.
- Ztls currently avoids picotls reallocation by pre-sizing handshake buffers and falls back to copy when `ptls_send` / `ptls_receive` reallocate.
- Other TLS stacks expose allocator hooks and expect them to be installed early in process initialization, which aligns with setting picotls hooks inside Ztls init.

## Product Requirements

### Buffer Hook Installation
- Description: Install Ztls-owned implementations of `ptls_buffer_alloc` and `ptls_buffer_free` during TLS library initialization so picotls uses Ztls/Zi allocators consistently.
- Change (IS -> WILL BE): IS: picotls uses its default allocator hooks and Ztls does not override them. WILL BE: `Ztls::Backend::init()` (or `Ztls::lib_init()`) sets the global `ptls_buffer_alloc` / `ptls_buffer_free` pointers exactly once, before any TLS sessions are created.
- Connections: Enables the ZiIOBuf-aware allocation behavior in the next requirements and depends on early initialization ordering.

### Origin-Tagged ptls Buffers
- Description: Mark Ztls-managed `ptls_buffer_t` instances with their owning `ZiIOBuf` so the hooks can detect application buffers vs internal picotls buffers.
- Change (IS -> WILL BE): IS: `ptls_buffer_init` is called with raw pointers; `origin` remains NULL, and capacity often equals current length. WILL BE:
  - Ztls sets `buf.origin = zbuf.ptr()` (or equivalent) immediately after `ptls_buffer_init` for handshake send buffers, alert send buffers, and record send buffers.
  - Ztls uses `zbuf->size` as capacity consistently across its `ptls_buffer_init` call sites, including receive-side buffers, so picotls sees the full available capacity.
- Connections: Required for the hook implementation to know when to grow `ZiIOBuf` vs allocate internal memory, and feeds the zero-copy behavior changes.

### ZiIOBuf-Aware ptls_buffer_alloc/free
- Description: Implement hook functions that mirror picotls default behavior while using Zi allocation APIs and `ZiIOBuf` for application buffers.
- Change (IS -> WILL BE): IS: picotls allocates with `malloc`/`free`, copies `buf->off` on growth, and uses no application-specific logic. WILL BE:
  - For `origin == NULL`: allocate/free via Zi’s allocator (e.g., `Zi::VHeap::valloc` / `vfree`) while preserving the default semantics (alignment handling, copying `buf->off`, clearing old memory, setting `is_allocated` and `align_bits`).
  - For `origin != NULL`: grow the owning `ZiIOBuf` to `new_capacity`, update `buf->base` and `buf->capacity`, and ensure `buf->align_bits` reflects the request. Growth must preserve the existing written prefix (`buf->off`) and any in-place plaintext region (see growth semantics below).
  - For `ptls_buffer_free`:
    - For internal buffers (`origin == NULL`), free via the Zi allocator, preserving any default zeroing behavior.
    - For application buffers (`origin != NULL`), do not transfer ownership away from the `ZiIOBuf`; instead use the ZiIOBuf API that releases/reset the view while keeping the underlying memory managed by the `ZiIOBuf` refcount (buffers are freed when the last `ZiIOBuf` reference goes out of scope).
- Connections: Depends on safe ZiIOBuf growth semantics and is used by the zero-copy path adjustments.

### ZiIOBuf Growth Semantics (Headroom + In-Place Safety)
- Description: Enhance `Zi::IOBuf::ensure()` to safely expand capacity for picotls while preserving already-written output bytes and any in-place plaintext region, even when `skip` is non-zero.
- Change (IS -> WILL BE): IS: `ZiIOBuf::ensure` asserts `skip == 0` and copies `length` only, which is insufficient for in-place TLS send buffers with headroom. WILL BE: `ZiIOBuf::ensure()` is enhanced (no new helper function) to:
  - grow to a requested capacity,
  - preserve the prefix `>= data() < end()`, i.e. `>= skip` and `< (skip + length)`
  - keep `skip` / `length` consistent for in-place send buffers,
  - respect alignment requests or explicitly fall back when they cannot be met.
- before `ensure` is called, IOBuf skip and length will be updated to align with ptls
- Connections: Required by the hook implementation to safely service `ptls_buffer_alloc` for send buffers; directly affects zero-copy resiliency.

### Zero-Copy Resilience and Handshake Buffer Removal
- Description: Remove the fixed handshake buffer sizing and adjust Ztls logic to treat hook-driven growth as zero-copy, avoiding copy fallbacks when buffers stay within `ZiIOBuf`.
- Change (IS -> WILL BE): IS: `HandshakeBufSize` is enforced and Ztls treats any `ptls_buffer_t` base change as a copy fallback. WILL BE:
  - `HandshakeBufSize` constant and pre-ensure logic are removed.
  - In-place `ptls_send` paths guarantee capacity ahead of time (based on required TLS record overhead and plaintext length) so reallocation is avoided entirely for these operations.
  - `handshake_send_`, `send_alert_`, and `send_record_` use origin-aware checks to decide whether output stays in the same `ZiIOBuf`, and only copy when hooks cannot preserve ownership or safety.
  - `ptls_buffer_dispose` is only called when buffers have detached from `ZiIOBuf` or after explicit copy.
- Connections: Depends on requirements above and influences diagnostics/tests.

### Alignment Policy and Fallback Rules
- Description: Define an explicit alignment and fallback policy for when picotls requests alignments that exceed ZiIOBuf’s guarantees or when in-place safety cannot be preserved.
- Change (IS -> WILL BE): IS: alignment is enforced by preconditions and triggers copy fallback if reallocation occurs. WILL BE: a documented, tested policy that:
  - treats `align_bits <= log2(ZiIOBuf_Align)` as safe for ZiIOBuf growth,
  - acknowledges picotls maximum `align_bits` is `6` and `ZiIOBuf_Align` is `(1<<6) == 64`, so higher alignment should never be requested,
  - still defines a defensive fallback (internal allocation + copy) if an unexpected alignment request exceeds ZiIOBuf constraints, with no leaks or use-after-free.
- Connections: Drives hook behavior and zero-copy decisions; should be reflected in tests and logs.

### Diagnostics and Tests
- Description: Update logs and tests to validate the new hook-based zero-copy behavior and buffer growth handling.
- Change (IS -> WILL BE): IS: warnings fire when reallocation occurs and no targeted tests exist for buffer hooks. WILL BE:
  - warnings are emitted only when a real copy fallback occurs or when hook growth fails,
  - add or extend ztls tests to force buffer growth (handshake and record send/recv) and assert expected ownership/zero-copy behavior,
  - document any new test binaries under `ztls/test/`.
- Connections: Covers all prior requirements and provides regression protection.

## Resolved Decisions and Constraints
- Application buffers remain owned by the `ZiIOBuf`; they are freed when the last `ZiIOBuf` reference goes out of scope. Hook code must not reclaim that memory early.
- In-place `ptls_send` must avoid reallocation by guaranteeing capacity ahead of time; do not rely on reallocation during send.
- Use `zbuf->size` for capacity in all Ztls `ptls_buffer_init` call sites.
- Enhance `Zi::IOBuf::ensure()` for non-zero `skip` and reuse it instead of adding a new helper.
- picotls maximum `align_bits` is `6`, matching `ZiIOBuf_Align == 64`; higher alignment requirements are not expected.
