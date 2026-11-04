# Zfix integration tests

Current repair verification passes 13 TAP programs/261 scopes (TCP127, TLS132,
demo2), including 42 demo scenario results;
see [STATUS.md](../STATUS.md) for evidence and verification scope. Earlier counts below
belong to their recorded snapshots. Current fixtures use
`fixRetrieveTx(TxContext, acknowledged, ticket)` for every resend and retain any
desired records in application stores at publication. Archive notifications carry
epoch/ranges and do not release core payloads. Version-4 native fixture state restores epoch and directional progress directly
into the selected link, without session-key matching, saved wire headers/mode or
old queued-memory debt. Ordinary Ready follows actual owner recovery;
only explicit liveness/progress/drain probes require their matching echo.

Reuse compatible dependencies and build `zfix/src`
first, then run:

```sh
make -C zfix/itest -j8
make -C zfix/itest test
```

`tcp_test` and `tls_test` compile the same transport fixture over Ztcp
and Ztls. Each runs one connection, three simultaneous connections, and two
connections carrying signed binary FIX frames larger than two TLS plaintext records.
The transport fixture sizes both rings for its complete simultaneous frame burst,
using the TCP/TLS buffer capacity and session count, plus the default reserve.
Small session-state fixtures retain the 128-slot defaults; process fixtures use
the workload tools' session/in-flight sizing. Deliberately small socket windows
remain unchanged so partial-write assertions still exercise real backpressure.
The initiator admits two completed frames through `WireTx` before their socket
completions. TCP borrows each frame without copying its bytes; TLS uses tagged
record buffers with the transport's negotiated headroom and plaintext bound. Each acceptor extracts pooled frames with `RxFramer`,
retains them across an Rx-to-Tx post, and echoes their exact bytes. Both endpoints
check Rx ownership and one local completion per admitted frame. Large packets
declare 212/213 and 93/89, carry every octet including SOH/NUL, and verify an
application HMAC-SHA256 over the serialized 35-through-payload bytes. Both
endpoints check decoded span addresses against the exact extracted allocation;
the echo post retains that allocation and checks the same payload/signature
addresses on Tx. Raw completed frames preserve their signatures during native
partial-write retries. Stop drains
physical I/O, then output-completion posts, before releasing links and finalizing
hub TLS contexts.
The fixture waits for physical close callbacks before finalizing hub TLS contexts.
It uses separate isolated Rx/Tx owner threads and ephemeral IPv4
loopback ports. Byte fragmentation at every boundary is tested deterministically
by `ZfixRxFramerTest`; OS socket fragmentation is not assumed.

On Linux, two additional cases constrain native socket receive/send buffers
through `ZiMxParams`, pause the native Rx owner after TCP/TLS readiness, and
observe real `send` results through a test-executable symbol that calls
`SYS_sendto` with the original arguments. Both require actual EAGAIN; resuming
must preserve all binary bytes and deliver ordered, exactly-once completion.
Positive short writes are required for TCP and for the resumed TLS case. TLS
close at the first EAGAIN can precede a positive short write. The close case
cancels both pending frames exactly once, retains no wire queue bytes/nodes,
and delivers no incomplete frame. The observer neither truncates requests nor
manufactures errors; no production instrumentation is added. These cases are
not compiled on other platforms.

Native persistent-link cases retain A's disconnect continuation, establish B,
feed B a partial FIX frame, then release A and require B's reply and balanced
physical shutdown accounting. A second ordering lets A enter close processing
before B associates. TLS additionally refuses B before handshake with A's close
pending, and pauses an already protected A record while B associates: A's
ciphertext must stay bound to A and B must authenticate and exchange traffic.

Stop-before-bind also retains the real B client while the server closes its
socket before binding. If B published its Logon, that exact attempt must complete
once. A failed completion before logical close reports Output; one after close
only settles the attempt. The fixture correlates publication, false completion
and optional Output fault/event, rejects other faults, and preserves the server's
physical accounting, stopped, no-Ready and no-business checks.

The Linux TCP partial-replacement case uses a raw A peer that does not read.
After real EAGAIN and completion of producer submission, B replaces A and must
receive its reply. A separate probe submits through the actual closed A socket
on I/O Tx and requires that its send setup retain no physical queue entries.
These checks use no fabricated send result or I/O context. Controlled held
callbacks/producer gates are fixture scheduling, not successor activation
barriers in production code.

Managed loopbacks configure different client/server Rx and Tx frame limits.
Both roles validate received Logon tag 383 against the peer's Rx capacity in
every managed mode, including asynchronous and refused admission paths.

`tls-test.sh ./tls_test` creates a temporary RSA key and a one-day localhost
certificate using OpenSSL, supplies that certificate as the client's trust root,
runs the fixture and removes the temporary directory. Hostname and certificate
verification remain enabled. No deployment credentials are used.

`session_tcp_test` and `session_tls_test` use the production Zfix hubs,
initiator/acceptor links and shared managed session owners. Each covers all
three managed modes; deterministic paused-input admission resumed from a third
application owner; real idle-heartbeat timers; inbound refusal; outbound Logon
preparation refusal; graceful Logout; and forced close during pending admission
or association, with ticket cancellation and owner snapshots during teardown.
Each test waits for managed stop before finalizing hubs or the multiplex.

The Stage 6 Linux gate covers administrative admission, partial-write faults,
multiple listeners/sessions, failover, overload, pending work and diagnostics.
Historical coverage is recorded below and in [status](../STATUS.md).

`make -C zfix/itest asan-test` instruments the fixture and adapter code and runs
both transports with ASan/LSan. Configured dependency DSOs remain uninstrumented.

`make -C zfix/itest installed-test ZFIX_PREFIX=/path/to/prefix` recompiles and runs
both managed fixtures using only installed headers and libraries.

`recovery_tcp_test` and `recovery_tls_test` each cover continuing reconnect
and restart in all three managed modes. A fault hook discards one complete,
transport-admitted business frame after recording a local completion. The first
connection stops before peer processing. Reconnect preserves the session owners;
restart writes both sides' cursor/history records to fixture-owned files with
`ZiFile`, stops the hubs/mux, destroys the stores and original body handles, then
loads new stores and restores fresh owners before opening a listener/connection.
The fixture file is native to this test, not a Zfix storage backend or file format.
Disk I/O runs on main after owner stop. Deferred reload continuations run through
a third application owner. Assertions check unchanged epoch/reset flag, advancing
new sequences, exact replay body/identity/original timestamp, one application
receipt of the lost request, and normal traffic after recovery. The fixture uses
ephemeral ports and ZiTestResidue cleanup/failed-artifact retention under `logs`.

`make -C zfix/itest asan-recovery-test` instruments and runs these fixtures.
`installed-recovery-test ZFIX_PREFIX=/path/to/prefix` compiles them with installed
library headers/libraries; only the repository's standalone test-residue helper
is linked from the build tree.


`raw_tcp_test` and `raw_tls_test` use the separate raw endpoint engine.
Each covers external control schemas without managed Logon fields, deliberately
mismatched FIX version/identities and old timestamps, nonmonotonic/repeated
sequence values, exact paused-input backing resumed through a third owner,
zero-output-quota refusal without publication, text payloads exceeding two TLS
records, and asynchronous Rx/Tx manager stop. Assertions check owner affinity,
one completion per admission and stopped callbacks after both external drains.

`make -C zfix/itest asan-raw-test` instruments the raw fixture/adapter templates;
dependency DSOs remain uninstrumented. `installed-raw-test
ZFIX_PREFIX=/path/to/prefix` recompiles/runs both transports using installed
headers/libraries only. The general `test` target includes all twelve frame, managed, recovery, raw,
reset and ingress programs.


The managed session programs retain three independent live logical links on
one client hub across two listeners in every managed mode. Per-session lanes
check routing; closing one link leaves the others exchanging traffic with
continuing sequence progress. `routeMatrix` preserves this coverage after
removal of the attachment registry. The obsolete duplicate-owner refusal cases
are removed. New abrupt-displacement coverage and verification remain in
progress under `../../zfix_repair2.md`.


Managed `startupCleanup` runs failed initialization/finalization/reinitialization
on the same hubs before a real Logon/request/reply/Logout exchange in each mode.
It rejects null/stopped multiplexes, invalid owner IDs and coincident Rx/Tx owners.
TLS also fails after partial CA/certificate allocation using a missing trust/key
path, finalizes explicitly, and then succeeds with the disposable test credentials.

`listenerCleanup` first creates a real bind conflict. It stops the original hub,
restarts the failed listener and rebinds the exact old ephemeral port while the
multiplex stays running. It then restarts/stops the original hub and performs 32
listen/immediate-stop/rebind cycles without waiting for bind completion before
stop. Each completed hub stop must leave the address available to the other hub.
This reproduced two Ztcp/Ztls defects: stop targeted configured port zero instead
of the OS-assigned address, and a late successful bind could leave a new listener
alive after stop. The native hubs now retain the bound address and guard late
success/failure/retry work against the lifecycle state.

Managed `setupCleanup` runs in all three modes for TCP and TLS. A silent local
UDP DNS endpoint exercises normal resolver timeout while stop is pending; no
external DNS is used. Numeric setup, a bound/non-listening TCP endpoint and a
blocked network-owner queue cover normal completion/failure during shutdown.
TLS also accepts TCP with a silent handshake peer, then closes it from either
end. No DNS/connect cancellation or setup deadline is used. Failed attempts
preserve restored Rx cursor 19 and Tx cursor 27/epoch 1 and publish no Logon.
Failure observers may override native defaults without skipping protocol drain.
A later automatic retry reaches a restarted server and exchanges FIX normally.

Raw `setupCleanup` repeats stop during setup three times before readiness, both with
immediate and deferred manager stop. Its deferred case acknowledges Rx while Tx
is deliberately held and verifies that stopped has not fired, then releases Tx.
Every attempt uses connection zero, drains independently and publishes no frame.
This catches stale completion flags when no successful open separated attempts.

reset_tcp_test/reset_tls_test exercise the production ResetRequest dispatcher
in 27 scopes each: all three modes, either live-reset driver, reconnect reset,
and explicit initial reset agreement. A separate application owner posts captured
commands; the common Rx owner batches the two agreed reconnect-disconnect
operations before native close can run. Tests cover refusal before drain,
retained unsequenced work through live turnover, both owner epoch notifications,
old business acknowledgement, reset Logons at sequence 1, preserved physical
connection for live reset, new connection for reconnect, and stale command
refusal. Snapshots are captured afresh per epoch; combining epochs in one record
is intentionally rejected. Old non-replayable control tails may be retired by
explicit turnover after business drain, rather than inventing peer acknowledgements.
The fixtures use local OS-assigned listener ports and disposable TLS credentials.
Fifteen additional cases cover each managed mode after committed epoch
preparation: physical close or a real Logon deadline before Start, physical close
or deadline after the first reset Logon is locally written and retained on the
paused peer, and outbound Logon preparation refusal. They assert no false Ready,
unchanged committed epoch, exact assigned cursors, cleared pending/native wire
backing, cancelled timers, returned administrative credits and retained
unsequenced business work. Reconnect continues the epoch, recovers any assigned
first Logon sequence, delivers that work, and rejects old connection commands.
A source overlay removing epoch timer cancellation fails the actual timeout
and stopped-barrier assertions; its failure path force-closes for bounded cleanup.

Run `asan-reset-test` or `installed-reset-test` for sanitizer/installed consumers.

The managed session fixture now retains Busy submissions on its own Tx-owned
unsequenced queue. A deterministic case posts a queue marker and a submission
in one Tx task so the submission precedes the marker's pump and must return Busy.
The application then queues the retained body; eventual selection and traffic
remain checked. This matches the documented immediate-admission contract instead
of treating control-queue contention as a transport/session failure.

ingress_tcp_test/ingress_tls_test cover application submission admission,
codec frame/output limits and wire traces. `asan-ingress-test` and
`installed-ingress-test` build both consumers. The standalone PostedIngress
submission/administrative boundary test runs in the unit directory.

The ingress fixtures also hold application Tx to test `submitBudget` with a
shared hub quota and a zero per-link quota. They verify synchronous refusal,
caller handle retention, no observer/publication for refused posts, capacity
reuse, and close with admitted work pending. Calls from outside AppRx/AppTx are
refused. The managed overload fixture verifies that refused handoffs do not
advance sequence progress before a later accepted submission.

Hub defaults are configured through `Client(HubLimits)`/`Server(HubLimits)` and
inherited by link constructors that omit explicit LinkLimits. The raw fixtures
configure their complete output/queue limits once per hub. Three `hubLimits`
scopes in each ingress transport fixture verify receive-frame, transmit-frame,
and wire-byte overrides at their actual refusal boundaries. The managed session
fixture additionally configures zero read-ahead count at the acceptor hub, first
completes an in-order exchange, then injects a valid high-sequence frame below
managed publication. It must close with Overload before business dispatch and
without retaining the out-of-order input.

Each ingress transport fixture also enables raw Rx/Tx trace rotation, compares
captured escaped plaintext, retains immutable observer records through link/hub
teardown, and checks counter snapshots against delivered/publication/completion
observers. The managed overload fixtures enable both owners' traces, append test
credentials to both Logons, assert their redaction on TCP/TLS, and check current
queue occupancy and cumulative local completion snapshots after close.

Recovery fixtures now include warm-store and file-restored standby failover in
all three managed modes (12 scopes per transport with the original recovery
cases). Two listeners coexist before the primary is stopped. A bound,
non-listening socket reserves the former primary port; a failed retry must not
open a new logical connection or move Rx/Tx cursors. The same client then moves
to the standby, whose owners are restored before network-Rx routing publication.
The standby restores application-owned state before routing exposes it. This
cold-owner handoff is distinct from replacing a connection on a live link.

A second business body is queued before the original disconnect and remains
unsequenced through the failed retry. Application policy releases it at new
readiness; it is never reconstructed by replay. Its association is held while a
real first-connection load reply is injected. After valid association, native
records are held in a fixture-owned Tx queue (already charged by WireTx) so a
real old token's failed completion arrives against a live current wire head.
Both stale callbacks must remain inert. Release then delivers the replayed old
request and the queued new request exactly once, with unchanged epoch, continuing
sequence associations and exact original replay timestamps. The cold variant
loads a distinct server store/body allocation graph from fixture files.

`asan-recovery-test` and `installed-recovery-test` include every new case. A
source-overlay negative control that removes wire-token comparison fails the
failover scope; recipes and terminal logs are recorded in STATUS.md. Listener
ports remain OS-assigned, including the reserved failed-primary endpoint.

The managed session fixture adds nine `adminBudget` scopes per transport.
They cover hub-inherited per-link and aggregate zero limits, large heartbeat
echo bodies and retained peer Reject backing, replay-command refusal after its
lifecycle event, generated Rejects, and isolation across two sessions sharing
one hub. Business submission capacity is set to zero after the normal exchange;
controls still progress. Held Tx and Rx owners verify that pending credit returns
remain reserved, capacity is reused, refused work has no sequence association,
and stopped waits for admitted work. A coalesced native write/TLS record must
stop framing after its first overload. These cases are included in the existing
source, `asan-test`, and `installed-test` entry points.

A terminal-metadata refusal case sets the hub quota to zero after readiness and
closes the connection. It requires both original transport-failure observers
and both disconnected events even though Tx admission refuses the terminal
snapshot; physical teardown still drains without recursive faults.

The zero-limit check also uses application owners equal to the native network
owners. Overload is reported before requesting physical close, and failed
administrative close bypasses graceful TLS output so a held Tx owner cannot
prevent shutdown progress.

`session_tcp_test` and `session_tls_test` also exercise public deferred-history
admission with a real jumbo body. Main/native owners cannot enqueue a response;
AppRx refusal preserves history and reports no implicit session fault. Per-link
and aggregate limits apply independently, including fixed envelopes for null
history. A held Tx owner makes repeated stale responses consume reservations;
a held Rx owner proves completed work retains credits until the return runs.
Exact returned capacity can be reused, physical close waits for admitted work,
and direct Tx stale completion remains safe after stop. The recovery fixtures
return real asynchronous store responses through AppRx. Alternating replies fill
the hub budget to force refusal, preserve the body and fall back to direct Tx
delivery from an application-owned post. Replay still delivers the stored bytes
once with their original sequence and SendingTime.

Managed session fixtures use the default catalog-aware header inspection and
secondary-selected typed receive hooks. Both roles check runtime session,
connection, epoch, sequence, version, duplicate/replay and receive timestamps.
An additional case per transport retains context plus a borrowed request ID
through link/hub destruction, then revalidates the original frame. Recovery
fixtures use typed receive for normal traffic and stored replay, including
pre-Ready delivery on a new physical connection. No business completion is
inferred from the wire replay flag.

Managed session fixtures use `send<Entry>` from both roles, including all four
secondary lane values. Public sends reject foreign-owner calls and a zero Rx-to-Tx
post quota without reserving sequences or disturbing established traffic. The
existing forced-Busy retained-body submission case remains covered. Raw server
responses use typed send with an explicit header, TxContext and administrative
classification; the manager keeps its nonstandard version, sequence, duplicate
and queued-output policy.

Managed session requests now declare four secondary-selected execution response
types (MsgType 8), and responders use typed reply. Three additional loopbacks per
transport initiate from the server and reply from the client, one for each managed
mode. Worker checks reject empty/wrong-variant/wrong-MsgType context before policy,
preserve application errors including offsets, then reuse context successfully
after refusal. The fixture explicitly authorizes its runtime session and epoch;
the library supplies no default business/reconnect policy. Raw server replies use
the external manager's explicit policy and outgoing header/sequence values.

Nine managed work cases per transport cover deferred replies, forced close with
one held lease, and reset drain waiting for work, each in modes 4/4.4A/T1.1.
Count and byte checks distinguish per-link and hub refusal, UINT64_MAX extra-byte
overflow, live-limit undercut refusal, exact-fit admission and retirement still
charged before the next Rx turn. Background work reads the original borrowed
request ID through the retained context. Close observers wait until explicit
release on a third owner; retained plain context remains readable after both
hubs are finalized. Drain blocks new work and produces its proof only after
retirement; no business response is synthesized for cancelled work. Raw server
text replies also pass through a third owner using a work lease; worker/result
callback allocation charges are derived from their actual delegate types.

The session fixture's application queue retains both Busy and Held local selection
results. In server-initiated exchanges a valid client reply may reach Tx before
its own new-send readiness is enabled. The subsequent readiness event resumes
selection; no sequence is reserved by the earlier local handoff.

## Native utility process suite

`native_io_test.py` launches the `../util/zfix` and `../util/zfixd` tools as
separate processes. It checks 18 cases: scalar orders, multiple sessions,
retention, groups with text data, pacing, duration, FIX4.4A, FIXT1.1 and
expected byte-quota refusal, each over TCP and verified TLS. Readiness is
pipe-driven; child results, exact business counts and shutdown exit codes are
validated. Every run retains commands, hashes, JSON results and stderr logs in
its printed artifact directory.

`make -C zfix/itest test` runs `native_io_test`, a thin ZuTest driver with
one scope requiring the complete18-case fixture to succeed. Build
the native utilities first, as the module's normal `src`, `test`, `util`, `itest`
traversal does. Direct and specialized entry points are:

```sh
make -C zfix/itest io-test
make -C zfix/itest asan-io-test
make -C zfix/itest installed-io-test ZFIX_PREFIX=/tmp/zfix-stage/opt/z
python3 zfix/itest/native_io_test.py --tcp-only --case scalar
```

The specialized targets compile utility consumers in the integration build
directory, retaining the original sanitizer and installed-header/library
checks, including a ZuTest driver for each build. They do not rebuild dependency
modules. `--client` and `--server` select explicit executables for the JSON
fixture. `ZFIX_IO_DIR`, `ZFIX_IO_CLIENT` and `ZFIX_IO_SERVER` configure the driver;
its child diagnostics go to stderr so only ZuTest emits TAP. Shared readiness,
certificate, process cleanup, workload profiles and case validation remain in
`../util/pair-test.py`. Interoperability callers invoke that reusable utility
runner directly and do not depend on an integration-test entry point.

## Application-policy regression fixtures

`zfix_demo` runs one native initiator and acceptor on an ephemeral loopback port.
Both roles use the same typed catalog. This is a finite integration fixture using
an explicit custom `U1` request, `U2` report and `U3` event dictionary, **not**
canonical NewOrderSingle/ExecutionReport workloads. Standalone public examples
live in `../example`.

Build `zfix/itest`, then run:

```sh
./zfix/itest/zfix_demo rpc
./zfix/itest/zfix_demo groups
./zfix/itest/zfix_demo events
./zfix/itest/zfix_demo reject
./zfix/itest/zfix_demo policy --mode T11 --auth example
```

Each invocation checks its outcome, emits a ZfJSON summary and exits nonzero on
failure. The five scenarios demonstrate:

- `rpc`: one application request and two immediate reports, acknowledgement and
  fill. Local handoff/send completion does not complete the business call.
- `groups`: the same request with two repeating party entries, decoded and
  checked by the acceptor.
- `events`: an unsolicited acceptor event; no client call is registered.
- `reject`: the acceptor rejects zero quantity. The initiator resolves RefSeqNum
  through its Tx-owned `(epoch, outbound sequence)` association to the explicit
  business key. Neither the reject frame sequence nor a numeric guess identifies
  the call.
- `policy`: two calls complete in reverse order, each with fill before
  acknowledgement and a repeated fill. A repeated request reuses cached execution
  IDs; an unknown report key creates no call. Expected outcomes are two business
  completions, four duplicate reports and one unknown key.

`../test/ZfixDemo.hh/.cc` owns correlation, result caching and the sequence/history
store.
`zfix_demo_io.hh` binds these policies to native typed receive/send/reply hooks,
work admission, optional Logon authentication and Tx recovery hooks.

The client explicitly registers numeric tag-11 keys. It completes only after
acknowledgement **and** fill with distinct nonzero tag-17 execution IDs, or an
associated session rejection. Duplicate/conflicting report policy and explicit
record release belong to `Calls`; no library registry supplies this rule.
The server's `Results` returns existing execution IDs for a repeated key and
quantity, refuses conflicting quantities and refuses an unknown replayed key
without creating new business work. A restored result is reusable on replay.

The policy scenario retains request contexts in bounded Work leases until both
requests arrive. The lease is acquired before creating a fresh business result.
Borrowed request spans must travel with their context; the fixture copies only
fixed keys into its job nodes. Calls/results are capped by the finite batch,
queued unsequenced output has count and byte caps, and retained Tx history has
independent count and byte caps. Stop clears these owner-shard structures.
The fixture reply hook authorizes the current runtime session and epoch; this is
an application choice, not automatic library correlation or reconnect policy.

The application Tx store associates before publication and records the original
timestamp there. Body-free archive ranges report sequence progress; the fixture
keeps its own records available under its finite-workload retention policy.
Every resend invokes the retrieval hook, independently of operational node
residency. Unit tests restore these records and business results independently. This
is an in-memory fixture, not durable storage. Actual resend/restart sequencing
and delayed retrieval are exercised by `itest/recovery_{tcp,tls}_test`.

Select managed mode `4` (default), `44A` or `T11`. For verified TLS:

```sh
./zfix/itest/zfix_demo policy --tls \
  --cert /path/server.pem --key /path/server.key --ca /path/ca.pem
```

The fixture connects with the `localhost` TLS name and verifies the supplied CA.
`--auth TOKEN` appends application-selected Logon username/password fields and
checks both peers before admission; `--bad-auth` deliberately gives the client a
wrong password and expects refusal before any business call. Use fixture values
with this command-line fixture. Authentication fields are not retained in its
replay store. Default wire tracing is disabled.

Verification entry points:

```sh
make -C zfix/itest test
make -C zfix/test asan-demo-test
make -C zfix/itest asan-demo-test
make -C zfix/test installed-demo-test ZFIX_PREFIX=/tmp/zfix-stage/opt/z
make -C zfix/itest installed-demo-test ZFIX_PREFIX=/tmp/zfix-stage/opt/z
./libtool exec valgrind --leak-check=full ./zfix/itest/zfix_demo policy --auth example
```

`zfix_demo_test` emits two ZuTest/ZuTestUtil TAP scopes, one per transport. Each
invokes a shell fixture running all five scenarios plus authentication
acceptance/refusal in all three modes: 21 TCP and 21 TLS invocations. The TLS
runner creates a temporary self-signed certificate with a localhost SAN, verifies
it and removes it on exit.
Sanitizer targets instrument the fixture and its templates; configured dependency
DSOs retain their configured instrumentation. Installed targets use installed
framework headers/libraries and the fixture sources.

`ZfixDemoTest` additionally checks unknown/conflicting keys, bounded registration,
explicit release, restored results, actual PossDup wire decoding with no fresh
work, epoch/sequence associations, byte/count refusal, original timestamps,
history restoration and custom Logon conversion.
