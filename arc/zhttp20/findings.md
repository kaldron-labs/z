# zhttp/src guideline audit

Audit date: 2026-10-18.

Scope: the current working-tree contents of 26 headers, 19 implementations,
and `zhttp/src/Makefile.am`, audited against the current `GUIDELINES.md` and
`AGENTS.md`. Existing modifications were included. The audit made no source
edits. References identify representative occurrences where a pattern repeats.

These findings extend the four previously recorded as resolved in
`zhttp/findings.md`. Explicit violations are distinguished from amber review
issues. Proposed remedies require establishing the owning component's contract
before implementation; this report does not authorize speculative repairs.

## Findings

1. **Rx/Tx ownership violation in H1 streaming.** `StreamBinding` is explicitly
   Rx-owned, but `streamTx_()` reads it on Tx through `m_stream.tx()`. The same
   pattern appears in native client/server links and TLS application links.
   Tx termination also mutates this Rx state through `end()`/`reset()`.
   Separate the Rx handshake state from Tx admission/termination state and
   dispatch mutations to their owners. See
   [ZhttpNative.hh:495](zhttp/src/ZhttpNative.hh#L495),
   [ZhttpNative.hh:701](zhttp/src/ZhttpNative.hh#L701),
   [ZhttpClientImpl.hh:1489](zhttp/src/ZhttpClientImpl.hh#L1489), and
   [ZhttpServerImpl.hh:653](zhttp/src/ZhttpServerImpl.hh#L653).

2. **Rx and Tx share a mutable RNG.** Under `ZiMultiplex_FILTER`, both
   packet-drop filters mutate the same `m_rng`. `ZmRandom` maintains mutable
   generator state; distinct Rx/Tx threads therefore contend on and race that
   state. Use separate shard-owned generators, with cache-line separation.
   See [ZhttpH3.hh:2146](zhttp/src/ZhttpH3.hh#L2146).

3. **Iterator outlives its traversal and container replacement.**
   `detachNeverIndex_()` keeps the iterator into `m_neverIndex` alive while
   assigning a replacement to `m_neverIndex`. This violates the explicit
   iterator-lifetime flag and leaves iterator teardown coupled to replacement
   of its underlying container. Scope the iterator before the assignment, as
   the QPACK equivalent already does. See
   [ZhttpHPack.cc:730](zhttp/src/ZhttpHPack.cc#L730).

4. **Explicit flush failures are discarded at control-message boundaries.**
   H2 initial SETTINGS ignores `tx.flush()` and then records SETTINGS as
   outstanding and arms its timeout. SETTINGS ACK, PING ACK, GOAWAY, RESET,
   and WINDOW_UPDATE similarly discard completion status. This violates the
   Tx-stream requirement to check explicit completion-boundary flushes and
   propagate synchronous failure. See
   [ZhttpH2Hub.hh:1175](zhttp/src/ZhttpH2Hub.hh#L1175) and
   [ZhttpH2Hub.hh:1589](zhttp/src/ZhttpH2Hub.hh#L1589).

5. **Alt-Svc reclamation is lazy and scan-based.** Expired alternatives are
   removed only when that particular origin is read. Updates for a new origin
   reject admission when the table reaches `maxOrigins`, even if every
   retained origin has expired. Thus stale entries can keep the bounded cache
   permanently full until those origins are revisited or explicitly deleted.
   This conflicts with deterministic reclamation and the prohibition on
   scanning containers for GC. See
   [ZhttpDiscovery.hh:315](zhttp/src/ZhttpDiscovery.hh#L315) and
   [ZhttpAltSvc.cc:283](zhttp/src/ZhttpAltSvc.cc#L283).

6. **Several I/O turns still perform whole-container work.** Connection
   WINDOW_UPDATE drains all connection-blocked streams; initial-window changes
   traverse all Tx windows twice; `clearFramesTx_()` drains every window and
   its frames. Ordered H1 response release/cancellation also uses unbatched
   queue traversal. Configurable limits do not supply a scheduler-turn bound.
   Batch these operations with continuations while preserving ordering. See
   [ZhttpH2Hub.hh:1856](zhttp/src/ZhttpH2Hub.hh#L1856),
   [ZhttpH2Hub.hh:1885](zhttp/src/ZhttpH2Hub.hh#L1885),
   [ZhttpH2Hub.hh:1912](zhttp/src/ZhttpH2Hub.hh#L1912), and
   [ZhttpServerImpl.hh:2408](zhttp/src/ZhttpServerImpl.hh#L2408).

7. **Concrete allocations bypass identified Z heaps.** `DiscoveryRequest`
   derives only from `ZmObject` but is allocated with ordinary `new`. HPACK
   and QPACK allocate their `ZmLHash` objects with ordinary `new`; those aliases
   do not supply concrete `ZmNHeap` specializations. Their table-storage
   allocation also follows `ZmLHash`'s allocator rather than a
   module-identified heap. Use concrete allocation types and appropriate heap
   identities. See
   [ZhttpDiscovery.hh:126](zhttp/src/ZhttpDiscovery.hh#L126),
   [ZhttpDiscovery.cc:537](zhttp/src/ZhttpDiscovery.cc#L537),
   [ZhttpHPack.cc:149](zhttp/src/ZhttpHPack.cc#L149), and
   [ZhttpQPack.cc:260](zhttp/src/ZhttpQPack.cc#L260).

8. **Short-lived continuations repeatedly retain their owning links.**
   Numerous cross-shard posts capture `ZmRef(link)`, adding owner reference
   churn where the prescribed model is raw back-pointers plus orderly
   draining. This is an explicit guideline violation, although removing
   references requires checking each teardown contract first. See
   [ZhttpNative.hh:708](zhttp/src/ZhttpNative.hh#L708) and
   [ZhttpClientImpl.hh:3873](zhttp/src/ZhttpClientImpl.hh#L3873).

9. **Diagnostic counters are needlessly atomic.** `serverFaults`,
   `rejectedRequests`, `parseFailures`, `bodyFailures`,
   `responseBuildFailures`, and `transportFailures` are telemetry, yet use
   atomic updates. This falls directly under the unnecessary-atomic-telemetry
   flag. Admission and retained-byte counters have operational uses and should
   be assessed separately. See
   [ZhttpServerImpl.hh:68](zhttp/src/ZhttpServerImpl.hh#L68).

10. **HPACK suppression matching is quadratic.** `commitBlock()` scans
    `m_suppressed` for every selected seed. This is the prohibited repeated
    integer matching pattern. Record suppression by seed index, or use an
    appropriate indexed representation. See
    [ZhttpHPack.cc:677](zhttp/src/ZhttpHPack.cc#L677).

11. **Fixed token sets use repeated string comparisons.** `Scheme::parse()`
    compares against `"http"` and `"https"` sequentially; `migrationMode()`
    similarly chains several alternatives. Alt-Svc parameter dispatch
    separately compares `"ma"` and `"persist"`. The guidelines require
    `ZuMatcher` for matching among multiple fixed strings. See
    [ZhttpURL.cc:284](zhttp/src/ZhttpURL.cc#L284),
    [ZhttpTransport.cc:17](zhttp/src/ZhttpTransport.cc#L17), and
    [ZhttpAltSvc.cc:224](zhttp/src/ZhttpAltSvc.cc#L224).

12. **Exposed struct data repeatedly follows behavior.** Examples include
    `StreamTask`, `BodyEmit`, `StreamBody`, server `Parser`,
    `ClientDiscoveryPost`, `Stream`, `EventRx`, `TxWindowEntry`,
    `RetainedBudget`, `RetainedEntry`, `NameView`, and compression `Prefix`.
    Their exposed members belong first, after required aliases. See
    [ZhttpServerImpl.hh:98](zhttp/src/ZhttpServerImpl.hh#L98),
    [ZhttpH2Hub.hh:80](zhttp/src/ZhttpH2Hub.hh#L80),
    [ZhttpWire.hh:306](zhttp/src/ZhttpWire.hh#L306), and
    [ZhttpCompression.hh:237](zhttp/src/ZhttpCompression.hh#L237).

13. **Struct/class ownership shape is inconsistent.** `PrefInt`,
    `ChunkedStream`, and H3 `DataStream` are structs containing private `m_`
    state. `QPackTxOrderEntry` combines public inherited state with private
    storage. `RouteState` and `StreamBytes` expose `m_`-prefixed members;
    `H3::Params` instead uses private members without `m_`. These violate the
    prescribed composite shape and member-prefix rules. See
    [ZhttpCompression.hh:309](zhttp/src/ZhttpCompression.hh#L309),
    [ZhttpQPack.hh:301](zhttp/src/ZhttpQPack.hh#L301),
    [ZhttpServerImpl.hh:1678](zhttp/src/ZhttpServerImpl.hh#L1678), and
    [ZhttpQPack.hh:185](zhttp/src/ZhttpQPack.hh#L185).

14. **Template-independent declarations remain inside templates.** Compression
    `Prefix`, H1 `StreamBinding::State`/`Term`, and H3 builder
    `Build::ValueSpan` do not depend on enclosing template parameters. Move
    them to non-templated scope. See
    [ZhttpCompression.hh:237](zhttp/src/ZhttpCompression.hh#L237),
    [ZhttpNative.hh:499](zhttp/src/ZhttpNative.hh#L499), and
    [ZhttpH3.hh:1417](zhttp/src/ZhttpH3.hh#L1417).

15. **Private state is accessed by another containing type.** The nested
    client `Producer` reads its owner's private `m_` state directly. Server
    `Parser::connectedInfo()` reaches through to `ServerCore::m_app`, despite
    an existing context accessor. C++ access permission does not exempt these
    accesses under the repository rule. See
    [ZhttpClientImpl.hh:398](zhttp/src/ZhttpClientImpl.hh#L398) and
    [ZhttpServerImpl.hh:1832](zhttp/src/ZhttpServerImpl.hh#L1832).

16. **Explicit enum and constant rules are violated.** `HPackSeedState` uses
    prohibited `enum class`; `EventType::T` and `QueueSlot::Kind` use
    prohibited named enums. Several `ZtEnum` declarations use unsigned
    underlying types despite the signed-enum rule. Small integral constants
    also use `constexpr` rather than enums. See
    [ZhttpHPack.hh:111](zhttp/src/ZhttpHPack.hh#L111),
    [ZhttpH2Hub.hh:195](zhttp/src/ZhttpH2Hub.hh#L195),
    [ZhttpH3.hh:2115](zhttp/src/ZhttpH3.hh#L2115), and
    [ZhttpCore.hh:83](zhttp/src/ZhttpCore.hh#L83).

17. **Four lambdas use prohibited blanket captures.** QPACK literal decoding
    uses `[&]` at four sites. Replace these with the specific referenced
    values. See [ZhttpQPack.hh:664](zhttp/src/ZhttpQPack.hh#L664).

18. **SFINAE remains encoded in return types.** Examples include native
    `produced`, `failed`, `streamID`, `printFailed`, `printReserve`,
    `printCommit`, and `mutableField`, plus `requestConnected` and parser
    `bodyMax_` adapters. Move participation tests into trailing template
    parameters, consolidating overloads where needed. See
    [ZhttpNative.hh:1017](zhttp/src/ZhttpNative.hh#L1017) and
    [ZhttpClient.hh:245](zhttp/src/ZhttpClient.hh#L245).

19. **Pure base forwarders and fluent forwarding persist.** `H2Config`
    redeclares five getter forwarders and forwards every TLS setter to preserve
    its outer return type. The guidelines explicitly prescribe base access
    through `using` and the same-named lambda configuration pattern. Native
    H2/TLS link `txStream()` forwarders likewise add no behavior. See
    [ZhttpConfig.hh:89](zhttp/src/ZhttpConfig.hh#L89) and
    [ZhttpClientImpl.hh:1015](zhttp/src/ZhttpClientImpl.hh#L1015).

20. **Headers depend on transitive includes.** Examples: `ZhttpConfig.hh` uses
    `ZuDerive` without its direct header; `ZhttpH3.hh` uses `ZmScratch`;
    `ZhttpClientImpl.hh` uses `ZuICmp`, `ZmEngine`, and `ZmObject`;
    `ZhttpServer.hh` uses `ZmEngine`. Include grouping is also inconsistent,
    notably Zt before Zm in `ZhttpDiscovery.hh`. `ZhttpSSE.hh` omits the
    component-header conditional from the required skeleton. See
    [ZhttpConfig.hh:16](zhttp/src/ZhttpConfig.hh#L16),
    [ZhttpH3.hh:1901](zhttp/src/ZhttpH3.hh#L1901), and
    [ZhttpSSE.hh:12](zhttp/src/ZhttpSSE.hh#L12).

21. **Repeated blocks violate DRY.** The three printable H3 field overloads
    repeat scratch construction, formatting, and failure handling.
    `FieldSectionBuffer::putPrint()` duplicates prefix encoding and gap
    recording across its branches. Native string/print writer tables and H1's
    header writer table substantially duplicate the same adapter. See
    [ZhttpH3.hh:1479](zhttp/src/ZhttpH3.hh#L1479),
    [ZhttpCompression.hh:169](zhttp/src/ZhttpCompression.hh#L169), and
    [ZhttpNative.hh:1155](zhttp/src/ZhttpNative.hh#L1155).

22. **Amber: storage/layout choices need attention.**

    - Long member types defeat alignment in `DiscoveryRequest`,
      `ClientSession`, and `HPackEncoder`; introduce local aliases.
    - Interleaved small and aligned members introduce avoidable padding, for
      example `QPackDecodedInsn` and `TxWindowEntry`.
    - Native H1 links place Tx-owned `m_streamEnd` beside Rx-owned state without
      cache-line separation.
    - `StreamTaskQ` allocates an extra node around the separately allocated,
      reference-counted stream driver.

    See [ZhttpDiscovery.hh:154](zhttp/src/ZhttpDiscovery.hh#L154),
    [ZhttpQPack.hh:63](zhttp/src/ZhttpQPack.hh#L63),
    [ZhttpNative.hh:779](zhttp/src/ZhttpNative.hh#L779), and
    [ZhttpServerImpl.hh:136](zhttp/src/ZhttpServerImpl.hh#L136).

23. **Amber: capacities and hot-path work lack the required justification.**
    Literal `256` scratch sizes repeat despite a named encoder scratch
    constant. `PrefixReserve = 16`, the prefix buffer's `11`, H2 queue
    defaults, and several compaction thresholds lack the required
    bound/trade-off maintenance comments. H3 printable field names use a plain
    heap-backed `ZtBArray` temporary; incremental string assembly pushes bytes
    individually despite having buffer growth facilities. See
    [ZhttpH3.hh:1484](zhttp/src/ZhttpH3.hh#L1484),
    [ZhttpCompression.hh:161](zhttp/src/ZhttpCompression.hh#L161),
    [ZhttpNative.hh:1300](zhttp/src/ZhttpNative.hh#L1300), and
    [ZhttpCompression.hh:443](zhttp/src/ZhttpCompression.hh#L443).

24. **Remaining explicit style violations.** These include invariant
    `body.length()` in a loop condition; repeated invariant indexing in
    URL/Alt-Svc parsing; char-equivalent pointer casts; division with
    surrounding spaces; redundant `this->m_` accesses; prefixed file-static
    codec functions; and braced switch cases with `break` inside the braces.
    See [ZhttpSSE.hh:193](zhttp/src/ZhttpSSE.hh#L193),
    [ZhttpAltSvc.cc:89](zhttp/src/ZhttpAltSvc.cc#L89),
    [ZhttpFields.cc:68](zhttp/src/ZhttpFields.cc#L68),
    [ZhttpH2.hh:937](zhttp/src/ZhttpH2.hh#L937), and
    [ZhttpQPack.cc:28](zhttp/src/ZhttpQPack.cc#L28).

## Remaining guideline families

| Guideline family | Static-audit disposition |
| --- | --- |
| Naming, filenames, width | No identifiers over the authoritative 28-byte limit or lines over 98 expanded columns found. |
| Header graph | Module-local header graph is acyclic; guards are present. Direct-dependency issues are listed above. |
| Concepts, `requires`, anonymous namespaces, STL, constant macros | No additional violations found. |
| Custom allocation operators, fixed objects through `ZmVHeap`, RC objects stored by value, prohibited `ZmRef<T>` container values | No additional instances established. |
| Fixed lookup tables | Huffman tables trigger amber review, but have protocol/performance rationale; replacing them with hashes is not justified by this audit. |
| Temporary copies | Callback-scoped decode scratch and retained output storage fit documented exceptions; no blanket copy-removal recommendation. |
| Logging | No deferred `ZiLOG` lambda capturing pointers/references found. |
| Scheduler blocking assumptions, polling, saga validity, count-before-traversal, redundant iterator-null checks, hand-rolled hashes | No additional demonstrated findings. |
| Timers and teardown | Absolute deadlines and cancellation/drain mechanisms are present; ownership exceptions are identified above. |
| Dead/compatibility code | No sufficiently proven additional finding. Commented API examples were not treated as dead code. |
| Metadata, persistence, dynamic modules, external dependency additions | No applicable production usage requiring a further finding in this scope. |
| Platform and runtime acceptance | Not established by static inspection; no sanitizer, leak, Windows, or performance acceptance claim. |

## Execution note

Before the user clarified that this task must not rebuild or test, two build
commands were run and reported that nothing needed building. The unit suite
was also attempted; its network cases failed at sandbox port allocation. The
unrestricted rerun was aborted. No further builds or tests were run, and those
results are not used as evidence for the findings above.

## Repair dispositions — 2026-10-18

These entries append to the original audit; they do not replace its evidence.
Runtime acceptance remains pending until the current configuration reaches a
major build and test milestone.

- **Finding 2 — repaired by static review.** `ZhttpH3.hh` now has separate
  `m_rxRng` and `m_txRng` instances behind `ZiMultiplex_FILTER`, each grouped
  with its own drop threshold and cache-line aligned. `ZiConnection` calls the
  receive filter from `executedRecv()` and the send filter from `send()`;
  each generator is advanced only by its corresponding shard. The H3 client
  and server install filters after base initialization and clear them in
  `final()` before base finalization releases the multiplex. Their stop paths
  finish before finalization. This adds one `ZmRandom` and up to two aligned
  groups per fault-enabled hub, with no allocations, copies, references,
  lookups, or scheduler posts.
- **Finding 3 — repaired by static review.** `HPackEncoder::detachNeverIndex_()`
  ends the `HPackNameSet` iterator scope before replacing `m_neverIndex`.
  Its reference-count guard and copy-on-write behavior are unchanged; the
  QPACK `Params::detach_()` traversal already uses this order. No additional
  allocation, copy, reference, lookup, or scheduler work is introduced.
- **Finding 7, fixed-size object subissues — repaired by static review.**
  `DiscoveryRequest` now inherits `ZmNHeap<DiscoveryRequest>` as its first
  base and declares its ADL heap ID. Both `discoverH3()` and `resolveH3()`
  allocate this concrete type; its `ZmRef<DiscoveryRequest>` owns deletion.
  HPACK exact/name and QPACK exact/name/section hashes now use concrete
  `ZmNHeap` derived types, with `ZuPtr` pointing to those concrete types so
  their deletion paths match allocation. Inheriting each hash constructor
  preserves its configured capacity and hash behavior. This adds five heap
  identities and no additional object allocation, reference, lookup, or post.
  `ZmLHash_Ops` still uses `Zm::alignedAlloc` for variable-size table storage;
  that foundational allocator subissue remains planned separately.
- **Finding 5, admission subissue — repaired by static review; strict expiry
  reclamation retained as a documented exception.** The chosen contract is
  that expiry makes values unobservable at lookup time, while bounded resident
  storage may remain until a cache operation. `AltSvcCache::update_()` now
  validates and builds a replacement before changing the cache, then evicts
  the first occupied hash slot when a new nonempty origin needs a full cache.
  This is a deterministic policy for a given hash-table state, with one
  iterator traversal and no expiry scan or background post. Replacement
  removes the old origin before adding the new one, so even transient entry
  count stays within `maxOrigins`. `clear`, explicit `del`, replacement,
  eviction, and a fully expired requested origin each remove state directly.
  The existing `get()` only compacts its requested origin. `ZhttpAltSvcTest`
  now covers bounded live eviction and admission after cold expiry. Strict
  physical reclamation during complete inactivity requires an event and
  conflicts with the no-timer-GC rule; it is not claimed as resolved.
- **Finding 17 — repaired by static review.** All four QPACK literal-decoder
  callbacks now list their captures. `decodeString()` invokes each callback
  synchronously before its stack scratch is destroyed, including the nested
  name/value callback; borrowed spans do not escape. The capture change adds
  no allocation or post.
- **Finding 10 — repaired by static review.** `HPackEncoder` now reserves a
  byte of suppression storage per selected seed during `bind()`, clears that
  indexed storage at `beginBlock()`, and records each suppressed seed once.
  `commitBlock()` checks one byte per selected seed instead of scanning the
  suppression list; its no-suppression table move and rollback behavior remain
  unchanged. Storage grows with the configured selected seed count and reuses
  capacity. The change adds one indexed write per newly suppressed seed and
  one zeroing pass per reserved block; it removes the nested membership scan
  and does not add per-field allocation or scheduler work.

### First implementation milestone

The existing clang GNU C++2b x86-64-v2 debug configuration was reused.
`make -C zhttp/src -j4` completed successfully and relinked `libZhttp`.
`make -C zhttp/test -j4 ZhttpAltSvcTest ZhttpHPackTest ZhttpQPackTest
ZhttpQPackDynamicTest` completed successfully. All four resulting test
binaries exited zero: Alt-Svc (3 groups), HPACK (13 groups), QPACK (3 groups),
and dynamic QPACK (34 groups). This milestone covers the local cache and codec
changes; H1/H2 streaming, dependent modules, final Valgrind, and remaining
audit findings are still pending.

- **Finding 4 — implementation in progress after this milestone.** Initial
  SETTINGS now records outstanding state and arms its timer only after
  `tx.flush()` succeeds. SETTINGS ACK returns a status, so rejection skips
  peer-settings publication and `h2SettingsReceived()`. PING ACK, RESET, and
  WINDOW_UPDATE route synchronous rejection through `h2Error(InternalError)`;
  GOAWAY rejection disconnects when graceful shutdown has not already entered
  stop. Receive-window credit is advanced only after WINDOW_UPDATE submits.
  `ZhttpH2HubTest` now checks rejected SETTINGS, PING ACK, and graceful
  GOAWAY. The second milestone covers these paths; review of every RESET
  caller and asynchronous transport-error path is still pending.
- **Finding 11 — implementation in progress after this milestone.** Scheme,
  migration-mode, and Alt-Svc parameter tokens now use `ZuMatcher` in their
  owning `.cc` files. Each input is ASCII-folded into bounded stack storage
  sized by the longest configured token before matching. The `disable`
  alias, default for unknown migration modes, and repeated Alt-Svc parameter
  assignment remain intact. URL already tests uppercase schemes; Alt-Svc and
  migration tests now exercise mixed-case matching. No heap allocation,
  reference operation, lookup structure, or scheduler post was added. The
  second milestone covers the changed matchers.

### Second implementation milestone

The same configured `zhttp/src` target rebuilt and linked successfully.
`ZhttpH2HubTest`, `ZhttpURLTest`, `ZhttpAltSvcTest`, and
`ZhttpTransportContractTest` rebuilt successfully and exited zero. The H2
hub test's local-network cases could not allocate ports in the sandbox; its
unrestricted rerun passed all 11 groups, including rejected SETTINGS, PING
ACK, and graceful GOAWAY. URL, Alt-Svc, and transport-contract tests passed
3, 3, and 16 groups respectively. The H2 control and fixed-token changes
now have this focused runtime evidence. RESET/WINDOW_UPDATE failure coverage,
dependent rebuilding, final Valgrind, and other findings remain pending.

### H1 ownership implementation — 2026-10-18

- **Findings 1 and 22, shard layout — implementation in progress.** Native
  H1 client/server links and TLS H1 application links now retain negotiation,
  dispatch, and peer-terminal state on Rx. `streamTx()` asserts Rx affinity
  and invokes its borrowed printer synchronously. `streamTx_()` asserts Tx
  affinity and reads a Tx-owned admission flag, published from Rx. End/reset
  dispatch to Rx for negotiation-state changes, then Tx to close admission
  and finish or cancel output. No borrowed printer or result reference is
  posted. `StreamBinding` now reaches Terminal on disconnect even if the
  upgrade never completed. Its state/termination enums are outside the
  template (finding 14 subissue).
- TLS H1 application links publish a separate raw Tx native pointer before
  admission. Clear/release posts hold the physical native link until that Tx
  pointer is retired. Direct Tx message construction and synchronous Tx
  activity checks use the Tx pointer. Rx and Tx groups are cache-line aligned;
  the physical TLS link's Tx end flag was moved into its own aligned group.
- Cost: one admission byte per H1 link, plus one native pointer per TLS
  application link and cache-line padding at the shard boundaries. Each enable
  publishes one boolean with one Tx post. End adds an Rx dispatch (inline on
  Rx) and one Tx post; reset adds the Rx dispatch and Tx/Rx posts before native
  disconnect. TLS association/retirement adds one Tx post. These continuations
  currently retain links; TLS handoffs also retain their native link through
  the handoff. Their removal is deferred to finding 8's teardown proof. There
  are no new per-write allocations, buffer copies, container links, or lookups.
- `ZhttpH2HubTest::runH1Shards` now exercises both TLS H1 application shapes
  with a deferred scheduler: synchronous borrowed writers, ordered publication,
  end/reset from Tx, and retirement of the Tx native handle. Build and test
  evidence for this batch is still pending.

### H1 ownership milestone

The current `zhttp/src` incremental build passed. The rebuilt `ZhttpH1HubTest`
(15 groups), `ZhttpH2HubTest` (12 groups), `ZhttpMessageH1TCPTest` (3 groups),
and `ZhttpMessageH1TLSTest` (3 groups) all exited zero with local network
access. The H2 hub suite includes the new deferred-shard H1 checks for both
TLS application shapes. The first test compile exposed a reversed pair of
template arguments in the new server harness; that harness was corrected
before the successful build. The ownership changes have focused acceptance;
bounded teardown and the per-capture lifetime audit remain open.

### H2 shadow-list cleanup correction

The three scheduling-list checks at the end of `clearFramesTx_()` now use
`ZiAssert` with `clean()` as the release fallback. Debug builds diagnose the
invariant failure; release builds log it and reset the shadow bookkeeping.
`ZmList::clean_()` now skips node traversal for shadow lists, whose dereference
and deletion operations were already no-ops. This makes the reset safe even
when borrowed node storage has been released. Owning-list cleanup is unchanged.
The owning window hash must still be empty before completion. These edits add
no allocations, copies, posts, or object storage. Build and runtime verification
are deferred to the next implementation milestone under the current build
configuration.

### H2 bounded window/retirement batch — 2026-10-18

- **Finding 6 — implementation in progress.** Connection-credit requeueing
  now inspects at most `FrameDrainBatch` blocked streams per turn. A separate
  intrusive all-window list supports bounded frame retirement and a two-pass
  initial-window update. Both validation and application count inspected
  windows, including retiring entries. Frame cleanup counts entries, frames,
  end markers, and terminal commands against the same turn budget.
- Window updates pause output while the original stream prefix is checked
  and updated. The traversal captures its tail; newly opened streams use the
  target initial window and append beyond that tail. Deletion adjusts the raw
  cursor/tail before unlinking the window. No hash iterator crosses a post.
  Later WINDOW_UPDATE and SETTINGS operations retain arrival order in an
  identified `PeerChanges` list, bounded by the configured queued-frame budget.
  Exceeding that budget closes the connection through its owning error path.
  SETTINGS ACK submission and Rx readiness now follow complete Tx application.
- Stop disables Tx admission, cancels window traversal, and retires deferred
  peer changes and frames in bounded turns. Completion follows the final Tx
  and Rx barriers, including previously posted work and ACK notifications.
  Per-stream removal waits for its final frame-retirement turn before the
  existing completion barriers.
- Cost: each Tx window gains a second intrusive link pair. Each connection
  gains all-window and deferred-change list bookkeeping, two raw cursors,
  window delta/phase, and continuation flags/counts. Only peer changes arriving
  behind an active update allocate queued nodes, using an identified Z heap;
  their payload is fixed metadata. Each bounded remainder adds one raw-owner
  scheduler post, without owner reference operations. Exact object-size and
  padding measurements remain deferred. There are no added I/O-buffer copies.
- **Finding 4 — additional repair by static review.** The four native-link
  `rst_()` implementations were removed. Their callers use checked
  `Wire::resetStream()` and stop normal close/reset processing on rejection.
  Wire's own reset callers likewise stop continuation after failed output.
  The standalone H2 client and both server native shapes now install a
  capture-free native TLS error handler that logs and returns failure, causing
  TLS to disconnect after asynchronous send rejection. The pooled TLS client
  reports native failure to its owning pool and now also returns failure to
  TLS, including when an application diagnostic callback returns handled.
- Regression source now covers bounded blocked-stream requeue, large
  single-stream cleanup, ordered window changes and delayed ACK, stream
  insertion/removal during traversal, overflow at the captured tail, stop
  during an update, and rejected RESET/SETTINGS ACK. `ZmTest` also covers
  shadow clean/destruction after external node retirement. These changes have
  not been rebuilt or run; validation remains deferred to the next milestone.
- Finding 6 is still open: header-chain emission/queueing and ordered H1
  response release/cancellation need bounded turns and completion integration.
  The current unbounded header-chain drain still preserves uninterrupted
  HEADERS/CONTINUATION emission; its replacement must serialize control output
  before allowing scheduler yields. Finding 8's full per-capture proof and the
  remaining findings and lifecycle migrations are also unfinished.

### H2 library milestone

`make -C zm/src -j8` exited zero with no work required.
`make -C zhttp/src -j8` exited zero without warnings, using the existing clang
GNU C++2b x86-64-v2 debug configuration and build artifacts. The first compile
identified an incorrect direct exception-header name; the next identified
ambiguous inherited link/state names. These were corrected before the passing
build. The final log is `/tmp/z-repair-h2-milestone.log`.

Dependent test builds and all test runs remain deferred. Source review also
corrected `runH1Shards`, `runWindowChanges`, and `runBoundedFrameCleanup` to use
runtime TAP scopes/checks for repeated and conditional assertions. The prior
H1 milestone's exit-zero result did not validate that repeated subtest's TAP
plan. Fresh runtime/TAP acceptance remains required. Diff whitespace and the
98-column limit pass for this batch.

### Bounded output and H1 disconnect batch — 2026-10-18

- **Finding 6, H2 header/control emission — repaired by static review.**
  `HeaderFrames` now uses the existing intrusive `ZiTxQueue`; encoding transfers
  the pooled buffer references into that queue, and stream admission splices
  the entire chain in constant time. `QueueAdmission::push(n)` reserves all
  frames with one successful compare/exchange, without partial reservations or
  unsigned addition overflow. No separate header-reference array is allocated.
  Header/data/control drain turns count every inspected frame against
  `FrameDrainBatch`, including frames that are reblocked rather than sent.
- `ControlTx` serializes all H2 control output on Tx. Controls wait behind an
  open HEADERS/CONTINUATION chain; a stream removed mid-chain rejects new work
  but finishes END_HEADERS before its remaining frames are retired. Control
  admission has its own configured frame budget (minimum one buffer for
  startup), so blocked DATA cannot consume its credit. SETTINGS readiness is
  published back to Rx after successful native ACK submission. Initial
  SETTINGS outstanding state/timing follows synchronous queue admission;
  subsequent native rejection is an asynchronous connection failure. Inline
  failure cannot publish initial SETTINGS success. Protocol/flow-control
  failure pauses DATA while allowing queued GOAWAY output; actual transport
  failure closes Tx output and disconnects its owner on Rx.
- Cost: one additional shared admission counter/limit and one intrusive
  control queue per physical H2 link. Controls reuse their existing pooled
  buffers and links, without separate queue-node allocation. A non-coalesced
  control adds one Tx drain post; up to one frame budget is coalesced per turn.
  Buffer references are transferred rather than copied. Header admission
  removes per-frame reference operations and array storage/transfer traversal.
- **Finding 6, H1 response release/cancellation — repaired by static review,
  runtime acceptance pending.** The shared response worker budgets response
  starts, ordered releases, producer cancellation, and completion/discard
  callbacks with `StreamStopBatch`. Synchronous response completion cannot
  recursively start an unbounded response chain. One continuation represents
  the remaining work; no iterator or response borrowed from a queue survives
  an unlink/callback. The preexisting sorted insertion traversal remains
  linear; its bound and any needed repair remain part of final liveness review.
- Native TCP H1 and TLS H1 now wait for `appDrained()` before their final Tx/Rx
  barriers and native admission release. The default hook is immediately
  complete. The owning server implementation registers a Tx callback, drains
  every response queue/producer, and posts completion back to Rx. Final Tx/Rx
  barriers include work reposted by completion callbacks. TCP carries its
  existing native owner reference through this chain; TLS retains its physical
  link and its owned H1 application link until the final Rx release. New raw
  response-worker continuations therefore have an independent lifetime owner.
  Other continuation captures still need finding 8's individual audit.
- Cost: H1 ordered state adds a 16-byte `ServerDrainFn` and three work flags,
  with 24 bytes of total growth in the current layout. `ZmFn::mvFn` transfers
  the existing owner reference into a stateless bound callback, without a
  captured-closure allocation or reference churn on each work turn. Multiplexed
  response links gain none of this ordered-state storage. Remaining response
  work adds one raw-pointer Tx post per batch; H1 disconnect gains its callback
  registration/completion handoff and retains the final dependency barriers.
- **Finding 9 — repaired by static review.** The six diagnostic counters are
  primitive integers in cache-line-separated `RxStats` and `TxStats` groups.
  Listen failure, admission rejection, and parser/body failure are Rx-owned;
  response construction and response transport failure are Tx-owned. TCP and
  QUIC listen failure paths explicitly dispatch to Rx. Existing server hubs
  share the configured Rx/Tx threads. Public telemetry getters read these
  counters directly under the guideline's unclean-observer contract. Admission,
  retained memory, queued response counts, and the operational failure flag
  keep their existing synchronization. No diagnostic update adds a post, lock,
  allocation, or snapshot.
- Debug information from the current library reports: `Stats` 192 bytes,
  `RxStats` 32, `TxStats` 16, `ZiTxQueue` 24, H1 `RouteState` 56, and
  `ServerDrainFn` 16. The former stats layout was 72 bytes on this configuration;
  shard separation adds 120 bytes per server. Layout inspection used
  `libtool exec gdb` without running a program; output is
  `/tmp/z-repair-layout.log`. No throughput/latency measurement is claimed.
- Test source adds deferred H2 header-turn/control/removal cases. The existing
  stream integration test adds TCP and TLS H1 queues exceeding one cleanup
  budget, covering both ordinary pending responses and ordered responses
  waiting for a missing sequence. A Tx probe checks that cancellation yields,
  and stop must observe all response callbacks and releases. These tests have
  not been rebuilt or run. Rejected-control and existing header-transfer mocks
  were updated for the new serialization/container contracts.

### Combined library milestone

`make -C zhttp/src -j8` passed without warnings using the existing build
configuration and artifacts. The first pass exposed that a scheduler run must
receive a callable wrapper rather than a `ZmFn` directly; that handoff was
corrected. The final pass also includes the allocation-free bound drain
callback. Log: `/tmp/z-repair-bounded-io-library.log`. Dependent builds/tests
and Valgrind remain deferred. Diff whitespace and expanded 98-column checks
pass. This milestone does not close the remaining audit findings, the complete
lifetime audit, or any `repair2.md` lifecycle migration.


### Codec storage and declaration batch — 2026-10-18

- **Finding 7, separately justified foundational scope extension.** Dynamic
  `ZmLHash` backing arrays now use `ZmVHeap_` through a new `ZmLHashHeapID`
  policy. This replaces the previously verified direct aligned allocation;
  it does not duplicate hash logic in HTTP. The default identity is `ZmLHash`;
  HPACK exact/name and QPACK exact/name/section tables supply distinct storage
  identities, separate from their concrete fixed-object heaps. Static table
  arrays keep their inline allocation. Hash node layout, ownership, lookup,
  growth thresholds, and element construction/destruction are unchanged.
- Allocator cost: dynamic tables acquire the allocator's alignment-sized
  prefix and pooled size-class rounding below its normal jumbo boundary.
  Alignment is the greater of the node alignment and a cache line. The minimum
  class derives from one node plus that prefix; it is not an arbitrary 1 KiB
  minimum. A payload near a power-of-two boundary can require nearly twice its
  former requested bytes after rounding. Recycling may retain free blocks in
  the framework heap caches. This is an instrumentation/recycling repair,
  without a claim of reduced memory use, improved latency, or measured speed.
  No per-entry allocation or nullable allocation-failure check is added.
  `ZmLHTest::tableHeap` adds source coverage for alignment, growth, deletion,
  reference destruction, and reuse across two cycles; execution is pending.
- **Finding 8, server H1 capture subset repaired by static review.** Native
  server admission publication, end/reset Rx-to-Tx work, reset's Tx-to-Rx
  disconnect, and Tx stream-close publication use raw owners. The native
  connection owns a live link; disconnect ingress transfers an owner reference
  to its Rx notification, then `ServerDrainFn` retains it through app response
  cleanup and final Tx/Rx barriers. Reset and close repost to Rx before those
  final barriers release ownership. Initial public cross-shard commands retain
  their handoff references because callers can release their ChannelRef after
  submission. This proof applies to server H1, not client or H2/H3 captures.
- TLS H1 admission/publication, native-handle retirement, and end/reset
  continuations use raw application/native pointers. `SrvLink::m_h1` owns the
  application until app cleanup and final Tx/Rx barriers finish; the bound
  drain callback independently retains the physical native link. Both pointers
  therefore survive the posts. The H1 `responseDisconnected_` handoff uses
  the same proof; multiplexed response links keep their reference. Response
  send/routeSend retains an owner because application messages may arrive after
  native registry retirement. The H1 response destructor now fatally asserts
  that no worker, drain callback, producer, response queue, or retained bytes
  remain. Each removed capture saves its per-post owner reference operation;
  these edits add no allocation, post, or ownership cycle.
- **Findings 12–15, inventoried declarations repaired by static review.**
  Shared data now precedes behavior in StreamTask, BodyEmit, StreamBody, server
  Parser, ClientDiscoveryPost, H2 Stream/EventRx/StreamBytes, RetainedBudget,
  RetainedEntry, and compression NameView/Prefix. Encapsulated PrefInt,
  ChunkedStream, and H3 DataStream are classes. RouteState and StreamBytes
  expose unprefixed data; QPackTxOrderEntry's storage is shared public data;
  H3 Params uses private `m_` fields. Prefix and ValueSpan are shared outside
  templates; H1 state enums were moved in the ownership batch. Producer inputs
  are passed explicitly, and server Parser uses ServerCore::context(). These
  changes add no copies or broad accessor interface. Local member aliases
  preserve shard grouping. QPackDecodedInsn moves its opcode beside flags;
  no positional aggregate initializer was found for that type.
- **Finding 16, static declaration repair.** Named/scoped enums use signed
  namespace/struct storage with anonymous values. HPACK/QPACK states and H3
  queue kinds fit int8; H3 connection-error constants fit int16. H2 FrameType
  uses int16 to cover unknown 8-bit types, Setting uses int32 for all 16-bit
  IDs, and Error uses int64 for every unknown unsigned 32-bit wire code.
  Serialization arguments and raw wire storage keep their original widths.
  Unsigned 32-bit slot sentinels use signed int64 enum declarations to preserve
  UINT_MAX. This can enlarge objects holding H2 error/type aliases; layout
  acceptance remains pending. Eligible scalar constants use enums; template
  limits that may exceed int and full-width sentinels keep constexpr storage.
- **Finding 18, static repair.** Native produced/failed/streamID, printable
  reserve/commit/failure, mutable fields, client requestConnected, parser
  bodyMax/retainedBodyMax/bodyInfo, and server/client bodyMax adapters move
  participation to trailing template parameters. Integer-priority fallback
  overloads remain distinct. Deduced decltype(auto) preserves the former exact
  return category where it was not a fixed bool/void/span result.
- **Findings 20/21/23, shared encoding subset repaired by static review.**
  Verified direct includes and SSE's component guard are corrected. Server.hh
  no longer names ZmEngine: its CtrlFn is explicitly a ZmFn alias, so adding
  ZmEngine.hh there would be an unused dependency rather than a repair.
  Compression::renderPrint shares synchronous scratch rendering across H2/H3
  using a named 256-byte starting capacity and each codec's existing identified
  heap fallback. The borrowed span is consumed before scratch destruction;
  template callbacks add no runtime dispatch, allocation, or copy.
- FieldSectionBuffer keeps formatter evaluation/reservation ordering while
  sharing prefix/gap finalization. Prefix's bound is one prefix byte plus
  ceil(64/7) continuation bytes; PrefixReserve adds the five-byte gap record,
  retaining the old 16-byte reservation. Shared ValueSpan preserves offset
  and length semantics. Native and H1 string writer tables now share an adapter
  with printable writers in FieldImpl. Compile-time selection preserves their
  different flush/failure/count and reserve/commit contracts; transport writers
  and FinalFields retain their distinct failure/ownership behavior.
  Callback-local printable compressed field names use identified stack scratch
  with heap fallback instead of an always heap-backed temporary. The same
  bytes still need rendering; this does not claim removal of that required copy.
- **Finding 24, partial static repair.** SSE's loop caches body length; file
  static HPACK/QPACK lookup/entry functions no longer carry module prefixes.
  Remaining style occurrences and capacity/compaction justifications remain
  open. Finding 19, the complete owner capture audit, optional StreamTask node
  embedding, runtime acceptance, and all repair2 migrations remain open.


### Codec library milestone and follow-on source review

`make -C zm/src -j8` and `make -C zhttp/src -j8` completed with the existing
clang GNU++2b x86-64-v2 debug flags and artifacts, without warnings. The first
HTTP pass exposed the removed QueueSlot::Kind declaration still being used by
callers; it now names signed storage for anonymous values, and the final pass
succeeded. Logs: `/tmp/z-repair-codec-zm.log` and
`/tmp/z-repair-codec-http.log`. Dependent builds/tests and Valgrind did not run.

Debug-info inspection with `libtool exec gdb`, without running the library,
reports QPackDecodedInsn 96 bytes, H2 Event 88, TxWindowEntry 64, Error::T 8,
FrameType::T 2, and Setting::T 4. Log: `/tmp/z-repair-codec-layout.log`.
This records current footprints, not a runtime or performance result.

Follow-on source changes after that build remain unbuilt: the incremental
compression string decoder appends each available contiguous chunk in bulk;
zero-byte turns skip the append and preserve partial-prefix, length, and
completion semantics. Char-equivalent formatter destinations use framework
span conversion; URL/Alt-Svc loops cache byte reads and true length invariants,
and decimal/power-of-two division follows local style. The storage test now
holds an external reference across hash removal, explicitly covering genuine
shared ownership and subsequent final release. These changes will join the
next large source batch; they do not trigger an individual rebuild or test.


Finding 19 follow-on source repair removes the four native H2/TLS link pairs
of pure txStream/txStream_ forwarders with explicit base using declarations.
The two client facade complete(bool) forwarders and the server raw-buffer
send forwarder likewise use their owning base overloads. Renamed adaptations
such as disconnectNative/quicReset remain distinct interface operations.
H2Config's TLS configuration surface and dependent fluent expressions remain
open. These declaration changes join the unbuilt follow-on source batch.


### H2 configuration and foundational lifecycle batch — 2026-10-18

- **Finding 19, repaired by static review; dependent acceptance pending.**
  H2Config hoists the TLS base overloads with using declarations and exposes
  mutable/const tls() plus a synchronous lambda overload returning H2Config&&.
  The five TLS setter forwarders are removed. Repository-wide constructor,
  named-variable, and generic profile configuration sites now use the base
  accessor/lambda pattern where H2-specific chaining follows. TLSConfig and
  QUICConfig expressions keep their own interface. zws assigns its TLS base
  through the accessor rather than a base cast. Captures are explicit and
  borrow roots synchronously; owning strings are not copied into callbacks.
  Documentation and transport contract source cover outer-type preservation
  and propagation of trust/identity settings to native parameters.
- **repair2, ZiResolver source implementation.** Main now directly uses a
  private CRTP ZmEngine and exposes its canonical state/running/stopping APIs.
  Public singleton start/stop callbacks and main-thread Sync methods return
  bool; initialization/runtime diagnostics use FailFn and query result unions
  remain unchanged. The separate lifecycle enum and start/stop waiter arrays
  are deleted. Typed lifecycle result callers were migrated deliberately;
  ZiEventLoop's independent result union remains part of that loop's interface.
- Resolver stop closes admission under its publication lock, drains queued work
  on its sole dedicated resolver shard, closes its activity gate, deletes its
  timer, cancels/unlinks c-ares queries, closes the channel, drains the event
  loop, and stops the child scheduler. Scheduler deletion happens on its own
  final control callback after workers join; its existing self-destruction path
  detaches that control handle. Every restart creates a fresh scheduler, so
  this path never inherits the scheduler's predecessor-thread join chain.
  Parent stop completion follows that deletion. final asserts Stopped and
  drained state and releases only configured/unstarted resources; it never
  initiates shutdown or waits. The destructor enforces the same precondition.
- Resolver configuration uses a void/Params union sentinel, keeping configuration
  across stop without a duplicate lifecycle flag or a new allocation. Channel
  reconstruction uses the retained options, including configured DNS servers.
  Numeric resolution preserves its synchronous lazy path. Ordinary queries
  use lazy startup while stopped; queries during stop are rejected, while
  explicit start during stop uses the engine's queued opposite operation.
  Publication and the stop gate order every accepted post before scheduler
  retirement; callback closures retain query objects, not their singleton owner.
  The resolver shard group is cache-line-separated from admission metadata.
- Cost: one parent engine's canonical callback queues/lock/state replace the
  resolver's old waiter arrays and private state. Query submission from its
  own shard now posts instead of invoking inline so admission and stop order
  are explicit; external submissions already posted. No owner reference is
  added. Restart recreates cold scheduler/channel resources. c-ares cancellation
  is an indivisible external operation on the dedicated resolver shard, not a
  container scan on a shard shared by unrelated components. No performance
  measurement or alternate configuration acceptance is claimed.
- **Separate demonstrated scheduler repair.** Worker creation could fail after
  earlier workers started and still report started(false) without rollback.
  The concrete scheduler now drains those workers before failed completion.
  Its worker count is atomic because control increments and worker exits
  update it on different threads; operations occur only at thread startup/exit,
  without per-job overhead. No new control thread or allocation is introduced.
- **repair2, zhttp source implementation.** PoolImpl and ClientCore now each
  have an engine for their complete operation; Hubs remains a child aggregate
  for the distinct transport/pool collection. Pool startup publishes Rx state,
  resets Tx sealing/idle/admission, and starts the request queue before success.
  Pool stop covers timer/discovery cancellation, native drainage, bounded Tx
  queue cancellation, and final Rx notifications. The private stop waiter
  arrays and duplicate stop-completion flags are removed; two owner-affine
  callbacks represent the actual ingress and Tx drain operations.
- Pool discovery callbacks now own Rx completion credits. Cancelling discovery
  can race an already-entered resolver completion, so a generation barrier
  alone did not cover a callback that had not posted yet. Each launch increments
  its credit before the external call; every final Rx post returns it even
  when stale. Stop combines those credits with the bounded cancellation scan
  and cannot proceed until both finish. This preserves raw owner captures
  with a drain proof and adds no reference churn, per-discovery allocation,
  atomic counter, scheduler post, or polling loop. It adds two unsigned
  per-pool bookkeeping counters and constant work per discovery completion.
- Client startup resets Tx-owned idle state in bounded batches, starts its
  owned resolver before pools, and rolls that resolver back if pool startup
  fails after the pool aggregate's rollback. Borrowed resolver startup requires
  its completed Running state and shutdown never stops that dependency.
  Parent stop completion follows pool and owned resolver completion. Public
  engine APIs are hoisted; final and destruction assert the parent boundary.
  These new parent engines add their own control queues/locks/state per client
  and pool; they do not duplicate their child's narrower lifecycle boundary.
- Concrete pool transport stop paths complete successfully after drainage,
  independently of earlier protocol failures. Unexpected false completion is
  treated as an integrity failure at that concrete boundary, not silently
  mapped to successful shutdown. Generic Hubs failure/restoration semantics
  still need final review before any broader stopped(false) claim.
- Hubs completion callbacks now use the canonical ZmEngine.CtrlFn heap identity,
  allowing direct engine start/stop exposure. Its two pure callback adapters
  are removed. This deliberately changes the former Zhttp.Hubs.Done callback
  heap identity; boolean result behavior is unchanged. It also removes the
  cross-heap callback conversion exposed by the library milestone compiler.
- Source tests migrate resolver booleans, exercise retained-option DNS restart,
  queued startup during blocked shutdown, callback-driven finalization, and
  client/pool restart with sealing/idle reset. The former final-while-callbacks-
  block/control-thread-join probe depended on the removed Finalizing state and
  conflicts with the new final contract; it is replaced explicitly. All test
  builds and runs remain deferred. zi/README.md and zhttp/README.md describe
  ownership, readiness, restart, and final preconditions. Remaining endpoint,
  service, and daemon migrations and final gates are not completed.


### Foundational library milestone

Incremental `make -C zm/src -j8`, `make -C zi/src -j8`, and
`make -C zhttp/src -j8` passed without warnings with the existing compiler,
flags, platform, and artifacts. Logs: `/tmp/z-repair-lifecycle-zm.log`,
`/tmp/z-repair-lifecycle-zi.log`, `/tmp/z-repair-lifecycle-http.log`.
No dependent target, runtime test, or Valgrind invocation ran. Two corrective
HTTP passes exposed the callback heap-type mismatch and an unconditional
raw-buffer send using declaration that is invalid for QUIC. The latter now
has a constrained native-only adapter: TCP/TLS's actual operation participates,
QUIC's nonexistent one does not. This is an explained exception to blanket
base hoisting, not a historical API shim. Other reviewed base functions remain
hoisted directly.

Debug-info inspection, without executing a program, reports ClientCore 1024
bytes and PoolImpl 1088, versus 960 and 1024 in the immediately preceding
library. Both complete-owner layouts grow by 64 bytes in this configuration;
each parent ZmEngine subobject itself is 104 bytes, with net owner growth
also reflecting removed drain arrays and existing alignment/padding. Resolver
Main is currently 960 bytes; no prior-size comparison was measured. Logs:
`/tmp/z-repair-lifecycle-before.log`, `/tmp/z-repair-lifecycle-layout.log`.
These are footprint observations, not performance measurements.

### Inline engine completion follow-on — source only

The required synchronous-hook/destruction audit found a further concrete
ZmEngine defect: start() called stateChanged() after successful spawn(), but
the default spawn runs its hook inline. An inline failed-start callback can
finalize and release its owner before that later access. Starting notification
now precedes spawn, and successful spawn has no later owner access. A repository
search found no production stateChanged override whose ordering depends on the
old path. Existing ZmEngineTest source adds inline failure/stop completion that
release their concrete, heap-identified owner and checks that no notification
follows release. No broad safety claim is made for finalization while an
opposite operation is still pending.

This follow-on header/test change was made after the successful library
milestone and remains unbuilt/unrun. It joins the next large source batch;
this single repair does not initiate another build. Final dependency refresh
must include all components instantiating the engine header. Remaining audit
closure, other module lifecycle migrations, relevant runtime suites, and the
final Valgrind gate are still required.


## 2026-10-18 — endpoint engine implementation batch (source only)

This batch continues repair2 after the foundational library milestone. No
compiler, configuration, build directory, build artifacts, or build flags
changed. No library/dependent rebuild, runtime suite, or Valgrind invocation
ran. The implementation milestone remains open for zum/ztc and the remaining
ownership/occurrence audits.

### JSON-RPC endpoint boundary

- `ZjrpcStdio.hh::IOEndpoint` owns one parent engine for both stdio facades.
  It retains StdioConfig, constructs a fresh in-place IOLink per run, and
  destroys it only in an owner continuation after its terminal callback and
  protocol drain return. Init/final without start constructs no workers.
- StdioConfig borrows the original input/output handles, including the
  standard streams. IOLink duplicates them with ZiFile::dup for each run;
  runtime stop closes only those duplicates. The caller keeps originals valid
  through final and closes its originals explicitly. Equal original handles
  remain invalid even though dup would produce distinct descriptor numbers.
  Costs: two duplicate-handle operations and closes per successful run,
  retained configuration and in-place runtime storage, no new per-message
  allocations or owner references. Runtime construction is moved from init
  to start. Exact endpoint sizes remain unmeasured.
- IO startup covers worker readiness and ioReady. An exception or rejected
  initial output rolls back workers and protocol work before started(false).
  Natural EOF ends runtime work but requires explicit stop before final or
  restart. An owner continuation retires the IOLink before engine completion;
  the endpoint's raw worker callbacks are protected by the existing
  RxDrained/RxExited/TxExited/TxDrained boundary.
- HTTPClient/HTTPServer and WSClient/WSServer have complete parent engines;
  HTTP/Zws controllers remain distinct children. Stored stop callbacks and
  parallel started/stopDone/finalOK flags are removed. Final asserts Stopped.
  HTTP stop includes dispatcher/call drains and Tx/Rx continuation barriers;
  WS stop also includes per-link RPC closure and native connection cleanup.
- WS client link publication uses a separate atomic handoff state. Off-shard
  construction posts an owning native Link handle to Tx. Stop retains the
  publication credit until that handoff arrives; Tx then assigns or drops
  the link before native shutdown. Endpoint flags are written on Tx; Rx
  callbacks post fixed status or existing link/message handles. This adds a
  connect handoff post and bounded cold-path state, with no owner reference
  count or new per-frame allocation. Connection/request readiness remains
  separate from completed transport startup.
- PendingCalls permits reopening only an empty, non-Closing table; capacity
  is reused. BatchCaller::closeCalls waits for both request ingress and
  outbound posts, covering notifications as well as registered calls.
  Stdio/WS restart resets the inbound ID history for the new run.

### MCP endpoint boundary

- IOClient/IOServer reuse the actual shared IOEndpoint operation. MCP keeps
  its protocol peer/context state in the facades; ioStarting reconstructs it
  on the owner shard, ioReady opens context/submits discovery, and ioClosed
  drains RPC work before ioDone retires the worker runtime. This removes their
  independent blocking orchestration, callback slots and completion flags.
- HTTPClient_::Client, HTTPServer, WSClient and WSServer have parent engines.
  Their start/stop/state/stopping APIs select the parent explicitly. Child
  transport completion cannot bypass MCP cleanup. HTTP client startup resets
  serial/drain/admission state and old session ID, then submits discovery;
  ready(era) still means handshake completion. Startup submission failure
  stops the child and drains RPC work before reporting false.
- HTTP client stop retires active/serial requests in the existing work budget,
  drains common RPC ingress/output, and posts Tx/Rx barriers. HTTP server
  stop drains dispatcher work, sessions, routes, SSE garbage and transport
  contexts. Removed sessions can still own closeSession_ continuations; a
  new Rx-owned closing-session count keeps those chains inside stop even
  after lookup membership ends. A separate Rx-owned disconnect count covers
  recursive disconnected_ work before transport contexts are released.
  These are drain progress facts, not duplicate lifecycle state. Costs are
  two owner-shard counters and cold updates; no lock, owner ref or scan index.
- WS applies the same native-link handoff and Tx-owned flags as JSON-RPC.
  Child transport stop plus per-connection close counters and Rx/Tx barriers
  guard raw endpoint captures. Protocol failure remains in application
  callbacks and does not turn successfully completed shutdown into false.
- Common MCP Client no longer stores a redundant done flag. Its closePeer_
  uses BatchCaller::closeCalls so queued notification posts are included.
  Each newly controlled independent endpoint has one engine (104 bytes for
  that subobject in the earlier foundational build); net endpoint sizes and
  callback allocation differences have not been measured. Canonical engine
  callbacks replace local waiter storage. No new hot-path lookup is added.

### REST endpoint boundary

- Client and Server now have complete parent engines. Both synchronous and
  asynchronous startup failure leave the parent Stopped after child rollback.
  Init/final retain their own resource responsibilities; no final initiates
  stop. Retained configuration supports restart, with drained-state assertions
  at the startup boundary.
- Client stop closes admission on Rx, stops HTTP, waits for posted admissions
  and request retirement, then posts Tx/Rx barriers. Server stop retains the
  existing bounded cancellation procedure, stops HTTP, and waits for live
  work and SSE producer posts. An Rx-owned session-drain count additionally
  covers recursive cancelSession_ continuations after work membership ends.
  Server's local stop waiter array, stopping/done/stopOK flags and wrappers
  are replaced by the engine. Costs: one owner counter and cold updates plus
  final Tx/Rx barriers; no added per-request reference operation/allocation.
  Engine size is known only from the prior foundational build; net REST
  endpoint footprints remain unmeasured.

### Source validation and deferred acceptance

- Restart coverage was added to existing stdio, WebSocket and REST integration
  fixtures. It reuses retained configuration/borrowed originals, checks fresh
  discovery/ID history and a new call, repeats start/stop, and closes every
  successfully started endpoint even when readiness fails. JSON-RPC's new
  restart paths use bounded readiness/request waits; existing older fixture
  waits have not been claimed generally hang-proof. Pipe fixtures explicitly
  transfer raw aliases to ZiFile or close their retained originals.
- JSON-RPC's init/final-without-start and failed stdio startup source cases
  cover runtime absence and rollback. The existing HTTP child-bypass probes
  now expect the parent to remain Running until parent stop drains protocol
  work. Tests are source edits only, not passing runtime evidence.
- Module READMEs document parent/child completion, main-thread synchronous
  calls, async boolean callbacks, explicit stop/final, restart and stdio
  ownership. `git diff --check -- zjrpc zmcp zrest` passed for this source
  batch. Changed endpoint source lines were checked for the 98-column limit.
- Still required: compile/instantiate every affected profile at the final
  implementation milestone; full endpoint callback/reentrancy/opposite-op
  cases; failure stages; lifetime closure for every post; scheduler budget
  review of preexisting cancellation/disconnect scans; generic engine pending
  opposite operations and destruction; Hubs failure restoration; indirect
  resolver callers; zum/ztc migration; repair1 occurrence closure; final
  runtime suites and Valgrind. No endpoint acceptance is claimed here.

### Next stage inventory: Zum (not yet migrated)

Current-source inspection confirms ServiceState still has an independent
Initial/Starting/Started/Stopping/Stopped enum and stop waiter vector;
Service::final merely releases its state. Its clear_ path wipes retained
configuration secrets as well as runtime tokens, so reusing it at stop would
prevent a correct restart. The migration must separate runtime reset from
final secret destruction and cover inflight HTTP callbacks/owner posts before
completion, while deliberately propagating the int diagnostic callback API
change. Existing start promises discovery/authentication/key loading and
optional SSF registration; that boundary must be retained rather than assumed
to mean transport-only startup.

Daemon::start/stop currently block on request-shard semaphores and maintenance
completion. Daemon::final still invokes stop. Cleanup, SSF delivery, Requests
admission, provider and HTTP form the dependency/drain inventory for its parent
engine. These are verified migration inputs, not completed repairs; inspect
provider/Requests/SSF callback contracts before changing that orchestration.


## 2026-10-18 — Zum::Service parent engine (source only)

Contract: Service startup still includes resource/management/transmitter
discovery, workload authentication, required key loading and workload-token
verification, plus optional SSF registration. It is not narrowed to starting
an HTTP transport: ServiceHTTPFn is an application-owned adapter. Stop must
cover the adapter's outstanding completions, protocol callbacks, owner posts
and both timers. Final releases an already-stopped component without waiting.

Implementation in ZumService.hh/.cc:
- Service directly inherits its sole ZmEngine, exposing canonical bool
  start/stop, synchronous main-thread calls, async callbacks, state and
  stopping. The old ServiceDoneFn int lifecycle callback and ServiceState
  lifecycle enum/stop waiter vector are removed. startError() retains the
  separate startup diagnostic; verify/publish/SET protocol error APIs remain
  distinct. The boolean API is deliberately propagated to the example,
  ZumClientTest and the current ZTC hub caller without a compatibility shim.
- Service uniquely owns ServiceState. All former ServiceState owner reference
  captures are raw pointers. Every HTTP send has an inflight credit retained
  through its owner callback and destruction of that callback's captures.
  Every accepted public input has an ingress credit retained through execution
  and action release. Queued variable input/response data is stored in a
  concrete first-base, identified ServiceAction allocation; scheduler closures
  carry its handle and fixed dispatch metadata rather than variable data.
- A single atomic ingress word combines a closed bit and outstanding credits.
  Admission CAS cannot publish new credited work after sealing. stop_ seals
  before posting to the protocol owner. An already admitted operation that
  loses the race to stop remains counted until its owner execution finishes.
  Callers still must not destroy the service while an external API call has
  not returned. Rejected non-Running operations complete immediately with
  ServiceError::Stopped; accepted operations retain their owner-shard callbacks.
  The refresh hook can be configured directly in quiescent Stopped state or
  replaced through a credited owner post while Running.
- Startup's result is retained only until credited callbacks finish and an
  owner continuation publishes started(ok). Failure cancels timers and
  finishes runtime cleanup before started(false). Successful stop uses the
  same drain boundary before stopped(true). Result/progress fields are not an
  alternate lifecycle enum or waiter queue. No private control thread is added.
- Both timers are cancelled with scheduler.del on the protocol owner; expiry
  scheduling now explicitly uses config.sid, as registration already did.
  Timer callbacks check active lifecycle before generating more work. Source
  inspection of ZmScheduler::schedule/del confirms timer enqueue and del are
  serialized by m_schedLock. The subsequent owner continuation therefore
  follows any already-enqueued callback, and a cancelled timer cannot enqueue
  a later raw-state callback behind the completion boundary.
- Runtime key tables and SET indices drain at most ServiceWork::Batch (64)
  logical entries per owner turn, across all three key sets and dedup state.
  This is a scheduler work budget, not an allocated capacity or a measured
  performance optimum. Iterator scope ends before a key table is released or
  another continuation is posted. Runtime tokens/discovery state are cleared;
  configuration secrets remain for restart and are cleared in final's
  quiescent state destruction. The old stop-time configuration secret wipe
  could not satisfy restart and is deliberately removed.
- Cleanup exposed a concrete existing SET index defect: receiveSET_ moved
  value.id into the lookup entry before copying it into the expiry index.
  The expiry index is now constructed before that move. Both indices receive
  the same actual ID; normal expiry and bounded stop can remove lookup state
  deterministically. The required second-index String copy remains explicit.
- ServiceState's public data precedes behavior. Immutable configuration and
  the shared admission word are separated from owner-shard mutable state by
  cache lines. Diagnostic startupError remains a primitive, observed after
  lifecycle completion rather than used for admission.

Cost and lifetime accounting:
- One complete parent engine replaces the local lifecycle enum/stop vector.
  The earlier foundational build measured an engine subobject as 104 bytes;
  this Service instantiation and net Service/ServiceState sizes have not been
  compiled or measured. Unique state ownership removes its ZmObject base and
  owner ref churn. Cache-line grouping adds padding; net footprint is deferred
  to the implementation build.
- Each accepted public input and each HTTP response handoff uses one explicit
  pooled fixed-size ServiceAction allocation and action-handle ref operations.
  This replaces variable closure capture with owned destination data; exact
  net callback allocation/size differences remain unmeasured. No claim of
  allocation-free service operations or performance improvement is made.
  Public input adds an admission CAS and release; stop has a cold seal RMW.
  HTTP credit updates stay ordinary owner primitives. Lifecycle completion
  adds an owner continuation (more turns only for nonempty bounded cleanup).
- Raw ServiceState callbacks cannot outlive credited HTTP/input execution or
  the cancelled timer owner barrier. No completion path accesses ServiceState
  after invoking the terminal engine completion. The adapter must call each
  completion exactly once; callback captures may themselves outlive invocation
  only when they perform no further owner access. This is not a claim about
  arbitrary destruction concurrent with an unfinished public call.
- Ordinary protocol waiter fanout and normal expiry traversal still need the
  final scheduler-budget audit. Generic ZmEngine pending-opposite/callback
  finalization remains open; no safety claim is made for final while a queued
  opposite lifecycle operation still requires the owner.

Source validation and remaining work:
- ZumClientTest source now covers valid init/final without starting its
  scheduler or issuing HTTP; direct final after failed startup rollback;
  restart without init using the adapter's actual Basic-auth and SSF callback
  secret checks; repeated start/stop; stop-callback finalization; held HTTP
  completion during stop; and stop queued at each held failed-start phase.
  The key-count cases include 64 resource keys plus the independent management
  set, requiring multiple cleanup turns. Existing diagnostic checks remain.
- README/example and the current ZTC hub caller use booleans. No current
  source declaration/use of ServiceDoneFn or int Service lifecycle callback
  remains in the repository search. Historical IMPLEMENTATION.md narrative is
  preserved; this dated disposition supersedes its old lifecycle description.
- git diff --check passed for the changed Service, test, example, README and
  ZTC caller. Added C++ lines were inspected against the 98-column limit.
  No rebuild, executable test, Valgrind, alternate configuration or performance
  measurement ran. The major remaining-module implementation batch is open.

Next dependency evidence (not migrated in this turn): Daemon still blocks on
cleanup semaphores and invokes stop from final. Its SSFTransmitter::stop only
cancels timers and posts a reference-holding barrier; it reports no completion
and does not drain its DB/sign/HTTP callback chains. Those chains include
sign/key callbacks into Daemon, so a parent stop must cover them rather than
merely retain the transmitter. Requests::deactivate has a completion but
currently publishes it after a drain-list clean, without using its admission
count as the whole boundary, and duplicate deactivate can complete early.
Zum::Server::stop finalizes its OIDC member and final calls stop. Inspect the
OIDC and Requests contracts before migrating Daemon, then propagate the
complete Zum boundary through ZTC. These are required remaining repairs/audits,
not reasons to narrow or declare the full objective complete.


## 2026-10-18 — daemon request/SSF dependencies (source only)

The preceding goal turn made source progress. This continuation inspected the
current dependency implementations before changing them. No live build or test
handle was found or resumed; no build/test/debugger/Valgrind command ran.
The existing configuration/artifacts remain in place and the remaining-module
implementation milestone is still open.

### Requests admission, cancellation and deadline retirement

Required boundary: all credited admissions, cancellation callbacks and queued
deadline accesses finish before deactivation completion. Requests is an
independently controlled resource owner (admission, an intrusive request list
and per-record timers); its DB activation/deactivation interface remains the
separate role-facing interface, not the daemon or database lifecycle.

Verified defects: duplicate deactivate completed immediately while an earlier
drain could still be running; deactivation did not wait for a producer that had
reserved admission but not yet posted its job. Normal completion also cancelled
a timer and removed the last list ownership without retaining its node behind
an already-enqueued raw timeout. Scheduler del cannot retract that queued
callback. These are concrete dependency/lifetime repairs needed by the daemon
boundary, rather than an engine added to a parser merely for having init.

- zumd_request.hh/.cc now give Requests one canonical admission-resource
  engine. activate maps to start and deactivation translates the canonical
  boolean completion into its existing void completion. Idle activation still
  opens admission inline; activation during drainage queues behind it.
  Duplicate deactivation uses engine callbacks. The old up/closing flags and
  single deactivated callback slot are removed.
- A sealed 64-bit word holds admission plus outstanding slot count. Acquiring
  a slot and closing admission cannot race into an uncounted publication.
  Accepted run jobs capture raw Requests under that counted boundary.
- Completed/cancelled records move from the active intrusive list into a
  retirement list. Timer del precedes posting its owner continuation, so a
  previously queued timeout can still inspect a retained done node. Both
  cancellation and retirement turns inspect at most RequestWork::Batch (64)
  records. No iterator survives a callback or post.
- Admission credit is released only after owner retirement releases the
  component's node reference. Including retiring records in the existing
  limit prevents a continuous producer from growing a separate unbounded
  retirement backlog. count() consequently includes those owned slots; an
  immediate replacement at a full limit can normally be refused until the
  retirement turn. This documented pressure result is necessary for the
  ownership bound and remains a normal run(false), not an assertion.
- RequestData stores an immutable scheduler/SID route and an atomic done
  flag. A retained cancelled token checks done before using its owner. A
  completion that races cancellation posts using that independent route and
  rechecks done on the owner shard before dereferencing Requests. A late
  completion after controller release therefore performs no owner access.
  The caller retains the scheduler through an external completion call that
  has not returned.
- Deactivation completes only after all sealed slots, retirement records and
  retirement continuations are gone. Destruction asserts that boundary.
  It does not stop/wait implicitly. DB/application owners still retain Requests
  through deactivation; this does not claim to drain arbitrary DB callback
  bodies that run outside RequestData::complete.

Costs: one engine subobject (104 bytes in the earlier foundational build; this
new layout is unmeasured), a widened sealed credit word, two cache-line groups,
and an independent pointer/SID plus atomic done flag in each request record.
Net sizes/padding await compilation. Existing intrusive links and node storage
are reused; retirement adds no per-record allocation or auxiliary index.
Active-to-retiring ownership moves through the existing NodeMvRef overload.
A coalesced owner retirement post and bounded continuation turns are added.
One-shot completion has a real cross-thread done read/store; admission remains
operational atomic synchronization. No diagnostic atomic is introduced.

ZumMgmtTest::requests source checks duplicate deactivation during its own
cancellation callback, pending activation during shutdown, refusal while a full
slot still owns its cancelled deadline, and cancelled/completed tokens used
after Requests release. Main-thread continuation barriers synchronize slot
reclamation; the added cases do not block an owner thread or poll completion.
The queued-timeout lifetime proof still needs the final stress/Valgrind gate.

### SSF transmitter dependency boundary

Verified defect: SSFTransmitter::stop previously only set a flag, cancelled
timers and posted a reference-holding callback. It neither reported completion
nor covered DB selections/updates, key loading, signing, saga persistence, HTTP
delivery or their owner continuations. Those callbacks can call back into
Daemon's signer/key/RNG resources. Keeping the transmitter alive did not drain
the parent dependency.

- zumd_revoke.hh/.cc now give SSFTransmitter its actual child engine and
  explicit final. Start opens admission and launches the initial deadline and
  retry queries; it does not promise completed outbox replay. Stop seals new
  inputs, cancels both timers on Requests' scheduler owner and drains the
  dependent work graph before stopped(true). Completed stop supports restart;
  final releases only quiescent configuration and clears its retained DB key.
  The started flag, timer generation and destructor-driven stop are removed.
- A concrete first-base, heap-identified SSFWork represents a dependency graph,
  with a raw transmitter back-pointer. Startup creates one graph; each
  accepted public registration/revocation reserves one graph through the
  sealed atomic credit word. Every internal stage borrows/transfers the
  existing Work handle: there is no allocation or credit increment per hop.
  Last dependent release posts retirement to the owner while the credit
  remains outstanding; it is released only in that owner job.
- DB, saga, key-load, sign, HTTP, timer and scheduler callbacks carry the graph
  handle. Every one-shot callback moves its captured credit into its execution
  frame when invoked. Multi-result selectors release their capture on the
  explicit void terminal result while retaining a frame handle through that
  final callback. Native ZmFn copies share the mutable lambda object, as
  verified in ZmFn.hh; retaining a fired completion handle therefore does
  not retain its active graph credit. The adapter must invoke each request
  completion once, and selectors must send their terminal result.
- DB-only deletion/commit callbacks are tracked too, so successful HTTP
  acknowledgement is not published as a completed stop before its terminal
  outbox transition. Pending timer callbacks hold graph handles; cancellation
  destroys/disarms them or the already-posted callback consumes its handle.
  They guard producer work with active engine state. Since every old timer
  access drains before restart, the former stop-generation atomic is not
  needed for this boundary.
- m_draining records that the owner has actually cancelled timers; it is not
  a lifecycle done flag. It prevents a simultaneous last-work retirement
  from publishing Stopped before the stop hook's owner task has arrived.
  m_drainPosted coalesces the final owner barrier. Engine state remains the
  sole lifecycle state, and terminal completion is the last owner access.
- Internal data paths preserve protocol status/error accounting and durable
  retry behavior. A public registration rejected by closed admission receives
  normal 503; malformed registrations retain their existing 400 handling.
  Revoke during closed admission remains ignored as before.
- Source review found issue_ using refreshID.issuerURL after moving refreshID
  into its completion, and makeSSF input arguments sharing data moved by a
  different argument's callback capture. Key-load issuer is now preserved
  before that move. Independent audience/RefreshID snapshots preserve
  makeSSF's synchronous borrowed-input contract regardless of argument
  evaluation order or inline String storage. This adds three String copies
  per signed notification, plus an issuer copy when keyLoad is configured.
  They are explicit correctness costs; no performance win is claimed.
- Daemon::prepare now completes child stop and final before replacing that
  transmitter or loading new signer state. This is its documented main-thread,
  admission-closed path. The daemon itself has not yet been migrated and its
  other dependencies still need their complete boundaries.

Costs: an engine subobject, the graph credit word and owner drain progress,
with cache-line separation. One identified pooled SSFWork allocation per
startup or accepted public input replaces owner-reference captures; descendant
handles retain actual work rather than the transmitter. No allocation is added
per internal stage. Graph fanout still has handle reference operations;
one-shot invocation transfers that handle, and terminal selectors have a frame
copy. Final graph retirement adds an owner post and one atomic release, with a
coalesced final completion post. Admission adds a CAS per accepted input and a
cold seal RMW. The old timer-generation RMW/read is removed. Net object,
callback and heap footprint remains unmeasured until the implementation build.

ZumRuntimeRevocationTest::transmitterLifecycle source seeds a real encrypted
receiver and durable outbox row in its existing in-memory DB fixture, holds the
remote HTTP reply, and requires two stop callbacks to wait for that reply and
the acknowledged outbox deletion. It deliberately retains the fired HTTP
completion through transmitter release. It also checks init/final without
start and restart/repeated start/stop without init. All synchronization is by
main-thread semaphores and owner continuations; no owner is blocked. The test
LDADD now includes the existing zumd_revoke object. Manifest regeneration and
all builds/runs remain deferred to the implementation milestone.

### Source checks and remaining scope

git diff --check passed for the changed request/transmitter source, daemon
prepare, tests, Makefile.am and READMEs. New C++ lines were checked against the
98-column limit. New tests are source coverage only, not passing evidence.
README documents owned-slot retirement pressure, token/route lifetime, child
startup meaning, complete stop and explicit final. Test documentation preserves
historical validation and explicitly does not apply it to this unbuilt batch.

Still required: inspect/repair OIDC protocol and HTTP-adapter cancellation,
Zum::Server provider finalization, and complete Daemon orchestration before ZTC
controllers. The transmitter's remaining variable data captures, DB selector
turn budgets and borrowed RNG ownership need the final shard/capture audit;
this change proves a drain design in source, not every unrelated property of
the component. Generic engine pending-opposite/callback destruction, Hubs
restoration, remaining service budgets, indirect resolver users, repair1
occurrence closure and the final build/test/Valgrind gates remain open.


## 2026-10-18 — OIDC adapter and protocol lifecycle source continuation

### Contracts, defects and owning boundaries

The native OIDC HTTP adapter previously started DNS during init and joined
origin-client shutdown during final. Its independently controlled state now
owns a canonical engine. Init stores configuration and reserves resolver
configuration without starting it; final asserts Stopped/drained state and
releases already-quiescent resources. Start/stop cover owned resolver startup,
origin-client startup/eviction/stop, accepted requests and owner callbacks.
Borrowed resolver startup requires Running; stop/final never control that
borrowed dependency. The native origin client now directly inherits its own
concrete identified first-base allocator, replacing the allocated empty facade
whose allocator was specialized for its implementation base.

OIDC protocol State previously opened admission during init, retained owner
references in HTTP/DB callbacks and asynchronously cancelled pending state
inside final without a completion boundary. It now owns one engine and exposes
start/stop/state through its existing private-state facade. Start means owner
admission is open, not authenticated/discovered. Stop atomically seals admission,
cancels pending login state/timers in batches of at most 64 and waits for every
accepted call's HTTP/DB descendants before publishing Stopped. Final neither
posts shutdown nor waits. Configuration survives completed stop for restart.

### Ownership and retirement proof in source

Each adapter input reserves a credit atomically with its admission gate before
posting. One concrete identified Action owns variable request/response data;
closures carry that handle and fixed metadata. Pending native starts, origin
evictions, responses and the application callback all retain that credit until
the final owner completion returns. Shutdown joins each child stop, then
releases clients in bounded owner turns, then stops the owned resolver. A
retiring origin joins its native engine stop without double-counting the
adapter stop's child completion. A retained public fn() intentionally owns the
private state; after final, sends reject before accessing the old scheduler.

Each protocol begin/finish reserves a sealed graph credit. Separate concrete
identified Begin/Finish work objects hold their stage payloads; all foreign
HTTP/DB callbacks consume the captured handle on invocation and post only the
handle. No internal callback retains an OIDCState reference. A raw owner pointer
is protected by the outstanding graph count. Work returns its credit from a
base destructor after payload/callback members have been destroyed, posting to
the owner before decrementing. An owner barrier follows timer cancellation and
last graph retirement before stop completion. Only the facade retains State.

OIDCUserLoad_ now optionally retains the same graph credit until its entire
helper graph is released. Its existing early failure completion can precede
other saga callbacks; counting only the application result would allow DB work
to outlive OIDC stop. The credit is the first data member, so all other helper
members are destroyed before it. Public standalone oidcLoadUser passes an empty
credit and retains its existing completion API. Pending requests use shadow
list links under the existing owning hash for constant-step cancellation;
no iterator spans callbacks or owner turns. Late responses see detached state
and cannot revive a cancelled login. Callback providers retain their existing
exactly-once/terminal completion contract.

### Allocation, reference, copy and scheduler costs

Adapter adds one identified Action allocation per accepted request, an engine,
a sealed 64-bit admission word, cache-line separation and owner drain counters.
It replaces variable cross-shard closures with handle moves. Stop/final release
native origin clients, so restart constructs them lazily again; the existing
array capacity survives stop. No alternate transport profiles or origin policy
were introduced. The normal origin lookup remains linear in configured origin
capacity and needs the final workload/budget disposition.

Protocol adds one identified Begin or Finish work allocation per accepted
public input, retaining existing pending-node allocations. Internal HTTP/DB
stages transfer the same work handle, not new work objects. Stage data moves
in place; no new protocol payload copies are required. Begin and Finish use
separate layouts to avoid reserving user/evidence output storage for discovery.
Two shadow list pointers per pending node and one list header support bounded
cancellation with no additional node allocation. OIDCUserLoad adds one graph
reference and matching retain/release; other graph references cover actual
callbacks and owner handoffs, not owner retention. Last graph destruction adds
one owner retirement post; stop adds cancellation continuations and a coalesced
final barrier. Engine/layout sizes and runtime overhead remain unmeasured for
this batch. Existing snapshot copies inside the DB helper were not broadened.

### Source coverage and open dependency propagation

Production main now explicitly starts/stops OIDCHTTP and integration callers
use the same contract. Adapter source cases cover never-started final with
workers stopped, borrowed dependency startup failure, restart without init,
repeated operations, accepted callback drainage, completion-driven final and a
retained function after final. RuntimeOIDC source adds protocol never-started
final, repeated start/stop, restart and held discovery. Stop must wait for the
held response; it then finalizes from its completion while retaining the fired
HTTP completion object. Source/diff and new-line width review passed. No build,
test or Valgrind ran; acceptance remains deferred to the implementation gate.

DEPENDENCY PROPAGATION IS STILL IN PROGRESS: Zum::Server still uses its old
init/stop/final lifecycle and must explicitly start/stop its OIDC child as part
of its own complete engine migration. Its current stop still calls child final;
this cannot be used after a successful child start. Do not treat this source
batch as executable provider/daemon readiness or rebuild before finishing the
remaining module migration milestone. Server arbitrary DB callback bodies need
a provider work drain beyond Requests cancellation. Daemon and ZTC migrations,
generic pending-opposite engine safety, Hubs failure restoration, outstanding
budget/capture/repair1 occurrence reviews and final build/test/Valgrind remain.


## 2026-10-18 — provider engine and callback graph continuation (unbuilt)

### Owning contract and implemented source boundary

Zum::Server is the embedded OAuth/passkey/OIDC operation owner, distinct from
its HTTP transport and the DB role's Requests controller. Its old stop only
called OIDC.final, while final also implicitly called stop and cleared callbacks
without a provider completion boundary. Server now directly inherits its own
canonical engine and exposes start/stop/state/stopping with using declarations.
Init leaves Stopped; start starts its OIDC child when configured and opens
provider admission on the Requests owner shard. Start does not activate a DB
role or promise authentication/readiness. Failure before child startup leaves
no provider work; OIDC reports its failed startup only after its own rollback.

Stop seals a shared 64-bit admission/credit word, asynchronously stops OIDC,
then waits for admitted provider callback credits and an owner continuation.
Final asserts Stopped, zero credits and no pending drain post; it finalizes the
quiescent OIDC child and releases configuration without starting shutdown or
waiting. Destructor final enforces the same stopped precondition. Restart keeps
configuration and recreates admission through the engine's start hook.

One concrete first-base identified ServerWork allocation covers an accepted
public input. It carries a raw provider back-pointer, never an owner reference.
Private asynchronous operation methods now explicitly receive the same Work
handle. All 74 operational mutable callback bodies in the current source have
explicit graph consumption. One-shot callbacks consume their captured handle;
metadata's row selector copies it per row and releases its capture at the void
terminal result. The last credit returns via a posted owner continuation.
Auth-route completion also consumes both local/OIDC branch lambdas immediately:
an adapter retaining the fired route completion must not retain the unselected
branch's credit. No separate lifecycle flags/waiter queues were introduced.

### Dependent source propagation and regression source

Repository searches found the provider owner in Daemon and the direct runtime
OIDC fixture. Daemon's current synchronous start now starts Server before HTTP
and stops Server when listener startup fails. Its existing stop call now waits
for the provider engine. The direct provider fixture explicitly starts/stops,
checks never-started final, repeated operations, restart, and final from a stop
completion. The old child-final-only stop path is removed. Daemon's own complete
engine migration remains open; these updates do not certify that controller.

### Costs and open proof obligations

Added one Work allocation per accepted public operation; no new allocation per
internal stage. Work copies/moves add reference operations at actual callback
branches and handoffs, protecting raw this captures across their callback frame.
Sealed admission adds one CAS per accepted input, plus one decrement on the
owner after final Work release. Added one retirement owner post per graph and
a coalesced stop barrier. Engine, admission cache-line separation and two owner
drain booleans increase Server size; exact size/cost remains unmeasured. Existing
variable callback captures, RNG use and DB snapshots were preserved in this
stage and still require the capture/shard audit, not claimed optimality.

CRITICAL OPEN DRAIN PROOF: cancellation of a Requests token consumes the
application completion while arbitrary helper DB/saga bodies may still run.
Provider callback credits alone do not prove those helper bodies have finished
using borrowed provider RNG/policy/signing resources. The concrete helper audit
identified AuthorizeComplete_/AuthorizeCodeComplete_ and their AuthorizeRequest_,
AuthorizeFinish_, AuthorizeOIDCFinish_, AuthorizeSessionFinish_,
AuthorizeConsentFinish_ descendants; TokenComplete_ and token/revoke helpers;
PasskeyBeginComplete_/PasskeyFinishComplete_ and enrollment/credential/recovery
helpers; SessionComplete_/SessionDoneComplete_ and their DB continuations.
These are in zumd_authorize.cc, zumd_token.cc, zumd_passkey.cc and
zumd_session.cc. Their cancel paths move m_complete independently of the actual
helper graph; they need explicit graph lifetime coverage, including early saga
results. Do not equate a cancelled request/result with complete helper drainage
or claim Server's final lifetime proof complete yet. This is the next provider
implementation step before Daemon/ZTC migration and final acceptance.

Source/diff whitespace and added-line 98-column checks pass for the changed
provider header/source, daemon startup and runtime fixture. No build/test or
Valgrind ran. The implementation milestone remains open. Generic engine pending
opposites/destruction, Hubs restoration, remaining budget/capture/repair1 audits,
remaining modules and final incremental build/test/Valgrind gates remain open.


## 2026-10-18 — provider DB helper graph lifetime continuation (unbuilt)

### Repair and source ownership boundary

The previous provider continuation identified a real gap: Requests cancellation
moves and delivers an application callback before DB/saga helpers necessarily
stop using borrowed provider resources. This stage gives those actual helper
graphs an explicit caller credit independent of application completion.

RequestHold in zumd_request.hh retains a ZmRef<ZmPolymorph> credit as a base.
It is destroyed after every derived helper member. Thirty concrete helper
and completion types now initialize that base: eight authorization types,
ten passkey/capability types, seven token/revocation types, two session types,
and the signing-key/discovery/JWKS loaders. Existing concrete first-base heaps
and their identities remain. Their own callback graphs retain their existing
helper references; the extra hold retains the credit, never the provider.

Twenty-two public helper APIs now take an explicit first credit argument.
Provider calls pass their current Work. Nested token/consent and other concrete
helper construction forwards the same credit; request-run callbacks obtain it
from their completion state's hold. Standalone callers and current main-thread
bootstrap/admin paths explicitly pass {} and retain external responsibility
for borrowed-resource lifetime. All repository call occurrences, declarations
and definitions found for these symbols were updated without overload shims or
default-argument compatibility paths. Daemon's full engine/work migration is
still required before its asynchronous paths can be certified.

The hold survives normal result delivery, request timeout/deactivation and
failed saga admission while sibling terminal callbacks still retain their
helper. Session's linear DB chains retain their completion state and therefore
its independent hold. SagaDriver::terminal_ removes the live saga and invokes
its moved terminal callback; pending submit/complete callbacks retain their
normal helper references until released. No helper relies on the application
result to keep its borrowed RNG/configuration alive.

Nine foreign one-shot policy/signing callbacks now move their captured helper
reference into the invocation frame. A retained fired callback cannot pin the
helper hold indefinitely. An empty-capture guard preserves ignored duplicate
policy/sign results after that move. DB row selectors keep their existing
multi-shot/terminal behavior; they are not converted to one-shot callbacks.

### Concrete destructor dispatch correction

Source review found that the earlier, unbuilt OIDC helper scope was erased as
ZmRef<ZmObject>. ZmObject deliberately has no virtual destructor, so a final
release through that type would skip concrete credit destruction and violate
heap deletion/retirement. All new erased holds use ZmRef<ZmPolymorph> instead.
ServerWork and OIDCBeginWork/OIDCFinishWork now inherit the framework's
polymorphic reference base. Their concrete first-base allocators remain in
place. This virtual destruction is required for the erased credit lifetime;
no virtual protocol dispatch/interface was added. OIDCUserLoad_'s existing
first-member hold uses the corrected type too. This was caught before builds
or runtime acceptance; the prior source batch was not verified safe.

### Costs

No extra allocation per helper and no new payload copies. RequestHold adds
one reference-sized field to each participating helper, with one retain and
release at its initialization/destruction; explicit parameter and forwarding
handles add the corresponding reference operations. The existing one Work
allocation per accepted provider/OIDC input is shared throughout the graph.
Erased concrete credits add framework virtual destruction/vptr cost; exact
layout sizes remain unmeasured. Nine foreign callbacks move their existing
reference rather than keep it after invocation; empty-repeat checking adds a
branch per invocation. No polling, scanning, control thread or owner-reference
capture was introduced. Final graph release uses the existing owner retirement
post and stop barrier; no new per-helper retirement posts were added.

### Regression source and remaining work

ZumRuntimeTokenTest::helperCredit uses the existing in-memory DB fixture and a
valid client-credentials request. It supplies an identified polymorphic credit
probe, holds the signing completion, deactivates Requests and requires the
application cancellation result while the probe remains live. Releasing signing
must retire the probe even while the already-fired foreign completion remains
retained; repeating that completion must not access the helper or deliver a
second result. This reproduces the result-vs-helper lifetime distinction and
checks erased concrete destruction. Main-thread semaphores/owner continuations
provide synchronization; no sleeps or worker blocking. Failure cleanup releases
a retained fired callback before waiting for its credit to avoid abandoning
borrowed stack state. The fixture reopens Requests afterward.

Source checks confirm each of the 30 added holds has its initializer, no
ZmRef<ZmObject> erasure remains in the changed graph code, changed C++ lines
satisfy the 98-column limit, and git diff --check passes for zum. Tests were
added to the existing wired runtime-token target. No build/test or Valgrind ran.

This closes the specific missing provider/helper lifetime mechanism in source;
it does not establish passing runtime acceptance or complete every shard/
variable-capture proof. Provider RNG ownership, variable cross-shard captures,
selector turn budgets, daemon maintenance/admin graphs and full Daemon engine
orchestration remain to audit/migrate. ZTC, generic pending-opposite engine
safety, Hubs failure restoration, repair1 occurrence closure and all deferred
incremental dependency builds/tests/final Valgrind gates remain open.


## 2026-10-18 — daemon engine and maintenance continuation (unbuilt)

### Contract and changes

Daemon's inherited HTTP start/stop and blocking private lifecycle orchestration
were separate from provider, SSF, Requests and maintenance shutdown. Final
implicitly stopped and waited. Daemon now directly inherits one ZmEngine for
that complete parent boundary, exposes its start/stop/state/running/stopping,
and keeps the child HTTP controller separate. Removed m_started, m_finalized
and the maintenance-stop semaphore. Maintenance Idle/Running/Stopping remains
because it describes a maintenance operation, not parent engine state.

Start asynchronously starts the provider, HTTP listener and any configured SSF
child, then opens parent work admission and initializes maintenance on its owner.
A failed listener start stops the provider before started(false); a failed SSF
start stops HTTP and provider before failure completion. The child SSF failed
start reports its own completed rollback. No lifecycle hook blocks a worker.

Stop seals parent credit/admission and closes HTTP acceptance, cancels the
maintenance timer on its owner, asks an active maintenance operation to stop
between stages, and joins Requests deactivation, provider stop and SSF stop.
It waits for parent work credits and maintenance, posts owner/Rx/Tx barriers,
then stops the HTTP child after admitted application replies have drained. Its
last owner continuation clears drain metadata and reports stopped(true).
Final asserts Stopped/no credits/no maintenance/no drain posts, releases the
quiescent children and secrets/configuration, and clears borrowed pointers.
HTTP final is still unconditional so partial/never-started initialization also
releases the child's stopped configuration. Destructor enforces the same
explicit-stop precondition rather than starting shutdown.

Prepare now asserts Stopped plus closed Requests admission before replacing
signing state or the SSF transmitter. The main activation path explicitly
stops the parent before prepare, then restarts it afterward. This drains owned
users before replacing their borrowed signer/RNG/configuration. The daemon
continues to borrow DB, scheduler and externally controlled HTTP adapter;
these remain alive through daemon stop. DB role readiness stays separate.

### Maintenance/auth-route work ownership

An identified concrete DaemonWork graph carries a raw owner pointer and sealed
admission credit. MaintenanceRun_ and AuthRouteLoad_ retain it with RequestHold
through destruction, independently of their application completion. The three
shared maintenance helpers BulkRevoke_, ExpiredCleanup_ and AppCleanup_ also
retain a forwarded credit through actual DB/saga lifetimes. The four cleanup
factory APIs take an explicit scope. Maintenance forwards its current graph;
still-unmigrated administrative callers currently pass {} pending the admin
stage below. Maintained source searches include every cleanup factory caller.

MaintenanceRun_, ExpiredCleanup_ and AppCleanup_ were allocated concrete types
without their own identified first-base heap; they now have one. No separate
runtime control thread or timer scan was added. Maintenance/auth-route admission
adds one Work allocation per accepted operation, plus reference operations in
actual helper holds. Internal stages share the graph, without per-stage Work
allocations. Existing MaintenanceResult snapshots are fixed metadata. The
engine, sealed credit, cache separation and child/drain counters increase parent
size; exact footprint/runtime cost remains unmeasured. Stop adds the bounded
number of child/owner/Rx/Tx posts shown in source. Configuration and runtime
container storage are reused on restart.

### Explicit remaining daemon gap

THIS IS NOT YET A COMPLETE DAEMON LIFETIME PROOF. Administrative HTTP request
callbacks and their DB/saga helper graphs still contain raw Daemon/RNG users
outside the new parent work credit. Requests deactivation can deliver their
application error before those arbitrary helper bodies finish, just as the
previous provider gap demonstrated. The next stage must gate/credit admin
admission, pass that graph through Daemon::adminRequest_/adminCall_/other async
member methods, and attach it to the actual administrative helpers (including
AppEdit_, AssignmentAdd_/Change_, BulkRevoke_, ActionAdd_, AppEnroll_,
CatalogPublish_/Retire_ and query helper families). Do not treat a parent
Stopped value as proof for those unconverted paths yet. The new prepare
ordering depends on closing this gap before runtime acceptance.

The scope-bearing cleanup factories, maintained owner hooks and main activation
source pass whitespace and 98-column added-line checks. Searches confirm the
old m_started/m_finalized/m_cleanupStopped lifecycle machinery and synchronous
Daemon start/stop implementations are removed. No build, executable test or
Valgrind ran. Daemon lifecycle tests and administrative graph migration remain
part of the same deferred implementation milestone. ZTC and previous final
engine/Hubs/shard/capture/budget/repair1 audits and verification gates remain.


## 2026-10-18 — daemon administrative graph continuation (unbuilt)

### Credit coverage and teardown order

The administrative gap recorded above now has source-level graph coverage.
Daemon's HTTP admin entry rejects new work outside Running, validates its
synchronous input under the HTTP child's lifetime, then reserves a sealed
credit before asynchronous request admission. One identified DaemonWork is
shared across reply/cancel/request callbacks and explicit Work parameters in
adminRequest_, adminAuth_, adminAccess_, idemBegin_, adminCall_ and adminAudit_.
One-shot mutable member callbacks consume their credit capture; direct row
selectors retain a local credit through each row and release at terminal.

Eight additional concrete helper families now derive RequestHold: AppEdit_,
AssignmentAdd_, AssignmentChange_, ActionAdd_, AppEnroll_, CatalogRetire_,
CatalogPublish_ and AdminQuery_. Together with the three maintenance helpers,
all eleven admin-source families have explicit hold initialization. Template
instantiations share the same mechanism. Constructors in member operations get
the current Work; nested catalog retirement forwards its parent helper hold.
The query/finder/delegation factories also receive a scope and retain it in
their actual DB callback frames or concrete query helper. Recovery/bootstrap
helper calls and application cleanup forward their parent's real credit rather
than {}. There is no independent owner reference or lifecycle waiter queue.

Consequently a cancelled administrative Requests token can return its HTTP
error while the same parent credit remains in DB/saga helper lifetimes. Parent
stop's existing count cannot reach zero until those actual holders are gone
and their retirement post reaches the owner. This addresses the particular
missing admission/helper lifetime mechanism in source; runtime proof and the
broader capture/shard/budget review are still required.

Startup/shutdown dependency ordering was refined after tracing revocation side
effects. Startup now starts provider then SSF before opening the HTTP listener,
then initializes owner runtime and completes parent startup. Failed SSF startup
rolls back provider; failed HTTP startup stops SSF then provider before reporting
false. Shutdown joins Requests/provider and parent producer graphs first, then
stops SSF, then runs Rx/Tx and HTTP completion barriers. Stopping the consumer
alongside still-live provider/admin producers would reject a late accepted
revocation even though its DB operation was admitted before shutdown. SSF now
remains available until those producers and their helper graphs have drained.

### Demonstrated RNG shard repair and costs

correlation_ was called on Rx at administrative ingress and also from an owner
reply fallback, using the same mutable RNG as signing, maintenance and DB work.
Rx correlation now has a separate cache-separated generator initialized while
Stopped. The reply fallback retains the request's original correlation ID
instead of generating another ID from that worker callback. Thus correlation_
has one Rx call site and no worker mutation of that generator. Existing worker
RNG ownership/configuration remains subject to the final owner-shard audit.

One parent Work allocation is added per accepted asynchronous admin input,
with CAS admission and reference copies/moves at real callback branches. Early
synchronous malformed/inactive replies remain under the HTTP child and do not
allocate a parent Work. Each of the eight new helper families adds one retained
reference-sized hold and its retain/release, without new helper allocation or
protocol payload copies. Query primitive callback captures add one scope handle.
The extra RNG adds its native context/entropy initialization and cache-line
layout cost, not a lock. Exact layout/runtime measurements remain deferred.
Existing variable callback captures and selector budgets have not been declared
optimal merely because this lifecycle count now covers them.

### Regression source and open acceptance

Added ZumDaemonTest.cc and wired it into zum/test's default all/test target.
It links the actual daemon/admin/transmitter objects plus existing support and
uses the application's catalogs. Cases cover never-started final with no mx
workers or DB runtime, repeated final, reconfiguration after final, and a
partial init failure after provider initialization when HTTP rejects port zero.
These cases exercise the explicit stopped final/resource-destructor boundary;
no socket is opened. Full listener/restart/failure/pending-opposite/active-admin
and maintenance cases still belong to final lifecycle acceptance.

Source whitespace and added-line width checks pass. There are no old
Daemon m_started/m_finalized/maintenance-stop semaphore paths. Searches cover
the scoped admin methods, concrete/template helper allocation sites, cleanup
factories and the sole Rx correlation call. No build/test or Valgrind ran.
Makefile regeneration/build of the new target stays deferred to the current
remaining-module implementation milestone. The goal remains incomplete for
ZTC migration, final ownership/capture/budget/occurrence audits, generic engine
pending-opposite safety, Hubs failure restoration and all runtime gates.


## 2026-10-18 — Ztc::App engine and runtime retirement (source only)

Contract: App init retains configuration while Stopped, start establishes the
owned scheduler, rings, PID ownership and watches, and stop joins the complete
publication/request/subscription graph before terminal completion. Agent or
network connectivity is not startup readiness. Final must release configuration
without starting shutdown or hiding a scheduler/request-thread join.

App now directly inherits ZmEngine and exposes canonical control methods and
state. The old control mutex/waiters, initialized/running flags and standalone
request thread are removed. Init creates no scheduler or watches. Start creates
a fresh scheduler each attempt; stop disposes it on its own existing control
thread after all scheduler workers have joined, following the resolver pattern.
Restart retains configuration but recreates runtime resources, avoiding retained
predecessor control threads. Final asserts stopped/drained state and has no
stop, join, block or post. Never-started final and failed startup release paths
remain explicit. Generic engine pending-opposite callback safety remains open.

The formerly independent native request loop runs on an extra isolated scheduler
slot. ZiPIDFile initialization can wait for another process's publication, so
it runs on that same dedicated native-I/O slot, not on mixed workers. There is
no additional control thread, and total request/control/worker thread count is
unchanged. There is an extra scheduler slot's queue/ring, thread metadata and
configured queue storage. Slot indices for configured timer/worker remain
unchanged. Placement scheduler storage stays inline; actual sizes and runtime
costs remain unmeasured. Each restart intentionally recreates scheduler queues.

A sealed atomic credit word covers accepted alert and RAG work through owner
callback and payload retirement, including reservations before publication.
The existing alert ingress lock serializes target closure with publication;
it does not permit wrong-shard access. Alert sinks retain only independently
owned ingress and safely reject after closure/final. Stop seals admission,
cancels timers, removes watches, wakes and retires the request loop, and clears
subscriptions, pending frames, indices and alert tail in 64-item batches.
Worker/timer/worker barriers precede final shutdown telemetry and scheduler
stop. Fixed metadata/raw owner handles are posted; no owner reference or
per-input lifecycle allocation is introduced. Each accepted input adds CAS and
retirement atomic work; stop adds bounded continuations and shard barriers.

Source review found a concrete watcher race: clearing existing per-Mx watches
before MxMgr::unwatch allowed a new registration to retain an App callback in
between. MxMgr::unwatch synchronously serializes with add/del callbacks under
its existing registry lock. It now precedes the all-Mx per-object unwatch pass;
the latter holds the registry read guard and per-object unwatch synchronizes
with connection callbacks. No new lock is introduced. Registry all/watch scans
are still synchronous and unbounded; their workload/budget disposition remains
open. Replay subscription lookup's iterator now ends before finish/failure code
that can remove the node. Linear replay lookup and ordinary publication fanout
still need their final workload/budget review.

ZtcAppTest source covers never-started final, no-init start failure, retained
sink release, repeated start/stop, restart, and final from a stop completion.
A real ZiPIDFile owner in the same process deterministically rejects startup;
the case checks rollback, stopped final, owner-file retention and subsequent
reinit/start. Existing request/snapshot/shutdown coverage is retained. The wire
publisher fixture now uses scope guards to stop on successful-start early exits
and to destroy registered sample objects before publisher stop. README states
the explicit lifecycle contract and extra native scheduler slot. These are test
sources, not passing test results.

Source whitespace review passes. No build, test, runtime measurement or
Valgrind ran in this stage; current configuration/artifacts remain unchanged.
Agent/Hubd migrations, remaining controllers, generic engine pending-opposite
handling, Hubs stop-failure restoration, final capture/shard/budget/occurrence
audits and all deferred acceptance gates remain incomplete.


## 2026-10-18 — Agent HTTP dependency lifecycle (source only)

The Agent audit demonstrated that Zum::PingHTTP::init synchronously started
resolver and HTTP destination clients, send could synchronously start a newly
discovered origin on an I/O callback, and final synchronously stopped clients
and resolver. Agent cannot implement an asynchronous complete shutdown while
this owned dependency hides those waits. This is a dependency migration of the
same init/start/stop/final contract, not a change to OAuth/protocol readiness.

PingHTTPState now directly owns an engine. Init reserves owned resolver
configuration before configuring clients, so every child observes that resolver
as borrowed. It initializes destination clients without starting them. Start
starts an owned resolver (or requires a borrowed resolver already Running),
then configured clients sequentially. A failed child start joins every child
stop and the owned resolver stop before reporting started(false). Stop seals
admission, joins every destination engine, waits actual accepted request object
retirement and owner completion posts, then stops its owned resolver. Final
asserts Stopped/drained and releases stopped clients/configuration without a
stop or wait. The client array is bounded by the existing eight-destination
policy and retained for restart. No configuration or allocator matrix changed.

The existing concrete identified Call allocation now holds the admitted request
and completion payload, plus a raw state return pointer. Request builders read
that request directly; no second body/manifest/authorization copy is added.
A base destructor returns the admission credit only after the actual Call
members are destroyed, including reference holders from native request slots.
The application callback is moved and run on the adapter's Rx owner; returning
a result alone does not return the credit. Mutable destination lookup/addition
is owner-affine. Tx receives the Call handle and client raw pointer under the
parent/child drain contract. Dynamic origin startup uses the child callback API;
already Running origins avoid an idempotent engine call and its lock. Retained
fn handles independently retain stopped facade state and reject before touching
a cleared multiplex pointer.

Cost: no extra per-input allocation; existing Call grows by request URL/method/
timeout metadata, response payload storage and one raw state pointer, while
existing body/manifest/header fields move into the request struct. Response
body copying from parser results is unchanged. Admission adds atomic CAS and
retirement, an owner admission post and owner completion/retirement posts;
Tx retains a Call handle rather than variable-sized request/completion captures.
New dynamic destinations add asynchronous start/owner completion posts.
Eight bounded origin probes and the existing configuration/start costs remain.
The parent adds engine storage, credit, child count and one drain fact. Exact
object sizes/reference/post counts await the final measurement gate.

Explicit adapter start/stop calls propagate to zumpingd, ztchub and the current
Agent path. Existing Agent startup rollback also stops its successfully started
WebSocket client before final (the complete-client final precondition makes the
old direct final invalid). The Agent itself is not yet migrated: its native
thread join, semaphores, synchronous child orchestration and shard/lifetime
issues still require replacement. Hubd and remaining controller audits stay
open. Concrete child-stop failure assertions follow the current native HTTP
contract, whose Hubs restoration audit is still outstanding; this is not proof
that every pending-opposite/failure path is safe.

ZtcAgentTest source now exercises the production adapter's no-init/never-started
final, retained fn rejection after final, borrowed-stopped resolver startup
failure without borrowed finalization, repeat operations, restart and final
from a stop callback. Main uses parse/log setup. No remote request is made;
active-request cancellation and transport failures remain final integration
coverage. README and test documentation were updated. Source whitespace and
added-line width checks pass. No builds/tests/Valgrind ran; implementation and
all deferred acceptance gates remain incomplete.


## 2026-10-18 — Agent complete engine migration (source only)

Agent now directly inherits ZmEngine and exposes its lifecycle/state APIs.
The old started flag, synchronous child orchestration, telemetry-thread join,
linkDone/routeDone/scanDone stack semaphores and final-initiated stop are removed.
Init validates and retains state without creating a multiplex or runtime thread.
Start creates a fresh multiplex, starts PingHTTP (and its resolver), configures
and starts the native WebSocket client, attaches telemetry and scans publisher
inventory before completing startup. OAuth/WSS readiness remains asynchronous.
A failed dependency/startup operation joins child rollback, clears runtime and
retires the multiplex before started(false). The native H1TLS WebSocket config
now uses its actual TLSConfig type, not H2Config; the binding requires TLSConfig.

The old independent telemetry thread is now a third isolated multiplex slot.
Rx/Tx retain slots 1/2; slot 3 runs the intentionally blocking native ring loop.
Physical thread count does not increase, but scheduler queue/ring storage and
slot metadata do. Shutdown sets the producer stop word and writes a bounded
zero-body ring wake record where capacity permits. A full ring already gives
the reader work; its stop test exits before another read. Native-loop exit
posts back to Rx; scheduler stop subsequently joins that native slot before
terminal parent completion. A fresh concrete heap-identified Mx allocation is
created per start and deleted on its own existing control thread, following the
resolver/App disposal pattern. State itself now has its actual concrete type
as the first heap base, removing the former State_ allocator specialization.
Native/shared telemetry data and following Rx-only state are cache-separated.

Stop seals provisioning admission and sets the stop word before posting Rx.
Rx cancels both timers and closes scan state. WebSocket stop joins link, codec,
request and Rx/Tx work before PingHTTP stop retires HTTP work and its owned
resolver; this preserves resolver lifetime through all WebSocket consumers.
HTTP completion is known to occur on this same Rx owner, so Agent consumes
metadata/token responses directly rather than posting variable-sized payload
captures. Stop waits actual PingHTTP request lifetime and queued provisioning
credits. Provisioning replies reserve the sealed count before publishing and
return it after their owner continuation; no post can race multiplex disposal.

Once both children and the native producer have retired, routing cleanup runs
in fanoutBatch turns. Each pending/active leg or request/publisher removal counts
one unit. Active legs retain the existing unsubscribe attempt before unlinking.
Request-list links are shadow links, so the publisher hash owns deletion;
cleanup removes a shifted leg directly from that hash instead of rewalking an
already-detached pending list. Request/publisher iterators end before mutation.
Rx/Tx/Rx barriers precede child final, then owned multiplex stop/join/disposal.
Final asserts stopped/drained and deletes only configuration; no stop, join,
wait, post or running scheduler destructor remains in that path.

Stop gates now prevent queued telemetry, fanout continuations, transmit posts,
connection inventory posts and timers from rearming work or recreating routing
state. Runtime native-reader failure still requires explicit application stop;
it does not invent a protocol-to-lifecycle failure transition. Child-stop
assertions rely on the current native client contract; Hubs failure restoration
and generic pending-opposite callback safety remain separate open audits.
Normal reconnect fences, inventory fanout, publisher retirement and hash-first
lookup capacity work still need workload/budget disposition; bounded shutdown
removal alone does not prove all normal mixed-work turns bounded.

Cost: one parent engine and sealed admission word, three distinct owner drain
facts, retained directory scan handle, and cache padding. No new control thread,
per-input allocation or parent reference captures. Provisioning adds admission
CAS/decrement; stop/start add fixed metadata/raw owner posts and barriers.
Initial directory enumeration counts every inspected entry (including skipped
names) against fanoutBatch and retains its native directory handle across
continuations. Callbacks no longer copy/move response objects into scheduler
closures. Runtime allocations remain a new multiplex plus its queue resources
per restart; exact footprints and post/ref measurements remain deferred.

ZtcAgentTest source now covers no-init/never-started final, closed provisioning,
borrowed-stopped resolver startup failure, a second failed attempt without init,
repeat operations, restart and final from a stop callback. Successful runtime
cases use an isolated telemetry ring, empty publisher registry and loopback
OAuth URL; they do not require successful OAuth or remote WSS. Main entry
already explicitly stops Agent before final. Header/README/test docs describe
startup readiness and final ownership. Searches show no Agent semaphore/join/
old-started paths; whitespace and new-line width checks pass.

No build/test/Valgrind ran; these are source changes and test definitions only.
Hubd, remaining controller/capture/shard/normal-budget/occurrence audits, generic
engine pending-opposite safety, Hubs restoration and the full deferred build,
runtime and memory gates remain incomplete.


## 2026-10-18 — Zdb activation correction and partial Hubd migration

User clarified the governing server contract: main starts the multiplexer
independently of cluster role. Zdb up/down owns listener/protocol-hub activation
and deactivation; Running process runtime does not authorize a standby listener.
repair2.md now records this explicitly, including generation-checked deferred
preparation and joining role-controlled work during process shutdown.

Review found zumd main started Daemon immediately after init, before processing
DB activation. That call was removed. The DB owns a fixed raw pointer to the
stack daemon, installed before DB startup and valid through completed DB stop.
DB up still notifies main to perform blocking bootstrap/signing preparation.
The continuation now returns to the DB thread via dbReady, asserts DB-thread
ownership, checks stopping/active/current generation there, and asynchronously
starts the daemon. DB down deactivates both Requests controllers and immediately
requests asynchronous daemon stop; stop failure is logged and signals process
shutdown. Main still joins daemon stop before changing prepared signing state
and explicitly stops on final process exit. This preserves the generation check
across an activation that is lost during preparation. Multiplexer startup stays
in main and is not role-gated. No extra allocation, lock or control thread was
added: one fixed raw pointer, one DB continuation/main wake and canonical
start/stop callbacks. Actual clustered failover/runtime proof remains deferred.

Hubd implementation was in progress when the user paused the goal. The current
source has a direct parent engine, separate readiness Down/Up, boolean lifecycle
completion plus startError, retained stopped Service configuration, DB/service
startup rollback and bounded parent stop cache cleanup. It removes the old
lifecycle enum/waiters/final-initiated stop. DB role and disconnect continuation
posts are counted with raw state lifetime tied to child drain, rather than
retaining state in scheduler captures. Final asserts stopped/post drainage and
finalizes only stopped dependencies. The API change is propagated to current
main/unit/integration readiness and callback callers, preserving diagnostics.

THIS HUBD WORK IS INCOMPLETE: main still starts Listener objects after hub.start
rather than from DB up, and DB down currently changes readiness without joining
actual listener stop. The next implementation stage must register role-controlled
listener activation, implement asynchronous listener/group start-stop, and join
those transitions in Hubd completion. No claim is made that the partial Hubd
engine already satisfies the clarified two-level contract. Its normal DB-down
cache cleanup remains unbounded; browser timer captures, public ingress/owner
contracts, partial DB init, stop-failure restoration and queued callback lifetime
still need final review. Preserve the paused stage's work rather than treating
its existing diff as completed evidence.

Whitespace and added-line width checks pass for current changes. No build,
test or Valgrind ran. Hubd/listener activation, remaining controller and repair1
inventories, generic engine pending-opposite safety, Hubs restoration and all
deferred executable/memory acceptance gates remain incomplete.


## 2026-10-18 — Hubd role-controlled listener wiring (unbuilt)

The previous partial Hubd note's listener-wiring gap is now addressed in source.
Main still starts the multiplex and HTTP adapter independently of role. It
configures, but does not start, each listener before Hubd process-runtime start.
Hubd's configured activation callback controls a separate Zhttp::Hubs aggregate
from DB up/down. Each Listener itself derives the existing Hubs controller to
join its WebSocket and SSF HTTP engines, replacing synchronous start/stop,
ZmBlock and three parallel initialized/started flags. Listener has a concrete
first-base identified heap. H1TLS WebSocket configuration uses its required
TLSConfig type. Failed initialization still finalizes only stopped resources.

DB up records readiness while process startup is incomplete. Successful DB
startup queues a counted owner continuation before engine completion; it starts
listeners only if the process is Running and still active when that continuation
executes. Subsequent DB up directly starts the aggregate on Rx. DB down sets
readiness Down and requests aggregate stop even when the parent is stopping.
This prevents a standby from listening merely because Hubd runtime is Running,
and avoids opening listeners before public routing admission is available.
The existing ready message now follows actual aggregate activation completion,
rather than being printed for a standby runtime. No output stream is changed.

State activation holds a pending-post credit through the actual external hub
completion and its Rx reply. Process stop also explicitly joins aggregate stop
in the already-inactive/failed-start cases, so lack of a new DB-down callback
cannot bypass that drain. Service drain and the post count protect raw parent
pointers; final follows completed listener/service/DB work. Main cleanup joins
Hubd stop, finalizes listener configuration, releases concrete listeners, then
finalizes Hubd/adapter and stops mx. A scope guard invokes the same cleanup on
exception exits; normal explicit cleanup is idempotent. Existing runtime App
failure diagnostics are collected only after listener stop, before release.

No extra control thread or per-message allocation is added. Each listener gains
an existing aggregate engine/entry array for its two independently controlled
protocol resources, and main owns one aggregate entry array for configured
listeners. Three duplicate flags and blocking control captures are removed.
Each role transition adds an atomic held credit, owner reply credit and fixed
metadata callbacks/posts. Listener lifetime stays under explicit main ownership;
State scheduler posts retain raw pointers with counted drainage. Actual layout,
allocation/ref/post measurements remain deferred. Normal DB-down cache cleanup
is still unbounded; readiness/public-owner and browser-timer/capture audits,
partial DB-init handling, activation-stop failure propagation and generic engine
callback/pending-opposite safety remain open. The new hook alone is not proof
of the full lifecycle contract in failure cases.

ZtcHubTest source covers held deactivation, never-started final, repeated ops,
restart and completion-driven final. The held case uses routing-only mode to
isolate parent drain without election/network fixtures; actual DB role change,
listener rebinding and failover still require integration evidence. README and
test docs state the two-level contract. No build/test/Valgrind ran.

## 2026-10-18 — hub aggregate idempotent stop retry (source only)

User pointed out stop is idempotent. Normal Hubs shutdown now invokes every
registered hub, including hubs already stopped by a preceding partial attempt.
Startup rollback still limits itself to hubs started by that attempt. Failed
stop no longer clears the corresponding Entry.started marker; that marker also
controls the explicit stopAccepting pass. The old behavior skipped the failed
hub on the next attempt and could falsely report completed shutdown.

Added a regression source with two hubs: one refuses its first stop, the other
stops; a second aggregate stop targets both, defers the formerly failing hub,
joins two aggregate callers, and completes only when that deferred reply arrives.
It also checks the still-active hub receives the next stopAccepting pass. No
allocation, member size, lock or thread is added. Retry adds one idempotent stop
call per registered hub instead of skipping those already stopped; successful
shutdown behavior is unchanged.

This is a retry-bookkeeping repair, not a claim that failed aggregate stop
restores the entire operational state. Other hubs may already have stopped,
and explicit stopAccepting may have closed admission before failure. Generic
Hubs operational restoration, failure of a restoration start, startup rollback
whose own stop fails, and Hubd activation-stop failure propagation remain open.
Source whitespace/new-line width checks pass. No tests/builds/Valgrind ran;
all deferred executable and memory gates remain outstanding.


## 2026-10-18 — engine pending-opposite completion and cancelled waiters

Source review reproduced the generic lifetime defect independently of module
controllers. started(true) with StopPending published Running, invoked startup
callbacks, then called stop on this. A callback could synchronously stop and
release the owner, leaving that replay to access freed memory. stopped(true)
with StartPending similarly exposed Stopped to teardown callbacks before
restarting through this. The owning fix belongs in ZmEngine.

Queued successful transitions now publish Stopping/Starting under the existing
lock before callbacks run. Startup callbacks finish before dispatching the
already-reserved stop through wake; stop callbacks finish before spawning the
already-reserved restart hook. Those callbacks see nonterminal state and cannot
legally finalize the still-required owner. The successful inline spawn path has
no subsequent owner access, retaining the earlier inline-release repair.
Control-thread polling remains a single-control-thread contract, now documented;
public completion callbacks may reenter start/stop. No module finalOK flag,
owner reference, new thread, lock, engine member or lifecycle allocation is added.

Failed startup with a queued stop has already rolled back. It publishes Stopped
and moves both completion queues to local storage before unlocking. Startup
failure callbacks may release the owner; subsequent stop acknowledgements use
only the local queue and no owner state. Failed shutdown with a queued start
similarly publishes restored Running and acknowledges queued starts locally.
No artificial recursive started/stopped notification or new runtime hook runs
for a state already achieved by rollback/restoration. This does not promise
owner destruction safety while state remains Starting/Stopping or while callers
still owe independent work/lifetime; final still requires Stopped.

A second demonstrated queue bug was found in the existing latest-request
cancellation transitions. start can cancel StopPending by returning to Starting,
and stop can cancel StartPending by returning to Stopping, but their opposite
callback queues were left waiting indefinitely. Those state transitions are
retained. At current-operation completion, the cancelled queue is moved locally
and receives whether its target state was attained: cancelled stop is false
when startup succeeds and true when it fails/rolls back; cancelled start is
false when stop succeeds and true when it fails/restores Running. Results are
issued only after the actual operation settles, with first-operation callbacks
before opposite notifications and no owner access after terminal callbacks.

Cost: an extra empty local CtrlFnRing header per completion and moving any
already-allocated opposite queue. ZmQueueParams.initial defaults to zero, so the
extra empty queue allocates nothing; queue moves use its ZmNoLock policy under
the existing engine lock. No callback payload is copied. Existing recursive
control replays are removed in the already-settled failure cases. Engine object
layout is unchanged; stack/code-size and runtime measurements remain deferred.
stateChanged no longer observes artificial intermediate states in those cases.

New concrete identified ReleaseEngine regression source checks reentrant stop
from startup completion, a stop queued by restart completion, owner deletion
from terminal callbacks, failed-start release with a pending stop, and failed
stop with queued start plus immediate reentrant terminal destruction. Further
source cases exercise cancelled opposite waiters for both operation outcomes.
Existing deferred/idempotent transition tests remain. Whitespace and added-line
width checks pass. No build/test/Valgrind ran. These are source-level repairs,
not executable acceptance; dependency-wide callback/reentrancy integration,
Hubs operational restoration, remaining shard/capture/budget/occurrence audits
and all deferred runtime/memory gates remain incomplete.


## 2026-10-18 — user correction: best-effort stop always ends Stopped

The user explicitly rejected the earlier restoration requirement and clarified:
stop is inherently best effort; even failure completion must end Stopped. This
supersedes all earlier findings/repair-review notes proposing restoration of
Running or restarting already stopped resources after partial shutdown. The
policy question about retry-vs-restoration is superseded by this clarification.
repair2.md now records best-effort shutdown, attempting all steps despite
individual errors, Stopped completion even for false, and fresh startup only
for a separately queued start. Callback failure is diagnostic, not permission
to leave the lifecycle Running or skip remaining cleanup/drain.

ZmEngine::stopped(false) now reaches Stopped. If StartPending is queued, either
stop outcome reserves Starting and invokes a new startup hook. A cancelled
pending start is acknowledged false once stop finishes, regardless of stop's
diagnostic result. Earlier engine pending-failure/cancellation regression source
was updated for this contract. Hubs clears each hub's started marker after any
completed stop and continues through all registered hubs, collecting failures.
Its regression source now checks deferred failure, continued cleanup, two joined
callbacks, Stopped after false, idempotent repeated stop and subsequent restart.
There is no restoration/reopening phase and no periodic or unbounded retry.

ClientCore no longer skips its owned resolver stop when its pool aggregate
reports an error; combined completion follows both stops. Hubd no longer stops
its parent completion early on DB error. It accumulates DB/service/listener
errors in an Rx-owned stopOK result, continues all drains, then reports the
combined result from Stopped. Listener activation acknowledgements contribute
their actual error. Main cleanup continues finalization and adapter/mx stop
following a false Hubd result. The held Hubd test source now includes failed
deactivation followed by Stopped and final. The aggregate result flag is a
shutdown diagnostic, not a parallel lifecycle state.

Independent timer review changed Hubd browser expiry captures to retain the
existing session-node handle plus raw state instead of a variable cookie string
in the scheduler closure. Timer cancellation clears the stored handle; any
already-queued expiry owns its node through callback execution and is drained
by the existing owner barriers. No owner reference or new allocation is added;
one node reference replaces cookie capture/copy. Browser-principal ingress now
rejects when engine/DB readiness is down, and source coverage rejects stale
cookies after stop/restart. Runtime and memory validation remain deferred.

User now explicitly authorized subagents for the remainder. Work is split into
engine/Hubs review, endpoint shutdown (zjrpc/zmcp/zrest), daemon shutdown
(zum/ztc), and root resolver/HTTP integration plus acceptance coordination.
All agents retain the existing build configuration/artifacts and defer builds,
tests and Valgrind until the implementation milestone. Source whitespace and
new-line width checks passed before delegation. No executable gate ran.

## 2026-10-18 — delegated stop-result and native rollback review (source only)

The engine/Hubs review added regression sources for a failed-stop callback
deleting its stopped engine while another waiter completes, fresh aggregate
startup after a separately queued start and failed stop, and startup rollback
continuing after predecessor stop errors. No new production transition defect
was found in that review. Actual `stopped(false)` always retires resources and
publishes Stopped, or reserves a separately queued fresh startup before invoking
callbacks. Superseded pending opposite requests remain distinct: the existing
latest-request cancellation contract can report an unperformed stop as false
after startup reaches Running. Such a callback is not proof of drainage;
Requests consumers are being audited against this distinction. No central
cancellation-policy change is claimed.

Ten HTTP/WebSocket endpoints across zjrpc, zmcp and zrest now continue their
existing cancellation and drain chains after child stop false, forwarding the
combined result only after their actual last drain. Six HTTP implementations
carry fixed result metadata through existing continuations. Four WebSocket
owners retain one diagnostic bool beside independent transport/link drain
facts. No allocations, locks, atomics or posts were added by this endpoint
result propagation. Module READMEs now require completed stop before final,
including when its result is false. The zrest streaming integration fixture
also stops the server after a client stop error instead of short-circuiting.

PoolImpl likewise retains the child hub result through its existing Tx/Rx
barriers and reports it after complete pool drainage. Assertions on its private
always-successful queue-retirement helpers remain internal integrity checks;
no request/protocol error is reclassified as shutdown failure. ClientCore
already combines pool and owned-resolver stop results without skipping either.

The dependency audit demonstrated a separate startup rollback defect in
ZiMultiplex::start__: native epoll/eventfd/IOCP setup could fail after scheduler
workers had started, returning false without joining those workers. Each native
failure path now closes partial handles as before and performs qualified base
scheduler shutdown before failure notification/completion. Native error values
are captured before rollback can overwrite them. No new state, allocation,
reference or scheduler post is introduced. ZiMxLoopTest has a Linux-only child
process regression source that lowers its own soft descriptor limit, verifies
worker-final counts before failed-start completion/destruction, restores the
limit and checks successful fresh startup/shutdown. Its alarm only bounds a
failed isolated test; lifecycle synchronization uses the synchronous main-thread
engine boundary. This regression has not run.

Resolver scheduler objects now allocate through concrete first-base
ZiResolver_Sched/ZmNHeap with ID ZiResolver.Sched. Both initial configuration and
restart use this type; deletion through the virtual ZmScheduler destructor
selects the concrete deallocation path. It adds no data members or additional
allocation. Existing numeric resolve semantics are preserved: numeric resolution
initializes resolver configuration but need not start its runtime, as required
by ZiResolverTest. ZiIP parses numeric addresses locally, so ordinary numeric
listen/bind setup does not reserve the global resolver as a side effect.

The H3 client stop scan previously counted only newly selected closes and
repeatedly traversed every already-stopping registry entry. Shutdown close now
retires its indexed entry before abort; all retirements during stop acquire one
pending credit centrally in removeLink. Existing retired ownership persists
until endpoint/QPACK/Rx/Tx completion and linkDown releases that credit. Each
collector handles at most ClientWorkBatch entries, ends its iterator before
calling closes, and never revisits an already-retired entry. stopPublishing
prevents inline linkDown from completing the aggregate inside its close loop.
No new allocation, field, ref operation or post is required; redundant active
registry down/stopping metadata was removed. Earlier generation-map removal
makes late failure dispatch a harmless miss for a connection already being
closed. Independent agent source review found no new counting/reentrancy defect;
existing H3 lifecycle tests still must run.

The database/store dependency review found no production engine stateChanged
override and no inspected DB/protocol test expecting Running after an actual
failed stop completion. Out-of-scope database code was not modified. Scheduler
worker/control lifetime review confirms that migrated owners dispose fresh
runtime schedulers from their control completion after workers have joined;
generic externally destroyed scheduler predecessor chains still require their
documented joins and are not silently claimed wait-free.

All changes in this section are source-only. The current clang GNU++2b debug
configuration and build artifacts remain unchanged. Implementation completion,
remaining role/capture/budget/occurrence reviews, incremental dependent builds,
relevant TAP tests and final Valgrind acceptance are still outstanding.

## 2026-10-18 — final implementation source batch and milestone build begun

### Best-effort daemon shutdown and role retirement

Daemon, Server, OIDCHTTP, PingHTTP and Agent accumulate child stop diagnostics
while continuing every existing teardown barrier; App forwards its scheduler
result after actual runtime disposal. Five owners gain one diagnostic bool
each (padding unmeasured). Daemon adds one owner post after SSF stop so result
updates stay on their shard. No request hot-path allocation/copy is added.

Requests startup now posts to its owner before accessing intrusive admission
state or opening credits. activate/deactivate take canonical boolean CtrlFn;
callers and runtime fixtures wait for activation completion. Main's dbActivate
brackets owner startup with DB-thread active-role/generation checks. Daemon's
required borrowed Requests drain checks authoritative Stopped on callback and
resubmits an opposite request superseded by activation; cancellation alone
cannot release its child credit. Actual completed false stop is never undone.
The queued cancellation case has deterministic ZumMgmtTest source coverage.
The activation owner post and DB return post are required synchronization costs.

Hubd DB down now finishes listener shutdown before clearing routing. The shared
clearRouting helper removes a configured finite node budget per owner turn,
ends iterators before deletion and cancels browser expiry timers. Latest DB role
and old-role retirement are independent facts; up during retirement defers
listener start until old routes are gone. Process stop joins those counted
continuations and prevents reactivation. This adds two role bools and fixed
callback chaining; no new heap type, owner ref, timer, thread or periodic scan.
ZtcHubRoleTest exercises held deactivation, down/up/down/up churn, bounded
partial retirement and activation after old routes disappear. It includes the
actual daemon implementation and is wired into default all and make test.
It is an isolated role-handler regression, not full clustered-election proof.

### Server fanout, shard ownership and ordered response insertion

Native and negotiated TLS H2 physical disconnect now collect streams through
Wire::clearStreams' bounded traversal, then notify at most StreamStopBatch
logical links per Rx turn. Final Tx/Rx barriers span response cancellation and
native-handle retirement before physical release/linkDown. A concrete identified
H2ClearState allocation shares the existing active-link array across collector
continuations; matching heap types avoid an array conversion. There is one
clear-state allocation per physical disconnect, moved existing app refs and a
physical handoff ref per remaining notification turn, plus two final barriers.
Array growth allocation can still move multiple handles; logical work budgets
do not claim hard bounds on allocator latency.

H2 ServerAppLink previously cleared native/id on Tx while Rx still read them.
Rx and Tx now own distinct cache-line-separated native/id snapshots. Initial
publication precedes user connected callbacks; Rx and Tx each retire their own
snapshot after disconnected callbacks. Tx output uses its own snapshot; methods
called from both response-terminal Tx paths and Rx select the current owner's
snapshot. This adds pointer+id storage and alignment padding (unmeasured), one
initial publication post and logical/physical handoff references. No locks or
atomics were introduced.

H1 pending response insertion no longer scans a sorted list. maxRequests was
not a valid bound: active parsing retires before an asynchronous response is
submitted, and queued zero-body responses do not consume retained-byte credit.
ServerRouteQ instead uses the existing unique shadow ZmRBTree, keyed by the
existing route sequence, in the same ResponseRecord allocation. Ready responses
still use the existing FIFO links. The intrinsic record gains three tree-link
words (24 bytes on this target, before any enclosing padding); no wrapper node,
extra response allocation or extra ownership reference is introduced. Pending
lookup/insertion is logarithmic, duplicate sequence rejection and routeNext
ordering are preserved, and cancellation remains bounded by StreamStopBatch.
Existing TCP/TLS stream fixtures reverse-insert 65 pending responses, reject a
duplicate and check ordered retirement across a scheduler yield.

### Retained amber dispositions and occurrence closure

Finding 8 capture families were reviewed individually. Public response handoffs
retain logical owners because an external ChannelRef can survive native
retirement. Client H2 clear/disconnect, admit/goaway, pool operation and H3
endpoint/ready/migration/goaway captures can outlast registry removal; generation
checks alone do not retain objects. Their executing terminal callbacks and late
normal callbacks have no stronger independent final drain than the retained
handoff. Those refs remain intentional. Server H1 removals retain the explicit
appDrained/native Rx/Tx proof already recorded above. This does not authorize
mechanical removal of any remaining ZmRef capture.

Finding 22's separate StreamTaskQ node remains an explained amber cost. Drivers
are distinct concrete first-heap types behind type-erased stop/release callbacks;
their queue nodes transfer one intrusive reference and unlink exactly once.
Embedding a common node would change that concrete allocation/ownership design,
rather than demonstrate a local allocation removal without a weak base. Current
identified node allocation and driver ownership are preserved; no performance
benefit of a redesign is claimed. The optional experiment is not required for
this repair's acceptance.

Finding 23 capacities retain their values as explicit configurable admission
policy, not protocol or measured workload optima. maxPending=1024 is per native
connection and does not eagerly allocate entries. maxQueuedFrames=4096 applies
independently to ordinary frames, controls and deferred peer changes, not one
combined 4096-entry total or retained-byte limit. Payload memory depends on
actual pooled buffer capacities and metadata. Codec slot bounds derive from
the protocol's minimum 32-byte entry charge, C/32 plus allocator/hash rounding.
Lazy compaction's half-dead rule charges survivor moves against evictions;
minimum64 remains an unmeasured batching heuristic. Tx hash rebuild additionally
clears every allocated hash slot, so total work is not claimed amortized solely
against evictions. Dead owning strings can remain in bounded spare slots until
compaction/final; protocol usedBytes is not heap residency. Immediate release
needs alias proofs for borrowed insert/duplicate spans and was not invented to
clear a flag. Source comments record these costs and retained tuning/residency
ambers. No size, RSS, throughput or latency measurement is claimed.

Final added-line review checked direct includes/new header build registration,
concrete allocation/deletion, prohibited capture/enum/concept patterns and
expanded 98-column source width. A parser test name was shortened to the
28-byte limit; three new client-stream fixture layout/capture issues were fixed.
Build acceptance has now begun with the existing configuration and incremental
make -j8. The first pass found an unsigned test-node initializer narrowing in
ZmTest; it was changed to 0U. A collecting make -k -j8 pass is running. No tests
or Valgrind have run yet. Compile completion, runtime behavior and memory gates
remain required; none are inferred from source review.

### Source-review corrections during milestone build

Four H2 clearStreams calls (native/pooled client and server) copied and moved
the same local state handle in separate function arguments. Argument evaluation
order did not guarantee the collector copied before the completion moved it.
Each collector is now constructed first, then moved alongside the independent
completion capture. Intended allocation/reference/post costs are unchanged.

App's connection index was redundant and unsafe: different multiplexes mutated
its no-lock tree under different connection-watch locks, while snapshot's reserve
hint used another manager lock. Actual samples already came from each
multiplex's allCxns traversal. The redundant index and connection watcher
registration/callbacks were removed, along with that reserve hint. Snapshot
data still comes from allCxns, with growable destination storage. This removes
one index-node allocation per connection and associated connection-watch work;
no new lock, owner reference or post is introduced. Global manager-watch
unregistration ordering remains intact.

Agent delPub/fence now end their hash iterator scope before unlinkLeg/delReq
can destroy the current node. No iterator survives mutation, and no new
allocation/state is required. Normal routing/sampling/fanout traversals remain
workload-dependent; this source review does not claim a constant-time rewrite
of normal application algorithms or a measured latency result. No additional
stop-specific undrained graph was demonstrated by that review.

Earlier daemon/provider RNG race claims are qualified: Ztls::Random is stateless
and forwards to the backend's thread-safe RAND_bytes, unlike mutable ZmRandom
used by H3 loss filters. Redundant m_rxRng/m_cookieRng wrappers and duplicate
initialization were removed, including Daemon's unnecessary aligned empty RNG
group. The corrected original-correlation reply behavior remains. Existing
graph credits protect borrowed wrapper lifetime; separate mutable ZmRandom
filter instances remain required. This correction supersedes earlier claims
that separate Ztls::Random wrappers fixed a shared-generator race.

## 2026-10-18 — milestone compile diagnostics (not acceptance)

The collecting current-config make -k -j8 pass reached the dependent modules.
It found eight incorrect Server_ qualifiers on the new H2ClearState/budget
declarations, which were corrected to their actual enclosing Zhttp namespace.
Three complete HTTP clients and Daemon now explicitly select their parent
Engine spawn/wake/stateChanged hooks when another inherited engine also supplies
defaults. These using declarations select actual CRTP hooks and add no runtime
forwarding or state. MCP IOClient similarly selects Common::send so batch sends
obey their peer gate instead of ambiguity with native endpoint send. One HTTP
probe uses an explicit this->send_ member reference for the lambda capture.

A distinct common cause affected RPC mixins' application impl() accessors:
ZmEngine's private same-named implementation helper entered inherited lookup.
Its two private accessors and internal calls are now named engineImpl_. No
public API, object layout, transition or callback behavior changed. This avoids
dozens of per-endpoint accessor-selection workarounds.

Const Work references then exposed a foundational ZmRef detector gap. The
existing forwarding converting constructor already copies lvalue handles,
including const wrappers, but its ADL type probe only recognized ZmRef*, not
const ZmRef*. A matching const-pointer friend declaration now preserves the
same pointed-to type during detection. This has zero runtime/layout cost and
avoids extra dispatch copies or repeated raw-pointer erasure. ZmTest adds a
const-derived-handle to base-handle conversion case on its existing Y object,
checking that the source is not consumed. No new allocation is added by that
case. The missed roleDelete_ definition was also propagated to its Work contract;
RoleDelete_/RoleScan_ retain the same request credit through nested scanning and
saga helper destruction, without another credit allocation.

Two HTTP lifecycle integration cleanup paths now attempt both client and server
stop before combining results, removing remaining stop short circuits.

Real cluster fixture review found obsolete standby HTTP expectations. Current
Zdb role policy requires process/replication/telemetry runtime on standby and
application HTTP listeners only on the active node. zumcluster.py and
zumdztccluster.py now verify independent standby readiness and actual HTTP
connection refusal. The latter checks same-process demotion listener closure
and subsequent promotion/rebinding; a bounded external socket probe waits for
the externally observed closed state rather than treating DB-passive telemetry
as completed listener drainage. Current README/MANAGEMENT claims are corrected;
IMPLEMENTATION retains historical evidence with an explicit current override.
Current SQLite and local crypto prerequisites are available, so these real
cluster gates are planned in addition to isolated handler tests. Neither has
executed yet.

The collecting build still contains earlier failures and two earlier warnings;
it is not a warning-free acceptance result. No tests or Valgrind have run. A
stable incremental pass must refresh affected libraries/dependents after this
diagnostic repair batch; touched foundational headers will be refreshed after
collection to avoid any ambiguity from concurrent source timestamps. The
existing compiler/configuration/artifacts remain in use throughout.

## 2026-10-18 — validation milestone in progress

The stable incremental current-configuration build is running. Foundation TAP
validation passed: ZmTest, ZmEngineTest, ZmLHTest and ZmSchedTest, four files and
53 checks. This includes const-reference and const-rvalue derived-to-base ZmRef
conversion: const handles are copied, retaining their source; existing mutable
move paths are unchanged. The detector adds no object-layout or runtime cost.

The I/O group completed with one failure: ZiResolverTest, ZiEventLoopTest and
ZiRxStreamTest passed, but ZiMxLoopTest's new native-start failure child reached
its 20-second alarm. Its other six subtests passed. The fault case requires a
focused stack diagnosis before acceptance; no memory-corruption conclusion is
supported by a timeout. Logs are /tmp/z-repair-test-foundation.log and
/tmp/z-repair-test-io.log. No Valgrind gate has run yet.

The targeted native-failure diagnosis found that all multiplexer workers had
already exited. Lazy diagnostic logger initialization under the injected
RLIMIT_NOFILE=0 threw while creating its own ring, preventing the fault case's
start completion. The fixture now starts its logger before lowering the child
limit, isolating injection to native multiplexer setup. Its worker-retirement
and successful fresh-start assertions remain. Target-only incremental rebuild
was warning-free; ZiMxLoopTest rerun passed all seven subtests. Logs:
/tmp/z-repair-mx-test-rebuild.log and /tmp/z-repair-test-mx-retry.log. This proves
native rollback with an initialized diagnostic logger; it does not establish
logger startup resilience under total descriptor exhaustion. The prior three
passing I/O executables were not rerun.

HTTP's completed default unit-test artifacts were validated while later
independent targets continued compiling. The exact 26 noinst_PROGRAMS wrappers
passed 262 top-level TAP tests using serial prove from zhttp/test; no rebuild
was invoked by that test command. Log: /tmp/z-repair-test-http-unit.log. This is
functional evidence, not the pending final memory gate. An invalid-content-length
error in ClientPool is expected malformed-input coverage; the TAP suite passed.

HTTP integration validation completed with two passing executables
(zhttplifecycletest and zhttpclientstreamtest) and one failing server-streaming
case: zhttpserverstreamtest subtest 15 of 38, terminal queue refusal after
rendering. Combined group: three files, 69 top-level tests, exit failure.
Producer-start/closure and transport-failure accounting diagnostics accompany
that failure; they are not separate passing evidence. The endpoint owner is
investigating before further runtime acceptance. Log:
/tmp/z-repair-test-http-integration.log.

Additional completed default targets passed direct serial TAP validation:
WebSocket unit group, five files and 15 tests; REST unit group, two files and
19 tests; REST endpoint integration, one file and four tests. Logs:
/tmp/z-repair-test-ws-unit.log, /tmp/z-repair-test-rest-unit.log,
/tmp/z-repair-test-rest-integration.log. No make command was invoked by these
runs against targets still being compiled.

The HTTP server-streaming failure was isolated by targeted GDB scheduling:
a one-byte request returns connection and stream receive credit, and with a
one-frame independent control limit the second WINDOW_UPDATE can be rejected
before the response producer starts. That valid control-pressure failure did
not exercise the fixture's intended terminal response-DATA refusal. QueueTrailers
now sends an empty request, preserving its one-frame response admission limit,
128-byte response stream window, and terminal-refusal assertions for H2 and H3.
No production capacity changed. The sole affected serverstream target rebuilt
warning-free and its full 38-case executable passed in two seconds; the two
previously passing HTTP integrations were not rerun. Logs:
/tmp/z-repair-build-serverstream.log and
/tmp/z-repair-test-serverstream-retry.log. This completes the recorded HTTP
functional gate across the original passing runs and the targeted correction.

RPC units passed four files and 32 tests. RPC integration's stdio and HTTP
executables passed; jrpcwstest failed top-level subtest 2 of 5. The group log is
/tmp/z-repair-test-rpc-integration.log. This new failure remains under diagnosis;
there is no claim of complete endpoint acceptance yet.

All four delegated endpoint unit groups passed once: WebSocket five files/15
tests, RPC four/32, MCP five/28, REST two/19 (16 files/94 total). MCP HTTP,
stdio and WebSocket integrations also passed, three files/20 tests. Logs:
/tmp/z-repair-test-{ws,rpc,mcp,rest}-unit.log and
/tmp/z-repair-test-mcp-integration.log. The RPC WebSocket intermittent failure
was reproduced separately: its final failure-counter assertion alone fails,
after calls, cancellation, restart and repeated stop checks pass. That callback
is under targeted diagnosis; passing stdio/HTTP integrations were not repeated.

RPC WebSocket's intermittent final-counter failure was traced under GDB to
AbnormalClose arriving after the second successful RPC while client.stop was
already in progress. Transport shutdown does not guarantee completion of the
WebSocket close handshake. Zjrpc::WSClient::error now snapshots the authoritative
engine's stopping state at that callback and does not set the application-failed
flag for this stop-induced AbnormalClose. The diagnostic callback and clearing
up remain; errors while Running, other error kinds, and prior failure state are
preserved. The exact analogous MCP WS callback receives the same correction.
The existing owner post carries one fixed bool; no additional post, allocation,
reference or lifetime extension is introduced. Dependency-order incremental
refresh of both affected modules and focused WS verification are in progress.

Both affected RPC/MCP modules completed their dependency-order all-target
incremental refresh with exit 0 and no warning/error diagnostics. The two
WebSocket integration executables then passed 18 top-level tests. Ten bounded
serial reproductions of only jrpcwstest also passed. These repeated runs support
the regression fix; they do not prove every scheduling interleaving. The source
argument is the stopping-state snapshot before the existing owner post, with
Running errors and diagnostics retained. Logs:
/tmp/z-repair-build-rpc-ws-fix.log, /tmp/z-repair-build-mcp-ws-fix.log,
/tmp/z-repair-test-endpoint-ws-fix.log, and
/tmp/z-repair-test-rpc-ws-fixed-1.log through -10.log. Other previously passing
endpoint functional groups were not repeated. Daemon and final memory gates
remain pending.

Zum default unit validation completed: 27 executables passed; the new
ZumDaemonTest failed both init scopes (28 files/55 top-level tests overall).
Diagnosis reached HTTP backend setup, then its documented native Ztcp::Hub
precondition: the borrowed multiplexer must already be running. The fixture had
incorrectly required cold daemon init with a stopped external runtime. The
contract requires the component's own engine to remain Stopped, while main
starts that multiplexer independently. A fixture-only correction is underway,
retaining no-init final before multiplexer start and finalizing the stopped
daemon before stopping its borrowed runtime. No native contract is relaxed.

Ztc default unit validation completed: eight executables passed, including the
held role-retirement test; ZtcAgentTest subtest 1 of 3 failed (nine files/41 tests
overall). Its exact failed invariant is being diagnosed independently. Logs:
/tmp/z-repair-test-zum-unit.log and /tmp/z-repair-test-ztc-unit.log. Passing
executables are not being rerun during these targeted repairs.

The stable top-level incremental make -j8 all completed successfully with no
warning/error diagnostics in /tmp/z-repair-final-build3.log. It used the existing
clang GNU++2b x86-64-v2 -g -DZDEBUG configuration and reused artifacts throughout;
no clean, reconfigure or alternate build was performed. Two later RPC/MCP header
fixes were refreshed in their modules while the top build progressed downstream;
a final incremental consistency pass remains planned after targeted fixture
rebuilds finish.

ZtcAgentTest's failing init scope independently had the same external-runtime
fixture mistake: Ztls::Hub requires the borrowed mx already Running. The fixture
now starts it before valid PingHTTP init while retaining the facade's Stopped
state, stopped-resolver failure and callback finalization cases. Its sole target
rebuilt warning-free and all three subtests passed; the previously passing eight
Ztc binaries were not rerun. Logs: /tmp/z-repair-ztc-agent-rebuild.log and
/tmp/z-repair-test-ztc-agent-retry.log. No production lifecycle contract changed.

ZumDaemonTest's target-only rebuild completed warning-free, and its two scopes
passed after correction. The no-init final still runs before mx startup; valid
init/final and provider-success/HTTP-port-zero partial init use an independently
Running borrowed multiplexer, while Daemon remains Stopped and no listener or DB
runtime is started. Final precedes borrowed-runtime stop. The test README now
states this actual prerequisite. Logs: /tmp/z-repair-build-zum-daemon-fixture.log
and /tmp/z-repair-test-zum-daemon-retry.log. The other 27 passing binaries were
not rerun. Across the recorded original passes and targeted corrections, all
Zum/Ztc default unit executables now pass. Real cluster gates have started;
a final incremental top-level consistency pass is running before memory checks.

The final top-level incremental consistency pass completed exit 0 without
warning/error diagnostics: /tmp/z-repair-final-build4.log. This refresh follows
all targeted source/header repairs; the current configuration remains unchanged.

The real publisher role gate passed (zumdztccluster.py, one file/one top-level
test, 17 seconds). It ran three real processes with SQLite/native Zdb role
transitions, passive process telemetry, actual HTTP listener closure after
demotion, unchanged publisher PID registration, and listener rebind/readiness
following re-promotion. Log: /tmp/z-repair-role-publisher.log. This supplies
external runtime evidence for main's independent multiplexer startup and DB-only
listener activation, beyond the held role-handler unit fixture. Enrollment/
failover and hot/warm WSS gates are running next; final memory checks remain open.

Real IAM enrollment/failover passed with separate fresh SQLite leader/follower
stores: one file/one top-level test, /tmp/z-repair-role-iam.log. The fixture
verified passive native replication while standby HTTP refused connections,
WebAuthn enrollment/replication and survivor login.

The first hot/warm WSS gate stopped before TAP subtests on an obsolete fixture
expectation: it waited for standby `ztchub ready` before promotion. The native
DB logs show that process joined as follower; the application readiness marker
correctly appeared only after cleanup stopped the hot peer and it promoted.
The fixture is being corrected to observe independent standby replication
runtime and refused application ports, then await actual listener readiness
following deliberate leader stop. Evidence: /tmp/z-repair-role-wss.log and
retained logs/ztchubtest.mltzqsn6. No production activation behavior is changed.

The corrected hot/warm WSS run passed standby replication/no-listener checks,
actual warm promotion, fresh front/agent telemetry and preserved IAM roles,
then timed out in the controlled publisher section. Isolated all-thread GDB
stacks demonstrated a foundational bootstrap recursion before `publisher ready`:
App.start had completed; constructing BaseObject allocated a configured heap,
and ZmHeapMgr_ held m_lock while constructing ZmHeapLookup. Its newly managed
LHash backing allocation reentered that same manager lock. Evidence:
/tmp/z-repair-publisher-mainstack.log and /tmp/z-repair-publisher-stack.log.

The smallest repair is private to ZmHeap.cc: HeapLookup's hash receives a private
allocator identity with an Ops specialization that uses aligned platform storage
for its growable backing table. Heap-manager bookkeeping must bootstrap without
allocating through itself, matching the existing manager-container null-heap
exception. Ordinary LHash tables retain their identified managed backing and
unchanged layout/API. No new lock, owner reference, fixed capacity, prewarming
requirement or application workaround is introduced; backing destruction uses
the matching aligned free. The library rebuilt warning-free; its dedicated heap
regression and controlled-publisher reproduction are next before WSS retry.

The heap-bootstrap library and dedicated ZmHeapTest target rebuilt without
warnings. ZmHeapTest passed its single outer TAP scope and 13 nested regression
scopes, covering lookup publication, bounds, activation, ranges, rebuild,
receivers/groups, transitions and cleanup. Log:
/tmp/z-repair-test-heap-bootstrap.log. The isolated controlled publisher then
printed readiness immediately and exited 0 on shutdown, with owned residue
cleaned. The full WSS fixture is retrying; a final dependency refresh is relinking
current artifacts after the implementation-only libZm repair. HeapTest joins
the upcoming single final memory pass; no Valgrind has run before this gate.

The full corrected hot/warm WSS fixture now passed with fresh isolated stores
and cleaned successful residue: one file/one top-level test, 58 seconds,
/tmp/z-repair-role-wss-final.log. It exercised passive runtime with refused
application ports, actual hot shutdown/warm promotion, fresh WSS sessions,
controlled publisher and real collector integration, and the remaining default
routing scenario. Together with the earlier publisher and IAM passes, all three
real current-config role gates have passed. The first two gates were not rerun
after the isolated WSS/heap repair. Final dependency relinking and the memory
pass remain open; no claim of Valgrind acceptance is made yet.

Independent source review of the bootstrap repair found no demonstrated ODR,
alignment or lifetime defect. The private tag/specialization precedes its hash
instantiation; init/resize/final consistently use paired raw aligned backing,
with element construction/destruction unchanged. The incomplete tag is never
instantiated as data. HeapTest includes ZmHeap.cc once without linking libZm,
so its internal test instance does not introduce mixed definitions. Review:
/tmp/z-heap-bootstrap-review.md. This source review complements, and does not
replace, the completed heap/publisher/WSS runtime evidence or pending memory gate.

The final dependent incremental refresh after the heap-bootstrap implementation
repair completed successfully without compiler warnings/errors:
/tmp/z-repair-final-build5.log. All functional and real role gates have passed
across their recorded runs. The single serial focused Valgrind gate has now
started through libtool exec over foundation, HTTP, seven complete endpoints,
service/provider and Ztc ownership graphs; HeapTest is included for the newly
exposed bootstrap path. No earlier Valgrind run was used during implementation.
Its TAP log is /tmp/z-repair-test-final-memory.log and per-process Memcheck logs
are /tmp/z-repair-final-memcheck.<pid>.log. Results remain pending.

The first final memory pass is still collecting results and is not an accepted
memory gate. Completed foundation runs have zero Memcheck errors and zero
selected leak kinds. H2Hub has no memory errors/leaks, but its TLS fixture races
explicit GOAWAY against maxStreamID=1 automatic native exhaustion. The prepared
fixture change uses maxStreamID=3, observes explicit GOAWAY while the first native
connection remains live, then deliberately exhausts the replacement with two
requests before checking rotation. Physical admissions remain four; one extra
logical pair changes expected logical disconnects to nine.

H3Hub's seven TAP cases pass but Memcheck reports uninitialized-condition errors
through libcrypto EVP_DecryptFinal_ex. Source inspection has not established an
input cause; a focused origins run is required after this collecting pass, with
no suppression or attribution to acceleration assumed.

Client/server streaming also expose definite leaks of debug reference nodes and
backtraces. Source proof identifies the owned Context to ZmRef adoption gap in
stateless ZmFn::mvFn and ZmContext::mvObject: the reference count transfers, but
the recorded context referrer does not. The planned foundational correction
transfers that debug referrer through a dedicated acquire overload without
changing reference counts or existing untracked acquire semantics. These are
actual leaked diagnostic resources, not attributed to reachable heap caches.

Under instrumentation the H2 terminal-pressure fixture also exposed initial
SETTINGS/ACK saturation with its one-frame control limit. Its prepared fixture
change permits two startup control frames, then explicitly admits two response
DATA frames in one retained Tx turn and rejects terminal third-frame admission.
H3's one-frame case is unchanged. No production queue capacity changes or
instrumentation-only timeouts are introduced. Source repairs are being batched;
artifacts in the running memory pass are unchanged.

The initial final-memory collection completed with failure: 30 files, 278
 top-level tests, 379 seconds, /tmp/z-repair-test-final-memory.log. Its functional
failures were H2Hub subtest 15, serverstream subtest 15 and REST endpoint subtest 4;
other nonzero exits reported Memcheck findings despite passing TAP cases.
Foundation and nine of ten selected Zum/Ztc executables were error-free with
zero definite/indirect/possible leaks. All ten Zum/Ztc records had zero selected
leak kinds; RuntimeRevocation had the same EVP finalization undefined-condition
family. Detailed read-only classifications are in
/tmp/z-foundation-memory-review.md and
/tmp/z-http-endpoint-memcheck-results.md.

The focused H3 origins diagnostic retained the existing binary/configuration,
passed seven TAP cases and reproduced the memory errors. Both undefined values
originate in stack allocations inside libcrypto, not an identified application
allocation: /tmp/z-repair-h3-origins.1593388.log. This narrows investigation; it
is not yet a justification for suppression or external-false-positive claims.
A failed-only verbose REST reproduction is now running to recover its nested
assertion. Prepared source repairs remain unbuilt until that diagnosis completes.

The failed-only verbose REST memory reproduction identified its first failing
nested assertion: `firstDeferred && secondObserved` in call(). After a redirected
response callback, the test reused its full two-request admission budget before
HTTP terminal Tx acknowledgement and final Rx retirement had released the prior
slot. The other REST multiplexed fixture already uses an Rx/Tx/Rx barrier for
this distinction. The fixture correction reuses that deterministic retirement
barrier at budget-dependent phase boundaries; production pending credits remain
held through actual retirement. Log:
/tmp/z-repair-test-rest-memory-diagnostic.log. No timeout increase is introduced.

The reference-adoption source batch is complete: acquire(ptr, previousReferrer)
transfers debug bookkeeping while adopting the existing count; Context mvObject,
stateless and bound callback adoption, and rvalue const/mutable handles use it.
Existing acquire(ptr) for untracked counts is unchanged. ZmFnTest adds a tracked
owner probe that checks remaining referrer identity after these handoffs. Object
layout/count semantics are unchanged. The foundational incremental library
rebuild has started; dependent rebuilding waits for this diagnostic repair batch.

The debug-adoption foundational library build completed warning-free. Its new
ZmFnTest regression initially exposed a TAP helper-scope compile error, corrected
by returning criteria from helpers/callbacks and checking them within the scope.
The target-only retry is warning-free, and ZmFnTest passes 23 top-level checks,
including the new tracked-owner adoption scope. Logs:
/tmp/z-repair-build-ref-adoption.log,
/tmp/z-repair-build-fn-adoption-retry.log and
/tmp/z-repair-test-fn-adoption.log. The coordinated dependent incremental build
has started with all reference/fixture source fixes frozen:
/tmp/z-repair-final-build6.log. Configuration and artifacts remain in use.

Exact-version OpenSSL source shows SHA AVX2 deliberately loading scratch into
upper YMM lanes for the final block; both origins map to those SHA stack frames.
That is useful evidence, but propagation into a meaningful GCM final comparison
is not yet classified. A temporary EVP/SHA-only diagnostic with fully initialized
inputs and Memcheck definedness checks is authorized, compiled with the exact
current generated Makefile CXX/CXXFLAGS. It changes no dependency, CPU capability,
suppression or build configuration. It is a targeted investigation of the actual
reported memory error, not an alternate acceptance matrix.

## 2026-10-18 — demonstrated OpenSSL/Memcheck correlation exception

The isolated diagnostic uses only libcrypto with initialized key, nonce, AAD,
plaintext, ciphertext and expected tag, checking definedness before each API
input. AES128/AES256 valid decrypt and tampered-tag rejection pass, with and
without earlier SHA256/SHA512. Ordinary and extra-precise Memcheck tracking still
report the same provider-final conditional. A second same-runtime vgdb inspection
established the actual dataflow rather than assuming dependency harmlessness:

- Before gcm_ghash_avx, Xi, the complete GHASH table and final input buffer have
  fully defined validity bits. After that callback, Xi is marked undefined.
- The exact installed code copies inherited vector registers into Zlo/Zhi/Zmi,
  then XORs each unchanged copy with its original to produce constant zero.
  Actual zero does not depend on the inherited register bits; Memcheck loses
  the correlation between the distinct copied registers.
- The resulting false undefinedness reaches computed tag verification. Valid
  expected/computed tag bytes match; a deliberately tampered expected tag differs
  in exactly the changed byte and is rejected. The expected tag remains defined.

The exact upstream source documents the zeroing sequence at lines 1377–1392:
[OpenSSL 3.5.5 GHASH assembly](https://github.com/openssl/openssl/blob/openssl-3.5.5/crypto/modes/asm/ghash-x86_64.pl#L1377).
Installed libcrypto build ID is 96e3095eff34eb22a2bcd9edb0e8b230c5dea19c;
Valgrind is 3.26.0. GHASH is ELF 0x3347e0 and the reported provider conditional
is ELF 0x470ecc. All eight original affected executables, including SSF AES256,
map to that same conditional. Raw diagnostic exits remain 99 and are retained.
Evidence: /tmp/z-evp-defined-memcheck.log,
/tmp/z-evp-defined-precise-memcheck.log, /tmp/z-evp-vgdb2-gdb.log and
/tmp/z-openssl-ghash-vbits-proof.md.

The opt-in openssl.valgrind.supp records this reviewed Memcheck:Cond stack only:
two libcrypto frames immediately above EVP_DecryptFinal_ex. It does not suppress
memory access errors, allocation errors, or leaks. The final gate checks the
installed library build ID before enabling this rule; another dependency/tool
version requires revalidation. The exception is not applied by default to tests
or production. No library patch, CPU capability override, build configuration
change, undefined-input annotation, or broad libcrypto suppression is used.
Final results must disclose this known tool/dependency exception and distinguish
it from unsuppressed errors. The real debug-reference leaks are fixed in source,
not suppressed.

Independent review accepts the exception for this reviewed access/leak gate and
confirms it cannot suppress Addr/Free/Leak records. Its explicit limitation is
that Valgrind stack matching cannot constrain validity-bit provenance or the
exact conditional instruction address: another genuine undefined-input
conditional with the same three-frame shape could also match. Therefore report
zero unsuppressed errors with the reviewed GCM conditional exception, not proof
that every possible GCM input is initialized. The standalone proof, all original
context-address mapping, exact build/tool guards and raw failures remain part of
the evidence. No framework memory-access/leak finding is waived by this rule.

After the coordinated rebuild completed the HTTP default unit targets, the
single affected H2Hub functional retry passed all 16 top-level cases:
/tmp/z-repair-test-h2-fixture-retry.log. This validates the explicit GOAWAY and
subsequent deliberate stream-ID exhaustion sequence without changing deadlines
or production limits. Serverstream and REST retries await their current targets;
the full memory rerun still awaits completion of the dependent build.

All three focused functional retries passed after their respective default-all
targets completed: H2Hub 16 top-level cases, serverstream 38, REST endpoint four.
Logs: /tmp/z-repair-test-h2-fixture-retry.log,
/tmp/z-repair-test-serverstream-fixture-retry.log and
/tmp/z-repair-test-rest-retirement-retry.log. Each executable ran once with
serial prove; no producer/barrier timeout or production capacity change was
introduced. The reference-adoption ZmFnTest also passed earlier. The final memory
gate will run in two serial groups as their current artifacts finish: foundation/
HTTP/seven endpoints, then Zum/Ztc. Each current executable runs once in that
corrected gate, with the reviewed version-guarded OpenSSL conditional exception.

The final memory gate's first serial group is running against completed current
foundation/HTTP/endpoint artifacts while independent daemon rebuilding continues.
Fresh Engine, LHash, Heap, Fn and Resolver records all passed with zero errors,
zero definite/indirect/possible leak bytes, and zero suppressions. The new Fn
adoption regression is included. H2Hub's corrected fixture also passed under
Memcheck with zero errors/leaks. H3Hub passed with zero unsuppressed errors and
zero lost bytes, using the documented OpenSSL conditional rule (440 reports in
two matched contexts for that process). This is partial gate evidence; complete
endpoint/debug-leak and daemon records remain pending. Logs:
/tmp/z-repair-test-final-memory2-core.log and
/tmp/z-repair-final2-memcheck.<pid>.log.

The corrected core memory group completed: 21 files/271 top-level TAP checks,
all TAP assertions passing, with one remaining Memcheck failure in MCP HTTP.
All other core records report zero unsuppressed errors and zero definite,
indirect or possible leaks. The formerly leaked debug records are absent in
HTTP client/server streaming, RPC HTTP/WS, MCP WS and REST; corrected H2 and REST
fixtures also pass under instrumentation. The sole remaining report is three
actual RawHTTPCxn fixture objects (1,536 bytes direct) retaining three input
buffers (3,456 bytes indirect), /tmp/z-repair-final2-memcheck.1706515.log.
No reference owner is removed merely to clear this finding: a failed-only vgdb
inspection is authorized to identify their remaining native/fixture credits.
The whole group log is /tmp/z-repair-test-final-memory2-core.log. This group is
not yet an accepted complete memory gate; daemon rebuilding is still running.

## 2026-10-18 — zero-count scheduler invocation leak

Failed-only MCP HTTP vgdb inspection after CRT cleanup found closed, quiescent
RawHTTPCxn objects with reference count zero, no debug tree, socket -1, Rx/Tx
flags down and empty callback contexts. This rules out a positive retained-owner
cycle. A different schedule left two instead of three objects but the same
allocation family. Evidence: /tmp/z-mcphttp-vgdb-blocks.log.

Two independent source reviews found the canonical cause in the specialized
ZmScheduler::invoke(object, lambda, sid) queued path: it adds o->ref(), then calls
l()->deref() without deleting when the last credit reaches zero. Native Tx object
invocation can return after the posted Rx disconnect continuation and main
fixture owner have released all other references. Ignoring deref's true result
then leaks the actual zero-count connection and its input buffer. Removing the
fixture's owner would not repair this scheduler defect.

The queued wrapper now retains the returned concrete pointer and deletes it when
deref() returns true. It preserves default referrer zero on the existing ref/
deref pair, allocation/capture/post counts, direct-owner invocation behavior and
all reference increments/decrements. No owner lifetime extension or fixture
workaround is added. A deterministic destructor regression is being added to
ZmSchedTest: the callback drops its captured user reference, leaving the queued
scheduler credit last; destruction signals the main thread without blocking a
worker. Build6 was deliberately stopped (exit 143, not a compiler diagnostic)
so this foundational header repair can receive a coherent incremental refresh.
The previously completed core gate passes remain recorded, but a current final
memory gate must cover this changed deletion behavior before acceptance.

The scheduler last-credit regression rebuilt warning-free and passed all seven
ZmSchedTest top-level cases, including exactly one callback and one destructor
for invokeLastRef. Logs: /tmp/z-repair-build-sched-release.log,
/tmp/z-repair-build-sched-test.log and
/tmp/z-repair-test-sched-release.log. No worker waits or extra runtime reference
operations were introduced. The coherent incremental build7 is now running:
/tmp/z-repair-final-build7.log. The final current memory gate includes this
scheduler executable, with 22 core graphs followed by ten Zum/Ztc graphs.
Prior passing graphs are rerun here because this canonical fix changes actual
last-owner deletion across their shared object-invocation path; the known MCP
leak must be absent without a fixture ownership change. The same compiler,
flags, platform and artifacts remain in use; no clean/reconfigure occurred.

## 2026-10-18 — current core memory gate accepted

The current core group completed successfully: 22 executables, 278 top-level
TAP checks, exit zero, 261 seconds. All 22 completed parent Memcheck records
have zero unsuppressed errors and zero definitely, indirectly or possibly lost
bytes. Engine, LHash, Heap, Fn, Scheduler and Resolver need no suppression.
The scheduler destructor regression is included. MCP HTTP's actual RawHTTPCxn
and input-buffer leaks are absent after the scheduler fix; fixture ownership
is unchanged. Corrected H2 rotation, server-streaming pressure and REST
retirement fixtures also pass under instrumentation.

The sole matched suppression family is the reviewed OpenSSL GHASH conditional:
2,261 reports across 14 process-local contexts in seven executables. Its
version/build-ID guards and stack-matching limitation remain as documented
above; this result is zero unsuppressed errors, not an exception-free crypto
definedness claim. Raw logs: /tmp/z-repair-final3-memcheck.<pid>.log. Group log:
/tmp/z-repair-test-final-memory3-core.log. The coherent build7 and the freshly
rebuilt Zum/Ztc functional and ten-graph memory group remain pending, so this
does not yet close whole-plan runtime acceptance.

## 2026-10-18 — final acceptance of both repair plans

Both repair plans are complete within the requested current configuration and
the explicitly retained, justified exceptions recorded above. All source was
frozen before the coherent final incremental `make -j8 all`; build7 completed
with exit zero and no compiler warnings or errors. The existing clang GNU++2b,
x86-64-v2, `-g -DZDEBUG` configuration and artifacts were retained. No clean,
reconfigure, alternate compiler/platform/flags, or sanitizer run was introduced.
Build log: /tmp/z-repair-final-build7.log.

| Validation | Accepted evidence |
| --- | --- |
| Current Zum functional group | 28 executables / 55 TAP checks, PASS; /tmp/z-repair-test-zum-final.log |
| Current Ztc functional group | 9 executables / 41 TAP checks, PASS; /tmp/z-repair-test-ztc-final.log |
| Current core memory group | 22 executables / 278 TAP checks, PASS; /tmp/z-repair-test-final-memory3-core.log |
| Current daemon memory group | 10 executables / 30 TAP checks, PASS; /tmp/z-repair-test-final-memory2-daemon.log |
| Earlier HTTP functional milestone | 26 unit executables / 262 checks and three integrations / 69 checks, with corrected fixture retries recorded above |
| Earlier WS/RPC/MCP/REST milestone | 16 unit executables / 94 checks and seven endpoint integrations / 32 checks, with corrected WS and REST retries recorded above |
| Real DB-role publisher cluster | PASS; /tmp/z-repair-role-publisher.log |
| Real IAM leader/follower cluster | PASS; /tmp/z-repair-role-iam.log |
| Real WSS hot/warm promotion | PASS; /tmp/z-repair-role-wss-final.log |

The daemon group log retains its historical `memory2-daemon` filename, but this
accepted run used freshly rebuilt build7 artifacts and writes `final3` per-PID
Memcheck records. It completed once, in 116 seconds. All ten parent records have
zero unsuppressed errors and zero definitely, indirectly or possibly lost bytes.
Only RuntimeRevocation matched the reviewed OpenSSL conditional exception: one
report from one context; the other nine required no suppression.

Together, the current final memory groups cover 32 executables and 308 top-level
TAP checks. Independent review and a combined summary of all 32 completed parent
records agree: zero unsuppressed errors and zero definite/indirect/possible lost
bytes. The only suppression family is the opt-in, version/build-ID-reviewed
OpenSSL GHASH conditional: 2,262 reports across 15 process-local contexts in eight
executables. Its documented stack-matching limitation still applies. This does
not claim exception-free crypto definedness, inspect incomplete assertion-death
child records as clean exits, or classify reachable process caches as leaks.
Raw records: /tmp/z-repair-final3-memcheck.<pid>.log.

Earlier functional and real-cluster results preceded the final scheduler
last-credit repair; the current selected owner graphs were rebuilt and exercised
under Memcheck afterward, including the new scheduler destructor regression,
all seven endpoints, and ten daemon graphs. The actual MCP HTTP native-object
leak is gone without dropping its fixture owner. No additional real-cluster,
third-party conformance, benchmark, or alternate-configuration run is claimed.

The user-directed lifecycle contracts are preserved: actual stop completion is
best effort and leaves Stopped even on failure, without rollback restart; a
separately queued start is fresh startup. Main starts process multiplexers,
while Zdb up/down alone controls active listeners, including asynchronous role
fences and retirement. Final releases already-stopped, drained owners without
initiating or waiting for shutdown. HTTP/2's three shadow scheduling lists use
ZiAssert with clean() as the release-build fallback requested by the user.

Retained Alt-Svc inactive residency, StreamTaskQ allocation, configurable queue
defaults, codec compaction/storage heuristics and required client/retirement
references remain qualified source dispositions, not unresolved acceptance
failures or measured performance claims. The individual findings, lifetime
proofs and costs above remain the authoritative detail. Final textual review:
git diff --check passed; selected new source files and the opt-in suppression
file have no trailing whitespace or expanded lines over 98 columns. No further
required implementation or validation work remains for these two plans.

## 2026-10-18 — repair3 execution and acceptance reset

The follow-up review demonstrated defects and costs not established by the
earlier runtime gates. Those passing results remain historical evidence; they
do not prove release-path assertion semantics, fluent API integrity, minimal
storage, performance neutrality or absence of shutdown polling. The active
repair3 implementation must receive its own coherent validation milestone.

Current source work, not executable acceptance:

- R1: Alt-Svc now selects the eviction victim outside the debug assertion,
  then checks the nonempty-table invariant and deletes within the iterator
  scope before admission. The existing cache fixture covers repeated bounded
  admission and zero capacity. The local numeric converter now delegates to
  ZuBoxed scanning, retaining only necessary leading-zero normalization and
  the uint32 significant-digit/range boundary because framework scanning is
  deliberately unchecked for overflow. There is no scratch allocation/copy
  or duplicate digit-validation pass. Overflow and leading-zero regressions
  were added to the existing cursor scope.
- R2: H2Config privately contains its TLS base implementation and exposes only
  tls() and tls(lambda) for TLS configuration. The five inherited setter/getter
  using declarations are deleted. Native H1 backend calls pass TLSConfig;
  H2/shared backends retain H2Config. Three H1-only WebSocket callers now use
  TLSConfig directly. The existing configuration fixture checks the outer type,
  private conversion boundary and propagation of both TLS and H2 settings.
  No forwarding setter family, conversion operator or extra data is added.
- R3: sized allocation/free reuse the canonical VHeap cache/jumbo dispatch;
  LHash supplies its already-known slot count, including the old count during
  resize. The redundant in-band prefix is absent on this path. A demonstrated
  4096-byte payload selects 4096 rather than 8192 bytes. Independent review
  caught an initial global minimum change that would shift legacy VShift
  tuning; that draft was corrected before any build. Original minimum/shift
  mapping, unsized prefix APIs and the private bootstrap allocator remain.
  Existing hash tests now check actual cache counters and alignment/boundaries.
- R4: pending-tree membership ends before ready-FIFO membership begins, but
  the existing ZmList field and ZmRBTree method/base contracts do not support
  a local overlay of their inherited node bases. Type-erased ResponseRecord
  is allocated before transport selection. A protocol-wide record hierarchy,
  wrapper allocation, bespoke queue or O(log n) ready FIFO would broaden or
  worsen the repair. The existing links remain provisionally retained; actual
  concrete size/class evidence is deferred to the validation milestone.
- R5: auth-route and maintenance credits are co-located with their existing
  concrete helpers; their last-destroyed guard returns the owner credit after
  all helper members and descendants retire. This deletes a separate
  DaemonWork allocation in each family. Heterogeneous daemon-admin/provider
  graphs retain their distinct credit: cancellation or first application
  result does not cover later DB/saga/signing descendants. Normal open-credit
  return uses CAS without an owner post; a closed-credit return stays counted
  through its owner handoff. No owner access follows a successful open return.
  Held-helper lifetime and compile/runtime validation remain to be checked.
- R6: shutdown polling is prohibited without exception. The source audit
  found more than REST's visible readiness loop: Requests deactivation, generic
  RPC ingress/send-credit close, MCP ingress close, active H2 header retirement
  and logger overflow shutdown also required repair. Implementations are being
  converted to actual completion events or bounded destructive/output work.
  No completion claim follows until all these paths and their registration
  races are reviewed and exercised.
- R7: the public cache comment now states the actual lookup expiry, bounded
  origin residency and reclamation events, including cache destruction.
  Pool final does not independently purge this cache; re-init replaces it and
  pool destruction releases it. Inactive expiry remains the separately named
  policy exception, not a claim of immediate reclamation or user approval.
  No timer, expiry index, scan or new ownership mechanism is added.

The current compiler/flags/platform/artifacts are retained. No build, executable
test or Valgrind run has occurred in this source batch. Rebuilding dependents
remains deferred until implementation completion. Source inspection follows
the per-finding GUIDELINES references in repair3.md; further compiler, runtime,
memory and concrete-layout evidence is still required.

### repair3 source milestone — shutdown completion and retained costs

The source batch is frozen and the coherent incremental `make -j8 all` is
running in the existing configuration. Executable acceptance is still pending.
Independent source review found no remaining readiness polling in the affected
shutdown families inventoried below; passing runtime checks will not substitute
for these progress and lifetime arguments.

- REST: actual Work retirement and HTTP completion notify `stopped_`; the
  consumed `ZuUnion<void,bool>` preserves the child result through final Tx/Rx
  barriers. The former global SSE-post counter is deleted. Producer close
  publishes its off-Tx post under the existing admission lock, closing the
  claim-before-publication gap; accepted work precedes final native barriers.
- REST cancellation: one cursor traverses the existing owner list. Every
  inspected node (including nonmatching/cancelled nodes) and queued session
  event charges the configured batch. Retirement advances the cursor before
  detach or application callbacks. An owner-admission ordinal and event cutoff
  exclude later admissions, including reused opaque session addresses after
  owner delivery. This replaces the provisional tail marker; no detached tail
  reference or per-session coordinator remains. Stop closes owner admission
  before superseding session scans, rather than using the earlier foreign
  Engine transition. The counter assumes fewer than 2^64 admissions in one
  activation; it does not strengthen preexisting Session identity before owner
  event delivery. The identified event queue has workload-dependent backing,
  not a new hard residency/latency guarantee. Work's fields are private trailing
  class data; its request union remains in the original layout order.
- RPC/MCP: shared BatchCaller seals admission in its credit word, registers
  one owner callback and notifies on actual final return. Registration rechecks
  zero, and the callback remains a prerequisite through publication of the
  notification, preventing both lost wakeup and premature owner release. The
  queued-only `continue_` contract orders notification after admitted bodies;
  every current implementation was checked. Ordinary returns add no post.
  Reposts retained by pending-call/dispatcher close consume actual bounded work.
- Requests: a fixed initial owner barrier arms draining after cancellation;
  actual final retirement notifies completion. Clear the phase before the
  completion callback, which may delete the owner. No readiness self-post.
- H2: active-header retirement performs one existing bounded frame drain before
  yielding removal. Complete header-chain admission and flow-independent
  CONTINUATION scheduling guarantee real output; rejection enters terminal
  cleanup. The existing posted-drain flag clears only in its queued wrapper.
- Logger: an overflow FIFO sentinel signals a stack semaphore; main then uses
  the existing EOF/join. No yield loop, retry wake or new per-log branch remains.
  The semaphore survives through worker join. New test handshakes model a
  blocking dedicated log sink; production scheduler shards do not block.

These changes apply GUIDELINES.md's Repair procedure, Liveness, Algorithm and
control flow, Callbacks, I/O sharding/Lifetime/Teardown, Storage and lifetime,
Containers and intrusion, and Class and struct shape. Scratch arrays formerly
used for REST cancellation are deleted; no scratch escapes callback scope,
new framework substitute, protocol-wide session ABI or compatibility shim is
introduced. Heterogeneous graph credits and pooled I/O ownership remain where
actual descendant lifetime requires them.

Read-only DWARF evidence from the prior configured artifacts (no inferior run)
measures ResponseRecord 88 bytes/alignment 8, comprising MessageRecord 16, list
links 16 and unique-tree links 24 before its other fields. The representative
contract response's actual node is 128 bytes/alignment 8, using a 128-byte fixed
heap class. Removing 24 tree bytes or overlaying list/tree links to save 16
logical bytes would still select 128 for this concrete node; these alternative
sizes are source-derived, not separately compiled layouts. Pending/ready
membership is disjoint, but canonical list fields and tree node operations do
not support a local inherited-base union. A new adapter/type-erased hierarchy,
wrapper allocation or logarithmic ready queue would broaden or worsen this
repair. R4 therefore retains the common 24-byte cost with this specific
justification, without a footprint repair or optimality claim for other
application builders. Fresh-artifact measurements remain pending.

R5 removes one separate DaemonWork allocation for each accepted authRoute and
maintenance operation by using their existing concrete helper and last-destroyed
ZuGuard. Other graph families retain their independent credit because first
result or request retirement does not cover DB/saga/signing descendants. Open
credit return now performs load/CAS without the prior owner post; Closed return
keeps its credit counted through publication of the owner handoff. Source CAS
contention retries update one atomic word; they do not wait for shutdown work.
Inline guard storage, changed endpoint layouts and actual heap-class effects
will be recorded from fresh artifacts. No latency/throughput claim follows.

Reports and raw baseline evidence: `/tmp/repair3-r3.md`,
`/tmp/repair3-r4.md`, `/tmp/repair3-r5.md`, `/tmp/repair3-r6.md`,
`/tmp/repair3-http-shutdown.md`, `/tmp/repair3-old-layout.md` and their linked
GDB logs. They are source/baseline evidence, not current executable acceptance.

### repair3 build, functional acceptance and measured storage

The coherent current-configuration incremental `make -j8 all` completed with
exit 0 in `/tmp/repair3-build-resume6.log`, with no warnings or errors on that
final pass. No clean, reconfiguration, compiler/flag/platform change, alternate
build directory or sanitizer pass occurred. Completed artifacts were reused
through compiler-guided incremental resumes. Preserve the original build log
and resume1 through resume5: they expose the corrected queue null protocol,
fixture private-base alias/parser mismatch, direct ZmFn scheduler submissions,
invalid ZuGuard include, and one missed H1-only load-test TLS argument. The
provisional fixture-only callback queue specialization was deleted after the
actual scheduler contract was established.

Final compiler follow-ups:

- CancelSession provides the boolean/null protocol required by ZmQueue; this
  is container integration, not another input validation pass.
- The REST regression selects the actual public HTTP base, including its real
  parser wrapper, for its independent child-stop prerequisite. No production
  API or private-access exception was added.
- ZmRingFn explicitly rejects ZmAnyFn-derived delegates. All six affected
  RPC/MCP work continuations now submit plain fixed-owner lambdas; the bound
  ReadyFn remains only for retained final-credit completion. There is no
  nested delegate wrapper or new explicit allocation on those continuations;
  existing scheduler overflow can still allocate. The fixture mirrors this
  same contract with its original queue type.
- ZuGuard is in ZuLib.hh, already available through the component dependencies;
  the nonexistent ZuGuard.hh include was removed.
- Four H1-only callers use TLSConfig: zdash.cc, ztchubwiretest.cc,
  ztchubloadtest.cc and ztchub_client.cc. The new load-test declaration configures
  its named object directly, avoiding a temporary fluent lvalue copy. Enabled
  default-all dependents/examples compiled; disabled GUI/platform/configuration
  variants were not built.

Fresh functional gates:

| Group | Executables | Top-level TAP checks | Result / raw log |
| --- | ---: | ---: | --- |
| Foundation, logger/resolver, HTTP, WS, REST, RPC, MCP and selected native integrations | 58 | 518 | PASS, `/tmp/repair3-functional-core.log` |
| Daemon/provider and Ztc lifecycle graphs, including new Requests late-retirement coverage | 11 | 33 | PASS, `/tmp/repair3-daemon-functional.log` |
| Selected real IAM cluster/auth-route path | 1 | 1 | PASS, `/tmp/repair3-role-iam.log` |

The core group includes the Alt-Svc release-admission/overflow cases, mixed
TLS/H2 configuration contract, actual sized heap class counters, H2 header
retirement across batches, logger overflow completion, RPC last-credit
registration/publication cases, and REST cancellation/session/child-retirement
case. HeapTest's deliberate duplicate-initialization child assertions are
expected negative cases; its TAP result passed. The IAM gate was selected to
exercise the changed co-located auth-route helper in real daemon local-user DB
handling. Other real publisher/WSS cluster gates were not repeated: their
activation contracts were not changed and their earlier evidence remains
historical. Fresh Ztc role/lifecycle unit graphs passed. Held auth-route DB work
during stop, external-provider auth-route handling and scheduled MaintenanceRun_
were not directly exercised by a dedicated fresh runtime case; their specific
co-location/destruction coverage remains source-based. Do not extrapolate from
these passing gates to every private helper branch or a performance result.

Fresh file/type-only GDB evidence (no inferior run):
`/tmp/repair3-new-layout.md`, `.log`, and `.r4.log`, compared with the preserved
old configured DWARF. Sizes below are actual complete-type DWARF byte sizes;
class columns apply the exact ZmHeapAllocSize rule. Classes on stack/embedded
controllers are conditional examples, not claims of actual allocations.

| Configured representative | Old sizeof | New sizeof | Fixed class old → new |
| --- | ---: | ---: | --- |
| BatchCaller base | 64 | 64 | embedded |
| RPC IOClient | 1152 | 1152 | conditional 1152 → 1152 |
| RPC IOServer | 1152 | 1216 | conditional 1152 → 1216 |
| RPC HTTPClient | 1344 | 1408 | conditional 1344 → 1408 |
| RPC WSClient, each of four configured profiles | 1472 | 1536 | conditional 1472 → 1536 |
| Accepted WSConnection | 256 | 256 | 256 → 256 |
| REST Server | 256 | 336 | conditional 256 → 384 |
| REST WorkNode | 312 | 320 | 320 → 320 |
| SSEProducer | 256 | 192 | 256 → 192 |
| AuthRouteLoad_ | 720 | 736 | 768 → 768 |
| MaintenanceRun_ | 88 | 104 | 128 → 128 |
| Separate DaemonWork | 32 | 32 | 32 → 32; eliminated in the two co-located families |
| Common ResponseRecord | 88 | 88 | embedded |
| Contract TCP response allocation node | 128 | 128 | 128 → 128 |

Unchanged base sizeof does not imply unchanged derived size: base tail padding
can be reused by dependent fields. The widened credit, ReadyFn and controller
result state therefore have visible costs above. The callback storage is needed
through final-credit publication and queued owner completion; replacing it with
special per-parent lifecycle hooks/link state would broaden the mechanism.
REST's queue/cursor/ordinals replace the unbounded snapshot scans and concurrent
session chains; their fixed owner cost is retained explicitly. The uint64 Work
ordinal costs eight logical bytes per request, with no class increase for this
measured node. The event queue retains the canonical Session plus cutoff value,
uses identified on-demand backing, and has no newly claimed hard memory bound.
SSE's deleted global-post pointer/counters shift its aligned queue and remove a
complete 64-byte fixed-class step. No module-wide net memory claim follows.

Each co-located Zum helper grows 16 logical bytes but keeps its old class; deleting
its separate 32-byte credit changes requested classes from 768+32 to 768 for
auth-route, and 128+32 to 128 for maintenance. One allocation and 32 requested
class bytes are saved per such admitted graph. Other Server/Daemon graph credits
remain necessary for heterogeneous descendants. Normal credit return eliminates
one owner scheduler post, using a load/CAS decrement with contention retries;
Closed return retains a counted owner handoff. No measured latency/throughput
or instruction-count improvement is asserted.

Logger overflow shutdown adds one cold identified ZmRingFn closure allocation
for its eight-byte semaphore capture: the queue's by-value delegate move must
own retained callback storage. It also adds one queued delegate value, possible
existing queue-backing growth, one stateless best-effort wake and a stack
semaphore. This avoids adding a persistent semaphore field/per-log branch; the
callback allocation is freed when transferred into the ring. Ordinary logging
adds no new branch, counter or allocation. This is an explicit cold cost of the
no-polling repair, not an allocation-free shutdown claim.

GDB alignment is not a compiler evaluation here. Raw DWARF has explicit
64-byte-aligned members but omits some enclosing class alignment attributes;
GDB incorrectly reports alignment 8 for BatchCaller/SSEProducer and some
controllers. Their source/member requirements remain at least 64; never use
that fallback to tune allocation alignment. DWARF sizes/offsets and the class
comparisons above remain valid. Response sizes are unchanged in fresh artifacts;
R4 still retains its 24 common tree bytes with the prior local-alternative
analysis, not a footprint repair. No actual alternate layout/configuration was
built and no RSS, cache-miss, latency or throughput benchmark was performed.

The final Memcheck gate is now running. Its results are not established by the
functional passes or these file-only measurements.

### repair3 final memory gate and closure

Final gates used the same fresh configured artifacts and `libtool exec`:

| Group | Parent executables | Top-level TAP checks | Result / prove log |
| --- | ---: | ---: | --- |
| Core changed lifetimes | 28 | 336 | PASS, `/tmp/repair3-memory-core.log` |
| Daemon/provider/Ztc graphs | 11 | 33 | PASS, `/tmp/repair3-daemon-memory.log` |

All 39 normally terminating parent records report zero UNSUPPRESSED errors and
zero definite, indirect or possible lost bytes. No parent lacks a final memory
outcome. Raw records are `/tmp/repair3-core-memcheck.<pid>.log` and
`/tmp/repair3-daemon-memcheck.<pid>.log`; machine-readable cross-checks are
`/tmp/repair3-core-memory-summary.json` and
`/tmp/repair3-daemon-memory-summary.json`. The independently reviewed daemon
record table is `/tmp/repair3-daemon-memory-results.md`.

This is not a claim that every auxiliary process has a clean memory report:

- Nine TransportContract assertion-death children, PIDs 1996627–1996635 of
  parent 1996582, deliberately terminate with SIGABRT through the exact expected
  Wire/StreamState assertion. Their 60 error contexts are possible-loss records:
  six snapshots of 5,112 bytes and three of 5,816, totaling 48,120 bytes/60 blocks
  across separate fork snapshots. Each has zero definite/indirect loss and zero
  suppression. Independent review accounted for every record: live path/name
  strings, catalog singleton storage, loader mappings and, in three cases,
  retained writer buffers. Aborting skips stack/global teardown. None reports
  invalid access/free, undefined-value, syscall-buffer or overlap errors.
  These are disclosed abort snapshots, NOT clean child runs or proof of child
  normal cleanup. No loss suppression, test skip or production cleanup change
  was added to conceal them. The normal parent independently reports zero
  errors/selected loss. Exact assertion/record mapping and source reasoning:
  `/tmp/repair3-death-child-review.md`, `.json`, `.py` and all nine raw logs.
- Another 76 auxiliary core logs contain only the six-line fork/exec header,
  with no final memory summary. They establish no clean-memory outcome for
  those helpers. The gate covers the selected parent executables; it does not
  instrument every exec descendant or every real cluster peer.

Both scripts checked Valgrind 3.26.0 and libcrypto.so.3 build ID
96e3095eff34eb22a2bcd9edb0e8b230c5dea19c before loading the existing opt-in
`openssl.valgrind.supp`. The sole additional custom rule remains Cond-only,
matching two libcrypto frames and EVP_DecryptFinal_ex; there is no new access,
free, allocation or leak suppression. Preserve the prior standalone/vgdb
initialized-input GHASH proof and its limits: stack matching can also match a
genuine undefined conditional with the same frames. This is not an
exception-free crypto-definedness claim.

Current parent ERROR SUMMARY counters total 2,228 suppressed reports in 15
contexts across eight executables. The core portion is 2,227/14 across H3Hub
404/2, clientstream 878/2, serverstream 384/2, jrpcws 76/2, MCP HTTP 80/2,
MCP WS 179/2 and REST endpoint 226/2. Those core logs have aggregate counters,
not suppression-name listings; they do not independently attribute each count
to a rule rather than a tool default. The daemon run enabled the name listing:
RuntimeRevocation PID 1998978 records exactly one use/one context of
openssl-3.5.5-ghash-avx-copy-xor, and its other ten parents suppress nothing.
Do not substitute old repair1/2 counts for these fresh results or rerun passing
graphs merely to change diagnostic verbosity.

Final per-finding disposition:

- R1 repaired: eviction executes independently of ZDEBUG; repeated bounds,
  invalidation/zero capacity and framework numeric scanning regressions pass.
- R2 repaired: private TLS implementation and canonical tls access preserve H2
  expressions; required enabled callers/examples compile and configuration
  propagation/type checks pass. No narrowing compatibility forwarding family.
- R3 repaired: known-size backing removes the prefix through canonical VHeap
  dispatch, with matching old-size free, preserved heap tuning/bootstrap and
  actual class-counter/alignment/growth/recycling coverage. A 4,096-byte table
  payload uses the 4,096-byte class, rather than the former 8,192-byte class.
  No new cache-class specialization/tuning family is introduced. Sized wrappers
  and shared dispatch add code paths; compiler text size/instruction count and
  RSS/latency/throughput effects were not measured.
- R4 retains the justified common 24-byte tree linkage cost. Fresh concrete
  layout/allocator evidence and the smallest alternatives are recorded above;
  no false footprint-repair or universal optimality claim.
- R5 repairs avoidable auth-route/maintenance credit allocations and normal
  return posts, retaining required heterogeneous graph credit allocations with
  descendant/last-destruction proofs. Real helper/class sizes, fresh selected
  runtime gates and the untested private branch limits are recorded above.
- R6 repaired across the affected inventories: REST/SSE, shared RPC/MCP,
  Requests, active H2 header retirement and logger overflow. Every retained
  continuation consumes actual bounded work or follows a completion event;
  there is no readiness self-repost, shutdown spin, sleep or timed retry in
  these repaired paths. Publication/registration, cursor retirement and final
  callback owner-release proofs supplement the runtime/memory gates. Existing
  hash empty-bucket scans, allocator relocation and application callback work
  remain separately qualified turn-cost limits, not polling dispositions.
- R7 remains an explicit unresolved inactive-residency policy exception.
  Logical expiry, bounded origins and owner-event removal pass their existing
  checks; immediate physical reclamation during complete inactivity is not
  implemented or claimed. No timer/index/sweep or fabricated user approval.

Stop remains idempotent and best effort, ending Stopped on actual failure;
queued start is fresh. Main-controlled multiplexer runtime stays separate from
Zdb-controlled active listeners and role retirement. Final releases stopped,
drained resources and does not initiate/wait for stop. The user-requested
ZiAssert/clean fallback on H2 shadow lists remains intact.

GUIDELINES references in repair3.md and the per-family notes govern these source
choices. Final git diff --check passes; new source width/name/class-shape checks
have no remaining issue identified in this batch. Required implementation and
selected current-configuration validation for repair3 are complete. Retained
R4/R5 costs, R7 policy conflict, intentional-abort snapshots, reviewed OpenSSL
exception and unmeasured performance/branch coverage remain explicit; none is
renamed into full guideline, platform, interleaving or performance compliance.
