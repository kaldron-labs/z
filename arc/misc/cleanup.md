## Zquic ZmRingFn Heap Allocation Reduction

The tuned `zhttp-caddy/h3/j2n100000` memdiag run shows `ZmRingFn`
heap allocation dominated by:

- `postAckSnapshot_` Tx posts
- `processAckFrameTx_()` rearming the loss timer
- `schedulePTO_()` rearming the PTO timer

These allocations do not primarily indicate undersized scheduler rings.  The
timer cases allocate because each arm stores a freshly captured lambda in a
`ZmScheduler::Timer`.  The ACK snapshot case allocates because Rx posts a
captured Tx continuation containing `AckSnapshot` state.

### Scheduler Timer Baseline

`ZmScheduler` now uses persistent timer callbacks by default.
`ZmScheduler::add(..., armFn, ...)` is called to schedule or reschedule a timer.
It might call `armFn` to install a new callback when the timer has no incumbent
callback.  Otherwise it reuses the stored callback and only updates `sid`,
`timeout`, and schedule position.

Use the new timer API consistently:

- call `add(timer, timeout, mode, armFn, sid)` to schedule or reschedule the
  timer
- `add` might call `armFn` to arm the timer callback when the incumbent
  callback is null
- the incumbent callback is null when it has never been set, or after it has
  been moved into a scheduler ring for execution
- `cancel` removes the timer from the schedule without clearing the incumbent
  callback, so a later `add` can reuse it
- `del` cancels and disarms the timer by clearing the callback
- use `del` only during final teardown

Then convert QUIC connection timers from generic per-arm lambdas into fixed
timer actions:

- ACK delay -> `ackDelay_()`
- loss -> `lossTime_()`
- PTO -> `pto_()`
- idle -> `idle_()`
- close -> close handling
- key discard -> key discard handling
- PMTUD -> PMTUD handling
- path validation -> path validation handling

This should remove the repeated `ZmRingFn` heap allocation from loss and PTO
timer arms.

### Typed Connection Timer Dispatch

Replace the generic templated `scheduleCxnTimer_(..., Fn fn)` with a typed
timer action, for example:

```cpp
enum class CxnTimer {
  AckDelay,
  Loss,
  PTO,
  Idle,
  Close,
  KeyDiscard,
  PMTUD,
  Path
};
```

The persistent scheduler callback should dispatch by timer identity on the Tx
thread.  The hot path should arm an existing timer action instead of
constructing a new capturing callable.

### Avoid Redundant Loss/PTO Rearms

`processAckFrameTx_()` currently ends by calling both:

```cpp
schedulePTO_();
scheduleLossTimer_();
```

Keep the functional behavior, but avoid scheduler churn when the effective
timer state did not change.  Track the currently armed deadline and relevant
timer identity, for example:

- `m_lossTimerOut`
- `m_ptoTimerOut`
- `m_ptoTimerLevel`

Only rearm when the deadline changes, the PTO level changes, or the timer was
inactive.  Normalize deadlines after the `out <= now` granularity adjustment so
minor now-based differences do not create needless churn.

### Coalesce ACK Snapshot Posts

`postAckSnapshot_()` should not post one Tx continuation for every ACK-eliciting
packet when a Tx-side ACK snapshot post is already outstanding.

Target behavior:

- at most one outstanding ACK snapshot post per packet space
- later Rx ACK updates refresh the pending snapshot instead of posting another
  Tx continuation
- Tx consumes the latest pending snapshot
- Rx clears the outstanding-post state only after Tx consumption is visible

Use a fixed mailbox or small Rx-to-Tx queue owned by the appropriate shard.  Do
not let Tx read mutable Rx-owned ACK state directly.

Also shrink any remaining posted ACK callable.  Prefer posting only stable
metadata such as link, packet space, and address, with the `AckSnapshot` stored
in Link-owned cross-thread state.  Avoid capturing a full `AckSnapshot` in every
scheduler lambda.

### Expected Result

The loss/PTO allocation classes should mostly disappear once QUIC uses
persistent timer actions and redundant rearms are suppressed.

The `postAckSnapshot_` allocation class should fall to the number of actual
coalesced ACK snapshot transfers rather than scaling with every received
ACK-eliciting packet.

These changes should reduce `ZmScheduler.Fn` heap churn, scheduler lock
traffic, and Tx wakeups without increasing scheduler ring sizes.
