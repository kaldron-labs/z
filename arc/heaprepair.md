# Narrow repairs for heap findings 1–4

This document defines the four narrow heap repairs: register ownership before
publishing an arena, reject a null ownership lower bound, cover every lookup
bucket, and attach shared lookups to receiving caches across partitions.

Arena resizing remains unsupported. Telemetry continues to permit approximate
snapshots and tearing. The public initialization API is outside these repairs.

## Finding 1: register ownership before enabling allocation

The current `ZmHeapCache::init_()` publishes `m_head` before the manager calls
`lookupAdd(c)`. A worker with a previously resolved cache pointer can allocate
from the new arena during that interval. Another partition can then fail to
find the arena and incorrectly send its block to the system allocator.

Split preparation from publication:

1. Allocate arena memory, initialize its free-list links, and set its bounds.
2. Register its address range and attach the lookup to matching caches using
   `lookupAdd()`, extended as described under Finding 4.
3. Publish its initial head using the existing release assignment to `m_head`.

Keep allocation, free and lookup algorithms otherwise unchanged. Use the
existing manager lock around preparation, registration and publication.
Workers with saved cache pointers continue to use fallback allocation while
the unpublished head is zero.

### Changes in `ZmHeap.hh` and `ZmHeap.cc`

Make the private `ZmHeapCache::init()` and `init_()` return `bool`, meaning
that this invocation successfully prepared a new arena. This is an internal
result, separate from any proposed public `ZmHeapMgr::init()` result.

- `init()` returns `false` when the cache already has a nonzero capacity.
  Otherwise it assigns the configuration and returns `init_()`'s result.
- `init_()` returns `false` for zero capacity or arena allocation failure.
  Preserve the existing failure behavior that resets capacity to zero.
- On successful preparation, `init_()` initializes links and bounds and returns
  `true`. Remove its assignment to `m_head`.
- Add a private `publish_()` containing just the initial release assignment:

```cpp
void publish_() {
  m_head = reinterpret_cast<uintptr_t>(m_begin);
}
```

Update the manager's existing-cache configuration path to publish only when
this call actually prepared a new arena:

```cpp
if (c->init(config, hwloc)) {
  lookupAdd(c);
  c->publish_();
}
```

The constructor continues to call `init_(hwloc)` to prepare any configured
arena. In the manager's new-cache path, retain `lookupAdd(cache)` and then
publish a successfully prepared arena before invoking `m_addFn` or returning
the cache pointer:

```cpp
lookupAdd(cache);
if (cache->begin()) cache->publish_();
if (m_addFn) m_addFn(cache);
return cache;
```

That `begin()` check is valid specifically in this newly constructed-cache
path. Its early return for an already existing cache remains unchanged.
Sharded caches still publish normally even though `lookupAdd()` skips them.
Zero-capacity and failed allocations retain their zero head.

**Never republish an existing arena's initial head.** Doing so would put
outstanding allocations back on the free list. In particular, `m_head == 0`
cannot identify an unpublished arena: it also represents an exhausted live
arena. The private preparation result provides the necessary distinction
without another per-cache state field.

Registration preceding publication closes Finding 1: any newly issued arena
block now has an ownership entry before it can be transferred to another
partition. Moving the existing `lookupAdd()` above preparation would not work;
the new arena's capacity and bounds must already exist.

## Finding 2: reject the initial null lower bound

Change `owned()` in `ZmHeap.hh` to read the lower bound once and reject null:

```cpp
bool owned(void *p) const {
  auto begin = m_begin;
  return begin && p >= begin && p < m_end;
}
```

The existing expression can read the old null lower bound, then the newly
initialized upper bound, and incorrectly claim a fallback or foreign pointer
below the real arena start. With this change, a null lower-bound observation
returns `false` without consulting the upper bound.

The argument relies on the existing one-time bounds initialization and the
explicit prohibition on arena resizing. This repair addresses the identified
ownership misclassification; it does not introduce an arena descriptor,
telemetry synchronization, or an allocation/free manager lock.

## Finding 3: interpolate every arena over the lookup buckets

Repair `ZmHeapLookup::add()` so the lookup uses the smallest arena-derived
shift introduced so far and covers every bucket intersecting each arena.
The arenas themselves retain their original sizes. This handles unequal
capacities across partitions sharing the same `{ID, size}` lookup.

Calculate a candidate shift for every added cache, using its inclusive last
address and the existing rounding rule:

```cpp
auto begin = reinterpret_cast<uintptr_t>(c->begin());
auto end = reinterpret_cast<uintptr_t>(c->end()) - 1;
unsigned shift = (sizeof(end)<<3) - ZuIntrin::clz(end - begin);
```

This is the ceiling of log2 of the arena's byte length. Valid arenas have
room for free-list links, so the existing zero-shift sentinel can continue
to represent an uninitialized lookup.

Under the existing lookup lock, handle the candidate as follows:

| Candidate | Action |
| --- | --- |
| First arena | Set `m_shift` to the candidate and add its covered buckets. |
| Less than `m_shift` | Save each existing arena once, remove all existing index entries, lower `m_shift`, and re-add all saved arenas with interpolation. Then add the new arena. |
| Equal to `m_shift` | Add the new arena's covered buckets; these span at most two buckets. |
| Greater than `m_shift` | Keep `m_shift` and interpolate the new arena across every bucket it covers. |

### Common range insertion

Factor a private insertion helper that assumes the lookup lock is held and
the shift has already been selected. For each cache, insert the pair
`{bucket, cache}` for every bucket from `begin >> m_shift` through
`(end - 1) >> m_shift`, inclusive, where `end` here is the cache's exclusive
end pointer. Each pair must occur exactly once.

An inclusive loop can terminate on the last key without calculating
`last + 1`:

```cpp
for (uintptr_t key = first; ; ++key) {
  m_hash.add(key, c);
  if (key == last) break;
}
```

Use this helper for the first arena, equal/larger additions, and every arena
reinserted after a shift reduction. Retain `find()`'s exact `owned(p)` check:
multiple distinct arenas may share a bucket, and bucket membership alone
does not establish ownership.

### Rebuild when the shift decreases

Keep the lookup lock held throughout collection, removal, shift change and
reinsertion. A concurrent `find()` must observe either the complete old index
or the complete new index.

Recover distinct arenas from the existing hash without adding a permanent
arena registry. Select only each arena's first-bucket entry: its key equals
`reinterpret_cast<uintptr_t>(arena->begin()) >> oldShift`. Because range
insertion creates exactly one entry for each `{bucket, arena}` pair, that
selects each arena exactly once even when it has many interpolated entries.

Count those selected entries, allocate workload-sized `ZmScratch` pointer
storage, then collect them in a second traversal under the same lock. Use
`i()` to retrieve each complete hash entry and inspect its key and cache
together; the separate iterator `key()` and `val()` operations advance the
iterator independently. End each iterator's scope before mutating the hash.

Call `m_hash.clean()` to remove the old entries while retaining its allocated
table, set `m_shift` to the smaller value, and invoke the private insertion
helper once per saved arena. Finally insert the new arena and assign its
lookup pointer. Existing arenas keep their lookup pointers throughout.

Do not call the public `del()`/`add()` recursively during this rebuild: they
acquire the same nonrecursive lock. The manager owns the arena lifetimes,
and the existing manager/lookup lock ordering keeps the saved pointers valid.

### Symmetric removal

Update `ZmHeapLookup::del()` to remove every `{bucket, c}` entry from the
arena's first through last bucket under the current shift, then clear its
lookup pointer. Deleting only the endpoints would leave stale interior
entries. Delete by both key and cache so other arenas sharing those buckets
remain indexed.

Use the same bucket-range calculation for insertion and removal. Removing
an arena need not raise `m_shift` or rebuild the index: retaining a finer
bucket width preserves coverage. Any later addition still computes and
compares its own candidate shift.

The extra index storage is proportional to the number of covered buckets;
smaller additions can increase that count for existing larger arenas. The
scratch array is proportional to the number of distinct existing arenas.
Keep this work in add/remove operations; `find()` retains its current path.

## Finding 4: attach the lookup to caches in the other partitions

When an arena is enabled, its address range is registered and its own cache
receives a lookup pointer. Existing zero-capacity caches in other partitions
are currently skipped. A free through one of those caches never consults the
registered ownership information.

Extend the manager's `lookupAdd()` to attach the shared lookup to all matching
non-sharded caches, including zero-capacity caches. Use the existing
`m_id2Cache` equal-ID traversal and filter by the lookup's normalized block
size. Match the actual lookup key `{ID, size}`: partition, alignment and
`vshift` do not add restrictions to that key. Skip sharded caches.

Normalize block size at the manager's cache entry point, before cache-key
lookup and construction, rather than only when preparing a nonzero arena.
Thus zero-capacity receivers and enabled arenas use the same normalized size,
and the size remains stable when a cache is enabled later.

Keep the manager lock held while accessing the lookup map and traversing
the caches. For a newly prepared arena, the complete sequence becomes:

1. Prepare its memory, links and bounds.
2. Find or create the shared `{ID, size}` lookup.
3. Register the new arena's address range, including Finding 3 interpolation.
4. Iterate matching existing caches across all partitions and attach that
   lookup wherever it is missing. Already attached matching caches retain
   their existing pointer.
5. Publish the new arena's head only after those pointer assignments finish.

The scan must complete before publication: making blocks available first
would leave another window in which a zero-capacity receiver can obtain an
arena block while its lookup pointer is still null.

### Caches created afterward

Handle the reverse creation order as well. When constructing a non-sharded
zero-capacity cache, search `m_lookups` for its `{ID, size}` and attach the
existing lookup before returning the cache pointer, even though this cache
has no address range to register. The current early return on zero capacity
must therefore move after this lookup-association work.

If no lookup exists and the new cache has no arena, leave its pointer null.
The scan when the first matching arena is enabled will attach it. There is
no need to allocate an empty lookup for every zero-capacity cache.

### Separate association from address registration

Remove `c->lookup()` as an unconditional reason to skip arena registration.
A zero-capacity cache can now already have a lookup pointer when it is later
enabled. That new arena still needs its range inserted.

Use the call-site distinction established in Finding 1: register ranges for
a newly constructed cache with an arena, or when private `init()` reports
successful preparation of a new arena. Those paths each register the range
once. Lookup association alone does not imply that this cache owns an arena,
and does not require another per-cache registration flag.

The cross-partition scan is required when a lookup first becomes available.
Later arena additions can reuse the established associations; newly created
caches attach through the construction path. Keep this work in manager
operations and preserve the existing free-path lookup algorithm.

### Cleanup

Adjust `ZmHeapCache::final_()` so a non-null lookup pointer alone does not
trigger range removal. A cache with both an arena and a lookup unregisters
its covered buckets. A zero-capacity cache with only a lookup pointer skips
that operation; it must not pass null bounds to the interpolating `del()`.

Retain the manager's existing lookup lifetime: the lookup outlives all caches
that reference it. Do not delete a shared lookup when an individual arena is
removed, since other caches can retain pointers to it. Sharded arena cleanup
continues to free its memory without lookup registration.

## Focused validation

For findings 1 and 2, use equal nonzero capacities for the participating
non-sharded arenas to isolate those repairs. For finding 3, deliberately vary
capacities while keeping receivers configured, so the separate zero-capacity
receiver finding does not interfere.
Use existing Z thread, semaphore and TAP test facilities. Keep any deterministic
scheduling hooks private to the test build.

1. **Preparation versus publication:** start with a worker's already resolved
   zero-capacity cache and a configured receiver in another partition. Pause
   the updater after preparation and before registration, then after
   registration and before publication. Allocations through the worker's saved
   pointer must still use fallback in both windows. After head publication,
   allocate an interior arena block and free it from the receiver before the
   updater returns. It must increment the owner's cache/cross-free counters,
   with no system free of that block. Also free a fallback block held across
   publication and verify it follows the system allocator path.
2. **Null-bound interleaving:** use a private test seam immediately after the
   local lower-bound snapshot. Start with that snapshot null, prepare the
   arena while the reader is paused, then resume it. A pointer below the real
   begin must remain unowned even though the end is now initialized. Also
   verify ordinary below-begin, in-range and end-exclusive checks after
   initialization. Synchronize the test handoff so it deterministically
   exercises the mixed observations rather than depending on stress timing.
3. **Publication occurs only once:** repeat configuration with outstanding
   arena allocations, including a fully exhausted arena. Verify that no
   outstanding block becomes available again. Cover configured construction,
   late activation, sharded construction/activation, zero capacity, and an
   injected arena allocation failure followed by successful preparation.
4. **Larger arena added later:** register a 256-byte arena followed by a
   4096-byte arena with the same ID and block size in another partition.
   Verify ownership lookup and cross-partition frees throughout the larger
   arena, including interior buckets absent from the old implementation.
5. **Smaller arena added later:** reverse that order. Verify that lowering
   the shift preserves lookup coverage for every existing arena, adds the
   new arena, and does not duplicate `{bucket, arena}` entries. Add another
   smaller arena to exercise a second rebuild and an equal-shift arena to
   exercise the no-rebuild path.
6. **Bounds and removal:** exercise ranges spanning bucket boundaries,
   non-power-of-two lengths, and distinct arenas sharing a bucket. Delete
   an interpolated arena and verify that every one of its entries disappears
   while other arenas remain findable. Cover removal after a shift reduction,
   emptying the lookup, and adding an arena again afterward. Use a focused
   internal lookup fixture for address layouts the OS allocator cannot
   reliably produce.
7. **Lookup during rebuild:** use deterministic test synchronization to
   hold the updater inside the locked rebuild while another thread attempts
   lookup. It must complete against the rebuilt index without observing
   partially removed or reinserted coverage.
8. **Existing zero-capacity receivers:** create matching zero-capacity caches
   in multiple partitions, then enable an arena in one partition. Transfer
   its interior blocks to each other partition for freeing immediately after
   publication. Verify owner cache/cross-free counts and no system free of
   those blocks. Unrelated IDs, sizes and sharded caches must not acquire
   that lookup.
9. **Receiver created later:** create and register the producer arena first,
   then construct a matching zero-capacity receiver in another partition.
   Verify that its first cross-partition free finds the owner. Also retain
   a receiver fallback allocation across lookup attachment and verify that
   it still goes to the system allocator.
10. **Receiver subsequently enabled and destroyed:** enable an arena on a
    cache that already received a lookup pointer through the partition scan.
    Verify that its new range is registered exactly once and is findable
    from other partitions. Separately destroy a lookup-only zero-capacity
    cache and verify that it neither attempts null-range removal nor changes
    the remaining arenas' lookup coverage.

Build `zm/src` and then the affected tests with `make -j4`, run the focused
checks and existing `ZmAllocatorTest`, and exercise the changes on Linux and
Windows. These checks cover all four findings and their interaction during
late activation, cross-partition free, and cleanup.

## Implementation and validation

The four repairs are implemented in `zm/src/ZmHeap.hh` and `ZmHeap.cc`.
The public initialization API remains unchanged. The manager publishes a head
only after successful preparation, ownership registration and receiver
association. Lookup insertion and removal cover every intersected bucket;
shift reductions rebuild the existing entries under the lookup lock.

`zm/test/ZmHeapTest.cc` implements deterministic publication, null-bound and
rebuild interleavings, allocation failure/retry, repeated initialization,
synthetic bucket layouts, actual cross-partition frees, and receiver lifecycle
checks. It also covers receivers whose requested size needs alignment rounding.
Its private hooks are compiled into a separate test executable, with no hooks
in the production library. `zm/test/README.md` documents the test entry points.

Validation so far on Linux x86-64 with Clang:

- `make -C zm/src -j8` and `make -C zm/test -j8`: passed.
- `make -C zm/test test`: passed, 34 programs / 263 top-level TAP tests,
  including `ZmHeapTest` and `ZmAllocatorTest`.
- Memcheck with full leak checking: the heap and allocator tests passed with
  zero errors and zero definitely, indirectly or possibly lost bytes. The
  existing `/tmp/heaptune-debug/vg-run` wrapper supplies a debug-enabled glibc;
  the host loader lacks symbols required by Valgrind.
- AddressSanitizer and LeakSanitizer: all 34 `zm` test programs passed in the
  separate `/tmp/heaprepair-asan` build, with `ASAN_OPTIONS=detect_leaks=1`.
  LeakSanitizer required execution outside the sandbox to inspect threads.
- The broader Memcheck suite stalled in existing concurrent hash tests and
  was stopped; it is not recorded as a suite pass.

At the user's request, remaining builds use `-j4`. The top-level sanitizer
build is continuing under a guard that stops it if available memory falls
below 12 GiB.
Windows build/runtime validation remains outstanding: this environment has
neither a MinGW compiler nor Wine, and a Windows/MSYS2 runner is needed.
