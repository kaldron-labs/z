# zquic supplementary guidelines

These guidelines are specific to the QUIC implementation in `zquic`.  They
supplement the repo-wide rules in `../GUIDELINES.md`; follow both when editing
`zquic`.

# General zquic

- Keep QUIC runtime work on the owning Rx or Tx thread.  Do not add locks to
  make non-owner access convenient.
- Keep protocol state changes close to the RFC mechanism they implement.
  Cross-thread entry points should snapshot only destination-owned metadata and
  immediately post to the owning thread.
- Use existing `Zquic` protocol enums, fixed-size protocol values, Z socket
  address types, and module-local helpers before adding ad hoc string or STL
  representations.
- Debug text logging is unrelated to qlog and should use `ZiLOG(Debug, ...)`.

# qlog

## Purpose

`ZquicLOG` is the required source marker for qlog-only runtime instrumentation.
It should make qlog code obvious during review and compile qlog-only work out
of release builds.

Use the same split as `ZiLog` / `ZiLOG`:

- `ZquicLOG(...)` is the macro used at call sites.
- `ZquicLog(...)` is the function used by the macro in debug builds.
- `ZquicLogger` or an equivalent internal object owns the qlog writer thread,
  ring, diagnostics, JSON helpers, and file output.

Qlog file output should follow the `ZiLog` file-sink precedent: when a qlog
path already exists, age it through the configured archive depth before opening
the new log file.  Automated tests may use `ZQUIC_TEST_KEEP` to preserve their
temporary qlog files for external tooling checks; this is a test artifact
retention switch, not a generic library/runtime qlog control.

Do not add `Zquic_LOG*`, `Zquic_DEBUG_LOG*`, or extra `ZquicLOG*` macros.
Debug text logging is unrelated to qlog and should use `ZiLOG(Debug, ...)`.

## Release-build rule

No qlog-only expression may be evaluated in release builds.
No qlog-only expression may be evaluated in debug builds when qlog is disabled
by configuration either.  `ZquicLOG` must check the runtime enabled flag before
evaluating its argument, just as `ZiLOG` filters severity before posting to the
logger thread.

`Zquic_DEBUG` is the controlling conditional compile for qlog code.  Release
build verification must use `z.config`, then top-level `make clean`, then
top-level `make -j8`; do not validate release behavior with partial
subdirectory rebuilds.

`ZquicLogger` is a `ZmSingleton` process-wide singleton.  The owning app
initializes it under main-thread control, early and once, from qlog parameters
during application `init`; it finalizes the logger after protocol app machinery
is drained and all activity has ceased during application `final`.  After that
initialization, `ZquicLogger::enabled()` is the single runtime availability
check for qlog.  Call sites and helpers should not separately consult the
owning app's qlog params to decide whether qlog is active.

This includes:

- reading runtime counters only used for qlog;
- reading congestion, recovery, path, CID, stream, TLS, or packet metadata only
  used for qlog;
- converting enums or times into qlog strings/units;
- building qlog event structs;
- counting qlog frames or ACK ranges;
- copying qlog-only strings, owned `ZeString` values, or arrays;
- calling qlog JSON helper functions.

Put all of that inside the `ZquicLOG` argument, usually in lambda capture
initializers.

Correct pattern:

```c++
ZquicLOG(([
  level,
  pn,
  bytes,
  bytesInFlight = m_congestion.bytesInFlight(),
  latestRTT = m_rtt.latest()
](auto &o, ZuTime time) mutable {
  ZquicLogPacketEvent event{
    .packetSpace = level,
    .packetNumber = pn,
    .packetSize = bytes,
    .bytesInFlight = bytesInFlight,
    .latestRTTUS = uint64_t(latestRTT.microsecs())
  };
  o.logPacketSent(event, time);
}));
```

Incorrect pattern:

```c++
uint64_t bytesInFlight = m_congestion.bytesInFlight();
ZquicLogPacketEvent event;
event.packetSpace = level;
ZquicLOG(([event, bytesInFlight](auto &o, ZuTime time) mutable {
  o.logPacketSent(event, time);
}));
```

The incorrect form leaves qlog-only reads and event construction in the hot
path even when qlog compiles out.

## Capture rules

Capture by value only.  The lambda is moved through the inter-thread ring and
executes later on the qlog thread.

Allowed captures:

- scalar IDs, packet numbers, sizes, timestamps, enum values, flags, counters;
- small fixed-size or bounded qlog snapshots;
- Z value types whose copy cost and lifetime are explicitly acceptable;
- qlog frame summaries after they have been reduced to bounded metadata;
- `ZeString` values only for genuinely arbitrary string data that must cross
  the async boundary.

Do not capture:

- references;
- raw packet or stream payload buffers;
- mutable runtime state;
- `this`, `impl()`, `app()`, stream objects, path objects, congestion objects,
  or crypto objects;
- secrets, plaintext, key material, tokens, or unbounded application data.

Most qlog fields are not arbitrary strings.  QUIC protocol data is normally
fixed-size scalar data, IP address/port data, protocol IDs, error codes,
enumerations, or other closed vocabularies.  Short string literals used in the
code for qlog reasons, triggers, actions, states, packet labels, or frame
labels should be modeled as enum values, aligning with system error-code style
classification.  Do not capture those literals as strings.

Do not capture `ZuCSpan` for arbitrary string data unless it points at static
storage that will outlive the logger thread.  If the data is genuinely
arbitrary or detailed at run time, capture it as an owning `ZeString` and move
it into the event on the qlog thread:

```c++
ZquicLOG(([
  foo = ZeString{fooSpan},
  bar = int64_t(value)
](auto &o, ZuTime time) mutable {
  QLogBaz event{
    .foo = ZuMv(foo),
    .bar = bar
  };
  o.logBaz(ZuMv(event), time);
}));
```

If the value is an enumeration or closed vocabulary, keep it as an enum and use
a `ZtEnumMap` with `ZtStruct` / `ZtJSON`.  The JSON string conversion belongs
on the logger thread, during `ZtJSON` serialization:

```c++
ZtEnum(TheEnum, int8_t, Value0, Value1);
ZtEnumMap(TheEnum, JSON, "value_0", "value_1");
ZtStruct((TheStruct, JSON),
  (((enum_), (Ctor<...>, Enum<TheEnum::JSON>)), (Int8)));
```

Do not translate enums to strings in helper functions just to feed those
strings back into `ZtJSON`.

If a value needs to come from runtime state, read it inside a capture
initializer:

```c++
ZquicLOG(([
  cwnd = m_congestion.cwnd(),
  ssthresh = m_congestion.ssthresh(),
  bytesInFlight = m_congestion.bytesInFlight()
](auto &o, ZuTime time) {
  ...
}));
```

## Threading model

The hot-path thread should only:

- evaluate capture initializers in debug builds after `ZquicLOG` has confirmed
  qlog is enabled;
- move the lambda by value into the qlog ring;
- update qlog enqueue/drop diagnostics.

The qlog thread should do as much as possible:

- construct qlog-specific structs from captured values;
- map enum names through `ZtEnumMap` and qlog field names through `ZtStruct`
  metadata;
- use `ZtJSON` / `ZtStruct` mappings;
- write JSON-SEQ records;
- perform bounded formatting.

Do not format JSON, allocate scratch JSON strings, or walk runtime-owned
containers on Rx/Tx I/O threads.

## Helper placement

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

Use `ZquicLogger::enabled()` only for qlog state that must be accumulated
synchronously on the protocol thread before a later `ZquicLOG` writer-thread
post.  The main case is packet receive frame summaries: frame metadata is only
available while parsing frames, but the final packet event is posted after the
packet is accepted.  `ZquicLogger::enabled()` must be conditionally compiled:
in `Zquic_DEBUG` builds it performs the qlog runtime-enabled check, and in
non-debug builds it is a `constexpr false` stub.  In this narrow case,
construct the accumulator only inside the enabled branch, use uninitialized
storage such as `ZuElem` so disabled qlog does not default-construct qlog
state, and keep the accumulated data bounded and payload-free.

## Event construction

Build qlog event structs inside the lambda body from captured values:

```c++
ZquicLOG(([
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
}));
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

Prefer typed event fields over string fields.  If a value is derived at run
time, first model it as a scalar, fixed-size value, error code, or enum with
`ZtEnumMap`.  Event string fields must own their data only when the string is
genuinely arbitrary free-form text; in that rare case use `ZeString`, move it
into the event with `ZuMv`, and let the logger thread serialize from owned
data.  Use borrowed `ZuCSpan` only for static event names or other static
literals with process lifetime.

For packet/frame summaries, reduce payload data to bounded metadata only:

- frame type;
- stream id, offset, length, FIN;
- ACK largest/ranges/counts/delay;
- crypto offset and length;
- close error code and redacted/length-limited reason metadata;
- CID sequence/length/reset-token presence;
- path challenge/response presence and length, not challenge bytes.

Do not log payload bytes or key material.

## Control flow

Do not add qlog-specific branches outside `ZquicLOG` unless they also affect
normal protocol behavior.

If an event should be skipped because it is empty, put the skip inside the
macro when the skip is qlog-only:

```c++
ZquicLOG(([
  ackedBytes = update.ackdBytes,
  ...
](auto &o, ZuTime time) {
  if (!ackedBytes) return;
  ...
}));
```

If the branch is protocol logic already needed without qlog, keep it outside:

```c++
if (!update.ackdBytes) return;   // protocol/recovery path already needs this
ZquicLOG(([...](auto &o, ZuTime time) { ... }));
```

## Review checklist

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
