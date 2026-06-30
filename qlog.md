`ZquicLOG` call-site guidelines
===============================

Purpose
-------

`ZquicLOG` is the required source marker for qlog-only runtime instrumentation.
It should make qlog code obvious during review and compile qlog-only work out
of release builds.

Use the same split as `ZiLog` / `ZiLOG`:

- `ZquicLOG(...)` is the macro used at call sites.
- `ZquicLog(...)` is the function used by the macro in debug builds.
- `ZquicLogger` or an equivalent internal object owns the qlog writer thread,
  ring, diagnostics, JSON helpers, and file output.

Release-build rule
------------------

No qlog-only expression may be evaluated in release builds.

This includes:

- reading runtime counters only used for qlog;
- reading congestion, recovery, path, CID, stream, TLS, or packet metadata only
  used for qlog;
- converting enums or times into qlog strings/units;
- building qlog event structs;
- counting qlog frames or ACK ranges;
- copying qlog-only bounded strings or arrays;
- calling qlog JSON helper functions.

Put all of that inside the `ZquicLOG` argument, usually in lambda capture
initializers.

Correct pattern:

```c++
ZquicLOG([
  level,
  pn,
  bytes,
  bytesInFlight = m_congestion.bytesInFlight(),
  latestRTT = m_rtt.latest()
](auto &o, ZuTime time) mutable {
  ZquicLogPacketEvent event{
    .packetSpace = qlogLevel(level),
    .packetNumber = pn,
    .packetSize = bytes,
    .bytesInFlight = bytesInFlight,
    .latestRTTUS = uint64_t(latestRTT.microsecs())
  };
  o.logPacketSent(event, time);
});
```

Incorrect pattern:

```c++
uint64_t bytesInFlight = m_congestion.bytesInFlight();
ZquicLogPacketEvent event;
qlogSet(event.packetSpace, qlogLevel(level));
ZquicLOG([event, bytesInFlight](auto &o, ZuTime time) mutable {
  o.logPacketSent(event, time);
});
```

The incorrect form leaves qlog-only reads and event construction in the hot
path even when qlog compiles out.

Capture rules
-------------

Capture by value only.  The lambda is moved through the inter-thread ring and
executes later on the qlog thread.

Allowed captures:

- scalar IDs, packet numbers, sizes, timestamps, enum values, flags, counters;
- small fixed-size or bounded qlog snapshots;
- Z value types whose copy cost and lifetime are explicitly acceptable;
- qlog frame summaries after they have been reduced to bounded metadata.

Do not capture:

- references;
- raw packet or stream payload buffers;
- mutable runtime state;
- `this`, `impl()`, `app()`, stream objects, path objects, congestion objects,
  or crypto objects;
- secrets, plaintext, key material, tokens, or unbounded application data.

If a value needs to come from runtime state, read it inside a capture
initializer:

```c++
ZquicLOG([
  cwnd = m_congestion.cwnd(),
  ssthresh = m_congestion.ssthresh(),
  bytesInFlight = m_congestion.bytesInFlight()
](auto &o, ZuTime time) {
  ...
});
```

Threading model
---------------

The hot-path thread should only:

- evaluate capture initializers in debug builds;
- move the lambda by value into the qlog ring;
- update qlog enqueue/drop diagnostics.

The qlog thread should do as much as possible:

- construct qlog-specific structs from captured values;
- normalize enum names and qlog field names;
- use `ZtJSON` / `ZtStruct` mappings;
- write JSON-SEQ records;
- perform bounded formatting.

Do not format JSON, allocate scratch JSON strings, or walk runtime-owned
containers on Rx/Tx I/O threads.

Helper placement
----------------

Helpers are encouraged, but they must preserve the compile-out rule.

Good helpers:

- logger-thread helpers called from inside the `ZquicLOG` lambda;
- pure qlog conversion helpers used only inside `ZquicLOG`;
- bounded event writers that take already-captured scalar metadata;
- JSON serialization helpers using `ZtJSON` / `ZtStruct`.

Bad helpers:

- hot-path wrappers such as `qlogFoo_()` that hide qlog work behind a normal
  function call;
- helpers that read runtime state before entering `ZquicLOG`;
- helpers that build qlog event structs before entering `ZquicLOG`;
- helpers that use `ZquicLogger::enabled()` to guard qlog work in normal code.

`ZquicLogger::enabled()` is acceptable for lifecycle checks and tests, but it
is not the hot-path instrumentation pattern.  Hot-path instrumentation should
use `ZquicLOG`.

Event construction
------------------

Build qlog event structs inside the lambda body from captured values:

```c++
ZquicLOG([
  action,
  reason,
  mtu = m_path.activeMaxUDP(),
  antiAmplification = m_path.antiAmplificationRemaining(),
  validated = pathValidated_()
](auto &o, ZuTime time) {
  ZquicLogPathEvent event{
    .action = action,
    .reason = reason,
    .mtu = mtu,
    .antiAmplification = antiAmplification,
    .validated = validated
  };
  o.logPathUpdated(event, time);
});
```

Qlog event types should be plain data structs with fields that can be
initialized directly from captured values.  Prefer aggregate/member
initialization over default construction followed by assignment:

```c++
ZquicLogPathEvent event{
  .action = action,
  .reason = reason,
  .mtu = mtu,
  .validated = validated
};
```

Avoid this pattern unless there is a real dependency between assignments:

```c++
ZquicLogPathEvent event;
event.action = action;
event.reason = reason;
event.mtu = mtu;
event.validated = validated;
```

Default construction can needlessly zero-initialize or default-construct fields
that are immediately overwritten.  Keep the data structs simple enough that
call sites and writer helpers can initialize only the emitted fields.

For packet/frame summaries, reduce payload data to bounded metadata only:

- frame type;
- stream id, offset, length, FIN;
- ACK largest/ranges/counts/delay;
- crypto offset and length;
- close error code and bounded reason length;
- CID sequence/length/reset-token presence;
- path challenge/response presence and length, not challenge bytes.

Do not log payload bytes or key material.

Control flow
------------

Do not add qlog-specific branches outside `ZquicLOG` unless they also affect
normal protocol behavior.

If an event should be skipped because it is empty, put the skip inside the
macro when the skip is qlog-only:

```c++
ZquicLOG([
  ackedBytes = update.ackdBytes,
  ...
](auto &o, ZuTime time) {
  if (!ackedBytes) return;
  ...
});
```

If the branch is protocol logic already needed without qlog, keep it outside:

```c++
if (!update.ackdBytes) return;   // protocol/recovery path already needs this
ZquicLOG([...](auto &o, ZuTime time) { ... });
```

Review checklist
----------------

For every qlog call site, verify:

- the visible marker is `ZquicLOG`;
- qlog-only reads and temporaries are inside capture initializers;
- qlog-only conversions and struct construction are inside the lambda body;
- qlog event structs use direct aggregate/member initialization where possible;
- captures are by value and bounded;
- no references, runtime owner pointers, packet payloads, or secrets escape to
  the qlog thread;
- JSON output uses `ZtJSON` / `ZtStruct`;
- release builds erase the qlog-only code at preprocessing time;
- the hot path does not do qlog formatting or JSON serialization.
