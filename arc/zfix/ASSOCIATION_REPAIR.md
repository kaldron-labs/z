# Early connection association repair notes

Status: implementation and authorized current verification are complete.
Managed session TCP58/TLS64 includes A→B→C with accepted Work/pending load,
independent SIDE traffic, partial A framing, TLS pre-authentication failure/C
and terminal-stop cases. The final unit/schema result is 29 programs/347 scopes;
final integration is 13 programs/261 scopes (TCP127, TLS132, demo2), including
current recovery TCP19/TLS18. Managed real-EAGAIN output is TCP-only; native
TLS target/output regressions remain separate. Current consumer/example and
canonical76/lifecycle24/probe6/SDK-policy4/native18 checks also pass.
[REPAIR2_AUDIT.md](REPAIR2_AUDIT.md) records current proof and historical limits.

This file records the execution contract for `zfix_repair2.md` sections 3 and 5,
not new DNS/connect cancellation, receive-limit, persistence or business policy.
`docs/GUIDELINES.md` remains authoritative. Defect descriptions explicitly marked
original or pre-repair explain the starting state, not current failures.

## Demonstrated original boundary

Before the repair, `CliLink::connected()` (`ZfixClient.hh`) and
`SrvLink::connected()` (`ZfixServer.hh`) called `transportUp_()` only after
native authentication; neither wrapper overrode `connectSocket_()`. Native TLS
had already installed the successor and abruptly closed its predecessor before
that hook, while FIX still regarded A as current until authentication finished.

The following original source-reachable ordering was incorrect independently of
physical queue isolation:

1. A has an established FIX session and outstanding connection-specific work.
2. Native TLS associates B and closes A. B has not authenticated.
3. An A admission reply, Tx failure/notice, or timer continuation reaches FIX.
4. The existing FIX connection number still identifies A. The callback can
   change FIX readiness/state or emit Close against the native current socket B.

Relevant paths are `Link::admitted_()`, `outputFailed_()`, `notice_()`,
`event_()`, and the session/gap timer callbacks in `ZfixLink.hh`.
`Rx::notice_()` checks the FIX connection, not the physical native pointer.
Clearing only `m_live` is insufficient: these paths are not all guarded by it,
and accepted receive work can still produce lifecycle events.

Calling the original one-step `replace_(now)` at the early hook was also incorrect:
`LinkState::open()` starts the Logon deadline and can emit SendLogon. Calling
`lost_()` is incorrect because it emits Transport Failure and Close for the
successor. Association must revoke authority without opening FIX or reporting
an orderly/final stop of the persistent owner.

## Representation and transition split

Reuse the existing FIX connection number. Allocate it at association, once per
bound socket, rather than at authenticated FIX opening. Do not add a second
generation, current-connection pointer, candidate queue, or transport-phase
member. The native transport already owns the current physical connection.

Use existing `State::Disconnected` while the selected socket is
unauthenticated. Reuse `m_live` to mean an associated socket, setting it true
at association and false on its physical down; authentication is already
represented by the session state. This avoids a second phase flag and prevents
an older `m_closeRef` from suppressing a new pre-authentication down callback. `LinkState::current()` already rejects Disconnected
state, so protocol callbacks for this phase have no active FIX authority.
Authentication moves that same connection number to Accepting or Logon.

Factor the current LinkState opening implementation into two owner operations:

- Association validates configuration and connection-number capacity, advances
  the existing connection number, clears predecessor connection-local state,
  and leaves FIX Disconnected without deadlines or SendLogon.
- Authenticated start consumes that already associated number, enters
  Accepting/Logon, arms the Logon deadline, and emits the existing initial
  control actions. It must not advance the connection number again.

These names describe operations; choose concise final method names when
implementing and propagate the API through direct unit fixtures. Do not retain
an alternate one-step path in native wrappers that silently increments twice.
Duplicate authenticated completion for the same native connection must be
inert rather than restart a current FIX session.

At association, clear the admission ticket, Tx boundary, admitted/ready and
owner-confirmation flags, peer Logout state, probe identity and deadlines using
the existing opening rules. Preserve epoch, sequence cursors, accepted logical
receive work, application queues, and the reset transaction described below.
Hold SeqRx and discard its unadmitted first-Logon input; do not clear its
accepted queue. The shared `Rx::bound()` cleanup now runs for socket-only
association too; it is not conditional on authenticated opening.

## Native wrapper integration

Add `connectSocket_()` to both FIX wrappers. In the initiator it must first
honor `NetBase::connectSocket_()` so native terminal setup shutdown remains
effective. Then clear the borrowed input pointer and invoke the engine's
association operation on Rx. The acceptor performs the same association after
its native socket guard. Return false on genuine refusal using the existing
native close path.

Keep `connected()` as authenticated FIX start. TCP invokes both hooks in the
same Rx turn; TLS can have arbitrary handshake work between them. Do not send
Logon, allocate TLS wire geometry, or announce FIX readiness at connectSocket_.

Association cancels existing session/gap timers, invalidates their serials,
discards partial framing/pending unadmitted input, sets the existing
`m_live` true, and resets per-connection error/stop flags. Authenticated start
starts normal FIX establishment on the same connection number. A native authentication
failure after association closes the selected connection normally; it must not
restore A or manufacture a FIX Logon that never occurred.

No predecessor callback/drain is an activation prerequisite. A then B then C
can associate while prior local resource completions are outstanding.

## Tx handoff before authentication

The early Rx association posts one fixed-metadata owner operation to AppTx.
It advances the existing Tx connection authority, holds publication, retires
old Wire admissions, cancels old idle/control scheduling, and preserves
logical application work and its outstanding load/unload/history tickets.
This is not `TxSide::stop()`: that invokes whole-link stop hooks. It is also
not the original generic Disconnected lifecycle event: `LinkTx::apply_()` treats
that as `closeTx()` and can abort a still-valid logical reset transaction.

Factor/reuse the existing replacement branch rather than add another queue or
copy application messages. Old administrative commands are connection-local;
selected/pending business association remains a logical operation. Keep the
existing operation-ticket and epoch checks. An application refusal or abort
remains authoritative across replacement.

Wire must stay closed while TLS authentication is pending. Its geometry is
available only after native Tx key installation. At authenticated start,
AppTx opens Wire for the already bound connection number and applies the
normal Logon lifecycle event. `TxSide::applyEvent()` compares against
`Wire::connection_()` and excludes Closing/Disconnected events, so early Ctl
rebinding neither suppresses authenticated Wire opening nor opens it for a
pre-authentication logical reset notice. Monotone connection/token checks remain.

AppRx FIFO posts association before authenticated start, so their AppTx posts
retain that order without a cross-shard wait. The native physical handoff must
independently prevent already-running A output from being rebound to B; FIX
owner posts do not substitute for that native guarantee. TLS now binds its
Tx key state to a socket. TCP likewise installs a Tx-owned producer target
after the early FIX bind post and before authenticated notification. This
replaces its earlier Rx-current snapshot in send_(), which could select B for
an already-running A producer. The target is cleared only by its own close;
it is a producer binding, not another Rx-current registry. No per-packet
reference or foreign-shard pointer read is added.

## Callback authority and lifetime

| Path | Required disposition |
| --- | --- |
| Old admission reply | Settle its one-shot reply and decrement existing `m_admissions`; old connection/ticket cannot admit B. |
| Old session/gap timer | Existing serial/connection/epoch checks make it inert after early association. |
| Old Tx idle/control task | Early Tx rebind invalidates its existing connection/ticket before further publication. |
| Old Tx notice or failure | Ignore connection-local effects after rebind; do not emit Close/Ready for B. |
| Old Wire completion | Retire its original token/context once; notify completion with A's identity, without failing B or decrementing B's in-flight state. |
| Deferred application load/unload/history | Retain operation authority when its logical ticket/epoch is still valid; do not cancel it merely because its connection changed. |
| Accepted receive/application work | Retain its logical disposition and sequence progress; old connection metadata alone cannot erase it. |
| Old final-stop continuation | Existing connection comparison must suppress whole-link stop while B is associated, even before authentication. |
| Current pre-authentication close | Drain the selected owner's existing obligations; do not wait for stale physical close accounting to activate another successor. |

Keep `m_closeRef` as the existing owner-drain reference. Association must not
drop it while earlier dependent callbacks remain outstanding. `m_admissions`,
posted credits and work counts remain aggregate lifetime obligations; they
must not be zeroed on replacement. Terminal stop still waits for those
obligations. Native connection references continue to own physical cleanup;
add no retired-connection collection in FIX.

## Reset and cold reconnect rules

Preserve the distinction between ordinary reconnect, armed reset, in-progress
logical reset preparation, and explicit cold reconstruction:

- Ordinary reconnect preserves epoch and all assigned/published/acknowledged
  cursors. It clears abandoned connection-specific reset handshake metadata by
  the existing ordinary replacement rule, not by resetting sequence progress.
- An already armed reset remains armed through association. Start its agreed
  Logon only after authentication, using the existing reset initiator/path.
- A logical reset preparation/commit retains its drain identity, target epoch,
  operation request and deferred completion. Association revokes old physical
  send authority without turning its valid completion into ResetAborted.
- If ResetReady arrives while the replacement is unauthenticated, commit its
  validated logical epoch/progress exactly once and retain the armed reset.
  Post the logical ResetReady metadata to Tx so its committed transaction
  finishes while held; emit no Logon or Logon timeout yet. If it arrives after authenticated start,
  preserve the existing retry/SendLogon continuation. If no socket remains,
  preserve the existing closed-connection reconciliation rule.
- Cold reconstruction remains an application operation after full old-owner
  drain. Restore ID/epoch/progress before association, then perform the same
  normal association/authentication sequence. Do not restore during active
  replacement or mutate immutable configuration in place.

Before this split, `LinkState::resetReady_()` identified replacement solely by
Accepting/Logon state. Disconnected state alone cannot distinguish socket-only
B from a no-socket close. At the Rx integration boundary use the existing
Rx-owned association flag and session state to supply that immediate
reconciliation context. Since `m_live` is now set at bind, it is sufficient;
no extra native-pointer lookup or required core `cxn()` dependency is needed. Native `connectSocket_(replacing)` carries
the predecessor-presence snapshot before publication; it distinguishes B-to-C
replacement of an armed, unauthenticated socket from ordinary cold reconnect
without storing another phase member. Do not store a duplicate socket-phase flag.
Retain `resetMatches_()` operation checks; do not indiscriminately reject every
old-connection ResetReady or accept every stale ResetAborted.

## Raw applicability and remaining contract boundary

Managed FIX4/FIX44A/FIXT11 engines all use `Link`, so the preceding repair
covers them directly. `Raw` is a distinct external-manager engine with no
FIX session, sequencing, admission or recovery policy. Its existing complete
frames carry caller-supplied connection metadata and cannot simply be
retargeted or re-encoded by managed FIX logic.

The original shared native wrappers exposed a concrete replacement gap:
`Raw::transportUp_()` rejected `m_live || m_closeRef` after the native transport
had already displaced A, closing B as well. Fixing only the native queue
handoff could not establish Raw replacement support. This does not justify adding FIX policy to Raw.

The implemented Raw replacement reuses `m_connection` at the socket hook,
separates socket association from authenticated Raw opening, retires old Wire
representations, and leaves complete logical work to the external manager.
The implementation repairs these former completion assumptions:

- `TxSide::outputDone_()` always settles its completion and observer, but a
  false completion from A must not clear B's `live` or report a B fault.
- `TxSide::checkStop()`/`stoppedTx_()` must carry/check the existing connection
  number; their former identity-free posted completion could complete B's stop.
- An asynchronous `fixRawStopRx_(A)` or `fixRawStopTx_(A)` already outstanding
  before B cannot be forgotten when B opens. The former booleans plus the
  current-connection-only stopped dispatchers could not represent overlapping
  external stop obligations. Do not remove the busy guard without resolving
  that demonstrated lifetime obligation with the external-manager contract.

The managed execution must state this Raw boundary explicitly. Do not silently
expand the managed repair into a new Raw retirement registry or imply Raw
logical frame preservation is provided by the FIX sequencer.

Raw now separates association/open, scopes output effects to their original
connection, and carries connection identity through stop posts. Rx/Tx aggregate
stop credits replace the old singleton completion booleans. A borrowed
`RawStopRequest` allocates a pooled one-shot callback only when the application
defers a hook. Callbacks settle their owner obligation without authority over
the successor. The native Raw TCP/TLS regressions pass seven scopes each,
including A-to-B-to-C traffic with held A output and independently delayed
external Rx/Tx stop replies.

## Minimum focused regression matrix

Use the existing actual-Link owner fixture in `ZfixWireTest.cc` for deterministic
owner scheduling. Its native wrapper fixture must call the real socket hook
separately from authenticated `connected()`, rather than only calling
`transportUp_()` and assuming the boundary is covered.

1. Associate A, establish it, hold A admission/notice/failure/timer work, then
   associate unauthenticated B. Run each stale continuation: B remains current,
   no Ready/Close/Logon is emitted, and every lifetime credit settles once.
2. Authenticate B: one new connection number total, ordinary Logon/recovery,
   no mandatory probe or predecessor drain. Repeat A/B/C before B authenticates.
3. Fail/refuse B before authentication and before FIX Logon. A stays obsolete;
   current stop completes after real owner obligations, without old cleanup
   altering a later C. Include an old already-posted stopped continuation.
4. Hold business association/loading and accepted receive work across early
   B association. Complete the logical operation during authentication delay,
   then establish B and prove publication/disposition occurs once with retained
   assignments and normal recovery. Test real application refusal separately.
5. Queue old Wire failure before and after early Tx rebind. Its original
   completion is delivered, B's counters/readiness remain unchanged, and old
   idle/control work cannot publish on B.
6. Exercise reset commit before early association, during socket-only B, and
   after B authentication; include failed B and third successor C. Preserve
   exactly-once epoch commit and normal fresh drain/probe guarantees.
7. Extend existing native TCP/TLS replacement regressions to the FIX wrapper:
   actual TLS handshake pause/failure must expose the socket-only interval.
   Retain physical queue/EAGAIN coverage separately; a synthetic owner fixture
   cannot prove socket context isolation or ciphertext target binding.
8. Keep existing ordinary reconnect, cold checkpoint restore (including changed
   wire configuration under the same application ID), reset and owner-drain
   suites. Add Raw overlap cases only together with an explicit resolution of
   the external asynchronous-stop obligation above.

Run only the affected Zfix Linux x64 targets with the existing configuration
and dependency artifacts when implementation is frozen and execution is
authorized. Source inspection alone is not a passing regression result.
