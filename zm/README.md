# Zm

## Heap arena sizing

`ZmHeap` uses `ZmHeapCacheT` to retain a thread-local pointer to a
`ZmHeapCache` arena. The arena owns its atomic local counters and points
to atomic fallback counters shared by all arenas of the same ID. The arena
is selected using the calling thread's logical partition. Configuration is
stored by `{ID, partition}`, while arenas are keyed by
`{ID, partition, size, alignment, sharded}`.

`ZmHeapConfig::cacheSize` specifies a **number of blocks**, not bytes. For
one fixed-size heap class, each arena receives the full configured block
count for its partition:

```text
arena bytes = configured cacheSize × aligned block size
```

The block size includes `ZmHeapAllocSize` rounding and the arena's alignment
rounding. There is no automatic division by the number of threads or
partitions.

If `M` is the configured block count, `N` is the number of threads, and `P`
is the number of used partitions configured with that count:

| Arrangement for one heap class | Total cached blocks |
| --- | --- |
| N threads sharing one partition | M |
| N threads distributed across P partitions | M × P |
| One partition per thread | M × N, because P = N |

Threads using the same arena share its blocks; each thread does not receive
a separate allotment of `M/N`. Likewise, each partition receives `M`, not
`M/P`. Partitions can have different configurations, in which case the total
is the sum of their configured block counts.

For example, 16 threads distributed across four partitions, each configured
with `cacheSize = 1,000`, reserve 4,000 blocks for one heap class. With an
aligned block size of 64 bytes, that is **256,000 bytes**, excluding metadata
and fallback allocations.

Configuration applies only to the specified partition. Configuring partition
0 does not configure other partitions. An unconfigured arena has zero cached
blocks and falls back to ordinary heap allocation. Arenas are created on
demand, so configuring an unused partition does not itself allocate an arena.
An arena created before configuration can acquire its reserve when configured;
resizing an existing nonzero reserve is not supported.

Multiple size, alignment, or sharding combinations under the same ID create
separate arenas. Each independently receives that partition's configuration;
`cacheSize` is not a shared budget across these arenas. Additional TLS
instances or DLL references using the shared Zm runtime do not independently
multiply arena capacity.

### Variable-size heaps

`ZmVHeap` uses several block size classes. Class `i` passes `i` as the cache's
`m_vshift`, so it receives `M >> i` blocks. Its total reserved bytes per
partition are:

```text
sum over instantiated classes i:
    (M >> i) × alignedBlockSize[i]
```

This reduces the number of cached blocks for larger classes; it does not
divide capacity by thread or partition count.

### Capacity and fallback allocation

When an arena is exhausted, allocations fall back to the ordinary heap.
Configured capacity is therefore a recycling reserve, not a limit on total
heap usage.

### Freeing across partitions

The `ZmHeap` base class routes `operator new` and `operator delete` through
the same cache specialization, including size, alignment, and sharding.
The freeing thread resolves that specialization's arena for its own logical
partition.

For a non-sharded heap, freeing first checks whether that arena owns the
block. Threads in the same partition share the arena, so a cached block
allocated by one thread and freed by another in that partition takes this
direct path.

If the current arena does not own the block, its lookup, when present,
searches for the owning arena. A successful `other` lookup in
`ZmHeapCache::free()` therefore means that the block came from a different
partition's arena: it requires more than one partition. The block is returned
to that originating arena, whose `frees` and `crossFrees` counters are
incremented. DLL boundaries alone do not cause this path.

A lookup can still be attempted with only one partition when the block came
from an ordinary heap fallback allocation. Such a block has no owning arena;
the lookup finds nothing and `alignedFree()` releases it.

Sharded heaps skip the other-arena lookup. Their cached blocks must be freed
within the owning partition.

See [ZmHeap.hh](src/ZmHeap.hh) for the TLS access path and block size rounding,
[ZmHeap.cc](src/ZmHeap.cc) for configuration lookup and arena allocation, and
[ZmVHeap.hh](src/ZmVHeap.hh) for variable-size classes.

### Statistics

`ZmHeapCache::globalStats()` exposes per-ID fallback counters shared across
all partitions, block sizes, alignments and sharding variants:

- `heapAllocs`: successful ordinary heap fallback allocations.
- `heapFrees`: ordinary heap fallback frees, regardless of freeing partition.
- `heapMax`: estimated peak of outstanding fallback allocations.

`ZmHeapCache::stats()` exposes counters for that arena:

- `allocs`: blocks allocated from its reserve.
- `frees`: blocks returned to its reserve, including cross-partition returns.
- `crossFrees`: reserve blocks returned from another partition.
- `heapAllocs`: fallback blocks allocated through this arena.
- `heapFrees`: fallback blocks freed through this arena.

Both sets of counters are retained after allocating/freeing threads exit.
They count blocks, not bytes; global counters combine size classes.

Telemetry calls the reserve counters `cacheAllocs`, `cacheFrees` and
`crossFrees`. `heapAllocs` and `heapFrees` are the arena's fallback counters.
These per-arena counters can be summed directly. The per-ID counters are
`globalHeapAllocs`, `globalHeapFrees` and `globalHeapMax`; each arena row
repeats them, so aggregate consumers must count them only once per ID.

`allocated()` combines the arena's reserve and fallback balances. Fallback
blocks carry no arena ownership, so a partition that frees blocks allocated
elsewhere can have a negative balance represented as an unsigned value.
Summing balances across all arenas of an ID recovers the total.

Telemetry samples counters independently, so live samples are approximate.
The peak is intentionally an estimate: concurrent updates may miss a peak
or overwrite it with a lower observation.

`zm/test/ZmAllocatorTest` covers reserve and fallback accounting, sharded
heaps, shared statistics after thread exit, cross-partition returns, and
per-ID sharing across size/alignment/sharding variants.
