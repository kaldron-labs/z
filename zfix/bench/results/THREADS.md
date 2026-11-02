# Fresh thread-owner comparison

All four client/server pairings were rerun sequentially in three native
thread configurations, with the same current wire format and release SDK:

| Configuration | Native client | Native acceptor |
| --- | --- | --- |
| Separate | Network Rx/Tx 1/2, application Rx/Tx 3/4 | Same |
| Acceptor shared | Network Rx/Tx 1/2, application Rx/Tx 3/4 | Application Rx/Tx use network Rx/Tx 1/2 |
| Both shared | Application Rx/Tx use network Rx/Tx 1/2 | Same |

Shared Rx and Tx remain separate threads. The worker count is four per
native peer with separate owners, and two with shared owners, across all
sessions. QuickFIX configuration and binaries are unchanged between matrices.

Clang release uses `-march=x86-64-v2 -O3 -g -DNDEBUG`, without ASan.
Only `zfix/util` was rebuilt, using `-j8`; dependencies were reused from
the verified release build. Each case has 10,000 measured orders and 1,000
warmup orders per session, 64 orders in flight per session, and three
repetitions. Both peers run in separate local processes over loopback TCP
or verified TLS. No concurrent builds or test suites run during timing.
The original source and build outputs were restored after the experiments.

All **144 cases passed**: 360 session runs, 3.6 million measured orders,
zero unfinished work, errors or timeouts. Source/executable/library hashes
were checked while each configuration was active; loaded files were
checked for ASan/LSan symbols. All other source hashes, loaded libraries,
compiler flags and SDK binaries match across the three configurations.

## Throughput

Rates below are thousands of orders/s, each cell **TCP / TLS**. Values
are medians across repetitions using the common measured session window,
not sums of independently timed session rates. Pair names are client-server;
`native` means zfix/zfixd.

| Sessions | Client-server | Separate | Acceptor shared | Both shared |
| ---: | --- | ---: | ---: | ---: |
| 1 | native-native | 69.86 / 63.35 | 54.31 / 53.56 | 57.26 / 61.60 |
| 1 | native-quickfix | 45.32 / 36.78 | 52.19 / 39.09 | 54.57 / 39.73 |
| 1 | quickfix-native | 51.64 / 33.54 | 58.46 / 38.25 | 62.62 / 38.39 |
| 1 | quickfix-quickfix | 54.74 / 36.09 | 58.48 / 37.18 | 54.62 / 35.89 |
| 4 | native-native | 81.62 / 72.66 | 57.01 / 55.42 | 64.74 / 62.90 |
| 4 | native-quickfix | 44.24 / 44.41 | 47.81 / 43.31 | 54.08 / 47.90 |
| 4 | quickfix-native | 54.98 / 42.37 | 62.84 / 50.84 | 59.71 / 50.01 |
| 4 | quickfix-quickfix | 62.98 / 49.41 | 62.23 / 49.80 | 61.71 / 46.45 |

## Acknowledgement tail latency

Milliseconds, **TCP / TLS**. Each value is the worst individual-session
p99 across repetitions. Quantiles are never pooled or averaged.

| Sessions | Client-server | Separate | Acceptor shared | Both shared |
| ---: | --- | ---: | ---: | ---: |
| 1 | native-native | 1.098 / 1.272 | 1.492 / 1.544 | 1.441 / 1.278 |
| 1 | native-quickfix | 1.644 / 2.078 | 1.592 / 2.066 | 1.551 / 2.022 |
| 1 | quickfix-native | 1.422 / 2.292 | 1.599 / 2.247 | 1.541 / 2.257 |
| 1 | quickfix-quickfix | 1.590 / 2.263 | 1.515 / 2.195 | 1.608 / 2.230 |
| 4 | native-native | 4.011 / 4.309 | 5.176 / 5.482 | 5.412 / 4.882 |
| 4 | native-quickfix | 6.532 / 7.174 | 6.456 / 7.180 | 6.325 / 6.958 |
| 4 | quickfix-native | 6.117 / 7.398 | 6.126 / 5.359 | 6.407 / 6.892 |
| 4 | quickfix-quickfix | 5.994 / 7.068 | 6.589 / 6.773 | 5.459 / 7.888 |

## Local CPU cost at four sessions

Median whole-process CPU seconds, **client + server**, including startup,
warmup and teardown; these are broader intervals than the measured phase.

| Client-server | Transport | Separate | Acceptor shared | Both shared |
| --- | --- | ---: | ---: | ---: |
| native-native | TCP | 1.574 + 1.612 | 1.872 + 1.364 | 1.178 + 1.180 |
| native-native | TLS | 1.772 + 1.825 | 1.963 + 1.335 | 1.137 + 1.226 |
| native-quickfix | TCP | 2.126 + 1.669 | 2.038 + 1.598 | 1.285 + 1.428 |
| native-quickfix | TLS | 2.242 + 1.786 | 2.236 + 1.768 | 1.298 + 1.645 |
| quickfix-native | TCP | 1.524 + 1.949 | 1.322 + 1.195 | 1.379 + 1.243 |
| quickfix-native | TLS | 1.866 + 1.995 | 1.624 + 1.246 | 1.649 + 1.231 |
| quickfix-quickfix | TCP | 1.337 + 1.309 | 1.353 + 1.326 | 1.339 + 1.306 |
| quickfix-quickfix | TLS | 1.637 + 1.428 | 1.657 + 1.455 | 1.772 + 1.559 |

These are sequential snapshots on the shared Intel Xeon Platinum 8488C
host, not randomized/interleaved trials. The repeated, unchanged
QuickFIX-to-QuickFIX rows show run-to-run variation. Small differences
should not be interpreted as a universal ranking. Full individual
quantiles, process resources, exact commands and hashes are retained.

## Evidence

[Raw archive](2026-10-31-threads.tar.xz): six complete timing matrices,
per-configuration reports, source snapshots, build logs, run script,
verification and checksums. Private test keys and executable backups
are excluded.

Archive SHA-256: `39b7fa114b3abe2e698f51b61c635d507045fd722bf0165f9de52f44a076608e`.
