# Zm tests

Build the library before the test programs:

```sh
make -C zm/src -j4
make -C zm/test -j4
make -C zm/test test
```

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
