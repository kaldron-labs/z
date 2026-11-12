# Zquic ZiTx dependency and submission audit

Audit of the current working source for `zquic_repair.md`, including its ZiRx
addendum. No Zi production changes or builds were made for this audit. Frozen
contract fingerprints are in [zquic-zitx-baseline.json](zquic-zitx-baseline.json).
ZiRx is intentionally outside that fingerprint set because the combined plan
reopens its framing implementation.

## Queue and completion contracts

- `ZiTx.hh` exposes `txQueue`, with one intrinsic `ZiTxBuf` node per packet.
  The queue owns a reference; `ZiTxContext::buf` independently holds the active
  backing. `send` sets `owner` to its CRTP implementation before dispatching.
  All final admission and priority insertion must therefore occur on mx Tx.
- `ZiConnection::send` uses `txInvoke`; on-owner invocation executes inline.
  `send_` runs the setup callback then calls the physical send loop. Capacity
  checking immediately before `ZiTx::send` cannot interleave with completion
  on the same owner. A nonempty queue includes the active head.
- Use `ZiTxQueue::Iter i{txQueue}; i(); i.pushNode(buf.ptr());` inside a bounded
  iterator scope to insert immediately after the active head. `pushNode` takes
  its own reference. Successive such inserts place the newest priority follower
  first; never unshift ahead of the active packet. No private links are needed.
- `sending(io)` runs initially and whenever a successor becomes active. It must
  overwrite both destination and TOS, including the empty NotECT TOS value.
- A negative completion invokes `sent(head, false)`, cleans remaining followers,
  and completes the context. Followers do not individually receive `sent`.
  A successful complete packet invokes `sent(head, true)` once, installs the
  next head and invokes `sending`. Queue-cap rejection inside ZiTx also calls
  `sent(false)`, so endpoint admission must reject/count pressure explicitly
  first to avoid misclassifying it as a socket failure.
- Native epoll `sendmsg` EAGAIN returns without touching offset, buffer, address,
  TOS or callback. EPOLLOUT subsequently services the same installed context.
  EINTR retries the same operation. Normal successful UDP output consumes the
  entire payload in one syscall. Socket failures pass through `errorSend`;
  UDP ECONNREFUSED/ECONNRESET report the failure without closing the socket,
  while other errors retain the physical close policy.

## Backend contract violation: positive short UDP result

Source and focused executable fixtures characterize invalid backend input:
ZiTx adds every nonnegative result to `io.offset`; a positive result smaller
than `io.buf->length` leaves the head installed. `ZiConnection::send` then
retries `data()+offset`, even for UDP. Thus a backend producing a positive short
UDP result would cause suffix datagram output. The native UDP syscall normally
has an all-or-error contract, but generic plugin completion has no UDP guard.
A UDP backend must report the whole datagram or an error. A positive short
completion is a backend defect; no ZiTx repair is required. The injected short
result characterizes that contract violation rather than supported UDP behavior.

## Backing and retirement

The current native Tx implementation is synchronous on Linux and Windows:
`sendmsg`/`WSASendMsg` return before completion processing. Unlike receive IOCP,
there is no native pending Tx OVERLAPPED hold in the current connection.
`ZiConnection::close_1` completes the Tx context on mx Tx before posting Rx
socket teardown. Endpoint queue cleanup must follow context retirement and
owner drain; it must not clear the active callback's head prematurely.

`ZiTxContext::complete` clears its callback and backing reference. The source
does not implement saved Tx contexts or a generic kernel-pending Tx reference
registry. Plugins can own references independently through their existing
contract; a fake provider should demonstrate retained ownership until its own
retirement. The prose in `docs/IO.md` describing saved Tx contexts/TxDataDone
does not describe the current `ZiIOContext.hh` representation and is not
evidence of such a facility. Do not infer kernel retirement solely from an
application callback.

## Protected packet rejection and physical endpoint selection

- Initial, Handshake, Early and AppData protectors in `ZquicLink.hh` all record
  protected packet/recovery references before submission. Each synchronous
  false return calls `discardProtPktTx`, removing only that packet's record
  and releasing its congestion reservation. Packet numbers remain consumed,
  preserving nonce uniqueness. Plaintext stream/CRYPTO ownership remains above
  the completed-packet queue and still needs ACK/retransmission retirement.
- Async submission returns true after posting. Later local queue rejection
  must retain recovery records for PTO/loss rather than invent a synchronous
  false return, retire protocol data on socket progress, or close every link
  sharing the endpoint. `sendPath*` anti-amplification accounting follows
  successful submission/posting and is conservative for later local drops.
- Client send lambdas return `Endpoint::send`; server `sendPktPath` returns
  `sendPktRaw` and reports its admitted/posted result through `sent`. Existing
  application drop hooks intentionally acknowledge a simulated local loss;
  preserve their behavior and retained records.
- The original off-owner `Endpoint_::send` posts `buf`, address and ECN, with
  `buf->owner` pointing at the endpoint. Its later `send_` obtains **current**
  `m_cxn` and current generation. A pre-rebind posted packet can therefore
  migrate to a replacement physical endpoint. Bind/snapshot its physical
  endpoint/generation when posting and reject stale work on Tx; final
  `owner` handoff to EndpointCxn still occurs only on admission. Preserve
  owner-drain lifetime and do not add buffer-to-owner strong references.
- `App_::sendRetry` currently uses ordinary `sendPkt_` without priority. Give
  that helper a priority argument and request priority for Retry, preserving
  its app filter and successful-output diagnostics.
- Long Initial/Handshake coalescing deliberately retains a recorded Initial
  before actual submission. A later coalesced datagram rejection discards the
  current Handshake record through its protector; the previously recorded
  Initial remains under its existing PTO/recovery ownership. Inspect this in
  loss/recovery coverage; do not mistake that retained plaintext for a leaked
  completed packet or discard an earlier record without protocol evidence.

## Focused verification entry points

`ZquicSockTest` already contains the real EndpointCxn synthetic-completion
fixture; migrate that fixture to ZiTx hooks and queue/context assertions while
retaining its socket plan, ECN and rebind tests. Cover mixed metadata, direct
and posted calls, held head plus FIFO/repeated priority insertions, capacity
including the active head, rejected-buffer release, negative/short results,
EAGAIN preservation, and close/rebind late completion.

`ZquicBufferTest` is the existing allocator target; assert Tx node identity
separately from Rx/plaintext and exercise builtin/stub backing geometry there.
`zquic/itest/zquictxqueuetest` is the existing UDP working-pair fixture. Its
README refers to an obsolete drain callback although the current test sends
three packets directly; update it with sustained output, endpoint isolation,
pressure/rebind/stop and fairness entry points. Full QUIC working-pair recovery
and HTTP QIR remain separate required gates, beyond this source audit.

Run library rebuild before tests and use the shared eight-job make jobserver.
Existing baseline logs under `/home/ubuntu/rebuild-evidence` are historical;
they do not establish current migration completion. Under a continuously
writable endpoint, ZiConnection drains the entire bounded queue in one turn;
profile its configured 4096-packet maximum alongside a scheduled task and a
second endpoint. A green functional test alone does not establish latency.

The migrated `ZquicSockTest` now exercises the actual ZiTx setup and endpoint
admission through a holding plugin. Its positive-short characterization injects
one byte of completion for a two-byte UDP packet and checks that offset one
remains installed on the same head. This deliberately records the dependency
limitation; it does not submit a suffix datagram. The fixture also covers mixed
metadata, FIFO/repeated priority, all 4096 admitted references plus rejection,
negative completion cleanup and independent physical retirement/backing holds.
Test execution results are recorded separately by the execution coordinator.

## Executable short-result evidence

The final `zquictxqueuetest` adds a native common-writer characterization using
its existing test-only syscall interposer. It injects a first positive short
result of 1 for a 4-byte UDP payload without invoking the kernel, then observes
the unchanged writer call sendmsg with the original pointer plus 1 and length
3. The receiver gets that real 3-byte suffix, while endpoint diagnostics record
one completed 4-byte datagram. This characterizes a backend violating the UDP all-or-error contract; it is
not a ZiTx defect and requires no ZiTx repair. Native kernel
UDP still uses an all-or-error contract; the injected result models a backend
violating that contract. The test adds no production writer or Zi changes.
Evidence: `/home/ubuntu/zquic-zirx-execution/zquic-itest-short-evidence.log`;
final endpoint integration suite now has four top-level cases, all passing.
