# zquic supplementary guidelines

These guidelines are the audit checklist for code changes in `zquic`.  They
supplement the repo-wide rules in `../GUIDELINES.md`; follow both when editing
or reviewing `zquic` code.  Implementation discovery and source navigation live
in `README.md`.

# General zquic

- Keep QUIC runtime work on the owning Rx or Tx thread.  Do not add locks to
  make non-owner access convenient.
- Keep protocol state changes close to the RFC mechanism they implement.
  Cross-thread entry points should snapshot only destination-owned metadata and
  immediately post to the owning thread.
- Use existing `Zquic` protocol enums, fixed-size protocol values, Z socket
  address types, and module-local helpers before adding ad hoc string or STL
  representations.
- Debug text logging is unrelated to qlog.  Use `ZiLOG(Debug, ...)` for debug
  text and keep qlog instrumentation behind `ZquicLOG`.

# Buffers

`Zquic` uses role-specific `ZiIOBuf` allocation aliases from
`ZquicBuf.hh`.  Keep those roles distinct:

- `PktRxBufAlloc` defaults to the `"Zquic.Pkt.Rx"` heap for received UDP
  datagrams and in-place QUIC packet protection removal.
- `PktTxBufAlloc` defaults to the `"Zquic.Pkt.Tx"` heap for transmit
  packetization and packet protection output.
- `StreamTxBufAlloc` defaults to the `"Zquic.Stream.TxBuf"` heap for
  application stream bytes that must be retained for ACK, loss,
  retransmission, reset, cancellation, or teardown handling.
- `CryptoRxBufAlloc` defaults to the `"Zquic.Crypto.RxBuf"` heap for retained
  CRYPTO receive fragments.
- `CryptoTxBufAlloc` defaults to the `"Zquic.Crypto.TxBuf"` heap for TLS
  output staged for QUIC CRYPTO frame packetization.

`BufSize` is `1472`.  It is packet-buffer capacity, not the active UDP payload
limit.  The active path payload starts at the QUIC minimum and is raised only by
PMTUD/DPLPMTUD success.

Transmit packet protection is source-to-destination.  Retained plaintext
stream/control byte ranges are gathered as read-only inputs and encrypted
directly into the final Tx packet buffer.  Do not add an intermediate
ciphertext staging buffer.

Receive packet protection decrypts in place in the Rx packet buffer.  STREAM
receive data is represented by retained slices of that decrypted packet buffer:
`StreamRxData` carries both a `ZmRef<ZiIOBuf>` to the packet and the payload
span.  Frame-processing APIs that accept STREAM payload must require the packet
reference; do not add a frame-only STREAM copy fallback.

CRYPTO receive is different from STREAM receive.  Fragmented or out-of-order
CRYPTO data may be copied into retained `CryptoRxBufAlloc` buffers for
reassembly.  In-order CRYPTO delivery and TLS output should use direct
spans/origin-backed buffers when possible.

Hidden fallback copies below the public API boundary are forbidden.  Any new
copy path must be documented in this section and covered by a test that explains
why the copy is required.

# qlog

## Contract

`ZquicLOG` is the source marker for qlog-only runtime instrumentation.  It must
make qlog work obvious during review and erase qlog-only work from release
builds.

Use the same split as `ZiLog` / `ZiLOG`:

- `ZquicLOG(...)` is the macro used at call sites.
- `ZquicLog(...)` is the function called by the macro in debug builds.
- `ZquicLogger` owns the qlog writer thread, ring, diagnostics, JSON helpers,
  and file output.

Do not add `Zquic_LOG*`, `Zquic_DEBUG_LOG*`, or additional `ZquicLOG*` macros.

`Zquic_DEBUG` is the controlling conditional compile for qlog code.  Release
build verification must use `z.config`, then top-level `make clean`, then
top-level `make -j8`; do not validate release behavior with partial
subdirectory rebuilds.

Qlog file output should follow the `ZiLog` file-sink precedent: when a qlog
path already exists, age it through the configured archive depth before opening
the new log file.  Automated tests must place qlogs in a `ZiTestResidue`-owned
directory family so successful runs remove them and failed runs retain them.

## Lifecycle and gating

`ZquicLogger` is a `ZmSingleton` process-wide singleton.  The owning app
initializes it under main-thread control, early and once, from qlog parameters
during application `init`; it finalizes the logger after protocol app machinery
is drained and all activity has ceased during application `final`.

After initialization, `ZquicLogger::enabled()` is the single runtime
availability check for qlog.  Call sites and helpers should not separately
consult the owning app's qlog params.

No qlog-only expression may be evaluated in release builds.  No qlog-only
expression may be evaluated in debug builds when qlog is disabled by
configuration.  `ZquicLOG` must check the runtime enabled flag before
evaluating its argument, just as `ZiLOG` filters severity before posting to the
logger thread.

## Call-site shape

All qlog-only work belongs inside the `ZquicLOG` argument, usually in lambda
capture initializers or in the lambda body.  This includes:

- reading runtime counters only used for qlog;
- reading congestion, recovery, path, CID, stream, TLS, or packet metadata only
  used for qlog;
- converting enums or times into qlog strings/units;
- building qlog event structs;
- counting qlog frames or ACK ranges;
- copying qlog-only strings, owned `ZeString` values, or arrays;
- calling qlog JSON helper functions.

Correct pattern:

```c++
ZquicLOG(([
  space,
  pn,
  bytes,
  bytesInFlight = m_congestion.bytesInFlight(),
  latestRTT = m_rtt.latest()
](auto &o, ZuTime time) mutable {
  PktEvent event{
    .packetSpace = space,
    .packetNumber = pn,
    .packetSize = bytes,
    .bytesInFlight = bytesInFlight,
    .latestRTTUS = uint64_t(latestRTT.microsecs())
  };
  o.logPktSent(event, time);
}));
```

Incorrect pattern:

```c++
uint64_t bytesInFlight = m_congestion.bytesInFlight();
PktEvent event;
event.packetSpace = space;
ZquicLOG(([event, bytesInFlight](auto &o, ZuTime time) mutable {
  o.logPktSent(event, time);
}));
```

The incorrect form leaves qlog-only reads and event construction in the hot
path even when qlog compiles out.

Do not add qlog-specific branches outside `ZquicLOG` unless they also affect
normal protocol behavior.  If an event should be skipped because it is empty,
put the skip inside the macro when the skip is qlog-only:

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

## Captures and threading

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

The hot-path thread should only:

- evaluate capture initializers in debug builds after `ZquicLOG` has confirmed
  qlog is enabled;
- move the lambda by value into the qlog ring;
- update qlog enqueue/drop diagnostics.

The qlog thread should do as much as possible:

- construct qlog-specific structs from captured values;
- map enum names through `ZtEnumMap` and qlog field names through `ZfStruct`
  metadata;
- use `ZfJSON` / `ZfStruct` mappings;
- write JSON-SEQ records;
- perform bounded formatting.

Do not format JSON, allocate scratch JSON strings, or walk runtime-owned
containers on Rx/Tx I/O threads.

## Data modeling and JSON

Most qlog fields are not arbitrary strings.  QUIC protocol data is normally
fixed-size scalar data, IP address/port data, protocol IDs, error codes,
enumerations, or other closed vocabularies.  Short string literals used in the
code for qlog reasons, triggers, actions, states, packet labels, or frame
labels should be modeled as enum values, aligning with system error-code style
classification.  Do not capture those literals as strings.

If a value is an enumeration or closed vocabulary, keep it as an enum and use a
`ZtEnumMap` with `ZfStruct` / `ZfJSON`.  The JSON string conversion belongs on
the logger thread, during `ZfJSON` serialization:

```c++
ZtEnum(TheEnum, int8_t, Value0, Value1);
ZtEnumMap(TheEnum, JSON, "value_0", "value_1");
ZfStruct(, (TheStruct, JSON),
  (enum_, (Ctor<...>, Enum<TheEnum::JSON>),	Int8));
```

Do not translate enums to strings in helper functions just to feed those
strings back into `ZfJSON`.

Use existing Z framework printing and JSON facilities for Z network value
types.  `ZiIP` already knows how to print itself, and qlog endpoint/address
JSON should be expressed as `ZfJSON` / `ZfStruct` mappings over `ZiIP`,
`ZiSockAddr`, ports, and enum/scalar fields.  Do not add qlog-local IP address
formatters, byte shifting, `{data(), length()}` span reconstruction, or string
parsing helpers for functionality the Z types already provide.

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

## Event construction and schema

Build qlog event structs inside the lambda body from captured values:

```c++
ZquicLOG(([
  action,
  reason,
  mtu = m_path.activeMaxUDP(),
  antiAmplification = m_path.antiAmplificationRemaining(),
  validated = pathValidated_()
](auto &o, ZuTime time) {
  PathEvent event{
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
PathEvent event{
  .action = action,
  .reason = reason,
  .mtu = mtu,
  .validated = validated
};
```

Avoid this pattern unless there is a real dependency between assignments:

```c++
PathEvent event;
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
`ZtEnumMap`.  Qlog event names are closed vocabulary values: model them as a
`ZtEnumMap`-backed enum and serialize them through `ZfJSON`; do not pass qlog
event names around as `ZuCSpan`, `ZeString`, or string literals outside the
enum map.  Event string fields must own their data only when the string is
genuinely arbitrary free-form text; in that rare case use `ZeString`, move it
into the event with `ZuMv`, and let the logger thread serialize from owned
data.  Use borrowed `ZuCSpan` only for static non-event literals with process
lifetime.

Event names and field shapes must track the current qlog draft schema for
`quic:*` events.  When an event or field extends beyond that schema, align it
with mainstream reference implementation precedent where one exists, such as
mvfst's `quic:path_validated` extension event and its `success` / `vantage`
payload shape.  Reference precedent covers both the event name and the data
fields; do not add zquic-specific detail fields to a reference-backed `quic:*`
extension event unless the reference implementation does the same.  If no
clear reference precedent exists, keep the event or field under the private
zquic schema instead of presenting it as standard QUIC qlog output.

For packet/frame summaries, reduce payload data to bounded metadata only:

- frame type;
- stream id, offset, length, FIN;
- ACK largest/ranges/counts/delay;
- crypto offset and length;
- close error code and redacted/length-limited reason metadata;
- CID sequence/length/reset-token presence;
- path challenge/response presence and length, not challenge bytes.

Do not log payload bytes or key material.

## Helpers and exceptions

Helpers are encouraged, but they must preserve the compile-out rule.

Good helpers:

- logger-thread helpers called from inside the `ZquicLOG` lambda;
- pure qlog conversion helpers used only inside `ZquicLOG`;
- bounded event writers that take already-captured scalar metadata;
- JSON serialization helpers using `ZfJSON` / `ZfStruct`.

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
construct the accumulator only inside the enabled branch, use
`ZuUnion<void, T>` so disabled qlog does not default-construct qlog state and
the union manages destruction, and keep the accumulated data bounded and
payload-free.

## Review checklist

For every qlog call site, verify:

- the visible marker is `ZquicLOG`;
- qlog-only reads and temporaries are inside capture initializers;
- qlog-only conversions and struct construction are inside the lambda body;
- qlog event structs use direct aggregate/member initialization where possible;
- captures are by value and bounded;
- no references, runtime owner pointers, packet payloads, or secrets escape to
  the qlog thread;
- JSON output uses `ZfJSON` / `ZfStruct`;
- release builds erase the qlog-only code at preprocessing time;
- the hot path does not do qlog formatting or JSON serialization.
