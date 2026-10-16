# Configuration-driven single-partition heap optimization

## Objective and scope

Optimize a configuration group `{ID, vshift}` with only one configured
partition. An own-arena ownership miss should proceed directly to system free,
without an ownership lookup, lookup lock, or additional hot-path flag.

Preserve the current preparation/publication ordering, null-bound ownership
check and interpolated lookup coverage. Keep arena registration unconditional
for non-sharded arenas; defer only the free-path lookup pointer for singleton
groups. Adjust cleanup so it does not depend on that pointer. Do not introduce
the initialization API from `heaptune2.md`.

The public initialization signature and CSV column order remain unchanged.
Different `vshift` rows for the same ID and partition remain valid: they
configure different groups. A duplicate is a repeated nonzero configuration
for the same full `{ID, vshift, partition}` key.

## Configuration key and dependent code

`ZmHeap.cc` already declares:

```cpp
using CfKey = ZuTuple<ZuID, uint8_t, uint16_t>; // id, vshift, partition
```

Complete the dependent changes as part of implementation:

- Construct every full configuration key as `ZuFwdTuple(id, vshift, partition)`.
- Update the configuration operations in private `init()`. Remove the old
  unconditional delete-and-replace path; detect duplicates before insertion.
- Update the configuration lookup in `cache()` to the new tuple order.
- Correct the `Configs` key comment and any tests constructing configuration
  keys or examining their tuple fields.
- Keep public `init(id, partition, vshift, config)` and cache-constructor
  argument order unchanged. Reordering the internal key does not reorder
  those function arguments or CSV fields.

Test unequal partition and shift values so swapping them cannot go unnoticed.
The CSV loader must continue accepting several shifts within one partition.

Configuration grouping does not change cache identity. Keep `ZmHeapCache::Key`
and `KeyAxor` unchanged: ID, partition, size, alignment and sharding. Do not add
`vshift` to that key or create separate caches solely to distinguish configuration
groups. Statistics remain aggregated by ID, and ownership indexes remain keyed
by `{ID, size}`.

## Derive the partition count

Do not store `partCount` in `ID2Stats`, `ZmHeapConfig`, caches or another map.
Leave the statistics records and their lifetimes unchanged.

Add a private manager helper, for example `partCount_(id, vshift)`, which
requires the existing manager lock and derives a local `uint16_t` count:

```cpp
uint16_t count = 0;
auto i = m_configs.citer<ZmRBTreeGreaterEqual>(CfKey{id, vshift});
while (auto node = i()) {
  const auto &key = node->key();
  if (key.p<0>() != id || key.p<1>() != vshift) break;
  ++count;
}
return count;
```

The partially initialized `CfKey{id, vshift}` defaults partition to zero,
the first possible partition. Use this full key as the lower bound: the tree
and tuple comparators do not accept a shorter two-element tuple directly.
The reordered tuple puts each group's rows together. Within that prefix, full
keys are unique and differ only in partition, so the number of rows is the
number of configured partitions. Stop at the first nonmatching prefix so the
count cannot include the next shift or ID. Use `ZmRBTreeGreaterEqual`, not
`ZmRBTreeEqual`: the latter follows duplicate full keys and will not traverse
the group's distinct partition keys in this unique tree.
No deduplication set or persistent counter is needed. Partition counts are only a handful; `uint16_t` is sufficient.

Derive the count during configuration changes and new-cache construction,
never during allocation or free. Pass an already-derived count to private
helpers when it applies to the same group rather than recounting it.

The policy is:

- Register every newly prepared non-sharded arena, irrespective of the count.
- Count zero or one: leave the group's free-path `m_lookup` pointers null.
- Count two or more: attach lookup pointers to matching caches and receivers.

Another ID or another `vshift` group having several partitions does not enable
lookup for this group. Thread count, observed cache count and successful arena
allocation count do not determine eligibility.

## Zero-capacity calls and duplicates

At the start of public `ZmHeapMgr::init()`, before obtaining the singleton:

```cpp
if (!config.cacheSize) return;
```

Zero capacity is the default. Such a call creates no manager, configuration
row or statistics record, changes no lookup association, and does not disable
an existing arena. This early return precedes duplicate detection.

For nonzero requests, check the full `{ID, vshift, partition}` key under the
manager lock. An existing stored configuration with nonzero `cacheSize` is a
duplicate. Reject it before any mutation using the existing project convention:

```cpp
ZmAssert(!duplicate, return);
```

This diagnoses the application error by aborting in debug builds and returns
without changing state in release builds. Keep the public return type `void`.
Do not silently overwrite the configuration or prepare another arena.

Use the stored configuration, not a cache's mutable capacity, for this check.
An arena allocation failure can leave a cache with zero capacity while its
stored configuration remains nonzero. That does not permit duplicate
initialization or remove the partition from the derived count.

Only nonzero accepted requests are inserted, so the configuration tree contains
no zero-capacity rows. Zero-capacity caches created before configuration do not
count; their first accepted nonzero configuration adds one row.

## Nonzero configuration sequence

Keep the manager lock throughout the following sequence:

1. Find the full key and reject a duplicate.
2. Derive the group's existing partition count. End the counting iterator's
   scope before mutating `Configs`.
3. Insert the new nonzero configuration using the reordered key.
4. If the previous count was one, attach the existing lookup to the previously
   singleton cache as described below. Use the existing ID-cache traversal to
   associate matching receivers as well, including a receiving cache whose
   arena allocation may fail.
5. Apply the new row to caches matching ID, `vshift` and partition. Register
   every newly prepared non-sharded arena exactly once, attach lookup pointers
   when the post-insertion count exceeds one, then publish the new head.

The count gates pointer attachment, never arena registration. The first arena
is already indexed when the second partition is configured. No existing arena
needs backfilling or re-registration. Duplicate and zero-capacity calls do not
reach this sequence.

Configuration defines the partitions permitted to receive that group's arena
allocations. Configuration of a receiving partition must finish before the
application starts those transfers. Freeing an arena block through an
unconfigured partition is outside this optimization's contract; cache creation
alone does not enable lookup.

## Single-partition path and cache construction

Separate registration from pointer attachment in the manager's existing
`lookupAdd()` path:

- Keep the sharded-cache exclusion.
- Find or create the `{ID, size}` ownership index and register each newly
  prepared non-sharded arena using the existing interpolation logic.
- Gate only pointer attachment on the derived `{ID, vshift}` count. For a
  singleton, keep `m_lookup` null even though its arena is indexed.
- Publish the head after registration and any required receiver association.

`ZmHeapLookup::add()` currently assigns `c->lookup(this)`. Move that assignment
out of the index insertion routine and into the manager's association path so
registration alone does not enable lookup on singleton fallback frees.

When `cache()` constructs a cache, use the reordered configuration key and
derive its group's count under the existing manager lock. Register any prepared
arena regardless of the count. Attach an existing lookup to a zero-capacity
cache only when its group is multi-partition. Preserve the early return for an
already-resolved cache; configuration updates adjust its pointer in place.

Leave `free()` unchanged:

```text
own-arena hit -> return block to own arena
own-arena miss, null lookup -> system free
```

There is no new global flag, per-free partition test or allocation-path work.
Different cache sizes and alignments do not add configuration rows.

## Enabling lookup for the former singleton

When a successful configuration insertion changes the derived count from one
to two, the only additional work for the former singleton is to set its
`m_lookup` pointer to the existing `{ID, size}` lookup. Its arena is already
registered. Do not reinsert its range, rebuild the index for this transition,
republish its head or add registration state.

Use the existing `m_id2Cache` traversal to make this assignment for the group's
existing cache objects. If several block sizes are represented, each uses its
existing `{ID, size}` index. This is pointer association only, not a new
backfill mechanism. Keep other `vshift` groups' singleton pointers null.

Complete the former singleton's pointer assignment and matching receiver
association before publishing a newly prepared arena in the second partition.
The ordinary new-arena registration can still lower the index shift and invoke
the existing index rebuild; that is unrelated to the single-to-multiple
transition. Therefore, compare raw index entries only across the pointer
association step. Across new-arena registration, assert ownership coverage and
uniqueness of each `{bucket, arena}` pair at the resulting shift; do not require
the old bucket keys or entry count to remain unchanged.

If the old cache has no arena and no index exists, there is nothing to attach
yet. The first successfully prepared arena creates the index and associates
matching receivers before its head is published. If the new
arena allocation fails but an old index exists, the new zero-capacity receiver
must still receive its lookup pointer; association must not depend on a
successful private `init()` result.

Ownership indexes retain their `{ID, size}` key. Reuse an existing index even
if another group also uses it. Receiver association matches ID, normalized size
and the eligible group's `vshift`, and skips sharded caches. Do not make
association conditional on index creation: the index normally already exists
from the singleton's registration. Already-attached caches retain their
pointers. Third and subsequent partitions use normal registration/association.

Configurations may also be loaded before caches exist. Later construction
registers its new arena and, using the derived count, attaches the appropriate
lookup before returning the cache or publishing its head.

## Cleanup

A null free-path `m_lookup` no longer means that the arena is unregistered.
Singleton arenas must be removed from their ownership indexes before memory
is freed, just like multi-partition arenas.

Use the manager's existing cache teardown to unregister each managed,
non-sharded arena through the `{ID, size}` index before releasing the cache
reference with `ZmDEREF`. The index map is still alive at this point. Do not
obtain the manager singleton from a cache destructor while that manager is
being destroyed, and do not add a second per-cache pointer solely for cleanup.

Move managed range removal out of `ZmHeapCache::final_()`; that function frees
arena memory without inferring registration from `m_lookup`. Ensure each
range is removed once, preserving entries belonging to other caches. A cache
without an arena has no range to remove, whether or not it has a lookup pointer.
Sharded caches remain outside shared ownership indexes. Keep direct lookup
test fixtures responsible for explicitly removing their synthetic ranges.

There is no configuration removal or demotion in this change. Allocation
failure and thread exit do not remove configuration rows.

## Validation

Extend the separately instrumented `ZmHeapTest`. Use its private `Find` hook to
verify lookup avoidance directly. Retain interpolation and rebuild fixtures.

1. **Single partition:** exhaust a configured arena and free fallback blocks.
   Assert null lookup pointers and zero `Find` calls, with correct arena and
   system-free statistics. Independently verify that the arena is already
   present in the ownership index.
2. **Full-key order:** use unequal values such as partition 3 and `vshift` 1.
   Test both caches created before configuration and caches created afterward;
   both must receive the correct capacity and group eligibility.
3. **Prefix counts:** insert `{ID, 0, 0}` and `{ID, 1, 0}` in internal key order.
   Both groups have count one. Add `{ID, 1, 1}`; only shift 1 becomes eligible.
   Test reversed insertion order and independence from a different ID.
4. **Successive transitions:** transition shift 0 after shift 1 is already
   multi-partition. During pointer association, before new-arena registration,
   verify that only the affected singleton pointers change and existing index
   entries remain unchanged. After registration, allow a shift reduction and
   rebuild; verify complete coverage and exactly one entry per covered
   `{bucket, arena}` pair. Add a third partition and verify ordinary new-arena
   registration.
5. **Shared index key:** exercise different eligible groups using the same
   `{ID, size}` index. Preserve old-arena coverage, register new ranges once and
   attach the new group's peers even though the index already exists. A group with
   count one must retain null pointers despite that index's existence. Preserve
   the existing cache identity rules; do not require distinct caches solely
   because configuration rows have different `vshift` values.
6. **Duplicate behavior:** repeat an exact nonzero full key, including after
   an injected arena allocation failure. Use subprocess assertion checks in
   debug builds and unchanged-state checks in release builds. Different shifts
   for the same ID/partition must succeed and remain separate groups.
7. **Zero-capacity no-op:** call with zero capacity before any manager use, for
   an existing default cache, for an already configured key and for a potential
   second partition. Verify no singleton creation in the first case and no
   configuration, count, lookup, arena-bound or head changes in all cases.
8. **Live transition:** hold old arena blocks across the second configuration,
   with the old arena partially used and fully exhausted. Verify unchanged
   index entries during pointer association, before new-arena registration.
   Then pause after registration and before new head publication: verify
   complete old/new arena coverage, no duplicate `{bucket, arena}` entries,
   and attachment of the former singleton and receiver pointers. Include a
   smaller new arena that lowers the shift and legitimately rebuilds entries.
   Cross-free in both directions after publication; no held block may become
   available again.
9. **Multiple cache sizes:** register several sizes while the group has one
   configured partition. During pointer association, verify each existing cache
   receives its corresponding lookup pointer without reinserting any old range.
   Other singleton groups and sharded caches retain null pointers.
10. **Failure and deferred construction:** fail allocation for the second row.
    Its stored configuration still counts, old arenas remain registered, and
    the zero-capacity receiver can cross-free an old arena block. Also configure
    two nonzero rows before any caches exist and verify later registration and
    association before publication.
11. **Cleanup:** verify removal of registered singleton ranges despite null
    `m_lookup`, ordinary multi-partition removal, preservation of other caches
    sharing an index, and zero-capacity receiver destruction. Confirm that
    manager teardown removes ranges before freeing arena memory, exactly once.

Adapt existing tests to the new duplicate contract and explicit nonzero
configuration of receiving partitions. Use allocation failure to exercise a
configured zero-capacity receiver where needed. Zero-capacity calls cannot
establish a receiving partition. Late activation starts from a default cache
and its first nonzero configuration, optionally preceded by zero-capacity no-ops.

Retain coverage of `ZmVHeap` configuration through the CSV loader and the
existing variable-size heap tuning integration cases; valid multi-shift rows
must continue to load. Update the heap benchmark's dependent code only if
needed; its public `init` call argument order remains valid.

Build `zm/src` before dependent tests, using `make -j4`. Run the full `zm` suite,
including `ZmAllocatorTest`, and the affected heap tuning integration cases.
Repeat relevant sanitizer and Memcheck checks. Validate on Linux and Windows
when a Windows runner is available. Retain the memory guard for long builds,
stopping below 12 GiB of available memory.

## Implementation status

The manager changes and regression tests are implemented. Configuration keys
use `{ID, vshift, partition}` throughout, counts are derived without stored
state, singleton arenas remain indexed with null free-path lookup pointers,
and manager teardown unregisters arenas before releasing cache references.
Allocation and free operations have no new eligibility checks.

Linux Clang debug validation and the full dependency build have passed.
The full 34-program Zm suite and complete heap tuning integration driver also
pass under AddressSanitizer with leak detection enabled. Memcheck runs of `ZmHeapTest`,
`ZmAllocatorTest` and the complete heap tuning driver (including its child
fixtures) reported zero errors and no definite or indirect leaks.
The clean release build and release duplicate checks remain in progress
or pending. The memory guard stopped the release build when four large Zhttp
test compilations reduced available memory below 12 GiB. That test directory
is being built with `-j1`; the full build then resumes with `-j4`, retaining
the same memory guard. No Windows compiler or runner is available in this
environment.
