# Zm: Multithreading foundation

Zm supplies Z's execution and allocation runtime: policy-based containers,
intrusive ownership, delegates, threads, rings, scheduling, named heaps and
diagnostics. Include individual headers such as `<zlib/ZmHash.hh>` and link
`libZm` with its dependencies. Networking builds on these facilities in
[Zi](../zi/README.md); owning text and arrays are in [Zt](../zt/README.md).

## Containers and indices

| Facility | Choose it for |
| --- | --- |
| `ZmHash` | Separately chained hash lookup, with node/ownership policies and optional striped locking |
| `ZmLHash` | Linear-probing, by-value lookup for unlocked or mostly uncontended reference data |
| `ZmPolyHash` | Multiple intrusive hash indices over the keys of a reflected object |
| `ZmList`, `ZmRBTree` | Linked traversal and ordered tree indices |
| `ZmCache`, `ZmPolyCache` | LRU eviction with one hash index or multiple reflected-key indices |
| `ZmStack`, `ZmQueue` | Stack and queue storage |
| `ZmPQueue`, `ZmPQRx`, `ZmPQTx` | Sequence reassembly and transmit/receive queueing, including gaps and overlaps |
| `ZmBitmap` | Runtime-sized bitmap used for topology and resource selection |

`ZmPolyHash` consolidates an object and its index nodes into one allocation;
the primary index normally owns it. Use the reflected key metadata and
[ZmPolyHashTest](test/ZmPolyHashTest.cc) when adding secondary indices.

`ZmPQueue` is a deterministic skip-list sequence queue, not a conventional
binary heap for arbitrary priorities. Configure its item key/length operations
for the sequence being represented.

A simple hash of unsigned keys uses the default value-as-key accessor:

```cpp
#include <zlib/ZmHash.hh>

void lookup() {
  ZmHash<unsigned> values;
  values.add(42U);
  auto found = values.find(42U);
  auto removed = values.del(42U);
}
```

For records, use `ZmHashKey` to select the accessor and named policies for
locking, heap identity and node behavior. `ZmHashDerive` exposes a derived
container/node pair. `ZmHashParams` controls bucket sizing, load factor and
lock striping; `ZmHashMgr` provides named configuration and telemetry.
[ZmHashTest2](test/ZmHashTest2.cc) demonstrates both reference-owned records
and concurrent operations. [ZmRBTest](test/ZmRBTest.cc) and
[ZmLHTest](test/ZmLHTest.cc) cover tree and linear-hash usage.

Container policies determine node retention and removal semantics. Do not
assume a raw pointer returned during iteration remains valid after mutation
or that a `ZmNoLock` instance becomes safe merely because another object is
locked. Use `iter()`/`citer()` and their node accessors as shown in the tests.

## Ownership and callbacks

`ZmObject` supplies atomic intrusive reference counting; `ZmRef<T>` retains
objects without a separate control block. Use `ZmPolymorph` when the lifetime
model requires polymorphic destruction. Atomic reference counting protects
ownership, not concurrent access to the object's fields. Owners still need to
quiesce callbacks and break any reference cycles before shutdown.

`ZmFn<Signature>` is a function delegate optimized for a function plus a small
context pointer; larger captures can allocate. `ZmRingFn` packages captured
functions for ring storage and invocation. Its fast path moves the payload
into the ring; deferred/move-assigned wrappers can require heap promotion.
See [delegate tests](test/ZmFnTest.cc) and [ring-function tests](test/ZmRingFnTest.cc).
Capture fixed metadata and owning handles across threads, not references to
stack objects that will have expired when the function runs.

## Threads, synchronization and scheduling

`ZmThread` starts an explicitly owned worker; `ZmThreadParams` selects its
stack, priority, affinity and partition. Join workers before destroying the
state they access. `ZmLock`, `ZmRWLock`, `ZmPLock`, `ZmGuard`, `ZmCondition`,
`ZmSemaphore` and `ZmAtomic` provide synchronization where shared state is
intentional. `ZmSpecific` supplies thread-specific objects; `ZmSingleton`
provides shared runtime instances.

`ZmScheduler` owns a configured set of destinations and timer dispatch:

```cpp
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

void schedule() {
  ZmSemaphore done;
  ZmScheduler scheduler{ZmSchedParams{}.id("app").nThreads(1)};
  if (!scheduler.start()) return;
  scheduler.run([&done] { done.post(); }, 1);
  done.wait(); // wait on the caller, never on the destination thread
  scheduler.stop();
}
```

`run()` always queues; `invoke()` can call immediately when already on the
selected destination. Use `run()` when deferral matters for ordering or stack
depth. Configure isolated destinations, CPU sets, partitions and queue sizing
before startup. `ZmTime`, `ZmTimeout` and `ZmBackoff` support clocks, timeouts
and retry scheduling. [ZmSchedTest](test/ZmSchedTest.cc) covers timed work and
thread-specific state; `ZmEngine` supplies a higher-level execution facility
with its own [tests](test/ZmEngineTest.cc).

## Ring buffers and message passing

`ZmRing` supports fixed- and variable-size records with single/multiple writers
and readers. Multiple-reader mode broadcasts each message to every reader;
use separate destination rings to distribute unicast work. Choose the writer,
reader and sizing policies for the actual producer/consumer topology.

The low-level interface reserves storage, publishes a completed record, and
lets readers consume/retire it. Respect those phases: uncommitted storage is
not a delivered message, and a borrowed record cannot outlive reader retirement.
Use [ZmRingTest2](test/ZmRingTest2.cc) and
[ZmRingTest3](test/ZmRingTest3.cc) for concrete setup and producer/reader loops.
Most applications should post functions through `ZmScheduler`; use the raw
ring when defining a record protocol. For cross-process broadcast see `ZiRing`.

## Heap tuning

`ZmNHeap<T>` supplies pooled fixed-size object allocation; `ZmVHeap` supplies
variable-size classes. Assign meaningful heap IDs and configure each logical
partition's reserve before the workload starts. Reserves recycle storage and
fall back to ordinary allocation when exhausted: they are not admission limits.
`ZmAlloc` provides scoped allocation support for temporary work.

The detailed capacity, ownership and counter rules follow. File-based tuning
and measurement utilities are exposed by [ZiHeapTune](../zi/README.md#resource-tuning).

### Heap arena sizing

`ZmNHeap` uses `ZmHeapCacheT` to retain a thread-local pointer to a
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

### Fixed-size object allocation

Inherit `ZmNHeap` with the concrete type and declare its heap ID through ADL:

```c++
struct Item : public ZmNHeap<Item> {
  friend inline ZuStringT<"App.Item"> ZmNHeapID(Item *);

  uint64_t value;
};
```

Declare `friend inline ZuTrue ZmNHeapSharded(Item *);` for partition-local
allocation and freeing. The default is non-sharded. An omitted ID or an empty
ID disables recycling and uses ordinary allocation. `ZmNHeapID_<Item>` and
`ZmNHeapSharded_<Item>` expose the selected metadata.

`ZmNHeap<T, false>` declares no allocation operators. Disable the base heap
when a derived class supplies its own allocator; shadow nodes use this form.

### Capacity and fallback allocation

When an arena is exhausted, allocations fall back to the ordinary heap.
Configured capacity is therefore a recycling reserve, not a limit on total
heap usage.

### Freeing across partitions

The `ZmNHeap` base class routes `operator new` and `operator delete` through
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

## Diagnostics and supporting facilities

`ZmBackTrace` captures/formats call stacks, `ZmBackTracer` tracks scoped execution,
`ZmAssert` enforces debug invariants, and `ZmTrap` installs process signal/trap
handling. The `Ztc::*Mgr` watch interfaces track resource creation/removal. `ZmRandom`
is a non-cryptographic Mersenne Twister, and `ZmCodec` supplies scoped
Base64/Base32/hex conversion callbacks. Use `Ztls::Random`
when the caller needs the security module's random generator. Component
headers and the [test directory](test/) show the corresponding APIs.

## Examples and tests

Run `make -C zm test` from a configured build tree. See
[test/README.md](test/README.md) for focused coverage and the top-level
[testing guide](docs/TESTING.md) for sanitizers and debugging. Heap tests include
cross-thread and cross-partition ownership cases; run them when changing
allocator policy rather than relying only on single-thread smoke tests.
