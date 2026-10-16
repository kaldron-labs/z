# Allocation failure audit and repair plan

Date: 2026-10-08

## Scope and contract

Audit of the current working tree, including untracked source files, for caller
checks that treat allocation or growth through `ZmHeap`/`ZmVHeap` as fallible.
`ZuAlloca` callers are out of scope: stack allocation cannot benefit from
`ZmHeapFail`. They should check for null and act accordingly; retain those
checks when repairing adjacent Z heap allocation checks.
The findings below record the original source audit. Locations in those findings
refer to the working tree at audit time. Repairs are now implemented; see the
execution record below. No rebuild or test execution was performed, as instructed.

For a nonzero allocation, `ZmHeap` and `ZmVHeap` return storage or invoke
`ZmHeapFail`, which retries when the registered handler returns true and aborts
otherwise. Callers must depend on that policy. They must not convert out of
memory into a null result, stream failure, protocol rejection, or local error.

Evidence:

- `zm/src/ZmHeap.hh:187-196`: cache miss falls back to aligned allocation, then
  calls `ZmHeapFail` and retries; there is no null return on failure.
- `zm/src/ZmVHeap.hh:81-100`: pooled allocations use `ZmHeap`; jumbo allocations
  retry through `ZmHeapFail`.
- `zm/src/ZmHeap.cc:386-389`: the registered failure handler authorizes retry,
  otherwise the process aborts.
- `zt/src/ZtArray.hh:881-884,1136-1157`: nonzero `ensure`/`size` allocations use
  `valloc` without a nullable failure contract.

Zero-size allocation, absent objects, failed I/O, protocol limits, integer
overflow, validation errors, and crypto backend errors require separate
reasoning. In particular, `ZmVHeap::valloc(0)` intentionally returns null.

## Confirmed findings

### 1. ZiIOBuf reintroduces nullable allocation failure

File: `zi/src/ZiIOBuf.hh`.

| Location | Finding | Repair |
| --- | --- | --- |
| 113-121, `alloc` | Tests `valloc(newSize)` and returns null after setting size to zero. Growth implies a positive request, so the failure branch is unreachable. | Assign the allocation result directly; remove the null branch. |
| 183-194, `realloc` | Tests the allocated pointer, clears size/length/skip on purported failure, and returns a nullable pointer. | Preserve/copy data directly after allocation; remove allocation-failure state changes and the nullable return expression. |
| 209-210, `ensure` | Tests `valloc` and sets `failed_` on purported allocation failure. | Remove this branch; retain the independently justified overflow, growth-policy, and existing-status checks. |
| 237-238, `prepend` | Tests `valloc` and sets `failed_` on purported allocation failure. | Remove this branch; review size arithmetic separately before changing caller contracts. |

These checks are the central source of the false allocation-failure contract
propagated into dependent modules.

### 2. TLS and QUIC scratch/context callers check infallible allocations

| File | Lines | Finding |
| --- | --- | --- |
| `ztls/src/ZtlsHPKE.cc` | 53, 75 | Tests scratch `.data()` after nonzero `ZmScratch(..., ScratchHeap)` allocation. |
| `ztls/src/ZtlsAge.cc` | 778-779, 873-874, 1030, 1117-1118, 1121-1122 | Returns allocation errors or false for scratch buffers backed by `AgeScratchHeap` or `AgeHeaderScratch`'s heap. These requests are positive. |
| `ztls/src/ZtlsVault.cc` | 603-604 | Returns a key-allocation exception after `ZtScratch(VaultScratch, ...)`. The key capacity is positive. |
| `ztls/src/ZtlsPico.cc` | 199, 251 | Returns false after nonzero `CtxHeap::valloc` in `AeadCtx::init` and `CipherCtx::init`. |
| `zquic/src/ZquicCrypto.cc` | 799 | Tests `plain` after scratch allocation and `plain.length(cipherLen)`. The `cipherLen` guard already excludes zero-size null storage. |

Remove these allocation checks. Keep algorithm/key validation, authentication,
backend setup errors, and input-size checks. Crypto initialization may remain
fallible for those reasons.

### 3. ZtArray growth is checked as nullable in ALPN and compression

| File | Lines | Finding |
| --- | --- | --- |
| `ztls/src/Ztls.hh` | 2206 | `bytes && !m_alpnData.ensure(bytes)` returns false for a positive `ZtArray` allocation. |
| `zquic/src/Zquic.hh` | 1071 | Same ALPN growth check. |
| `zhttp/src/ZhttpCompression.hh` | 102, 118, 172 | Tests results of `Bytes::ensure`; the Z array/scratch implementations used by HPACK/QPACK rely on `ZmVHeap`. |

Call `ensure` directly for positive capacities. Preserve zero-length handling
and genuine size/encoding failures. The compression helpers are templates:
review all supported `Bytes` instantiations before removing generic status
machinery. `PrintBytes::ok()` and `m_ok` need a dependent-use audit after removing
these branches, accounting for remaining behavior outside this audit's scope.

### 4. File transmit stream checks a freshly allocated heap object

`zi/src/ZiFileTxStream.hh:70`: `ensureBuf()` tests `m_buf` after `allocBuf_()`.
The callback is private and always returns `new Buf`; `Buf` inherits its concrete
`ZmHeap` specialization at lines 45-48. This null check and its failure return
are redundant. Simplify the helper and its callers; retain write/flush failure
handling and oversized-printable status.

### 5. Dependent modules treat ZiIOBuf::alloc as fallible

The following callers branch on `alloc`, whose positive growth cannot fail
locally. Remove the allocation-result checks and their exclusive error paths.
Keep independent bounds checks and nullable callback results where the callback
can reject work for another reason.

| File | Lines | Current consequence |
| --- | --- | --- |
| `zi/src/ZiRxStream.hh` | 304, 310, 324 | Returns `-1` after buffer allocation/growth. The callback null checks are a separate contract question. |
| `zjrpc/src/ZjrpcIO.hh` | 112 | Clears the input buffer and rejects message assembly. |
| `ztc/src/ZtcApp.cc` | 436 | Converts allocation failure into `LoadResult::IOError`. |
| `ztc/src/ZtcApp.cc` | 1537 | Combines the valid maximum-frame check with an allocation check and reports invalid-message handling. |
| `ztc/src/ztcagent_daemon.cc` | 689 | Conditionally routes a telemetry frame; the failure branch only releases telemetry admission. |
| `ztc/src/ztcagent_daemon.cc` | 1169-1172 | Closes the link with `TooLarge` for purported allocation failure. |

Additional fixture/interop instances:

- `zhttp/test/ZhttpH2MessageTest.cc:42`.
- `ztc/test/ZtcTestClient.hh:115`.
- `zmcp/itest/zmcphttptest.cc:386`.
- `zi/interop/iobaseline.hh:71`.

For the interop allocator, retain or add explicit size/conversion validation if
its `size_t` input cannot be represented by the unsigned I/O buffer API; that is
independent of allocation success.

### 6. Header construction checks prepend's purported allocation failure

`zdb/src/ZdbMsg.hh:135`: tests the pointer returned by
`buf->prepend(sizeof(Hdr))` and returns an empty buffer. The current implementation
returns null on this growth path only through its redundant `valloc` null check.
Remove that caller branch once `prepend` is repaired. Keep the preceding builder
result check: a builder may fail validation or capacity checks.

### 7. Hex dump copies silently discard data on purported allocation failure

`zt/src/ZtHexDump.hh:122-128`: `init_()` branches on `valloc(m_length)` and clears
the dump on failure. For positive lengths the failure branch is unreachable;
for zero length it handles the intentional null allocation result. Make the
zero-length case explicit, then allocate/copy positive lengths directly.

## Related defects and contract gaps

### 8. Empty cache iteration can retain a lock indefinitely

`zm/src/ZmCache.hh:418-420` and `zm/src/ZmPolyCache.hh:430-432` allocate scratch
for the cache count, then return false when `.data()` is null. Both helpers are
entered with `m_lock` held, and their unlock occurs later, at lines 431 and 443
respectively. A zero-entry cache produces a null scratch pointer by design, so
this path returns without unlocking. Subsequent access can deadlock.

Priority: repair this before the mechanical allocation-check cleanup. Empty
iteration should follow the normal unlock/completion path. Review all helper
callers when removing their boolean success result.

These particular scratch calls use the two-argument macro, so their heap
fallback currently has the separate failure-policy gap below. Do not simply
remove the nonzero OOM handling without repairing that gap or selecting an
explicit Z heap for these calls.

### 9. Default scratch fallback bypasses ZmHeapFail

`zm/src/ZmScratch.hh:575-580` and `zm/src/ZmAlloc.hh:61-65` call
`Zm::alignedAlloc` directly in their default heap fallback. That primitive
returns null on failure (`zm/src/ZmPlatform.hh:147-162`); it does not invoke
`ZmHeapFail`. The overloads with an explicit VHeap use its allocation policy.

Consequently, the blanket scratch guideline is not yet true for default scratch
allocation. Repair the fallback policy centrally, while preserving alignment
and matching deallocation, or move dependents to appropriate identified Z heaps.

Besides the cache helpers, explicit checks of this default fallback occur at
`zi/src/ZiPlatform.cc:25` and `zm/bench/zmheapbench.cc:109-112`. They are not dead
OOM checks under today's implementation; remove them after the allocation
policy is fixed. Keep any intentional zero-size behavior.

### 10. zpicotls buffer hook has build-dependent allocation behavior

`ztls/src/ZtlsPico.cc:125-129` tests `internal_alloc_` and increments
`internal_alloc_fail`. In a normal build the helper uses `Zi::VHeap::valloc`, so
the nonzero OOM check is redundant. Under ASAN, lines 70-73 use raw aligned
allocation and can return null, including on a size bound.

Unify the OOM policy for the ASAN path with `ZmHeapFail` while preserving its raw
allocation/free pairing. Keep size-limit rejection explicit. Then remove the
synthetic OOM branch/counter as appropriate, propagating API changes to stats
and their dependents rather than keeping compatibility shims. Handle an
intentional zero-capacity request separately if the hook contract permits it.

## Checks requiring semantic review, not mechanical deletion

- `zi/src/ZiIOBuf.hh:198-206`: `ensure` rejects existing failure, overflow, and
  a growth policy returning insufficient capacity. These are real conditions
  independent of OOM. Checks of this API can therefore be valid.
- `zi/src/ZiTxStream.hh:114-121,183-184`: allocation callbacks can reject work
  because the underlying stream failed, and `ensure` can report real buffer
  status/limits. `TxLayer::allocBuf_` explicitly returns null when the lower
  layer failed. Preserve that behavior rather than treating every null callback
  result as a heap failure.
- `zf/src/ZfFB.hh:647-648`: the `Arena` growth-result check is redundant for
  `ArrayStorage`, but `ZiFB::IOStorage::grow` (`zi/src/ZiFB.hh:91-95`) can return
  null for a detached owner or size overflow. Retain real capacity/state errors;
  clarify the storage contract before specializing or eliminating this check.
- `ztls/src/Ztls.hh:696,860,1036`: these calls grow freshly allocated buffers,
  with zero skip and clear status, to a fixed record capacity. After removing
  the allocator checks, assess these as candidates to simplify directly.
  At lines 774 and 992, verify existing buffer status and geometry first.
- `zdbus/src/ZdbusConnection.cc:327,367,418-420` and
  `zdbus/src/ZdbusMessage.hh:110`: fresh bounded buffers cannot run out of memory
  locally, but existing buffers can carry failed status. Prove the bounds and
  status at each site. In particular, do not label a size/status rejection
  `ENOMEM`; replace that diagnosis with its actual cause.
- `zi/src/ZiRx.hh:87,171,244,306`, `zi/src/ZiRxStream.hh:398,402`,
  `zjrpc/src/ZjrpcIO.hh:192-196`, and `ztls/src/ZtlsPico.cc:111`: retain checks
  that enforce overflow, rejected callback, or buffer-status contracts. Remove
  OOM interpretations once the buffer API is clarified.
- Database insert/update callback null results report operation rejection, not
  `new ZdbRow` failure. The nearby `new` statements do not make those callback
  checks allocation checks. Likewise, crypto backend allocation/setup results,
  lookups, optional owners, and zero-sized storage were excluded from confirmed
  redundant OOM checks.

## Repair sequence

1. Fix the cache lock leak for zero-entry iteration. Establish the default
   scratch fallback's `ZmHeapFail` policy before removing its OOM checks.
2. Remove the four direct `valloc` failure branches in `ZiIOBuf`. Document the
   remaining non-OOM reasons for rejection and preserve existing stream status.
3. Remove the confirmed TLS/QUIC scratch, context, ALPN, compression, file-stream,
   header, and hex-dump checks. Preserve explicit empty-buffer behavior.
4. Propagate `ZiIOBuf::alloc`'s contract through the listed production callers
   and fixtures. Remove dead failure plumbing and misleading error messages.
   Propagate any helper return-type changes through all dependents.
5. Reconcile the zpicotls ASAN path and stats with the common failure policy.
6. Review the mixed status/capacity APIs individually. If changing them, make
   validation/status handling explicit before removing pointer checks; do not
   add wrappers that hide the same redundant check.
7. Repeat the source audit for direct allocation, `new`, `ensure`, `realloc`,
   `prepend`, scratch truthiness, and allocation-error diagnostics. Review the
   remaining matches against zero size, ownership, bounds, and I/O state.

## Validation for the repair

- Add a focused regression for empty cache iteration followed by another cache
  operation, including synchronous and asynchronous iteration variants.
- Exercise the `ZmHeapFail` retry policy at the allocator boundary; avoid fake
  caller-side OOM injection for nonzero Z heap allocations.
- Check zero-size scratch/heap behavior, buffer preservation across growth,
  overflow/capacity rejection, and sticky stream failure after genuine I/O errors.
- Build affected modules and their dependents from the top level; run relevant
  existing `ZmCache`/`ZmPolyCache`, heap/scratch, `ZiIOBuf`, Tx/Rx stream, TLS,
  QUIC, HPACK/QPACK, RPC, telemetry, and FlatBuffers tests. Exercise both normal
  and ASAN zpicotls allocation paths.
- Review tests that inject null allocation callbacks (`zi/test/ZiTxStreamTest.cc`,
  `zi/test/ZiRxStreamTest.cc`, `zws/test/ZwsProtocolTest.cc`, and
  `zhttp/test/ZhttpTransportContractTest.cc`). Retain cases modeling legitimate
  admission/status rejection; replace cases claiming to simulate Z heap OOM.

The audit used repository-wide source searches plus manual tracing of allocator
and wrapper implementations. It is not a compiler-based proof that every
indirect allocation caller has been identified; template/callback contracts
listed for review need verification during implementation.

## Execution record

Implemented on 2026-10-08. The instruction not to rebuild or retest supersedes
the build/run commands in the validation section above. Regression source was
added, but neither new nor existing tests were compiled or executed.

| Finding / step | Disposition |
| --- | --- |
| 1. ZiIOBuf | Removed all four `valloc` result branches and the associated synthetic failure state changes. `ensure` documents its remaining status, overflow, and growth-policy rejection contract. |
| 2. TLS/QUIC scratch and contexts | Removed every listed scratch/context allocation-result check; crypto validation and setup failures remain. |
| 3. ALPN/compression | ALPN initialization now returns void, with both dependent call sites updated. Compression no longer checks Z array growth results. Its `m_ok`/`ok()` machinery remains necessary for the direct `ZuAlloca` path, whose null check is unchanged. |
| 4. File Tx stream | `ensureBuf` now returns void; its append/rollover callers no longer handle synthetic allocation failure. File write/flush failures still set stream status. |
| 5. Buffer callers | Removed the listed `alloc` result checks in Rx extraction, JSON RPC, telemetry, fixtures, and interop. Callback admission rejection and maximum-frame limits remain explicit. The legacy FlatBuffers interop allocator now checks size representation before allocation. |
| 6. Header construction | Removed the `prepend` null branch in Zdb header construction and the additional matching interop caller. Builder capacity/validation failure checks remain. Header sizes are bounded by the builder wire capacity; no new arithmetic rejection was needed for these calls. |
| 7. Hex dump | Empty input is handled explicitly; positive allocations proceed directly to the copy. |
| 8. Cache lock leak | Both internal iteration helpers return void and always reach the unlock for empty iteration. Added empty synchronous/asynchronous iteration regressions, including deleting ZmCache variants, and documented them in `zm/test/README.md`. |
| 9. Default scratch fallback | Added `ZmHeapAlloc<Align>` at the allocator boundary. The default ZmAlloc/ZmScratch heap fallbacks use its retry/abort policy and retain their original `alignedFree` pairing and zero-size semantics. Removed the now-redundant platform/benchmark checks, preserving the platform zero-size case explicitly. |
| 10. zpicotls / ASAN | ASAN raw allocation uses `ZmHeapAlloc` and raw aligned free; ordinary builds retain Zi::VHeap. Internal zero/oversized requests are rejected before allocation in both builds. Removed the synthetic OOM counter from the implementation and public stats struct; no references remain. |
| Mixed storage policies | Added `canGrow` to ArrayStorage and IOStorage. Arena checks owner/capacity admission before calling the allocation-backed `grow`, eliminating its nullable allocation-result check. Detached-owner and size-limit rejection remain supported. |
| Mixed TLS buffers | Removed growth-result checks for freshly allocated fixed-record buffers and for Tx buffers whose status and record geometry are already validated. Retained the Rx buffer status/capacity check and corrected its diagnostic. KeyUpdate buffer rejection is described as geometry rejection. |
| D-Bus | Removed fresh bounded-buffer allocation checks and their synthetic allocation-rejection/ENOMEM paths. Existing Rx buffer status/capacity rejection remains `CxnError::Resource`, without an invented ENOMEM system error. |
| Callback tests | Renamed/documented the injected Tx/Rx/WebSocket/HTTP callback rejections as admission rejection. They occur before allocation and continue to cover a real fallible callback contract. |
| Follow-up audit | Repeated repository-wide source searches and reviewed allocation/growth assignments followed by null branches. Removed additional `prepend` failure branches in ZiIOBufTest and Zdb interop, and the synthetic allocation-failure assertion in the MCP stdio fixture. |

Retained checks were reviewed individually:

- `ZiIOBuf::ensure` and its append/print dependents propagate sticky output
  status, integer overflow, and a growth policy returning insufficient capacity.
- `ZiTxStream` retains lower-layer/admission rejection and send failure; its
  callback contract and flush comment now describe those conditions explicitly.
- `ZiRxStream` extraction retains nullable buffer-request callbacks; splice
  additionally retains `ensure` status/capacity checks. Its callback contract
  documents that null does not represent Z heap OOM.
- `ZiRx`, JSON RPC buffered output, D-Bus Rx, TLS Rx, and the zpicotls origin
  hook retain checks of existing buffer status/capacity. Their underlying
  allocator no longer supplies a nullable OOM result.
- FlatBuffers arena `ensure`/`push` still report wire-size limits, detached
  owners, and prior builder errors. Allocation itself is unconditional after
  storage admission.
- Direct `ZuAlloca` null checks, zero-sized storage, optional owners, lookups,
  database operation rejection, external allocator callbacks, and crypto
  backend errors remain outside the redundant Z heap failure cleanup.

Validation performed: reviewed the edits against a snapshot taken before the
repairs, traced updated helper/return-type/stat dependents, and checked source
whitespace. Runtime behavior, compiler acceptance, allocator retry under actual
OOM, and the added cache regressions remain untested because rebuild/retest was
explicitly prohibited.
