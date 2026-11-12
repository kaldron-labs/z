# Frozen dependency defects observed during the Ztls packet migration

These are current source/runtime findings, not a retrospective change to the
completed Phase 1/2 review. Zi and Ztcp remain frozen and were not edited for
these findings.

## Ztcp: repeated close can leave a connectionless link Disconnecting

Observed with the Clang debug worktree while verifying the FIX dependent:

- `python3 /home/ubuntu/rebuild-evidence/ztls-fix/jobmake.py -C zfix`
  completed successfully after fixture allocator migration.
- `make -C zfix/itest test` intermittently fails the final telemetry assertion in
  `session_tcp_test`'s `nativeReplacement`, `nativeChain`, or
  `nativeStopBeforeBind` cases. The business exchange, physical close counts,
  and owner drain assertions pass.
- Running `cd zfix/itest && ./session_tcp_test -q` reproduced the failure with
  diagnostic `stopped native state=8 ... connection=0`. State 8 is
  `Ztc::LinkState::Disconnecting`; both a server link and `ownB` client have
  exhibited it.

Authoritative logs:

- `/home/ubuntu/rebuild-evidence/ztls-fix/zfix-session-stop-state.log`
- `/home/ubuntu/rebuild-evidence/ztls-fix/zfix-session-tcp-quiet1.log`
- `/home/ubuntu/rebuild-evidence/ztls-fix/zfix-itest-recheck.log`

`ztcp/src/Ztcp.hh`'s `Link::disconnect()` and `disconnect_()` set
`m_disconnecting = 1` before determining whether there is a current physical
connection. If the connection is already absent, no close is initiated and no
new physical `disconnected_()` callback remains to clear that flag.
`state_()` prioritizes the flag over the absent connection, so the link remains
Disconnecting despite having retired its physical connection. A repeat request
between the first Rx close notification and its final owner-drain continuation
can likewise reassert the flag after its clearing notification.

The frozen dependency author should make repeated close requests preserve a
terminal Down state and verify pending-connect cancellation separately. The
owning FIX callers were traced and corrected as described below, so a redundant
protocol close is not issued after its physical close obligation is discharged.
No telemetry assertion has been weakened or skipped.

### Owning FIX caller correction

A temporary fixture-only stack capture established two callers, saved in
`zfix-session-native-close-stacks.log` and symbolized in
`zfix-session-native-close-symbols.log` under the evidence directory above:

1. `Zfix::Link::event_` receives a queued `TxSide::notifyTx` / `Rx::notice_`
   terminal event after native Rx has cleared the physical connection but
   before the final FIX owner-drain notification sets the protocol owner down.
   Its earlier `m_live` check consequently does not establish a physical close
   obligation.
2. `Ztcp::Server::stop_0` calls `Hub::downLinks_` for a server link retained only
   for pending FIX completions, which calls native `Link::down` without a
   physical connection.

The migration corrects these owning calls in Zfix: the native protocol-close
hook checks the physical connection before requesting close, and the FIX server
binding's `down()` skips native down for a connectionless retained link. The
standalone FIX engine's default hook continues to delegate to its implementation.
Public client disconnect and outbound-setup cancellation remain native operations;
the protocol hook does not replace them. All original final Down, business,
completion, and physical-drain assertions remain in place. The native Ztcp
idempotency defect itself remains for its author to repair in the frozen phase.
