# Plan: use `ZmScheduler` in `Ztc::App`

## Objective

Replace the privately owned `ZiMultiplex` in `Ztc::App` with a privately owned
`ZmScheduler`. The publisher needs queued work and timers; its transport is
shared memory and does not need network Rx/Tx threads or polling resources.

Keep the publisher's independent lifecycle and the existing ownership model:
one worker owns subscriptions, serialization, publication and alert storage;
a separate timer destination schedules sampling; the request-reader thread
reads the request ring and posts work to the worker. Sharing an application's
scheduler or merging the timer and worker is outside this change.

This is an implementation plan only.

## Existing implementation

[`ZtcApp.hh`](ztc/src/ZtcApp.hh) embeds
`mutable ZuUnion<void, ZiMultiplex> m_mx` and exposes it internally through
`serviceMx_()`. `AppCf` contains `ZvMxCf mx`, timer/worker role names, and four
distinct thread indices. Defaults reserve four scheduler slots: timer 1,
Rx 2, Tx 3 and worker 4. The blocking request reader is separate.

[`ZtcApp.cc`](ztc/src/ZtcApp.cc) constructs `ZiMxParams` and starts/stops the
multiplex. Publisher operations use `run`, `add`, `del`, `running`, and
`invoked_`, all available through `ZmScheduler`. Network settings are configured
but the publisher does not open a network transport.

The publisher also observes external multiplexes through `Ztc::MxMgr`.
`mxAdded_`, `mxDeleted_` and `unwatch_` exclude the publisher's own multiplex
from per-multiplex watches. Those exclusions become obsolete once the owned
execution object is no longer a multiplex.

## 1. Replace publisher configuration

Introduce a small publisher scheduler configuration type, `AppSchedCf`, using
the existing `ZfStruct` conventions. Replace `AppCf::mx` with
`AppCf::scheduler`. There is currently no scheduler-only equivalent of
`ZvMxCf` in `zv`; avoid introducing a broader shared configuration framework
for this one consumer.

Preserve applicable scheduler tuning:

- `nThreads`, default 2.
- `stackSize`, `priority`, `partition` and `quantum`.
- `queueSize`, `ll`, `spin` and `timeout`.

Retain `timerRole` and `workerRole`, and retain their explicit thread indices
with defaults `timerThread = 1` and `workerThread = 2`. Both roles remain
isolated scheduler threads. Permit extra configured slots and alternate valid
indices, rather than hard-coding an unconfigurable thread topology.

Remove publisher `rxThread` and `txThread`, Rx/Tx role names, receive/send
buffer sizing, epoll tuning and multiplex debug/trace/fragmentation settings.
Remove their validation and conditional compilation branches as well.

Validate that both selected indices are nonzero, within `nThreads`, and
different, and that the role names are nonempty and distinct. Preserve
applicable capacity/range checks. Keep direct C++ configuration and parsed
configuration defaults consistent, including the zero/unset conventions for
optional tuning. Preserve the existing units and conversions for `quantum`
and `timeout` when constructing `ZmSchedParams`.

Intended configuration shape:

```text
scheduler: {nThreads: 2},
timerRole: "timer", workerRole: "worker",
timerThread: 1, workerThread: 2
```

This is a deliberate configuration/API change. Update dependent code and
configuration; do not add an `mx` alias, compatibility accessor or duplicate
Rx/Tx settings. Check the parser's handling of unknown fields and ensure
repository examples and documentation do not suggest removed settings still
have an effect.

## 2. Replace storage and scheduling calls

Include `ZmScheduler.hh` directly and remove `ZiMultiplex.hh` and
`ZvMxParams.hh` as dependencies for the owned execution object. Keep direct
includes still needed for observing multiplexes and for other publisher
facilities; this does not remove all Zi/Zv dependencies from the Ztc module.

Use `mutable ZuUnion<void, ZmScheduler> m_scheduler` and an internal
`scheduler_()` accessor. Preserve in-place construction and delayed resource
initialization, without an additional heap allocation for the scheduler.

Construct `ZmSchedParams` directly, using the publisher ID and scheduler
tuning. Configure only the timer and worker roles. Replace the multiplex
construction flag and exception cleanup with scheduler equivalents, including
the union state when construction throws.

Migrate all execution uses of `serviceMx_()`, renaming it to `scheduler_()`:

- Alert ingress, asynchronous RAG reads/writes and startup publication.
- Request-reader dispatch, including invalid-request responses.
- Periodic subscription timers and their worker handoff.
- Snapshot batching and continuations.
- Alert replay batches.
- Lifecycle checks, shutdown barriers and pending-work drain.

Rename execution-related local variables to `scheduler` or `sched`. Keep
`mx` names for actual observed multiplexes and their telemetry indices;
do not mechanically rename `m_mxIdx` or `m_mxQueueIdx`.

## 3. Preserve lifecycle and thread ownership

Retain the existing `init`, synchronous/asynchronous `start`, `stop` and
`final` interfaces. The scheduler remains owned by `Ztc::App` and starts and
stops with it, independent of any observed application or database state.

Verify these boundaries explicitly:

1. `init` validates configuration, claims the publisher, constructs state and
   scheduler, warms indices and installs watchers. Partial failure releases
   every successfully acquired resource.
2. `start` starts the scheduler, alert storage, request ring, PID registration
   and request reader, then opens ingress and queues initial publication.
   Failure unwinds through the existing synchronous-start cleanup contract.
3. `stop` closes admission, stops/joins the request reader, closes alert
   ingress, cancels subscriptions/timers and drains work while the scheduler
   is running. Preserve the worker/timer/worker barrier ordering and explain
   any adjustment required by `ZmScheduler` semantics.
4. Stop the scheduler only after pending publisher callbacks are drained.
   Preserve alert-store closure, shared-ring closure, registration removal
   and best-effort shutdown publication.
5. `final` stops safely, removes watches, clears indices and destroys the
   scheduler/state, releasing the publisher claim.

Preserve the timer-generation check so a cancelled or replaced timer cannot
resurrect work during teardown. Preserve worker ownership of mutable
subscriptions and buffers; do not add locks to make non-owner access legal.

Blocking lifecycle calls must reject invocation from an owned scheduler
thread that would deadlock a stop or drain. Review all configured slots,
including optional extra slots, rather than retaining checks for the deleted
Rx/Tx indices. Keep cleanup idempotent for repeated `start`/`stop`, explicit
`final`, destruction and failed startup.

## 4. Preserve observation of application multiplexes

Remove comparisons between observed multiplex pointers and `serviceMx_()`
in `mxAdded_`, `mxDeleted_` and `unwatch_`. Every multiplex in `MxMgr` is now
external to the publisher's execution scheduler and should follow the normal
connection/queue watch path.

Retain all application multiplex, connection, queue, hub and database
telemetry indices and snapshots. Do not remove `Group::Mx` support merely
because the publisher itself no longer owns a multiplex.

The publisher's former self multiplex row disappears from multiplex
telemetry. Its execution threads remain eligible for normal thread telemetry.
`ZmScheduler` implements `Ztc::QueueMgr`, but the current publisher queue
discovery includes multiplex/hub ownership paths: audit and document the
effect on self queue visibility without promising a new scheduler telemetry
schema. Adding general scheduler discovery is separate work unless needed
to preserve an explicitly required contract.

## 5. Update consumers and documentation

Search all `Ztc::AppCf` construction and metadata use, including
[`ZtcAppTest.cc`](ztc/test/ZtcAppTest.cc),
[`ZtcAlertTest.cc`](ztc/test/ZtcAlertTest.cc), and the publisher mode in
[`ztchubwiretest.cc`](ztc/itest/ztchubwiretest.cc). Update configurations and
tests that depend on old thread numbers, multiplex settings or self telemetry.

Review build inputs for newly direct dependencies and remove only those
includes/link dependencies actually made obsolete. Agent and hub networking
continues to require multiplexes.

Update the [collector README](ztc/README.md) with the scheduler configuration
and execution roles. Update the [zumd publishing plan](zumd.md) to reference
the scheduler-based publisher; its requirement for process-lifetime publishing
independent of Zdb activation is unchanged. Account for any intervening
implementation of that plan when updating consumers.

## 6. Validation

Extend existing TAP tests with behavior-focused cases:

- Default and parsed configurations create valid timer/worker roles; reject
  duplicate/out-of-range indices, invalid names and invalid scheduler tuning.
- Remapped timer/worker indices work, proving no hidden dependency on slots
  1/4 or the former Rx/Tx slots.
- A publisher adds no `MxMgr` multiplex entry or network polling resources.
  Verify removal of the two Rx/Tx roles without assuming the scheduler has
  no internal timer-management thread.
- Create a real application multiplex and verify its multiplex, connection
  and queue telemetry before and after dynamic registration/removal. This
  catches regressions from deleting the self-watch exclusions.
- Preserve snapshots, periodic subscriptions, unsubscribe, alert replay,
  request handling, ring attachment and shutdown behavior.
- Exercise stopping with an armed timer, queued snapshot/replay work and
  concurrent alert ingress. Assert completion without callbacks after final,
  leaked workers or retained publisher registration.
- Cover repeated lifecycle operations, failed startup followed by cleanup,
  and blocking lifecycle calls from owned threads.

Use existing `ZuTest`/`ZuTestUtil` and isolated ring/registry/alert fixtures.
Build with `make -j8`, then run `make -C ztc/test test`. Run the persistent
hub/collector integration fixture with an available Zdb backend to exercise
the real publisher-to-agent path. Build default targets before test targets.
Use the configured sanitizer build for lifetime checks and `libtool exec`
for any Memcheck/debugger runs; do not run `.libs` executables directly.

Completion means `Ztc::App` owns only a scheduler for execution, has no
publisher Rx/Tx configuration or self-multiplex special cases, and retains
working publication, external telemetry observation and deterministic cleanup.
