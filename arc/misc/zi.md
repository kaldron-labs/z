# Zi message-stream substrate plan

## Scope

Provide the generic stream capabilities required by framed, message-oriented
protocols layered over `Ztcp`, `Ztls`, and multiplexed transports.  This work
belongs below WebSocket and must not contain WebSocket opcodes, masking, close
codes, handshake fields, or other protocol-specific policy.

The current baseline is:

- `ZiTxStream` allocates pooled `ZiIOBuf` output, sends a buffer on rollover,
  and sends a partial buffer on `flush()` or destruction;
- its CRTP callback is `sendBuf_(ZmRef<ZiIOBuf>)`, so an upper layer cannot
  distinguish rollover from final flush;
- `ZiTxLayer` composes fixed headroom and tailroom over a lower stream;
- `ZiRxStream` owns a queue of input buffers and offers synchronous
  `consume()` framing and delivery callbacks; and
- `Ztls` currently requires application plaintext `skip` to equal its
  negotiated TLS headroom.

Preserve pooled buffers, in-place operation, shard ownership, and the current
no-copy single-buffer paths.  Apply `GUIDELINES.md`.

## Tx boundary signaling

Change the `ZiTxStream` CRTP contract to:

```text
sendBuf_(ZmRef<ZiIOBuf>, bool final)
```

`final` is false when appending more data rolls a full buffer and true when
explicit `flush()`, `<< Zi::flush()`, or destruction sends the current
buffer.  Propagate the flag through `ZiTxLayer`; layers that do not attach
message semantics may ignore it.

Retain these behaviors:

- a buffer that becomes exactly full remains current until more data arrives
  or the stream is flushed;
- appending beyond capacity sends the current buffer with `final = false` and
  continues in a new buffer;
- flushing a non-empty current buffer sends it with `final = true`;
- destruction is equivalent to final flush for pending output; and
- no empty buffer is sent by the generic stream.

An upper message layer may represent an empty message by allocating or
activating an empty buffer before final flush.  Keep that policy out of
`ZiTxStream`; ordinary byte streams must not start emitting empty buffers.

Update every in-tree CRTP implementation and test.  This is a deliberate
breaking contract change; do not add a legacy forwarding overload.

## Composed headroom and tailroom

Permit an upper framing layer to reserve its maximum header size and prepend a
shorter actual header at buffer finalization.  The unused reserved prefix
remains outside `ZiIOBuf::data()`/`length`; it must not be compacted or copied.
Each lower `ZiTxLayer` prepends its own header immediately before the active
span and preserves its configured tailroom.

Audit lower streams for assumptions that `skip` equals their minimum
headroom.  In particular, relax the `Ztls` application-Tx invariant from:

```text
skip == tlsHeadroom
```

to:

```text
skip >= tlsHeadroom
```

Validate the complete input and output bounds, record capacity, AEAD overlap,
alignment, and negotiated overhead before encrypting.  `ptls_send()` must
continue reading the active plaintext span and writing the serialized record
at `data_()` in place.  TCP already transmits from the active span and should
not need payload movement.

Do not weaken assertions unrelated to unused upper-layer headroom.

## Bounded Rx stream layers

Add `ZiRxLayer`, a bounded, refillable layer that presents a logical message through the
normal `ZiRxStream` consumption interface while a lower decoder retains
control of framing.  The layer must:

- expose payload buffers and spans without contiguous assembly;
- cap consumption at the current logical message boundary;
- refill by asking the owning decoder for more payload when the current
  buffer or protocol fragment is exhausted;
- allow the decoder to consume hidden framing and control records between
  payload fragments;
- never expose hidden framing, control bytes, or bytes from the next message;
- preserve mutable in-place payload access;
- report message start, newly available input, and final completion without
  exposing lower protocol fragment boundaries; and
- remain synchronous and shard-affine.

Use a stack/scoped layer or embedded state.  Do not allocate one wrapper object
per input span and do not copy input into a second queue merely to manufacture
the view.  The lower decoder remains responsible for lifetime and for
advancing the native `ZiRxStream` only as the application consumes payload.

The current native receive contract has no asynchronous application
pause/resume-read facility.  Initial message views therefore require the
application to consume or copy offered input before returning.  If a future
protocol needs retained asynchronous consumption, add a bounded native
pause/resume contract first; do not permit unbounded queue growth, polling, or
cross-shard locks.

## Verification

Extend `zi/test` and the affected transport tests to cover:

- the exact `final` sequence for partial flush, exact-capacity flush,
  multi-buffer rollover, repeated flush, move, and destruction;
- propagation of `final` through one and multiple `ZiTxLayer` instances;
- unchanged behavior for layers that ignore `final`;
- maximum reserved headroom with every shorter active header size;
- nested headroom/tailroom through TCP and TLS without payload movement;
- TLS encryption with extra unused headroom for every supported cipher and
  record-size boundary;
- bounded Rx consumption from one buffer, many buffers, and incremental
  refill;
- hidden records between payload fragments;
- refusal to consume beyond a logical message boundary; and
- cleanup with partially consumed input and during transport disconnect.

Rebuild from the top level with `make -j8` after changing the generic stream
contracts, then run the focused `zi`, `ztcp`, and `ztls` tests.
