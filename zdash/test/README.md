# Dashboard module tests

`make -C zdash test` runs the schema test and the real dashboard with the
libtool-built `test/.libs/zdash_test.so` module. Both emit TAP, evaluated by
`prove`. The shell fixture requires `xvfb-run`, forces X11 on a disposable
display and never maps the dashboard onto the desktop. The synthetic window
is mapped on Xvfb and must paint after detail records populate its model;
`ZDASH_TEST_MAPPED` selects this check in the module. The small mirrored
ring exercises byte ownership, wrap-spanning records, bounded GTK updates,
EOS retention, agent disconnection/reconnection, publisher replacement and
window-close events. A source-attributed EOS leaves row references, selection
and expansion intact. A new connection generation with the same publisher
start time preserves them too; a changed start time retires the old subtree.
Populated
telemetry checks every row's key columns and grouping labels, including heap
identity fields, both socket endpoints, enum names, hexadecimal hash addresses,
database RAG and GTK path round trips. Two databases with matching child IDs
check separate grouping, child-created placeholders, repeated updates and
source teardown. Heaps are grouped by ID; each arena shows partition, size,
alignment and sharding. Two arenas sharing an ID must reuse one group, and
updates and sorted ID insertion preserve the selected arena's iterator.
Field headings appear once on `heaps`; ID subgroup rows contain only the ID.
`vshift` is not a key field.
Connections and links also arrive before their parent records. Later heap
insertion checks sorted sibling paths, persistent iterators, selection and
expansion; DB updates check RAG transitions with the heap branch collapsed.
Model notification callbacks verify GTK thread affinity.
Keyboard checks send Left/Right key events and verify collapse/expansion
without moving the cursor. Left on a leaf selects its immediate parent without
collapsing it; another Left collapses that parent. These checks focus a non-ID
column to ensure arrows navigate the hierarchy rather than the cells.
Repeated real cell rendering after warm-up checks that allocation counters on `ZDash.Value` do
not increase.
Both publishers are expanded before retirement, checking release of an
expanded subtree while another publisher remains. The custom model deletes
descendants before their parent and notifies GTK for each deletion.
One rejected detail group is retired while inventory and heap delivery continue;
a late reply for the cancelled group cannot add a row. The final burst closes
the real window while telemetry remains queued, exercising the drain and
callback teardown rather than closing only an idle dashboard.
The burst spans multiple refresh quanta; the test requires partial drains and
GTK events while telemetry remains queued, followed by delivery of the tail.
Inventory alone has no detail groups. The GTK checks expand populated branches,
verify the stable `ID` headings and group field labels, and ensure uncolored
rows retain the GTK theme's foreground and background colors. RAG is confined
to the status indicator. The fixture also checks default window/pane geometry
and the selected-object details table: unquoted text, numeric alignment and
thousands separators, typed dates including nanoseconds, alternating
backgrounds, stable selection through sorted insertion, updated
symbolic DB state/RAG and clearing on source retirement. The mapped fixture
also exercises GTK keyboard bindings across every parent depth and leaf,
including expansion, collapse, arrows, Home/End and page navigation. Detail records
use independently admitted group IDs and the production subscription filter.
The custom model reuses a named-heap string buffer; consumers inspect borrowed
text before the next text read reuses that storage.

The module exports `ZdashModule`; `zdash` loads it with `ZiModule` only when
`ZDASH_TEST` names a module, resolves the factory, and invokes the
returned `ZDash::Module` interface. Test state, synthetic telemetry and
assertions live in the module. The host interface documents Rx/GTK ownership;
the module remains loaded through process teardown, like Zdb store backends.

For an authenticated live hub, run the same module with the normal
`--config`, `--wss`, `--device-id` and `--ca` options, setting
`ZDASH_TEST_TOKEN` to a pre-issued client token. Use an isolated Xvfb display.
The module then checks receipt/rendering and closes via the GTK event loop.
`ZDASH_TEST_TIMEOUT` sets a 1–3600 second deadline (default 15).
Without a WSS URL the module runs the synthetic phases. No test CLI flags
or synthetic telemetry are compiled into `zdash.cc`.

ASan/LSan remain enabled when supplied by the current build configuration.
The fixture suppresses only Fontconfig's process-lifetime cache; other leaks
and sanitizer errors remain fatal.
`fontconfig.valgrind` provides the corresponding narrow Memcheck suppression
for Fontconfig's XML pattern cache, without suppressing Z allocations or
invalid memory accesses.
