# Release acceptor performance

QuickFIX-to-zfixd was slower than QuickFIX-to-QuickFIX in a clean release
build as well as in the historical ASan comparison. Two fixture choices
explain much of the observed gap: extra network/application thread handoffs and
execution reports that cost more for the QuickFIX receiver to parse.

## Build and workload

The build uses `./z.config -L -P /opt/z`, Clang 21, `-march=x86-64-v2 -O3 -g
-DNDEBUG`, without ASan. `-P` disables the unavailable PostgreSQL dependency.
Dependency rebuilds are limited to `src/` in `zu zm zt ze zf zi zpb zfb zv zrl
zdbus ztcp ztls`; all Zfix directories are rebuilt, with no modules after Zfix.
Builds use `-j8`. The pinned, patched QuickFIX 1.16.0 SDK is also rebuilt with
Clang in Release (`-O3 -DNDEBUG`). Loaded libraries and peer executables are
checked for ASan/LSan references. Zfix integration helpers compile the shared
Zi residue fixture locally, avoiding a dependency on a previously instrumented
`zi/test` library.

Each process case uses 10,000 measured orders and 1,000 warmup orders per
session, 64 orders in flight per session, one or four sessions, and three
repetitions. Both transports run on loopback with separate client/server
processes and verified TLS. All four pairings use the same scalar order model
and bounded history settings. Native typed validation and QuickFIX canonical
dictionary validation retain their distinct implementation costs.

Rates are medians of shared-window throughput across repetitions: all measured
completions divided by the interval from the first session's measured start to
the last session's measured finish. Ack p99 is the worst individual-session
p99 across repetitions. Quantiles are never pooled. Process CPU includes
startup, warmup and shutdown. The shared eight-CPU Linux host uses an Intel
Xeon Platinum 8488C; these results describe this workload and host.

## Thread handoffs

The original native fixture used network Rx/Tx threads 1/2 and explicitly
selected application Rx/Tx threads 3/4. The QuickFIX fixture uses its socket
worker and an application scheduler. In the native acceptor's CPU profile,
`ZmRing_::Blocker::wait` accounts for 17.6% inclusive samples, wake for 8.3%,
and `ZmScheduler::tryPush_` for 3.9% flat samples. Inclusive percentages can
overlap and must not be added as disjoint costs.

Changing only server parameters from `{&mx, "3", "4"}` to `{&mx}` raised
four-session TCP throughput from 49,121 to 57,227 orders/s and TLS from 38,447
to 43,808. Median server CPU fell from 1.989 to 1.292 seconds for TCP and from
2.231 to 1.252 seconds for TLS. The wire and client were unchanged; threads
3/4 remained configured but idle. All six controlled cases passed.

The acceptor-only experiment uses two active workers with normal Hub
parameters: application Rx shares network Rx, and application Tx shares network
Tx. Rx and Tx ownership remain separate, with no new locks or engine changes.

A complete intermediate matrix also applied this owner assignment to the
native client. It improved native-to-QuickFIX throughput but reduced
native-to-native throughput: four-session TCP fell from 80,588 to 63,346
orders/s, with worst Ack p99 rising from 4.079 to 5.642 ms. Lower server CPU
does not imply lower end-to-end latency. Restoring the client's application
workers did not recover native-to-native throughput (58,836 TCP and 56,088 TLS
orders/s). Keeping server Rx separate and sharing only server Tx also failed
to recover it (57,311 TCP and 55,486 TLS orders/s).

Restoring the original four-worker assignment on both peers, with only the
wire change, recovered 79,000 TCP and 72,873 TLS orders/s in the controlled
four-session runs. QuickFIX-to-native still improved to 57,218 and 42,881
orders/s. The final fixture therefore retains its original worker assignment.
The topology changes are experimental results, not a universally faster
default. The extra pipeline stages can improve native-peer latency despite
their CPU cost; they are expensive for the QuickFIX pairing. Deployments can
select application Rx/Tx owners through the existing Hub parameters according
to their own workload.

## QuickFIX receiver work

Captured ordinary native Ack/Fill bodies contained 16 fields, including the
unnecessary `41=0`, and used declaration order. QuickFIX emitted 15 fields
without OrigClOrdID, in ascending tag order. At the pinned SDK revision,
`FieldMap::lookup()` switches from linear search to binary search at 16 fields.
`Message::setString()` also sorts its decoded header, body and trailer maps.
An extra field and the original field order therefore affect the receiver as
well as wire length. The captured native Ack was 204 bytes versus 199 bytes
for QuickFIX with the same timestamp precision and numeric representations.

A standalone SDK experiment parses one captured Ack, validates it against the
canonical dictionary, calculates length and retrieves the same fields used by
the receiver. Each process decodes 100,000 reports; values below are the median
of three **mean process CPU times**, not latency quantiles. Numeric conversion,
fixture arena copies and scheduler transfer are excluded.

| Input | CPU ns/report |
| --- | ---: |
| Original native | 4,604 |
| Native, scalar header/body sorted | 4,212 |
| Native, omit `41=0` | 4,192 |
| Native, both changes | 3,806 |
| Captured QuickFIX Ack | 3,796 |

The production fixture change declares only the scalar Report fields in tag
order and uses a render facet to omit tag 41 when `original == 0`. Nonzero
original IDs on cancel/replace reports and meaningful zero fields are retained.
Header generation is unchanged; the isolated decoder's sorting variant also
sorts the header. No runtime sorter or flat tag map is added. Order/Party group
declarations and scope-aware parsing are unchanged. Binary Length/Data
adjacency is preserved.

With the server owner change plus the report change, the controlled
four-session runs reached 62,897 TCP and 49,771 TLS orders/s. Median server
CPU was 1.211 and 1.219 seconds respectively. All six cases passed. These
measurements precede removal of idle server workers. The wire-only and
owner-only observations remain separate from the complete comparison.

## Final comparison

The final 48 timing cases passed: 120 session runs, 1.2 million measured
orders, zero errors, zero unfinished orders and successful peer exits.
Runs were sequential, with no concurrent build or integration suite.
Executable, library and source hashes were rechecked after measurement.
The original four-worker topology is unchanged on both native peers.

| Client-server | Transport | 1 session before orders/s | 1 session final orders/s | 4 sessions before orders/s | 4 sessions final orders/s |
| --- | --- | ---: | ---: | ---: | ---: |
| native-native | TCP | 70,415 | 71,140 | 80,588 | 80,874 |
| native-native | TLS | 63,133 | 62,599 | 73,689 | 73,327 |
| native-quickfix | TCP | 45,077 | 45,823 | 45,080 | 45,579 |
| native-quickfix | TLS | 37,819 | 39,301 | 42,871 | 45,926 |
| quickfix-native | TCP | 48,711 | 55,268 | 49,121 | 55,999 |
| quickfix-native | TLS | 33,372 | 33,093 | 38,447 | 46,262 |
| quickfix-quickfix | TCP | 54,670 | 56,875 | 60,086 | 59,036 |
| quickfix-quickfix | TLS | 41,207 | 35,976 | 49,281 | 50,110 |

QuickFIX-to-native improves by 14% TCP and 20% TLS at four sessions,
while native-to-native throughput is preserved. It still trails the final
QuickFIX-to-QuickFIX reference by 5% TCP and 8% TLS. The shared-owner
configuration closes more of that gap but regresses the native pairing.
There is no universal topology win. Single-session TLS varies between
snapshots, including the unchanged QuickFIX reference; the result does not
establish a wire-only speedup for every scenario.

| QuickFIX client | Sessions | Original zfixd Ack p99 ms | Final zfixd Ack p99 ms | Final QuickFIX Ack p99 ms |
| --- | ---: | ---: | ---: | ---: |
| TCP | 1 | 1.577 | 1.454 | 1.586 |
| TCP | 4 | 6.224 | 6.006 | 5.969 |
| TLS | 1 | 2.290 | 2.252 | 2.292 |
| TLS | 4 | 7.742 | 7.322 | 6.832 |

Native-to-native four-session worst Ack p99 is 4.528 ms TCP and 4.318 ms
TLS, versus 4.079/4.004 ms originally. Similar throughput does not establish
better tail latency. These short local tests include scheduler and shared-host
variation; the archive retains all individual quantiles and repetitions.

## Validation

The release unit gate passed 29 binaries and 354 scopes, including report
field presence and cancel/replace ID preservation. TCP/TLS integration
passed 262 TAP scopes, including native process workflows. Independent
codec/load/SDK checks and all 76 canonical cross-engine process cases passed.
After restoring the original owners, the native TCP/TLS process gate passed
again. Repository-owned final builds are warning-free; loaded peer files
contain no ASan/LSan references. Release-only assertion side effects in two
test fixtures were corrected; the core Tx change only marks an assert-only
result unused. No Rx parser or framing algorithm is changed by this repair.

## Evidence

The [release evidence archive](2026-10-31-release-acceptor.tar.xz) contains
original and final full matrices, controlled experiments, manifests with
commands/source/executable/library hashes, individual JSON results, process
logs, decoded-wire inputs, decoder source/results, raw CPU profiles and
symbolized summaries, build and test logs, and the source patch.

Archive SHA-256:
`1ce476d63fd539865c2f15af1f3ebb6a00d234b6fc88ba0c4c6a085d70c00b5a`.

CPU sampling used gperftools `libprofiler` at 1KHz, preloaded only into
diagnostic processes, without replacing malloc. Both profiled pairings
completed 400,000 measured orders each (100K/session, four sessions, TCP).
Kernel perf events were unavailable. The included profile reader decodes the
legacy gperftools format and resolves ELF symbols; profile totals match the
profiler's recorded sample counts. Profiles were symbolized against the
original binaries before rebuilding. Ordinary timing matrices do not preload
the profiler or run under strace. Captured profiles and CPU fractions are
diagnostic evidence, not the ordinary throughput measurements.
