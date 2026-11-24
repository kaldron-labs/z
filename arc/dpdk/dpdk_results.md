# DPDK implementation verification

## Clang release baseline

The run used Ubuntu x86-64 with Linux 7.0.0, Clang 21.1.8, and
`z.config -L`. The initial configuration used `-K` and a clean build.
The top-level dependency order was repaired after the clean build reached
Zutcp consumers before their Ztcp library. Zutcp follows both Ztcp and
Ztls in the repaired traversal.
The rebuild also exposed an ambiguous `Frame` in the gRPC interop client.
Removing its redundant namespace directive before the QUIC headers keeps
gRPC names out of the header's lookup scope.
The Zdb saga test needed a mutable callback to call its captured
`ZmBlock<bool>` completion.

| Phase | Unit suite | TAP files | TAP tests |
| --- | --- | ---: | ---: |
| 1 | zu | 42 | 974 |
| 1 | zm | 40 | 326 |
| 1 | zt | 17 | 394 |
| 1 | ze | 1 | 1 |
| 1 | zf | 12 | 352 |
| 1 | zi | 22 | 157 |
| 1 | zv | 3 | 38 |
| 2 | ztcp | 3 | 7 |
| 3 | zdbus | 7 | 26 |
| 3 | ztls | 19 | 94 |
| 5 | zfix kernel baseline | 30 | 399 |

All listed suites passed. Each test directory's default target ran before its
`test` target. Recursive builds shared one eight-slot GNU make jobserver.

## DPDK configuration

The host provides DPDK 25.11.3, including the stock `net_ring` and `net_pcap`
PMDs. Explicit `--with-dpdk=invalid` failed with the yes/no diagnostic.
With a pkg-config search path containing the baseline dependencies and omitting
`libdpdk.pc`, explicit `--with-dpdk=yes` failed at the DPDK dependency check.

Checkpoint 1 passed both complete top-level Clang release builds.
The disabled `libZpkt` had no DPDK dynamic dependency. The enabled Zpkt
unit target passed 15 top-level TAP tests across five programs, including
the explicit EAL subprocess driver. The independent Release QuickFIX SDK
built with Clang under the same eight-slot jobserver as the enabled build.

## Diagnostic runs

The preliminary DPDK runs passed the explicit EAL, device lifecycle, pool,
ring packet, pcap packet, wake and duplex tests. The pcap fixture accepts
microsecond and nanosecond classic capture headers and checks exact frame
bytes. The first Zutcp TCP run exposed a redundant ingress retention
reference that prevented the typed receive stub from taking ownership of
unique native backing. The ownership repair passed 134 diagnostic assertions
across IPv4 and IPv6 TCP, UDP and mixed traffic.

The baseline AF_XDP regression run passed 89 tests across the five native
Zutcp drivers and eight more across the Ztcp and Ztls application drivers.
The standalone Zpkt packet, classifier, wake and loopback runs passed nine
top-level TAP tests. A separate Zfix AF_XDP diagnostic covered 76 canonical
TCP and TLS process cases with the pinned Debug QuickFIX SDK.

The configured release results appear below. Raw logs and benchmark records
are under `/home/ubuntu/rebuild-evidence/dpdk-20261119`.

## Configured DPDK release

| Check | TAP files | TAP tests | Result |
| --- | ---: | ---: | --- |
| Disabled Zpkt with the DPDK selector | 4 | 14 | Pass |
| Enabled Zpkt unit target | 6 | 17 | Pass |
| DPDK packet, wake and loop drivers | 3 | 8 | Pass |
| Final Zutcp unit target | 22 | 319 | Pass |
| Zutcp DPDK integration | 1 | 1 | Pass, 133 nested TAP assertions |

The disabled selector reported a structured unsupported-build error. The
enabled selector reported a structured error before EAL initialization. The
device tests exercised rollback, exclusive binding, both pool-set retry paths
and reuse across restart. The software fixtures verified exact bytes, native
Tx retirement and Rx pool provenance. The Zutcp fixture verified typed
receive delivery and ownership handoffs through the real multiplexer.

The final AF_XDP run passed all four packet cases, classifier, wake and
loopback, the five native Zutcp drivers (89 TAP tests), the Ztcp and Ztls
application drivers (eight TAP tests), and both UDP and mixed-traffic cases
over IPv4 and IPv6. All four DPDK checkpoints rebuilt the top-level dependency
closure without compiler warnings. The final top-level `make -j8` and
`git diff --check` also passed.

## Zfix interoperability

The kernel integration target passed 130 TCP and 135 TLS tests, two demo
tests and one native utility I/O test covering 18 separate-process cases.
The AF_XDP integration target passed 125 TCP and 133 TLS tests across six
drivers per transport. Direct kernel socket probes remain in the kernel suite.
The Release QuickFIX dependency, codec and load checks passed. Each canonical
matrix passed five TAP groups and all 76 process cases over TCP and TLS. The
AF_XDP matrix covered Zfix in both QuickFIX pairings, native Zfix on both
ends, and the QuickFIX kernel control in the same namespace topology.

The AF_XDP socket fixtures exposed two test harness lifetime issues. The TLS
silent peer needed the socket receive allocator and a retained reference to
the received ClientHello. The native recovery fixture needed to retain the
wire buffer through the failover check. Valgrind through libtool helped locate
the failing paths. An intermittent late-bind teardown timeout did not recur
in focused diagnostics. The fixture now waits for the client's socket bind
and the mux to queue the server's bind before releasing the server stop gate.
The rebuilt TLS session driver passed all 67 cases in a separate rerun after
the full AF_XDP suite passed.

## Zfix transport comparison

The same Clang release `zfixbench` binary used IPv4 over a private veth pair
with application work colocated on the respective I/O owners. Each session
issued 1,000 warmup orders and 10,000 measured orders at pipeline depth 64.
Rates are aggregate completions per second. Latencies are request round trips.
All runs reported exact completion counts and drained every connection.

| Transport on both peers | Connections | Orders/s | p50 µs | p99 µs |
| --- | ---: | ---: | ---: | ---: |
| Kernel TCP | 1 | 114,226.8 | 559.7 | 673.3 |
| Zutcp/AF_XDP | 1 | 133,177.6 | 479.3 | 529.1 |
| Kernel TCP | 4 | 133,526.0 | 1,922.1 | 2,347.7 |
| Zutcp/AF_XDP | 4 | 127,685.5 | 2,000.6 | 2,159.0 |

## Zfix and QuickFIX pairing comparison

These are separate-process, scalar FIX order workloads over kernel TCP.
Each session used 1,000 warmup orders, 10,000 measured orders and up to
64 in flight. Each row represents three runs. Throughput is the median
shared-window completion rate. The latency value is the highest session
terminal p99 across those runs. All 24 runs completed the expected orders
without errors or timeouts.

| Client / server | Connections | Median orders/s | Worst terminal p99 ms |
| --- | ---: | ---: | ---: |
| Zfix / Zfix | 1 | 72,198.6 | 1.861 |
| Zfix / Zfix | 4 | 87,642.5 | 4.196 |
| Zfix / QuickFIX | 1 | 51,640.2 | 1.477 |
| Zfix / QuickFIX | 4 | 43,746.5 | 6.652 |
| QuickFIX / Zfix | 1 | 58,633.5 | 1.426 |
| QuickFIX / Zfix | 4 | 53,050.4 | 5.984 |
| QuickFIX / QuickFIX | 1 | 57,284.8 | 1.608 |
| QuickFIX / QuickFIX | 4 | 64,327.6 | 5.978 |
