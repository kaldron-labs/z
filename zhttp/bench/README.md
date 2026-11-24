# HTTP measurements

`zhttpbench MASK COUNT JOBS` runs fixed 64-byte responses on one loopback
connection. Masks select H1TCP (1), H1TLS (2), H2TLS (4), H3QUIC (8), shared
H1/H2 TLS (6), H2/H3 (12), or all profiles (15). Secure selections use H2 when
available and H3 when selected; H3 is forced for reproducible comparisons.
Listeners remain explicitly activated. All endpoints use the same temporary
certificate fixture as the message-hub tests.

The program warms ten waves of JOBS requests, measures COUNT requests in waves
of at most JOBS, then holds JOBS response routes before releasing them. It reports
request submission through logical completion latency (p50/p99), elapsed time,
framework heap allocation calls (including cache hits), received payload bytes,
and live framework heap bytes before/after a
connection and while responses are held. The main thread waits on semaphores;
Rx/Tx barriers retire the preceding wave before heap snapshots. Payload reception
consumes borrowed spans without staging.

The scheduler uses four threads, with I/O on 1/2 and endpoint Rx/Tx on 3/4. Pool
configuration fixes one link, JOBS concurrency and JOBS per-link operations.
Framework heap defaults are retained. `ZHTTP_BENCH_HEAPS=1` prints each heap's
size, allocation count and live objects at each snapshot for memory diagnosis;
leave it unset for timing. Run repeated baseline/current pairs with otherwise
idle build/test workers. Retain matching libraries and the exact compiler argv
for any baseline binary. The pre-erasure TLS/H1 backend permits only one held
request per physical link, so use JOBS=1 for that baseline comparison.

## H3 interop over AF_XDP

The `zhttpmatrix` case `zhttp-zhttpd/h3/j3n10000` sends 10,000 GETs at
concurrency three. On 2026-11-24, five interleaved release-build trials used
zhttp and zhttpd with Zutcp on a private one-queue veth pair. QUIC Rx used its
own thread and QUIC Tx shared the multiplexer Tx thread. Qlog was disabled.
The table shows median requests per second from client process elapsed time.

| Transport | Before | After |
| --- | ---: | ---: |
| AF_XDP | 17,061 | 18,108 |
| Kernel UDP control | 19,221 | 19,163 |

The AF_XDP baseline used 1,000 idle polls. The new configuration used the
4,096-poll default for both Rx and the sender at both endpoints. Reposting a
returned frame also restarted the Rx budget. The comparison measures the two
changes together. [AF_XDP configuration](../../zpkt/INTERFACE.md#af_xdp)
