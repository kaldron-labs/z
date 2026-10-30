# Link, session and connection state audit — 2026-10-29

> Historical pre-repair audit. Names, layouts and passing results below describe
> its captured source, not the current breaking repair. See
> [zfix_repair.md](../zfix_repair.md) and the latest [STATUS.md](STATUS.md).


Scope is the Zfix plan implementation, including its native and independent-peer
consumers. Existing TCP/TLS/Zi state was inspected as the lower-layer contract;
no dependency source or binary was changed for this audit. Runtime scope remains
Linux x64.

## Ownership

| State | Owner and access contract |
| --- | --- |
| Configuration and stable back-pointers | Constructor-published, immutable. Source declares configuration `const` and places it before mutable fields. |
| Session, receive sequence queue, framing, Rx trace, attachment, admission and work/posted credits | Application Rx. Tx and worker completions post mutations/refunds back to Rx. These are ordinary counters and fields. |
| Association/history, send/replay/archive progress, control/wire queues, Tx trace and completion/drain state | Application Tx. Rx posts retained payload handles and fixed metadata. |
| Physical connection, socket stream/queue, TLS record state and reconnect attempts | Existing Ztcp/Ztls/Zi owners. FIX neither shadows their socket counters nor adds another physical connection or stream. |
| Scheduler timer nodes | Shared through the existing scheduler lock. The three nodes are packed into one shared group, separated from Rx/Tx deadline, generation and stop metadata. Logical session deadlines remain Rx-owned. |
| Work and immutable frame/body references | Immutable published payload; reference counts are genuinely shared. Final work destruction/credit retirement is posted to Rx. |
| Independent SDK posted admission/error state | Genuinely shared among foreign callback threads and the application owner. Exact pre-post admission cannot be replaced by an Rx-only check after enqueueing. Its contended group has a separate cache-line boundary. |
| Persistent snapshots and result records | Cold, phase-owned DTOs transferred through existing post/semaphore/stop barriers. They do not authorize reading another owner's live state. |

Application Rx/Tx are distinct from the multiplexer's network Rx/Tx where the
hub uses separate shards. Wrappers add no redundant application/socket pointer.
Public dispatchers carry fixed metadata/retained handles; callbacks execute on
the destination owner. Tx-to-Rx callbacks now capture the destination pointer
directly, avoiding even an immutable back-pointer read from a Tx cache line.

## Removed state and consolidation

- Link keeps the specific immutable configuration it uses rather than the full
  `LinkLimits` aggregate. Role comes from Session configuration; the pending
  admission's connection comes from the current session. The admission ticket
  remains until the external continuation drains, even after Session clears its
  protocol admission state.
- Six physical lifecycle timers become one earliest-deadline timer. A separate
  gap timer remains. The callback reselects the current logical deadline and
  always synchronizes afterward; an earlier physical wake may remain when input
  activity postpones a logical deadline. Equal deadlines preserve enum order.
  Connection, epoch and per-arm serial guards still reject stale callbacks.
- Session probe activity/publication and reset preparation/arming derive from
  their existing challenge, sequence boundary and target epoch. The redundant
  peer-Logon flag is removed. Rx uses Session's Tx-recovery status rather than a
  second copy.
- Tx retains compact header identity/mode/sequence configuration; Rx-only
  heartbeat, role and timeout values are absent. Pending context derives from
  the sole pending association. Reset target/ticket values encode activity and
  scheduled work. Sequence task channels retain their own monotonically advancing
  tickets; the redundant common sequence ticket is removed.
- SessionTx uses lower Tx attachment/configuration and nonzero drain/boundary
  sentinels rather than another live bit, codec copy and two drain flags.
- Stored counters contain cumulative values only. Trace count comes directly from
  its list rather than a mirrored counter. Queue/history/control/in-flight
  snapshot fields are filled from the actual owners when requested. Tx no longer
  stores an empty WireStats block or fields owned by SessionTx. Raw omits unused
  read-ahead limits; RxFrame uses its embedded framer's immutable limits.
- Mixed-ownership `LinkTimer` objects are removed. The session, gap and idle
  scheduler nodes share one packed group before the Rx boundary. Session arm
  time and both Rx generations reside in Rx; idle flags remain in Tx. The gap
  deadline is calculated at arm time rather than retained without a reader.
- Native posted/work ledgers and the borrowed input stream now reside in the
  selected engine's Rx group through its `NativeRx<Net>` storage parameter.
  `LinkHooks_` and `NetInput` are stateless, including in Raw. Standalone engines
  use an empty extension with no per-instance storage. This removes the native
  adapter Rx region previously placed after engine Tx by multiple inheritance.
- Utility/example/fixture links have contiguous Rx and Tx groups with one boundary
  at each owner transition. Same-Rx ledgers no longer have redundant separating
  alignment. Proven single-owner/publication-only utility counters are primitive;
  exact cross-thread failure/admission/barrier counters remain synchronized.

## State deliberately retained

Physical transport state and FIX protocol state have different lifetimes. A
logical connection generation rejects old callbacks after socket replacement;
an identity attachment remains held through owner drain. Neither is equivalent
to a native socket pointer or transport telemetry registry.

Assigned, published, acknowledged and archived sequence positions describe
different progress. Replay requests, the active batch, asynchronous history
lookup and unavailable prefixes likewise represent distinct phases. The active
replay bit remains: eliminating it would require proving every transient generic
queue callback boundary, and gives no demonstrated footprint saving.

A wire queue can become empty before its asynchronously posted session completion
runs. Session/Raw in-flight counters therefore remain until their completion
continuations run. Similarly, an admission ticket and the sole teardown handle
must outlive the protocol's closed state. These are drain obligations, not
second copies of queue length or connection state.

The session timer's cached arm deadline is Rx-owned. The scheduler consumes its
own timeout while moving a callback into the owner's queue; the callback may
still be pending there. Reading that scheduler field directly would cross its
lock/ownership boundary. The cached arm time and serial preserve early-wake and
stale-callback behavior without such reads.

Per-link and hub credit ledgers enforce independent caps without duplicating
payload storage. Independently owned Rx/Tx configuration strings and cold
snapshots retain their required lifetimes. Link's small immutable codec copy
keeps Rx formatting reads outside a lower Tx cache line containing mutable
counters; no shared mutable configuration or new reference-counted config owner
was introduced.

## Layout and verification

The previous isolation pass separated several owner boundaries but needlessly
padded individual timers and left native adapter Rx state after engine Tx.
The corrected layout packs by ownership across the FIX engine and adapter
bases. Sizes below are Linux x64, configured Clang optimized/ZDEBUG/ASan.

| Concrete component | Before this layout repair | After | Reduction |
| --- | ---: | ---: | ---: |
| Standalone managed engine | 3,456 | 3,264 | 192 bytes |
| Managed TCP client | 4,096 | 3,840 | 256 bytes |
| Managed TCP server | 3,904 | 3,648 | 256 bytes |
| Managed TLS client | 4,352 | 4,096 | 256 bytes |
| Managed TLS server | 4,096 | 3,840 | 256 bytes |
| Raw TCP client/server | 1,344 / 1,152 | 1,344 / 1,152 | unchanged |
| Raw TLS client/server | 1,600 / 1,344 | 1,600 / 1,344 | unchanged |

The native managed engine includes 104 bytes of native Rx data and occupies
3,328 bytes. Its three 96-byte scheduler nodes begin at offsets 64, 160 and
256; Rx begins at 384 and Tx at 1856. The nodes occupy a single 320-byte shared
region including its terminal padding, rather than two 192-byte timer wrappers
plus Tx's separately padded idle node. No owner-local metadata shares these
scheduler lines. The native ledgers and input pointer are inside the Rx range;
Tx state and trace follow. Both native adapter bases are empty and have no
cache-line alignment of their own.

Native Raw moves the same ledgers/input into its Rx range without increasing
its complete object size. Standalone Raw remains 704 bytes. Its native engine
occupies 832 bytes; these are moved bytes previously held in sibling bases.

This is a FIX-owned storage consolidation. Existing Ztcp/Ztls transport bases
still contain their own physical-I/O Rx/Tx/reconnect regions. Application-derived
links can add their own Rx/Tx data. Consequently a complete link may still have
more than one Rx region across those layers; this audit does not claim global
owner contiguity across independently stateful dependencies or application
classes. No transport/dependency layout was changed by this repair.

Compile-only layout checks cover all eight managed/raw TCP/TLS client/server
compositions, including the native extensions, packed scheduler nodes,
shared/Rx/Tx boundaries and empty adapter bases. The unit link test now also
checks that TCP/TLS hooks and input adapters remain stateless.

Current repair verification and terminal logs are in
`/tmp/zfix-cache-layout/`; the earlier state/member ledgers and historical
verification remain in `/tmp/zfix-state-audit/`. Verification of this
repair used a frozen pre-concurrent-codec baseline plus the six repaired headers
and link test, avoiding declaration/caller changes in the active codec refactor.
The repaired headers and test match the workspace. Two private Zfix DSOs and
focused consumers were built with the configured Clang 21 GNU C++2b/O3/ZDEBUG/
ASan flags. No dependency module was rebuilt.

- Nine focused unit programs / 98 scopes pass.
- Ten TCP/TLS integration programs / 212 scopes pass: session, recovery, reset,
  Raw and posted ingress, including timer and teardown paths.
- Eight compile-only core compositions and 20 complete compiled client/server
  instance layouts pass packed-node, native Rx storage and shard-boundary checks.
- ASan/LSan report no findings; the private build emits no warnings. All eight
  dependency DSOs remain byte-identical.

The full current-workspace gate is not claimed: the initial workspace build
encountered the concurrent codec API transition. This repair's private build
and results are recorded in `verification.json` and the `snapshot-*.log` files. No throughput/latency improvement
is inferred solely from the smaller layout.

### Subsequent combined workspace verification

The codec transition is complete and the packed layout is now included in the
warning-free full current-workspace Clang 21/O3/ZDEBUG/ASan Zfix build. It passes
30 unit programs/320 scopes plus schema fixtures, 14 native integration
programs/225 scopes, independent codec/load/dependency checks, all 76 canonical
process cases and standalone TCP/TLS examples. No sanitizer findings; all eight
dependency DSOs remain unchanged. The repaired source and test hashes are part
of the final current-source manifest under `/tmp/zfix-path-audit/`, whose logs
supersede the earlier private-build limitation above. Layout sizes remain those
recorded in this report; no timing improvement is inferred.
