# Repair2 matched workload

`ZdbRepairBench.hh` adds `--repair-bench COUNT ROUNDS VECTOR_WIDTH` to the
existing `zdb/test/ZdbSagaTest` fixture. The same header and command-line branch
are applied only to the test in an isolated HEAD snapshot. To meet the matched
default/field-presence requirement, that snapshot's canonical fixture schemas
must use the native metadata defaults (including integer null sentinels).
`head-schema-normalization.json` records those baseline-only changes. C++ library
algorithms remain at HEAD; label the result a **matched-schema HEAD baseline**,
not an unmodified HEAD wire-layout result. Build the libraries and existing test
with the retained configuration before measuring. Do not run timings alongside
builds or other acceptance programs.

The store workload repeatedly inserts one row with two populated integer
vectors, a nonempty bitmap and three indexes, finds and selects it, updates it,
then deletes it. Codec construction is timed separately from asynchronous store
completion. Both direct store paths receive the same primary-key-only delete
projection. The complete three-step saga uses the existing LiveSaga fixture and
its original producer in each implementation, including intent/effect commits
and terminal cleanup. Its small application vectors are the fixture's existing
one-element vectors. A separate `codec.saga-save` sample serializes the existing
PayloadSaga definition with `VECTOR_WIDTH` bytes, exercising retained builtin
and heap payload storage. This is a warm single-row workload, not a growing-table or
network throughput measurement.

CSV records individual operation latency from `CLOCK_MONOTONIC_RAW`, encoded
bytes, and allocation/free deltas from all registered Z heaps. These counts
include cache requests and backing allocations; they do not count arbitrary
system allocations. Heap snapshots occur outside the timed interval. Codec
timings include construction/framing; store timings include scheduler dispatch
and completion waits. Saga timings include construction, submission and terminal
completion. The zero saga byte column means size was not measured by this path.
One full store cycle and one saga per round warm the paths and are omitted.
Sample storage is allocated before measurement. Preserve raw samples and report
timer/dispatch scope and variability when summarizing distributions.

Run at least small and large vector widths, and alternate paired HEAD/candidate
processes. Check semantic defaults and field presence separately before treating
measurements as acceptance. Results from this workload alone do not certify
cold recovery, every type/operation, or broader saga payload sizes.

The scheduler control runs after the codec/store/saga phases. It measures chains
of 2, 8 and 32 posts alternating between the same configured DB and store
threads, with one completion wait and no codec or data-store operations. It
reports `scheduler.*-posts` samples using the same timing and heap snapshots.
These controls help distinguish shared scheduling costs; they neither replace
complete-saga measurements nor establish the cause of a regression by themselves.

`--repair-latency COUNT ROUNDS VECTOR_WIDTH` runs the same operations and timer
without heap snapshots between samples. Allocation/free CSV cells are empty
because those counts are unmeasured. Compare this mode separately in matched
HEAD/candidate runs; do not merge its samples with `--repair-bench` samples.
The normal mode now uses a noncapturing heap observer to avoid counting its own
closure allocation. Earlier recorded counts include that observer overhead.
Both modes retain the same sample storage, warmup, completion waits and output.
Snapshots are outside timing, but their scanning and allocation work between
operations may affect scheduling; the latency-only mode isolates that effect.
Z heap telemetry also excludes `ZmVHeap` jumbo allocation paths.

Milestone20 extends the identical shared benchmark with `store.recover` result
materialization and a separate `codec.update-one` / `store.update-one` pair.
The latter toggles one element of the populated quantity vector and retains
full-field update serialization. The original `*.update` samples still change
only seqNo. Width32/4096 comparisons distinguish these mutations from inserts
and small-vector operations. Row versions now advance through both updates
before deletion; untimed reconstruction checks both updated fields. New samples
require rebuilding both benchmark fixtures before they can be compared.

## Matched sqlite3/postgres backend selection

The same fixture optionally loads `ZDB_MODULE` through the existing `ZdbStore`
module factory. `ZDB_REPAIR_CONFIG` supplies a complete inline ZfCf configuration
with the same four scheduler threads, standalone host and table cache settings,
plus the selected store's connection. Both variables are unset for the default
memory workload. Direct-store setup now calls start/stop outside measured
operations; the saga DB retains its own existing lifecycle. Operations, schema,
row/vector contents, warmup and timer remain shared across implementations.

Use fresh disposable storage for each process so retained UN/SN and saga state
do not cause idempotent skips or recovery work in another process. Keep backend
configuration, durability settings and database versions matched. Store samples
include real backend completion and scheduler waits; they do not isolate encode
CPU cost. PostgreSQL/schema creation and connection setup are outside timing.
Allocation snapshots still exclude SQLite/libpq/system and ZmVHeap allocations.
`external70/backend.py` under the repair evidence records exact environment,
artifacts, commands and bounded-process cleanup for these comparisons.

`ZDB_REPAIR_STORE_ONLY=1` stops after the existing direct-store/codec phases.
It omits full saga and scheduler controls, and its results must be labeled
accordingly. This selection isolates direct-store measurement when another
phase fails; it does not convert that failure into acceptance of the omitted
phase. The recorded baseline full sqlite3 saga failure is retained in external70.
