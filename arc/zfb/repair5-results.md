# repair5 implementation and validation

All eight planned source repairs, the user-directed handshake stack repair,
and the runtime assertion fixes are implemented and validated. This is the
evidence record for `repair5.md`. Both runtime assertion causes were repaired:
missing padding for tiny Handshake packets and incorrectly ordered fixture
teardown. Strict assertions remain. The final runtime Valgrind check passes
all TAP scopes but retains an independently reproduced 32-byte core TLS wrapper
leak; it is not an unqualified clean memory pass.

Valgrind must run outside the sandbox with networking available. The current
session has sandboxing disabled and networking enabled; memory checks use
`libtool exec` directly on the local system.

## Configuration and build discipline

The existing generated Makefiles specify `clang++`, GNU C++2b,
`-march=x86-64-v2 -g -DZDEBUG`, and the existing framework warning and ABI flags.
No configure, autoreconf, clean, compiler switch, platform switch, or alternate
release/debug/sanitizer build has been requested or run for this repair.
Source changes for all eight findings were batched before starting the first
incremental top-level `make -j4`. Existing artifacts are reused. Only one build
runs at a time. Dependent source APIs were updated before dependent rebuilding.

## Ownership changes and allocation identities

The following counts identify the allocations in the matched production source
paths. They exclude hash tables/buckets, range-tree entries, buffer growth,
framing buffers, and application callbacks. Runtime heap measurements and object
layout evidence are recorded separately below; these counts do not imply a
measured latency improvement.

| Path | Before | After |
| --- | --- | --- |
| Outstanding flow | `new Flow` plus `Flows::add(key, ref)` | `Flows::add(key)` allocates the flow node directly |
| Retained CRYPTO fragment | `CryptoRxBufAlloc` plus `CryptoRxPQueue::Node` | pooled `CryptoRxBufAlloc` includes the priority-queue node |
| SSE event | `SSEBuf` plus `SSEQueue::push(SSERecord)` | pooled `SSEBuf` includes the list node and terminal flag; `pushNode` |
| Stdio receive chunk | `StdioBuf` plus `StdioRxQueue::push(StdioRecord)` | pooled `StdioRxBuf` includes the list node; `pushNode` |
| Browser session | `BrowserSession` plus `browserSessions.add(cookie, ref)` | `BrowserSessions::Node` is the session; `addNode` |
| QUIC stream | Rx stream node plus Tx reference hash node | Rx stream allocation includes the Tx hash node |
| Writable stream enqueue | reference queue storage | stream allocation includes list links; `pushNode` does not allocate |
| Endpoint link | application link plus endpoint reference hash node | application link includes the endpoint hash node; `addNode` |

Rx and Tx stream registries retain independent owning references because they
retire on different shards. The writable list also retains its former independent
reference: queued notifications may survive Tx-registry retirement. Its links
are embedded, deduplication still uses `txQueued`, and explicit Tx queue clears
reset that flag before releasing queued ownership. Endpoint membership retains
an owning reference alongside outstanding callbacks and uses pointer identity.
All inner stream/link/buffer nodes disable their allocator with an empty heap ID;
final concrete object and buffer allocators remain responsible for allocation.

The stdio Tx wrapper remains intentional: it accepts arbitrary caller-owned
buffers that may be shared independently. STREAM packet slices, JSON-RPC batch
call records, and shared Ztc alert buffers remain outside this repair.

## Validation log

- `git diff --check`: passed before the build milestone.
- `make -j4 > /tmp/repair5-build.log 2>&1`: stopped at two stale debug-verifier
  wrapper accesses; corrected to direct flow-node accesses.
- `make -j4 > /tmp/repair5-build-2.log 2>&1`: QUIC library/tests/examples compiled;
  HTTP/3 exposed a final-type declaration cycle in the initial stream composition.
  Factored common stream data and memberships to eliminate that cycle.
- `make -j4 -k > /tmp/repair5-build-3.log 2>&1`: interrupted after finding a
  `ZmListDerive` argument-parenthesization error in that refactor; corrected.
  The matched layout probe then compiled both old and corrected compositions.
  No application tests were run on an incomplete dependent build.
- `make -j4 -k > /tmp/repair5-build-4.log 2>&1`: passed, exit 0.
  All enabled modules, dependent binaries, examples, and integration targets
  compiled using the retained configuration and existing artifacts.
- `python3 /tmp/repair5-run-tests.py > /tmp/repair5-test-run.log 2>&1`:
  exercised 89 binaries and 580 TAP scopes across QUIC, HTTP, JSON-RPC, MCP,
  Ztc, Zum, WebSocket, D-Bus, and GTK. All dependent groups passed. The only
  failure was a new handshake test expecting the asynchronous callback on the
  unchanged in-order borrowed-span path. Corrected that expectation to verify
  the returned span and absence of a callback.
- `make -j4 -C zquic/test ZquicHandshakeTest >
  /tmp/repair5-handshake-build.log 2>&1`: passed; only the changed test rebuilt.
- From `zquic/test`, `prove -j1 ./ZquicHandshakeTest >
  /tmp/repair5-handshake-rerun.log 2>&1`: passed (4 TAP scopes).
  All 89 exercised binaries now pass. Original per-group commands, working
  directories, and outcomes remain under `/tmp/repair5-tests`; the original
  failed assertion is preserved in its log.
- `python3 /tmp/repair5-memory.py > /tmp/repair5-memory-run.log 2>&1`:
  completed the initial final-memory batch. Exact commands and initial results
  are recorded under `/tmp/repair5-memory`. The initial handshake result is
  superseded by the stack repair and its final check described below; runtime
  follow-up checks are described below. The live Ztc fixture passed.
- `valgrind --quiet --error-exitcode=99 /bin/true`: passed. Valgrind can start in
  the current environment; the earlier loader-symbol startup failure did not
  recur. Application memory checks follow the successful dependent rebuild.
- Pre-rebuild GDB inspection of the existing QUIC shared library confirms
  `sizeof(Zquic::RxData) == 16`. Most old application/header-only types are not
  present in its DWARF. A matched before/after layout probe is under
  `/tmp/repair5-layout` using saved pre-edit headers and the current compiler
  configuration; its completed measurements are recorded below.

## Matched object layouts

`python3 /tmp/repair5-layout/run.py` passed. It compiled two measurement objects
sequentially with the current Makefile compiler and flags, using saved pre-edit
headers for the before measurement. `nm -S --radix=d` reads the sizes of BSS
arrays whose extents are `sizeof` the corresponding types. No library build
configuration was changed. Exact probe commands are in
`/tmp/repair5-layout/commands.txt`; compile logs and symbol tables are alongside
it. Browser and flow probes reproduce the exact production data/container
compositions, since those types are implementation-private.

Sizes below are C++ object bytes, excluding allocator rounding and bookkeeping.
Before values with `+` are separately allocated objects. The stream/link fixtures
instantiate the complete common QUIC types without application-specific fields.

| Allocation | Before bytes | After bytes |
| --- | ---: | ---: |
| Outstanding flow and hash node | 48 + 40 | 72 |
| CRYPTO receive buffer and PQ node | 1600 + 64 | 1664 |
| SSE buffer and list record | 1152 + 40 | 1216 |
| Stdio receive buffer and list record | 1152 + 32 | 1216 |
| Browser session and hash node | 344 + 56 | 360 |
| Complete Rx stream node | 1344 | 1344 |
| Common link | 121344 | 121408 |

The stream's added memberships fit existing alignment padding in this fixture.
The common link grows 64 bytes, including for client links without endpoint
membership. CRYPTO builtin payload capacity remains 1488 bytes. SSE and stdio
receive builtin capacity remains 1040 bytes. The unchanged generic stdio/framing
buffer remains 1152 bytes with 1040 bytes of builtin payload. The fewer
allocations do not imply lower `sizeof` totals on every buffer path; pooled
allocation granularity must be considered separately.

## Handshake stack root cause and repair

Investigated the default-Valgrind stack-switch warning without rebuilding or
changing compiler flags. The current handshake binary's `exchangeTLS_()`
prologue reserves `0x4414a0` = **4,461,728 bytes (4.255 MiB)**. The earlier
"4.46 MiB" description confused decimal MB with MiB. Valgrind's default
`--max-stackframe` threshold is 2,000,000 bytes.

GDB `sizeof` measurements account for the frame:

| Locals in `exchangeTLS_()` | Bytes |
| --- | ---: |
| Two `TLSQueue` objects | 4,195,344 |
| Two `TLSPeer` objects | 266,000 |
| Compiler bookkeeping/alignment | 384 |
| Total frame reservation | 4,461,728 |

`TLSBufSize` is 65,536, `MaxTLSMessages` is 32, and each `TLSMessage` is
65,552 bytes including epoch, length, and padding. Each queue embeds all 32
message slots, making `sizeof(TLSQueue)` 2,097,672. Both direction queues are
local variables even when only a few small messages are queued. Their embedded
payload arrays are zero-initialized at construction. This worst-case capacity
reservation is 94% of the frame and is the primary cause.

Each `TLSPeer` also embeds four Tx and four Rx `CryptoStream` objects;
`sizeof(CryptoStream)` is 16,624 and `sizeof(TLSPeer)` is 133,000. Each stream
has two 8 KiB builtin buffers. Nested helpers add further stack use:
`drainTLS_()` reserves 65,600 bytes for its full-size copied message;
`enqueueFlight_()` reserves 65,664 bytes including another 64 KiB frame buffer.
`TLSQueue::pop()` copies the whole message slot regardless of actual payload
length and shifts remaining full-size slots with `memmove`.

`git blame` attributes the fixed queues and local peer/queue declarations to
commit `7901010f` (2026-06-03), before the intrusive-container repair. The
source and compiled frame agree: the dominant allocation is an existing
fixed-capacity test-fixture design. Production link-owned CRYPTO streams are
members of their allocated link object; they do not explain this fixture frame.

Evidence: GDB `sizeof` on the current binary, `objdump -d -C` saved to
`/tmp/repair5-handshake-disassembly.txt`, source lines 89–186 of
`zquic/test/ZquicHandshakeTest.cc`, and `valgrind --help` for the default.
The diagnostic rerun with `--max-stackframe=8388608` reports zero memory errors
and zero definite/indirect leaks, but **does not resolve or accept the excessive
stack usage**. Its logs are `/tmp/repair5-memory/ZquicHandshakeTest.stack.*.log`.

The repair should replace fixed message-slot arrays with dynamically populated
intrusive queues of pooled I/O buffers carrying epoch metadata. Write frames
directly into the queued buffers, then move node references on dequeue; remove
the 64 KiB frame/message temporaries and full-slot copies/shifts. Keep peer
state in an explicitly heap-managed fixture if needed rather than placing all
16 buffered stream objects on the stack. Moving the entire fixed queue to the
heap would retain the unnecessary worst-case capacity and copying. Acceptance
requires measuring the reduced frame and running Valgrind with its default
stack-frame threshold. The user authorized this additional source repair, now implemented. The
queued message is a `ZiIOBuf` carrying its epoch and intrusive list links,
allocated through `Zquic.Test.TLSBuf` with framework-default builtin capacity
and the existing CRYPTO maximum. Frames are encoded into the final buffer;
dequeue moves a node reference. Peer states use `Zquic.Test.TLSPeer` and
`ZuPtr` ownership, with all four Tx/Rx epoch elements explicitly constructed.

| Function frame reservation | Before bytes | After bytes |
| --- | ---: | ---: |
| `exchangeTLS_` | 4,461,728 | 224 |
| `drainTLS_` | 65,600 | 144 |
| `enqueueFlight_` | 65,664 | 288 |
| `handleTLS_` | 1,008 | 1,024 |

Only `ZquicHandshakeTest` rebuilt incrementally with `make -j4 -C zquic/test
ZquicHandshakeTest`, retaining all compiler/configuration flags. Its TAP run
passed. Valgrind then caught missing explicit lengths in the new peer's
`ZuArray` initialization, leaving stream elements unconstructed; corrected to
`tx(4), rx(4)` and rebuilt that test. The final default-threshold Valgrind run
passed all four scopes, with **zero errors and zero definite, indirect, or
possible leaks**, without suppressions or a `--max-stackframe` override. Logs:
`/tmp/repair5-memory/ZquicHandshakeTest.final.memory.log` and
`/tmp/repair5-memory/ZquicHandshakeTest.final.output.log`.

## Final memory checks and follow-up qualifications

Checks ran unsandboxed with networking enabled through `libtool exec`, using
Valgrind 3.25.1 and the existing binaries/configuration. The standalone commands
use `--leak-check=full --show-leak-kinds=definite,indirect
--errors-for-leak-kinds=definite,indirect --error-exitcode=99`, with separate
memory and test-output logs. No suppressions were used.

| Check | Final result |
| --- | --- |
| `ZquicRecoveryTest` | Exit 0; zero errors, zero definite/indirect leaks |
| `ZquicHandshakeTest` after stack repair | Exit 0; zero errors/leaks; default stack threshold |
| `ZquicAPITest` | Exit 0; zero errors, zero definite/indirect leaks |
| `ZjrpcWireTest` | Exit 0; zero errors, zero definite/indirect leaks |
| `jrpcstdiotest` | Exit 0; zero errors, zero definite/indirect leaks |
| `zmcphttptest` | Exit 0; zero errors, zero definite/indirect leaks |
| Live `ztchubtest.py`, `ZTC_VALGRIND=1` | Exit 0; both hub processes report zero errors and zero definite/indirect leaks |
| `ZquicRuntimeTest` | All 5 scopes pass after assertion fixes; existing 32-byte TLS wrapper leak remains |

The live hub fixture used `PYTHONPATH=zi/itest:zum/itest`, the current
`zdb_sqlite/src/.libs/libZdbSL.so` module, and a fresh database under
`/tmp/repair5-ztc-db-cw8nak59`. Its built-in Valgrind mode uses error exit 97;
all hub shutdown statuses were zero. The fixture exercises OAuth, WSS routing,
overflow, restart, cookie consumption, and shutdown with an armed browser expiry
callback. Output and both complete memory reports are retained in
`/tmp/repair5-memory/ztchubtest.log`.

The first full runtime check passed its TAP assertions but reported a 32-byte
`ZmSpecific_Object` allocation made by `ZmThread_Main` during shared-library
startup. A confirmation run reported zero memory errors and zero leaks, but
failed the endpoint's `!diag(clientLink).failures()` assertion. Thus the runtime
result has varied; neither run is described as an unqualified combined pass.

A pre-repair probe used saved original QUIC headers and `ZquicCrypto.cc`, the
unchanged runtime test, retained compiler flags, and reused unchanged QUIC/core
objects. It did not link the repaired `libZquic` (verified with `readelf -d`).
The full probe failed during multi-connection teardown with a freed-link read
and scheduler assertion; its aborted leak counts are not accepted as a baseline.
A reduced endpoint-only pre-repair probe reproduced the same diagnostics
assertion, with zero memory errors/leaks. These probes do not establish a clean
full pre-repair memory baseline. Commands and logs are under
`/tmp/repair5-runtime-baseline` and `/tmp/repair5-runtime-baseline-subset`.

A separate minimal control using only the unchanged core
`ZmSpecific_Allocator` reproduces the 32-byte wrapper leak: its destructor calls
`pthread_key_delete` without deleting the current thread's `ZmSpecific_Object`;
reuse and clearing of that key removes the remaining TLS pointer. This is an
existing core allocator lifecycle defect, independent of QUIC container nodes.
The control was compiled with the current `zm/test/Makefile` flags and core
artifacts; source, exact command, and Valgrind report are
`/tmp/repair5-tls-control.cc`, `/tmp/repair5-tls-control-command.txt`, and
`/tmp/repair5-tls-control.memory.log`. It deliberately exits 99 with one 32-byte
lost block. The core allocator has not been changed or suppressed by this repair.
The unchanged `ZmTIDTest`, `ZmSchedTest`, and `ZiMxLoopTest` controls passed with
zero memory errors/leaks; the TLS leak is not present in every workload's exit
report.

## Final review and limitations

All eight planned intrusive-container repairs and the authorized stack repair
are implemented. The initial full configured dependent build passed incrementally
with `-j4`; the handshake test repair was then rebuilt and checked separately.
The later runtime assertion investigation below adds packet-padding source fixes
and another incremental affected-dependent build. All 89
normal test binaries passed, with the initial new test expectation corrected
as recorded above. Final `git diff --check` passed. No obsolete CRYPTO Rx,
SSE, Tx stream, writable queue, or endpoint reference-wrapper declaration
remains in the changed production paths. Genuine shared-buffer exceptions
remain documented and unmodified.

The final additional runtime check is retained at
`/tmp/repair5-memory/ZquicRuntimeTest.final.*.log`: it again reported the same
32-byte core TLS wrapper leak, with no invalid-access/uninitialized-value
errors, and failed the endpoint qlog `drain_expired`/`aborted` assertion. No
further repeats were run to obtain a favorable timing result. At that stage the full runtime Valgrind check was qualified by the existing core
TLS lifecycle defect and unresolved diagnostics and close/drain assertions.
Those assertions were defects to investigate and repair, not accepted flakes;
the subsequent investigation and repairs are recorded below.
The minimal allocator control establishes an independent core defect, not a
clean full-runtime before/after leak comparison. No new container-node leak or
corruption was reported in the repaired paths. The core TLS allocator defect
and pre-repair runtime teardown failure are not repaired by this change.

Only the current configured Clang/debug Linux build was exercised. No other
compiler, build mode, platform, sanitizer configuration, or latency benchmark
was run. Object-size and source allocation-identity evidence support allocation
consolidation; they do not establish a measured latency improvement.

## Runtime assertion investigation

The close/drain fixture used `waitDisconnect(mx)`, which waited on the
multiplexer's default shards 1/2 while both QUIC apps run on shards 3/4.
`forceCloseTimeout()` also posted work without waiting for execution. The next
`server.stop()` could mark the link disconnecting before expiry ran;
`closeTimeout_()` then returned without emitting `drain_expired`. The retained
failed qlog contains `peer_close_frame` and draining-to-closed state, but no
expiry event. This is a fixture ordering defect, not permission to accept
intermittent assertions.

The fixture now waits on the app's owning shards, completes forced expiry on
Tx, and observes the server's completed disconnect before stopping. Its test
server callback preserves the base server's membership/stop bookkeeping.
The policy-server case waits on the policy app, not the already-finalized
first app. Client qlog and separate Rx/Tx failure diagnostics are enabled for
the remaining diagnostics investigation. All five normal runtime scopes pass
in `/tmp/repair5-runtime-sync4-normal.log`.

One diagnostic attempt overlapped normal and Valgrind runs using the same
fixed UDP port and residue paths. Its results (`*.sync.*`, without `sync4`)
are invalid and excluded; subsequent runs are sequential and have distinct
residue directories. Intermediate test-hook mistakes (missing base callback
bookkeeping and a wait on the finalized first server) were corrected before
the passing normal run.

The old-source counter trace reproduced the handshake diagnostics failure and
identified the Tx increment at original `ZquicLink.hh:7814`, the failure return
from Handshake `PktProt::protectLongV`. The protect routine requires ciphertext
through `pnOffset + 4 + 16`; a PING-only probe with a one-byte packet number and
16-byte tag needs three plaintext bytes, not one. Handshake emission omitted
`padForProtSample`, so PTO probes failed depending on handshake scheduling.
The deterministic regression emits tiny long packets for all four packet-number
widths, checks sample coverage and encoded length, decrypts the result, and checks
PING/PADDING bytes. It also covers 0-RTT, whose existing padding was applied after
encoding the long-header length. Both paths now pad before writing that length.
The padding helper takes only packet-number and tag widths, since the required
plaintext minimum is independent of the header offset.

Trace evidence: `/tmp/repair5-runtime-diagcounter/trace1.log` contains exactly
`FAILURE tx ZquicLink.hh:7814` and the failed client diagnostics assertion.
The temporary old-header instrumentation reports only actual counter increments;
it is outside the repository. A debugger-assisted old-source probe did not
reproduce that counter failure and is not treated as a passing regression gate.

After correcting teardown ordering, all five runtime scopes also passed under
Valgrind (`/tmp/repair5-memory/ZquicRuntimeTest.sync4.output.log`). That run
retained only the 32-byte startup TLS-wrapper leak, with no invalid access or
uninitialized-value errors. This was not accepted as a fix for the diagnostics
assertion: the old-source counter trace then isolated the missing Handshake
padding, now repaired explicitly. Endpoint teardown barriers include both the
app-owned shards and the multiplexer's UDP endpoint shards in handoff order.

The padding source batch is complete. `make -j4 -C zquic/src` passed; the
incremental top-level `make -j4 -k` is recorded in
`/tmp/repair5-padding-dependent-build.log`. Follow-up tests and final runtime
memory checks remain pending that build.

## Final follow-up validation

The source batch and complete configured dependent rebuild passed:

- `make -j4 -C zquic/src`: exit 0,
  `/tmp/repair5-padding-src-build.log`.
- Incremental top-level `make -j4 -k`: exit 0,
  `/tmp/repair5-padding-dependent-build.log`. No configuration changes, clean,
  or parallel second build. No further production/test edits followed.
- `prove -j1` for all configured `zquic/test` and `zhttp/test` binaries:
  **46 binaries / 382 TAP scopes passed** (20/172 QUIC, 26/210 HTTP), including
  `testLongProbePadding`, `testEndpointOpen`, and HTTP/3 dependent cases.
- Final focused unsandboxed Valgrind, through `libtool exec`, using the default
  stack threshold and no suppressions: `ZquicAPITest` passed all 16 scopes with
  **zero errors and zero definite/indirect/possible leaks**.
  `ZquicRuntimeTest` passed all 5 scopes, including strict diagnostics and
  close/drain qlog assertions. It reports exactly one existing startup TLS
  wrapper leak (32 bytes), no indirect/possible leaks, and no invalid-access or
  uninitialized-value errors; its memory-check exit remains 99.

Commands, exit codes, complete test output, and memory logs are under
`/tmp/repair5-padding-checks` (`results.json`, `zquic.output.log`,
`zhttp.output.log`, `ZquicAPITest.*.log`, `ZquicRuntimeTest.*.log`). The runner
is `/tmp/repair5-padding-checks.py`. These final runs were sequential, with
distinct residue roots, and were not repeated to obtain favorable scheduling.
`git diff --check` passed.

The intrusive-container and fixture repairs are complete. No test failures are
accepted as timing-sensitive or intermittent. The unrelated core allocator
lifecycle defect remains explicitly recorded above; no suppression, stack-limit
change, or weakened assertion was introduced to obtain these results.
