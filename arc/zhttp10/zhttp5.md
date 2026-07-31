# Zhttp dependent stream API normalization plan

## Objective

Normalize the `Zhttp` dependent-facing stream contracts before `zws` is
implemented.  Preserve the repository-wide rule:

- Tx is obtained through `txStream()` or, where a borrowed lower layer makes
  direct return unsafe, callback-scoped `txStream(fn)`;
- Rx is owned below and passed by reference into a dependent `process(...)`
  callback; and
- protocol layers use `ZiRxLayer` and `ZiTxLayer` when they present a decoded
  or framed view over another stream.

This plan owns only the required `zhttp` changes.  `ws.md` is a subsequent
dependent plan and starts only after this plan is complete.

## Current codebase and gap

The lower layers are already consistent and are not changed:

| Layer | Tx path | Rx path |
| --- | --- | --- |
| `Ztcp::Link` | `txStream()` | owns `m_rxStream`; calls `impl()->process(m_rxStream)` |
| `Ztls::Link` | `txStream()` | owns decrypted `m_rxStream`; calls `impl()->process(m_rxStream)` |
| `Zquic::Stream` | `txStream()` | owns ordered `m_rx`; calls `impl()->process(m_rx)` |
| H2 wire transport | obtains the TLS link's `txStream()` | implements `process(Ztls::RxStream &)` |
| ordinary `Zhttp` links | expose `txStream()` | receive the concrete transport Rx stream in `process()` |

`Zquic::Stream::rxStream()` exists for owner-side explicit re-entry after
FIN/reset notification.  It is not the normal dependent delivery contract and
does not justify adding an Rx accessor to TCP or TLS.

The current upgraded logical-stream facade differs:

- `Zhttp::Stream<Link, Rx>` bundles link control and Rx;
- dependents receive `streamProcess(Stream{link, rx})`;
- the Rx accessor is `rx()`; and
- callback-scoped Tx access is `tx(fn)`.

The implementation is already no-copy and correctly maps:

- H1 Upgrade to unread native TCP/TLS input and drained connection close;
- H2 Extended CONNECT to DATA/END_STREAM/RST_STREAM; and
- H3 Extended CONNECT to DATA/QUIC FIN/RESET_STREAM/STOP_SENDING.

Normalize only the dependent surface.  Do not redesign those data paths.

## Two dependent-facing HTTP stream families

### HTTP message bodies

An HTTP body stream represents the finite entity body of one request or
response:

- `Zhttp::BodyRx` is a `ZiRxLayer` passed by reference to the application's
  body callback;
- H1 fixed/chunked body Tx, H2 DATA Tx, and H3 DATA Tx are `ZiTxLayer`s passed
  to body producers/builders;
- content length, chunking, HTTP DATA framing, body completion, and ordinary
  message completion remain owned by `zhttp`; and
- H2 flow control remains in `zhttp`, H3/QUIC flow control remains in
  `zquic`, and H1 has no flow control.

This plan does not change the body API or its proven Rx/Tx paths.  Body tests
are regression gates because the upgraded-stream implementation reuses
`BodyRx` and H2/H3 DATA layers.

### Upgraded binary logical streams

An upgraded logical stream represents generic reliable ordered bytes after H1
Upgrade or H2/H3 Extended CONNECT.  It has no application protocol semantics.
The target dependent contract is:

```c++
template <typename Link>
class Zhttp::Stream {
public:
  bool localCap() const;
  bool peerCap() const;

  template <typename Fn>
  void txStream(Fn &&);              // callback receives borrowed Tx stream

  void end();                        // ordered graceful local completion
  void reset();                      // abort logical stream
};

template <typename Stream, typename Rx>
int process(Stream stream, Rx &rx);  // Rx is passed, never obtained from link
```

The dependent callback may be an overload alongside ordinary HTTP
`process(Link &, Rx &)`: its first argument is the `Zhttp::Stream<Link>`
facade, so the two roles remain unambiguous at compile time.

Use the established return convention: positive means progress, zero means
await more input, and negative means dependent-protocol failure.  The
dependent must consume or copy every currently offered callback-scoped span
before returning.  `StreamDispatch` maps a negative result to `reset()` for
the affected logical stream; it does not turn zero/positive results into
transport flow control.

`txStream(fn)` is callback-scoped because H2 and H3 DATA `ZiTxLayer`s borrow a
method-local lower Tx object.  Do not return a dangling layer.  An owning
composite that permits zero-argument `txStream()` is outside this plan and is
unnecessary for `zws`.

## Protocol neutrality

Production `zhttp` owns generic HTTP negotiation and byte-stream capability,
not the protocol carried over an upgraded stream.  It must contain no
dependent-protocol:

- ALPN identifier;
- H1 Upgrade value;
- Extended CONNECT `:protocol` value;
- subprotocol;
- framing, masking, opcode, or close semantics.

`zws` will supply `Upgrade: websocket`, `:protocol = websocket`, WebSocket
headers, and WebSocket framing.  Generic zhttp tests may use values such as
`opaque` or `websocket` as parser test data; no such value may select
production behavior.

HTTP transport ALPN (`http/1.1`, `h2`, and `h3`) remains library-owned and is
not affected by this rule.

## Invariants and non-goals

- No changes to `ztcp`, `ztls`, `zquic`, or their public APIs.
- No changes to H1, H2, or H3 wire framing.
- No changes to H1/H3 proven Rx or Tx algorithms beyond dependent callback
  adaptation.
- No new queue, payload copy, gathering, allocation, virtual dispatch, type
  erasure, scheduler hop, lock, or flow-control logic.
- No compatibility overloads for `streamProcess()`, `Stream::rx()`, or
  `Stream::tx()`.  Update all dependents and tests atomically.
- No hardcoded WebSocket behavior in `zhttp`.
- Preserve explicit application `flush()` as the message/latency boundary.
- Preserve `end()` ordering behind queued Tx and exact-once `Final`/`Error`
  delivery.
- Preserve continuation-based engine and link teardown; no synchronous
  completion assumptions.

## Build and diagnostic discipline

Use the current configured clang debug build exactly as it is.

- Do not run `z.config`, `configure`, `autoreconf`, or `make clean`.
- Do not switch to gcc, release, ASAN/LSAN, or another configuration.
- Do not rebuild `ztcp`, `ztls`, or `zquic`; this plan changes only `zhttp`,
  its tests, and `zhttp/README.md`.
- Finish each coherent source slice before compiling.
- Use at most one planned incremental build for a source-changing slice:

```text
make -C zhttp -j3
```

- After a failure, inspect the diff first and rebuild only the affected
  zhttp target after repair.
- Diagnose an unexplained crash with gdb first, then valgrind.  Use an
  sanitizer build only if neither identifies the regression.

## Slice 0 — Freeze and audit the current contracts

### Entry state

Start from the committed `Zhttp::Stream<Link, Rx>` implementation with all
current zhttp tests green.

### Work

Record the current call graph for each profile:

```text
H1 native process(RxStream &)
  -> H1::StreamBinding
  -> StreamDispatch
  -> dependent streamProcess(Stream<Link, Rx>)

H2 Ztls::process(RxStream &)
  -> H2 wire parser
  -> H2 message parser / BodyRx
  -> StreamDispatch
  -> dependent streamProcess(Stream<Link, Rx>)

H3 Zquic::Stream::process(RxStream &)
  -> H3 request parser / BodyRx
  -> StreamDispatch
  -> dependent streamProcess(Stream<Link, Rx>)
```

Record the existing allocation, copy, scheduler-hop, flush, and terminal-event
tests that prove:

- H1 exposes coalesced bytes following the final Upgrade header without copy;
- H2/H3 expose pooled DATA spans without another queue;
- explicit Tx flush propagates once;
- `end()` is ordered after flushed output;
- reset affects only the H2/H3 logical stream;
- shared H2/H3 connections survive one logical-stream close; and
- `stop(done)` runs after logical streams and pending Rx/Tx work drain.

Do not modify production source and do not rebuild in this slice.

### Acceptance criteria

- The exact public and internal symbols to remove or rename are listed.
- Every current dependent and test implementation of `streamProcess`, `.rx()`,
  and `.tx(...)` is accounted for.
- Body-stream callers are separately identified and excluded from API
  migration.
- No lower-layer source change is proposed.

### `GUIDELINES.md` alignment audit and repair

Audit the proposed shape against the CRTP, naming, layering, Rx/Tx shard
ownership, borrowed lifetime, allocation, and teardown guidance.  Repair this
plan before handoff if it implies an Rx accessor, retained borrowed stream,
lock, synchronous wait, compatibility shim, or lower-layer ownership leak.

## Slice 1 — Normalize the logical-stream API atomically

### Entry state

Start from Slice 0's complete inventory.  All existing behavior is covered,
and the body API is explicitly outside the migration.

### Production changes

Update `zhttp/src/ZhttpStream.hh`:

- reduce the public facade to `Stream<Link>`;
- remove the stored Rx pointer and `Stream<Link, Rx>` specialization;
- rename public `tx(fn)` to `txStream(fn)`;
- retain `localCap()`, `peerCap()`, `end()`, and `reset()`;
- keep compile-time validation of the link and callback-supplied Tx stream;
- make `StreamDispatch::process(Rx &rx)` call the dependent
  `process(Stream{link}, rx)` overload, return its `int`, and reset the
  logical stream on a negative result; and
- retain the two borrowed back-pointers and the existing
  `disable_()`/`final_()` teardown discipline.

Update the H1 adapter in `ZhttpH1Stream.hh` and `ZhttpLink.hh`:

- keep `H1::StreamRx` as the same no-copy `ZiRxLayer`;
- pass that layer separately to the dependent `process(stream, rx)`;
- retain a side-effect-safe compile-time default for ordinary HTTP
  applications that never call `streamAccept()`; this is a CRTP base default,
  not an old-API compatibility shim;
- preserve immediate re-entry with unread bytes after `streamAccept()`;
- preserve exact-once Start/Input/Final/Error behavior;
- preserve `txStream(fn)` construction over the native TCP/TLS Tx stream;
- preserve Tx -> Rx ordered H1 `end()` teardown; and
- preserve reset as H1 connection abort.

Update H2 in `ZhttpH2Message.hh` and `ZhttpH2Engine.hh`:

- leave `Wire_::process(Ztls::RxStream &)` and wire Tx unchanged;
- leave `BodyRx` and `H2_::DataStream` unchanged;
- route Extended CONNECT DATA Rx through `StreamDispatch` into
  `process(stream, rx)`;
- expose H2 DATA Tx through `txStream(fn)` without adding a flush;
- preserve END_STREAM/RST_STREAM and flow-control behavior; and
- name any unavoidable parser-only forwarding hook `streamRx_`; it is
  internal plumbing and must not become a second dependent-facing callback
  convention.

Update H3 in `ZhttpH3.hh` and `ZhttpH3Engine.hh`:

- leave the request-stream parser, `BodyRx`, `H3::DataStream`, QPACK, and
  `Zquic::Stream::process(RxStream &)` unchanged below the adapter;
- route Extended CONNECT DATA Rx through `StreamDispatch` into
  `process(stream, rx)`;
- expose H3 DATA Tx through `txStream(fn)` without adding a flush;
- preserve QUIC FIN and RESET_STREAM/STOP_SENDING mapping; and
- name any parser-only forwarding hook `streamRx_` and keep it internal.

Update all zhttp tests and `zhttp/README.md` in the same source change.  Delete
the old names rather than forwarding them.

### Test changes

Update the common fixtures so the same dependent consumer template implements:

```c++
template <typename Stream, typename Rx>
int process(Stream stream, Rx &rx);

stream.txStream([](auto &tx) {
  tx << bytes;
  tx.flush();
});
```

Keep explicit tests for:

- Start with and without immediately coalesced Input;
- multiple refills and explicit read-and-clear events;
- Final with and without final payload;
- Error/reset before and after input;
- no callback after terminal completion;
- multiple explicit Tx flushes and residual scope-exit flush;
- flushed data immediately followed by `end()`;
- reset with pending Tx;
- engine stop with active logical streams; and
- shared H2/H3 connection survival.

Perform one incremental `make -C zhttp -j3`, then run the affected focused
tests without rebuilding between them.

### Acceptance criteria

- Product and test code contain no dependent-facing `streamProcess`,
  `Stream<Link, Rx>`, `.rx()`, or `Stream::tx(...)`.
- Every profile invokes the same `process(Stream<Link>, Rx &)` consumer
  overload.
- Every profile supplies callback-scoped Tx through `txStream(fn)`.
- H2 wire transport still implements `process(Ztls::RxStream &)` and obtains
  `txStream()` from TLS.
- HTTP body API signatures and tests are unchanged.
- Focused H1 TCP/TLS, H2, H3, body, transport-contract, and lifecycle tests
  pass in the unchanged clang debug build.
- No lower library was rebuilt or modified.

### `GUIDELINES.md` alignment audit and repair

Audit and repair naming, CRTP dispatch, type lengths, direct dependencies,
hard-tab layout, member ordering, borrowed lifetimes, Rx/Tx ownership,
allocation/copy behavior, scheduler hops, and teardown.  In particular:

- `StreamDispatch` must disable ingress before finalizing its consumer;
- no callback-scoped Rx/Tx object may be retained;
- terminal work must use the existing continuation drain;
- the facade must remain allocation-free and inlineable; and
- no compatibility wrapper may survive.

After any repair, rebuild only the affected zhttp target and rerun its focused
tests before handing off.

## Slice 2 — Separate body and upgraded-stream contracts and verify

### Entry state

Start from Slice 1's green normalized API.  No old dependent logical-stream
surface remains.

### Work

Audit the complete zhttp surface and documentation:

- message-body Rx remains `BodyRx`/`ZiRxLayer` passed to body callbacks;
- message-body Tx remains the existing H1/H2/H3 `ZiTxLayer` passed to
  producers/builders;
- upgraded-stream Rx is passed to `process(stream, rx)`;
- upgraded-stream Tx is obtained with `txStream(fn)`;
- neither family reimplements framing or flow control owned below it; and
- upgraded streams remain generic opaque bytes.

Add or update a source-boundary test over production `zhttp/src` that rejects:

- `websocket` and `Sec-WebSocket-*`;
- WebSocket opcodes, masking, and close codes; and
- dependent-protocol ALPN, H1 Upgrade, or Extended CONNECT identifiers.

Permit protocol-neutral HTTP field parsing and arbitrary identifiers in test
fixtures.

Run:

- all focused H1/H2/H3 logical-stream and body tests;
- the boundary and transport-contract tests;
- the full `make -C zhttp/test test` regression;
- representative repeated H1, H2, and H3 logical-stream runs under valgrind;
  and
- the existing H3/qlog stream lifecycle test.

Do not rebuild before these tests unless Slice 2 repairs source.

### Acceptance criteria

- The full zhttp regression is green in the unchanged clang debug build.
- Representative H1/H2/H3 runs report no invalid access or logical-stream
  leak.
- No production zhttp source contains dependent-protocol policy.
- Body and upgraded-stream contracts are distinct and documented.
- No normal Rx/Tx data path gained an allocation, copy, queue, gather,
  scheduler hop, or implicit flush.
- Explicit `flush()` remains the prompt lower-layer handoff boundary.
- `end()`/`reset()` mappings and continuation-based `stop(done)` teardown are
  unchanged.
- `ws.md` can begin without modifying `ztcp`, `ztls`, `zquic`, or `zhttp`.

### `GUIDELINES.md` alignment audit and repair

Perform the final repository-wide zhttp audit for dead code, compatibility
surface, naming, include hygiene, duplicated profile logic, protocol leakage,
flow-control ownership, no-copy behavior, latency, and orderly teardown.
Repair every finding and rerun the affected focused tests plus the full
regression before declaring the plan complete.

## Completion handoff to `ws.md`

Hand off only this surface:

```text
Zhttp::Stream<Link>
  localCap()
  peerCap()
  txStream(fn) -> borrowed concrete Tx stream
  end()
  reset()

dependent process(Zhttp::Stream<Link>, Rx &)
```

At handoff:

- all four profiles have identical dependent control flow;
- HTTP body streams remain unchanged;
- upgraded bytes carry no dependent protocol identity;
- `ws.md` owns all WebSocket handshake identifiers and semantics; and
- `ws.md` is prohibited from changing lower layers without a separately
  demonstrated defect and owning-layer plan.
