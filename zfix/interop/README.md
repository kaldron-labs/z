# Zfix independent interoperability fixtures

Recorded managed repair verification passed the codec/load/SDK checks and
all 258 process cases; see [STATUS.md](../STATUS.md) for its captured source and scope.
Earlier recorded passing counts below belong to their recorded snapshots.
Native resend always requests application-owned content. The version-4 native
fixture format saves epoch and directional progress for direct restoration into
the selected link, without a session key, saved wire headers/mode or restored
queue-memory debt. QuickFIX keeps its independent SDK session/store identity. The liveness fixture
below checks ordinary readiness without a synchronization challenge; its current
execution status is recorded in STATUS.md. Historical measurement archives
and their source/ELF manifests remain unchanged.

ASan identified a QuickFIX TCP shutdown race in `dependency-test`:
`Acceptor::stop()` calls `onStop()` before joining its worker. Both threads
could enter `SocketServer::close()`, while the worker also deleted the server
and its socket map. The pinned patch now gives an active `start()`/`block()`
worker exclusive ownership of that cleanup. Caller-side closure remains for
non-processing paths, including serialized polling and failed startup. The
guard reads QuickFIX's existing atomic processing flag, cleared only after
`onStart()` returns; it adds no lock or second lifecycle flag. The existing
one-second socket-monitor timeout bounds wakeup without a concurrent close.
The 2026-10-31 SDK ASan verification passed all ten TAP scopes and six JSON/TLS
scenarios with leak detection enabled, including eight forced/graceful TCP
shutdown rounds. The original failing probe binary also passed against the
repaired SDK before the regression was added.

The peer dependency is pinned to [QuickFIX/C++ 1.16.0](https://github.com/quickfix/quickfix/releases/tag/v1.16.0),
commit `2ce8a60667d95a55cdc57a210f165e19cb757126`.
`quickfix.lock` records the source archive, canonical `spec/FIX44.xml` and
fixture-only `quickfix.patch` SHA-256 hashes. Fixture dictionaries must be named
explicitly; custom tags/groups will
use separately declared fixture dictionaries rather than changing this canonical
file or disabling validation globally.

Build the independent peer library with verified source, dictionary and patch hashes:

```sh
sh zfix/interop/build-quickfix.sh /tmp/zfix-deps
```

This requires curl, SHA-256 tools, tar, patch, cmp, CMake, a C++17 compiler and OpenSSL
headers/libraries. It builds Release with SSL, without upstream example/test
programs, and installs only under the selected root's `install` directory.
The exact dictionary remains under that root's pinned source directory. The
script preserves configured network proxy and CA trust. QuickFIX is a fixture
dependency; `libZfix` never links it.

The first upstream SDK build emits OpenSSL deprecation warnings. Repository-owned
helper/framework builds are warning-free. One early provisional SDK TCP process
ended with SIGSEGV without a core; its cause remains undiagnosed. It did not recur
in five debugger runs, 20 SDK-ASan fresh connections or 100 ordinary fresh TCP
runs. The later memory and process checks do not establish that anomaly's cause.

The script derives exact original and patched file states from the verified
archive. Every affected file must match either the complete original set or the
complete patched set; drift/mixed application is rejected. Unchanged files are
left alone. Each affected file is verified after applying.
A repeated invocation verifies the patch and reuses CMake's existing build.
Rebuild the SDK and fixture consumers together; the SSL acceptor's new port map
changes its class layout. Keep source/installed SDK headers and libraries matched.

The small dependency patch corrects the fixture prerequisites, TCP shutdown and observed TLS
memory failures:

- `SocketServer` stores each listener's actual bound port. Multiple sessions
  configured with port zero obtain distinct listeners; fixed nonzero ports still
  share their existing listener. SSL routing uses actual ports and exposes the
  same `sessionToPort()` view as TCP. Unsigned port handling preserves high
  ephemeral ports without changing `SocketInfo` layout.
- TCP acceptor shutdown closes the server on the caller only when no worker
  is processing it. Active workers close and delete their own server before
  publishing completion and being joined. Polling retains caller-side closure.
- The SSL initiator sets DNS or numeric-IP reference identity before registering
  its connector socket, retaining existing CA verification and SNI. IPv6 brackets
  are normalized for the certificate parameter; the pinned upstream connector
  still has IPv4 transport semantics. This patch does not add IPv6 networking.
- Certificate/CRL diagnostics go to stderr with their original text intact,
  leaving stdout available for strict JSON readiness/results. Fixture LogFactory
  output must also use stderr.
- TLS initialization uses OpenSSL's system entropy polling instead of passing
  uninitialized stack bytes into its random generator. Read-only certificate
  algorithm inspection borrows its public key; CA aggregation releases temporary
  stacks, duplicate names and directory-path allocations after transferring
  retained names. These repairs address actual Valgrind failures in the SDK probe.
  OpenSSL documents [system entropy polling](https://docs.openssl.org/3.5/man3/RAND_add/)
  and [borrowed certificate public keys](https://docs.openssl.org/3.5/man3/X509_get_pubkey/).

## Dependency routing and TLS check

After building the SDK and configured fixture programs:

```sh
make -C zfix/interop -j8
make -C zfix/interop dependency-test
```

`quickfixprobe` uses QuickFIX's own session/parser, memory stores and TCP/TLS
initiator/acceptor classes; it includes neither native FIX parser nor session
headers. Ten TAP scopes cover one socket server's distinct ephemeral ports and
fixed-port reuse; duplicate CA-name aggregation across a file and directory;
two real FIX sessions routed through separate port-zero
listeners over TCP and verified TLS; DNS/IP SAN success; trusted-chain DNS/IP
mismatch; and untrusted CA refusal before FIX Logon. Two shutdown scopes add
eight two-session TCP rounds with forced and graceful stop. Both peers reach
Logon before the initiator and acceptor are stopped and joined, in that order.
Applications, stores, logs and callback state remain alive through both joins.
The regression checks session readiness and sanitizer-clean shutdown; forced
stop does not promise a logout callback for every session. Main waits on
callback semaphores; workers never block to establish fixture ordering. No
sleeps, SDK interception or sanitizer suppression establish shutdown ordering.

`dependency-test.py` creates a disposable CA, SAN certificates and keys through
OpenSSL. It verifies the TAP plan, then reruns the six session cases through the
probe's JSON interface: every stdout line must decode as a readiness/result
record, and TLS certificate diagnostics must remain in stderr. Successful fixture
keys/logs are removed; failed runs use the repository's Python residue helper.
Pass `--artifacts NEW_DIRECTORY` directly to the script to retain a successful
run's configuration/diagnostic evidence. These are SDK prerequisites, not the
four-pairing workload/protocol interoperability gate.

The `quickfix`/`quickfixd` fixture programs use the independent SDK's own session,
parser and bounded message store. Their shared workload model does not share
native FIX wire/session code. The canonical four-pairing matrix passes 76
process cases; identity/TLS security passes 40; directional fault coverage
passes 104; the historical duplicate-attachment run covered eight cases;
checkpoint restart/replay/reset passes 24; and the previous Native
synchronization-probe timeout passed six. Each includes
TCP and verified TLS. Stage 9 measurements have separate acceptance gates. See
[status](../STATUS.md) and [the plan](../../zfix_plan.md).

Run the canonical and directional fault profiles with retained evidence:

```sh
python3 zfix/interop/matrix.py --profile canonical --artifacts /tmp/zfix-canonical
python3 zfix/interop/matrix.py --profile faults --artifacts /tmp/zfix-faults
```

Artifact directories must be new. The canonical profile includes scalar,
multiple sessions, bounded retention, group/text payloads, pacing/duration,
cancel/replace, quota refusal and separately declared native 44A/T11 modes.
The fault profile exercises both directions of fragmentation, frame loss,
duplication, unknown tags, CheckSum and BodyLength corruption, plus malformed
order quantity, recording each receiver's declared behavior.

The pinned QuickFIX acceptor binds wildcard IPv4 addresses. `quickfixd` exposes
that effective default as `--bind 0.0.0.0` and rejects unsupported bind-address
values with exit status 2. The native `zfixd` default remains `127.0.0.1`.
Matrix clients connect through localhost; reports record this bind difference
when comparing the peers. No SDK bind-address feature was added.

## Real-peer identity and TLS rejection

After building all four peers, run:

```sh
make -C zfix/interop security-test
```

`security-test.py` emits 40 TAP results across native/native, native/QuickFIX,
QuickFIX/native and QuickFIX/QuickFIX. Each pairing covers a valid TCP session;
wrong FIX SenderCompID and TargetCompID over TCP and TLS; verified TLS with DNS
and numeric-IP identities; trusted-chain DNS/IP mismatches; and an independent,
untrusted client CA. Certificate cases connect directly without a TLS relay.
CA and reference-identity checks remain enabled.

Valid counterparts complete Logon, three order/fill exchanges and Logout.
Each negative case requires both process exits to be 1, no initiator Logon
readiness, and no admitted/completed/reported business work. Certificate failures
also require zero FIX frames in both directions on both peers. QuickFIX client
diagnostics must report verification errors 62 (DNS), 64 (IP) or 20 (issuer),
as appropriate. Native diagnostics currently expose source locations rather
than the TLS verification reason; their result establishes controlled refusal,
without attributing a generic Logon deadline to a specific TLS error.

The fixture verifies its certificate controls with OpenSSL before launching
peers: valid DNS/IP identities pass, mismatched leaves retain trusted chains and
valid alternate SAN identities, and the independent CA cannot verify the leaf.
Only the declared identity, certificate or trust input changes from the valid
counterpart. The canonical dictionary hash, SDK lock, binary hashes, exact
commands, readiness/results, diagnostics, certificate controls and TAP are
retained under the printed artifact directory, including successful runs.
Set `--artifacts NEW_DIRECTORY` when invoking the script directly to choose it.
The manifest records native loopback versus QuickFIX wildcard listener binding.

The separate QuickFIX duplicate-policy fixture below establishes ordering while the
original client remains connected.

## QuickFIX duplicate active-session policy

```sh
make -C zfix/interop duplicate-test
```

`duplicate-test.py` emits four TAP results: native and QuickFIX initiators each
connect to a QuickFIX acceptor over TCP and verified TLS. This gate checks the
SDK's active-session rejection policy. It does not run a native acceptor or
establish native connection displacement behavior; that repair coverage remains
missing. The former eight-case refusal gate is historical and does not define
native session policy after removal of the attachment registry.

After the original client's actual Logon readiness event, the parent sends that
owned process SIGSTOP and waits for its stopped status with
`waitpid(WUNTRACED)`. It records the established socket's inode, descriptor and
peer before launching a second client with the same FIX identity. The paced
original workload uses four orders at one order per second; readiness and OS
stop confirmation establish the ordering, without a timed sleep.

QuickFIX must refuse the duplicate, which exits 1 without Logon readiness or
business admission. Before resuming the original, the same socket must still be
established, detecting
replacement/disconnection even if an initiator could otherwise reconnect.
After SIGCONT, the original must complete exactly four order/fill exchanges and
clean Logout with the acceptor; both exit 0 and report no errors or extra work.
If the original exits before its stopped-state precondition, the fixture fails.
Cleanup always continues a stopped child before terminating its owned process
group. No third-party or unrelated process is signaled.

The gate is Linux-only because it combines POSIX stopped-child status with
owned-process socket state from `/proc`. Other platforms emit an explicit TAP
skip, which does not constitute a passing platform gate. All commands,
dictionary/SDK/binary pins, JSON, diagnostics, ordered events and results remain
in the printed artifact directory. Use `--artifacts NEW_DIRECTORY` to select it.

## Checkpoint restart, persisted replay and explicit reset

```sh
make -C zfix/interop lifecycle-test
# Retain a specifically named successful run:
python3 zfix/interop/lifecycle-test.py \
  --dictionary /tmp/zfix-deps/install/share/quickfix/FIX44.xml \
  --artifacts /tmp/zfix-lifecycle
```

`lifecycle-test.py` passes 24 cases: all four pairings over TCP/TLS, each with
ordinary checkpoint restart, persisted-history replay and a separate explicit
reset. One acceptor remains running for eight orders across two fresh four-order
client processes. Client
one writes its bounded application checkpoint after network join and owner
drain. Client two restores it with `--state-in` and `--key-offset 4`; its new
business IDs continue after the completed first phase. Profiles use one session
and zero warmup. Native and SDK checkpoint formats are separate, versioned
same-host fixture files, with identity/completed-key/budget validation.

Ordinary restart preserves the next FIX sequence in both directions: the relay
requires the second Logon to use exactly one more than the previous connection's
last original sequence, without `141=Y`. Explicit reset instead uses a fresh
client and `--reset-logon`; both second Logons must carry sequence one and
`141=Y`. The acceptor must also be configured to agree to that reset. Its business
cache remains owned across connections; ordinary reconnect does not reset it.
SDK checkpoints retain bounded exact wire records and original SendingTime;
the load test additionally rejects identity/key mismatches, corrupt checksums,
trailing bytes and storage-budget violations.

The recovery profile sets server `--recover-rx 1` to model a stale receive
checkpoint after the first connection fully drains. Only that receive boundary
moves backward; sender state, retained history and the business cache remain
owned. On the second connection, the high Logon triggers an actual ResendRequest.
The fresh restored client must replay every old D with PossDup, the exact original
body hash and OrigSendingTime, while gap-filling unavailable administrative
history. Cached business replies increment `cached`; the client diagnoses them
through `oldReports` without creating new calls. Both four-order phases must
still complete exactly eight unique business orders in total.

The driver waits for the first client process to exit and the acceptor's actual
`reconnect_ready` event before starting the second client. Native reports an
`owner-drained` barrier. QuickFIX keeps the same acceptor/Session and reports
`sdk-drop-before-accept`: its single IO thread closes the dropped socket and
unregisters the old session before accepting the next socket. The event does not
claim that the old SDK connection object has already been destroyed.

Each case retains exact commands, binary/dictionary/SDK hashes, checkpoint hash,
ordered readiness/drain events, strict JSON results and both connection wire
transcripts. Continuity/reset checks and actual persisted-body replay are
separate profile assertions; a successful reconnect alone cannot pass recovery.

## Published liveness-probe timeout

```sh
make -C zfix/interop probe-test
```

`probe-test.py` specifies six cases: native/native, native/QuickFIX and
QuickFIX/native over TCP/TLS. Both initiators must reach Ready and admit business
after Logon and actual recovery, without waiting for a synchronization exchange.
Native then publishes a receive-idle liveness TestRequest. A verified relay passes Logon, then drops every frame
in one selected direction, including the peer's actual Heartbeat response.
The detailed trace must contain a published TestRequest with nonempty tag 112
and a later dropped Heartbeat carrying that exact opaque value. Native must
emit `FIX session timeout`, exit one and report failure before the application's
separate deadline. Deadline expiry alone cannot pass the fixture.

Neither initiator may withhold all ordinary business admission pending that
liveness echo. The current source requires nonzero admitted work, zero completed
work in the silenced direction, and the matching dropped echo plus Native's own
timeout diagnostic. See [STATUS.md](../STATUS.md) for current repair execution evidence.
QuickFIX/QuickFIX is excluded from this Native-specific
timer oracle. All six detailed traces, JSON records, diagnostics, declared
phases, binary/dictionary pins and valid TAP remain under the printed artifact
directory; pass `--artifacts NEW_DIRECTORY` to choose it.

The relay's optional `--summary` mode suppresses per-frame/per-fault records and
body hashes while retaining aggregate frame, fault, chunk and byte counts.
Protocol fault/probe gates use detailed traces; measurement harnesses can use
summary mode to avoid per-message transcript work.

## Bidirectional canonical codec check

Enable the fixture dependency when configuring the repository:

```sh
./configure [your existing options] --with-quickfix=/tmp/zfix-deps/install
make -C zfix -j8
make -C zfix/interop codec-test
```

`--with-quickfix=yes` uses pkg-config (`quickfix >= 1.16.0`); the prefix form
also supports the pinned CMake installation. Configure checks the peer's actual
SSL configuration/header and link boundary. `QUICKFIX_DICT` can supply the
canonical dictionary path; by default it is `PREFIX/share/quickfix/FIX44.xml`.
The codec runner verifies the dictionary's pinned SHA-256 before using it.
QuickFIX flags belong only to the independent fixture, and an unconfigured
`codec-test` fails explicitly.

The native helper writes twelve FIX 4.4 frames: scalar, text, nested groups,
a 32-party/eight-subparty group-heavy shape, acknowledgement/fill reports,
cancel and replace, plus four binary cases. The binary cases are two `35=n`
XMLDataLen/XMLData 212/213 frames using native String and Bytes mappings, a Logon
95/96 credential frame, and an order with StandardTrailer 93/89. Their payloads
include SOH, NUL, equals signs, high octets and embedded FIX-looking fields. The
independent parser checks these with the canonical dictionary and all default
validation settings, serializes them with QuickFIX, and the native helper checks
IDs, quantities, decimals, timestamps, text, binary octets and every group entry
after roundtrip. Each borrowed binary value's pointer and extent must match its
payload range in the corresponding original native or QuickFIX input allocation. The trailer signature is a literal byte fixture for structural
interoperability; cryptographic verification is established separately by
`ZfixSignTest` and the managed signing integration fixtures. The new canonical
codec cases passed the 2026-11-01 clang release run; earlier recorded verification
counts above remain historical evidence.

The Binary/API interop gate used Ubuntu clang 21.1.8 with GNU C++2b and
`-march=x86-64-v2 -O3 -g -DNDEBUG`, the existing matched QuickFIX Release SDK at
`/tmp/zfix-qf-release/install`, and its pinned canonical FIX44.xml. No external
SDK or prerequisite module was rebuilt. `make -C zfix/interop -j8` and
`make -C zfix/interop test` passed: the twelve-frame codec scope, five workload
scopes, ten dependency scopes and six independent TCP/TLS/negative diagnostic
scenarios. Native binary spans matched the original input payload addresses on
both directions of codec conversion. Build/test logs are
`/tmp/zfix-ctor-interop-build.log` and
`/tmp/zfix-ctor-interop-test.log`; this gate covers those fixtures rather
than rerunning the historical process matrices. Final executable/library/SDK hashes
are in `/tmp/zfix-ctor-interop-manifest.json`. Binary pointer inspection uses
the payload tag; inspecting the primary tag returns the decimal length range.
A preserved twelve-frame corpus, source/ELF hashes and replay evidence are under
`/tmp/zfix-interop-preserved-qidn36u0`.

The independent fixture includes no Zfix codec/session headers, enforced by
compile-time guards. Its ELF dependency list contains the common foundation,
QuickFIX, and Zi for cold-main checkpoint file I/O, and excludes libZfix/libZfixFmt.
The independent peer still uses QuickFIX for networking, framing and sessions.

| Input | QuickFIX default expectation | Native expectation checked |
| --- | --- | --- |
| Canonical scalar/groups/text D/8/F/G | Valid | Values preserved after peer serialization |
| Canonical 212/213, 95/96 and 93/89 | Valid | Exact opaque payload bytes preserved |
| Extra unknown body tag 9999 | `InvalidTagNumber` | Skip it and preserve known fields |
| Corrupted CheckSum | `InvalidMessage` | Existing native framing negative gate |

The native producer/consumer can be instrumented with ASan and the independent
validator can run through `libtool exec valgrind`; see the acceptance evidence in
[STATUS.md](../STATUS.md). The native frame producer/consumer is the explicit `interop/nativecodec`
fixture. Isolated workload unit tests and their installed-consumer entry points
are documented in [test](../test/README.md); native network suites belong to
[itest](../itest/README.md).

This codec check covers independent message conversion. The process gates above
cover live peer pairings, order progression, transport faults, duplicate
attachment, checkpoint restart/reset and liveness-probe timeout.
Benchmark measurements and configured toolchain/memory gates remain separately
reported in [STATUS.md](../STATUS.md).
