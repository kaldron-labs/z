`Zquic` packet send linearization
=================================

`sendInitialPkt_()` and nearby send helpers are harder to read than the
underlying work requires. The current implementation composes packet
protection, Initial/Handshake coalescing, path admission/accounting,
application gating, and final datagram send through nested synchronous
lambdas.

The nesting is explainable, but it obscures that there is no asynchronous
boundary in the common path. It also makes stack-local state such as server
`recordRefs` look more fragile than it is, because callbacks visually resemble
deferred work even though they are executed synchronously today.

Current implementation audit
----------------------------

The current code has these nested send chains:

- client Initial:
  `sendProtInitialPkt_()` -> `holdInitialForCoalesce_()` ->
  `sendPathPkt_()` -> `Endpoint::send()`;
- client Handshake:
  `sendProtHandshakePkt_()` -> `sendHandshakeCoalesced_()` ->
  `sendPathPkt_()` -> `Endpoint::send()`;
- client Short:
  `sendProtShortPkt_()` -> `sendPathPkt_()` or
  `sendPathProbePkt_()` -> `Endpoint::send()`;
- client `flushCoalescedInitial_()` repeats
  `sendPathPkt_()` -> `Endpoint::send()`;
- server Initial:
  `sendProtInitialPkt_()` -> optional `app()->sendFrame()` loop ->
  `holdInitialForCoalesce_()` -> `sendPathPktApp_()` ->
  `sendPktPath_()`;
- server Handshake:
  `sendProtHandshakePkt_()` -> `sendHandshakeCoalesced_()` ->
  `sendPathPktApp_()` -> `sendPktPath_()`;
- server Short:
  `sendProtShortPkt_()` -> optional `app()->sendFrame()` loop ->
  `sendPathPktApp_()` or `sendPathProbePktApp_()` -> `sendPktPath_()`;
- server `flushCoalescedInitial_()` repeats
  `sendPathPktApp_()` -> `sendPktPath_()`.

`sendPktPath_()` is server-specific application gating for whole datagrams:
it calls `app()->sendPkt(buf)` first, then `sendPktRaw_()` only if the app
accepts the datagram. `sendPathPktApp_()` and `sendPathProbePktApp_()` keep a
local `bool sent` so path accounting is updated only when a datagram is
actually sent.

`app()->sendFrame()` is different: it gates individual recorded frames. It is
currently used by server Initial and Short sends, but not by server Handshake
sends. The cleanup should not silently invent or remove this behavior. Audit
that discrepancy before changing it:

- if Handshake `recordRefs` can include app-gated frames, add the same
  `sendFrame()` check there;
- if Handshake intentionally bypasses frame gating, document that with a named
  helper or local comment-sized structure in the refactor;
- in either case, keep `recordRefs` stack-local and visibly non-escaping.

Goals
-----

1. Make packet send flow read in packet order:
   - build payload;
   - protect packet;
   - optionally hold/coalesce;
   - check path/application admission;
   - send datagram;
   - reserve path/congestion accounting after accepted send;
   - record Tx metadata.

2. Keep the current Tx-thread ownership model:
   - all helpers remain direct Tx-thread helpers;
   - no new locks or cross-shard state access;
   - no heap allocation for helper state;
   - no change to packet protection, ACK accounting, or loss tracking
     semantics.

3. Replace nested call-site lambdas with named endpoint-local helpers where the
   behavior is endpoint-specific.

Guideline constraints
---------------------

This refactor is in the hot Tx packet path, so apply the repository
guidelines strictly:

- keep the design CRTP/template based; do not introduce virtual dispatch,
  concepts, `requires`, STL containers, or compatibility shims;
- keep helpers Tx-thread owned and trailing-underscore named; public
  cross-shard wrappers are not needed for these internal send paths;
- do not add locks or atomics to make cross-shard access acceptable;
- do not add heap allocation for helper state, scratch arrays, closures, or
  frame/reference lists; continue using existing `ZiIOBuf` pooled buffers;
- do not add refcount churn from short-lived packet/helper objects back to
  longer-lived links, apps, endpoints, or streams;
- do not capture large or variable-size state in lambdas; after the cleanup,
  remaining lambdas should capture only `this` or small fixed metadata and
  should be shallow enough to match the lambda layout guidance;
- keep packet/frame data movement in place; do not introduce temporary
  contiguous copies around packet protection, coalescing, or datagram send;
- keep optional app hooks as direct CRTP-style calls through `app()`/`impl()`
  and side-effect-safe defaults.

Target shape
------------

Split the endpoint-specific "protected packet is ready, now send it" behavior
out of `sendInitialPkt_()`, `sendHandshakePkt_()`, and the analogous short
packet paths.

Client-side examples:

```
bool sendPathBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendPathProbeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendInitialBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendHandshakeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendShortBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
```

Server-side examples:

```
bool sendPathBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendPathProbeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendInitialBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendHandshakeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
bool sendShortBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr);
```

The client/server variants should hide the existing difference between:

- client `Endpoint::send()`;
- server `app()->sendPkt()` plus `sendPktRaw_()`;
- client `Base::sendPathPkt_()`;
- server `Base::sendPathPktApp_()`.

Implementation plan
-------------------

1. Add client Tx-thread helpers for final datagram send:
   - `sendPathBuf_()` wraps `Base::sendPathPkt_()` and `Endpoint::send()`;
   - `sendPathProbeBuf_()` wraps `Base::sendPathProbePkt_()` and
     `Endpoint::send()`;
   - these should be the only client helpers that directly call
     `Endpoint::send()`.

2. Add client Tx-thread helpers for coalesced long packets:
   - `sendInitialBuf_()` wraps `Base::holdInitialForCoalesce_()` and then
     `sendPathBuf_()`;
   - `sendHandshakeBuf_()` wraps `Base::sendHandshakeCoalesced_()` and then
     `sendPathBuf_()`.

3. Use those helpers from client `sendInitialPkt_()` and
   `sendHandshakePkt_()`:
   - keep `Base::sendProtInitialPkt_()` and
     `Base::sendProtHandshakePkt_()` responsible for protection and Tx
     metadata;
   - pass only a shallow lambda that calls the named helper, or split the
     base helper further if that produces cleaner code;
   - avoid nested lambdas at the `sendInitialPkt_()` call site.

4. Apply the same cleanup to client short packet send paths:
   - route ordinary short packets through `sendShortBuf_()`;
   - route PMTUD/probe short packets through `sendPathProbeBuf_()`, because
     those paths use different path accounting;
   - update client `flushCoalescedInitial_()` to use `sendPathBuf_()` instead
     of repeating the nested final-send chain.

5. Add server Tx-thread helpers mirroring the client shape:
   - keep the existing `sendPktPath_()` logic but rename or wrap it as the
     server final datagram sender;
   - `sendPathBuf_()` should own the local `bool sent` plumbing and
     `Base::sendPathPktApp_()` call;
   - `sendPathProbeBuf_()` should own the same local `bool sent` plumbing and
     `Base::sendPathProbePktApp_()` call;
   - `sendInitialBuf_()` and `sendHandshakeBuf_()` should own coalescing and
     call `sendPathBuf_()`;
   - update server `flushCoalescedInitial_()` to use `sendPathBuf_()` instead
     of repeating the nested final-send chain.

6. Normalize server `recordRefs` application gating:
   - keep the optional `SentFrameRef` to local `TxPktRefs` conversion at the
     top of each send helper, before invoking the base protection helper;
   - introduce a small synchronous helper such as
     `sendFrameRefs_(const TxPktRefs *)`;
   - use that helper from server Initial and Short where the current code
     already calls `app()->sendFrame()`;
   - decide explicitly whether server Handshake should also use it, since the
     current code does not;
   - keep any `recordRefs` pointer visibly stack-local and non-escaping;
   - preserve current ordering unless deliberately changed: packet protection
     currently runs before `app()->sendFrame()`, and `recordProtPktTx_()` runs
     only after the final send helper accepts the datagram.

7. Revisit `Base::sendProtInitialPkt_()`,
   `Base::sendProtHandshakePkt_()`, and `Base::sendProtShortPkt_()` after the
   endpoint helpers exist:
   - if the remaining allocator/send callbacks are still noisy, split each
     helper into "protect into buffer" plus "record protected packet Tx";
   - then endpoint send helpers can call those steps linearly without a
     generic callback trampoline;
   - do this only if it simplifies the code materially, because the protection
     helpers also centralize important Tx metadata updates.

8. Keep coalescing state in `Base`:
   - `beginLongCoalesce_()`, `holdInitialForCoalesce_()`,
     `sendHandshakeCoalesced_()`, `flushCoalescedInitial_()`, and
     `endLongCoalesce_()` should stay shared unless client/server behavior
     genuinely diverges;
   - the cleanup should reduce callback nesting, not duplicate packet
     coalescing rules.

Review points
-------------

Check these invariants while refactoring:

- `Initial` padding to `MinUDPPayload` remains client-only where it is today.
- Initial and Handshake coalescing behavior is unchanged.
- Path amplification/congestion accounting still checks before send and
  reserves bytes only after a datagram is accepted for send.
- Server `app()->sendPkt()` gating still happens before raw datagram send.
- Server `app()->sendFrame()` behavior is preserved exactly, except for any
  deliberate Handshake change made after auditing the current omission.
- `recordProtPktTx_()` and `ackSentTx_()` still run only after the packet has
  been accepted by the send path.
- `sendPathProbePkt_()` and `sendPathProbePktApp_()` still use probe path
  accounting; do not route probes through ordinary `sendPathPkt_()`.
- No callback captures stack pointers that appear to outlive the function
  frame.
- All new helpers assert `app()->txInvoked()` or use an existing called helper
  that asserts it.

Acceptance criteria
-------------------

The cleanup is acceptable only if all of the following are true:

- client and server `sendInitialPkt_()` bodies no longer contain nested
  coalescing/path/final-send lambdas; they delegate protected-buffer send to
  named Tx helpers;
- client and server `sendHandshakePkt_()` and `sendShortPkt_()` receive the
  same treatment where they currently repeat the nested send chains;
- client and server `flushCoalescedInitial_()` reuse the same named final-send
  helpers as ordinary Initial/Handshake paths;
- PMTUD/probe packets still route through `sendPathProbePkt_()` or
  `sendPathProbePktApp_()`, never through ordinary path accounting;
- the only remaining callbacks passed to `sendProtInitialPkt_()`,
  `sendProtHandshakePkt_()`, and `sendProtShortPkt_()` are shallow allocator
  or named-helper trampolines, unless those base helpers are split into
  explicit protect/record steps;
- no new helper accesses Rx-owned state from Tx or Tx-owned state from Rx;
- no new lock, atomic, heap allocation, STL container, virtual function,
  concept, or `requires` expression is introduced;
- no new reference is retained from short-lived packet/send helper state to
  longer-lived owners beyond the existing `this`/raw-pointer local pattern;
- server `app()->sendPkt()` and `app()->sendFrame()` hooks are called in the
  same observable circumstances as before, except for a deliberate and
  documented decision about the current server Handshake `sendFrame()` gap;
- `recordProtPktTx_()`, `ackSentTx_()`, PTO scheduling, packet-number
  increment, path reservation, and congestion/path diagnostics remain
  observationally unchanged for accepted and rejected sends;
- packet bytes and packet-number protection output are unchanged for Initial,
  Handshake, Short, close-frame, ACK-only, stream/control, coalesced, and PMTUD
  packets in the cases covered by existing tests;
- the final diff follows local hard-tab indentation and does not introduce
  deep multiline lambda indentation that the refactor was meant to remove;
- `make -C zquic/test test`, `make -C zhttp/test test`, and
  `git diff --check` pass on Linux.

Suggested verification
----------------------

Build and run the module test targets:

```
make -C zquic/test test
make -C zhttp/test test
```

The QUIC runtime coverage should include packet-drop and frame-drop hooks,
including the server `sendPkt()` and `sendFrame()` paths. If the Handshake
`sendFrame()` behavior is changed, extend the tests to cover that explicit
decision.

The HTTP test target should include integrated HTTP/3 coverage. If it does not
exercise a representative matrix case for Initial/Handshake coalescing and
server application send gating, also run:

```
libtool exec ./zhttp/test/zhttpmatrix --case=zhttp-zhttpd/h3/j5n10 --timeout=10 --stall-timeout=5 --quiet-timeout=0 --debug
```

If available in the local test suite, also run a short-timeout or packet-loss
HTTP/3 case that exercises retransmission/coalescing after dropped Initial,
Handshake, and Short packets.

Finish with:

```
git diff --check
```
