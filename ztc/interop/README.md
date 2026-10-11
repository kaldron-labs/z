# Telemetry FlatBuffers interoperability

Explicit target, outside ordinary production builds:

```
make -C ztc/interop -j4
make -C ztc/interop test
```

Requires canonical FlatBuffers >= 24, `flatc`, and already built owning
libraries. Regenerate through `z.config` with the active build settings;
the current release build uses `./z.config -c -L /usr`. Keep compilation
sequential with other module builds and within the four-job limit.

`telemetry.fbs` is an independent canonical oracle for control messages,
shutdown telemetry, and the nested payload envelope. Its union deliberately
covers only shutdown at protocol tag 14; it does not purport to describe all
telemetry alternatives. Full `Ztc::Payload` BFBS and FBS exports are imported
and compiled by the canonical library, with object/field/default comparison.
Payload tests exchange requests in both directions and copy a canonical
shutdown span unchanged into a new routing envelope. They check empty versus
absent fields, present default preservation, sequence mutation, and nested
corruption rejection despite a structurally valid outer envelope.

Initial M2: built with the then-retained Clang debug configuration and passed all four
TAP groups. Schema parsing explicitly selects the C++ target, which supports
inline structs in unions. Connection flags now have their own enum name.

The installed canonical 25.12.19 `FieldDef::Serialize` uses signed
`StringToInt` for unsigned defaults and serializes `ulong` values above
`INT64_MAX` as zero. For `ulong`, the comparison reads the canonical parser's
constant before that lossy BFBS serialization; other integer defaults are
compared directly in BFBS. ZiFB's emitted BFBS retains the correct bit pattern.
Both native BFBS roots pass canonical verification and deserialization.

Native telemetry unit tests passed 7 binaries / 28 top-level tests. Agent
provisioning, complete WSS routing/restart and cluster failover fixtures passed.
The cluster fixture now gives the promoted standby the same full-snapshot
capacity used by the restarted single hub; the initial active hub retains its
small queues for the explicit overflow check. The transport Valgrind rerun passed both hub lifetimes after repairing the
agent shutdown drain. The full 2048-agent/32-client network workload failed
during TLS admission with `bad_record_mac`, before any telemetry samples were
sent. The fixture's failure teardown now drains all started connections before
releasing links and starts the normal Z logger to retain TLS diagnostics. The
full-scale rerun remains outstanding; no throughput or latency pass is claimed.

The diagnostic rerun admitted all 2048 agents and exited its failure path
without the TLS teardown assertion. It sent no telemetry: a front-end closed
while authorization was pending. The hub's `error=-1` log denotes
`ServiceError::OK`, but the link was already closed after receiving a second
unauthenticated request. The load peer had sent all 32 subscriptions immediately.
It now waits for the first subscription ACK before pipelining the other 31,
using protocol readiness rather than another delay or a larger pending-input
allowance. The next debug run submitted all 786,432 samples but received only two before
timing out. GDB found the hub Rx thread blocked in `ZiMultiplex::writeWake`;
the wake pipe write descriptor had blocking flags. A focused burst regression
showed that merely dropping wake bytes on `EAGAIN` leaves polling callbacks
without matching wakes. The Linux path now uses one nonblocking semaphore-mode
`eventfd` to preserve one wake per queued polling callback. The focused
131,072-callback regression passed, including shutdown. Final Valgrind checks
of that test and `ZiFBTest` report zero errors and zero lost blocks. The
control-message test also found trailing uninitialized UDP TOS padding; the
transmit control length now covers only initialized bytes. Full release
workload verification remains pending.

Performance acceptance is release-only. Debug workload runs check admission,
delivery counts, and teardown; the load peer enforces the fanout and delivery
latency thresholds only under `NDEBUG`. Existing debug timings are diagnostic
history and establish no release regression or performance pass.


### Release telemetry comparison

After the release build, run `ztc/interop/ztcmsginterop --bench 100000 11`.
This optional mode reuses the independent request/union/envelope oracle and
`zi/interop/iobaseline.hh`, the frozen custom pooled IOBuilder. It compares
nested serialization and byte-preserving routing copies, each with fresh and
reused pooled owners, alternating codec order across samples. It records
encoded bytes, elapsed time and a varying subscription-ID checksum. Both
roots, all request fields and copied payload identity are checked before
timing. Input construction is outside timing for both codecs. The canonical
nested path follows the original `Zfb::nest` algorithm directly in the pooled
arena. Debug execution is rejected. The mode has been built and its
[initial release measurements](../../zi/interop/evidence/m2-release-initial/telemetry.csv)
are retained. Refresh these after the subsequent codec fixes and current
dependent rebuild; performance acceptance remains open.


Release compilation exposed two `ztchub` egress-hash removals inside
`ZmAssert`, which is elided in release builds. The removals now execute
unconditionally and their identity checks remain debug-only. This prevents
stale shadow-hash entries after the ready queue releases an egress. The agent
publisher-index assertion now also marks its debug-only result explicitly.
These fixes were included in the completed earlier release build. Subsequent
load results and the still-open full-workload gate are recorded below.

### Release load timing diagnostic (2026-10-08)

The workload peer reports `max_publish_lag_us` immediately before handing
an encoded frame to its Tx stream. `max_latency_us` still measures reception
against the scheduled publication time and remains the acceptance gate.
Their difference is a lower bound on some message's delay after handoff,
not a measurement of hub-only latency or a decomposition of the maxima.

The 128-agent diagnostic retained 32 clients, 1024 subscriptions, 256
publishers and all 786432 samples. It delivered every sample but failed
latency: 18319306 us end to end, 2540373 us maximum publication lag and
15 us fanout. At least 15778933 us therefore accumulated after handoff.
Logs: `/tmp/zffb-publish-lag.log` and `ztchubtest.23ebor5g`; focused retained
Clang release build: `/tmp/zffb-publish-lag-build.log` (no diagnostics).
This reduced-agent run does not replace full-workload acceptance.

A GDB queue-growth trace then found 1046528 pending hub Rx callbacks,
including TLS record self-dispatch and multiplex wake callbacks. Changing
`Ztls::connected_0` and `recvRecord` to existing `rxInvoke` avoids that
self-dispatch when I/O and application Rx share a thread, retaining queued
cross-thread dispatch. The focused release build had no diagnostics and all
16 `ZtlsBufHookTest` checks passed. The repeated 128-agent workload delivered
all samples with 8016079 us maximum latency, 2542299 us publication lag and
11 us fanout. This improves the observed run but still fails the 200 ms gate.
Evidence: `/tmp/zffb-rx-invoke-{build,tls,load}.log`; final dependent rebuilding
and full-workload acceptance remain outstanding.


### Isolated-message and rate-sweep diagnosis (2026-10-08)

The load peer now reports actual per-message distributions (nanoseconds) from
serialization start and from transport handoff through subscriber verification.
Both endpoints are in the same peer process; delivery traverses the production
agent → TLS/WebSocket → hub → TLS/WebSocket → subscriber path. Setup, admission,
sorting and reporting are outside the measured interval. `max_in_flight` checks
whether a low-rate run really had only one message outstanding. Timing storage
is allocated once before connection setup, owned by the peer's application Rx
thread; it does not change production code or wire metadata.

`ZTC_LOAD_MESSAGES_PS` overrides the publication rate without changing the
sample count. Zero retains the existing rate. `ZTC_LOAD_TURN` controls the
existing producer batch cap (default 64). Neither changes the configured SLO.
The old `max_latency_us` starts at the scheduled publication time: batches can
send messages before that time, so it is not actual per-message transit time.
The `over_200us` count reports the requested isolated-message expectation;
a successful old 200 ms workload assertion does not prove that expectation.

Initial release results, with unchanged transport configuration:

| Agents / clients / subscriptions | Messages/sec | Samples | Batch cap | Median actual µs | p99 actual µs | Maximum in flight |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 / 1 / 1 | 100 | 1000 | 64 (effective 1) | 116.077 | 156.934 | 1 |
| 2 / 1 / 1 | 1000 | 10000 | 64 (effective 1) | 96.870 | 115.657 | 1 |
| 2 / 32 / 1024 | 32768 | 786432 | 64 (effective 32) | 408.503 | 648.897 | 56 |
| 2 / 32 / 1024 | 32768 | 786432 | 1 | 69.702 | 110.000 | 31 |
| 2 / 32 / 1024 | 65536 | 786432 | 64 | 469.614 | 1055.514 | 156 |
| 2 / 32 / 1024 | 131072 | 786432 | 64 | 3168.986 | 23014.574 | 3585 |
| 2 / 32 / 1024 | 262144 | 786432 | 64 | 3109324.861 | 3285711.964 | 466521 |
| 128 / 32 / 1024 | 262144 | 786432 | 64 | 5298862.515 | 5544668.950 | 781022 |

The isolated 100/sec run exceeded 200 µs for 3/1000 messages (max 957.247 µs).
The 1000/sec run exceeded it for 1/10000 (max 858.980 µs). These are measured
percentiles, not a guarantee that every message meets 200 µs. At 32768/sec,
changing only producer batching isolates a large generator-induced delay.
At 262144/sec, essentially all of the seconds-long delay persists after
handoff: this is a saturated pipeline, not single-buffer serialization latency.
The source also shows per-frame Rx→Tx→Rx completion dispatch in the hub egress
pump. The rate sweep does not by itself isolate that component's contribution.

A separate sampled peer CPU profile attributes 19.83% to send, 18.36% to recv,
21.82% to synchronization syscalls, and 13.84% to clock reads. These are sampled
CPU shares across the run, not per-message stage timings. The profile is not
latency acceptance evidence. No production scheduling, buffering, TLS or
clocksource configuration was changed.

The host uses HPET. Its boot log reports TSC frequency skew, marks TSC unstable,
and switches to HPET. Seven independent million-call loops measure realtime
clock reads at 1198–1211 ns each. This matters for the diagnostic itself:
the initial instrumentation used four clock reads per message including receive.
A follow-up removes the redundant publication/handoff read, retaining three.
It does not replace the framework clock or use the rejected hardware clock.

Raw reports, compiler/source/binary provenance, the CPU profile text and the
clock microbenchmark are in `zi/interop/evidence/m2-e2e-latency/`. The reduced
agent counts, new test-only timestamps and prior consumer binaries mean these
are diagnostic results, not final full-workload migration acceptance.

Reproduce an isolated run with a fresh SQLite database:

```sh
ZDB_MODULE="$PWD/zdb_sqlite/src/.libs/libZdbSL.so" \
ZDB_CONNECT=/tmp/zffb-isolated-new.db \
PYTHONPATH=zi/itest:zum/itest ZTC_LOAD=1 \
ZTC_LOAD_AGENTS=2 ZTC_LOAD_CLIENTS=1 ZTC_LOAD_SUBS=1 \
ZTC_LOAD_PUBLISHERS=1 ZTC_LOAD_ROUNDS=1000 ZTC_LOAD_MESSAGES_PS=100 \
python3 ztc/itest/ztchubtest.py
```

For the rate sweep use clients=32, subs=32, publishers=256, rounds=3 and the
rates in the table, with a fresh database for each run. Keep performance runs
sequential, without compilation or instrumentation. The initial single-route
higher-rate trials reduced the hub queue to two frames and explicitly overflowed;
they are excluded from throughput conclusions.

Final recheck after removing the duplicate clock read: isolated 1000/sec,
10000 messages, median 96.311 µs, p99 112.654 µs, maximum 665.868 µs,
3 samples over 200 µs and exactly one message in flight. The two-agent
262144/sec run again delivered all 786432 samples but failed the old gate:
actual median 287.370 ms, p99 965.249 ms, maximum 1073.089 ms, with 75511
messages in flight and maximum publication lag 2499.327 ms. This substantial
change in the division between producer lag and transit backlog is not proof
of a production improvement: the instrumentation changed, it is one repeat,
and the workload still fails. `final-*.log` and `final-snapshot.json` distinguish
these results from the initial four-clock-read sweep and CPU profile. The
focused release rebuild passed without diagnostics; no dependent production
rebuild was required for these test-only changes.


### Direct WSS versus hub forwarding (2026-10-08)

`ztcdirecttest.py` runs the load peer against a separate-process WSS echo
endpoint in that same binary (`direct: true`, server additionally `echo: true`).
It uses the production TLS/WebSocket stack and verifies the external FlatBuffer
before echoing it. There is no Zum authentication, routing lookup, subscription
management or hub egress queue in the echo endpoint. The existing timing,
pacing, pooled buffers, sample validation and teardown paths are reused.
The echo endpoint has the hub's four-thread configuration (I/O and application
Rx=1, Tx=2); the peer retains its eight-thread configuration (I/O Rx=1/Tx=2,
application Rx=3/Tx=4). This is round-trip latency over two loopback traversals,
comparable in traversal count to agent→hub→subscriber; it is not a one-way
remote latency measurement.

All rate comparisons below use 786432 messages, 1024 logical streams, 256
publishers, three rounds and the default producer batch cap of 64. Runs are
sequential, without compilation or instrumentation, using the retained release
configuration. High-rate cases were repeated in reversed order. All messages
arrived; nonzero exits at high rates are the unchanged 200 ms scheduled-time
SLO, not lost data. The table reports actual serialization-start→verified-receive
latency, in milliseconds; ranges denote separate runs, not confidence intervals.

| Offered messages/sec | Path | Producer connections | Receiver connections | Median ms | p99 ms |
| ---: | --- | ---: | ---: | ---: | ---: |
| 32768 | Direct echo | 32 | 32 (same sockets) | 0.186 | 0.294 |
| 32768 | Hub | 2 | 32 | 0.416 | 0.657 |
| 131072 | Direct echo | 32 | 32 (same sockets) | 4088–4117 | 4537–4604 |
| 131072 | Direct echo | 2 | 2 (same sockets) | 1909 | 3371 |
| 131072 | Hub | 2 | 32 | 2.541 | 37.281 |
| 131072 | Hub | 32 | 32 | 5599 | 5848 |
| 262144 | Direct echo | 32 | 32 (same sockets) | 4007–4635 | 4453–4966 |
| 262144 | Direct echo | 2 | 2 (same sockets) | 3520 | 5301 |
| 262144 | Hub | 2 | 32 | 791–3337 | 1880–3442 |

The direct two-connection controls retain 1024 logical streams by using
`clients=2, subs=512`; all other table cases use `clients=32, subs=32`.
Hub agent count controls producer socket count. Payload byte totals establish
the remaining envelope difference: both paths transmit 144678912 bytes
(184/message). Echo returns exactly those bytes; the hub returns 201302016
bytes (256/message) after adding routing metadata. These are application
payload totals, excluding TLS/TCP/IP overhead. Both carry identical telemetry
values; this is not a claim of byte-identical network traffic or identical
connection topology.

The isolated direct preflight (one connection, 1000 messages at 1000/sec)
passed with median 77.315 µs, p99 102.667 µs, maximum 198.280 µs, and one message
in flight. Its build precedes the added byte counters; the comparison runs use
the final counter-enabled release binary. All normal echo processes shut down
cleanly. The test-only build had no warnings/errors and diff whitespace checks
pass. No production code or host settings were changed.

Interpretation: the large backlog persists when routing and hub queues are
removed, and changing the number of producer connections changes the saturation
behavior dramatically. This supports investigating the shared local networking,
TLS and per-socket scheduler path. It does not prove a kernel-only bottleneck,
a universal 250K/sec limit, or a codec regression. Echo still uses the same Z
transport and scheduling framework. The user confirmed no second host is
available, so no cross-host result is claimed. HPET timing overhead identified
above also remains present in every measurement.

Raw logs, exact comparison commands, byte counts, source/binary hashes and the
machine-readable summary are in `zi/interop/evidence/m2-loopback/`. Reproduce a
direct case with:

```sh
ZTC_LOAD_CLIENTS=32 ZTC_LOAD_SUBS=32 ZTC_LOAD_PUBLISHERS=256 \
ZTC_LOAD_ROUNDS=3 ZTC_LOAD_MESSAGES_PS=262144 ZTC_LOAD_TURN=64 \
python3 ztc/itest/ztcdirecttest.py
```

Use the same parameters with `ztchubtest.py` and a fresh SQLite fixture for the
hub path. `compare.py` and `connections.py` in the evidence directory record
the actual case order, environment and commands used in this run. They contain
local evidence-output paths; choose fresh paths when repeating them.
