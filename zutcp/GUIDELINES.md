# Phase-1 execution boundary

The mechanically renamed baseline is recorded in
`../review_context/zutcp-execution/zutcp-mechanical-rename.json`. Phase 1 is now
being adapted under `../zutcp_plan.md`; the QUIC-specific material below describes
the copied baseline until the corresponding facilities are removed. It does not
require retaining TLS/protection, migration or multiple streams in Zutcp.

The public transport owner is `Connection`, specialized by `CliConnection` and
`SrvConnection`; use the corresponding Connection-named headers. No old Link
owner type/header aliases are retained.

Phase 1 consolidates `Stream` into `Connection`. There is no separate stream
concept in the target implementation: no Stream/CliStream/SrvStream type, object,
identifier, map, factory or callback. Move surviving byte reassembly/delivery,
receive-credit retirement, Tx retention and producer behavior into Connection,
with the same Rx/Tx owners and original buffer references. Do not preserve a
single-stream wrapper or alias. Setup now negotiates only connection byte credit
through `maxData`; do not
restore stream byte/count transport parameters or Hub settings. The copied
stream guidance below describes only the baseline until consolidation removes it.

The current merged implementation exposes connection `reader()` and
`writer()`/`writer_()` facilities, with `Reader` and `TxBufAlloc` byte storage.
Use `ZutcpConnectionUtil.hh` for connection credit and receive final-size state.
Data/reset/stop wire records have no stream ID; connection controls are
`MaxData` and `DataBlocked`. Do not restore removed per-stream/count controls,
ID/limit utilities, child owner pointers, stream lifecycle logging or a
self-selection/refcount step in packetization. CID sequence metadata is distinct
from connection byte offsets. Remaining copied fixtures must be ported directly.

Use `dataRxAllocator()`/`dataTxAllocator()` for connection byte storage. Parsed
`Frame` has no generic sequence/ID field; CID control records use `cidSeq`.
Data recovery identity consists of wire offset, length and FIN, independently of
buffer coordinates or CID/control metadata. Remove obsolete stream diagnostics
and accessors outright; do not preserve their layout or add compatibility aliases.

Reset merged byte state on its Rx/Tx owners, including queue sequence state and
terminal flags. Publish one byte-work generation from Rx and install that value
on Tx; reject stale posted admissions, writers and completion/control handoffs.
Retire only installed queue credit during reset: pending admissions release their
own reserved credit once when dispatched. Generation checks protect reuse, while
owner lifetime still requires the independent teardown/drain contract.

Keep all Zi provider/object composition in Zutcp. `ZutcpAlloc` supplies the first
Tx/control provider over its embedded caller-owned raw pool. `ZutcpMx::Stack`
composes those providers with one physical Rx reserve, accepted-output metadata
detachment and mx owner lifecycle; see the current checkpoint in
`../zutcp_plan.md`. The stripped transport remains subsequent work.
`ZutcpIP` owns intermediate Ethernet/IP/UDP framing and integrity checks. Its
writer receives an already populated payload at the family-specific header
offset and writes only headers/padding. Keep payload ownership and endpoint
routing in the transport; there is no packet codec or route lookup in Zxdp.
`resolveUDPPath` queries kernel route/static-neighbor state during cold control
setup only. Never run its blocking request/reply on packet Rx/Tx owners. It
constrains lookup to the configured interface, preserves an explicit local source
and publishes the resulting path only on complete success. It does not run ARP/ND
or keep a routing-table replica.
Peer transport parameters now belong to the Rx link, independently of the
remaining TLS readiness/flight machinery. Crypto no longer encodes, stores or
decodes transport parameters; only plaintext Initial input installs peer limits. Sender ACK timing and stream-credit readers use the
bounded snapshot installed on Tx. Do not restore reads of Crypto-owned peer
parameters or write Tx connection credit from Rx. Initial output now uses the
plaintext codec on Tx with a separately constructed local parameter record and copied bootstrap identity. Rx installs decoded peer
parameters. TLS readiness, Handshake/application protection deletion and native
establishment remain phase-1 execution work.
`InitialPkt` in `ZutcpTransport` supplies the plaintext codec: parameter-length
prefix, encoded values and directly constructed frames, without protection or
tag/sample reservations. The codec does not install limits or complete setup;
the owning link must do that. The transitional Initial sender gathers retained
frame spans directly into the final packet after its encoded parameters. This copy constructs the required
contiguous datagram; it adds no intermediate payload buffer. The TLS frame
carrier itself remains scheduled for deletion.
Bootstrap no longer generates reset tokens. Existing Retry tokens remain
in the normal Initial header. Pure ACK Initials omit parameter values so a
parameter response cannot induce an ACK loop.
Keep Zi types, adapter state and mx scheduler integration in Zutcp. Zxdp owns
its required configurable spin-before-epoll `rx()` and interrupting `wake()`.
Delegate the plugin hooks to that pair; use the raw named-parameter pool/callback
interface in the updated plan.

Reuse Clang/debug/ASan for development and acceptance with leak detection enabled.
Apply `../REBUILD.md` dependency order, freezes, batching and build reuse with the
author's ASan override; avoid unnecessary clean builds. Separate optional release
profiling and any non-ASan Memcheck build from the active development tree.

# zutcp supplementary guidelines

These guidelines are the audit checklist for code changes in `zutcp`.  They
supplement the repo-wide rules in `../GUIDELINES.md`; follow both when editing
or reviewing `zutcp` code.  Implementation discovery and source navigation live
in `README.md`.

# General zutcp

- Keep QUIC runtime work on the owning Rx or Tx thread.  Do not add locks to
  make non-owner access convenient.
- Keep protocol state changes close to the RFC mechanism they implement.
  Cross-thread entry points should snapshot only destination-owned metadata and
  immediately post to the owning thread.
- Use existing `Zutcp` protocol enums, fixed-size protocol values, Z socket
  address types, and module-local helpers before adding ad hoc string or STL
  representations.
- Debug text logging is unrelated to qlog.  Use `ZiLOG(Debug, ...)` for debug
  text and keep qlog instrumentation behind `ZutcpLOG`.

# Buffers

`Zutcp` exposes UDP packet allocator classes and ordinary plaintext allocation aliases through
`Zutcp.hh`. Keep those roles distinct:

- `PktRxBufAllocator` defaults to the `"Zutcp.Pkt.Rx"` heap for received UDP
  datagrams and in-place QUIC packet protection removal. Configure it through
  `ZiMultiplex::allocator` registration and the UDP call's receive allocator;
  the endpoint receives the original `io.buf`.
- `PktTxBufAllocator` defaults to the `"Zutcp.Pkt.Tx"` heap for transmit
  packetization and packet protection output. Each endpoint retains its own
  allocator; active output belongs to `ZiTxContext::buf` until kernel retirement.
- `StreamTxBufAlloc` defaults to the `"Zutcp.Stream.TxBuf"` heap for
  application stream bytes that must be retained for ACK, loss,
  retransmission, reset, cancellation, or teardown handling.
- `CryptoRxBufAlloc` defaults to the `"Zutcp.Crypto.RxBuf"` heap for retained
  CRYPTO receive fragments.
- `CryptoTxBufAlloc` defaults to the `"Zutcp.Crypto.TxBuf"` heap for TLS
  output staged for QUIC CRYPTO frame packetization.

Completed transmit datagrams use `Zutcp_::TxPkt`, an intrinsic `ZiTxBuf`
node with destination address and ECN metadata. Rx and retained plaintext buffers
remain separate metadata types. The mx factory selects TxUDP backing; no builtin
backing assumption is permitted. Packet protection writes directly into the
provider's writable span with its original headroom and tailroom.

Each physical `EndpointCxn` owns its common `ZiTx` queue on mx Tx. Its 4096-packet
capacity includes the installed active head. Reject full-queue submissions before
counting submitted bytes, recording both backpressure and a local drop. Posted
submissions may acknowledge posting before rejection; QUIC recovery still owns
retained plaintext and packet records above this boundary. Priority admission
inserts immediately after the active head, newest priority first among pending
packets. Ordinary submissions remain FIFO. Address and ECN stay immutable while
queued and are installed afresh for every selected head.

Posted output snapshots only raw physical identity and generation. Compare that
identity on Tx without dereferencing or retaining the old connection; obtain the
current connection locally at admission. Existing endpoint owner-drain rules
protect the raw endpoint callback. Do not add per-packet owner references.

When a protocol ACK opens congestion credit, service the existing bounded retained
retransmission batch before ordinary producers. Fresh bulk output must not spend
all reopened credit before deferred recovery runs. Socket completion reports one
local datagram and byte count, without ACK,
recovery retirement or producer notification. EAGAIN retains the same protected
packet. Bind posted output to its original physical connection; reject stale
submissions after rebind rather than routing them through a replacement. Closing
prevents admission; physical disconnect and Tx owner drain precede context and
queue retirement. Backend-held backing remains the backend's responsibility until
its retirement. ZiTx advances queued output in the common completion turn rather
than posting the former per-packet scheduler continuation; pacing remains above
this queue. Measure bounded-drain latency when changing the capacity.

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

Cross-shard STREAM sends transfer an owned range or construct directly in the
stream writer's retained buffers. Do not stage bytes in an asynchronous byte
array before copying them into stream storage. TLS output already has a
`CryptoTxBufAlloc` owner: transfer it through the CRYPTO send callback and retain
its ranges for retransmission until the existing epoch/reset teardown retires
them. Packet protection reads those ranges directly into final UDP output.

Hidden fallback copies below the public API boundary are forbidden.  Any new
copy path must be documented in this section and covered by a test that explains
why the copy is required.

# Recovery and handshake confirmation

PTO reclamation selects the oldest eligible retained packets. Newly emitted
retransmissions must not indefinitely displace older STREAM or control work.
Use the existing recovery references and ACK retirement for HANDSHAKE_DONE;
do not add a separate completion timer or retry queue.

A PTO services its selected packet-number space. When another space has data
in flight and transmit keys, leave room for an acknowledgment-eliciting probe
there within the two-datagram budget (RFC 9002 section 6.2.4). A successful
Handshake retransmission must not suppress all outstanding 1-RTT probes. A
failed primary send does not authorize an unrelated substitute. Application
Data does not independently select PTO before handshake confirmation.
Candidate path proof retains priority over ordinary recovery for both active
client migration and passive server rebinding.

Server confirmation occurs at TLS completion; client confirmation occurs on
HANDSHAKE_DONE. Retire the actual Handshake secrets on Rx and the independent
Tx protector/recovery state on Tx. Defer server Rx retirement until its current
CRYPTO delivery callback unwinds. Late crypto and ACK snapshots must not
repopulate a retired packet-number space. Initial-key storage has a separate
existing cross-owner read and is not changed by the Handshake retirement repair.

# qlog

## Contract

`ZutcpLOG` is the source marker for qlog-only runtime instrumentation.  It must
make qlog work obvious during review and erase qlog-only work from release
builds.

Use the same split as `ZiLog` / `ZiLOG`:

- `ZutcpLOG(...)` is the macro used at call sites.
- `ZutcpLog(...)` is the function called by the macro in debug builds.
- `ZutcpLogger` owns the qlog writer thread, ring, diagnostics, JSON helpers,
  and file output.

Do not add `Zutcp_LOG*`, `Zutcp_DEBUG_LOG*`, or additional `ZutcpLOG*` macros.

`Zutcp_DEBUG` is the controlling conditional compile for qlog code.  Release
build verification must use `z.config`, then top-level `make clean`, then
top-level `make -j8`; do not validate release behavior with partial
subdirectory rebuilds.

Qlog file output should follow the `ZiLog` file-sink precedent: when a qlog
path already exists, age it through the configured archive depth before opening
the new log file.  Automated tests must place qlogs in a `ZiTestResidue`-owned
directory family so successful runs remove them and failed runs retain them.

## Lifecycle and gating

`ZutcpLogger` is a `ZmSingleton` process-wide singleton.  The owning app
initializes it under main-thread control, early and once, from qlog parameters
during application `init`; it finalizes the logger after protocol app machinery
is drained and all activity has ceased during application `final`.

After initialization, `ZutcpLogger::enabled()` is the single runtime
availability check for qlog.  Call sites and helpers should not separately
consult the owning app's qlog params.

No qlog-only expression may be evaluated in release builds.  No qlog-only
expression may be evaluated in debug builds when qlog is disabled by
configuration.  `ZutcpLOG` must check the runtime enabled flag before
evaluating its argument, just as `ZiLOG` filters severity before posting to the
logger thread.

## Call-site shape

All qlog-only work belongs inside the `ZutcpLOG` argument, usually in lambda
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
ZutcpLOG(([
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
ZutcpLOG(([event, bytesInFlight](auto &o, ZuTime time) mutable {
  o.logPktSent(event, time);
}));
```

The incorrect form leaves qlog-only reads and event construction in the hot
path even when qlog compiles out.

Do not add qlog-specific branches outside `ZutcpLOG` unless they also affect
normal protocol behavior.  If an event should be skipped because it is empty,
put the skip inside the macro when the skip is qlog-only:

```c++
ZutcpLOG(([
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
ZutcpLOG(([...](auto &o, ZuTime time) { ... }));
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

- evaluate capture initializers in debug builds after `ZutcpLOG` has confirmed
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
ZutcpLOG(([
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
ZutcpLOG(([
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
fields; do not add zutcp-specific detail fields to a reference-backed `quic:*`
extension event unless the reference implementation does the same.  If no
clear reference precedent exists, keep the event or field under the private
zutcp schema instead of presenting it as standard QUIC qlog output.

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

- logger-thread helpers called from inside the `ZutcpLOG` lambda;
- pure qlog conversion helpers used only inside `ZutcpLOG`;
- bounded event writers that take already-captured scalar metadata;
- JSON serialization helpers using `ZfJSON` / `ZfStruct`.

Bad helpers:

- hot-path wrappers such as `qlogFoo_()` that hide qlog work behind a normal
  function call;
- helpers that read runtime state before entering `ZutcpLOG`;
- helpers that build qlog event structs before entering `ZutcpLOG`;
- helpers that use `ZutcpLogger::enabled()` to guard qlog work in normal code.

`ZutcpLogger::enabled()` is acceptable for lifecycle checks and tests, but it
is not the hot-path instrumentation pattern.  Hot-path instrumentation should
use `ZutcpLOG`.

Use `ZutcpLogger::enabled()` only for qlog state that must be accumulated
synchronously on the protocol thread before a later `ZutcpLOG` writer-thread
post.  The main case is packet receive frame summaries: frame metadata is only
available while parsing frames, but the final packet event is posted after the
packet is accepted.  `ZutcpLogger::enabled()` must be conditionally compiled:
in `Zutcp_DEBUG` builds it performs the qlog runtime-enabled check, and in
non-debug builds it is a `constexpr false` stub.  In this narrow case,
construct the accumulator only inside the enabled branch, use
`ZuUnion<void, T>` so disabled qlog does not default-construct qlog state and
the union manages destruction, and keep the accumulated data bounded and
payload-free.

## Review checklist

For every qlog call site, verify:

- the visible marker is `ZutcpLOG`;
- qlog-only reads and temporaries are inside capture initializers;
- qlog-only conversions and struct construction are inside the lambda body;
- qlog event structs use direct aggregate/member initialization where possible;
- captures are by value and bounded;
- no references, runtime owner pointers, packet payloads, or secrets escape to
  the qlog thread;
- JSON output uses `ZfJSON` / `ZfStruct`;
- release builds erase the qlog-only code at preprocessing time;
- the hot path does not do qlog formatting or JSON serialization.

Short-header key trials preserve ciphertext in scratch until authentication
succeeds, then copy authenticated plaintext back into the original packet. This
copy permits old/next-key retry after a failed authentication; STREAM slices
still retain the original packet backing.
