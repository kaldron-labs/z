> Column-contract correction: `repair2.md` is authoritative. Zdb stores map one
> field to one supported scalar/vector column, retaining explicit Bitmap/IP
> representations. Recursive codec UDT support does not authorize composite UDT
> columns or arrays of composites. Any contrary requirement below is withdrawn.

# Repair plan: Zdb copying and type coverage

Repair the current `Zdb` implementation and its memory, sqlite3 and postgres
stores for two problems:

1. Inefficiency from unnecessary copying and intermediate owning storage.
2. Type-coverage gaps in consumers of the existing field metadata.

This scope correction supersedes requirements in the consolidated `repair2.md`
that call for additional validation, checked parameter sizing, new setup/rebind
rejection, changed diagnostics or changes to error-completion behavior. Existing
validation, error handling, lifecycle, resource limits and failure policies must
remain unchanged. This document records requirements, not completed work.

## Existing contracts

- UDTs have always decomposed recursively through nested `ZfStruct` field
  metadata across formats: `ZfJSON`, `ZfURI`, `ZfASN1`, `ZfTOML`, `ZfYAML`,
  `ZfCLI`, `ZfCf` and `ZfFB` (now `ZiFB`). That codec contract does not extend
  store columns to composite UDTs or arrays of composites. Preserve one field
  per supported scalar/vector column and explicit Bitmap/IP representations.
- Preserve HEAD's storage representations, numeric conversions, defaults,
  key/index behavior, operation filters, callbacks and shard ownership, except
  for the explicit copying and type-coverage repairs.
- Preserve existing external-input and saved-definition verification at their
  current boundaries. Adding verification or rejecting previously accepted
  inputs is outside this repair.
- Apply `GUIDELINES.md`, preserving Z containers, tunable heaps, pooled I/O
  buffers, asynchronous lifetimes and hard-tab style.

## 1. Eliminate unnecessary copying

Trace construction, storage and ownership through insert, update, delete,
find/select, recovery, replication and saga paths. Identify each source and final
owner before changing data movement.

### Direct transmit-buffer construction

Follow the current `ZiFB::save(S &, ...)` implementation in `zi/src/ZiFB.hh`
and the buffer-owning insertion in `zi/src/ZiTxStream.hh`:

```text
object → backward construction directly in ZiIOBuf → operator << owner handoff
```

- Preserve `ZiIsTxStream` detection through callers and wrappers so transmit
  output uses its existing direct-buffer path.
- Transfer completed buffers through `operator <<(ZmRef<ZiIOBuf>)`. Avoid
  converting them into spans and copying their bytes into another output buffer.
- `ZiIOBuf` grows. Exceeding its initial allocation does not require a separate
  destination copy. Preserve growth, alignment, head/tail reservations and
  end-relative builder offsets.
- Account for growth relocation, existing detach compaction and existing stream
  splitting separately from an avoidable copy between owning buffers. Preserve
  current stream size and splitting policies; add no new limits or checks.
- Preserve prefix ordering, layer preparation, flush behavior, failure state and
  asynchronous buffer ownership.

### Stored values and retained payloads

- Construct decoded owning values directly in their final Tuple or result slot.
  Remove temporary bitmap/vector/string ownership and deep copies at proven
  last-use sites; retain ownership needed by independent index entries.
- Move temporary index keys into retaining nodes using existing move-capable
  container APIs. Preserve snapshots required across mutable updates.
- Current `MSaga::save` uses `ZiFB::save` with retained `SagaPayload` storage.
  This generic destination uses backward scratch construction and one
  completed-span insertion. Preserve its builtin/heap tuning and replace
  semantics; remove any additional relay storage or copies.
- Audit cold find/select and saga recovery for temporary-to-temporary copies.
  Use direct destination construction, moves or existing buffer-owner handoffs.
  Retain copies required by actual representation or ownership boundaries and
  document those boundaries.
- Use actual metadata-derived alignment with the current builder/storage APIs.
  Preserve forced alignment, pooled-buffer tuning, growth and buffer reuse.

Acceptance must identify allocations, copied bytes and ownership transfers on
actual paths. Buffer identity demonstrates owner handoff; allocation counts alone
cannot prove that no byte copy occurred. Layout and source inspection do not
establish a latency improvement.

## 2. Close type-coverage gaps

Trace existing metadata through both native → wire → store and
store → wire → native paths for all three data stores.

- Support `CStringVec` alongside `StringVec` using the existing equivalent
  representation and destination ownership.
- Handle Float32 scalar/vector wire widths independently of the store's existing
  normalized representation. Preserve HEAD's sqlite3 and postgres numeric
  conversion and storage semantics, including nonfinite values and signed zero.
- Preserve one field per supported scalar/vector column and explicit Bitmap/IP
  representations. Composite UDTs and arrays of composites are codec values,
  not supported store columns. Add no composite storage, field flattening or
  opaque Bytes substitutes.
- Cover primitive widths, strings/bytes and their vectors, mapped inline values,
  defaults, constructor fields, Init/Skip/Required membership and key projections.
- Exercise supported representations through insert, mutable update,
  primary/secondary-key lookup, select, delete, reopen/recovery and sagas.

Implement the missing conversions within existing paths. Do not substitute new
setup rejection, runtime capability probing or per-row validation for coverage.
Keep existing error handling unchanged while propagating the repaired types.

## Build and acceptance

- Batch source changes into large batches. Rebuild and retest only at major
  milestones, incrementally reusing current artifacts. Build changed libraries
  before dependent binaries; use `-j4` for aggregate builds.
- Retain the current compiler, build flags and platform. Other configurations,
  sanitizer configurations and clean builds are out of scope. If regeneration
  is needed, use `z.config` with the existing settings.
- Reuse existing `zi`, `zdb`, sqlite3 and postgres fixtures and canonical interop
  entry points. Add focused coverage for repaired types and ownership/copy
  invariants, without adding production validation or instrumentation.
- Preserve existing validation and error-path behavior in regression checks.
  New validation, diagnostic or error-policy work is outside acceptance scope.
- Use matched workloads, schemas, defaults and pool settings for performance
  comparison. Report codec, store and saga allocations, payload sizes and latency
  distributions separately. Use the approved matched HEAD baselines; introduce
  no new absolute saga target.
- Use Valgrind sparingly for memory-corruption debugging as needed and as the
  final leak/corruption gate. Use `libtool exec` for source-tree debugging tools.
- Record exact source/configuration scope, commands, results and remaining gaps.
  Skips and planned checks do not establish acceptance.

## Scope of pending edits

The recently introduced postgres validation, checked-sizing, diagnostic and
error-completion edits are outside this corrected scope. When implementation
resumes, remove those additions while preserving unrelated work and existing
behavior. Retain only changes justified by copying inefficiency or missing type
coverage. This document update does not itself change implementation files.
