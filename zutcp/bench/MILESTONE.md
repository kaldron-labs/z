# Native TCP performance milestone — 2026-11-15

This milestone records lower median application round-trip latency and higher
throughput than kernel TCP for managed small-message FIX exchanges over the
same veth path. With one persistent connection, all-native Zutcp achieves
51–55% higher throughput and 37–40% lower median latency. Lower median latency
is the primary result; the throughput improvement is an additional gain.

## Comparison with kernel TCP

Both endpoints use `zfix/bench/zfixbench` and the same ZiMultiplex/application
stack. Kernel/kernel is compared with Zutcp/Zutcp in the table below. Throughput
is thousands of completed request/reply exchanges per second; latency is
application RTT in microseconds, including formatting, allocation and enqueue.

| Connections/pipeline | Family | Kernel K/s | Zutcp K/s | Rate change | Kernel median µs | Zutcp median µs | Median change |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1/1 | IPv4 | 23.65 | 35.75 | +51.2% | 41.48 | 25.95 | −37.4% |
| 1/1 | IPv6 | 23.42 | 36.23 | +54.7% | 42.01 | 25.38 | −39.6% |
| 4/32 | IPv4 | 159.64 | 187.32 | +17.3% | 801.47 | 684.18 | −14.6% |
| 4/32 | IPv6 | 159.83 | 189.53 | +18.6% | 800.34 | 677.62 | −15.3% |
| 128/32 | IPv4 | 152.91 | 187.83 | +22.8% | 26265.33 | 21521.88 | −18.1% |
| 128/32 | IPv6 | 155.16 | 184.85 | +19.1% | 26027.66 | 22000.13 | −15.5% |

At 128 connections, all-native p99 is 26.06 ms versus kernel's 27.12 ms for
IPv4, and 23.53 ms versus 27.26 ms for IPv6. Mixed native-client/kernel-server
and kernel-client/native-server cases also run in every comparison group;
their complete results are included in the machine-readable record below.

The latest combined timer-reservation and advancing-peer-ACK changes improve
128-connection all-native throughput by 20.0%/21.3% and reduce p99 by
41.5%/32.3% versus the retained pre-change build (IPv4/IPv6). These are separate
comparisons from the kernel-relative results above. That retained build already
contains the earlier performance repairs; it is not the source-base Git commit.
The two latest changes were measured together, without isolated attribution.

## Implementation included

- Preserve original application/packet backing through receive, transmit and
  independent peer-ACK/kernel-CQ retirement; handle temporary allocation
  pressure through deterministic owner continuations.
- Send application data immediately, publish cumulative sent state before
  AF_XDP descriptor visibility, and remove deferred ACK-validation packet queues.
- Publish ordinary receive frontiers atomically and coalesce advancing peer
  ACKs into one coherent scalar snapshot and pending Tx notification. Keep
  window changes, duplicate ACKs, SACK and lifecycle exceptions on their full path.
- Hold outgoing cumulative ACKs for piggybacking, reuse timer reservations when
  logical deadlines move later, and retain immediate teardown cancellation.
- Drain AF_XDP Tx on an explicitly assigned scheduler slot with coalesced
  notification; repair scheduler overflow progress and spin-to-wait handoff.
- Isolate shared and owner-specific cache lines, expose actual queue overflow
  telemetry, and configure bounded metadata caches and hash tables at startup.

## Method and validation

The 48-case campaign covers two builds, IPv4/IPv6, one/four/128 connections,
and all four endpoint combinations. Each connection has 256 warmup and 2,000
measured exchanges. There is one paired observation per configuration, with
declared order and no retries, replacements or excluded cases.

The native stack has congestion control disabled; kernel TCP retains congestion
control. Both builds use the same application, direct private veth, MTU 1500,
disabled offloads, LL0, spin count 20,000, 64 KiB scheduler rings, ACK hold 25 ms,
reserve 4,096 and cold allocation/hash tuning. Each endpoint uses three slots:
Rx, Tx and XDP Tx; application work shares Rx/Tx. Client CPUs are 0/1/4 and
server CPUs are 2/3/6. The kernel configuration leaves XDP Tx idle.

The Linux x86-64 Clang build uses `-O3 -g -DNDEBUG`, without ASan/LSan or `-pg`.
The final scoped validation passes 311 Zutcp and 11 Zxdp unit cases, 117 native
integration cases, 36 independent Linux-peer cases, fresh passive smoke and
benchmark self-test. The campaign validates all 4,256,000 measured replies,
96 endpoint drains, 288 empty scheduler queues and 480 telemetry CSVs.
No hash table resized and no non-scheduler queue reported full.

The frozen 14-ELF sets differ only in `libZutcp`; the application and `libZxdp`
are byte-identical. Shared ACK state and owner boundaries passed compiled layout
inspection. Both concrete connection types grew by 128 bytes in the latest
combined change.

## Remaining work

This is an accepted development milestone, with further tail-latency and
scheduling work remaining. One observation per configuration does not establish
repeatability or complete the broader workload/capacity acceptance plan.

Both serial all-native candidate cases contain one sample above 1 ms. IPv6
serial p99.9 rises from 56.15 µs to 397.01 µs versus the retained build.
At 128 connections, IPv4 all-native p99.9 remains 3.0% above kernel; mixed
native-client p99 remains above kernel in both families.

Across the 24 native endpoints per build, scheduler function backing allocations
fall from 1,304,395 to 694,753 and fallback entries from 1,230,907 to 286,922.
Scheduler full encounters fall from 2,583 to 1,876. Tx callback bytes decrease,
but callback counts increase from 16,383,387 to 16,680,460. Traffic-envelope
voluntary context switches increase from 99,771 to 116,056 while user CPU time
decreases. These counters have different scopes: queue/heap totals cover the
lifecycle; traffic resource usage covers setup, warmup and measured exchanges,
excluding CSV/barrier/logout/drain. They are not syscall measurements.

## Evidence and reproduction

[The compact results record](results/2026-11-15-milestone.json) contains all 48
observations, outlier counts, paired ratios, conditions, source/ELF identities,
validation counts and the native endpoint totals. Raw samples, CSVs, logs and
the frozen private fixture remain in the local archive
`review_context/zutcp-stack-audit/timer-reservation` identified by the record.
Raw output and compiled binaries are not part of this source milestone.

Build the required dependency closure using the repository's scoped rebuild
instructions, then `make -C zfix/bench -j8 zfixbench` and
`zfix/bench/zfixbench --selftest`. The [managed benchmark documentation](../../zfix/bench/README.md)
describes the application and generic veth invocation. The measured campaign additionally uses
its archived three-read stable link-snapshot fixture. Generic smoke commands do
not reproduce that strict snapshot gate or reconstruct the retained baseline.
