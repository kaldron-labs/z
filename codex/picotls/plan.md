## Summary
The goal is to integrate the local picotls buffer alloc/free hooks so Ztls can grow buffers in-place via ZiIOBuf, remove the fixed handshake buffer size, and treat hook-driven growth as zero-copy. The plan installs the hooks during Ztls initialization, tags Ztls ptls_buffer_t instances with their owning ZiIOBuf, expands ZiIOBuf::ensure() to support non-zero skip/length preservation, and updates send/receive paths to rely on origin-aware growth with explicit alignment/fallback rules. Diagnostics and tests will be added to validate buffer growth during handshake and record send/recv without spurious copy fallbacks.

IS -> WILL BE (per requirement):
- Buffer hook installation: IS picotls uses default allocators; WILL BE Ztls init sets ptls_buffer_alloc/free exactly once before TLS sessions.
- Origin-tagged ptls buffers: IS origin is NULL and capacity often equals length; WILL BE Ztls sets buf.origin for handshake/send/alert buffers and uses zbuf->size as capacity for all ptls_buffer_init call sites (including receive).
- ZiIOBuf-aware ptls_buffer_alloc/free: IS malloc/free + copy; WILL BE internal buffers use Zi::VHeap valloc/vfree and application buffers grow via ZiIOBuf::ensure without ownership transfer.
- ZiIOBuf growth semantics: IS ensure asserts skip==0 and copies only length; WILL BE ensure preserves the active region even when skip>0 and is used by hooks after adjusting skip/length to cover written prefix + plaintext.
- Zero-copy resilience & handshake buffer removal: IS HandshakeBufSize and base-change triggers copy fallback; WILL BE no fixed handshake size and origin-aware checks treat in-place growth as zero-copy.
- Alignment policy: IS enforced by preconditions; WILL BE explicit policy with defensive fallback if align_bits exceeds ZiIOBuf_Align.
- Diagnostics/tests: IS warn on any reallocation; WILL BE warn only on real copy fallback or hook failure, plus new ztls tests for growth.

## Architecture Documentation
- New or changed components
  - New Ztls buffer-hook module (e.g., `ztls/src/ZtlsPicotlsBuf.cc` + header) implementing ptls_buffer_alloc/free and a one-time install function.
  - Enhanced `Zi::IOBuf::ensure()` to handle non-zero skip and preserve active regions.
- New or changed processes or threads
  - None (hooks execute in existing picotls/TLS thread contexts).
- New or changed interfaces
  - Global picotls hook pointers set during Ztls init.
  - Origin tagging for ptls_buffer_t tied to ZiIOBuf ownership.
- New or changed data flows
  - TLS send/handshake buffers flow through ZiIOBuf-backed ptls_buffer_t instances; growth stays within ZiIOBuf via hooks.
- New or changed event-driven or timer processing
  - None.
- New or changed network programming
  - TLS record send/receive logic updated to treat origin-owned reallocation as zero-copy.
- New or changed data stores
  - None.

## Detailed Design and Implementation Plan

### Phase 1: Picotls Hook Integration (install + internal allocator behavior)
- Focus: add Ztls-owned ptls_buffer_alloc/free and install them early.
- Add new module file(s):
  - `ztls/src/ZtlsPicotlsBuf.hh` (declarations, small helpers)
  - `ztls/src/ZtlsPicotlsBuf.cc` (hook implementations + one-time install)
- Modify `Ztls::Backend::init()` (or `Ztls::lib_init()`) to call the hook installer exactly once before any TLS sessions are created.
- Scrutinize the local fork contract in `/usr/include/picotls.h` and `../picotls/lib` to ensure we preserve:
  - copy-and-clear semantics for `buf->off` data
  - `is_allocated`, `align_bits`, and `capacity` updates
  - origin usage to distinguish application vs internal buffers
- Hook behavior design:
  - Internal buffers (`origin == NULL`): allocate with `Zi::VHeap::valloc`, copy `buf->off` bytes, clear old data via `ptls_clear_memory`, free with `Zi::VHeap::vfree`.
  - Application buffers (`origin != NULL`): treat `origin` as `ZiIOBuf*`, adjust skip/length to cover the active region (written prefix + plaintext), grow with `ensure(new_capacity)`, update `buf->base`, `buf->capacity`, `buf->align_bits`, keep ownership with ZiIOBuf.
  - Alignment: if `align_bits > log2(ZiIOBuf_Align)`, fall back to internal allocation + copy rather than using ZiIOBuf.

Pseudocode sketch (application-buffer path):
```c++
// ptls_buffer_alloc hook (origin != NULL)
ZiIOBuf *zbuf = static_cast<ZiIOBuf *>(buf->origin);
// preserve both written prefix [buf->base, buf->base + buf->off)
// and in-place plaintext [zbuf->data(), zbuf->end())
PreserveRegion pr = compute_preserve_region(buf, zbuf);
Save original skip/length; set zbuf->skip/length to cover pr;
if (!zbuf->ensure(new_capacity)) return nullptr;
restore skip/length; buf->base = zbuf->data() - zbuf->skip;
buf->capacity = zbuf->size; buf->is_allocated = 1; buf->align_bits = align_bits;
return buf->base;
```

Complexity/feasibility: low-to-medium. Hooks are isolated, but correctness depends on a precise interpretation of the local fork’s ptls_buffer contract and alignment rules.

### Phase 2: ZiIOBuf::ensure() Growth Semantics
- Focus: allow safe growth when `skip != 0` and preserve active bytes for in-place TLS operations.
- Modify `Zi::IOBuf::ensure()` to:
  - allow non-zero skip,
  - preserve data in `[skip, skip + length)` when reallocating,
  - leave `skip` and `length` intact after growth,
  - retain alignment guarantees from ZiIOBuf’s allocator.
- Ensure the hook adjusts skip/length to cover the union of:
  - the output prefix (`buf->off` bytes from `buf->base`)
  - the in-place plaintext region (`data()`..`end()`)
- Document invariants expected by hooks (e.g., `buf->base == data() - skip` for in-place buffers).

Complexity/feasibility: medium. Correct region preservation is crucial; careful reasoning is needed for union coverage and base/skip consistency.

### Phase 3: Ztls Zero-Copy Path Updates
- Focus: origin tagging, capacity usage, and removal of HandshakeBufSize.
- Remove `HandshakeBufSize` and the pre-ensure logic in `handshake_send_`.
- Update ptls_buffer_init call sites to use `zbuf->size` as capacity and set `buf.origin = zbuf.ptr()` for:
  - handshake send buffers
  - alert send buffers
  - record send buffers
- Receive-side `ptls_buffer_init` should use `buf->size` to expose full capacity (origin remains NULL unless we decide to extend it).
- Update `flush_sendbuf_` and `send_record_`:
  - treat `sendbuf.base` changes as zero-copy if `sendbuf.origin` matches the owning ZiIOBuf and buffer invariants hold
  - only copy + `ptls_buffer_dispose` when hooks fall back to internal allocation or origin is NULL
- Ensure in-place send path guarantees capacity before calling `ptls_send`:
  - for buffers built via `alloc_txbuf`, keep existing behavior
  - for externally supplied buffers, check tailroom and call ensure or fall back to copy/close
- Alignment policy:
  - if `align_bits <= log2(ZiIOBuf_Align)`, use ZiIOBuf growth
  - if higher, fall back to internal allocation + copy and log a warning

Complexity/feasibility: medium. This changes correctness-sensitive TLS send/receive behavior; requires careful validation.

### Phase 4: Diagnostics & Tests
- Update warnings:
  - only warn on actual copy fallback or hook growth failure
  - avoid warnings for in-place growth with origin-owned buffers
- Add/extend tests:
  - new `ztls/test/ZtlsBufHookTest.cc` to force buffer growth in handshake and record send paths (small IOBuf sizes), asserting:
    - origin-preserving growth stays zero-copy
    - fallback occurs only on forced alignment mismatch or deliberate failure
  - extend `ztls/test/ZtlsClient.cc` / `ZtlsServer.cc` or add a dedicated client/server test to exercise `ptls_receive` growth and verify no unwanted copies
  - optional: extend `zi/test/ZiIOBufTest.cc` to cover `ensure()` with non-zero skip and preservation of the active region
- Update `ztls/test/Makefile.am` to include new test binary and document entry point.

## Code References to Impacted Code
- `ztls/src/Ztls.hh:36` - HandshakeBufSize constant to remove.
- `ztls/src/Ztls.hh:340` - receive-side `ptls_buffer_init` capacity currently uses `buf->length`.
- `ztls/src/Ztls.hh:468` - `handshake_send_` allocates and enforces HandshakeBufSize.
- `ztls/src/Ztls.hh:481` - `send_alert_` ptls_buffer_init without origin tagging.
- `ztls/src/Ztls.hh:492` - `flush_sendbuf_` treats base changes as copy fallback.
- `ztls/src/Ztls.hh:532` - `send_record_` capacity and zero-copy logic.
- `ztls/src/ZtlsOpenSSL.cc:289` - `Backend::init()` initialization hook location.
- `ztls/src/ZtlsLib.cc:17` - `lib_init()` entry point.
- `zi/src/ZiIOBuf.hh:192` - `ensure()` implementation to extend for non-zero skip.
- `zi/src/ZiIOBuf.hh:47` - `ZiIOBuf_Align` for alignment policy.
- `ztls/test/Makefile.am` - add new buffer hook test binary.

## Detailed Test Plan
- `ZtlsBufHookTest` (new binary)
  - Configure minimal IOBuf size (e.g., template parameters) to force growth.
  - Handshake growth: initiate client/server handshake and assert that send buffer stays within same ZiIOBuf when origin set.
  - Record send growth: send payload > default size and assert no copy fallback unless align_bits is forced out-of-range.
  - Alignment fallback: simulate align_bits > 6 and assert internal allocation + copy path used with warning.
- `ZtlsClient`/`ZtlsServer` (extend or new test)
  - Send/receive large TLS records to ensure `ptls_receive` can grow output buffer without spurious copy warnings.
- `ZiIOBufTest` (optional extension)
  - Set `skip` and `length`, call `ensure(new_capacity)`, verify preserved region and correct skip/length post-grow.
- Document new test binaries in `ztls/test/Makefile.am` and module test README/notes if present.

## Options and Open Questions
- Hook placement: keep hooks in `ZtlsOpenSSL.cc` (minimal diff) vs new `ZtlsPicotlsBuf.cc` module (clear separation).
- Receive-side origin tagging: keep origin NULL (minimal change) vs tag to allow in-place growth for recv buffers.
- Exact preserve-region semantics: confirm how to compute the union of written prefix (`buf->off`) and plaintext region when ptls_send grows mid-flight.
- Failure policy: should alignment or ensure failures trigger disconnect vs copy fallback with warning?
- Verify local picotls default `ptls_buffer_alloc/free` behavior in `../picotls/lib` to avoid subtle contract violations.
