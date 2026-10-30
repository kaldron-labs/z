**Zfix has substantive policy and application-control problems beyond its member-data footprint.** The strongest concerns are automatic publication/replay of retained messages, internal history retention, and mandatory synchronization before readiness.

This review excludes MT differences and generic Mx connectivity.

1. **Application resend decisions depend on cache residency.**

   Zfix retrieves history from the application only when the sequence queue lacks the record. A resident business body is automatically considered replayable. Consequently:

   - An expired order still in the queue is replayed without consulting `fixRetrieveTx_`.
   - Evicting that same record first lets the application return unavailable and obtain a gap-fill.

   **Cache residency must not determine business policy.** [ZfixTx.hh:409](/home/ubuntu/src/z/zfix/src/ZfixTx.hh:409)

   Previously assigned but unpublished business messages have a related problem: reconnect resumes their publication. The lower `Tx::abort_` preserves assigned sequence numbers while removing content, but `SessionTx`’s private inheritance hides that operation from ordinary managed applications. The application cannot adequately implement “abort this queued order and gap-fill its assignment.” [ZfixSeq.hh:376](/home/ubuntu/src/z/zfix/src/ZfixSeq.hh:376), [ZfixCtl.hh:87](/home/ubuntu/src/z/zfix/src/ZfixCtl.hh:87)

   This needs an application decision covering resident and retrieved messages, ordinary publication and replay. It needs no cancellation ledger or distinction between cancellation and unavailable history.

2. **Zfix retains published history independently of application storage.**

   Its sequence queue retains the node, encoded body, original timestamp and association after publication. The unsent-byte charge decreases, but the retained record remains. Therefore, **`queuedOutputBytes` does not bound published history**. The default archive hook releases nothing. [ZfixSeq.hh:616](/home/ubuntu/src/z/zfix/src/ZfixSeq.hh:616), [ZfixLink.hh:473](/home/ubuntu/src/z/zfix/src/ZfixLink.hh:473)

   This duplicates ownership and metadata when the application maintains history, although shared immutable body references avoid necessarily duplicating payload bytes.

   Applications can release a published prefix through `archived()` without waiting for peer acknowledgement. The load utility already does this after local completion, then supplies replay records from its application-owned bounded cache. [load_native.hh:208](/home/ubuntu/src/z/zfix/util/load_native.hh:208)

   Operational retention through publication/completion is justified. Long-term replay retention should be clearly application-owned rather than an implicit additional library cache.

3. **Mandatory readiness synchronization adds policy and state.**

   Zfix completes Logon and directional recovery, then always sends a synchronization TestRequest and holds new outbound business until the matching Heartbeat arrives. This adds a wire round trip, readiness flags, challenge state and cross-shard notices even when there was no recovery gap. [ZfixSession.cc:289](/home/ubuntu/src/z/zfix/src/ZfixSession.cc:289), [ZfixSession.cc:458](/home/ubuntu/src/z/zfix/src/ZfixSession.cc:458)

   **That barrier should not be mandatory ordinary readiness policy.** Receive-timeout probes and explicit drain/reset proofs have independent reasons to exist.

4. **The managed connection-establishment phase is redundant.**

   Zfix calls `transportUp_()` after TCP establishment/TLS authentication. It then opens Session in `Connecting`, arms its connection deadline, and immediately calls `connected_()` to clear that deadline and enter Logon. That timeout cannot cover DNS, TCP or TLS establishment. [ZfixLink.hh:233](/home/ubuntu/src/z/zfix/src/ZfixLink.hh:233), [ZfixSession.cc:157](/home/ubuntu/src/z/zfix/src/ZfixSession.cc:157)

   The native managed session should begin with transport already established. Its synthetic connection phase adds state transitions, notifications and timer work without providing connection management.

5. **Session-control customization is limited.**

   Zfix permits replacement of Logon through `fixLogonTx_`. Other control/publication callbacks are observations after decisions or frame admission; they cannot rewrite or veto the generated control. [ZfixCtl.hh:539](/home/ubuntu/src/z/zfix/src/ZfixCtl.hh:539)

   Requiring an application to replace the entire session manager with Raw merely to customize another session message is too coarse.

The basic application callback coverage is otherwise substantial:

| Responsibility | Current Zfix boundary |
|---|---|
| State and recovery notifications | `fixStateRx_`, `fixEventRx_` |
| Incoming/outgoing controls | `fixControlRx_`, `fixControlTx_` |
| Accept/refuse/defer assignment | `fixAssociateTx_`, `loaded()` |
| Supply synchronous/deferred history | `fixRetrieveTx_`, `reloaded()` |
| Publication, completion, session progress | `fixPublishedTx_`, `fixCompleteTx_`, `fixAckdTx_` |
| Admission and shutdown | Admission continuation, stop hooks, final stopped callback |

These hooks are implemented; I found no demonstrated missing state-notification capability. Their presence, however, does not supply the missing publication decisions described above.

Connection generations, epochs, outstanding continuation tickets, distinct sequence frontiers and final shutdown barriers remain justified. Protocol `Disconnected` does not mean that callbacks and output have drained. Those should survive simplification.

No files changed; no builds, compilation or tests ran.
