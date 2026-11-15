# Zm tests

`ZmCacheTest` and `ZmPolyCacheTest` cover empty synchronous and asynchronous
iteration followed by cache access, including the deleting `ZmCache` variants.
These regressions ensure empty scratch storage does not leave the cache locked.

Build the library before the test programs:

```sh
make -C zm/src -j8
make -C zm/test -j8
make -C zm/test test
```

`ZmSchedProgressTest` gates overflow publication after the worker's last queue
check and before its empty-ring read. It verifies LL1 progress without a future
submission, FIFO execution, and final drain. It compiles a separate copy of Zm
with `ZmSchedProgress_TEST`; its gates are absent from the library.

`ZmRingWaitTest` covers LL0 spin, waiter registration, publication and EOF before
sleep, full writers, timeouts, and shared reader/writer wake obligations across
all four ring modes. Existing `ZmRing_FUNCTEST` gates coordinate the races.
Its counters measure blocker entries and wake calls, not time actually asleep.
The ordinary `ZmRingTest2` suite also exercises zero-spin, indefinite blocking.

`ZmHeapTest` covers late arena publication, null ownership bounds, singleton
lookup avoidance, configuration prefix counts and key order, duplicate rejection,
zero-capacity no-ops, allocation failure, unequal arena sizes, lookup rebuilding,
live partition transitions, configured zero-capacity receivers, and teardown.
Duplicate tests launch fresh child processes: debug builds check assertion deaths;
release builds check that rejection leaves the configuration and cache unchanged.
It emits TAP and runs in the regular `make test` target. For focused validation:

```sh
./zm/test/ZmHeapTest
./zm/test/ZmAllocatorTest
```

The heap test executable compiles a separate copy of Zm with `ZmHeap_TEST`.
Private hooks coordinate the relevant interleavings with semaphores and inject
arena allocation failure. The internal lookup fixture supplies exact address
ranges to test bucket boundaries without depending on OS allocation layout.
The installed library contains none of these hooks. No alternate library is
loaded alongside `libZm` in the test process.

Use `./libtool exec` to run test binaries under debugging or memory-checking
tools. Windows/msys2-mingw uses the same test sources and cases.

`ZmThreadExitTest` leaves a worker to library-level singleton cleanup, after
application globals have retired. It verifies startup through TAP; run it with
`./libtool exec valgrind --leak-check=full ./zm/test/ZmThreadExitTest` to verify
that the thread's exit destructors release their TLS records before key cleanup.

`ZmSpecificTest` checks POSIX TLS-record destruction when a key retires on its
current thread and when a worker exits before key retirement. It verifies each
record is destroyed once, including a key used by both threads. This test runs
on POSIX platforms only.

`ZmPolyHashTest` verifies both owner-named derive nodes and direct
`ZmPolyHash` nodes across all four indexes, including secondary-index removal.
The node resolver aliases the native node instead of adding another layer of
inherited constructor overloads, avoiding GCC's recursive overload expansion.
