# Native transport repair execution status

The user explicitly authorized repairing the TCP stale disconnect and checking
equivalent TLS/QUIC bugs. TCP/TLS header fixes are implemented and verified by
seven TCP and eight TLS native scopes. These include delayed stale entry,
replacement after initial close processing, and (TLS) refusal of the successor
before handshake while the predecessor has a pending close. QUIC review found
no equivalent demonstrated defect; no QUIC change was made.

Pending-close authority is revoked at successor association. Deferred cleanup
checks that authority on each owner; TLS retirement also checks its existing
generation. Each physical close completes hub accounting after its local
reference-retirement posts, regardless of whether logical cleanup is obsolete.
Those local posts neither flush the old socket nor gate successor activation.

Verification reused the configured Linux x64 Clang/ASan Zfix build. No dependency
library rebuild or configuration change was made. Logs:

- `/tmp/zfix-transport-close-staged-build.log`: native integration build passes.
- `/tmp/zfix-transport-close-tcp-staged.log`: seven TCP scopes pass.
- `/tmp/zfix-transport-close-tls-refusal-build.log`: final TLS fixture build passes.
- `/tmp/zfix-transport-close-tls-staged.log`: eight TLS scopes pass.
- `/tmp/zfix-transport-close-unit.log`: 29 programs / 328 scopes pass.
- `/tmp/zfix-transport-close-managed-tcp.log` and
  `/tmp/zfix-transport-close-managed-tls.log`: five programs / 97 scopes per
  transport pass.

The historical source review below predates those fixes. Its stale TCP cleanup
and missing TLS accounting defects are repaired. Subsequent physical queue handoff and TLS target-binding repairs now pass
the expanded native gates recorded at the end of this file. Early FIX
association remains open; these checks do not establish the entire plan. Old source line numbers below identify
the reviewed snapshot rather than current definitions.

# Persistent-link transport prerequisite audit

Historical read-only source review of `zfix_repair2.md` section 3, before
the explicitly authorized header fixes and verification recorded above. The handoff below is a
proposed invariant, **not a verified implementation or a completed prerequisite**.

## Existing ownership and mechanisms

- `Ztcp.hh:113–124` and `Ztls.hh:248–260`: each physical `Cxn` owns its
  link reference. `disconnected()` moves that reference out once, retaining the
  owner through posted disconnect work. Repeated physical notifications no
  longer have a link reference to dispatch.
- `ZiMultiplex.cc:522–529`: each `ZiConnection` owns distinct Rx/Tx
  `ZiIOContext`s, and each context has its original `cxn` pointer.
- `ZiTx.hh:46–92`: the queue belongs to the **link** (`ZiTx<Impl>`), not the
  socket. Socket-send setup pushes into that shared queue; only the transition
  from an empty queue initializes that socket's context. The active callback
  holds a raw queue-head pointer and shifts the same shared queue.
- `ZiIOContext.hh:78–82`: `complete()` clears the callback and data pointer
  without disconnecting. It must run on the context's owner before releasing
  buffers referenced by that context.
- `ZiMultiplex.cc:2609–2650`: `ZiConnection::close()` invokes the abrupt
  close path; it sets old Tx down and then closes on I/O Rx. It does not call
  graceful `disconnect()`, emit TLS close-notify, or flush queued bytes.
  `disconnect()` is a distinct shutdown path at `2483–2553`.
- A close is asynchronous: queued owner dispatch is not a synchronous socket
  barrier. A delayed `ZiConnection::send_()` still invokes its setup callback;
  only the subsequent `send()` checks Tx-up (`1988–2020`). Therefore a closed
  socket alone does not prevent a late setup callback from changing the queue.
- Existing `ZmList` move/clean operations can detach or release an old physical
  batch without a permanent retired-connections container (`ZmList.hh:322–344,
  701–706`). `clean()` only releases nodes; it does not call `sent(false)`.

The `ZiTx` queue contains socket representations, including partially sent
buffers and encrypted TLS records. These are not transferable logical work.
Persistent FIX assignments/application work must remain in their logical owners;
discarding obsolete physical records must not erase those logical queues.

## Concrete TCP defects

`Ztcp.hh:276–336` currently installs a successor and closes the predecessor,
but predecessor disconnect handling still unconditionally:

1. clears disconnect status and reports link-state changes, even when the
   initial pointer comparison did not match;
2. crosses App Tx and I/O Tx, then cleans the shared socket queue;
3. calls `connectClosed_()`/`disconnected(peer)`, cleans current Rx input and
   updates hub completion accounting.

An early stale-pointer return is insufficient. A close can be current at its
first Rx step, then become obsolete before its queued Tx/final Rx steps.
Clearing `m_cxn` at the first step also loses the identity needed to distinguish
two successive closing connections when both finish after a newer association.

The queue aliases independently of that callback bug: B's first send can find
A's nonempty queue and append without initializing B's context (`ZiTx.hh:60–70`).
A's cleanup then drops B's records, or A's active callback advances the shared
queue into B's records. Keeping the queue without isolating the old context is
therefore unsafe; merely removing `clean()` is not a solution.

## Concrete TLS defects and existing resource support

- `Ztls.hh:794–841` has an initial current-connection check. Once it passes,
  later `clearTx_()`, shared socket-queue cleanup and final `reset_tls_()`/
  Rx-stream cleanup are unconditional. A successor installed between stages
  can lose its Tx TLS state, physical output, Rx TLS state or input.
- The initial stale return also skips `linkDisconnected_()`. Server
  `connected_1()` increments the physical pending count for each association;
  `linkDisconnected_()` decrements it (`2580–2592`). Obsolete connections must
  release that acquired obligation once without gaining current-link authority.
  TCP has the same accounting split at `1055–1067`.
- `m_tlsGen` and Tx-owned `m_txTLSGen` already identify TLS epochs. Existing
  install/error paths check them (`573–632`, `1103–1130`). Capture the applicable
  generation for later cleanup, and free only that generation on Tx. Do not read
  Rx's current pointer from Tx as a cleanup guard.
- `reset_tls_()` (`1322–1349`) already retires pending asynchronous TLS state
  through `AsyncJob`. The retired job keeps its TLS pointer until its completion
  frees it (`1201–1258`); generation/pointer checks prevent it from resuming a
  successor handshake. This supports asynchronous old-resource reclamation
  without waiting for that job before starting the successor.
- Client `connectIP_()` resets TLS and sets SNI **before** socket association
  (`1570–1588`). If overlapping client attempts are enabled, merely attempting
  B can destroy A's current TLS state before B connects. Fresh TLS/SNI setup
  must belong to successful association, before B's handshake. Acceptor setup
  already resets in its association hook (`1694–1696`).

## Minimal proposed handoff, without a per-Cxn queue

Reusing the single physical queue is possible only if its ownership transfer is
atomic on I/O Tx and every send is bound to one physical socket. Required steps:

1. On Rx, capture the predecessor handle and make the newly associated socket
   current. Preserve the predecessor identity in its existing captured handles.
   No further stale close may change successor state. Capture old telemetry
   state before moving `m_cxn`; the current code reads it after clearing the
   predecessor pointer and can report an artificial Down-to-Up transition.
2. Before any B socket-send setup can enter the shared queue, execute one I/O-Tx
   handoff: abruptly mark A down, complete **A's** Tx context, and retire A's
   physical queue. Never complete B's context or clean a queue that already
   belongs to B. This is bounded local owner work, not a drain/flush or a wait
   for A's network/application callbacks.
3. Enqueue B's first socket-send setup after that handoff. A late setup callback
   must inspect its own `io.cxn`/closed-socket status and settle its original
   attempt **before touching the shared queue**. Existing `ZiTx` does not do
   this. Also eliminate re-resolution of the destination between admission and
   socket submission: Link `send_()` currently checks `m_cxn`, then `ZiTx::send`
   asks for `impl()->cxn()` again. An already executing A send must not resolve
   to B midway through encryption/publication.
4. Carry the original socket and TLS-generation identities through every close
   continuation. On Tx, perform only generation-specific TLS retirement. On
   final Rx, perform current-link callbacks/reset/reconnect policy only if the
   captured socket is still authoritative. Separately release each physical
   hub count/reference once, including obsolete closes.
5. For an ordinary current close, retain its identity until final cleanup, using
   the existing current handle plus disconnect phase rather than clearing it
   too early. A successor overwrites current authority immediately. Keeping the
   closing handle prevents an older A finalizer from treating an empty pointer
   after B's close as authority to clean/restart the link. No additional
   generation or pending-candidate collection is needed for this Rx check.

The existing `ZiConnection::send_()` callback can access its own context on I/O
Tx, so completing A does not inherently require a new public close mode.
Whether to expose a narrow transport helper or adjust `ZiTx` internally remains
an implementation decision. The single queue is reusable only after these
ordering requirements are proved; otherwise connection-owned physical queues
are the natural ownership fix, not something Zfix should conceal.

## Proof gaps that still block enabling overlap

- Show the exact producer ordering for I/O Tx handoff versus B sends, including
  an App-Tx send already executing during Rx replacement. A first-stage boolean
  check is not enough. Acquire socket handles on their proper owner and carry
  them through existing posts; do not add convenient foreign-shard pointer reads
  or mirror pointers to hide this gap.
- Settle old physical attempts once. `clean()` itself supplies no send-failure
  callbacks. Zfix Wire close can finish old logical frame attempts, but each
  transport consumer's callback/byte-accounting contract must be checked rather
  than assuming silent release equals completion. If callbacks are emitted,
  detach the obsolete batch before callouts so reentry cannot retire B's queue.
- Prove old contexts are inert before their raw buffer pointers are released,
  including queued writable events. Current Linux send entry checks Tx-up;
  this source review is not Windows/IOCP validation.
- Prove every counted physical association decrements once even if obsolete,
  pre-handshake failure occurs, or terminal hub stop overlaps replacement.
  Preserve connection refs until all queued old callbacks that need them retire.
- Verify TLS old async completion, generation-scoped Tx retirement and final Rx
  reset cannot touch B/C. Failed outbound connection attempts must not retire A
  through a generic `connectRetired_()` application hook before association.

These are prerequisite dependency correctness changes, outside the current
Zfix edit/build scope. No source inspection or proposed algorithm here proves
that the current transport implementation can safely enable displacement.

## Zfix-only wrapper assessment

Some dispatch uses CRTP and can be intercepted by a derived Zfix link, but that
does not expose enough of the transport transition to implement a small complete
wrapper:

- Native `connected_1`, `disconnected_0`, `disconnected_`, `disconnected_1`
  and `m_cxn` are private (`Ztcp.hh:228–336`, `Ztls.hh:424–841`). An override
  cannot delegate to the private base implementation to install or retire the
  current socket. A public `cxn()` getter does not supply mutation authority.
- Overriding TCP `disconnected_(cxn)` cannot stop its enclosing cleanup chain:
  that caller ignores its result and still queues `txQueue.clean()`. Overriding
  final `disconnected_1` can protect FIX state, but the shared queue has already
  been cleaned before that hook.
- The optional `connectSocket_()` hook occurs after native current-pointer
  replacement. It can observe B and schedule work, but cannot prevent an already
  executing A App-Tx send from resolving B during that earlier window.
- A Zfix `send_` override could dispatch a physical handle through Rx, but the
  existing `ZiTx::send` still obtains `impl()->cxn()` internally. There is no
  bound-Cxn entry point to preserve that snapshot. Copying the sender, replacing
  the whole Cxn/transport implementation, or introducing specialization/private
  access tricks is not the proposed minimal framework repair.

Consequently this review has not identified a legal **small** Zfix-only wrapper
that proves safe overlap through the existing hooks. That is an API/ownership
finding, not a claim that a separate custom transport could never be written.
The prerequisite remains open until a sanctioned handoff path is implemented
and validated within an explicitly authorized dependency scope.

Physical hub completion must also remain distinct from successor activation:
the successor must not wait for A's callbacks, but terminal hub stop must not
finish merely because a stale early-return balanced `m_pending` while A still
has owner callbacks accessing that hub. Accounting belongs after A's own local
retirement obligations; it does not authorize clearing B's state.

## Logical reset ordering review

Separate from native transport handoff, managed replacement must preserve an
already committed logical epoch reset. A permitted ordering is Tx committing
E+1 and posting ResetReady for A, followed by Rx associating B before consuming
that notice. Simply clearing Rx reset metadata and opening B at E would reject
the later notice as stale while Tx rejects B's old-epoch events. This finding
requires reconciliation of the logical reset result across replacement, with a
controlled continuation regression. It is not authority for a new physical
connection to reset the epoch. The managed implementation review is still open.

## FIX association boundary to integrate with the transport repair

Native association precedes TLS readiness, but current Zfix wrappers receive
`transportUp_()` only at `connected(Connected)`. Until that notification, a
replaced FIX connection's generation can still be current to Rx. A native
handoff repair must therefore coordinate the association boundary with FIX
invalidation and owner-local publication gating; fixing the final stale-close
notification alone is insufficient. Existing `connectSocket_()` runs after
native installation and may provide the early application hook, but the current
sender/queue ownership still needs the sanctioned handoff described above.

Do not open FIX before TLS authentication, invent a connecting FIX phase, or
wait for the predecessor to drain. Keep logical queued work for the successor,
and preserve logical reset completions independently of physical callback
authority. This integration remains unimplemented and is not proved by the
managed transportUp replacement unit tests.


## Physical queue handoff — reproduced and repaired candidate

A new Linux TCP `partialReplacement` case uses an actual raw peer with a small
receive buffer that does not read. The server reaches a real kernel EAGAIN,
then associates B on the same persistent link. Before repair B becomes ready
but receives no reply: its first output is appended behind A's orphaned queue
head without initializing B's I/O context. The TAP runner returns 1; evidence:
`/tmp/zfix-transport-partial-before-tap.log` (seven old scopes pass, the new
scope fails). Socket sizes constrain the kernel rather than fabricating a
partial send or EAGAIN.

The candidate posts abrupt A close and A context/physical-queue retirement on
I/O Tx before publishing B. A's context is completed before its queued buffers
are released. Existing pending-close references cover A even when its initial
disconnect step already cleared the current pointer. Late A setup rejects the
closed physical connection before touching the queue. No new queue, registry,
retired list, generation or queue-owner data member is added. No predecessor
callback/drain is awaited.

This requires a small generic `ZiTx` correction: its starter checks the actual
context connection's existing `up()` state. The old starter ran even after close
and could repopulate a retired queue. `ZiTx` also gains a bound-target send
entry point so already prepared physical representations do not re-resolve the
link's current connection. The original current-target send remains meaningful
for senders whose representation is not yet connection-specific. The direct
fake connection fixture now supplies a real base I/O context identity; it has
not been rebuilt outside the authorized Zfix scope.

The first candidate passes eight TCP scopes (including actual partial-output
replacement) and the existing eight TLS scopes:
`/tmp/zfix-transport-handoff-tcp.log`,
`/tmp/zfix-transport-handoff-tls.log`. Those runs precede the additional bound
TLS-target change and closed-setup regression. They are intermediate evidence,
not verification of source still being edited.

Source review separately confirms that App-Tx encryption can straddle Rx
replacement: after encrypting with A's detached TLS state, finalization formerly
re-resolved B as its target. The next repair binds the socket to the Tx-owned
TLS state and carries the Rx target with handshake output. The regression will
hold an already encrypted A record, associate B, then release A and require B
handshake/traffic to succeed. Early FIX association integration remains separate.


## Bound TLS target and closed-setup regression results

The candidate now binds detached Tx keys and their Cxn with the same Tx-owned
lifetime. Application records, key updates and close-notify target that Cxn;
Rx handshake output snapshots its target before serialization. The existing
Tx-install acknowledgement also checks that captured Cxn before publishing
readiness. TCP snapshots its target once before submitting a physical attempt.
The one added TLS member is an encryption-state target, not a second registry
or an independent current-connection generation.

With source frozen, eight TCP and nine TLS native scopes pass:
`/tmp/zfix-transport-bound-final-tcp.log` and
`/tmp/zfix-transport-bound-final-tls.log`. The pressure fixture now waits for
producer submission after real EAGAIN, separating queued-output handoff from a
producer still streaming across replacement. The earlier unfenced producer run
failed its flush during displacement and is preserved in
`/tmp/zfix-transport-bound-tcp.log`; it is not accepted as a passing run.

Negative controls remove only the closed-starter gate (TCP) or bound TLS send
target (TLS) from the candidate while retaining all other fixes. Exactly the
new corresponding scope fails, with TAP runner status 1:
`/tmp/zfix-transport-closed-gate-negative.log` and
`/tmp/zfix-transport-binding-negative.log`. Fixed headers were restored byte
for byte before those negative binaries ran. They are being rebuilt for the
final affected unit/integration run; no negative binary is final evidence.

Early FIX association/start integration remains unimplemented. Its concrete
owner/state/reset split and required regression matrix are recorded in
[ASSOCIATION_REPAIR.md](ASSOCIATION_REPAIR.md). This phase does not claim full
managed replacement completion, Raw overlap, or a new full process matrix run.


## Restored-source affected verification

The repaired headers were restored byte for byte before final rebuild. The
final `make -C zfix/itest -j4` completes; the full unit/schema target passes
29 programs / 328 scopes. Six TCP integration programs pass 105 scopes and
six TLS programs pass 106 scopes, including the new native regressions.
Commands reuse the current configuration and dependency artifacts:

- `make -C zfix/test -j4 test`
- in `zfix/itest`, `prove -v ./ingress_tcp_test ./tcp_test ./session_tcp_test
  ./recovery_tcp_test ./raw_tcp_test ./reset_tcp_test`
- in `zfix/itest`, `sh ./tls-test.sh -v ./ingress_tls_test ./tls_test
  ./session_tls_test ./recovery_tls_test ./raw_tls_test ./reset_tls_test`

Logs: `/tmp/zfix-transport-final-unit.log`,
`/tmp/zfix-transport-final-itest-build.log`,
`/tmp/zfix-transport-final-tcp.log`, `/tmp/zfix-transport-final-tls.log`.
Configuration checksum verification against the original baseline passes.
There were no dependency library builds, reconfiguration, SDK edits, or
platform expansion. The broad interop/example workload matrix has not been
rerun for this phase; the early association implementation is still required.
