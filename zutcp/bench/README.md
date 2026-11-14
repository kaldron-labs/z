# Short FIX comparison (OQ7)

One `ZiMultiplex` / `ZiConnection` / `ZiRx` / `ZiTx` application supplies both
choices. `kernel` leaves the plugin unset and uses ordinary TCP sockets. `native`
installs the existing `Zutcp::Stack` before registration of application allocators.
Each run has two endpoints of the selected kind. Independent real-kernel-peer
interoperability is covered separately in `zutcp/interop`.

## Shared FIX workloads

All records have real SOH delimiters, BeginString, BodyLength, directional
MsgSeqNum/SenderCompID/TargetCompID, bounded representative tags and modulo-256
CheckSum. Header SendingTime and order TransactTime use fixed valid corpus text;
they are payload fields, never benchmark clocks or RTT measurements. Full expected
records and sequence are validated across arbitrary TCP receive splits. All six
fixed records fit within 256 bytes, below a normal Ethernet TCP payload. These
are representative FIX application records, not a FIX session engine or an
exchange/order execution service. No Zfix/Ztcp/Ztls dependency is needed.

| `BENCH_WORKLOAD` | Sender application record | Receiver response |
| --- | --- | --- |
| `order` | NewOrderSingle-shaped `35=D`, symbol, side, quantity, price and ClOrdID | Distinct `35=8` ExecutionReport-shaped record with matching ClOrdID, ExecID and status |
| `marketdata` | `35=V` MarketDataRequest-shaped record with MDReqID, entry type and symbol | Distinct `35=W` Snapshot-shaped record with matching MDReqID, entry price and size |
| `updates` | Fixed `35=X` IncrementalRefresh-shaped update with symbol/price/size | Same update fields with reverse SenderCompID/TargetCompID as application feedback |

The request and response corpora may differ within a workload, but are
byte-identical for kernel/native comparisons. Directional sequence and request
identity are checked for every response. The updates workload intentionally
measures ordered update-shaped delivery with application feedback, not pure
one-way dissemination latency or a full subscription/multicast service.

Each persistent connection warms, drains its warmup pipeline, then measures a
bounded number of requests. `pipeline=1` is persistent ping-pong. A larger value
bounds outstanding offers **per connection** and continuously replenishes the
stream on each complete response. `BENCH_BURST_SIZE>0` deliberately releases a
bounded application burst, then schedules the next using the existing
multiplexer scheduler on the same Rx owner and existing clock. No owner sleeps,
new worker, transport timer, backend or protocol batching delay is introduced.
A burst larger than the pipeline remains limited by feedback/available slots.

The nominal inter-burst interval is `BENCH_BURST_US` from the current burst's
start. If servicing/completing the burst runs beyond its next deadline, the next
burst is deferred by one interval; missed bursts are not replayed as an unbounded
catch-up loop. Burst/late-burst counts and actual pipeline high water are recorded.
At most one application timer exists per connection. Its callback uses a raw
owner pointer and a serial checked before changing state. The application
preallocates at most 128 owner-reference slots and retains each connection
through the existing multiplexer stop barriers. Measurement completion and
disconnect delete/invalidate timers on Rx. Main-thread shutdown first posts a
bounded Rx cancellation/flagging pass, then existing stop barriers drain
already-dispatched work before the application releases owners. Shutdown also runs from the App
destructor, including failure paths, before member/provider destruction. Timer
callbacks perform no owner-reference churn and introduce no ownership cycle.
Warmup and measurements use the same selected shape, and intentional gaps remain
inside throughput observations. Connections warm independently; throughput uses
the envelope from the earliest measured start to the last measured response.

Latency starts before common application formatting/allocation/enqueue and ends
on the complete validated response on Rx. It is RTT, not one-way latency. Local
`ZiTx` completion is never called a peer acknowledgment. The client requests
ordinary FIN after the last exact response. The server waits for that EOF and
uses the ordinary peer-close drain. Both await normal disconnect
before stopping. Native stopped snapshots also verify connection-slot,
retained-Rx and physical-Tx/control-credit drains.

## Registration and build scope

`Makefile.am` links only the existing Zu/Zm/Zt/Ze/Zf/Zi/Zxdp/Zutcp libraries.
`configure.ac` registers `zutcp/bench/Makefile`, and the module traversal includes
`bench`. Build the explicit directory after its library closure. No dependent
library or application fixture run is introduced.

Keep active ASan correctness inputs intact. Use separate clean snapshots and
output/prefixes of **current** source for plain Clang release and
`z.config -L -p` profiling. Do not copy objects, `.libs`, `.la` or dependency
metadata from ASan. Lower-library flags must match. Use the established scoped
library closure/order; avoid deferred application modules. Scripts never build
or configure. First compile/run `--selftest`, then small functional counts with
zero/positive warmup, continuous and burst shapes, multiple connections and all
three corpora. Use the `check` build-purpose label for these functional runs. Timing from the
correctness build is not a release observation. The label does not inspect
compiler flags; the selected build/closure still needs to be recorded.

## Private-veth runs and complete workload matrix

Use normal fixture privileges (for example sudo) and the intended build's
absolute executable. For one configured row:

```
BENCH_WORKLOAD=order BENCH_MESSAGES=10000 BENCH_WARMUP=1000 BENCH_PIPELINE=1 \
  bash run-compare.sh /absolute/build/zutcp/bench/zutcpbench /absolute/out/run1 release
```

For the full short-FIX matrix, `run-matrix.sh` runs each corpus through all four
application shapes, both families and both transport choices:

| Shape | Per-connection pipeline | Connections | Warmup | Measured per connection | Burst |
| --- | ---: | ---: | ---: | ---: | --- |
| `pingpong` | 1 | 1 | 1,000 | 10,000 | Continuous |
| `pipeline` | 8 | 4 | 1,000 | 100,000 | Continuous |
| `sustained` | 32 | 4 | 1,000 | 250,000 | Continuous |
| `burst` | 16 | 4 | 1,024 | 100,000 | 16 offers, nominal 1 ms interval |

```
bash run-matrix.sh /absolute/build/zutcp/bench/zutcpbench /absolute/out/matrix release
```

These are reproducible workload settings, not numerical pass/fail performance
targets. `BENCH_WORKLOADS` and `BENCH_SHAPES` select matrix subsets;
`BENCH_MESSAGES`/`BENCH_WARMUP` can shorten every row for functional validation.
Use separate output roots for each repetition and build. OQ7 measurement/profile
evidence is not complete merely because the executable builds.

`run-compare.sh` unshares a fresh namespace per backend/family run;
`private-veth.sh` refuses the initial host namespace, creates two private endpoint
namespaces, and cleans up child processes/links on exit. Both scripts refuse
existing output paths. Only task-owned veth settings/neighbors are changed.
They record effective offloads, kernel options, before/after link/device/network
counters, wrapper and real ELF hashes, and workload/configuration. There is no
physical interface selection or physical-NIC/zero-copy advantage claim.

For custom rows, shared overrides are `BENCH_WORKLOAD`, `BENCH_BURST_SIZE`,
`BENCH_BURST_US`, `BENCH_MESSAGES`, `BENCH_WARMUP`, `BENCH_PIPELINE`,
`BENCH_CONNECTIONS`, `BENCH_RESERVE`, `BENCH_SPIN`, `BENCH_CLIENT_RX_CPU`,
`BENCH_CLIENT_TX_CPU`, `BENCH_SERVER_RX_CPU`, `BENCH_SERVER_TX_CPU`,
`BENCH_FAMILIES` and `BENCH_BACKENDS`. Defaults select four CPUs in the allowed
affinity set, reusing CPUs when fewer are available. Explicit placement is
recommended. Both modes use the same three-thread scheduler, Rx=1, Tx=2, queue
65536, wait timeout 1 ms, Rx quantum 8, scheduler quantum 1 us, low-latency mode
false and selected spin count. First Rx/Tx turns record actual CPUs. Kernel
`TCP_NODELAY` is set/read back. Native retains its immediate whole-original sender.

`BENCH_RESERVE` is the **requested** network/Tx allocator count and native ingress
packet count, not equal physical pool mechanics. The builtin kernel allocator
ignores factory count. Both use identical application buffer sizes/alignment and
a bounded application pipeline. The existing mx `rxBufSize` setting means socket
bytes for kernel TCP and ingress frame count for Stack: kernel runs request 1 MiB
socket buffers; native runs request `BENCH_RESERVE` ingress frames. These units
are reported explicitly rather than passing a byte count as a native frame count.
Actual kernel socket buffers and
native default retention/control/reserve settings, stopped counters and pressure
are reported separately. Do not add a dummy kernel pool or label unlike backing
mechanisms equal. Larger pipelines must retain all refusal/stall observations.

Owners are released after stop and before reporting native pool drain.
Client `samples.csv` contains preallocated bounded raw RTT samples (at most two
million across connections). Sorting/reporting/files occur after owner shutdown.
Summaries include p50/p99/max, throughput and separate request/response FIX bytes,
excluding transport headers. Internal `process_resources` stops after owner
barriers before sorting/report/CSV and object destruction; it includes setup and
warmup. External `/usr/bin/time -v` covers the entire command, including analysis
and CSV. Neither is labeled measurement-interval-only CPU. Namespace counter
deltas include setup/teardown. `BENCH_CAPTURE=1` enables an equally configured
diagnostic tcpdump, whose overhead must be labeled. Default runs do not capture;
AF_PACKET visibility of native output is not assumed complete.

Repeat with the same row/settings and alternate order using
`BENCH_BACKENDS='native kernel'`. Keep raw runs. No latency/throughput/CPU cutoff
or automatic speedup verdict is provided.

## Separate `-pg` observations

Pass the separately instrumented executable and `pg` label to the same scripts,
with a fresh output root. The label records intent, not compiler-flag detection.
Each endpoint changes to its own output directory before execution, preserving
separate `gmon.out` files; `GMON_OUT_PREFIX` additionally separates PIDs where
supported. Normal exit is required. A timeout/kill is a failed observation, not a
profile to merge. Preserve symbols/build closure, settings, raw samples and
role-specific profiles with gprof diagnostics.

Collect plain release rows first, then representative order, execution and
market-data profiles. `-pg` overhead is not plain release timing. gprof omits
kernel TCP work and may incompletely attribute shared-library/inlined code;
verify DSO coverage before claiming a libZutcp hot path. Compare process/system
CPU, drops and pressure alongside profiles; user-space self-time alone cannot
establish total CPU advantage. Examine profiles before optimizing, then repeat
the same row afterward. No compiler-PGO input, new profiling backend, physical
NIC claim or hard performance gate is introduced.

## Explicit mixed-role functional checks

Default comparison rows remain kernel/kernel and native/native. To exercise the
same bounded concurrent/bursty FIX application against an independently
implemented **kernel TCP transport**, select explicit mixed roles:

```
BENCH_BACKENDS='native-client native-server' BENCH_MESSAGES=64 BENCH_WARMUP=16 \
  BENCH_SHAPES='pipeline burst' bash run-matrix.sh \
  /absolute/build/zutcp/bench/zutcpbench /absolute/out/mixed check
```

`native-client` pairs an actual Stack client with an ordinary kernel-socket
server; `native-server` reverses the roles. Each connection checks the plugin
and null/non-null kernel socket identity, and output records actual per-role
backends. The script verifies those role-specific result records and labels
mixed runs `performance_comparison=0`. The `check` purpose also disables that
claim for either same-backend selection. They are functional interoperability
observations, not a matched performance comparison or a kernel/kernel substitute
for native TCP. The application/framer is shared; the kernel's TCP state machine
is independent of Zutcp. Existing independent POSIX utilities remain separate
interop evidence. Concurrent short/bursty corpora are exercised here without a
new peer implementation. Controlled loss/reorder scenarios remain the existing
interop fixture's responsibility and are not claimed by this no-fault runner.


## Optional middle-bridge wire diagnostics

`BENCH_TOPOLOGY=bridge BENCH_CAPTURE=1` keeps the same application arguments,
corpus, scheduler/affinity, reserves and endpoint roles, but inserts a private
middle Linux bridge between two one-queue veth pairs. Default `direct` topology
is unchanged. Root-namespace bridge egress taps produce `to-client.pcap` and
`to-server.pcap`, with separate closed `capture-client.log`/`capture-server.log`
zero-drop footers. They observe ordinary TCP before the endpoint AF_XDP ingress,
including native/native traffic that endpoint AF_PACKET taps can miss. No fault
is injected by this plain bridge mode. The topology and capture location are
recorded explicitly; compare like topologies/settings and never relabel bridge
capture overhead as the default direct run. All bridge/ports/namespaces belong
to the fresh outer namespace and are removed during cleanup. Normal completion
closes both capture processes before publishing the complete marker.

## Independent capture analysis

After a captured run closes, use `python3 analyze-wire.py RUN_DIRECTORY` to
check the TCP streams, FIN acknowledgments, complete FIX records and application
readbacks. Bridge mode checks both direction-specific captures and supports
native/native visibility. Endpoint capture supports kernel and mixed runs;
native/native endpoint captures are explicitly excluded. The CLI rejects RSTs.
Run `python3 analyze-wire-selftest.py` for the analyzer's positive and negative
controls. Capture evidence establishes functionality, not physical-NIC behavior.

### Selected recovery/control runs

`BENCH_RECOVERY=loss|reorder` opts into one bounded first-request fault on the
middle bridge. Use `BENCH_TOPOLOGY=bridge BENCH_CAPTURE=1`, one connection, no
warmup, and pipeline/message counts of at least 16. The default remains `none`.
These are representative recovery profiles, not an additional full matrix or a
performance threshold. Keep corpus, CPU, reserve and scheduler settings identical
when comparing matched backends; mixed roles remain functional observations.

For example, run a matched order/execution loss pair and mixed market-data
reordering with a previously built binary and fresh output directories:

```sh
sudo env BENCH_TOPOLOGY=bridge BENCH_CAPTURE=1 BENCH_RECOVERY=loss \
  BENCH_WORKLOAD=order BENCH_MESSAGES=128 BENCH_WARMUP=0 BENCH_PIPELINE=16 \
  BENCH_CONNECTIONS=1 BENCH_BACKENDS='kernel native' BENCH_FAMILIES=4 \
  zutcp/bench/run-compare.sh /absolute/path/zutcpbench /new/path/fix-loss release
sudo env BENCH_TOPOLOGY=bridge BENCH_CAPTURE=1 BENCH_RECOVERY=reorder \
  BENCH_WORKLOAD=updates BENCH_MESSAGES=128 BENCH_WARMUP=0 BENCH_PIPELINE=16 \
  BENCH_CONNECTIONS=1 BENCH_BACKENDS='native-client native-server' BENCH_FAMILIES=6 \
  zutcp/bench/run-compare.sh /absolute/path/zutcpbench /new/path/fix-reorder release
```

The unchanged endpoint programs produce and parse every FIX record. A read-only
observer first captures the actual server SYNACK; the bridge drops that carrier
while the fixture installs an exact learned first-DATA sequence filter. The
observer then forwards that same SYNACK unchanged. It generates no peer ACK,
sequence position, option or application data. Setup hold time is part of this
profile and can affect the endpoints' RTT estimates; compare these fault runs
separately from ordinary latency results.

Loss drops the first actual request original, then removes its selector only
after a positive qdisc drop count. Recovery must contain a source-captured retry
that actually reaches the receiver. Reorder delays that original 20ms without
loss, requires a later DATA range to arrive first, and then requires the exact
source frame to arrive; queued originals must drain before capture teardown.
The independent analyzer still requires all FIX records, byte coverage and both
FIN acknowledgments. Source/destination captures, observer overflow count,
qdisc state and isolated kernel counters accompany `recovery-evidence.json`.
Only reordering may classify a post-close bare kernel RST: each requires both
FIN ACKs first, an identical pre-close source DATA copy delivered after close,
a unique trigger and actual source RST, and matching kernel OutRsts deltas.
Other resets remain errors. No active native reset is silently ignored.

Run `python3 -B zutcp/bench/bench-recovery-selftest.py` for pure evidence-boundary
vectors (no sockets). These complement the independent FIX analyzer tests;
they do not substitute for running the real private-veth profiles. Reuse `pg`
instead of `release` only with a separately built profiling closure, retaining
the per-role profiling and DSO-attribution limitations described above.
