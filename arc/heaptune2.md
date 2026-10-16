# One-time heap configuration plan

## Required behavior

Replace row-at-a-time `ZmHeapMgr::init()` with one process-wide call accepting
the complete set of configuration rows. Return `true` on success and `false`
on rejection or a recoverable initialization error.

The user confirmed both of these requirements:

- The restriction is once for the entire process, across all IDs, partitions
  and size classes.
- Starting any worker permanently disqualifies initialization, even after
  that worker exits or is joined.

Only the first invocation is eligible to succeed. Interpret this literally:
a failed first invocation consumes the attempt as well. Later calls return
`false`, including identical input and calls with previously unused keys.
There is no reset or retry API. An empty batch is a valid first call and
consumes the attempt.

Success additionally requires the process's original main thread and evidence
that no other thread has started. A paused or joined worker does not satisfy
this contract. Failure to establish the thread condition returns `false`.

This permits main-thread allocations before configuration. Existing default
caches and outstanding fallback allocations are allowed, provided no worker
has ever started. Heap allocation itself does not consume the configuration
attempt.

## Why cache pointer resolution does not prevent the existing race

`ZmHeapCacheT` resolves its cache under the manager lock once per TLS instance,
then retains the pointer. Subsequent `alloc()` and `free()` use that pointer
without taking the manager lock. Late manager initialization mutates the
same cache object through `c->init()`; it does not replace the worker's pointer.

New cache construction is different: the manager finishes initialization and
lookup registration before returning the pointer. The unsafe case is a
previously returned zero-capacity cache being enabled while its users run.
The startup restriction removes that overlap. See [heapupdate.md](heapupdate.md)
for the reproduced interleaving and the separate ownership lookup defects.

## 1. Change the public API to accept the complete configuration

Add a module-level row type in `zm/src/ZmHeap.hh`, following the existing
all-public struct style. A proposed interface is:

```cpp
struct ZmHeapInit {
  ZuID id;
  uint16_t partition;
  uint8_t vshift;
  ZmHeapConfig config;
};

static bool init(ZuSpan<const ZmHeapInit> rows);
```

Use an owning ID in the row so CSV parsing cannot leave dangling ID spans.
The input span is borrowed only for the call; copy or move the necessary
configuration into manager-owned storage before returning. Retain the existing
configuration key `{ID, partition, vshift}` and arena identity.

Remove the old public single-row signature and propagate the change through
all dependents. Do not keep a single-row forwarding overload: that would make
the process-wide contract easy to misuse. An internal row application helper
must not independently claim the process-wide attempt.

Validate the entire batch before applying rows. Reject duplicate configuration
keys, including identical duplicate rows, so there is no hidden replacement
order. Preserve valid zero-capacity rows and existing CPU-set semantics.
Check arena-length arithmetic where a row is matched to an existing arena.

## 2. Introduce a process-wide startup guard

Put shared guard state in the `zm` implementation, with one exported owner
across the executable and DLLs/shared libraries. Header-local statics would
permit multiple successful calls on Windows and are unsuitable.

Use a small, statically initialized state with an atomic first-attempt claim,
a permanent worker-started latch, and an initialization-in-progress state.
Use existing Z atomic/initialization conventions; the guard must not create
a heap manager, allocate from a configurable heap, or publish a thread
context merely to reject a call. `ZmGlobal.cc` contains a precedent for
statically initialized atomic storage used before singleton construction.

Claim the attempt before checking eligibility or constructing the manager.
Concurrent and reentrant invocations therefore return `false`. Terminal
success and failure both retain the consumed attempt.

Record worker startup in the creator's path, before entering the native
thread-creation call. Do not wait for `ZmThread_start()`, telemetry publication,
or the worker's first `ZmSelf()` call. Never clear the latch on join, detach,
exit or scheduler shutdown. Conservatively closing the gate on a failed
creation attempt is acceptable and must be documented and tested.

Coordinate `ZmThread::run_()` with initialization. It must reject a start
attempt during the initialization transaction rather than allowing a worker
to run through cache mutation. Avoid waiting on a guard held by the same
main thread. Keep guard operations out of allocation/free hot paths.

When touching the Linux creation path, fix its existing `pthread_create()`
failure test: failure is a nonzero return value, not necessarily a negative
value. Correct failure handling is needed to test startup state transitions.

## 3. Establish thread eligibility on Linux and Windows

The Z thread registry is insufficient: it records participating live contexts,
can miss native/library-created threads, and removes exited threads. Counting
entries in it cannot implement the requested history rule.

Complete platform qualification before claiming this feature works on either
platform. Use both the framework latch and platform/runtime evidence that
covers threads created outside `ZmThread`. A live-thread count alone must
never be an affirmative historical check.

### Linux

Check main-thread identity using the native TID against `getpid()`, matching
the Linux thread-group-leader definition. Avoid assigning main-thread identity
to the first caller. [Linux gettid documentation](https://man7.org/linux/man-pages/man2/gettid.2.html)

For supported glibc builds, evaluate `__libc_single_threaded` from
`<sys/single_threaded.h>`. Current glibc clears it when runtime thread creation
begins and does not restore it merely because workers exit. This supplies
history for direct `pthread_create()`/`thrd_create()` users as well as Z
threads. Zero means reject, even when only the main thread currently remains.
The documented contract permits future glibc versions to restore the flag,
so its current historical behavior must be a qualified platform assumption,
covered by the joined-native-worker regression test.
[glibc documentation](https://www.gnu.org/software/libc/manual/html_node/Single_002dThreaded.html)

Use `/proc/self/task` enumeration as a supplementary check for live native
threads and reject enumeration failure. The directory represents current
threads; it cannot prove that an exited thread never existed.
[Linux proc_pid_task documentation](https://man7.org/linux/man-pages/man5/proc_pid_task.5.html)

The historical coverage boundary must be explicit: raw `clone`/`clone3` thread
creation can bypass glibc's flag. Such creation paths must record startup in
the common guard before creation. Audit repository and supported dependency
paths; migrate or instrument bypasses. If a host permits uninstrumented raw
thread creation, this combination cannot establish the literal historical
condition. Supporting that host requires creation-time tracking from process
startup; do not silently accept it using a `/proc` snapshot. Similarly, a
non-glibc build needs a qualified history mechanism before enabling success.
Unsupported or unknown coverage must fail closed.

### Windows

Qualify native process thread history using
`NtQuerySystemInformation(SystemProcessInformation)` and the current process's
`NumberOfThreadsHighWatermark`. The native layout describes that member as
the peak thread count. Require a current count of one and a historical peak
of one, and verify the sole thread ID against `GetCurrentThreadId()`.
The inference is that a process which has only ever had one thread is still
executing its original thread; a worker replacing an exited initial thread
necessarily required a prior overlap during creation.
[PHNT native structure definition](https://github.com/winsiderss/phnt/blob/master/ntexapi.h)

This is a native ABI dependency, not a documented Toolhelp history feature.
Isolate the minimal required declarations in the platform implementation,
validate layouts and returned lengths for supported architectures, and resolve
the query from the already loaded system DLL. Use the returned required
buffer size and existing framework storage facilities. Validate offsets and
process identity while walking the returned records. Query failure,
unsupported layout, missing process, or ambiguous history means `false`.
Microsoft documents that this native interface can change; qualify the actual
supported Windows versions with runtime tests.
[Microsoft NtQuerySystemInformation documentation](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation)

Do not substitute the current `ZmThread_main` marker as the entire proof: it
marks the thread executing a library static initializer, which need not be
the original thread if the library is loaded late. A recorded startup identity
may be used as an additional check, but must not be inferred from the first
`init()` caller.

Toolhelp enumeration can supplement live-thread validation, filtering by
`th32OwnerProcessID`. It cannot replace historical evidence. A native
`CreateThread` or `_beginthreadex` worker that has already exited must still
cause rejection. [Microsoft Toolhelp documentation](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot)

Include loader/runtime-created threads in qualification. If a supported
runtime has already started such threads before application entry, the
literal requirement makes even an early call from `main()` return `false`.
Do not silently exclude those threads to obtain a passing startup test;
record this as a platform usability constraint requiring earlier integration
or an explicit change to the requirement.

### Check through the transaction

Perform platform checks before preparing configuration and again immediately
before committing. Preparation must not invoke application callbacks. If a
library operation starts a worker during preparation, discard the prepared
state and return `false`.

Commit must consist of controlled internal publication with no callbacks,
thread creation, or deferred library initialization. Since successful checks
establish that the main thread is the only thread and none has previously
started, no ordinary in-process worker can concurrently initiate another
thread. Keep framework startup excluded until publication is complete.

## 4. Apply the configuration with defined failure behavior

After claiming the attempt and establishing eligibility, acquire the manager
lock and prepare the full configuration. Existing default caches may be
enabled; future caches obtain their configuration from the committed rows.

Prepare required arena memory, free-list links, configuration nodes and lookup
entries before publishing them into existing caches. A recoverable preparation
failure frees prepared resources and returns `false`, leaving existing cache
state and configuration unchanged. The attempt remains consumed. Include
CPU-set copy/allocation and lookup allocation in the ownership review.

Publish the prepared state only after the final thread check succeeds.
Normalize block size before it becomes part of a manager-map key, preserving
the ordering of existing cache identities. Return `true` after the complete
batch has been applied and registered.

Define `true` as successful configuration and initialization of currently
existing affected caches. It cannot guarantee that a cache first constructed
much later will obtain arena memory; preserve that path's documented fallback
behavior. Framework-fatal allocation handling remains fatal rather than being
misrepresented as a recoverable boolean result.

The startup restriction addresses the live mutation race. The larger-arena
lookup holes and zero-capacity receiver defect in `heapupdate.md` remain
separate issues, including for valid startup batches. Do not claim this change
fixes them or silently expand this implementation to repair them.

## 5. Migrate callers and the CSV lifecycle

- `zi/src/ZiHeapTune.hh`: parse all rows into workload-sized Z storage, then
  call the batch API exactly once. Propagate a boolean result through
  `ZiHeapTune::init(path)` and `load()`, with existing I/O/parse diagnostics.
  A missing environment path submits an empty batch and seals default
  configuration. Input parse failure must occur before manager publication.
- `zm/bench/zmheapbench.cc` and `zmheapbench2.cc`: collect every ID/partition/
  class row, then make one checked call before creating workers or starting
  the scheduler.
- `zm/test/ZmAllocatorTest.cc`: move all required configuration to one setup
  batch before any test creates a worker. The shared key for sharded and
  non-sharded Stats arenas needs only one row. Remove in-test initialization.
- `ZmHashTest2.cc` and `ZmPQTxTest.cc`: submit their single row as a batch and
  check the result before thread startup.
- `zhttp` and `zrest` startup callers: handle configuration failure explicitly
  before starting application workers; do not continue as though tuning was
  applied successfully.
- `zi/itest/zheapwork.cc`: revise the existing late scenario. It currently
  starts workers, waits for them, and then loads another configuration; that
  must now fail. Test legal late initialization by creating default caches on
  the main thread before the sole load. Other partitions first instantiate
  their configured caches after workers start. An initial empty load followed
  by a second load is always a rejection case.
- Audit native creation paths, including `zm/bench/zmlockbench.cc` and
  `zmlockbench2.cc`, against the common startup-history mechanism. Update
  module test documentation and the relevant lifecycle sections of
  `heaptune.md` to describe one load per process.

Keep CSV export usable during the existing supported lifecycle. Saving tuning
for the next process invocation does not reopen initialization in this one.

## 6. Validation and implementation order

Use separate executable invocations for independent first-call tests. A
process-wide reset added just for tests would undermine the contract. Use
ZuTest/ZuTestUtil TAP tests; exercise subprocess orchestration from the
appropriate integration-test directory.

| Case | Expected result |
| --- | --- |
| Main thread, fresh process with no prior workers, multiple rows | First call succeeds and all rows apply. |
| Main thread with existing default caches and live fallback allocations | First call succeeds; fallback allocations remain valid and free correctly. |
| Empty batch | First call succeeds; all subsequent calls fail. |
| Second call, same or different rows | Fails without modifying caches or stored configuration. |
| Invalid or duplicate rows on first call | Fails without publication; the next call also fails. |
| First call from a worker | Fails; a later call on main also fails. |
| Z worker running, paused, detached, or already joined before first call | Fails in every case. |
| Direct native worker, with no Z API calls, running or already joined | Fails on Linux and Windows. |
| Worker created before Z library initialization | Historical platform check rejects it, including after exit. |
| Main-thread identification under late library loading | Worker loading the library never gains permission to initialize. |
| Thread-start attempt during preparation | Cannot overlap commit; initialization or thread start is rejected according to the guard ordering. |
| Native history query unavailable or fails | Returns false; no live-count-only fallback. |
| Injected recoverable arena/preparation failure | Returns false, releases staged resources, preserves existing state, and consumes the attempt. |
| CSV with multiple partitions and classes | One checked manager call; all rows round-trip and apply. |
| Existing integration-test second-load/late-worker cases | Explicit rejection with unchanged configuration. |

First build minimal platform probes for main-thread identity, native worker
creation, worker exit, and historical evidence. Linux validation must include
direct pthreads, and Windows validation must include direct `CreateThread`
and `_beginthreadex`, not only `ZmThread`. Record the actual libc/Windows
versions and architectures; an unsupported native history mechanism is an
implementation blocker rather than grounds to weaken the requirement.

Then implement the shared startup guard and batch API, migrate callers,
revise the lifecycle tests, and run the focused allocator/tuning tests. Build
with `make -j8`, rebuilding `zm/src` before dependent targets. Validate both
native Linux and Windows/msys2-mingw runs; cross-compilation alone does not
establish thread-history correctness. Run the applicable configured debug
and memory-checking suites after the focused tests pass.

This document is the implementation plan. Platform probes, API changes and
caller migrations have not yet been implemented.
