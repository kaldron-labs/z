# Restore zdash telemetry browsing

## Objective and baseline

Restore the working behavior of revision 1, commit
`d2df7e7ffab4b0ae1ebb3bcfc46ba977f34b0714`, using the current OAuth/WSS
transport and Ztc telemetry types. The dashboard must receive independently
requested telemetry groups, construct its tree from actual records, and allow
users to expand populated subtrees while telemetry continues updating.

Recovery includes both objects first observed after the initial snapshot and
later updates to existing objects. Incoming records must update the model
without a click, selection change, collapse/re-expand, or another frontend
request. Publisher sampling at the configured subscription interval remains
valid: real-time presentation means processing each incoming batch promptly,
within the configured GTK refresh scheduling, rather than waiting for EOS.

Revision 1 is the behavioral reference, rather than a source file to copy
over the current implementation. Preserve current authentication, source
attribution by device/publisher/generation, multiple-database identities,
complete key fields, pooled buffers, bounded queues and shard ownership.
No Zum REST changes are required.

EOS is solely a snapshot completion boundary. It may synchronize or batch
received records; it never indicates publisher removal, connection loss or
cache invalidation. The dashboard applies incoming records in order without
waiting for EOS. Collector generation changes rebuild transport routes while
preserving GTK rows, expansion and selection. Publisher `startTime` changes or
explicit `Shutdown` retire the old publisher subtree; `AgentGone` only marks
the source disconnected. This clarification supersedes earlier verification
notes that describe retiring rows on collector generation replacement.

Retain the custom-model implementation pattern in `zdash/src/zdash.cc.orig`
from line 455 onward: the typed hierarchy and iterator union, CRTP model,
compile-time child-to-parent mapping, and on-demand key/RAG rendering. Adapt
that pattern to the current hierarchy; it is an architectural requirement of
this repair.

The original implemented tree displayed object keys and RAG colors; it did
not display every telemetry metric as a column. Preserve all incoming metric
data in the model and update the existing presentation where it depends on
that data. Completing the original unfinished watchlist, graphs or alerts UI
is outside this repair.

This document is an implementation plan. The recent attempted fixes remain
uncommitted and must be reviewed against this plan before proceeding.

## Findings from history

- Revision 1 accepted all supported telemetry types through
  `processTelemetry`, queued them for GTK, and dispatched them through
  `processTel2`/`processTel3`. A group row was inserted when its first item
  arrived. GTK expansion did not initiate telemetry requests.
- This queue was an intentional thread boundary: incoming telemetry messaging
  ran separately from the GTK thread. The scheduled GTK drain performed model
  mutations and UI notifications on the GTK thread. Preserving that separation
  is part of behavioral recovery, not merely a performance optimization.
- The original command bridge allowed telemetry requests to be forwarded to
  a selected publisher connection. The telemetry service maintained separate
  subscriptions by group/filter. The original dashboard did not automatically
  request every group on login; subscription control and rendering were
  separate concerns.
- Revision 1 deliberately used `ID` followed by unnamed top-level columns.
  Group rows supplied field labels such as `partition`, `size` and `addr`.
  Those columns contain different key fields for different row types.
- Its keyed `processTel3` path inserted previously unseen objects and updated
  existing objects in place with `ZfbStruct::update`, followed by a GTK row
  update notification. Its bounded GTK drain ran independently of EOS.
  These behaviors survive in parts of the current renderer and should be
  preserved and exercised through the real transport.
- Commit `8cec895b` replaced the command transport with a single configured
  WSS subscription, defaulting to `App` inventory. The renderer for the other
  groups survived, but the ordinary session lacked a subscription path to
  populate them.
- The agent implementation introduced in `f2b46c0c` treated a successful ACK
  as terminal for a zero-interval request. Current publishers acknowledge
  acceptance before asynchronously emitting the snapshot records and EOS.
  Deleting the route on ACK discards those records.
- Agent and hub routing also treat every EOS as terminal. Current publishers
  emit EOS after each snapshot within a continuing subscription, so a valid
  subscription is removed after its first snapshot.
- The populated-tree test bypasses subscription filtering when injecting
  detail telemetry. Its success does not establish that normal subscriptions
  can deliver those records through a publisher, agent and hub.
- The recent attempt added empty groups, expansion-triggered snapshot polling
  and selection-dependent column headers. This changed the original behavior.
  Empty groups exposed the inherited model's disagreement between
  `iter_has_child` and its actual children, causing the GTK consistency warning
  and lockup. That child-reporting behavior already existed in revision 1;
  pre-creating empty groups exposed it.
- Desktop validation found another inherited defect in sorted row insertion:
  `ZuInterSearch` can choose pivot zero for skewed comparison distances and
  repeat the same partition forever. Revision 1 has the same search code.
  The current publisher's hash-table keys expose it; retaining the original
  model does not require retaining this nontermination bug.

## 1. Remove the unsuccessful workaround

Review the working-tree changes individually; do not reset whole files or
discard unrelated work.

- Remove `inspect_`, the detail polling timer, sequential snapshot cycle, and
  the expansion/collapse callbacks that control that cycle.
- Remove the eager insertion of every telemetry group under an inventory row.
  Restore group insertion on receipt of the first real item.
- Remove selection-dependent global headings and column visibility. They
  relabel other visible rows according to the selected row's type.
- Remove documentation and tests that require these workaround behaviors,
  including the artificial two-slot limit and the most-recently-expanded
  publisher refresh policy.
- Retain independently justified corrections, including theme-default colors
  for uncolored rows and the successful-ACK routing correction, after checking
  them against the contracts below.

## 2. Correct request and subscription lifetimes

Affected code: `ztc/src/ZtcApp.cc`, `ztc/src/ztcagent_daemon.cc`,
`ztc/src/ztchub.cc`, and `ztc/src/ztchub_daemon.{hh,cc}`.

Establish and apply this contract throughout the routing path:

| Event | Snapshot request (`interval == 0`) | Continuing subscription |
| --- | --- | --- |
| Successful ACK | Request accepted; retain route | Subscription accepted; retain route |
| Telemetry | Deliver records | Deliver records |
| EOS | Snapshot complete; retire completed request | Current snapshot complete; retain subscription |
| Rejected ACK or terminal error | Deliver failure and retire affected request/leg | Deliver failure and retire affected subscription/leg |
| Unsubscribe or owning session disconnect | Cancel outstanding work | Cancel subscription |
| Source retirement | Cancel affected source leg | Cancel affected source leg; retain unrelated discovery/delivery |

The table applies at the scope identified by the response. A source leg of a
wildcard request is distinct from the aggregate request. In particular, source
retirement must not cancel continuing inventory discovery for other sources.

Steps:

1. Keep the agent's request legs after successful ACK, including zero-interval
   requests. Handle all non-OK ACK statuses consistently.
2. Make agent EOS handling depend on the request lifetime. Retain continuing
   legs across successive snapshots. Keep publisher shutdown and inventory
   retirement distinct from ordinary snapshot completion.
3. Preserve request lifetime in hub route state when admitting a request.
   Carry that state through route lookup rather than inferring termination
   solely from the response body type.
4. Retire hub routes on snapshot completion only for snapshot requests. Do not
   send an unsubscribe after EOS for a continuing subscription.
5. Check wildcard requests that fan out to multiple publishers: one publisher's
   EOS or source-specific failure must not prematurely end delivery from the
   others. Define aggregate snapshot completion over the admitted source set,
   including failed/retired legs. Continuing discovery must admit publishers
   appearing after its initial inventory EOS.
6. Preserve response ordering, sequence correlation, generation fences,
   bounded egress and cancellation of in-flight work. Review queue handling
   that classifies EOS as terminal so it agrees with the corrected lifetime.
7. Audit request ownership when two frontend consumers target the same
   publisher/group/filter. Publisher `subscribe_` currently replaces the
   sequence number of a matching group/filter subscription, and `unsubscribe_`
   removes it by that key. Establish whether upstream sharing already isolates
   consumers; otherwise implement proper sharing with consumer lifetimes or
   independent publisher requests. One consumer must not steal another's
   replies or cancel its continuing stream.

Do not change the wire schema unless this audit demonstrates that internal
request/route state cannot express the contract. Propagate any necessary API
change through its callers without compatibility shims.

## 3. Restore independent dashboard subscriptions

Affected code: `zdash/src/zdash.cc`, its configuration metadata, and
`zdash/README.md`.

Implement subscription management independently of GTK tree expansion:

1. Retain publisher inventory discovery. On discovering an eligible publisher,
   establish the requested detail-group subscriptions for that source, even
   when it appears after the initial inventory EOS. Repeated inventory updates
   for the same source/generation must update its row without creating duplicate
   detail subscriptions.
2. Support an explicit set of requested groups. Normal browsing should request
   `Heap`, `Hash`, `Thread`, `Mx`, `Queue`, `Hub` and `DB`; inventory supplies
   application rows. Alert handling remains outside this tree repair.
3. Give each request a session-scoped subscription ID. Track group, filter,
   device, publisher, generation and lifecycle in an Rx-owned registry.
   Route incoming replies through that registry rather than one global
   `m_sub`. Reject mismatched live replies and handle late cancelled replies
   according to the generation/cancellation rules.
4. Keep subscriptions for different groups and publishers independent.
   Expanding, collapsing or selecting a GTK row must not change what data
   the dashboard receives. Keep each subscription across snapshot EOS; do not
   replace it with repeated one-shot requests or resubscribe after each EOS.
5. Cancel source subscriptions deterministically on source retirement,
   transport generation replacement and shutdown. Rediscover and subscribe to a new agent
   generation within the live dashboard session; never reuse old routes across
   generations. On dashboard session restart or a supported reconnect, rebuild
   inventory and detail subscriptions. Renew the access token using the refresh
   token before expiry, save each rotation in Vault, and reconnect WSS while
   retaining the GTK window, model, expansion and selection.
6. Report admission/rejection failures clearly. A failure in one source/group
   must not silently erase the inventory or unrelated subscriptions.

Automatically subscribing to the configured group set replaces revision 1's
external command control; it is the current CLI/WSS browsing policy, not a
claim that revision 1 automatically subscribed on login. Document group/filter
selection and sampling intervals, including their effect on coverage and load.

Subscription sizing must follow the requested workload. Document the inventory
slot plus the detail subscriptions required per publisher and align the
walkthrough's hub capacity accordingly. For one publisher and the seven detail
groups, budget eight frontend slots; for N publishers, budget `1 + 7*N` with
this targeted-request design. Check the corresponding agent/publisher limits
and queue budgets as well. Bound the registry and pending admissions through
configuration, and surface capacity exhaustion when later publishers arrive;
do not replace real subscriptions with polling to fit the old two-slot setting.

### Thread ownership and queued delivery

Preserve revision 1's decoupling of incoming messaging from GTK updates:

- Keep subscription admission, response validation and routing on their owning
  messaging shard. Incoming callbacks enqueue owned telemetry buffers/control
  events and schedule GTK work; they must not access widgets or mutate the
  presentation's telemetry containers, source objects or tree rows.
- Keep all presentation state on the GTK thread. The GTK drain performs object
  lookup, insertion, `ZfbStruct::update`, RAG changes, source removal and GTK
  model notifications. Updating an existing object's data off-thread and only
  deferring its notification is insufficient: GTK reads that data when rendering.
- Continue transferring pooled I/O buffers through the bounded ring. Retain
  buffer ownership until GTK has consumed the record; do not queue borrowed
  FlatBuffer pointers into receive storage that can be reused. Cross-shard
  posts carry owned source identifiers or handles and small metadata, not GTK
  row pointers. Route any outbound requests through the appropriate owner.
- Preserve empty-to-nonempty scheduling, bounded GTK drain work and rearming
  while records remain. Messaging must not synchronously wait for GTK to render,
  and GTK must not wait for network replies. Do not replace ownership with locks
  around shared model mutation or invoke the GTK drain inline on a producer.
- Order source retirement and generation changes with queued telemetry, or fence
  queued events equivalently, so late records cannot recreate retired rows.
  During teardown, stop producers and drain/cancel queued work and scheduled
  GTK callbacks before releasing their targets. Destroy presentation state on
  GTK and release subscription state on its owning shard.

## 4. Restore the data-driven tree and field labels

Affected code: `zdash/src/zdash.cc` and, if a shared-model correction is needed,
`zgtk/src/ZGtkTreeModel.hh`.

### Preserve the custom model and reusable formatting storage

- Keep `ZGtk::TreeHierarchy::Model<Model, Iter, Depth>` and its typed row/parent
  relationships. Extend the iterator alternatives, parent mapping and depth for
  the current database collection and pools. Correct iterator and notification
  defects within this pattern; do not replace it with a generic GTK tree store
  or a second materialized copy of the presentation hierarchy.
- Preserve on-demand column formatting from telemetry keys and RAG state.
  Keep one GTK-owned `m_value` string buffer in the model, clear its logical
  length for each formatted value, and reuse its capacity. Do not construct
  temporary strings or cache separately allocated strings for every row/cell.
- Declare the buffer using a `ZuDerive`-defined type with an explicit heap ID,
  for example `ZuDerive(ValueString,
  (ZtString<ZtStringHeapID<"ZDash.Value">>));`, and store `ValueString m_value`.
  Replace the current `String::Base` member with the named derived type.
  Adapt metadata/UDT printer interfaces to accept it without erasing its heap
  identity or introducing conversion strings.
- Preserve the original model/view buffer-consumption pattern: formatting
  feeds the cell renderer before the buffer is reused for another text value.
  Audit the current `set_string` copy against the original `set_static_string`
  handoff and restore the latter where that consumption lifetime holds. Check
  actual consumers and reentrancy; borrowed values must not survive buffer
  reuse. Any consumer requiring ownership must copy at that boundary rather
  than impose an unconditional extra allocation on every model value read.
- Keep the buffer, formatting and cell-renderer consumption entirely on GTK,
  consistent with the thread-ownership contract above.

### Hierarchy and update behavior

Cover every implemented revision 1 branch, mapped to the current telemetry
types. Paths below are relative to the publisher/application root; angle
brackets denote object rows rather than literal labels.

| Tree path | Incoming telemetry | Subscription group |
| --- | --- | --- |
| Publisher/application root | `AppTelemetry` | `App` inventory |
| `heaps / <ID> / <arena>` | `HeapTelemetry` | `Heap` |
| `hashTbls / <hash>` | `HashTelemetry` | `Hash` |
| `threads / <thread>` | `ThreadTelemetry` | `Thread` |
| `multiplexers / <mx>` | `MxTelemetry` | `Mx` |
| `multiplexers / <mx> / <connection>` | `CxnTelemetry` | `Mx` |
| `queues / <queue>` | `QueueTelemetry` | `Queue` |
| `engines / <engine>` | `HubTelemetry` | `Hub` |
| `engines / <engine> / <link>` | `LinkTelemetry` | `Hub` |
| `databases / <db>` | `DBTelemetry` | `DB` |
| `databases / <db> / hosts / <host>` | `DBHostTelemetry` | `DB` |
| `databases / <db> / tables / <table>` | `DBTableTelemetry` | `DB` |
| `pools / <pool>` | `PoolTelemetry` | `Hub` |

Revision 1 had a single `dbenv` branch. Retain the current database collection
and database-scoped hosts/tables rather than reverting that extension. Pools
are also a current extension to preserve. Neither nested types nor pools
require additional subscriptions beyond the seven detail groups above.

- Create group rows from real telemetry, as revision 1 did. Preserve only
  parent placeholders required by real child records arriving before their
  parent records: connections before their multiplexer, links before their
  engine, and hosts/tables before their database. Later parent telemetry must
  populate the same parent and preserve its children.
- Key objects by telemetry type, complete object key and source identity
  (device/publisher/generation), including database identity for DB children.
  Insert a new object once; apply subsequent records to the existing item and
  notify GTK. Preserve `ZfbStruct` field-presence/default semantics, including
  updates to zero/false values where the schema supports them. Update stored
  metrics and RAG state as well as visible keys.
- Keep receiving and updating objects while their ancestors are collapsed.
  Expanding must reveal the latest stored children and values immediately.
  Do not clear/rebuild the tree at EOS or on repeated snapshots. EOS is not an
  object-deletion signal, and absence from a batch is not evidence of deletion.
  Follow explicit publisher retirement semantics; do not
  invent object-removal semantics absent from the protocol.
- Restore the stable `ID`/group-row field-label layout. Preserve the expanded
  key width needed by current telemetry types. Do not globally name columns
  `Publisher`, `Device` and `Generation` when child rows use those slots for
  other fields. The subsequently requested sizing, RAG indicator and selected-object details changes below extend this recovery without replacing its model.
  Preserve type-aware key formatting, including hexadecimal addresses and
  symbolic enum values, and keep every composite key component accessible.
- Verify that `iter_has_child`, `iter_children` and `iter_n_children` agree,
  including while a group transitions from empty to populated. Emit the
  required GTK notifications when the first child appears or the last child
  disappears.
- Audit insertion, update, deletion, iterator validity and path round trips
  while a subtree is expanded. Apply shared iterator/model fixes in ZGtk when
  appropriate rather than accumulating dashboard-specific overrides. Preserve
  selection and expansion of existing objects when a new sibling changes their
  paths; an update must not create a duplicate row or reset the view.
- Preserve ordering, source identity and database-scoped child identities.
  Retain GTK theme colors for row text, backgrounds and selection. Show RAG
  with a compact colored indicator beside the ID, as subsequently requested;
  do not paint saturated backgrounds across the row.
- Ensure sorted insertion makes progress for skewed key comparisons. Fix the
  shared `ZuInterSearch` pivot invariant rather than bypassing sorting in the
  dashboard, and cover the observed endpoint distances in `ZuSearchTest`.

Audit the complete push path: publisher sampling must re-enumerate current
objects, not replay a frozen initial list; routing and dashboard filtering must
admit later records; the Rx-to-GTK ring must wake on new work, drain within its
refresh quantum, and rearm while work remains. GTK must stay responsive during
sustained input. Preserve bounded queue behavior and make overflow/failure
visible rather than silently presenting stale data as current.

## 5. Validate the real behavior

First build with top-level `make -j4` so dependent headers, libraries and
fixtures are consistent. Run the relevant Ztc, ZGtk and zdash checks after
each substantive implementation stage.

### Transport regression tests

Use actual publishers and the production agent/hub path, with disposable
fixtures under `ztc/itest`:

- Successful ACK followed by asynchronous heap records and EOS delivers every
  snapshot record before retirement.
- A continuing subscription delivers at least two complete snapshots across
  EOS, then stops on unsubscribe. After the first EOS, create another object
  and change mutable telemetry on an existing object. Assert that both arrive
  under the same subscription ID without another subscribe request. Replaying
  two identical snapshots is insufficient coverage.
- After initial inventory EOS, start a new publisher and verify that discovery
  continues. Repeated inventory updates do not duplicate dashboard subscriptions.
- Concurrent groups and publishers deliver without cancelling one another.
- Two frontend consumers of the same publisher/group/filter both receive
  updates; unsubscribing one leaves the other running.
- Wildcard fan-out completes correctly across multiple publisher responses.
- Rejected requests, publisher shutdown, agent generation replacement,
  cancellation races and queue overflow preserve unrelated subscriptions and
  release their own state.

### GTK regression tests

Use the real GTK window on an isolated Xvfb display with fatal GTK warnings.
Map the synthetic session for painting checks; keep the live transport session
hidden while checking its model and subscription behavior:

- Deliver telemetry from a non-GTK messaging thread through the production
  handoff. Assert GTK thread affinity at model mutation and notification points,
  including existing-object updates, source retirement and error presentation.
  Tests that call the renderer directly on GTK do not establish this boundary.
- Inventory alone does not create fabricated detail groups.
- A real heap record creates the group and child; the subtree expands and
  remains responsive through subsequent updates.
- Exercise every path in the coverage table, including connections, links,
  DB hosts/tables and pools. Test child-before-parent delivery and later parent
  updates without losing children. Distinguish identical object keys belonging
  to different sources or databases.
- After an initial batch, insert new objects and update existing objects while
  ancestors are both expanded and collapsed. Check values, RAG transitions,
  stable object identity and row counts. Repeated records must not duplicate
  rows; updates and new siblings must preserve selection and expansion.
  Include the first record for a previously absent group after initial EOS,
  so coverage includes newly appearing branches as well as new leaf rows.
- Group field labels and every displayed key component remain correct while
  several row types are visible and selection changes.
- Exercise successive text-column reads and actual renderer consumption with
  different key lengths, including buffer growth, to check borrowed-value
  lifetimes and prevent text leaking between cells. Check allocation behavior
  after warm-up: model formatting reuses capacity and any buffer growth is
  attributed to its named heap. Distinguish GTK's own required allocations from
  avoidable model formatting/copy allocations.
- First-child insertion, source removal and generation replacement work with
  expanded trees and valid paths. Include last-child removal wherever supported
  by actual lifecycle events and insertion before an existing sorted sibling.
- Retire descendants before their parent, with GTK notifications at each step.
  Verify removal of an expanded publisher while another publisher remains;
  checking deletion of the only root does not cover this lifetime.
- Row text and selection respect the theme, and compact RAG indicators reflect
  the incoming severity without overriding row backgrounds.
- Records become visible before EOS. Exercise an idle-to-active ring transition
  and a burst spanning multiple GTK refresh quanta; verify that no wakeup or
  queued tail is lost and that GTK continues processing input.
- Map the synthetic window on its isolated Xvfb display and require an actual
  draw after detail records populate the model. Programmatic cell rendering
  and unmapped-window callbacks alone do not prove the visible view paints.
- Exercise retirement/generation replacement and shutdown with telemetry and
  GTK callbacks pending. Verify that stale rows are not resurrected and no
  callback accesses released presentation state.

Synthetic fixtures remain useful for isolated renderer and ring checks, but
must not stand in for subscription coverage. Add a real dashboard integration
check under `zdash/itest` that receives detail telemetry through normal
subscription filtering. Keep the same dashboard session running through initial
population, new-object creation, existing-object updates, late publisher
discovery and agent generation replacement. Assert the resulting model and
expand nested GTK subtrees; verify the request stream has no expansion-triggered
requests or EOS-triggered resubscriptions. Use controlled publisher fixtures
for otherwise unavailable types. Derive delivery deadlines from configured
sampling/refresh intervals with a scheduling margin.

### Desktop acceptance

After the automated checks pass, run the updated `zdash/README.md` sequence.
Verify all available branches against the coverage table. While the dashboard
remains open, create a new telemetry object and generate activity that changes
an existing object's metrics/RAG. Observe their arrival without requesting a
refresh, then collapse/re-expand and switch selection. Exercise publisher
arrival and agent shutdown/restart without lockups or GTK consistency warnings.
Empty telemetry groups need not appear; automated controlled fixtures must
cover branches the walkthrough publisher does not expose.

Record test commands, results and any remaining limitations. Do not claim
success based on inventory rows, fabricated group labels or injected records
alone.

## Completion criteria

- Normal configured subscriptions populate the dashboard with real detail
  records without expansion-driven polling.
- Publishers and objects appearing after initial EOS arrive automatically;
  existing objects continue updating in place under the same subscriptions.
  Collapsed branches retain current data, and rendering does not wait for EOS.
- ACK and EOS have consistent request/subscription semantics at publisher,
  agent, hub and dashboard boundaries.
- Every branch in the coverage table expands to its actual children and updates
  without GTK warnings, duplicate rows or lost selection/expansion state.
- Messaging remains decoupled from rendering through the bounded handoff;
  presentation data mutations and UI notifications occur exclusively on GTK,
  including retirement and teardown while incoming work is pending.
- Field labels follow the revision 1 layout and remain meaningful across
  heterogeneous rows.
- The original custom-model pattern is retained for the current hierarchy,
  with GTK-owned reusable formatting storage declared as a named `ZuDerive`
  type with a heap ID and no avoidable per-cell formatting allocations.
- Subscription and queue capacity match the documented workload, and source
  teardown releases all associated state deterministically.
- The walkthrough and automated tests exercise the implemented behavior.

## Implementation evidence

The implementation retains the original typed custom model, updated for pools
and database collections, and uses `ValueString` on `ZDash.Value`. Model reads
reuse its capacity and hand borrowed text directly to the renderer. The UDT
printer preserves the derived stream type. The original `.orig` file remains
byte-for-byte identical to revision 1.

The dashboard now admits independent continuing detail subscriptions on its
Rx shard, creates groups only from real records, and renders through the GTK
ring drain. The shared hierarchy implements valid paths, requested child
indices and first/last-child notifications. The GTK scheduler bridge queues
one loop continuation after pending work, preventing redundant idle loops
from blocking queued mutations or teardown. A restarted collector sends the
existing publisher reset request before inventory, clearing the previous
request-sequence fence.

Verified in the clang debug build:

- `prove ./zu/test/ZuSearchTest`: 27 checks passed, including the skewed
  comparison distances that stalled real hash-table insertion.
- `prove ./zu/test/ZuCmpTest`: 132 checks passed; `./zdf/test/ZdfSeriesTest`
  also passed, covering another user of the shared interpolation search.
- `prove ./zm/test/ZmBitmapTest`: 11 checks passed, including replacement
  of an occupied bitmap and self-move.
- `make -C zf/test test`: 10 files, 311 checks passed.
- `make -C ztc/test test`: 7 files, 27 checks passed, including independent
  publisher consumers and cancellation of only one consumer.
- `make -C zgtk/test test`: passed.
- `make -C zdash/test test`: schema and isolated GTK session passed with fatal
  warnings. Checks include all implemented tree paths, child-before-parent
  records, borrowed input destruction, wrap-spanning ring bursts, persistent
  iterators, sorted sibling insertion, selection/expansion retention, collapsed
  updates, RAG transitions, generation replacement and GTK notification affinity.
- Repeated actual cell rendering after warm-up leaves `ZDash.Value` allocation
  counters unchanged.
- The mapped GTK session records eight partial queue drains and fourteen
  GTK events while telemetry remains queued. It also closes the window during
  an incoming burst and rejects late records for a cancelled detail group.
- `ztc/itest/ztchubtest.py`: real heap snapshots, continuing EOS, post-EOS heap
  creation and metric updates, two consumers, overflow and restart passed.
- `ZDB_MODULE=... python3 zdash/itest/zdashlivetest.py`: passed. One live GTK
  session receives a new heap, an existing heap's RAG update, late publisher
  discovery and collector generation replacement through production filtering.
- Mapped GTK Memcheck: passed with zero errors and zero definite leaks after
  the narrow Fontconfig process-cache suppression; no Z allocation or invalid
  access is suppressed.

The controlled production publishers cover every tree branch, including
connections, links, pools and both databases' hosts/tables. The strengthened
live integration passes with outgoing request counts, two-publisher wildcard
snapshot completion and continuing delivery after one wildcard leg retires.
Expansion and EOS issue no new requests. Late discovery adds seven detail
requests; generation replacement cancels fourteen and establishes fourteen.
An inventory overflow with two cached publishers and a one-frame queue now
reports its own error while another frontend's subscription keeps delivering.
The hub's idempotent ordinary-route cancellation API also succeeds for an
absent route; the overflow handler uses the cancellation callback to distinguish
an ordinary route from inventory. Inventory cancellation is deferred to the
next Rx turn so it cannot invalidate an active inventory iterator.
The isolated sanitizer build exposed an occupied bitmap move assignment which
lost its prior allocation when telemetry rings reconnect. The ownership fix
and occupied, self-move and empty-move regression checks pass in both the normal
and sanitizer builds. The normal and isolated sanitizer dependent telemetry
checks also pass.

Builds now use `-j4` and run sequentially, as requested, to limit peak memory.
Remaining acceptance work is the walkthrough's visible desktop session. The
standalone
OAuth refresh fixture also passes after making its Vault program explicit as
`zdash` in both seeding and runtime; changing the logger's program name had
otherwise selected a different credential store. These results do not yet
establish all completion criteria above.

The desktop run exposed an infinite search while inserting real hash-table
rows. A debugger stack identifies `TreeHierarchy::Parent::add` calling
`ZuInterSearch` with `n=10`, `l=2`, `r=-39` and `p=0`; neither bound advances.
The shared search now advances that pivot to one. Its regression covers both
insertion and matching. The synthetic GTK fixture now maps its window on
Xvfb and requires a populated-model draw before proceeding to teardown.
The search regression passes, as do the repeated normal dependent checks and
live integration. The mapped GTK fixture passes normally and under Memcheck,
including GTK draws with telemetry still queued; Memcheck reports zero errors
and zero definite leaks with the same narrow Fontconfig suppression. The final
ownership audit also moves the collector's outgoing link lookup onto its Rx
owner; posts carry only the owned frame and generation. Its normal build and
dependent checks passed again after the sanitizer run.

The local README services start successfully and administrative CLI queries
work with the retained credentials. The saved dashboard credential was migrated
through Vault to the explicit `zdash` program and refresh login succeeds.
At that stage the desktop was locked (`org.gnome.ScreenSaver.GetActive`
returned true), so visible expansion, activity and restart acceptance awaited
unlocking. The services were available and `zumd` reported ready.

The sanitizer unit and mapped GTK checks pass. The live integration's final
shutdown exposed an expanded-tree leak in GTK 3.24.52: a plain `GtkTreeStore`
with two expanded parents also leaks the removed parent's child tree. Removing
the descendants first eliminates that reproduction's leak. The shared custom
model now retires descendants before their parent; no GTK leak suppression is
added. The synthetic test expands both publishers before source retirement.
The live fixture drains stdout/stderr while waiting for dashboard exit, so a
large sanitizer report cannot fill its pipe and conceal the error as a timeout.
The revised top-level and dashboard sanitizer builds pass, followed by the
strengthened mapped GTK test and full live integration, with ASan/LSan enabled.
The comparator and dataframe series checks pass under ASan as well. The final
normal builds, Ztc/ZGtk/dashboard tests, OAuth refresh and full live integration
all pass. The final mapped Memcheck run also passes: 33 partial GTK drains,
32 GTK events with queued telemetry, a populated-model draw, zero errors and
zero definite leaks. Valgrind 3.25.1 warns that syscall 317 (`seccomp`) is
unhandled; the fixture still completes. Its reported possible/indirect residues
are not a claim of zero total retained allocations.

### Current acceptance audit

| Requirement | Final automated evidence |
| --- | --- |
| Original custom model, reusable named buffer and field labels | Complete-key checks, actual renderer consumption and unchanged warm `ZDash.Value` counters |
| Every hierarchical branch and stable updates | Synthetic and real production fixtures, both databases, persistent iterators, selection and expansion checks |
| Push after EOS and new publisher discovery | Live session receives new objects, RAG updates, late publisher and generation replacement with observed request counts |
| Independent request lifetimes and bounded queues | Two consumers, wildcard completion/retirement, detail rejection and inventory overflow isolation |
| GTK ownership, responsiveness and deterministic teardown | Mapped drawing, queued GTK events, child-first source retirement and window close during queued telemetry |
| Memory and dependent correctness | Clang debug checks, isolated ASan/LSan checks and mapped Memcheck pass |

After unlocking GNOME, desktop acceptance established live heap expansion,
new-object arrival and green-to-red RAG updates without a refresh, plus
connections, links and pools. At that stage the five-minute access-token expiry
closed the session; the credential-renewal follow-up below corrects that
limitation. The subsequent
presentation fixes passed normal and sanitizer mapped tests, authenticated
live delivery and Memcheck. Native desktop inspection confirmed the larger
geometry, striped details, numeric alignment and indicators which retain
their colors on selected rows. Both database subtrees expanded; collector
restart rediscovered the source generation and publisher retirement removed
its expanded subtree while the other publisher remained. Window close exited
cleanly. The typed-date and general formatting follow-up below adds its own
verification before the final acceptance record.

### Requested presentation follow-up

- Start the supplied Glade window at 1600 by 900, with a 1000-pixel tree pane.
  Let the tree grow when the window grows; retain draggable pane resizing and
  horizontal scrolling for complete composite identities.
- Replace full-row RAG fills with a small red, amber or green indicator beside
  the ID. Leave uncolored indicators blank and row text, backgrounds and
  selection to the GTK theme. Preserve configured severity colors.
- Clicking an object displays all public telemetry fields, including derived
  counters and symbolic states, in a two-column Field/Value table. Include
  publisher, device and generation when selecting an application root.
  Print strings directly without JSON quoting; right-align numeric fields and
  left-align symbolic enums/flags and other text. Stripe alternate rows using
  a subtle theme-relative tint.
- Use a second custom model with a reusable named-heap formatting buffer.
  Borrow the selected GTK-owned live record through a persistent iterator;
  guard its lifetime with a GTK row reference. A sorted insertion follows the
  same object, incoming updates refresh its details, and retirement clears
  them. Formatting and all table notifications remain on GTK.
- Give each custom-model instantiation its own GObject type name. The original
  hard-coded TreeModel registration cannot support two model types in one
  process; this is a shared ZGtk correction, not a dashboard bypass.
- Extend the mapped fixture to verify the initial geometry, indicator-only
  coloring, complete selected fields, unquoted renderer text, numeric
  alignment, alternating backgrounds, stable selection, push refresh and
  retirement clearing. Re-run mapped memory checks and authenticated live
  delivery, then inspect the actual desktop presentation.


### Typed dates and grouped numbers

The subsequent formatting requirement makes `AppTelemetry::startTime` a
`ZuDateTime` with `DateTime` metadata and a `Zfb.DateTime` wire field. Capture
the publisher start time from `Zm::now()` without discarding fractional seconds.
Update schema round-trip checks to cover Julian date, seconds and nanoseconds,
and adapt the load fixture's timestamp/latency calculation to the typed date.
Do not keep an integer compatibility path. Rebuild and restart all telemetry
participants together for this wire-schema change, retaining the local IAM
configuration and credentials.

Print typed non-string fields using
`typename Field::Type::template Print<ZtFmt::Comma<>>`; print strings directly
to preserve the requested unquoted display. `ZtFmt` must define aliases for
all nine `ZuFmt` scalar directives with
`ZtFmt::Default` as their default base. Preserve explicit alternate bases and
all template arguments; import only non-directive scalar constants/types.
The extended date, flags and vector behavior then survives every directive,
including `ZtFmt::Comma<>` for numeric commas. Verify a real formatted date
and a numeric value above one thousand in
the mapped details table, plus the schema round trip and live telemetry path.

### Final formatting verification

The normal Clang debug top-level `make -j4` and separate dashboard build
completed without compiler warnings or errors. `ZtFmtTest` passes for all
nine scalar directives, inherited date/flags/vector defaults, nested explicit
bases and custom numeric separators. The existing date, enum, string and
boolean checks pass (193 test groups), as do the Zf checks (311 groups),
Ztc checks (27 groups) and ZGtk check.

The mapped dashboard fixture passes with a nanosecond-precision date and
the packed version rendered as `12.34.567` through `ZuSemVer`, including
actual renderer text and alignment. Tree keys use the same formatter; the port fixture now expects
`54,321`. Selection, expansion, push refresh and retirement checks also pass.
Mapped Memcheck reports zero errors and zero definite leaks, with 33 partial
GTK drains, 31 GTK events while telemetry remained queued and one populated
model draw. Only the existing Fontconfig cache suppression is used. These
normal-build checks verify the typed-formatting follow-up; the earlier
sanitizer results above cover the custom-model recovery and presentation work.

OAuth refresh and authenticated live dashboard integration pass. A reduced
load run with two agents, two clients, four subscriptions and two publishers
receives all eight samples, validates the typed timestamp latency against the
200 ms bound and tears down cleanly. That fixture needed its send helper to
use the public application-thread TLS entry point, and to close its links and
wait for disconnect callbacks before releasing links and finalizing the client
and scheduler.

The local Zumd, hub and agent were rebuilt and restarted together using the
retained IAM configuration and credentials. Native desktop inspection confirms
the 1600-by-900 window, wide tree pane, expanded live heaps, striped details,
readable `startTime` and right-aligned grouped numbers. The dashboard has been
reopened using its saved refresh credential and remains visible.


Credential renewal follow-up: the reported keyboard exit had no new core and
returned status 1 exactly at the hub's five-minute access-token deadline. The
dashboard had refreshed only at startup. It now reads `expires_in`, renews at
80% of the lifetime using the refresh token, saves every rotation explicitly
to the `zdash` Vault program, and replaces the authenticated WSS link. The TLS
client, GTK window, custom model and named formatting buffers stay alive. Old
link callbacks drain before replacement; intentional closure does not post a
session failure. Inventory replay restores independently routed detail groups.
All presentation updates remain on GTK. Renewal rejection is reported clearly.

Verification uses sequential `-j4` builds without compiler diagnostics. The
two-second OAuth fixture checks successive renewal deadlines, different access
tokens, refresh-token rotation across processes, persistence and rejection.
The mapped GTK fixture now drives real keyboard bindings over every hierarchy
depth, checks focus and Home cursor movement, and restores the heap selection
for subsequent push/retirement checks. Normal mapped tests, OAuth and full
authenticated live integration pass. Mapped Memcheck reports zero memory errors
and zero definite leaks with the existing narrow Fontconfig suppression.

The native dashboard renewed against the retained IAM services after four
minutes. Debugger snapshots on GTK confirmed the same window, model, selected
heap iterator and cursor before/after renewal, with the seven detail routes
re-established (`nextSub` advanced from 16 to 23). The adjacent striped detail
table remained populated and incoming heap counters continued updating.

The same native process and visible window remained open beyond the original
five-minute token expiry, with no error diagnostics. The dashboard is left
running on the desktop.

## EOS correction and verification

EOS now completes snapshots only. Agent and hub retain continuing subscriptions
across EOS, and the agent retains cached publisher inventory after an
unsolicited snapshot completion. One-shot fan-out emits its aggregate EOS only
after all publisher legs complete; failure reports `SnapshotFailed`.
Agent disconnection reports `AgentGone`, and confirmed publisher removal sends
`Shutdown`. Hub egress preserves record/EOS order within each subscription.

The dashboard ignores EOS for object and route lifetime. Collector reconnection
replaces detail routes while preserving existing GTK objects, selection and
expansion. A changed publisher `startTime` or explicit `Shutdown` removes the
old subtree. All model changes remain on GTK, behind the bounded Rx handoff.

The omitted `telSpin` configuration field previously loaded the unsigned null
sentinel (`UINT_MAX`) despite its C++ initializer of zero. Its metadata now
specifies zero explicitly; omitted transport timer settings also match their
C++ defaults. A regression test loads a minimal configuration and checks these
values. The restarted local agent's CPU usage fell from approximately 100% to
1–3%, with `telLL` remaining false.

Validation used `-j4` builds throughout. Telemetry unit tests (27 groups), mapped
GTK tests, OAuth renewal tests and authenticated live integration all pass.
The live test covers unsolicited EOS, aggregate snapshots, collector
reconnection with stable rows/selection/expansion, continuing delivery after
one publisher retires, publisher shutdown, and snapshot failure with no
publishers. Mapped Memcheck reports zero memory errors and zero definite leaks.
The rebuilt native dashboard is open with the heap tree expanded.

## Heap ID subgroups

The custom model now presents `heaps / <ID> / <arena>`. Each ID has one
GTK-owned parent row allocated from `ZDash.HeapID`; arena leaves retain their
complete telemetry identity and full formatted details. Arena keys display
partition, size, alignment and sharding. Sorted ID insertion reuses existing
groups and preserves arena iterators, selection and expansion.

Mapped GTK tests cover two arenas sharing an ID, field labels, sorted insertion,
keyboard navigation, EOS retention, collector reconnection and subtree teardown.
These tests and authenticated live integration pass. Mapped Memcheck reports
zero memory errors and zero definite leaks. The updated native dashboard is
open; its tree was not manually expanded.
