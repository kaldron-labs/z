# Zhttp logical-stream migration plan

## Objective

Replace the current `Zhttp::Tunnel<Link>` adapter and the separate
`tunnelData()`/`tunnelEnd()`/`tunnelReset()` receive callbacks with one
compile-time HTTP logical-stream contract.

An established application protocol must see the same operations for:

- an upgraded H1 TCP connection;
- an upgraded H1 TLS connection;
- an H2 Extended CONNECT stream; and
- an H3 Extended CONNECT request stream.

The common contract directly composes:

- the callback-scoped Rx stream or bounded Rx view;
- callback-scoped access to the native Tx stream;
- graceful local end and abortive reset; and
- local and peer stream-establishment capability.

HTTP version, framing, compression, flow control, scheduling, and physical
connection ownership remain below the contract.  A dependent protocol such as
WebSocket may retain profile-specific opening-handshake code, but its
established framing, message, control, and close logic must be one template
that consumes the common logical stream.

Backward compatibility is not a goal.  Once all in-tree users have migrated,
delete `Zhttp::Tunnel`, the `tunnel*` link methods, and the `tunnel*` parser
callbacks rather than retaining aliases or forwarding shims.

## Current implementation and gaps

The current code has most of the required lower-layer behavior, but exposes it
as two disconnected interfaces.

`Zhttp::Tunnel<Link>` is a non-owning link wrapper with:

```c++
peerCap()
localCap()
send(callback)
end()
reset()
```

It contains only a raw `Link *`.  It has no Rx stream and is therefore not a
complete logical-stream abstraction.

Receive is exposed independently by the H2 and H3 parsers:

```c++
tunnelData(rx)
tunnelEnd()
tunnelReset()
```

The resulting gaps are:

- applications must assemble one logical stream from a Tx/control object and
  unrelated parser callbacks;
- the name describes Extended CONNECT establishment rather than the
  application-visible object, which is an ordered bidirectional stream;
- H1 Upgrade cannot use the adapter even though its established data phase has
  the same semantics;
- H2 client/server links duplicate the same five tunnel operations, and H3
  client/server links duplicate another copy;
- H2/H3 parser state and callback names leak establishment terminology into
  the established data path;
- receive end/reset and local end/reset are represented differently instead of
  being part of one stream lifecycle;
- tests validate H2 and H3 tunnel behavior separately rather than compiling
  and running one stream contract across H1 TCP, H1 TLS, H2 TLS, and H3 QUIC.

Do not solve these gaps by adding a virtual base, type erasure, another receive
queue, a copied contiguous buffer, or an owning adapter.  The target remains a
small borrowed template facade over the current link and current Rx object.

## Architectural boundary

Stream establishment and established-stream use are separate concerns:

```text
                 protocol-specific establishment
          +------------------+----------------------+
          |                  |                      |
      H1 Upgrade      H2 Extended CONNECT    H3 Extended CONNECT
          +------------------+----------------------+
                             |
                    Zhttp logical stream
            Rx | Tx | local end/reset | capability
                             |
               protocol-independent consumer
```

The opening handshake cannot be made completely protocol-blind:

- H1 uses `Upgrade` and changes the mode of the entire TCP/TLS link;
- H2 uses RFC 8441 and requires
  `SETTINGS_ENABLE_CONNECT_PROTOCOL`;
- H3 uses RFC 9220 and requires the corresponding H3 setting.

The binding selects and validates the opening handshake.  Once it succeeds,
the consumer receives only the common stream contract.  The consumer must not
branch on H1/H2/H3, TCP/TLS/QUIC, HTTP frame types, or physical-connection
ownership.

Lower-layer ownership does not move:

- H1 has no transport flow control beyond TCP;
- H2 DATA framing, stream/connection windows, and scheduling remain in
  `Zhttp`;
- H3 DATA framing and QPACK remain in `Zhttp`;
- QUIC stream/connection flow control, FIN, RESET_STREAM, and STOP_SENDING
  remain in `Zquic`;
- TCP, TLS, and QUIC continue to own pooled `ZiIOBuf` allocation and physical
  I/O teardown.

## Target API

Add `zhttp/src/ZhttpStream.hh` and expose a borrowed
`Zhttp::Stream<Link, Rx = void>` facade.  Use class-template argument
deduction so callers do not spell concrete link or Rx types.

The Rx-bearing form is constructed only inside the common receive-process
callback.  The application drives `ZiRxLayer` in the normal order:
`input()` first makes pending input/terminal state visible, `events()` reads
and clears the resulting flags, and `consume()` advances payload without
copying.  A consume of the last final bytes may raise `Final`, which is
observed on the next loop iteration:

```c++
void streamProcess(auto stream)
{
  auto &rx = stream.rx();
  for (;;) {
    bool input = rx.input();
    auto events = rx.events();

    if (events & Zi::RxEvent::Start()) {
      // initialize this logical stream's receive codec
    }
    if (events & Zi::RxEvent::Error()) {
      // peer/lower layer aborted the stream
      return;
    }
    if (input) {
      int64_t n = rx.consume(frame, data);
      if (n < 0) {
	auto error = rx.events();       // consume may have raised Error
	(void)error;
	return;
      }
      if (!n) return;                // wait; never spin without progress
      continue;
    }
    if (events & Zi::RxEvent::Final()) {
      // peer gracefully ended its sending side
    }
    return;
  }
}
```

The application also owns Tx flush boundaries:

```c++
Zhttp::Stream stream{link};
stream.tx([](auto &tx) {
  tx << message;
  tx.flush();                       // production boundary; force handoff
});
stream.end();                       // ordered after already-flushed Tx
// or: stream.reset();
```

The Rx-less form is used while opening the stream and for local Tx/control:

```c++
Zhttp::Stream stream{link};
if (stream.peerCap()) {
  // emit the profile-specific opening request
}
```

The facade contract is:

```c++
template <typename Link, typename Rx = void>
class Stream {
public:
  bool localCap() const;
  bool peerCap() const;

  Rx &rx();                         // Rx-bearing form only

  template <typename L>
  void tx(L &&);                    // callback receives borrowed Tx stream

  void end();                       // graceful local end
  void reset();                     // abortive local reset
};
```

These functions forward only to the final link boundary:

| Facade call | Link call |
| --- | --- |
| `localCap()` | `streamLocalCap()` |
| `peerCap()` | `streamPeerCap()` |
| `tx(callback)` | `streamTx(callback)` |
| `end()` | `streamTxEnd()` |
| `reset()` | `streamTxReset()` |

Use a partial specialization or a small private base to omit `rx()` from the
Rx-less form.  Do not use `void *`, `std::function`, virtual dispatch, a
variant, or a run-time profile tag.  The facade stores raw pointers to the
longer-lived link and callback-scoped Rx object and performs no allocation.
It must not be retained after the input or Tx callback returns.

`tx(callback)` is intentionally callback-scoped.  It does not promise socket
send completion and must not expose a lower stream beyond its valid lifetime.
It also does not flush:

- H1 obtains the link's current `txStream()` and invokes the callback;
- H2 obtains the selected logical stream's DATA body layer;
- H3 obtains `H3::dataStream()` over the selected QUIC request stream.

Keep the name `tx`, rather than `send`, to make clear that the callback
receives the composed Tx stream and may write more than one span.  Preserve
`ZiTxLayer` headroom/tailroom composition and propagate each application
`flush()` through the composed layers.  A flush demarcates an application
production/message boundary and forces prompt handoff toward the transport;
lower-layer framing, flow control, and packetization may still divide or
combine transport output.  The application may issue multiple flushes in one
`tx()` callback, using either `tx.flush()` or the existing
`tx << Zi::flush()` manipulator.

Retain normal `ZiTxStream`/`ZiTxLayer` RAII semantics: destruction at callback
scope exit flushes any residual buffered output.  The facade and link adapter
must not add another explicit final flush around the callback.  Applications
must not rely on scope-exit destruction to demarcate messages or achieve
minimum latency; tests and examples explicitly flush each completed
application message.  `end()` is called only after `tx()` returns and is
therefore ordered after explicit flushes and any residual scope-exit flush.

`end()` means that no more local application bytes will be sent:

- H1 orders physical-link disconnect after already queued Tx data;
- H2 emits/orders END_STREAM on only the logical stream;
- H3 finishes only the selected request stream, producing QUIC FIN.

`reset()` aborts the application stream:

- H1 aborts the physical link because the link and application stream are the
  same object;
- H2 cancels only the selected logical stream;
- H3 issues STOP_SENDING/RESET_STREAM for only the selected request stream.

Both operations are idempotent.  `reset()` dominates a pending `end()`.
Neither may synchronously infer completion from `run()` or `invoke()`.
Completion remains continuation-driven.

The facade contains no lifecycle state.  Keep stream lifecycle and terminal
notification state on the owning link/session, Rx-owned unless an existing
lower-layer state machine already owns it.  Public `end()`/`reset()` entry
points are thin dispatchers; their owner-shard variants use the repository's
trailing-underscore convention.  Cross-shard posts capture only fixed
metadata or destination-owned buffer handles, never a borrowed Rx view.

`localCap()` and `peerCap()` describe whether the selected profile can
establish the logical stream:

- for H2/H3 they retain the current local-setting and peer-setting meanings;
- an H1 binding reports local capability when its Upgrade handler is enabled;
- H1 peer capability becomes true only after a valid Upgrade request or `101`
  response has been accepted.

H1 has no pre-handshake capability advertisement.  Code choosing an H1
opening request must not treat an initial false `peerCap()` as rejection; it
learns peer capability from the handshake response.  Do not introduce a
three-state capability enum unless an actual in-tree selection policy needs
to distinguish unknown from rejected.

The sole application receive callback is:

```c++
streamProcess(stream)    // inspect stream.rx().events(), then consume input
```

`Zi::RxEvent` is the common `ZtFlags` vocabulary:

- `Start` is delivered once for a newly established logical stream;
- `Input` reports newly exposed payload;
- `Final` reports a graceful peer half-close, including an empty FIN/end; and
- `Error` reports reset, cancellation, or lower-layer failure.

`Final` and `Error` are mutually exclusive terminal events and each is
delivered at most once.  After either terminal event, no application callback
is permitted.  They replace separate application-facing
`streamRxEnd()`/`streamRxReset()` callbacks; maintaining both mechanisms
would duplicate terminal notification and make exact-once behavior
ambiguous.

The bindings normalize events at their existing protocol transition sites:

| Logical event | H1 Upgrade | H2 Extended CONNECT | H3 Extended CONNECT |
| --- | --- | --- | --- |
| `Start` | accepted Upgrade/`101` | accepted final CONNECT headers | accepted final CONNECT headers |
| `Input` | readable native TCP/TLS bytes | DATA payload exposed by `BodyRx` | DATA payload exposed by `BodyRx` |
| `Final` | graceful peer EOF | END_STREAM | clean QUIC FIN |
| `Error` | abort/transport failure | RST_STREAM/parser cancellation | RESET_STREAM/applicable STOP_SENDING/parser failure |

Engine teardown after ingress has been disabled is not a peer event and is
silent at this interface.  Local `end()`/`reset()` likewise do not synthesize
Rx flags; a later peer/lower-layer outcome is reported only if dispatch is
still active and has not already delivered a terminal event.

Existing parser `complete(state)` hooks remain available for parser/engine
cleanup and ordinary HTTP message completion.  After entry into
`ParserState::Stream`, they must not become a second application
logical-stream terminal path; the workload observes only `Final`/`Error` through
`streamProcess()`.

`Zhttp::StreamDispatch<Link, Consumer>` is the shared, non-owning adapter
between the protocol-owned Rx layer and this application hook.  It stores raw
back-pointers to the logical link and consumer.  Its `process(rx)` function
constructs the Rx-bearing facade and calls `streamProcess()`.  Bind it while
the logical link is established, prevent new dispatch before teardown, and
clear it only after pending Rx callbacks have drained.  It performs no
allocation, buffering, cross-shard post, or type erasure.

Its required shape is:

```c++
template <typename Link, typename Consumer>
class StreamDispatch {
public:
  void init(Link &, Consumer &);

  template <typename Rx>
  void process(Rx &);               // consumer.streamProcess(Stream{link, rx})

  void disable_();                  // Rx phase 1: null link, reject new calls
  void final_();                    // after Rx drain: null consumer
};
```

`disable_()` and `final_()` are owner-shard lifecycle operations, not a
synchronous public `stop()` contract.  The owner prevents ingress before
`disable_()`, posts the Rx drain continuation, calls `final_()` from that
continuation, and only then advances to Tx drain.  The two pointer null values
encode disabled/final state; do not add independent booleans or atomics.
`init()` is once-only, `process()` becomes a no-op after `disable_()`, and
`final_()` asserts that the link is already null before clearing the consumer
pointer.

H2 and H3 parsers continue to use `Zhttp::BodyRx`/`ZiRxLayer` for bounded,
no-copy DATA delivery.  Extend `BodyRx` locally in `Zhttp`, without changing
its payload path, so it can:

- invoke the same process callback with `Input`;
- raise `Final` and invoke the callback even when end/FIN has no payload;
- raise `Error` and invoke the callback for reset/cancellation; and
- silently `cancel()` only during teardown, when dispatch is already
  disabled.

Keep the existing no-argument `finish()` for ordinary HTTP body completion.
Add stream-mode callback overloads with these semantics:

```c++
start(process)              // Start; invoke once when establishment succeeds
offer(span, final, process) // Input; Final follows consumption of final span
finish(process)             // empty Final; always invokes process once
fail(process)               // Error; always invokes process once
cancel()                    // silent teardown; never invokes process
```

`start(process)` consumes/delivers the initially pending `Start` even if no
payload has arrived, so establishment itself hands the common stream to the
consumer.  `finish(process)`/`fail(process)` are idempotent against
already-terminal state and return whether the requested transition was
valid.  They prime the layer sufficiently for the subsequent
`input()`/`events()` loop to observe the flag, but the application remains
responsible for reading and clearing events.  Do not use `cancel()` to
represent a peer reset.

Replace the parser's three tunnel callbacks with one concrete-Rx callback:

```c++
template <typename Rx>
void streamProcess(Rx &rx) { dispatch.process(rx); }
```

The existing H2 END_STREAM and H3 FIN/reset detection sites feed
`BodyRx`'s `Final`/`Error` events and invoke this callback at those same
sites.  Do not relocate frame parsing, add a scheduler turn, or add a queue.

Native `ZiRxStream` does not expose `input()`/`events()`, so H1 must not pass
it directly as the common Rx type.  Add a thin `H1::StreamRx` implemented as
a `ZiRxLayer` over the current native TCP/TLS `ZiRxStream`.  It exposes native
spans and advances them in place, synthesizes `Start`/`Input`, and raises
`Final`/`Error` from the existing disconnect outcome.  It owns no payload,
does not gather or copy, and never retains the callback-scoped native stream
pointer after the native process callback returns.

Thus every application-visible `stream.rx()` supports:

```c++
input()
events()
consume(frame, data)
empty()
complete()
failed()
available()
```

Do not force the native H1 queue into `BodyRx`, and do not flatten H2/H3 DATA
into a new queue.  Normalize behavior with stacked `ZiRxLayer` composition,
not storage or copying.

## Naming migration

Use stream terminology consistently in public and internal code:

| Current | Target |
| --- | --- |
| `ZhttpTunnel.hh` | `ZhttpStream.hh` |
| `Zhttp::Tunnel` | `Zhttp::Stream` |
| `ParserState::Tunnel` | `ParserState::Stream` |
| `tunnel()` | `stream()` |
| `tunnelData()` | `streamProcess()` |
| receive-side `tunnelEnd()` | `Zi::RxEvent::Final` |
| receive-side `tunnelReset()` | `Zi::RxEvent::Error` |
| `tunnelPeerCap()` | `streamPeerCap()` |
| `tunnelLocalCap()` | `streamLocalCap()` |
| `tunnelSend()` | `streamTx()` |
| link-side `tunnelEnd()` | `streamTxEnd()` |
| link-side `tunnelReset()` | `streamTxReset()` |

These directional names are fixed before implementation:

- `streamTxEnd()` and `streamTxReset()` initiate local Tx terminal work;
- `streamProcess()` is the sole application Rx entry point and reports peer
  terminal state through `Zi::RxEvent`; and
- the public facade retains the concise application operations `end()` and
  `reset()`.

Do not introduce application `streamRxEnd()`/`streamRxReset()` callbacks in a
later slice or leave mixed callback/event terminal reporting.

## Slice sequencing ledger

Each slice starts from the complete, green exit state of the preceding slice.
Do not begin code from a later slice early, and do not hand off a slice with
expected failures.  The only temporary production surface is
`Zhttp::Tunnel`: H3 compiled callers need it through Slice 2, and stale
documentation names it through Slice 4.  It is deleted in Slice 5.

| Slice | Entry state | Exit state handed to next slice |
| --- | --- | --- |
| 0 | Current `Tunnel`/`tunnel*` H2/H3 implementation | Current behavior is covered and green; no production API change |
| 1 | Slice 0 baseline | Final `Stream`/`StreamDispatch`, explicit-flush Tx boundary, event-bearing Rx contract, `BodyRx` terminal injection, and final link-operation names exist; temporary `Tunnel` delegates to them; parser callbacks remain `tunnel*` |
| 2 | Slice 1 mixed parser state | H2 parser, links, and tests use explicit Tx flush plus one event-driven `streamProcess`; H3 parser/tests still use tunnel receive terminology |
| 3 | Slice 2 | H3 parser, links, and tests also use explicit Tx flush plus event-driven `streamProcess`; no compiled caller uses `Tunnel`, but the temporary header remains for documentation handoff |
| 4 | Slice 3 H2/H3 implementation | H1 TCP/TLS Upgrade binds the same contract through no-copy `H1::StreamRx`, and the four-profile fixture is green |
| 5 | Slice 4 | All documentation and remaining consumers use `Stream`; `Tunnel` and every `tunnel*` symbol are deleted |
| 6 | Slice 5 clean implementation | Lifecycle, interop, leak, allocation, copy, and latency gates are complete in the current clang debug build |

At each handoff, record in the implementation change:

- the exact remaining `rg` matches for `Tunnel`, `tunnel`, and the new
  directional names;
- the focused tests run in that slice;
- whether any link/session lifecycle or scheduler continuation changed; and
- the next slice's allowed starting surface.

## Build discipline for every slice

Use the existing configured build exactly as it is: clang debug.  Throughout
this plan:

- do not run `z.config`, `configure`, `autoreconf`, or any equivalent
  reconfiguration;
- do not run `make clean`;
- do not switch to gcc, release, ASAN/LSAN, or another build configuration;
- do not rebuild unaffected libraries or rerun a broad build merely to
  establish a baseline;
- finish the coherent source changes for a slice before compiling;
- perform at most one planned incremental build at that slice's acceptance
  gate; and
- after a compile or test failure, rebuild only the affected target after the
  diagnostic fix.

Use these exact parallelism limits for every incremental library build:

```text
make -j8 <affected libraries other than zquic and zhttp>
make -j4 zquic
make -j3 zhttp
```

Do not build `zquic` merely because `zhttp` uses it; build it only if that
slice changes `zquic` source or an installed dependency that requires its
recompilation.  The planned implementation is confined to `zhttp`, its tests,
and documentation unless an acceptance test proves a lower-layer change is
necessary, so the normal slice gate is one incremental `make -j3 zhttp`.
Run all relevant tests against that completed incremental build without
rebuilding between tests.

## Slice 0 — Freeze the current behavior

### Entry state

This slice starts from the checked-in implementation:

- `ZhttpTunnel.hh` forwards to the five `tunnel*` methods on H2/H3 logical
  links;
- H2 `ParserState::Tunnel` and H3 `ParserState::Tunnel` deliver through
  `tunnelData(Rx &)`, `tunnelEnd()`, and `tunnelReset()`;
- H2 uses `BodyRx` over `H2_::EventRx`;
- H3 uses `BodyRx` inside the existing request-stream parser; and
- H1 has no logical-stream/Upgrade adapter.

Before production changes, capture the current behavior in tests.

Extend the existing `ZhttpH2MessageTest`, `ZhttpH2EngineTest`,
`ZhttpQPackDynamicTest`, and `ZhttpH3EngineTest` tunnel tests to record:

- local and peer capability independently;
- the current `Tunnel::send()` final flush behavior and multiple writes
  within one callback;
- no-copy Rx delivery from the existing pooled view;
- ordering of DATA before current local `Tunnel::end()`;
- exact-once remote end and reset;
- H2/H3 logical-stream closure not closing the shared connection;
- no application callback after the stream's terminal callback.

Where current lower-layer state already promises idempotent end/reset and
reset dominance, add assertions.  If it does not, record that as a Slice 2 or
3 delta rather than changing production code here.

Extend `ZhttpTransportContractTest` to freeze current `ZiRxLayer` event
semantics: `Start` is initially pending, `input()` raises `Input`, consuming
the final region raises `Final`, `events()` reads and clears flags, and a
refill failure raises `Error`.  Record that native `ZiRxStream` lacks this
event surface; do not change `zi` in this slice.

Extend `ZhttpParserTest`, `ZhttpH1EngineTest`, or a focused new H1 fixture to
record the current TCP/TLS behavior needed for Slice 4:

- unread bytes following the `101` headers remain in the native
  `ZiRxStream`;
- the H1 parser returns `Complete` at the end of the `101` header section
  without consuming following application bytes;
- a native Tx stream flush queues bytes before `disconnect()`; and
- disconnect is reported exactly once after pending Rx/Tx work drains.

Do not add `ZhttpStream.hh`, rename symbols, or change wire/lifecycle
behavior in this slice.  Do not write a mock test against a type that is not
introduced until Slice 1.

### Acceptance gate

- All new and existing focused tests pass against the current `Tunnel`
  implementation.
- The H2 and H3 baseline tests prove DATA-before-end, exact-once terminal
  notification, no-copy DATA delivery, and shared-connection isolation.
- The `ZiRxLayer` baseline proves the precise read-and-clear event ordering
  that the common Rx contract will preserve.
- The H1 baseline proves `101` leaves coalesced following bytes unread and
  records current Tx/disconnect ordering.
- Production files have no semantic change.
- The handoff record identifies any missing idempotence/reset-dominance
  behavior assigned to Slice 2 or Slice 3.

### `GUIDELINES.md` alignment audit and repair

Audit only the added tests and any test utilities for callback-scoped
ownership, no hidden copies/allocations, owner-shard access, deterministic
continuation-based completion, and absence of sleeps/polling.  Repair every
violation and leave the Slice 0 test suite green before starting Slice 1.

## Slice 1 — Install the final facade and link-operation boundary

### Entry state

Start only after Slice 0's baseline tests pass.  Production code still has
the original `Tunnel`, all H2/H3 link methods and parser callbacks are named
`tunnel*`, and H1 has no stream support.

Add `ZhttpStream.hh`, its public library include, CTAD guides, and the
Rx-bearing/Rx-less forms described above.  Add
`StreamDispatch<Link, Consumer>` in the same header or a directly included
internal header.

Install the final link-operation vocabulary in all four existing logical-link
specializations:

- H2 `ClientLogical` and `ServerLogical`;
- H3 `ClientLink<..., H3QUIC>` and
  `ServerLink<..., H3QUIC, ...>`;
- `streamPeerCap()`;
- `streamLocalCap()`;
- `streamTx(callback)`;
- `streamTxEnd()`; and
- `streamTxReset()`.

This slice is a mechanical operation-boundary rename, not the client/server
factoring work of Slices 2 and 3.  Remove the old five `tunnel*` link methods
after the new facade compiles.

Retain `ZhttpTunnel.hh` temporarily, but change `Tunnel<Link>` to contain or
construct `Stream<Link>` and forward its current public
`peerCap()`/`localCap()`/`send()`/`end()`/`reset()` calls to that facade.
Thus the stable new implementation never depends on old `tunnel*` link
methods; only unmigrated callers depend on the temporary wrapper.

The final `Stream::tx()`/link boundary does not call `flush()` around the
callback.  Normal `ZiTxLayer` destruction still flushes residual output at
scope exit, preserving Slice 0 behavior through temporary `Tunnel::send()`
without a compatibility-specific flush.  New `Stream` tests and every
migrated caller must flush explicitly at application message boundaries.

Extend `Zhttp::BodyRx` with callback-bearing final/error event injection in
this slice and test it in `ZhttpTransportContractTest`, but do not connect it
to the H2/H3 parsers until their respective migration slices.  This
establishes the final event mechanism before either protocol depends on it.

Add internal SFINAE/static-contract machinery:

- an Rx-bearing instance requires the complete common
  `input()`/`events()`/`consume()`/`empty()`/`complete()`/`failed()`/
  `available()` surface;
- every link requires capability, Tx, end, and reset operations;
- the Tx callback receives a `ZiTxStream`-compatible concrete object with
  `flush()`;
- unsupported methods fail at compile time at the link boundary.

Keep detection traits internal.  Do not use concepts or `requires`.  Add
`ZhttpStreamTest` (or extend `ZhttpTransportContractTest`) with mock
event-bearing Rx layers, H2/H3 client/server instantiations, CTAD checks,
`StreamDispatch::process()` callback-shape checks, explicit/multiple Tx flush
checks, and object-size checks.

Document explicitly that:

- `Stream` is non-owning and shard-affine;
- `StreamDispatch` is non-owning, is disabled before teardown, and is cleared
  only after Rx drain;
- Rx and Tx callback objects cannot be retained;
- `tx()` completion is not transport send completion;
- the application, not the facade, owns every Tx flush boundary;
- `streamProcess()` drains read-and-clear Rx events and payload;
- `end()`/`reset()` initiate asynchronous lower-layer work;
- the physical H2/H3 session outlives its logical stream.

### Acceptance gate

- The common facade compiles against H2 client/server and H3 client/server
  link types using only the final five `stream*` link operations.
- The mock contract verifies the exact public surface, CTAD, both Rx
  event paths, and the sole `StreamDispatch::process()` call.
- Object-size tests show one raw pointer for Rx-less `Stream`, two for
  Rx-bearing `Stream`, and only the link/consumer raw pointers for
  `StreamDispatch`.
- No buffer allocation, queue, copy, lock, atomic, or virtual call is added.
- All Slice 0 tests still pass through the temporary `Tunnel` wrapper.
- Direct `Stream::tx()` tests prove explicit flush causes immediate
  lower-layer handoff, multiple flushes remain distinct, and residual output
  is flushed exactly once by normal layer destruction; temporary
  `Tunnel::send()` retains its old observable scope-exit behavior.
- `BodyRx::start()` delivers empty `Start`; callback-bearing
  `offer()`/`finish()`/`fail()` deliver read-and-clear
  `Input`/`Final`/`Error`; `cancel()` remains silent.
- `rg` finds no `tunnelPeerCap`, `tunnelLocalCap`, `tunnelSend`, or link-side
  `tunnelEnd`/`tunnelReset` definitions; remaining `tunnel*` matches are the
  temporary wrapper, parser callbacks/state, tests, and documentation.
- No H2/H3 parser state or receive callback is renamed yet.

### `GUIDELINES.md` alignment audit and repair

Audit header structure, direct includes, SFINAE style, class/member layout,
name lengths, raw-pointer lifetime, dispatch-disable/drain ordering,
CRTP/static dispatch, and avoidance of STL/type erasure.  Confirm the
temporary wrapper points toward the final API, not vice versa.  Inspect
optimized output for the facade's forwarding hot path if inlining is not
obvious.  Repair all findings and rerun the focused contract plus Slice 0
tests before Slice 2.

## Slice 2 — Migrate H2 receive and factor H2 logical links

### Entry state

Start from Slice 1: H2/H3 links expose only final `stream*` link operations;
`Stream` and `StreamDispatch` are final; `Tunnel` delegates to `Stream`; both
parsers and their tests still use `ParserState::Tunnel` and receive-side
`tunnel*` callbacks.

Rename only H2 parser state and receive callbacks:

- `H2::ParserState::Tunnel` to `H2::ParserState::Stream`;
- `H2::Parser::tunnel()` to `stream()`;
- `tunnelData(Rx &)` to `streamProcess(Rx &)`;
- replace receive-side `tunnelEnd()` with `BodyRx` `Final` injection; and
- replace receive-side `tunnelReset()` with `BodyRx` `Error` injection.

Do not rename the corresponding H3 symbols in this slice.

Factor the duplicated H2 client/server behavior into one compile-time helper
or CRTP base that:

- implements the final Slice 1 link-operation surface;
- reads local and peer Extended CONNECT capability from the current logical
  Tx stream;
- creates the H2 DATA body Tx layer, invokes the callback, propagates
  application-issued flushes, and relies only on normal layer destruction
  for residual scope-exit flush;
- orders END_STREAM after queued DATA;
- cancels only the selected stream on reset;
- preserves `ClientLogical::disconnect()` and
  `ServerLogical::disconnect()` as the existing stream-cancellation
  primitives; and
- does not move session ownership into the facade.

Implement any H2 idempotence or reset-dominance delta recorded by Slice 0 in
the existing H2 logical-stream/session state, not in `Stream` or
`StreamDispatch`.

Bind one `StreamDispatch` in each H2 test/application parser owner before
calling `link.receive(parser, rx)`.  Its parser CRTP methods delegate
synchronously:

```c++
template <typename Rx>
void streamProcess(Rx &rx) { dispatch.process(rx); }
```

On DATA, END_STREAM, or reset, the H2 parser raises the corresponding
`Input`, `Final`, or `Error` flags in its existing `BodyRx` and invokes this
same callback.  The dispatch target receives only public
`streamProcess(Stream)`.  Disable dispatch before logical-link teardown and
clear its raw pointers in the Rx drain continuation.  Do not retain the
callback-scoped `BodyRx::Layer`.

Do not move H2 flow-control windows, DATA framing, HPACK, admission, or
scheduling into `Zhttp::Stream`.  Do not add locks to make logical streams
cross-shard callable.

Update H2 builders and parsers so `ParserState::Stream` is entered only after
a valid, capability-authorized Extended CONNECT handshake.  Ordinary CONNECT
and ordinary request/response bodies must retain their existing behavior.
Immediately after that transition, call `BodyRx::start(process)` so the
consumer receives `Start` and the common stream even if the peer sends no
DATA.

Migrate `ZhttpH2MessageTest` and `ZhttpH2EngineTest` to the new API in this
slice.  Keep README and `ws.md` edits for Slice 5 so Slice 2 does not edit
documentation that still has to describe unmigrated H3.  Do not retain H2
parser `tunnel*` forwarders.

### Acceptance gate

- The Slice 1 facade/dispatch contract remains unchanged and green.
- H2 Extended CONNECT client/server echo, half-close, reset, refusal, and
  shared-session isolation tests pass using public `Stream`.
- Explicitly flushed DATA preceding `end()` is observed before END_STREAM;
  the adapter adds no manual flush around the callback, and normal layer
  destruction flushes residual output exactly once.
- Rx remains callback-scoped and no-copy through `BodyRx`/`ZiRxLayer`.
- `Start`, `Input`, empty/non-empty `Final`, and `Error` are delivered
  through `streamProcess()` exactly once as applicable; no separate
  application terminal callback remains.
- H2 parser `complete(state)` performs only parser/engine cleanup after
  stream establishment and cannot duplicate `Final`/`Error`.
- H2 flow-control and frame tests are unchanged and pass.
- No H2 production or H2 test symbol contains `Tunnel` or `tunnel`.
- Ordinary H2 request/response regression tests pass.
- H3 Slice 0 behavior tests still pass unchanged through temporary `Tunnel`;
  their parser states and receive callbacks are still `tunnel*`.
- The H2 factor introduces no additional scheduler post, allocation, or copy.

### `GUIDELINES.md` alignment audit and repair

Audit Rx/Tx shard ownership, continuation ordering, duplicate client/server
blocks, iterator lifetime, buffer ownership, dispatch raw-pointer lifetime,
and exact-once terminal notification.  Verify that reset and end dispatch to
the existing owner-shard operations and that no code assumes
`run()`/`invoke()` completed synchronously.  Repair findings and rerun H2
focused tests plus the unchanged H3 baseline before Slice 3.

## Slice 3 — Migrate H3 receive and factor H3 logical links

### Entry state

Start from Slice 2: the facade and link-operation names are final; H2 is
fully stream-named and uses `StreamDispatch`; H3 links already implement the
new operation names but its parser/tests still use tunnel receive names; the
temporary `Tunnel` wrapper remains only for those unmigrated H3 callers and
documentation.

Rename only H3 parser state and callbacks:

- `H3::ParserState::Tunnel` to `H3::ParserState::Stream`;
- `H3::Parser::tunnel()` to `stream()`;
- `tunnelData(Rx &)` to `streamProcess(Rx &)`;
- replace `tunnelEnd()` with `BodyRx` `Final` injection; and
- replace `tunnelReset()` with `BodyRx` `Error` injection.

Apply the Slice 2 `StreamDispatch` pattern to H3 test/application parser
owners.  Factor only the duplicated H3 client/server link operations into one
compile-time utility:

- local and peer RFC 9220 capability;
- `H3::dataStream()` Tx construction, propagation of application-issued
  flushes, and normal residual scope-exit flush through layer destruction;
- request-stream FIN;
- STOP_SENDING/RESET_STREAM.

Keep DATA delivery, frame parsing, peer FIN detection, and reset detection in
their existing H3 parser locations.  At those same sites raise
`Input`/`Final`/`Error` in the existing `BodyRx` and delegate the one
`streamProcess(Rx &)` callback to `StreamDispatch`; do not move them into the
link utility.
Immediately after a valid Extended CONNECT transition to
`ParserState::Stream`, call `BodyRx::start(process)` so establishment
delivers the common stream even if no DATA follows.
Implement any H3 idempotence or reset-dominance delta recorded by Slice 0 in
the existing H3 request-stream/session state, not in the facade or dispatch
adapter.

Preserve the current proven H3 framing and Tx paths.  This is an interface
migration, not an H3 Rx/Tx rewrite:

- retain current QPACK integration;
- retain current H3 frame parsing;
- retain current `Zquic::Stream::txStream()` and buffer composition;
- retain QUIC flow control, retransmission, loss recovery, FIN, reset, and
  stop handling in `Zquic`;
- do not add an intermediate queue or copy.

Map existing QUIC terminal events exactly once:

- clean peer FIN raises application-visible `Zi::RxEvent::Final`;
- RESET_STREAM or applicable STOP_SENDING raises
  application-visible `Zi::RxEvent::Error`;
- a local `end()` finishes only the request stream;
- a local `reset()` posts the current Tx-owner stop/reset sequence.

Migrate `ZhttpH3EngineTest` and the H3 stream sections of
`ZhttpQPackDynamicTest` in the same slice.  Do not retain H3 parser `tunnel*`
forwarders.  Leave `ZhttpTunnel.hh`, its aggregate-header/build-list entries,
README, and `ws.md` present until Slice 5; at the Slice 3 exit no `.cc` caller
may explicitly instantiate `Tunnel`.

### Acceptance gate

- The unchanged Slice 1 facade contract and Slice 2 H2 tests remain green.
- The same `StreamDispatch` contract used for H2 compiles and runs for H3
  client and server links.
- H3 Extended CONNECT echo, FIN, reset, refusal, and shared-QUIC-connection
  isolation tests pass.
- qlog confirms explicitly flushed DATA-before-FIN and stream-local reset
  behavior.
- `Start`, `Input`, empty/non-empty `Final`, and `Error` reach the sole
  `streamProcess()` callback exactly once as applicable.
- H3 parser `complete(state)` performs only parser/engine cleanup after
  stream establishment and cannot duplicate `Final`/`Error`.
- Non-stream H3/QPACK framing and dynamic-table expectations remain unchanged
  and pass; stream-specific expectations change only in names/callback shape.
- No H3 production or H3 test symbol contains `Tunnel` or `tunnel`.
- `rg` finds `Tunnel`/`tunnel` only in `ZhttpTunnel.hh`, its
  `Zhttp.hh`/`Makefile.am` references, README, `ws.md`, top-level migration
  plans, or other non-compiled historical text.
- The established H3 data path has no new allocation, copy, queue, or
  scheduling turn.

### `GUIDELINES.md` alignment audit and repair

Audit the H3 utility and dispatch binding for CRTP factoring, raw
back-pointers, Rx/Tx separation, bounded work per scheduler turn,
continuation captures, exact-once terminal notification, and preservation of
pooled-buffer ownership.  Compare the diff specifically against the Slice 0
H3 Tx/Rx path and remove any accidental rewrite.  Repair findings and rerun
H3 focused tests plus the unchanged H2 gate before Slice 4.

## Slice 4 — Add H1 Upgrade stream adaptation

### Entry state

Start from Slice 3: H2 and H3 both use the final stream API and receive
vocabulary; no compiled code uses `Tunnel`; H1 generic
`Zhttp::ClientLink`/`ServerLink` still expose only their existing native
TCP/TLS process and Tx surfaces; the H1 parser still completes `101` as a
normal header-only response while leaving following bytes unread.

Implement the same link operations in the generic H1 TCP/TLS links.

Add one `H1::StreamBinding<Link, Consumer>` utility in `Zhttp`; do not put the
mode switch in `ztcp` or `ztls`.  The generic H1
`ClientLink<App, Impl, Profile>` and
`ServerLink<App, Impl, Profile, Session>` each own this binding, specialized
through their existing `App`/`Impl` types and shared by `H1TCP` and `H1TLS`.
It owns only H1 logical-stream state and a `StreamDispatch`.  Its explicit
states are `HTTP`, `Stream`, and `Terminal`, using a `ZtEnum` in accordance
with `GUIDELINES.md`.

The binding is responsible for:

- recording whether the application enabled the Upgrade profile;
- accepting an Upgrade only after the application has emitted/validated the
  protocol-specific request/response;
- changing its Rx-owned mode from `HTTP` to `Stream` only after the
  application has validated the complete Upgrade request/`101` response;
- marking local/peer capability at the appropriate handshake point;
- preserving unread native `ZiRxStream` bytes;
- binding that same native Rx stream to the no-copy `H1::StreamRx` layer and
  routing it immediately through `StreamDispatch::process(streamRx)` after
  the state transition;
- preventing any subsequent HTTP parser dispatch on that connection.

Expose two H1-establishment operations outside the common facade:

- `streamEnable(bool)` is protocol-specific configuration performed before
  the opening message and controls `streamLocalCap()`; and
- `streamAccept()` is called by the application header-completion handler
  only after it validates the Upgrade request/`101`, sets peer capability,
  and changes `HTTP` to `Stream`.

The application continues to use the existing H1 builder/parser to emit and
validate `Upgrade` and `Connection` fields.  Do not move WebSocket- or
profile-specific header rules into the generic binding.

Give generic H1 client/server links the already-final Slice 1 operation
surface:

- `streamLocalCap()` and `streamPeerCap()` read the binding state;
- `streamTx(callback)` obtains the existing link `txStream()`, invokes the
  callback, propagates application-issued flushes, and relies only on normal
  stream destruction for residual scope-exit flush;
- `streamTxEnd()` prevents new application writes and orders normal
  disconnect after pending Tx;
- `streamTxReset()` prevents new work and initiates abortive disconnect;
- peer EOF/disconnect raises `Final`/`Error` through the same
  `streamProcess()` event path used by H2/H3.

`Stream::rx()` returns the binding-owned `H1::StreamRx` layer.  That layer
temporarily points at the native TCP/TLS `ZiRxStream` only while the native
process callback is active and forwards span/advance operations without
copying.  It is not `BodyRx` and adds no queue.

Update the generic H1 link `process(rx)` path precisely:

1. if the binding is already in `Stream`, call
   `H1::StreamRx::process(rx, dispatch)` so it raises `Input` and calls
   `StreamDispatch::process(streamRx)`; do not call the application's HTTP
   process handler;
2. while it is in `HTTP`, call the existing application process handler;
3. if that handler called `streamAccept()`, invoke `streamProcess()` once
   with `Start` even if `rx` is empty, then synchronously expose any still
   unread bytes in that same native `rx` as `Input` before returning to the
   transport; and
4. in `Terminal`, consume nothing and initiate no new work.

The stream consumer must consume all input it accepts according to the native
process callback contract.  Do not loop in `Zhttp` merely because unread
input remains.

If the current TCP/TLS layer cannot distinguish graceful end from abort
without violating teardown ordering, keep both operations distinct in
`H1::StreamBinding` state but allow them to converge on the existing
physical `disconnect()` primitive.  A local end must first disable further
application Tx and order disconnect after the preceding `tx()` callback has
returned, including its explicit and residual scope-exit flushes.  `end()`
itself must not call `flush()`.  Completion is the later
continuation-driven disconnect notification.  Do not redesign `ztcp`,
`ztls`, or `zi` without a failing acceptance test proving that a lower-layer
addition is necessary.

On peer disconnect, raise exactly one terminal event in `H1::StreamRx` and
invoke `streamProcess()`: `Final` for graceful peer EOF, `Error` for known
abort/reset/failure.  Then prevent further stream dispatch and follow the
existing link/session disconnect path.  During engine teardown, prevent
ingress first, call `StreamDispatch::disable_()` on Rx, drain Rx, call
`final_()`, then advance to the existing Tx drain and completion
continuation.  Teardown after dispatch is disabled is silent; it does not
synthesize an application `Error`.  Do not infer synchronous completion from
`disconnect()`.

Transport-specific configuration and TLS behavior remain in their current
profile traits.  Migrate or add H1 Upgrade cases in `ZhttpParserTest` and
`ZhttpH1EngineTest`; do not edit H2/H3 machinery in this slice.

### Acceptance gate

- All Slice 1–3 contract, H2, and H3 focused tests remain green without
  source changes.
- One templated established-stream echo fixture runs over H1 TCP, H1 TLS, H2
  TLS, and H3 QUIC.
- Coalesced `101` and first application bytes are consumed without copying or
  losing bytes.
- H1 `tx()` uses the current pooled Tx stream and adds no allocation beyond
  the transport buffer, propagates explicit flushes, and adds no manual flush
  beyond normal stream destruction.
- Graceful end follows the completed Tx callback and its scope-exit flush
  before disconnect; reset terminates exactly once.
- `H1::StreamRx` delivers `Start`, `Input`, empty/non-empty `Final`, and
  `Error` with the same read-and-clear semantics as H2/H3 and without
  copying.
- H1 TCP/TLS engine, message, lifecycle, and interoperability tests pass.
- The established consumer contains no profile or transport branch.
- `H1::StreamBinding` never calls the H1 parser after entering `Stream`, and
  its terminal event is exact-once through the sole `streamProcess()`
  callback.
- No changes exist in `ztcp`, `ztls`, or `zi` unless an acceptance test
  demonstrated and documents the required lower-layer delta.

### `GUIDELINES.md` alignment audit and repair

Audit the H1 mode transition, owner-shard state, Tx completion assumptions,
dispatch disable/clear lifetime, disconnect sequencing, timer cancellation,
and three-phase teardown.  Confirm that no code assumes `run()`/`invoke()` or
`disconnect()` is synchronous and that the enum follows `ZtEnum` guidance.
Repair findings and rerun the four-profile fixture plus affected H1 tests
before Slice 5.

## Slice 5 — Migrate consumers and remove the tunnel API

### Entry state

Start from Slice 4: all compiled H1/H2/H3 tests and examples use
`Zhttp::Stream`; H2/H3 parser/link code has no tunnel terminology;
`ZhttpTunnel.hh` remains only as a temporary wrapper for stale documentation
or any repository consumer found by the final search.

Run a repository-wide search and migrate every remaining consumer or
reference, including:

- `zhttp/README.md`;
- `ws.md`; and
- any example, test, boundary script, installed-header list, comment, or
  non-migration design document discovered by a repository-wide search.

Top-level migration plans such as this file are historical implementation
records and necessarily name the removed API; exclude them from the product
vocabulary gate rather than rewriting their history.

Update `ws.md` so the WebSocket binding performs profile-specific
establishment and hands the same `Zhttp::Stream` to one codec.  Its established
Rx, Tx, ping/pong, close, end, and reset logic must not mention H1/H2/H3,
TCP/TLS/QUIC, HTTP DATA, or `Zhttp::Tunnel`.

The common WebSocket codec must:

- call `tx.flush()` after each complete encoded frame/message production
  boundary and before a following `end()`;
- drive Rx with `input()` followed by read-and-clear `events()`;
- consume `Input` without copying;
- treat `Final` as the sole graceful peer half-close indication;
- treat `Error` as the sole peer abort indication; and
- contain no separate protocol-specific end/reset receive callbacks.

Then delete:

- `ZhttpTunnel.hh`;
- `Zhttp::Tunnel`;
- its entry in `zhttp/src/Makefile.am` and its include from `Zhttp.hh`;
- stale tunnel examples, comments, and tests.

The link methods and parser callbacks were already removed in Slices 1–3;
finding one now is a failed earlier-slice handoff, not new Slice 5 migration
work.  Return to and repair the responsible slice before proceeding.  Do not
leave aliases, deprecated names, or compatibility forwarders.

### Acceptance gate

- `rg` over `zhttp/src`, `zhttp/test`, `zhttp/README.md`, `ws.md`, and other
  current product documentation finds no `Zhttp::Tunnel`, `ZhttpTunnel`,
  `tunnelData`, `tunnelEnd`, `tunnelReset`, `tunnelSend`, `tunnelPeerCap`,
  `tunnelLocalCap`, `ParserState::Tunnel`, application `streamRxEnd`, or
  application `streamRxReset`; archival top-level migration plans are
  explicitly outside this search.
- All dependent code uses `Zhttp::Stream`.
- The WebSocket plan describes one established-stream codec over all four
  profiles, explicit Tx flush boundaries, and the common Rx event loop.
- Every Tx example explicitly flushes complete application messages, and
  every Rx example uses the sole `streamProcess()` event path.
- Header installation and library exports contain `ZhttpStream.hh` and no
  removed tunnel header.
- The four-profile fixture and all focused H1/H2/H3 tests remain green.
- The single planned incremental `make -j3 zhttp` gate succeeds in the
  unchanged clang debug build; no unaffected library is rebuilt.

### `GUIDELINES.md` alignment audit and repair

Perform a repository-wide dead-code, compatibility-shim, naming, include,
duplication, API-boundary, and documentation/code agreement audit.  Delete
stale code rather than preserving it.  Confirm no later layer reimplements
framing, flow control, buffering, or teardown owned below it.  Repair all
issues and rerun affected focused tests before final verification.

## Slice 6 — Final lifecycle, interoperability, and performance verification

### Entry state

Start from Slice 5's clean vocabulary: `ZhttpStream.hh` is the sole installed
logical-stream header; all four profiles and all consumers use it; the
repository contains no `Tunnel`/`tunnel*` API; all focused tests are green
under the unchanged current clang debug configuration.

If the Slice 5 audit changed source after its build gate, perform one final
incremental `make -j3 zhttp`; otherwise do not rebuild.  Then run the focused
regression set:

- H1 TCP/TLS engine and lifecycle tests;
- H2 message, frame, session, engine, and interop tests;
- H3 state, engine, QPACK, QUIC interop, and qlog checks;
- the four-profile common stream fixture;
- service/agent shutdown tests affected by link lifecycle.

Exercise:

- local end, peer end, simultaneous end, and reset;
- explicitly flushed DATA immediately before end;
- multiple application flushes in one `tx()` callback;
- residual output at callback return, proving normal layer destruction
  flushes it exactly once and the adapter adds no second flush;
- `Start` with first input and with an initially empty stream;
- `Input` across multiple callback/refill boundaries;
- `Final` with and without final payload;
- `Error` before input, after input, and during an application callback;
- reset with pending Tx;
- peer terminal events during an application callback;
- engine stop with active logical streams;
- shared H2/H3 connection survival after one stream closes;
- no callback after terminal completion;
- repeated connect/open/exchange/close cycles.

Use deterministic synchronization, not sleeps.  Inspect the code diff first
for any regression.  For an unexplained crash use gdb first, then valgrind.
Do not create an ASAN/LSAN, gcc, or release build while executing this plan.

Compare allocation/copy instrumentation and latency before and after:

- the facade must inline away;
- H1 and H3 proven Rx/Tx paths must remain unchanged below the adapter;
- H2/H3 DATA must still be delivered from pooled buffers without gathering;
- H1 `StreamRx` must expose native spans without a queue or copy;
- no per-callback or per-DATA allocation is permitted;
- no additional scheduler hop is permitted on the normal input or output
  path.

### Acceptance gate

- All focused and full regression tests pass under the unchanged current
  clang debug build.
- No configure, clean, gcc, release, or sanitizer build was performed.
- Valgrind on representative repeated stream runs reports no leaks or invalid
  access.
- qlog confirms correct H3 FIN/reset ordering and connection survival.
- Allocation/copy counts and latency are no worse than baseline.
- Flush instrumentation proves one prompt lower-layer handoff per explicit
  application flush boundary, plus at most one normal residual scope-exit
  flush, with no additional facade-generated flush.
- Each profile delivers the same read-and-clear `Start`/`Input`/`Final`/
  `Error` semantics through only `streamProcess()`.
- Engine `stop(done)` completes only after active logical streams and pending
  Rx/Tx work drain, followed by `final()`.
- The final repository state is exactly the Slice 5 API state; verification
  has not introduced compatibility shims, diagnostic-only source
  workarounds, or protocol-specific branches in consumers.

### `GUIDELINES.md` alignment audit and repair

Perform the final full alignment audit: style, naming, compile-time dispatch,
allocation, copying, buffer pooling, Rx/Tx ownership, continuations,
three-phase teardown, timer cancellation, exact-once callbacks, test
determinism, and dead-code removal.  Trace teardown explicitly as:
disable new work, post/drain Rx, post/drain Tx, release ownership/notify the
completion continuation.  Repair every finding and rerun only the affected
focused tests plus the final regression gate.

## Completion criteria

The migration is complete only when:

- `Zhttp::Stream` is the sole application-facing established logical-stream
  abstraction;
- it directly provides Rx access, callback-scoped Tx access, local end/reset,
  and local/peer capability;
- every application-visible Rx object exposes the same `Zi::RxEvent` flags
  and stream operations, with `Final`/`Error` as the sole peer-terminal
  notifications;
- the application explicitly controls every semantic/message Tx flush
  boundary, normal stream destruction handles only residual scope-exit
  output, and the facade adds no extra flush;
- H1 TCP, H1 TLS, H2 TLS, and H3 QUIC satisfy one compile-time contract;
- dependent established-protocol logic is transport- and HTTP-version
  independent;
- H2/H3 framing and flow control remain in their current lower layers;
- H1/H3 proven data paths have not been rewritten;
- the old tunnel API and terminology are deleted;
- shutdown is asynchronous, continuation-ordered, and leak-free; and
- tests demonstrate no new allocation, copy, queue, lock, virtual dispatch,
  scheduler hop, or latency regression on the data path.
