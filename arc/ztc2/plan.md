# Implementation plan: replace `Ztc::App` TCP telemetry with `ZiRing`

This is an intentional protocol/API break. Implement `goal.md` directly: one
consumer owns the shared telemetry ring; each publisher owns one request ring and
PID registration; extra readers are passive monitors, not identified consumers.
Delete the TCP implementation and propagate the break without adapters.

Prefer existing framework behavior throughout. In particular, do **not** expose
a ring-GC API: `ZmRing_::Ring::open(Write)` already calls its internal `gc()`.

## 1. Minimal `ZiRing` open-existing support

Publishers must open, but never create, the consumer-owned telemetry ring.

1. Retain the existing size convention: nonzero `ZiRingParams::size` creates;
   zero opens an existing ring and obtains its size from the control mapping.
2. In `ZiRing.cc`, use `params.size != 0` as the create/open-existing signal
   (the control mapping's `size` argument is always the control-block size).
   Add `ZiFile::Create` to `CtrlMem`, `DataMem`, and `MirrorMem` only in create
   mode.
3. On Windows, make `ZiRing_::Blocker::open()` choose
   `CreateSemaphore`/`OpenSemaphore` from `params.size`, and update the
   `ZiFile::open_()` shared-memory branch to honor `ZiFile::Create` by choosing
   `CreateFileMapping`/`OpenFileMapping` with matching access. Linux already
   obtains the corresponding behavior when `ZiFile::Create` is absent and
   `shm_open` therefore omits `O_CREAT`.
4. If `ZmRing_::Ring::openSize_()` returns zero, unwind the control mapping,
   blockers, flags, and size exactly like the later `openData()` failure path so
   the same object can retry later.
5. Do not make `RingExt::gc` public or call it explicitly. Write-open already
   reaps dead readers; `reset()` immediately afterward detects a remaining live
   reader.

Test that size-zero open creates no ring/blocker object, fails cleanly when
absent, succeeds on the same object after creation, and preserves creator size.

Guidelines: reuse the size sentinel and built-in GC rather than adding flags or
duplicating lower-level behavior ([No dogma](GUIDELINES.md#no-dogma),
[Framework fit](GUIDELINES.md#framework-fit)). Cleanup remains explicit and
timely, not a background scan ([General principles](GUIDELINES.md#general-principles)).
The Windows branch is required by [Target systems](GUIDELINES.md#target-systems).

## 2. Apply the FlatBuffers break once

Change schemas and regenerate through existing `Makefile.am` rules; never edit
generated headers manually.

- `ztc_telemetry.fbs`: move `id:string` from `AppTelemetry` to the first field of
  `Telemetry`; retain `seq_no` and `value`.
- `ztc_request.fbs`: add first-field `id:string` to `Ack` and `Error`; rename
  `SnapshotComplete` to `EOS`; define `EOS` as only `id` and `seq_no`. Delete
  `count`: no repository consumer uses it and `EOS` itself terminates a snapshot.
- Retain `Error.seq_no` and `has_seq_no`; malformed input produces
  `Error{id, seqNo = 0, hasSeqNo = false, ...}` and processing continues.
- `ztc_msg.fbs`: replace the union member with `EOS`.
- Remove `AppTelemetry::id` from `ZtcAppTypes.hh`; renumber its `ZtcFB.hh`
  `Ctor<>` fields so `version` starts at zero.
- Require a present, nonempty publisher ID in `validTelemetry()` and in the
  `validMsg()` cases for `Ack`, `Error`, and `EOS`. Retain ordinary
  `Zfb::Verifier` validation; add no trust envelope or consumer identity.
- Rename `sendComplete_` and all generated/test uses to `sendEOS_`/`EOS`.

Guidelines: schema spelling retains FlatBuffers conventions, while C++ uses
camel case and worker-only trailing underscores
([Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes),
[Trailing underscores](GUIDELINES.md#trailing-underscores)). Removing obsolete
fields follows [Scope and cleanup](GUIDELINES.md#scope-and-cleanup). Using `Zfb`
instead of another validation layer follows [Metadata](GUIDELINES.md#metadata)
and Postel's Law in [No dogma](GUIDELINES.md#no-dogma).

## 3. Reduce `AppCf` and the public API

In `ZtcApp.hh`:

1. Initialize `AppCf::id` with `ZuID{ZiProgram::name()}`; reject only an empty ID
   in `App::init()`.
2. Delete `ip`, `port`, `nAccepts`, `rebindFreq`, their defaults/metadata,
   `Ztcp::Server<App>` inheritance and `Base`, `App::Link`, `localIP()`,
   `localPort()`, `nAccepts()`, `rebindFreq()`, `listening()`, `listenFailed()`,
   and `accepted()`.
3. Add only request-ring controls:

   - `DefltReqSize = 1U << 21`, `unsigned reqSize = DefltReqSize`;
   - `DefltReqTimeout = 1`, `unsigned reqTimeout = DefltReqTimeout` (seconds);
   - `bool reqLL = false`.

   Reflect them in `ZfStruct`. Require nonzero timeout and enough capacity for a
   maximum frame plus ring framing/alignment. Document the configurable 2 MiB
   default as margin over the 1 MiB maximum frame.
4. Correct `ZmRing_::ParamData::timeout`'s comment to seconds, matching Linux
   `Zm::now(timeout)` and Windows `timeout * 1000`.
5. Include `ZiProgram.hh` directly; remove `Ztcp.hh`. Keep transport headers in
   `ZtcApp.cc`. Add no diagnostic accessors or exposed transport objects.

Use `req*`, not `ring*`: these settings do not configure the shared telemetry
ring. Guidelines: `req` is prescribed, names stay below 28 bytes, public struct
fields need no accessors, and class members retain `m_`
([Length and abbreviations](GUIDELINES.md#length-and-abbreviations),
[Accessors](GUIDELINES.md#accessors),
[Class and struct shape](GUIDELINES.md#class-and-struct-shape)). Named,
documented capacity addresses [Storage and capacity](GUIDELINES.md#storage-and-capacity).
Direct includes and no shims follow [Module layout](GUIDELINES.md#module-layout)
and [General principles](GUIDELINES.md#general-principles).

## 4. Keep transport state small and explicitly owned

At file scope in `ZtcApp.cc` define:

```cpp
static unsigned msgSize(const void *ptr)
{
  auto hdr = static_cast<const Ztc::Hdr *>(ptr);
  return sizeof(Ztc::Hdr) + unsigned(hdr->length);
}

ZuDerive(Ring, (ZiRing<ZmRingSizeAxor<msgSize, ZmRingMW<true>>>));
```

The wire cast is necessary; add no casts among interoperable Z spans/strings.

Keep the concrete ring type and transport storage behind the existing opaque
`App_::State` boundary. Add to `App_::StateData`, grouped by owner:

- immutable-after-first-start `Zi::Name telName` and `Zi::Path pidName`;
- control-owned `ZuUnion<void, ZiPIDFile> pidFile` and `ZmThread reqThread`;
- control/request handoff state `Ring reqRing` and
  `ZmAtomic<unsigned> reqStop = 0`; control opens/closes it only while the
  request thread is stopped, and the request thread alone shifts records;
- worker-owned `Ring telRing` and `bool appPending = true`; all opens, writes,
  and closes of `telRing` run on the worker thread.

Delete TCP `clients`, `m_boundPort`, `m_linkGeneration`, and
`m_serverInitialized`. Do not heap-allocate `ZiPIDFile`, cache `reqName`, or add
`namesCached`: `m_cf.id` is the request name and non-null `pidName` is the
first-start sentinel. Construct/destroy the PID object in its union arm.

Order members as cached values, control state, then thread-exclusive groups,
largest alignment first. Do not cache-line-align the local `Ring` instances;
contention occurs in their shared-memory control/data mappings, not in the local
handles. Only `reqStop` and the existing `m_running` cross ownership atomically.
Add no locks or atomic telemetry counters. Use named private constants, with
tuning comments, for retained `ZiIOBufAlloc` pool counts. Consolidate deleted TCP
Rx/Tx allocators into the minimum request/message pools; keep a separate alert
pool only if its lifetime/tuning differs.

Guidelines: `msgSize` is a short file-static name; complex aliases use `ZuDerive`;
struct fields omit `m_` ([Naming](GUIDELINES.md#naming),
[Assertions and type mechanics](GUIDELINES.md#assertions-and-type-mechanics),
[Class and struct shape](GUIDELINES.md#class-and-struct-shape)). In-place PID
storage removes an uninstrumented allocation; pooled `ZiIOBuf` is the I/O-buffer
exception ([Heap allocation](GUIDELINES.md#heap-allocation),
[I/O and system integration](GUIDELINES.md#io-and-system-integration)). Layout and
alignment follow [Storage and capacity](GUIDELINES.md#storage-and-capacity) and
[I/O sharding](GUIDELINES.md#io-sharding).

## 5. Sample names once, without extra policy

On first `start()` (detected by null `pidName`) derive `telName` from
`getenv("ZTC_RING")`, defaulting to `"ztc"` only for sentinel null, and `pidName`
as `<getenv("ZTC_DIR") or "ztc">/<id>.pid`.

Pass `ZiFile::tmpDir()` and relative `pidName` directly to `ZiPIDFile::init()`.
Allow nested components; do not normalize or prevalidate `ZTC_DIR`.
`ZiPIDFile` owns that policy. Convert to `Zi::Path` only at the filesystem
boundary, preserving narrow ring/program names and Windows wide paths. Never
resample on restart.

Guidelines: the value sentinel replaces a parallel boolean
([Framework fit](GUIDELINES.md#framework-fit),
[Assertions and type mechanics](GUIDELINES.md#assertions-and-type-mechanics)).
Direct string conversion avoids casts ([Strings](GUIDELINES.md#strings),
[Type and API friction](GUIDELINES.md#type-and-api-friction)); `ZiPIDFile`/`ZiFile`
reuse follows [I/O and system integration](GUIDELINES.md#io-and-system-integration).

## 6. Make startup transactional

After starting the existing multiplex and alert store:

1. Initialize `reqRing` from `m_cf.id`, `reqSize`, `reqTimeout`, and `reqLL`.
2. Open write-only (which reaps dead readers), then `reset()`. A surviving reader
   makes reset fail with `Zu::NotReady`.
3. Close, reopen read-only, and `attach()` the sole publisher request reader.
4. Construct `pidFile` in place and call `init(tmpDir, pidName)`. PID publication
   is the last registry step, so every advertised publisher already has an open,
   attached request ring.
5. On `ZiPIDFile::Running`, and on every other failure including request-thread
   creation, destroy the active PID union arm if constructed, then unwind the
   ring/store/multiplex state in reverse order and complete `start(CtrlFn)` with
   `false`.
   Do not exit inside the library; caller policy decides whether primary service
   continues.
6. Clear `reqStop`, start named thread `ztcReq`, open alert ingress, set
   `m_running`, and report success.
7. Post a worker continuation that sets worker-owned `appPending = true` and
   attempts initial sequence-zero App publication through `publishRaw_()`.
   PID/ring registration must precede publishing; the control thread must not
   write `appPending` on restart. The continuation rechecks `m_running` so a
   concurrent `stop()` cannot publish afterward.

Missing shared telemetry ring/consumer is not a startup error. `start()` and
`stop()` remain idempotent. Guidelines: reverse-order unwind satisfies graceful
24x7 teardown ([General principles](GUIDELINES.md#general-principles)); callback
completion must not assume scheduler `run` is synchronous
([Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[Callbacks](GUIDELINES.md#callbacks)).

## 7. Isolate the request-reader thread

Implement `reqRun_()` as the only request-ring reader:

1. Non-LL mode calls blocking `shift()`; `reqTimeout` bounds stop observation.
   LL mode uses stop-aware `tryShift()` spinning. After a null return, inspect
   `readStatus()`: empty/timeout is normal, while EOF/I/O failure is terminal.
2. Compute `msgSize(ptr)` once, copy exactly that record into pooled `ZiIOBuf`,
   and always call `shift2(size)` immediately. On allocation failure, release and
   drop the record.
3. Validate owned input with `Ztc::msg()`. Malformed input posts a worker
   continuation for one best-effort attributed unsequenced `Error`, then continues.
4. Valid input posts only `ZmRef<ZiIOBuf>` to `m_cf.workerThread`; the continuation
   rechecks `m_running` and calls `request_()`.
5. Recheck `reqStop` before classifying a null shift or error. Requested stop
   exits cleanly; an unexpected exception/terminal ring error logs fatal and
   calls `Zm::exit(1)`.

This dedicated thread is required and does not block a mixed scheduler shard. It
touches no subscription/alert/output state. `ZiLOG` callbacks capture durable
values by copy only.

Guidelines: the sole shared-memory-to-pool copy transfers ownership; only a
pooled handle crosses threads ([Cross-Shard Handoffs](GUIDELINES.md#cross-shard-handoffs),
[I/O and system integration](GUIDELINES.md#io-and-system-integration)). Computing
size once and releasing every branch address [Code structure](GUIDELINES.md#code-structure)
and [Liveness](GUIDELINES.md#liveness). Logging follows
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).

## 8. Make publication one-shot, lossy, and worker-owned

Factor only:

- `publishRaw_(ZmRef<ZiIOBuf>)`: initialize `telRing` once with size-zero params
  using `telName`. If it is closed, attempt write-open; a failed open leaves it
  closed and retryable. Call `tryPush(length)` once, copy into the returned
  uninitialized reservation, commit with the MWMR form `push2(ptr, length)`, and
  return success. A null reservation returns `false` without calling `push2`.
- `publish_(ZmRef<ZiIOBuf>)`: if `appPending`, attempt `publishApp_(0)` once and
  clear the bit only on commit; then attempt the ordinary message exactly once,
  regardless of reset success. `publishApp_()` builds its frame and calls
  `publishRaw_()` directly; it must not recurse through `publish_()`.

Delete any `openTel_`, output queue, retry timer, delivery counter, reader
eviction, or degraded-on-drop behavior. Each response naturally retries opening
through `publish_`; no request-side reopen path is needed. Full/absent/no-reader/
slow-reader cases drop immediately and cannot affect primary work.

Every frame carries `m_cf.id`: in `Telemetry.id`, or directly in `Ack`, `Error`,
and `EOS`. Add no consumer ID or global ordering. Reserve request `seqNo == 0`
as the consumer-restart sentinel: on receipt, the publisher clears active
subscriptions/pending snapshots and does not run ordinary group dispatch. The
external consumer lazily attempts that reset request before every ordinary
request until it commits once. Because no production consumer exists here,
implement consumer sending only in the ring test fixture.

Retain the final buffer-to-ring copy: exact reservation size is known only after
FlatBuffer construction, and owned buffers support persistence/replay. Add no
intermediate contiguous copy.

Guidelines: `tryPush` favors latency and uses framework IPC
([General principles](GUIDELINES.md#general-principles),
[Concurrency and sharding](GUIDELINES.md#concurrency-and-sharding)). Direct writes
to reserved storage address [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).
Worker-only primitives/no locks follow [I/O sharding](GUIDELINES.md#io-sharding)
and [Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies).

## 9. Collapse connection state

Delete `ClientData`, `Client_`, `ClientIdx`, `client_()`, `clientRelease_()`,
`disconnected_()`, `snapshotCancel_()`, and link/generation state. Use only:

- `SubKey = ZuTuple<uint8_t, ZuCSpan>` for group/filter;
- `PendingKey = uint64_t` for request sequence;
- `Subscription_` and `Pending_` without client/generation/count.

Delete `PendingData::count`, `Subscription_::replayCount`, attempted-record
increments, and `sendBatch()` count plumbing. Remove `Link` from all request,
snapshot, subscription, replay, batch, and send signatures; route responses via
`publish_()`.

Preserve only transport-independent behavior: duplicate nonzero sequence
rejection, group/filter matching, interval clamping, replay bounds, dirty reruns,
batched continuations, alert persistence, and configured limits. Unsubscribe
removes the global group/filter subscription. A sequence-zero App telemetry
frame marks publisher restart and tells the consumer to discard that publisher's
subscription view; a sequence-zero request is the inverse consumer-restart
signal described in section 8.

Guidelines: deletion follows [Scope and cleanup](GUIDELINES.md#scope-and-cleanup).
Existing intrusive `ZmHash` indexes follow [Containers and intrusion](GUIDELINES.md#containers-and-intrusion).
Keep iterator scope narrow and snapshot batches bounded
([Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[Liveness](GUIDELINES.md#liveness)).

## 10. Centralize construction and teardown

- Make `sendTelemetry()` construct `Telemetry{id, seqNo, type, value}`.
- Factor `appTelemetry_(AppTelemetry &)` for both captured snapshots and
  `publishApp_(seqNo)`; batched snapshots retain captured snapshot-time values.
- Persist canonical alerts with publisher ID. Load/replay accepts only frames
  whose ID matches the current `m_cf.id`, then mutates only `seqNo`.
- Treat old or mismatched-ID persisted frames as incompatible; add no
  migration/ID injection.
- Construct `EOS{id, seqNo}`, `Ack`, and `Error` through one path per type.

`stop()` first destroys the active `ZiPIDFile` union arm (its destructor removes
the registry entry), then sets `reqStop` and joins from control/main context,
detaches/closes `reqRing`, and posts/barriers worker cleanup including timer
cancellation and closing only this publisher's `telRing` handle. Stop the
multiplex and alert store afterward. Never reset/unlink the shared telemetry
ring. `final()` is idempotent. Callbacks do not retain an owning `App` reference;
drain them before destruction.

Guidelines: shared constructors eliminate repeated blocks
([Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)); `Zfb`
remains the serializer ([Metadata](GUIDELINES.md#metadata)). Shutdown follows
[Timer Teardown](GUIDELINES.md#timer-teardown), [Teardown](GUIDELINES.md#teardown),
and [Lifetime](GUIDELINES.md#lifetime).

## 11. Propagate deletion and update tests

- Remove `ztcp` includes and `libZtcp.la` after the last `Ztcp` use is gone.
- Replace socket code in `ZtcTestClient.hh`, `ZtcAppTest`, and `ZtcAlertTest` with
  one ring fixture. It creates/resets/attaches the telemetry ring once before
  requests; lazily sends its sequence-zero consumer-reset request; discovers the
  PID file; size-zero opens the request ring; uses the MWMR
  `tryPush(size)`/`push2(ptr, size)` pair; and reads boundedly.
- Remove POSIX guards/includes used only by sockets.
- Repository-wide searches for deleted TCP API, `SnapshotComplete`, old
  `CreateTelemetry`, and `AppTelemetry::id` must return no stale references.

Guidelines: shared TAP fixture and Z synchronization avoid duplication and flaky
timing ([Tests](GUIDELINES.md#tests),
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)). Total
deletion follows compatibility-as-non-goal
([General principles](GUIDELINES.md#general-principles)).

## 12. Explicit audit-flag review

This checklist is normative and covers every subsection of
[Audit flags](GUIDELINES.md#audit-flags).

| Area | Required disposition |
|---|---|
| Code structure | `msgSize` stays outside templates; cache record size/loop bounds and repeated indexed values. |
| Storage/capacity | Named configurable ring/pool capacities with rationale; no fixed arrays, separately maintained lengths, fixed lookup tables, workload-unaware arrays, or new GC scan; order `StateData` without pointless local-ring alignment; no intrusive ref-counted values. |
| Heap allocation | PID in place; cross-thread frames use identified pooled `ZiIOBuf`; no raw `new`, untagged/scratch heap, or separate object/refcount/node allocations; consolidate like pools/nodes. |
| Data movement/initialization | The transport adds only the two ownership-required copies in §§7–8; write into uninitialized reservations; no temporary contiguous conversion or initialize-before-overwrite churn; log captures by value. |
| Algorithm/control flow | One raw publisher/wrapper; `switch` for union dispatch; flat, early-exit control flow; scoped iterators; no needless member-container aliases, redundant iterator-value null checks, pre-iteration count snapshots, or avoidable work; no synchronous assumption after scheduler posts; dedicated-thread blocking is the stated exception; tests synchronize without sleeps. |
| Framework fit | Use `ZiRing`, `ZiPIDFile`, `ZiIOBuf`, `ZiLog`, `Zf`/`Zfb`, `ZmThread`, `ZmScheduler`; no `FILE`, `syslog`, `printf`, STL transport, hand hash, or duplicate GC/open logic. |
| Type/API friction | Only the `Hdr` wire cast; direct Z string/span conversion; no char-equivalent, CRTP-base, or enum-mapping casts. |
| Scope/cleanup | Delete TCP/client/count compatibility code and stale dependencies; callbacks do not ref-count `App`; drain all owned work. |
| Atomics/locks/copies | Only `reqStop` and existing cross-thread `m_running` control are atomic; `appPending` and diagnostics are worker primitives; no delivery counters/locks. |
| Liveness | Blocking/spinning owns a dedicated thread; snapshot/replay batches repost continuations; no long mixed-worker loop. |
| Language/style | No concepts/`requires`, anonymous namespace, broad lambda capture, scoped enum, or STL substitute; use `Zu` casts/moves and hard-tab local layout per the applicable Core language and Code style sections. |

## 13. Acceptance tests

Focused TAP tests must prove:

- ID default/override/empty rejection and its ring/PID/header use;
- `ZTC_DIR`/`ZTC_RING` null defaults, nested directories, and sample-once behavior;
- `reqSize`, seconds-based `reqTimeout`, and `reqLL` map to `ZiRingParams`;
- ring reset/attach precedes PID publication; stale/dead state is reclaimed; live
  reader or PID collision makes `start()` return false;
- publishers start without consumer/ring and create no telemetry-ring artifacts;
- later output retries open and attempts pending App reset before ordinary output;
- sequence-zero request resets publisher-side subscriptions without ordinary
  dispatch; sequence-zero App telemetry resets consumer-side subscription state;
- publisher ID validation, no `AppTelemetry.id`, and `EOS` without count;
- snapshot/subscription/unsubscribe/replay/persistence semantics after deleting
  connection state;
- full/no-reader/stalled-reader cases drop immediately; a keeping-up passive
  monitor observes broadcast records;
- malformed input emits unsequenced attributed Error and continues; unexpected
  reader failure terminates a subprocess deterministically;
- stop removes PID, joins within timeout, drains worker state, and restarts using
  cached names; old or mismatched-ID persisted alerts fail without migration.

Use PID/nonce names, scoped environment restoration, reader detach-before-close,
and existing residue cleanup. Use `ZmBlock`/`ZmSemaphore`, never sleeps/polling
([Tests](GUIDELINES.md#tests), [Acceptance](GUIDELINES.md#acceptance)).

## 14. Build and verification

Keep the existing Clang debug build configuration. Do not run `z.config`,
`configure`, `autoreconf`, `make clean`, or otherwise reconfigure the tree.

Do not rebuild after individual edits or module milestones. Complete the schema,
`zm`, `zi`, `zv`, test, and build-file changes first, using source inspection and
stale-reference searches during implementation. Defer all dependent rebuilding
to this final sequence:

1. Run `git diff --check` and the repository-wide stale-reference searches from
   section 11. Resolve these before invoking the compiler.
2. Run one top-level `make -j8`. This is the first rebuild and lets the existing
   dependency order regenerate FlatBuffers headers and rebuild `zm`, `zi`, `zv`,
   and all dependents consistently under the current Clang debug configuration.
3. Run the focused existing `ZiRingTest`, `ZiRingTest2`, `ZtcSchemaTest`,
   `ZtcAppTest`, and `ZtcAlertTest` binaries from the completed build. Run a
   `ZmRing*` binary only if implementation changes extend beyond the documented
   timeout comment into `ZmRing` behavior.
4. Run top-level `make test` once after the focused tests pass.
5. Use `libtool exec` for any required debugger or Valgrind diagnosis. Do not
   switch build type or toolchain as part of this implementation; ASan/LSan,
   release, and MinGW rebuilds are outside this pass unless separately requested.

This follows [Build configuration](GUIDELINES.md#build-configuration),
[Acceptance](GUIDELINES.md#acceptance), and [Debugging](GUIDELINES.md#debugging).

## Deleted design elements

- Public `gc()` or explicit second GC pass.
- A `ZiRingParams` creation flag.
- Heap-owned `ZiPIDFile`, cached request name, or `namesCached` boolean.
- `openTel_`, reconnect state, retry queue/timer, delivery counters, or
  degraded-on-drop behavior.
- TCP compatibility API/state/dependency and client identity.
- `EOS.count`, count-only state, consumer identity, global ordering.
- Validation beyond nonempty IDs and built-in `Zfb`/`ZiPIDFile` checks.
- Production consumer code absent from this repository and persisted-alert
  migration.

There are no remaining implementation questions in the stated scope.
